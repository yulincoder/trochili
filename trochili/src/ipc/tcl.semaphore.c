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
#include "tcl.semaphore.h"

#if ((TCLC_IPC_ENABLE)&&(TCLC_IPC_SEMAPHORE_ENABLE))

static TState TryObtainSemaphore(TSemaphore* pSemaphore, TBool* pHiRP, TError* pErrno);
static TState ObtainSemaphore(TSemaphore* pSemaphore, TOption option, TTimeTick timeo, TReg32*
                              pIMask, TError* pErrno);
static TState TryReleaseSemaphore(TSemaphore* pSemaphore, TBool* pHiRP, TError* pErrno);
static TState ReleaseSemaphore(TSemaphore* pSemaphore, TOption option, TTimeTick timeo, TReg32*
                               pIMask, TError* pErrno);

/*************************************************************************************************
 *  功能: 尝试获得计数信号量                                                                     *
 *  参数: (1) pSemaphore 计数信号量结构地址                                                      *
 *        (2) pHiRP     是否因唤醒更高优先级而导致需要进行线程调度的标记                        *
 *        (3) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：当信号量可以申请的时候，如果在信号量的阻塞队列中存在线程，那么说明信号量的阻塞队列是   *
 *        Release队列,需要从信号量的阻塞队列中找到一个合适的线程，并且直接使得它释放信号量成功,  *
 *        同时保持信号量的计数不变                                                               *
 *************************************************************************************************/
static TState TryObtainSemaphore(TSemaphore* pSemaphore, TBool* pHiRP, TError* pErrno)
{
    TState state;
    TIpcContext* pContext = (TIpcContext*)0;

    if (pSemaphore->Value == 0U)
    {
        *pErrno = IPC_ERR_INVALID_VALUE;
        state = eFailure;
    }
    else if (pSemaphore->Value == pSemaphore->LimitedValue)
    {
        /* 尝试从信号量的阻塞队列中找到一个合适的线程并唤醒,保持信号量计数不变 */
        /* 如果被唤醒的线程的优先级高于当前线程优先级则设置线程调度请求标记 */
        if (pSemaphore->Property & IPC_PROP_PRIMQ_AVAIL)
        {
            pContext = (TIpcContext*)(pSemaphore->Queue.PrimaryHandle->Owner);
            uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);
        }
        else
        {
            /* 如果没有找到合适的线程，则信号量计数减1 */
            pSemaphore->Value--;
        }

        *pErrno = IPC_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        /* 信号量计数直接减1 */
        pSemaphore->Value--;

        *pErrno = IPC_ERR_NONE;
        state = eSuccess;
    }

    return state;
}


/*************************************************************************************************
 *  功能: 获得计数信号量                                                                         *
 *  参数: (1) pSemaphore 计数信号量结构地址                                                      *
 *        (2) option     访问信号量的的模式                                                      *
 *        (3) timeo      时限阻塞模式下访问信号量的时限长度                                      *
 *        (4) pIMask     中断屏蔽寄存器值                                                        *
 *        (5) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：当信号量可以申请的时候，如果在信号量的阻塞队列中存在线程，那么说明信号量的阻塞队列是   *
 *        Release队列,需要从信号量的阻塞队列中找到一个合适的线程，并且直接使得它释放信号量成功,  *
 *        同时保持信号量的计数不变                                                               *
 *************************************************************************************************/
static TState ObtainSemaphore(TSemaphore* pSemaphore, TOption option, TTimeTick timeo, TReg32*
                              pIMask, TError* pErrno)
{
    TBool HiRP = eFalse;
    TState state;
    TIpcContext* pContext;

    /* 首先尝试获得信号量
       会记录是否有优先级更高的线程就绪，但不记录具体是哪个线程 */
    state = TryObtainSemaphore(pSemaphore, &HiRP, pErrno);

    /* 如果在ISR环境下则直接返回。
    只有是线程环境下并且允许线程调度才可继续操作，
    否则即使之前唤醒了更高优先级的线程也不许进行调度。
    或者当当前线程获得信号量失败，也不能阻塞当前线程 */
    if ((uKernelVariable.State == eThreadState) &&
        (uKernelVariable.Schedulable == eTrue))
    {
        /* 如果当前线程能够得到信号量,直接从函数返回;
        如果当前线程不能得到信号量，但是采用的是简单返回方案，则函数也直接返回
        如果当前线程在得到信号量的同时唤醒了信号量阻塞队列中的线程，则需要尝试调度线程 */
        if (state == eSuccess)
        {
            if (HiRP == eTrue)
            {
                uThreadSchedule();
            }
        }
        else
        {
            /* 明确知道当前线程被阻塞
               如果当前线程不能得到信号量，并且采用的是等待方式，
               那么当前线程必须阻塞在信号量队列中 */
            if (option & IPC_OPT_WAIT)
            {
                /* 得到当前线程的IPC上下文结构地址 */
                pContext = &(uKernelVariable.CurrentThread->IpcContext);

                /* 设定线程正在等待的资源的信息 */
                uIpcSaveContext(pContext, (void*)pSemaphore, 0U, 0U, option | IPC_OPT_SEMAPHORE,
                                &state, pErrno);

                /* 当前线程阻塞在该信号量的阻塞队列 */
                uIpcBlockThread(pContext, &(pSemaphore->Queue), timeo);

                /* 当前线程无法获得信号量，申请线程调度 */
                uThreadSchedule();

                CpuLeaveCritical(*pIMask);
                /* 因为当前线程已经阻塞在IPC对象的线程阻塞队列，所以处理器需要执行别的线程。
                当处理器再次处理本线程时，从本处继续运行。*/
                CpuEnterCritical(pIMask);

                /* 清除线程挂起信息 */
                uIpcCleanContext(pContext);
            }
        }
    }

    return state;
}


/*************************************************************************************************
 *  功能: 尝试释放计数信号量                                                                     *
 *  参数: (1) pSemaphore 计数信号量结构地址                                                      *
 *        (2) pHiRP      是否因唤醒更高优先级而导致需要进行线程调度的标记                        *
 *        (3) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：当信号量可以释放的时候，如果在信号量的阻塞队列中存在线程，那么说明信号量的阻塞队列是   *
 *        Obtain队列,需要从信号量的阻塞队列中找到一个合适的线程，并且直接使得它成功获得信号量,   *
 *        同时保持信号量的计数不变                                                               *
 *************************************************************************************************/
static TState TryReleaseSemaphore(TSemaphore* pSemaphore, TBool* pHiRP, TError* pErrno)
{
    TState state;
    TIpcContext* pContext = (TIpcContext*)0;

    if (pSemaphore->Value == pSemaphore->LimitedValue)
    {
        *pErrno = IPC_ERR_INVALID_VALUE;
        state = eFailure;
    }
    else if (pSemaphore->Value == 0U)
    {
        /* 在信号量可以释放的时候，如果有线程处于阻塞队列，说明是信号量申请队列 */
        /* 如果被唤醒的线程的优先级高于当前线程优先级则设置线程调度请求标记 */
        if (pSemaphore->Property & IPC_PROP_PRIMQ_AVAIL)
        {
            pContext = (TIpcContext*)(pSemaphore->Queue.PrimaryHandle->Owner);
            uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);
        }
        else
        {
            pSemaphore->Value++;
        }

        *pErrno = IPC_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        pSemaphore->Value++;

        *pErrno = IPC_ERR_NONE;
        state = eSuccess;
    }

    return state;
}


/*************************************************************************************************
 *  功能: 释放信号量                                                                             *
 *  参数: (1) pSemaphore 计数信号量结构地址                                                      *
 *        (2) option     访问信号量的的模式                                                      *
 *        (3) timeo      时限阻塞模式下访问信号量的时限长度                                      *
 *        (4) pIMask     中断屏蔽寄存器值                                                        *
 *        (5) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState ReleaseSemaphore(TSemaphore* pSemaphore, TOption option, TTimeTick timeo,
                               TReg32* pIMask, TError* pErrno)
{
    TBool HiRP = eFalse;
    TState state;
    TIpcContext* pContext;

    /* 首先尝试释放信号量
       ★会记录是否有优先级更高的线程就绪，但不记录具体是哪个线程★ */
    state = TryReleaseSemaphore(pSemaphore, &HiRP, pErrno);

    /* 如果在ISR环境下则直接返回。
    只有是线程环境下并且允许线程调度才可继续操作，
    否则即使之前唤醒了更高优先级的线程也不许进行调度。
    或者当当前线程释放信号量失败，也不能阻塞当前线程 */
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
        else
        {
            /* ★明确知道当前线程被阻塞★
               如果当前线程不能释放信号量，并且采用的是等待方式，
               那么当前线程必须阻塞在信号量队列中 */
            if (option & IPC_OPT_WAIT)
            {
                /* 得到当前线程的IPC上下文结构地址 */
                pContext = &(uKernelVariable.CurrentThread->IpcContext);

                /* 设定线程正在等待的资源的信息 */
                uIpcSaveContext(pContext, (void*)pSemaphore, 0U, 0U, option | IPC_OPT_SEMAPHORE,
                                &state, pErrno);

                /* 当前线程阻塞在该信号的阻塞队列，时限或者无限等待，由timeo参数决定 */
                uIpcBlockThread(pContext, &(pSemaphore->Queue), timeo);

                /* 当前线程无法释放信号量，申请线程调度 */
                uThreadSchedule();

                CpuLeaveCritical(*pIMask);
                /* 因为当前线程已经阻塞在IPC对象的线程阻塞队列，所以处理器需要执行别的线程。
                当处理器再次处理本线程时，从本处继续运行。*/
                CpuEnterCritical(pIMask);

                /* 清除线程挂起信息 */
                uIpcCleanContext(pContext);
            }
        }
    }

    return state;
}


/*************************************************************************************************
 *  功能: 线程/ISR 获得信号量获得互斥信号量                                                      *
 *  参数: (1) pSemaphore 信号量结构地址                                                          *
 *        (2) option     访问信号量的的模式                                                      *
 *        (3) timeo      时限阻塞模式下访问信号量的时限长度                                      *
 *        (5) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xSemaphoreObtain(TSemaphore* pSemaphore, TOption option, TTimeTick timeo, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);
    if (pSemaphore->Property & IPC_PROP_READY)
    {
        /* 如果强制要求在ISR下获得信号量 */
        if (option & IPC_OPT_ISR)
        {
            /* 中断程序只能以非阻塞方式获得信号量，并且不考虑线程调度问题 */
            KNL_ASSERT((uKernelVariable.State == eIntrState),"");
            state = TryObtainSemaphore(pSemaphore, &HiRP, &error);
        }
        else
        {
            /* 自动判断如何获得信号量 */
            state = ObtainSemaphore(pSemaphore, option, timeo, &imask, &error);
        }
    }
    CpuLeaveCritical(imask);

    * pErrno = error;
    return state;
}


/*************************************************************************************************
 *  功能: 线程/ISR尝试释放信号量                                                                 *
 *  参数: (1) pSemaphore 信号量结构地址                                                          *
 *        (2) option     线程释放信号量的方式                                                    *
 *        (3) timeo      线程释放信号量的时限                                                    *
 *        (4) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xSemaphoreRelease(TSemaphore* pSemaphore, TOption option, TTimeTick timeo, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);
    if (pSemaphore->Property & IPC_PROP_READY)
    {
        /* 如果强制要求在ISR下释放信号量 */
        if (option & IPC_OPT_ISR)
        {
            /* 中断程序只能以非阻塞方式释放信号量，并且不考虑线程调度问题 */
            KNL_ASSERT((uKernelVariable.State == eIntrState),"");
            state = TryReleaseSemaphore(pSemaphore, &HiRP, &error);
        }
        else
        {
            /* 自动判断如何释放信号量 */
            state = ReleaseSemaphore(pSemaphore, option, timeo, &imask, &error);
        }
    }
    CpuLeaveCritical(imask);

    * pErrno = error;
    return state;
}


/*************************************************************************************************
 *  功能: 初始化计数信号量                                                                       *
 *  参数: (1) pSemaphore 计数信号量结构地址                                                      *
 *        (2) value      计数信号量初始值                                                        *
 *        (3) mvalue     计数信号量最大计数值                                                    *
 *        (4) property   信号量的初始属性                                                        *
 *        (5) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：信号量只使用基本IPC队列                                                                *
 *************************************************************************************************/
TState xSemaphoreCreate(TSemaphore* pSemaphore, TBase32 value, TBase32 mvalue,
                      TProperty property, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (!(pSemaphore->Property & IPC_PROP_READY))
    {
        property |= IPC_PROP_READY;
        pSemaphore->Property     = property;
        pSemaphore->Value        = value;
        pSemaphore->LimitedValue = mvalue;
        pSemaphore->InitialValue = value;
        pSemaphore->Queue.PrimaryHandle   = (TObjNode*)0;
        pSemaphore->Queue.AuxiliaryHandle = (TObjNode*)0;
        pSemaphore->Queue.Property        = &(pSemaphore->Property);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

    CpuLeaveCritical(imask);

    *pErrno = error;
    return state;
}


/*************************************************************************************************
 *  功能: 信号量取消初始化                                                                       *
 *  参数: (1) pSemaphore 信号量结构地址                                                          *
 *        (2) pError     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xSemaphoreDelete(TSemaphore* pSemaphore, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pSemaphore->Property & IPC_PROP_READY)
    {
        /* ★会记录是否有优先级更高的线程就绪，但不记录具体是哪个线程★ */
        /* 将信号量阻塞队列上的所有等待线程都释放，所有线程的等待结果都是TCLE_IPC_DELETE  */
        uIpcUnblockAll(&(pSemaphore->Queue), eFailure, IPC_ERR_DELETE, (void**)0, &HiRP);

        /* 清除信号量对象的全部数据 */
        memset(pSemaphore, 0U, sizeof(TSemaphore));

		/* 尝试发起线程抢占 */
		uThreadPreempt(HiRP);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

    CpuLeaveCritical(imask);

    *pErrno = error;
    return state;
}


/*************************************************************************************************
 *  功能: 重置计数信号量                                                                         *
 *  参数: (1) pSemaphore 信号量结构地址                                                          *
 *        (2) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xSemaphoreReset(TSemaphore* pSemaphore, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pSemaphore->Property & IPC_PROP_READY)
    {
        /* ★会记录是否有优先级更高的线程就绪，但不记录具体是哪个线程★ */
        /* 将信号量阻塞队列上的所有等待线程都释放，所有线程的等待结果都是TCLE_IPC_RESET */
        uIpcUnblockAll(&(pSemaphore->Queue), eFailure, IPC_ERR_RESET, (void**)0, &HiRP);

        /* 重置信号量计数和属性 */
        pSemaphore->Property &= IPC_RESET_SEMN_PROP;
        pSemaphore->Value = pSemaphore->InitialValue;

		/* 尝试发起线程抢占 */
		uThreadPreempt(HiRP);

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

    CpuLeaveCritical(imask);

    *pErrno = error;
    return state;
}


/*************************************************************************************************
 *  功能：信号量阻塞终止函数,将指定的线程从信号量的线程阻塞队列中终止阻塞并唤醒                  *
 *  参数：(1) pSemaphore 信号量结构地址                                                          *
 *        (2) option     参数选项                                                                *
 *        (3) pThread    线程地址                                                                *
 *        (4) pErrno     详细调用结果                                                            *
 *  返回: (1) eSuccess   操作成功                                                                *
 *        (2) eFailure   操作失败                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xSemaphoreFlush(TSemaphore* pSemaphore, TError* pErrno)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pSemaphore->Property & IPC_PROP_READY)
    {
        /* ★会记录是否有优先级更高的线程就绪，但不记录具体是哪个线程★ */
        /* 将信号量阻塞队列上的所有等待线程都释放，所有线程的等待结果都是TCLE_IPC_FLUSH  */
        uIpcUnblockAll(&(pSemaphore->Queue), eFailure, IPC_ERR_FLUSH, (void**)0, &HiRP);

		/* 尝试发起线程抢占 */
		uThreadPreempt(HiRP);

        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pErrno = error;
    return state;
}

#endif

