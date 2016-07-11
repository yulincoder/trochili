/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                       www.trochili.com                                        *
 *************************************************************************************************/
#include <string.h>

#include "tcl.types.h"
#include "tcl.config.h"
#include "tcl.object.h"
#include "tcl.cpu.h"
#include "tcl.ipc.h"
#include "tcl.debug.h"
#include "tcl.kernel.h"
#include "tcl.timer.h"
#include "tcl.thread.h"

/* 内核进就绪队列定义,处于就绪和运行的线程都放在这个队列里 */
static TThreadQueue ThreadReadyQueue;

/* 内核线程辅助队列定义，处于延时、挂起、休眠的线程都放在这个队列里 */
static TThreadQueue ThreadAuxiliaryQueue;


#if (TCLC_THREAD_STACK_CHECK_ENABLE)
/*************************************************************************************************
 *  功能：告警和检查线程栈溢出问题                                                               *
 *  参数：(1) pThread  线程地址                                                                  *
 *  返回：无                                                                                     *
 *  说明：                                                                                       *
 *************************************************************************************************/
static void CheckThreadStack(TThread* pThread)
{
    if ((pThread->StackTop < pThread->StackBarrier) || (*(TBase32*)(pThread->StackBarrier) !=
            TCLC_THREAD_STACK_BARRIER_VALUE))
    {
        uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
        pThread->Diagnosis |= THREAD_DIAG_STACK_OVERFLOW;
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    if (pThread->StackTop < pThread->StackAlarm)
    {
        pThread->Diagnosis |= THREAD_DIAG_STACK_ALARM;
    }
}

#endif


/*************************************************************************************************
 *  功能：线程运行监理函数，线程的运行都以它为基础                                               *
 *  参数：(1) pThread  线程地址                                                                  *
 *  返回：无                                                                                     *
 *  说明：这里处理线程的非法退出问题                                                             *
 *  说明：函数名的前缀'x'(eXtreme)表示本函数需要处理临界区代码                                   *
 *************************************************************************************************/
static void xSuperviseThread(TThread* pThread)
{
    TState state;
    TError error;
    TReg32 imask;

    /* 调用用户ASR线程主函数，这类函数的特点是解挂后一次性执行，然后立刻挂起，等待下次调用。
    虽然Irq和Timer守护线程也是中断服务线程，但不是做为ASR在此处理。
    这两个函数自己处理挂起问题 */
    if ((pThread->Property &THREAD_PROP_RUNASR))
    {
        while (eTrue)
        {
            /* 执行线程函数 */
            pThread->Entry(pThread->Argument);

            /* 线程运行结束后直接挂起,等待再次被执行 */
            CpuEnterCritical(&imask);
            state = uThreadSetUnready(pThread, eThreadSuspended, 0U, &error);
            if (state == eFailure)
            {
                uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
                pThread->Diagnosis |= THREAD_DIAG_INVALID_STATE;
                uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
            }
            CpuLeaveCritical(imask);
        }
    }
    /* 处理RUN TO COMPLETION 线程退出问题, 防止退出后处理器执行到非法(未知)指令 */
    else if (pThread->Property &THREAD_PROP_RUN2COMPLETION)
    {
        /* 执行线程函数 */
        pThread->Entry(pThread->Argument);

        /* 线程运行结束后直接挂起,等待后继处理 */
        CpuEnterCritical(&imask);
        state = uThreadSetUnready(pThread, eThreadDormant, 0U, &error);
        if (state == eFailure)
        {
            uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
            pThread->Diagnosis |= THREAD_DIAG_INVALID_STATE;
            uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
        }
        CpuLeaveCritical(imask);
    }
    else
    {
        /* 执行用户线程函数 */
        pThread->Entry(pThread->Argument);

        /* 防止RUNFOREVER线程不小心退出导致非法指令等死机的问题 */
        uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
        pThread->Diagnosis |= THREAD_DIAG_INVALID_EXIT;
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }
}


/*************************************************************************************************
 *  功能：计算就绪线程队列中的最高优先级函数                                                     *
 *  参数：无                                                                                     *
 *  返回：HiRP (Highest Ready Priority)                                                          *
 *  说明：                                                                                       *
 *************************************************************************************************/
void uThreadCalcHiRP(TPriority* priority)
{
    /* 如果就绪优先级不存在则说明内核发生致命错误 */
    if (ThreadReadyQueue.PriorityMask == (TBitMask)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }
    *priority = CpuCalcHiPRIO(ThreadReadyQueue.PriorityMask);
}

/*************************************************************************************************
 *  功能：本函数会发起线程抢占                                                                   *
 *  参数：(1) HiRP 是否已经得到比当前线程优先级高的线程                                          *
 *  返回：无                                                                                     *
 *  说明：                                                                                       *
 *************************************************************************************************/
void uThreadPreempt(TBool HiRP)
{
    /* 在线程环境下，如果当前线程的优先级已经不再是线程就绪队列的最高优先级，
    并且内核此时并没有关闭线程调度，那么就需要进行一次线程抢占 */
    if ((uKernelVariable.State == eThreadState) &&
            (uKernelVariable.Schedulable == eTrue) &&
            (HiRP == eTrue))
    {
        uThreadSchedule();
    }
}


/*************************************************************************************************
 *  功能：初始化内核线程管理模块                                                                 *
 *  参数：无                                                                                     *
 *  返回：无                                                                                     *
 *  说明：内核中的线程队列主要有一下几种：                                                       *
 *        (1) 线程就绪队列,用于存储所有就绪线程(包括运行的线程)。内核中只有一个就绪队列          *
 *        (2) 线程辅助队列, 所有初始化状态、延时状态和休眠状态的线程都存储在这个队列中。         *
 *            同样内核中只有一个休眠队列                                                         *
 *        (3) IPC对象的线程阻塞队列                                                              *
 *************************************************************************************************/
void uThreadModuleInit(void)
{
    /* 检查内核是否处于初始状态 */
    if (uKernelVariable.State != eOriginState)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    memset(&ThreadReadyQueue, 0, sizeof(ThreadReadyQueue));
    memset(&ThreadAuxiliaryQueue, 0, sizeof(ThreadAuxiliaryQueue));

    uKernelVariable.ThreadReadyQueue = &ThreadReadyQueue;
    uKernelVariable.ThreadAuxiliaryQueue = &ThreadAuxiliaryQueue;
}

/*
1 当前线程离开就绪队列后，再次加入就绪队列时，一定放在相应的队列尾部，并重新计算时间片。
2 当前线程在就绪队列内部调整优先级时，在新的队列里也一定要在队列头。
 */

/*************************************************************************************************
 *  功能：将线程加入到指定的线程队列中                                                           *
 *  参数：(1) pQueue  线程队列地址地址                                                           *
 *        (2) pThread 线程结构地址                                                               *
 *        (3) pos     线程在线程队列中的位置                                                     *
 *  返回：无                                                                                     *
 *  说明：                                                                                       *
 *************************************************************************************************/
void uThreadEnterQueue(TThreadQueue* pQueue, TThread* pThread, TQueuePos pos)
{
    TPriority priority;
    TObjNode** pHandle;

    /* 检查线程和线程队列 */
    KNL_ASSERT((pThread != (TThread*)0), "");
    KNL_ASSERT((pThread->Queue == (TThreadQueue*)0), "");

    /* 根据线程优先级得出线程实际所属分队列 */
    priority = pThread->Priority;
    pHandle = &(pQueue->Handle[priority]);

    /* 将线程加入指定的分队列 */
    uObjQueueAddFifoNode(pHandle, &(pThread->ObjNode), pos);

    /* 设置线程所属队列 */
    pThread->Queue = pQueue;

    /* 设定该线程优先级为就绪优先级 */
    pQueue->PriorityMask |= (0x1 << priority);
}


/*************************************************************************************************
 *  功能：将线程从指定的线程队列中移出                                                           *
 *  参数：(1) pQueue  线程队列地址地址                                                           *
 *        (2) pThread 线程结构地址                                                               *
 *  返回：无                                                                                     *
 *  说明：FIFO PRIO两种访问资源的方式                                                            *
 *************************************************************************************************/
void uThreadLeaveQueue(TThreadQueue* pQueue, TThread* pThread)
{
    TPriority priority;
    TObjNode** pHandle;

    /* 检查线程是否属于本队列,如果不属于则内核发生致命错误 */
    KNL_ASSERT((pThread != (TThread*)0), "");
    KNL_ASSERT((pQueue == pThread->Queue), "");

    /* 根据线程优先级得出线程实际所属分队列 */
    priority = pThread->Priority;
    pHandle = &(pQueue->Handle[priority]);

    /* 将线程从指定的分队列中取出 */
    uObjQueueRemoveNode(pHandle, &(pThread->ObjNode));

    /* 设置线程所属队列 */
    pThread->Queue = (TThreadQueue*)0;

    /* 处理线程离开队列后对队列优先级就绪标记的影响 */
    if (pQueue->Handle[priority] == (TObjNode*)0)
    {
        /* 设定该线程优先级未就绪 */
        pQueue->PriorityMask &= (~(0x1 << priority));
    }
}


/*************************************************************************************************
 *  功能：线程时间片处理函数，在时间片中断处理ISR中会调用本函数                                  *
 *  参数：无                                                                                     *
 *  返回：无                                                                                     *
 *  说明：本函数完成了当前线程的时间片处理，但并没有选择需要调度的后继线程和进行线程切换         *
 *************************************************************************************************/
/* 当前线程可能处于3种位置
1 就绪队列的头位置(任何优先级)
2 就绪队列的其它位置(任何优先级)
3 辅助队列里
只有情况1才需要进行时间片轮转的处理，但此时不涉及线程切换,因为本函数只在ISR中调用。*/

/* 本函数要求在应用代码里的中断优先级最高 */
void uThreadTickISR(void)
{
    TThread* pThread;
    TObjNode* pHandle;
    TPriority priority;

    /* 将当前线程时间片减去1个节拍数,线程运行总节拍数加1 */
    pThread = uKernelVariable.CurrentThread;
    pThread->Ticks--;
    pThread->Jiffies++;

    /* 如果本轮时间片运行完毕 */
    if (pThread->Ticks == 0U)
    {
        /* 恢复线程的时钟节拍数 */
        pThread->Ticks = pThread->BaseTicks;

        /* 如果内核此时允许线程调度 */
        if (uKernelVariable.Schedulable == eTrue)
        {
            /* 判断线程是不是处于内核就绪线程队列的某个优先级的队列头 */
            pHandle = ThreadReadyQueue.Handle[pThread->Priority];
            if ((TThread*)(pHandle->Owner) == pThread)
            {
                priority = pThread->Priority;
                /* 发起时间片调度，之后pThread处于线程队列尾部,
                当前线程所在线程队列也可能只有当前线程唯一1个线程 */
                ThreadReadyQueue.Handle[priority] = (ThreadReadyQueue.Handle[priority])->Next;

                /* 将线程状态置为就绪,准备线程切换 */
                pThread->Status = eThreadReady;
            }
        }
    }
}


/*************************************************************************************************
 *  功能：用于请求线程调度                                                                       *
 *  参数：无                                                                                     *
 *  返回：无                                                                                     *
 *  说明：线程的调度请求可能被ISR最终取消                                                        *
 *************************************************************************************************/
/*
1 当前线程离开队列即代表它放弃本轮运行，再次进入队列时,时间片需要重新计算,
  在队列中的位置也规定一定是在队尾
2 导致当前线程不是最高就绪优先级的原因有
  1 别的优先级更高的线程进入就绪队列
  2 当前线程自己离开队列
  3 别的线程的优先级被提高
  4 当前线程的优先级被拉低
  5 当前线程Yiled
  6 时间片中断中，当前线程被轮转

3 在cortex处理器上, 有这样一种可能:
当前线程释放了处理器，但在PendSV中断得到响应之前，又有其它高优先级中断发生，
在高级isr中又把当前线程置为就绪，
1 并且当前线程仍然是最高就绪优先级，
2 并且当前线程仍然在最高就绪线程队列的队列头。
此时需要考虑取消PENDSV的操作，避免当前线程和自己切换 */
void uThreadSchedule(void)
{
    TPriority priority;

    /* 如果就绪优先级不存在则说明内核发生致命错误 */
    if (ThreadReadyQueue.PriorityMask == (TBitMask)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    /* 查找最高就绪优先级，获得后继线程，如果后继线程指针为空则说明内核发生致命错误 */
    uThreadCalcHiRP(&priority);
    uKernelVariable.NomineeThread = (TThread*)((ThreadReadyQueue.Handle[priority])->Owner);
    if (uKernelVariable.NomineeThread == (TThread*)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    /* 完成线程的优先级抢占或者时间片轮转;
    或者完成线程调度(我们在这里区分"抢占"和"调度"的含义) */
    if (uKernelVariable.NomineeThread != uKernelVariable.CurrentThread)
    {
#if (TCLC_THREAD_STACK_CHECK_ENABLE)
        CheckThreadStack(uKernelVariable.NomineeThread);
#endif
        uKernelVariable.NomineeThread->Status = eThreadRunning;
        if (uKernelVariable.CurrentThread->Status == eThreadRunning)
        {
            uKernelVariable.CurrentThread->Status = eThreadReady;
        }
        CpuConfirmThreadSwitch();
    }
    else
    {
        CpuCancelThreadSwitch();
        uKernelVariable.CurrentThread->Status = eThreadRunning;
    }
}


/*************************************************************************************************
 *  功能：线程结构初始化函数                                                                     *
 *  参数：(1)  pThread  线程结构地址                                                             *
 *        (2)  status   线程的初始状态                                                           *
 *        (3)  property 线程属性                                                                 *
 *        (4)  acapi    对线程管理API的许可控制                                                  *
 *        (5)  pEntry   线程函数地址                                                             *
 *        (6)  TArgument线程函数参数                                                             *
 *        (7)  pStack   线程栈地址                                                               *
 *        (8)  bytes    线程栈大小，以字为单位                                                   *
 *        (9)  priority 线程优先级                                                               *
 *        (10) ticks    线程时间片长度                                                           *
 *  返回：(1)  eFailure                                                                          *
 *        (2)  eSuccess                                                                          *
 *  说明：注意栈起始地址、栈大小和栈告警地址的字节对齐问题                                       *
 *  说明：函数名的前缀'u'(Universal)表示本函数为模块间通用函数                                   *
 *************************************************************************************************/
void uThreadCreate(TThread* pThread, TThreadStatus status, TProperty property, TBitMask acapi,
                   TThreadEntry pEntry, TArgument argument, void* pStack, TBase32 bytes,
                   TPriority priority, TTimeTick ticks)
{
    TThreadQueue* pQueue;

    /* 设置线程栈相关数据和构造线程初始栈栈帧 */
    KNL_ASSERT((bytes >= TCLC_CPU_MINIMAL_STACK), "");

    /* 栈大小向下4byte对齐 */
    bytes &= (~((TBase32)0x3));
    pThread->StackBase = (TBase32)pStack + bytes;

    /* 清空线程栈空间 */
    if (property &THREAD_PROP_CLEAN_STACK)
    {
        memset(pStack, 0U, bytes);
    }

    /* 构造(伪造)线程初始栈帧,这里将线程结构地址作为参数传给线程监管函数 */
    CpuBuildThreadStack(&(pThread->StackTop), pStack, bytes, (void*)(&xSuperviseThread),
                        (TArgument)pThread);

    /* 计算线程栈告警地址 */
#if (TCLC_THREAD_STACK_CHECK_ENABLE)
    pThread->StackAlarm = (TBase32)pStack + bytes - (bytes* TCLC_THREAD_STACK_ALARM_RATIO) / 100;
    pThread->StackBarrier = (TBase32)pStack;
    (*(TAddr32*)pStack) = TCLC_THREAD_STACK_BARRIER_VALUE;
#endif

    /* 设置线程时间片相关参数 */
    pThread->Ticks = ticks;
    pThread->BaseTicks = ticks;
    pThread->Jiffies = 0U;

    /* 设置线程优先级 */
    pThread->Priority = priority;
    pThread->BasePriority = priority;

    /* 设置线程唯一ID数值 */
    pThread->ThreadID = uKernelVariable.ObjID;
    uKernelVariable.ObjID++;

    /* 设置线程入口函数和线程参数 */
    pThread->Entry = pEntry;
    pThread->Argument = argument;

    /* 设置线程所属队列信息 */
    pThread->Queue = (TThreadQueue*)0;

    /* 设置线程定时器信息 */
#if (TCLC_TIMER_ENABLE)
    uTimerCreate(&(pThread->Timer), (TProperty)0, eThreadTimer, TCLM_MAX_VALUE64,
                 (TTimerRoutine)0, (TArgument)0, (void*)pThread);
#endif

    /* 清空线程IPC阻塞上下文 */
#if (TCLC_IPC_ENABLE)
    uIpcInitContext(&(pThread->IpcContext), (void*)pThread);
#endif

    /* 清除线程占有的锁(MUTEX)队列 */
#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_MUTEX_ENABLE))
    pThread->LockList = (TObjNode*)0;
#endif

    /* 初始线程运行诊断信息 */
    pThread->Diagnosis = THREAD_DIAG_NORMAL;

    /* 设置线程能够支持的线程管理API */
    pThread->ACAPI = acapi;

    /* 设置线程链表节点信息，线程此时不属于任何线程队列 */
    pThread->ObjNode.Owner = (void*)pThread;
    pThread->ObjNode.Data = (TBase32*)(&(pThread->Priority));
    pThread->ObjNode.Prev = (TObjNode*)0;
    pThread->ObjNode.Next = (TObjNode*)0;
    pThread->ObjNode.Handle = (TObjNode**)0;

    /* 将线程加入内核线程队列，设置线程状态 */
    pQueue = (status == eThreadReady) ? (&ThreadReadyQueue): (&ThreadAuxiliaryQueue);
    uThreadEnterQueue(pQueue, pThread, eQuePosTail);
    pThread->Status = status;

    /* 标记线程已经完成初始化 */
    property |= THREAD_PROP_READY;
    pThread->Property = property;
}


/*************************************************************************************************
 *  功能：线程注销                                                                               *
 *  参数：(1) pThread 线程结构地址                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：初始化线程和定时器线程不能被注销                                                       *
 *************************************************************************************************/
TState uThreadDelete(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;

    if (pThread->Status == eThreadDormant)
    {
#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_MUTEX_ENABLE))
        if (pThread->LockList)
        {
            error = THREAD_ERR_FAULT;
            state = eFailure;
        }
        else
#endif
        {
            uThreadLeaveQueue(pThread->Queue, pThread);
#if (TCLC_TIMER_ENABLE)
            uTimerDelete(&(pThread->Timer));
#endif
            /* 如果当前线程在ISR里先被deactivate,然后被deinit，那么在退出isr时，会发生线程切换，
            还会有一次针对当前线程的栈操作，所以这里最好不要对被deinit的线程结构做内存清零操作，
            除非在汇编代码里对此问题进行处理，对deinit后的当前线程不做上下文入栈操作 */
            memset(pThread, 0, sizeof(pThread));
            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
    }
    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：更改线程优先级                                                                         *
 *  参数：(1) pThread  线程结构地址                                                              *
 *        (2) priority 线程优先级                                                                *
 *        (3) flag     是否被SetPriority API调用                                                 *
 *        (4) pError   保存操作结果                                                              *
 *  返回：(1) eFailure 更改线程优先级失败                                                        *
 *        (2) eSuccess 更改线程优先级成功                                                        *
 *  说明：如果是临时修改优先级，则不修改线程结构的基本优先级                                     *
 *************************************************************************************************/
TState uThreadSetPriority(TThread* pThread, TPriority priority, TBool flag, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_PRIORITY;
    TPriority temp;

    /* 本函数只会被线程调用 */
    KNL_ASSERT((uKernelVariable.State == eThreadState), "");

    if (pThread->Priority != priority)
    {
        if (pThread->Status == eThreadBlocked)
        {
            uIpcSetPriority(&(pThread->IpcContext), priority);
            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        /* 就绪线程调整优先级时，可以直接调整其在就绪线程队列中的分队列
        对于处于就绪线程队列中的当前线程，如果修改它的优先级，
        因为不会把它移出线程就绪队列，所以即使内核不允许调度也没问题 */
        else if (pThread->Status == eThreadReady)
        {
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            pThread->Priority = priority;
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosTail);

            if ((flag == eTrue) && (uKernelVariable.Schedulable == eTrue))
            {
                /* 得到当前就绪队列的最高就绪优先级，因为就绪线程(包括当前线程)
                在线程就绪队列内的折腾会导致当前线程可能不是最高优先级。 */
                if (priority < uKernelVariable.CurrentThread->Priority)
                {
                    uThreadSchedule();
                }
            }

            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        else if (pThread->Status == eThreadRunning)
        {
            /* 假设当前线程优先级最高且唯一，假如调低它的优先级之后仍然是最高，
            但是在新的优先级里有多个就绪线程，那么最好把当前线程放在新的就绪队列
            的头部，这样不会引起隐式的时间片轮转；当前线程先后被多次调整优先级时，只有
            每次都把它放在队列头才能保证它最后一次调整优先级后还处在队列头。 */
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            pThread->Priority = priority;
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosHead);

            if ((flag == eTrue) && (uKernelVariable.Schedulable == eTrue))
            {
                /* 因为当前线程在线程就绪队列内的折腾会导致当前线程可能不是最高优先级，
                所以需要重新计算当前就绪队列的最高就绪优先级。*/
                uThreadCalcHiRP(&temp);
                if (temp < uKernelVariable.CurrentThread->Priority)
                {
                    pThread->Status = eThreadReady;
                    uThreadSchedule();
                }
            }

            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        else
        {
            /*其它状态的线程都在辅助队列里，可以直接修改优先级*/
            pThread->Priority = priority;
            state = eSuccess;
            error = THREAD_ERR_NONE;
        }

        /* 如果需要则修改线程固定优先级 */
        if (flag == eTrue)
        {
            pThread->BasePriority = priority;
        }
    }

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：将线程从指定的状态转换到就绪态，使得线程能够参与内核调度                               *
 *  参数：(1) pThread   线程结构地址                                                             *
 *        (2) status    线程当前状态，用于检查                                                   *
 *        (3) pError    保存操作结果                                                             *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：函数名的前缀'u'(communal)表示本函数是全局函数                                          *
 *************************************************************************************************/
/* 中断或者线程都有可能调用本函数 */
TState uThreadSetReady(TThread* pThread, TThreadStatus status, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;
    TBool HiRP = eFalse;

    /* 线程状态校验,只有状态符合的线程才能被操作 */
    if (pThread->Status == status)
    {
        /* 操作线程，完成线程队列和状态转换,注意只有中断处理时，
        当前线程才会处在内核线程辅助队列里(因为还没来得及线程切换) */
        uThreadLeaveQueue(&ThreadAuxiliaryQueue, pThread);
        uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosTail);

        /* 当线程离开就绪队列时，已经放弃它的本轮执行，哪怕时间片并未耗尽。
        当线程再次进入就绪队列时，需要恢复线程的时钟节拍数，重新计算其分享处理器时间的能力 */
        pThread->Ticks = pThread->BaseTicks;
        pThread->Status = eThreadReady;
        state = eSuccess;
        error = THREAD_ERR_NONE;

        /* 如果是在线程环境下，那么此时pThread一定不是当前线程；
        如果是在中断环境下，那么
        第一，当前线程不一定在就绪队列里，不能作为任务调度的比较标准
        第二，中断在最后一级返回时统一做线程调度检查，这里得到的HiPR无意义 */
        if (pThread->Priority < uKernelVariable.CurrentThread->Priority)
        {
            HiRP = eTrue;
        }

        /* 尝试发起线程抢占 */
        uThreadPreempt(HiRP);

#if (TCLC_TIMER_ENABLE)
        /* 如果是取消定时操作则需要停止线程定时器 */
        if ((state == eSuccess) && (status == eThreadDelayed))
        {
            uTimerStop(&(pThread->Timer));
        }
#endif
    }

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程挂起函数                                                                           *
 *  参数：(1) pThread   线程结构地址                                                             *
 *        (2) status    线程当前状态，用于检查                                                   *
 *        (3) ticks     线程延时时间                                                             *
 *        (4) pError    保存操作结果                                                             *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState uThreadSetUnready(TThread* pThread, TThreadStatus status, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;

    /* ISR不会调用本函数 */
    KNL_ASSERT((uKernelVariable.State != eIntrState), "");

    /* 如果操作的是当前线程，则需要首先检查内核是否允许调度 */
    if (pThread->Status == eThreadRunning)
    {
        /* 如果内核此时禁止线程调度，那么当前线程不能被操作 */
        if (uKernelVariable.Schedulable == eTrue)
        {
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            uThreadEnterQueue(&ThreadAuxiliaryQueue, pThread, eQuePosTail);
            pThread->Status = status;

            /* 线程环境马上开始线程调度 */
            uThreadSchedule();

            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
        else
        {
            error = THREAD_ERR_FAULT;
        }
    }
    else if (pThread->Status == eThreadReady)
    {
        /* 如果被操作的线程不是当前线程，则不会引起线程调度，所以直接处理线程和队列 */
        uThreadLeaveQueue(&ThreadReadyQueue, pThread);
        uThreadEnterQueue(&ThreadAuxiliaryQueue, pThread, eQuePosTail);
        pThread->Status = status;

        error = THREAD_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        error = error;
    }

#if (TCLC_TIMER_ENABLE)
    if ((state == eSuccess) && (status == eThreadDelayed))
    {
        /* 重置并启动线程定时器 */
        uTimerConfig(&(pThread->Timer), eThreadTimer, ticks);
        uTimerStart(&(pThread->Timer), 0U);
    }
#endif

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程结构初始化函数                                                                     *
 *  参数：(1)  pThread  线程结构地址                                                             *
 *        (2)  status   线程的初始状态                                                           *
 *        (3)  property 线程属性                                                                 *
 *        (4)  acapi    对线程管理API的许可控制                                                  *
 *        (5)  pEntry   线程函数地址                                                             *
 *        (6)  pArg     线程函数参数                                                             *
 *        (7)  pStack   线程栈地址                                                               *
 *        (8)  bytes    线程栈大小，以字为单位                                                   *
 *        (9)  priority 线程优先级                                                               *
 *        (10) ticks    线程时间片长度                                                           *
 *        (11) pError   详细调用结果                                                             *
 *  返回：(1)  eFailure                                                                          *
 *        (2)  eSuccess                                                                          *
 *  说明：函数名的前缀'x'(eXtreme)表示本函数需要处理临界区代码                                   *
 *************************************************************************************************/
TState xThreadCreate(TThread* pThread, TThreadStatus status, TProperty property, TBitMask acapi,
                     TThreadEntry pEntry, TArgument argument, void* pStack, TBase32 bytes,
                     TPriority priority, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 检查线程是否已经被初始化 */
        if (!(pThread->Property &THREAD_PROP_READY))
        {
            uThreadCreate(pThread, status, property, acapi, pEntry, argument, pStack, bytes,
                          priority, ticks);
            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程注销                                                                               *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：IDLE线程、中断处理线程和定时器线程不能被注销                                           *
 *************************************************************************************************/
TState xThreadDelete(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_DEINIT)
            {
                state = uThreadDelete(pThread, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：更改线程优先级                                                                         *
 *  参数：(1) pThread  线程结构地址                                                              *
 *        (2) priority 线程优先级                                                                *
 *        (3) pError   详细调用结果                                                              *
 *  返回：(1) eFailure 更改线程优先级失败                                                        *
 *        (2) eSuccess 更改线程优先级成功                                                        *
 *  说明：(1) 如果是临时修改优先级，则不修改线程结构的基本优先级数据                             *
 *        (2) 互斥量实施优先级继承协议的时候不受AUTHORITY控制                                    *
 *************************************************************************************************/
TState xThreadSetPriority(TThread* pThread, TPriority priority, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_SET_PRIORITY)
            {
                if ((!(pThread->Property & THREAD_PROP_PRIORITY_FIXED)) &&
                        (pThread->Property & THREAD_PROP_PRIORITY_SAFE))
                {
                    state = uThreadSetPriority(pThread, priority, eTrue, &error);
                }
                else
                {
                    error = THREAD_ERR_FAULT;
                    state = eFailure;
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}



/*************************************************************************************************
 *  功能：修改线程时间片长度                                                                     *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) slice   线程时间片长度                                                             *
 *        (3) pError  详细调用结果                                                               *
 *  返回：(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xThreadSetTimeSlice(TThread* pThread, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_SET_SLICE)
            {
                /* 调整线程时间片长度 */
                if (pThread->BaseTicks > ticks)
                {
                    pThread->Ticks = (pThread->Ticks < ticks) ? (pThread->Ticks): ticks;
                }
                else
                {
                    pThread->Ticks += (ticks - pThread->BaseTicks);
                }
                pThread->BaseTicks = ticks;

                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}



/*************************************************************************************************
 *  功能：线程级线程调度函数，当前线程主动让出处理器(保持就绪状态)                               *
 *  参数：(1) pError    详细调用结果                                                             *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：因为不能破坏最高就绪优先级占用处理器的原则，                                           *
 *        所以Yield操作只能在拥有最高就绪优先级的线程之间操作                                    *
 *************************************************************************************************/
TState xThreadYield(TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;
    TPriority priority;
    TThread* pThread;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    /* 只能在线程环境下同时内核允许线程调度的条件下才能调用本函数 */
    if ((uKernelVariable.State == eThreadState) && (uKernelVariable.Schedulable == eTrue))
    {
        /* 操作目标是当前线程 */
        pThread = uKernelVariable.CurrentThread;
        priority = pThread->Priority;

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_YIELD)
            {
                /* 调整当前线程所在队列的头指针
                当前线程所在线程队列也可能只有当前线程唯一1个线程 */
                ThreadReadyQueue.Handle[priority] = (ThreadReadyQueue.Handle[priority])->Next;
                pThread->Status = eThreadReady;

                uThreadSchedule();
                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：激活线程，使得线程能够参与内核调度                                                     *
 *  参数：(1) pThread  线程结构地址                                                              *
 *        (2) pError   详细调用结果                                                              *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xThreadActivate(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_ACTIVATE)
            {
                state = uThreadSetReady(pThread, eThreadDormant, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程终止，使得线程不再参与内核调度                                                     *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：(1) 初始化线程和定时器线程不能被休眠                                                   *
 *************************************************************************************************/
TState xThreadDeactivate(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_DEACTIVATE)
            {
                state = uThreadSetUnready(pThread, eThreadDormant, 0U, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程挂起函数                                                                           *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：(1) 内核初始化线程不能被挂起                                                           *
 *************************************************************************************************/
TState xThreadSuspend(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_SUSPEND)
            {
                state = uThreadSetUnready(pThread, eThreadSuspended, 0U, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程解挂函数                                                                           *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xThreadResume(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_RESUME)
            {
                state = uThreadSetReady(pThread, eThreadSuspended, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}

/*************************************************************************************************
 *  功能：线程解挂函数                                                                           *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xThreadUnblock(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 检查线程是否已经被初始化 */
    if (pThread->Property &THREAD_PROP_READY)
    {
        /* 检查线程是否接收相关API调用 */
        if (pThread->ACAPI &THREAD_ACAPI_UNBLOCK)
        {
            if (pThread->Status == eThreadBlocked)
            {
                /* 将阻塞队列上的指定阻塞线程释放 */
                uIpcUnblockThread(&(pThread->IpcContext), eFailure, IPC_ERR_ABORT, &HiRP);

                /* 尝试发起线程抢占 */
                uThreadPreempt(HiRP);

                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_STATUS;
            }
        }
        else
        {
            error = THREAD_ERR_ACAPI;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}

#if (TCLC_TIMER_ENABLE)
/*************************************************************************************************
 *  功能：线程延时模块接口函数                                                                   *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) ticks   需要延时的滴答数目                                                         *
 *        (3) pError  详细调用结果                                                               *
 *  返回：(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  说明：                                                                                       *
 *************************************************************************************************/
TState xThreadDelay(TThread* pThread, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 如果没有给出被操作的线程地址，则强制使用当前线程 */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_DELAY)
            {
                state = uThreadSetUnready(pThread, eThreadDelayed, ticks, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  功能：线程延时取消函数                                                                       *
 *  参数：(1) pThread 线程结构地址                                                               *
 *        (2) pError  详细调用结果                                                               *
 *  返回：(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  说明：(1) 这个函数对以时限等待方式阻塞在IPC线程阻塞队列上的线程无效                          *
 *************************************************************************************************/
TState xThreadUndelay(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* 只允许在非中断代码调用本函数 */
    if (uKernelVariable.State != eIntrState)
    {
        /* 检查线程是否已经被初始化 */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* 检查线程是否接收相关API调用 */
            if (pThread->ACAPI &THREAD_ACAPI_UNDELAY)
            {
                state = uThreadSetReady(pThread, eThreadDelayed, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}

#endif
