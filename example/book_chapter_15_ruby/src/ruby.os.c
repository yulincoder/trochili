#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"
#include "ruby.os.h"

#if 0 //memory
static TMemBuddy SysMemoryBuddy;
static char SysMemoryPool[MEMORY_PAGE_SIZE* MEMORY_PAGES];


void OS_SetupMemory(void)
{
    TState state;
    TError error;

    state = TclInitMemBuddy(&SysMemoryBuddy, SysMemoryPool, MEMORY_PAGES,
                            MEMORY_PAGE_SIZE, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");
}


void* OS_MallocMemory(int len)
{
    TState state;
    TError error;
    void* pMsg = 0;

    state = TclMallocBuddyMem(&SysMemoryBuddy, len, &pMsg, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");
    return pMsg;
}


void OS_FreeMemory(void* pMsg)
{
    TState state;
    TError error;
    state = TclFreeBuddyMem(&SysMemoryBuddy, pMsg, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");
}
#else
  TMemPool SysMemoryPool;
static char SysMemoryData[32*256];
void OS_SetupMemory(void)
{
    TState state;
    TError error;

    state = TclInitMemoryPool(&SysMemoryPool, SysMemoryData, 32, 256,
                              &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");
}


void* OS_MallocMemory(int len)
{
    TState state;
    TError error;
	void* pMsg;
    state = TclMallocPoolMemory(&SysMemoryPool, &pMsg, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");

	return pMsg;
}


void OS_FreeMemory(void* pMsg)
{
    TState state;
    TError error;
    state = TclFreePoolMemory(&SysMemoryPool, pMsg, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_MEMORY_NONE), "");
}
#endif

extern void OS_Error(char* str)
{

}

static void* IOUartReqMsgPool[IO_MQ_LEN];
static void* IOUartCnfMsgPool[IO_MQ_LEN];
static void* IOUsbReqMsgPool[IO_MQ_LEN];
static void* IOUsbCnfMsgPool[IO_MQ_LEN];
static void* AIMsgPool[AI_MQ_LEN];

static TThread AllThreads[NUM_OF_THREADS];
static TMsgQueue AllQueues[NUM_OF_QUEUES];
static TFlags AllEventGroups[NUM_OF_EVENT_GROUPS];
static TTimer ALLTimer[NUM_OF_TIMERS];

void OS_SendMessage(TIndex QId, void** pMsg)
{
    TState state;
    TError error;
    TMsgQueue* pQueue;

    pQueue = &AllQueues[QId];
    state = TclSendMessage(pQueue, pMsg, 0, 0, &error);

    if (state != eSuccess)
    {
        OS_Error("err OS_SendMessage\r\n");
    }
    else
    {
        OS_Error("OK OS_SendMessage\r\n");
    }
}

void OS_GetMessage(TIndex QId, void** pMsg)
{
    TState state;
    TMsgQueue* pQueue;
    TError error;

    pQueue = &AllQueues[QId];
    state = TclReceiveMessage(pQueue, pMsg, 0, 0, &error);
    if (state != eSuccess)
    {
        *pMsg = (void*)0;
    }
}


void OS_PendMessage(TIndex QId, void** pMsg)
{
    TState state;
    TMsgQueue* pQueue;
    TError error;

    pQueue = &AllQueues[QId];
    state = TclReceiveMessage(pQueue, pMsg, TCLO_IPC_WAIT, 0, &error);
    if (state != eSuccess)
    {
        *pMsg = (void*)0;
    }
}

void OS_SetEvent(UINT16 flagID, UINT32 EvtBit)
{
    TState state;
    TError error;
    TFlags* flag;

    flag = &AllEventGroups[flagID];
    state = TclSendFlags(flag, EvtBit, &error);
    if (state != eSuccess)
    {
        OS_Error("err OS_SetEvent\r\n");
    }
    else
    {
        OS_Error("OK OS_SetEvent\r\n");
    }
}

void OS_WaitEvent(UINT16 fid, UINT32* pattern)
{
    TState state;
    TError error;
    TFlags* flag;
    TOption option;

    flag = &AllEventGroups[fid];

    option = TCLO_IPC_WAIT | TCLO_IPC_OR | TCLO_IPC_CONSUME;
    state = TclReceiveFlags(flag, pattern, option, 0, &error);
    if (state != eSuccess)
    {
        OS_Error("err OS_WaitEvent\r\n");
    }
    else
    {
        OS_Error("OK OS_WaitEvent\r\n");
    }
}

void OS_SetupFlags(void)
{
    TState state;
    TError error;
    TFlags* flag;

    /* 初始化IO事件标记 */
    flag = &AllEventGroups[IO_EVENT_GROUP_ID];
    state = TclInitFlags(flag, TCLP_IPC_DUMMY, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IPC_NONE), "");

    /* 初始化AI事件标记 */
    flag = &AllEventGroups[AI_EVENT_GROUP_ID];
    state = TclInitFlags(flag, TCLP_IPC_DUMMY, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IPC_NONE), "");
}


void OS_SetupQueues(void)
{
    TState state;
    TError error;
    TMsgQueue* queue;

    /* 初始化IO事件标记 */
    queue = &AllQueues[IO_UART_REQ_QUEUE_ID];
    state = TclInitMsgQueue(queue, (void**)(&IOUartReqMsgPool), IO_MQ_LEN,
                            TCLP_IPC_DUMMY,  &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IPC_NONE), "");

    queue = &AllQueues[IO_UART_CNF_QUEUE_ID];
    state = TclInitMsgQueue(queue, (void**)(&IOUartCnfMsgPool), IO_MQ_LEN,
                            TCLP_IPC_DUMMY,  &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IPC_NONE), "");

       /* 初始化AI事件标记 */
    queue = &AllQueues[AI_QUEUE_ID];
    state = TclInitMsgQueue(queue, (void**)(&AIMsgPool), AI_MQ_LEN,
                            TCLP_IPC_DUMMY,  &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IPC_NONE), "");
}


static void TestTimerFunc(TArgument data)
{
}


extern void WallTimerFunc(TArgument data);
void OS_SetupTimers(void)
{
    TState state;
    TError error;
    TTimer* pTimer;

    pTimer = &ALLTimer[UI_WALL_TIMER_ID];

    /* 初始化墙上时间每秒更新一次 */
    state = TclInitTimer(pTimer,
                             TCLP_TIMER_PERIODIC,
                             TCLM_MLS2TICKS(100),
                             &WallTimerFunc,
                             (TArgument)0,
                             &error);
    if (state != eSuccess)
    {
        OS_Error("err OS_WaitEvent\r\n");
    }
    else
    {
        OS_Error("OK OS_WaitEvent\r\n");
    }

}


extern TBitMask IO_DataUartISR(TArgument data);
extern TBitMask IO_KeyISR(UINT32 data);
void OS_SetupISR(void)
{
    TState state;
    TError error;

    /* 设置和串口相关的外部中断向量 */
    state = TclSetIrqVector(UART_IRQ_ID, &IO_DataUartISR, (TArgument)0, (TThread*)0, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IRQ_NONE), "");

    /* 设置和KEY相关的外部中断向量 */
    state = TclSetIrqVector(KEY_IRQ_ID, &IO_KeyISR, (TArgument)0, (TThread*)0, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_IRQ_NONE), "");
}

void OS_StartTimer(UINT16 tid, UINT32 lag)
{
    TState state;
    TError error;
    TTimer* pTimer;

    pTimer = &ALLTimer[tid];
    state = TclStartTimer(pTimer, TCLM_MLS2TICKS(lag), &error);
    if (state != eSuccess)
    {
        OS_Error("err OS_WaitEvent\r\n");
    }
    else
    {
        OS_Error("OK OS_WaitEvent\r\n");
    }
}



/* IO 线程栈定义 */
static TWord ThreadIOStack[THREAD_IO_STACK_BYTES / 4];

/* CTRL线程栈定义 */
static TWord ThreadAIStack[THREAD_AI_STACK_BYTES / 4];


/*  */
void OS_SetupThreads(void)
{
    TState state;
    TError error;
    TThread* pThread;

    /* 初始化IO线程 */
    pThread = &AllThreads[IO_THREAD_ID];
    state = TclInitThread(pThread, &IO_ThreadEntry, (TArgument)0,
                          ThreadIOStack, THREAD_IO_STACK_BYTES,
                          THREAD_IO_PRIORITY, THREAD_IO_SLICE, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_THREAD_NONE), "");

    /* 激活IO线程 */
    state = TclActivateThread(pThread, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_THREAD_NONE), "");

    /* 初始化AI线程 */
    pThread = &AllThreads[AI_THREAD_ID];
    state = TclInitThread(pThread, &AI_ThreadEntry, (TArgument)0,
                          ThreadAIStack, THREAD_AI_STACK_BYTES,
                          THREAD_AI_PRIORITY, THREAD_AI_SLICE, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_THREAD_NONE), "");


    /* 激活AI线程 */
    state = TclActivateThread(pThread, &error);
    TCLM_ASSERT((state == eSuccess), "");
    TCLM_ASSERT((error == TCLE_THREAD_NONE), "");
}
#endif

