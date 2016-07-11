#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"
#include "ruby.os.h"
#include "ruby.dev.h"

static TIrq  KeyIrq;

/* 这个arg就是上下左右键的编号 */
static void KeyIrqCallback(TArgument arg)
{
	CoMsgHead * pMsg = 0;

    /* 把待发送数据打包成消息 */
    pMsg            = (CoMsgHead *)OS_MallocMemory(SIZEOF_MSG_HEAD);
    pMsg->Sender    = KEY_ISR_ID;
    pMsg->Primitive = arg;
    pMsg->Length    = SIZEOF_MSG_HEAD;

    /* 将消息发入UI任务的消息队列 */
    OS_SendMessage(UI_QUEUE_ID, (void**)(&pMsg));
}

UINT32 IO_KeyISR(UINT32 data)
{
    TState state;
    TError error;
    static int i = 0;
    int key;

    key = EvbKeyScan();
    state = TclPostIRQ(&KeyIrq,
                       (TPriority)5,
                       KeyIrqCallback,
                       (TArgument)key,
                       &error);
	
	 return 1;
}

#endif

