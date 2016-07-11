/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                       www.trochili.com                                        *
 *************************************************************************************************/
#include <string.h>

#include "tcl.types.h"
#include "tcl.config.h"
#include "tcl.cpu.h"
#include "tcl.debug.h"
#include "tcl.thread.h"
#include "tcl.kernel.h"
#include "tcl.ipc.h"
#include "tcl.flags.h"

#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_FLAGS_ENABLE))

static TState TryReceiveFlags(TFlags* pFlags, TBitMask* pPattern, TOption option, TError* pError);
static TState ReceiveFlags(TFlags* pFlags, TBitMask* pPattern, TOption option, TTimeTick timeo,
                           TReg32* pIMask, TError* pError);
static TState TrySendFlags(TFlags* pFlags, TBitMask pattern, TBool* pHiRP,  TError* pError);
static TState SendFlags(TFlags* pFlags, TBitMask pattern, TError* pError);

/*************************************************************************************************
 *  功能：尝试接收事件标记                                                                       *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要接收的标记的组合                                                      *
 *        (3) option   访问事件标记的参数                                                        *
 *        (4) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState TryReceiveFlags(TFlags* pFlags, TBitMask* pPattern, TOption option, TError* pError)
{
    TState state;
    TBitMask match;
    TBitMask pattern;

    pattern = *pPattern;
    match = (pFlags->Value) & pattern;

    if (((option & IPC_OPT_AND) && (match == pattern)) ||
            ((option & IPC_OPT_OR) && (match != 0U)))
    {
        if (option & IPC_OPT_CONSUME)
        {
            pFlags->Value &= (~match);
        }

        *pPattern = match;

        *pError = IPC_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        *pError = IPC_ERR_FLAGS;
        state = eFailure;
    }
    return state;
}


/*************************************************************************************************
 *  功能：尝试发送事件标记                                                                       *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要发送的标记的组合                                                      *
 *        (3) pHiRP    是否在函数中唤醒过其它线程                                                *
 *        (4) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState TrySendFlags(TFlags* pFlags, TBitMask pattern, TBool* pHiRP, TError* pError)
{
    TObjNode* pHead = (TObjNode*)0;
    TObjNode* pTail = (TObjNode*)0;
    TObjNode* pCurrent = (TObjNode*)0;
    TBitMask mask = 0U;
    TBool match = eFalse;
    TOption option;
    TBitMask* pTemp;
    TState state;
    TIpcContext* pContext;

    /* 检查事件是否需要发送 */
    mask = pFlags->Value | pattern;
    if (mask != pFlags->Value)
    {
        *pError = IPC_ERR_NONE;
        state = eSuccess;

        /* 把事件发送到事件标记中 */
        pFlags->Value |= pattern;

        /* 事件标记是否有线程在等待事件的发生 */
        if (pFlags->Queue.PrimaryHandle != (TObjNode*)0)
        {
            /* 开始遍历事件组阻塞队列 */
            pHead = pFlags->Queue.PrimaryHandle;
            pTail = pFlags->Queue.PrimaryHandle->Prev;
            do
            {
                pCurrent = pHead;
                pHead = pHead->Next;
                match = eFalse;

                /* 获得等待事件标记的线程和相关的事件节点 */
                pContext =  (TIpcContext*)(pCurrent->Owner);
                option = pContext->Option;
                pTemp = (TBitMask*)(pContext->Data.Addr1);

                /*  得到满足要求的事件标记 */
                mask = pFlags->Value & (*pTemp);
                if (((option & IPC_OPT_AND) && (mask == *pTemp)) ||
                        ((option & IPC_OPT_OR) && (mask != 0U)))
                {
                    match = eTrue;
                    *pTemp = mask;
                    uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);
                }

                /* 如果事件标记检查成功 */
                if ((match == eTrue) && (option & IPC_OPT_CONSUME))
                {
                    /* 消耗某些事件，如果事件全部被消耗殆尽，则退出 */
                    pFlags->Value &= (~mask);
                    if (pFlags->Value == 0U)
                    {
                        break;
                    }
                }
            }
            while(pCurrent != pTail);
        }
    }
    else
    {
        *pError = IPC_ERR_FLAGS ;
        state = eError;
    }

    return state;
}


/*************************************************************************************************
 *  功能：线程接收事件标记                                                                       *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要接收的标记的组合                                                      *
 *        (3) timeo    时限阻塞模式下访问事件标记的时限长度                                      *
 *        (4) option   访问事件标记的参数                                                        *
 *        (5) pIMask   中断屏蔽寄存器值                                                          *
 *        (6) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
static TState ReceiveFlags(TFlags* pFlags, TBitMask* pPattern, TOption option, TTimeTick timeo,
                           TReg32* pIMask, TError* pError)
{
    TState state = eFailure;
    TIpcContext* pContext;

    /* 尝试从事件标记中获得事件 */
    state = TryReceiveFlags(pFlags, pPattern, option, pError);

    /* 因为事件标记线程队列中不会存在事件发送队列，所以不需要判断是否有新线程要调度，
    但是要处理是否需要将事件消耗的问题 */
    if ((uKernelVariable.State == eThreadState) &&
            (uKernelVariable.Schedulable == eTrue))
    {
        /* 如果当前线程不能得到事件，并且采用的是等待方式，
        那么当前线程必须阻塞在事件标记的等待队列中，并且强制线程调度 */
        if (state == eFailure)
        {
            if (option & IPC_OPT_WAIT)
            {
                /* 得到当前线程的IPC上下文结构地址 */
                pContext = &(uKernelVariable.CurrentThread->IpcContext);

                /* 保存线程挂起信息 */
                uIpcSaveContext(pContext, (void*)pFlags, (TBase32)pPattern, sizeof(TBase32), 
                                option | IPC_OPT_FLAGS, &state, pError);

                /* 当前线程阻塞在该邮箱的阻塞队列,读取操作导致线程进入线程基本阻塞队列，
                注意IPC线程挂起函数会设置线程状态 */
                uIpcBlockThread(pContext, &(pFlags->Queue), timeo);

                /* 当前线程申请调度，其它线程即将得以执行 */
                uThreadSchedule();

                CpuLeaveCritical(*pIMask);
                /* 此时此处发生一次调度，当前线程已经阻塞在IPC对象的阻塞队列。处理器开始执行别的线程；
                当处理器再次处理本线程时，从本处继续运行。*/
                CpuEnterCritical(pIMask);

                /* 清除线程IPC阻塞信息 */
                uIpcCleanContext(pContext);
            }
        }
    }

    return state;
}


/*************************************************************************************************
 *  功能：线程或者ISR向事件标记发送事件                                                          *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要接收的标记的组合                                                      *
 *        (3) pError   详细调用结果                                                              *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：本函数不会引起当前线程阻塞                                                             *
 *************************************************************************************************/
static TState SendFlags(TFlags* pFlags, TBitMask pattern, TError* pError)
{
    TState state = eFailure;
    TBool HiRP = eFalse;

    state = TrySendFlags(pFlags, pattern, &HiRP, pError);

    /*  如果在ISR环境下则直接返回。
        只有是线程环境下并且允许线程调度才可继续操作 */
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
    }

    return state;
}


/*************************************************************************************************
 *  功能：线程/ISR接收事件标记                                                                   *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要接收的标记的组合                                                      *
 *        (3) timeo    时限阻塞模式下访问事件标记的时限长度                                      *
 *        (4) option   访问事件标记的参数                                                        *
 *        (5) pError   详细调用结果                                                              *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xFlagsReceive(TFlags* pFlags, TBitMask* pPattern, TOption option, TTimeTick timeo,
                     TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* 如果强制要求在ISR下读取事件 */
        if (option & IPC_OPT_ISR)
        {
            /* 中断程序只能以非阻塞方式从事件标记中接收事件，并且不考虑线程调度问题  */
            /* 在中断isr中，当前线程未必是最高就绪优先级线程，也未必处于内核就绪线程队列。
            所以在isr中调用TryReceiveFlags()后得到的HiRP标记无任何意义。*/
            KNL_ASSERT((uKernelVariable.State == eIntrState),"");
            state = TryReceiveFlags(pFlags, pPattern, option, &error);
        }
        else
        {
            /* 自动判断如何接收事件 */
            state = ReceiveFlags(pFlags, pPattern, option, timeo, &imask, &error);
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程/ISR向事件标记发送事件                                                             *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pPattern 需要接收的标记的组合                                                      *
 *        (3) pError   详细调用结果                                                              *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：本函数不会引起当前线程阻塞,所以不区分是线程还是ISR来调用                               *
 *************************************************************************************************/
TState xFlagsSend(TFlags* pFlags, TBitMask pattern, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        state = SendFlags(pFlags, pattern, &error);
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能: 清除事件标记阻塞队列                                                                   *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pError   详细调用结果                                                              *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xFlagsReset(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* 将阻塞队列上的所有等待线程都释放，所有线程的等待结果都是IPC_ERR_RESET */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_RESET, (void**)0, &HiRP);

        pFlags->Property &= IPC_RESET_FLAG_PROP;
        pFlags->Value = 0U;

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
 *  功能：事件标记终止函数,将指定的线程从事件标记的线程阻塞队列中终止阻塞并唤醒                  *
 *  参数：(1) pFlags   事件标记结构地址                                                          *
 *        (2) option   参数选项                                                                  *
 *        (3) pThread  线程地址                                                                  *
 *        (4) pError   详细调用结果                                                              *
 *  返回：(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xFlagsFlush(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* 将事件标记阻塞队列上的所有等待线程都释放，所有线程的等待结果都是TCLE_IPC_FLUSH  */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_FLUSH, (void**)0, &HiRP);

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
 *  功能：初始化事件标记                                                                         *
 *  参数：(1) pFlags     事件标记的地址                                                          *
 *        (2) property   事件标记的初始属性                                                      *
 *        (3) pError     函数调用详细返回值                                                      *
 *  返回: (1) eFailure   操作失败                                                                *
 *        (2) eSuccess   操作成功                                                                *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xFlagsCreate(TFlags* pFlags, TProperty property, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (!(pFlags->Property & IPC_PROP_READY))
    {
        property |= IPC_PROP_READY;
        pFlags->Property = property;
        pFlags->Value = 0U;

        pFlags->Queue.PrimaryHandle   = (TObjNode*)0;
        pFlags->Queue.AuxiliaryHandle = (TObjNode*)0;
        pFlags->Queue.Property        = &(pFlags->Property);

        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：取消事件标记初始化                                                                     *
 *  参数：(1) pFlags   事件标记的地址                                                            *
 *        (2) pError   函数调用详细返回值                                                        *
 *  返回: (1) eFailure 操作失败                                                                  *
 *        (2) eSuccess 操作成功                                                                  *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xFlagsDelete(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* 将阻塞队列上的所有等待线程都释放，所有线程的等待结果都是IPC_ERR_DELETE  */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_DELETE, (void**)0, &HiRP);

        /* 清除事件标记对象的全部数据 */
        memset(pFlags, 0U, sizeof(TFlags));

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
 *  功能：事件标记查询                                                                           *
 *  参数：(1) pFlags 事件标记结构地址                                                            *
 *        (2) pData  复制保存事件标记数据地址                                                    *
 *        (3) pError 函数调用详细返回值                                                          *
 *  返回：无                                                                                     *
 *  说明：                                                                                       *
 *************************************************************************************************/
void xFlagsQuery(TFlags* pFlags, TFlags* pData, TError* pError)
{
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        memcpy(pData, pFlags, sizeof(TFlags));
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
}

#endif

