/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                       www.trochili.com                                        *
 *************************************************************************************************/
#include <string.h>

#include "tcl.types.h"
#include "tcl.config.h"
#include "tcl.cpu.h"
#include "tcl.thread.h"
#include "tcl.debug.h"
#include "tcl.kernel.h"
#include "tcl.ipc.h"
#include "tcl.mailbox.h"

#if ((TCLC_IPC_ENABLE)&&(TCLC_IPC_MAILBOX_ENABLE))

static TState TryReceiveMail(TMailBox* pMailbox, void** pMail2, TBool* pHiRP, TError* pError);
static TState TrySendMail(TMailBox* pMailbox, void** pMail2, TBool* pHiRP, TError* pError);
static TState ReceiveMail(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                          TReg32* pIMask, TError* pError);
static TState SendMail(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                       TReg32* pIMask, TError* pError);

/*************************************************************************************************
 *  功能: 尝试读取邮箱中的邮件                                                                   *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) pHiRP   是否在函数中唤醒过其它线程                                                 *
 *        (4) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：在邮箱可以读取的时候,如果有线程处于邮箱的阻塞队列,说明是当前邮箱的线程阻塞队列是邮件   *
 *        发送队列, 这时需要从邮箱的阻塞队列中找到一个合适的线程,直接使得发送邮件成功。并且要    *
 *        保持邮箱的状态不变                                                                     *
 *************************************************************************************************/
static TState TryReceiveMail(TMailBox* pMailbox, void** pMail2, TBool* pHiRP, TError* pError)
{
    TState state;
    TIpcContext* pContext = (TIpcContext*)0;

    /* 如果邮箱状态为空,则立刻返回,如果邮箱状态为满,则首先读取邮箱中的邮件 */
    if (pMailbox->Status == eMailBoxFull)
    {
        /* 从邮箱中读取邮件 */
        *pMail2 = pMailbox->Mail;

        /* 如果此时在邮箱的线程阻塞队列中有线程存在,即该线程等待发送邮件,
           则从中唤醒一个合适的线程。此时紧急邮件优先 */
        if (pMailbox->Property &IPC_PROP_AUXIQ_AVAIL)
        {
            pContext = (TIpcContext*)(pMailbox->Queue.AuxiliaryHandle->Owner);
        }
        else
        {
            if (pMailbox->Property &IPC_PROP_PRIMQ_AVAIL)
            {
                pContext = (TIpcContext*)(pMailbox->Queue.PrimaryHandle->Owner);
            }
        }

        /* 如果有线程被唤醒,则将该线程待发送的邮件保存到邮箱中, 保持邮箱状态不变 */
        if (pContext != (TIpcContext*)0)
        {
            uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);
            pMailbox->Mail = *((TMail*)(pContext->Data.Addr2));
        }
        /* 否则读空邮箱,设置邮件为空,设置邮箱状态为空 */
        else
        {
            pMailbox->Mail = (void*)0;
            pMailbox->Status = eMailBoxEmpty;
        }

        /* 设置线程成功读取邮箱标记 */
        *pError = IPC_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        *pError = IPC_ERR_INVALID_STATUS;
        state = eFailure;
    }

    return state;
}


/*************************************************************************************************
 *  功能: 尝试向邮箱发送邮件                                                                     *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) pHiRP    是否在函数中唤醒过其它线程                                                *
 *        (4) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState TrySendMail(TMailBox* pMailbox, void** pMail2, TBool* pHiRP, TError* pError)
{
    TState state;
    TIpcContext* pContext = (TIpcContext*)0;

    if (pMailbox->Status == eMailBoxEmpty)
    {
        /* 当邮箱为空的时候,如果有线程处于邮箱的阻塞队列,说明是当前邮箱的的线程阻塞队列是
        邮件读取队列, 这时需要从邮箱的阻塞队列中找到一个合适的线程,并直接使得它读取邮件成功。
        同时邮箱的状态不变 */
        if (pMailbox->Property &IPC_PROP_PRIMQ_AVAIL)
        {
            pContext = (TIpcContext*)(pMailbox->Queue.PrimaryHandle->Owner);
        }

        /* 如果找到了一个合适的线程,就将邮件发送给它 */
        if (pContext != (TIpcContext*)0)
        {
            uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);
            *(pContext->Data.Addr2) = * pMail2;
        }
        else
        {
            /* 否则将邮件写入邮箱,并置邮箱状态为满 */
            pMailbox->Mail = * pMail2;
            pMailbox->Status = eMailBoxFull;
        }

        /* 设置线程成功发送邮件标记 */
        *pError = IPC_ERR_NONE;
        state = eSuccess;

    }
    else
    {
        /* 邮箱内已经有邮件了，不能再放入其他邮件 */
        /* The mail could not placed in this mailbox because the mailbox alreday contains a mail */
        *pError = IPC_ERR_INVALID_STATUS;
        state = eFailure;
    }
    return state;
}


/*************************************************************************************************
 *  功能: 用于线程读取邮箱中的邮件                                                               *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) option   访问邮箱的模式                                                            *
 *        (4) timeo    时限阻塞模式下访问邮箱的时限长度                                          *
 *        (5) pIMask   中断屏蔽寄存器值                                                          *
 *        (6) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState ReceiveMail(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                          TReg32* pIMask, TError* pError)
{
    TState state;
    TBool HiRP = eFalse;
    TIpcContext* pContext;

    /* 尝试从邮箱读取邮件 */
    state = TryReceiveMail(pMailbox, (void**)pMail2, &HiRP, pError);

    /* 如果在ISR环境下调用本函数则直接返回;
       如果当前线程成功接收邮件,但此时禁止线程调度,即使唤醒了更高优先级的线程也从函数返回;
       如果当前线程成功接收邮件,并且此时没有禁止线程调度,则检查是否需要进行线程调度
       如果当前线程不能接收邮件,但此时禁止线程调度,则直接从函数返回;
       如果当前线程不能接收邮件,即使此时没有禁止线程调度,但是采用的是不阻塞方案,则直接返回;
       如果当前线程不能接收邮件,并且此时没有禁止线程调度,同时采用的是阻塞方案,才阻塞线程;

       综上各种情况：
       只有是线程环境下并且允许线程调度才可继续操作,
       否则即使之前唤醒了更高优先级的线程也不许进行调度。
       或者当前线程接收邮件失败,也不能阻塞当前线程 */
    if ((uKernelVariable.State == eThreadState) && (uKernelVariable.Schedulable == eTrue))
    {
        /* 如果当前线程唤醒了更高优先级的线程则进行调度。*/
        if (state == eSuccess)
        {
            if (HiRP == eTrue)
            {
                uThreadSchedule();
            }
        }
        /* 如果当前线程不能得到邮箱,同时采用的是等待方式,并且内核没有关闭线程调度,
           那么当前线程必须阻塞在邮箱队列中,并且强制线程调度 */
        else
        {
            if (option &IPC_OPT_WAIT)
            {
                /* 得到当前线程的IPC上下文结构地址 */
                pContext = &(uKernelVariable.CurrentThread->IpcContext);

                /* 保存线程挂起信息 */
                uIpcSaveContext(pContext, (void*)pMailbox, (TBase32)pMail2, sizeof(TBase32),
                                option | IPC_OPT_MAILBOX | IPC_OPT_READ_DATA,
                                &state, pError);

                /* 当前线程阻塞在该邮箱的阻塞队列,读取操作导致线程进入线程基本阻塞队列,
                   注意IPC线程挂起函数会设置线程状态 */
                uIpcBlockThread(pContext, &(pMailbox->Queue), timeo);

                /* 当前线程申请调度,其它线程即将得以执行 */
                uThreadSchedule();

                CpuLeaveCritical(*pIMask);
                /* 因为当前线程已经阻塞在IPC对象的线程阻塞队列,所以处理器需要执行别的线程。
                   当处理器再次处理本线程时,从本处继续运行。*/
                CpuEnterCritical(pIMask);

                /* 清除线程IPC阻塞信息 */
                uIpcCleanContext(pContext);
            }
        }
    }
    return state;
}


/*************************************************************************************************
 *  功能: 线程发送邮件                                                                           *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) option   访问邮箱的模式                                                            *
 *        (4) timeo    时限阻塞模式下访问邮箱的时限长度                                          *
 *        (5) pIMask   中断屏蔽寄存器值                                                          *
 *        (6) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState SendMail(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                       TReg32* pIMask, TError* pError)
{
    TState state = eError;
    TBool HiRP = eFalse;
    TIpcContext* pContext;

    /* HiRP在线程环境下才有意义 */
    state = TrySendMail(pMailbox, (void**)pMail2, &HiRP, pError);

    /* 如果在ISR环境下调用本函数则直接返回;
       如果当前线程成功发送邮件,但此时禁止线程调度,即使唤醒了更高优先级的线程也从函数返回;
       如果当前线程成功发送邮件,并且此时没有禁止线程调度,则检查是否需要进行线程调度
       如果当前线程不能发送邮件,但此时禁止线程调度,则直接从函数返回;
       如果当前线程不能发送邮件,即使此时没有禁止线程调度,但是采用的是不阻塞方案,则直接返回;
       如果当前线程不能发送邮件,并且此时没有禁止线程调度,同时采用的是阻塞方案,才阻塞线程;

       综上各种情况：
       只有是线程环境下并且允许线程调度才可继续操作,
       否则即使之前唤醒了更高优先级的线程也不许进行调度。
       或者当前线程发送邮件失败,也不能阻塞当前线程 */
    if ((uKernelVariable.State == eThreadState) &&
            (uKernelVariable.Schedulable == eTrue))
    {
        /* 如果当前线程唤醒了更高优先级的线程则进行调度。*/
        if (state == eSuccess)
        {
            if (HiRP == eTrue)
            {
                uThreadSchedule();
            }
        }
        /* 如果当前线程不能得到邮箱,并且采用的是等待方式,并且内核没有关闭线程调度,
           那么当前线程必须阻塞在邮箱队列中,并且强制线程调度 */
        else
        {
            if (option &IPC_OPT_WAIT)
            {
                /* 发送紧急程度不同的邮件的线程进入不同的阻塞队列 */
                if (option &IPC_OPT_UARGENT)
                {
                    option |= IPC_OPT_USE_AUXIQ;
                }

                /* 得到当前线程的IPC上下文结构地址 */
                pContext = &(uKernelVariable.CurrentThread->IpcContext);

                /* 保存线程挂起信息 */
                uIpcSaveContext(pContext, (void*)pMailbox, (TBase32)pMail2, sizeof(TBase32),
                                option | IPC_OPT_MAILBOX | IPC_OPT_WRITE_DATA, &state, pError);

                /* 当前线程阻塞在该邮箱的阻塞队列 */
                uIpcBlockThread(pContext, &(pMailbox->Queue), timeo);

                /* 当前线程申请调度,其它线程即将得以执行 */
                uThreadSchedule();

                CpuLeaveCritical(*pIMask);
                /* 因为当前线程已经阻塞在IPC对象的线程阻塞队列,所以处理器需要执行别的线程。
                   当处理器再次处理本线程时,从本处继续运行。*/
                CpuEnterCritical(pIMask);

                /* 清除线程IPC阻塞信息 */
                uIpcCleanContext(pContext);
            }
        }
    }
    return state;
}


/*************************************************************************************************
 *  功能: 线程/ISR从邮箱中读取邮件                                                               *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) option   访问邮箱的模式                                                            *
 *        (4) timeo    时限阻塞模式下访问邮箱的时限长度                                          *
 *        (5) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xMailBoxReceive(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                       TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);
    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 如果强制要求在ISR下读取邮件 */
        if (option &IPC_OPT_ISR)
        {
            /* 中断程序只能以非阻塞方式从邮箱中读取邮件,并且暂时不考虑线程调度问题。
               在中断中,当前线程未必是最高就绪优先级线程,也未必处于内核就绪线程队列。
               所以在此处得到的HiRP标记无任何意义。*/
            KNL_ASSERT((uKernelVariable.State == eIntrState), "");
            state = TryReceiveMail(pMailbox, (void**)pMail2, &HiRP, &error);
        }
        else
        {
            /* 自动判断如何读取邮件 */
            state = ReceiveMail(pMailbox, pMail2, option, timeo, &imask, &error);
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}



/*************************************************************************************************
 *  功能: 线程/ISR发送邮件                                                                       *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pMail2   保存邮件结构地址的指针变量                                                *
 *        (3) option   访问邮箱的模式                                                            *
 *        (4) timeo    时限阻塞模式下访问邮箱的时限长度                                          *
 *        (5) pErrno   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xMailBoxSend(TMailBox* pMailbox, TMail* pMail2, TOption option, TTimeTick timeo,
                    TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);
    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 如果强制要求在ISR下释放邮件 */
        if (option &IPC_OPT_ISR)
        {
            /* 中断程序只能以非阻塞方式从邮箱中读取邮件,并且暂时不考虑线程调度问题
               在中断中,当前线程未必是最高就绪优先级线程,也未必处于内核就绪线程队列。
               所以在此处得到的HiRP标记无任何意义。*/
            KNL_ASSERT((uKernelVariable.State == eIntrState), "");
            state = TrySendMail(pMailbox, (void**)pMail2, &HiRP, &error);
        }
        else
        {
            /* 自动判断如何发送邮件 */
            state = SendMail(pMailbox, pMail2, option, timeo, &imask, &error);
        }
    }
    CpuLeaveCritical(imask);

    *pErrno = error;
    return state;
}

/*************************************************************************************************
 *  功能: 清除邮箱阻塞队列                                                                       *
 *  参数: (1) pMailbox  邮箱结构地址                                                             *
 *        (2) pError    详细调用结果                                                             *
 *  返回: (1) eFailure  操作失败                                                                 *
 *        (2) eSuccess  操作成功                                                                 *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xMailboxReset(TMailBox* pMailbox, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);
    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 将阻塞队列上的所有等待线程都释放,所有线程的等待结果都是IPC_ERR_RESET */
        uIpcUnblockAll(&(pMailbox->Queue), eFailure, IPC_ERR_RESET, (void**)0, &HiRP);

        /* 设置邮箱的状态为空,清空邮箱中的邮件 */
        pMailbox->Property &= IPC_RESET_MBOX_PROP;
        pMailbox->Status = eMailBoxEmpty;
        pMailbox->Mail = (TMail*)0;

        /* 尝试发起线程抢占 */
        uThreadPreempt(HiRP);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}



/*************************************************************************************************
 *  功能：邮箱阻塞终止函数,将指定的线程从邮箱的读阻塞队列中终止阻塞并唤醒                        *
 *  参数: (1) pMailbox 邮箱结构地址                                                              *
 *        (2) pThread  线程结构地址                                                              *
 *        (3) option   参数选项                                                                  *
 *        (4) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明?                                                                                        *
 *************************************************************************************************/
TState xMailBoxFlush(TMailBox* pMailbox, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 将邮箱阻塞队列上的所有等待线程都释放，所有线程的等待结果都是TCLE_IPC_FLUSH  */
        uIpcUnblockAll(&(pMailbox->Queue), eFailure, IPC_ERR_FLUSH, (void**)0, &HiRP);

        /* 尝试发起线程抢占 */
        uThreadPreempt(HiRP);

        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：邮箱广播函数,向所有读阻塞队列中的线程广播邮件                                          *
 *  参数: (1) pMailbox  邮箱结构地址                                                             *
 *        (2) pMail2    保存邮件结构地址的指针变量                                               *
 *        (3) pError    详细调用结果                                                             *
 *  返回: (1) eFailure  操作失败                                                                 *
 *        (2) eSuccess  操作成功                                                                 *
 *  说明：只有邮箱的线程阻塞队列中存在读邮箱的线程的时候,才能把邮件发送给队列中的线程            *
 *************************************************************************************************/
TState xMailBoxBroadcast(TMailBox* pMailbox, TMail* pMail2, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 只有邮箱空并且有线程等待读取邮件的时候才能进行广播 */
        if (pMailbox->Status == eMailBoxEmpty)
        {
            uIpcUnblockAll(&(pMailbox->Queue), eSuccess, IPC_ERR_NONE, (void**)pMail2, &HiRP);

            /* 尝试发起线程抢占 */
            uThreadPreempt(HiRP);

            error = IPC_ERR_NONE;
            state = eSuccess;
        }
        else
        {
            error = IPC_ERR_INVALID_STATUS;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：初始化邮箱                                                                             *
 *  参数：(1) pMailbox   邮箱的地址                                                              *
 *        (2) property   邮箱的初始属性                                                          *
 *        (3) pError     详细调用结果                                                            *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xMailBoxCreate(TMailBox* pMailbox, TProperty property, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (!(pMailbox->Property &IPC_PROP_READY))
    {
        property |= IPC_PROP_READY;
        pMailbox->Property = property;
        pMailbox->Status = eMailBoxEmpty;
        pMailbox->Mail = (void*)0;

        pMailbox->Queue.PrimaryHandle   = (TObjNode*)0;
        pMailbox->Queue.AuxiliaryHandle = (TObjNode*)0;
        pMailbox->Queue.Property        = &(pMailbox->Property);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：重置邮箱                                                                               *
 *  参数：(1) pMailbox   邮箱的地址                                                              *
 *        (2) pError     详细调用结果                                                            *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：注意线程的等待结果都是IPC_ERR_DELETE                                                   *
 *************************************************************************************************/
TState xMailBoxDelete(TMailBox* pMailbox, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pMailbox->Property &IPC_PROP_READY)
    {
        /* 将邮箱阻塞队列上的所有等待线程都释放,所有线程的等待结果都是IPC_ERR_DELETE  */
        uIpcUnblockAll(&(pMailbox->Queue), eFailure, IPC_ERR_DELETE, (void**)0, &HiRP);

        /* 清除邮箱对象的全部数据 */
        memset(pMailbox, 0U, sizeof(TMailBox));

        /* 尝试发起线程抢占 */
        uThreadPreempt(HiRP);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


#endif

