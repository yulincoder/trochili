#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"
#include "ruby.os.h"

static void AIIO_SendUartReq(char* req, int len);
static void AIIO_ProcUartRsp(CoDataRspMsg* pRsp);
static void AIIO_ProcUartInd (CoDataIndMsg* pIndMsg);
static void AIIO_SendUartCnf(CoMsgCnf* pCnf);

/* 请求发送UART数据,请求开始UART发送过程 */
static void AIIO_SendUartReq(char* req, int len)
{
    CoDataReqMsg * pMsg = 0;
    UINT8* pData = 0;

    /* 复制需要从串口发送的数据 */
    pData = (UINT8 *)OS_MallocMemory(len);
    memcpy(pData, req, len);

    /* 把待发送数据打包成消息 */
    pMsg = (CoDataReqMsg*)OS_MallocMemory(SIZEOF_DATAREQ_MSG);
    pMsg->Head.Sender    = AI_THREAD_ID;
    pMsg->Head.Primitive = AIIO_UART_REQ;
    pMsg->Head.Length    = SIZEOF_DATAREQ_MSG;
    pMsg->DataBuf        = (UINT8*)pData;
    pMsg->DataLen        = len;

    /* 设置消息响应数据 */
    pMsg->RspInfo.MsgQId = AI_QUEUE_ID ;
    pMsg->RspInfo.Prim   = AIIO_UART_RSP ;
    pMsg->RspInfo.Send   = 1;
    pMsg->RspInfo.Data   = pData;

    /* 将消息发入IO任务的消息队列 */
    OS_SendMessage(IO_UART_REQ_QUEUE_ID, (void**)(&pMsg));

    /* 通知IO任务 */
    OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_TXREQ_FLG);
}


/* 接收处理本次UART发送请求的响应消息 */
static void AIIO_ProcUartRsp(CoDataRspMsg* pRsp)
{
    if (pRsp->DataBuf!= 0)
    {
        OS_FreeMemory((void*)(pRsp->DataBuf));
    }
}


static char* teststring= "abcdefghijklmnopqrstuvwxyz";
static char teststring2[32];
/* 接收处理本次UART接收到的数据 */
static void AIIO_ProcUartInd (CoDataIndMsg* pIndMsg)
{
    char* data;
    int len;
    CoMsgCnf CnfInfo;

    CnfInfo.Prim   = pIndMsg->CnfInfo.Prim;
    CnfInfo.Data   = pIndMsg->CnfInfo.Data;
    CnfInfo.Send   = pIndMsg->CnfInfo.Send;
    CnfInfo.MsgQId = pIndMsg->CnfInfo.MsgQId;

    /* 处理接收到的数据 */
    len  = pIndMsg->DataLen;
    data = (char*)OS_MallocMemory(len);
    memcpy(data, pIndMsg->DataBuf, len);
	memcpy(teststring2, pIndMsg->DataBuf, len);
    OS_FreeMemory(data);

    /* 发送数据确认信息 */
    AIIO_SendUartCnf(&CnfInfo);

    AIIO_SendUartReq(teststring2, len);
}


/* 确认UART数据已经被接收完毕,请求结束UART接收过程 */
static void AIIO_SendUartCnf(CoMsgCnf* pCnf)
{
    CoDataCnfMsg* pMsg = 0;

    if (pCnf->Send)
    {
        pMsg = (CoDataCnfMsg*)OS_MallocMemory(SIZEOF_DATACNF_MSG);
        pMsg->Head.Sender    = AI_THREAD_ID;
        pMsg->Head.Primitive = pCnf->Prim;
        pMsg->Head.Length    = SIZEOF_DATACNF_MSG;
        pMsg->DataBuf        = pCnf->Data;
        OS_SendMessage(pCnf->MsgQId, (void**)(&pMsg));
        OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_RXCNF_FLG);
    }
    else
    {
        if (pCnf->Data !=0)
        {
            OS_Error("error ");
        }
    }
}



/* 接收处理本次从IO任务接收到的数据 */
static void AI_ProcIOPrimitive(CoMsgHead* pMsg)
{
    switch (pMsg->Primitive)
    {
        case AIIO_UART_RSP:
        {
            AIIO_ProcUartRsp((CoDataRspMsg*)pMsg);
            break;
        }
        case AIIO_UART_IND:
        {
            AIIO_ProcUartInd((CoDataIndMsg*)pMsg);
            break;
        }
        default:
        {
            break;
        }
    }

}


/* 接收处理本次从FS任务接收到的数据 */
static void AI_ProcFSPrimitive(void* pMsg2)
{
}


/* 接收处理本次从UI任务接收到的数据 */
static void AI_ProcUIPrimitive(void* pMsg2)
{
}


/* AI线程的主函数 */
void AI_ThreadEntry(unsigned int arg)
{
    CoMsgHead* pMsg;

    while (eTrue)
    {
        /* 从消息队列里读出具体消息 */
        OS_PendMessage(AI_QUEUE_ID, (void**)(&pMsg));
        if (pMsg)
        {
            switch (pMsg->Sender)
            {
                case IO_THREAD_ID:
                {
                    AI_ProcIOPrimitive(pMsg);
                    break;
                }
                case FS_THREAD_ID:
                {
                    AI_ProcFSPrimitive(pMsg);
                    break;
                }
                case UI_THREAD_ID:
                {
                    AI_ProcUIPrimitive(pMsg);
                    break;
                }
                default:
                {
                    break;
                }
            }

            /* 处理完收到的消息后，释放该消息占用的内存 */
            OS_FreeMemory(pMsg);
        }
    }
}

#endif

