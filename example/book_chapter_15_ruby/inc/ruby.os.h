#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)

#ifndef TOPAZ_OS_H
#define TOPAZ_OS_H

#include "ruby.types.h"

#define IO_MQ_LEN (32)
#define FS_MQ_LEN (4)
#define UI_MQ_LEN (4)
#define AI_MQ_LEN (4)


#define IO_THREAD_ID              0
#define FS_THREAD_ID              1
#define UI_THREAD_ID              2
#define KEY_ISR_ID                3
#define TIMER_ISR_ID              4
#define AI_THREAD_ID              5
#define NUM_OF_THREADS            ((AI_THREAD_ID) + 1)


#define IO_UART_REQ_QUEUE_ID      0
#define IO_UART_CNF_QUEUE_ID      1
#define FS_QUEUE_ID               2
#define UI_QUEUE_ID               3
#define AI_QUEUE_ID               4
#define NUM_OF_QUEUES             ((AI_QUEUE_ID) + 1)


#define IO_EVENT_GROUP_ID        0
#define FS_EVENT_GROUP_ID        1
#define UI_EVENT_GROUP_ID        2
#define AI_EVENT_GROUP_ID        3
#define NUM_OF_EVENT_GROUPS     (AI_EVENT_GROUP_ID + 1)


#define IO_TIMER_ID               0
#define FS_TIMER_ID               1
#define UI_TIMER_ID               2
#define UI_WALL_TIMER_ID         3
#define AI_TIMER_ID               4
#define TS_TIMER_ID               5
#define NUM_OF_TIMERS            (TS_TIMER_ID + 1)


/* AI线程参数 */
#define THREAD_AI_STACK_BYTES   (512)
#define THREAD_AI_PRIORITY       (7)
#define THREAD_AI_SLICE          (20)

/* IO 线程参数 */
#define THREAD_IO_STACK_BYTES   (512)
#define THREAD_IO_PRIORITY      (7)
#define THREAD_IO_SLICE          (20)


#define MEMORY_PAGE_SIZE      (32)
#define MEMORY_PAGES    (TCLC_MEMORY_BUDDY_PAGES)

extern void OS_SetupMemory(void);
extern void OS_SetupFlags(void);
extern void OS_SetupQueues(void);
extern void OS_SetupISR(void);
extern void OS_SetupThreads(void);
	
extern void* OS_MallocMemory(int len);
extern void OS_FreeMemory(void* pMsg);
extern void OS_SendMessage(TIndex QId, void** pMsg);
extern void OS_GetMessage(TIndex QId, void** pMsg);
extern void OS_PendMessage(TIndex QId, void** pMsg);
extern void OS_SetEvent(UINT16 flagID, UINT32 EvtBit);
extern void OS_WaitEvent(UINT16 flagID, UINT32* pattern);
extern void OS_Error(char* str);
extern void IO_ThreadEntry(unsigned int arg);
extern void AI_ThreadEntry(unsigned int arg);
#endif 
#endif
