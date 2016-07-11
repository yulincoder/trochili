#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"
#include "ruby.os.h"

/* 本文件处理全部图形显示的工作，
   其他任务或者ISR通过发送不同类别的消息来驱动
   1 按键中断处理函数会发送不同按键编号被按下的消息
   2 定时器任务会发送不同的时间消息到来
   本任务处理这些消息，用户可以通过扩展消息来增加其他功能

   本文件中的代码需要用户
   1 更新按键处理函数的实现。参考colibri.dev.key.c文件和它的相关bsp
   2 更新添加TFT驱动代码.
   */
#define KEY_UP_ID    0x1
#define KEY_DOWN_ID  0x2
#define KEY_LEFT_ID  0x3
#define KEY_RIGHT_ID 0x4

/* 各界面编号 */
#define NORMAL_WINDOW_VISIABLE  (1)
#define WINDOW1_VISIABLE         (2)
#define WINDOW2_VISIABLE         (3)

/* 指示当先显示的是哪个界面 */
static int CurrentWinID = 0;

/* 确认USB数据已经被接收完毕,请求结束USB本次接收过程 */
static void  AIIO_ProcKeyMsg(int keyid)
{
    switch (CurrentWinID)
    {
        case NORMAL_WINDOW_VISIABLE:
        {
            /* 处理常驻画面收到的按键 */
            switch (keyid)
            {
                    /* 处理常驻画面收到的向上按键 */
                case KEY_UP_ID:
                {
                    break;
                }
                case KEY_DOWN_ID:
                {
                    break;
                }
                case KEY_LEFT_ID:
                {
                    break;
                }
                case KEY_RIGHT_ID:
                {
                    break;
                }
                default:
                {
                    break;
                }
            }
            break;
        }
        case WINDOW1_VISIABLE:
        {
            /* 处理画面1收到的向上按键 */
            break;
        }
        case WINDOW2_VISIABLE:
        {
            /* 处理画面2收到的向上按键 */
            break;
        }
        default:
        {
            break;
        }
    }
}




/* 描画常驻界面 */
static void DrawNormalWindow (void)
{

}

/* 描画界面1 */
static void DrawWindow1 (void)
{

}

/* 描画界面2 */
static void DrawWindow2 (void)
{

}


/* 接收处理本次从IO任务接收到的数据 */
static void UI_ProcKeyPrimitive(CoMsgHead* pMsg)
{
    switch (pMsg->Primitive)
    {
        case ISR_KEY_UP:
        {
            AIIO_ProcKeyMsg(KEY_UP_ID);
            break;
        }

        case ISR_KEY_DOWN:
        {
            AIIO_ProcKeyMsg(KEY_DOWN_ID);
            break;
        }
        case ISR_KEY_LEFT:
        {
            AIIO_ProcKeyMsg(KEY_LEFT_ID);
            break;
        }

        case ISR_KEY_RIGHT:
        {
            AIIO_ProcKeyMsg(KEY_RIGHT_ID);
            break;
        }
        default:
        {
            break;
        }
    }
}

static void UI_ProcTimerPrimitive(CoMsgHead* pMsg)
{
    switch (pMsg->Primitive)
    {
        case WIND_TIMER_IND:
        {
            /* 当前窗口的显示时间到达，可以关闭了 */
            break;
        }
        case WIND_WALL_TIMER_IND:
        {
            /* 更新"墙上时间",即十分秒那个当前显示时钟 */
            break;
        }
        break;
    }
}


/* AI线程的主函数 */
void UI_ThreadEntry(unsigned int arg)
{
    CoMsgHead* pMsg;

    /* 首先显示常驻画面 */
    DrawNormalWindow();

    while (eTrue)
    {
        /* 从消息队列里读出具体消息 */
        OS_PendMessage(UI_QUEUE_ID, (void**)(&pMsg));
        if (pMsg)
        {
            switch (pMsg->Sender)
            {
                case KEY_ISR_ID:
                {
                    UI_ProcKeyPrimitive(pMsg);
                    break;
                }
                case TIMER_ISR_ID:
                {
                    UI_ProcTimerPrimitive(pMsg);
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

/* 更新墙上时间函数 */
void WallTimerFunc(TArgument data)
{
    /* 读取当前时间，更新屏幕 */
    CoMsgHead * pMsg = 0;

    /* 把待发送数据打包成消息 */
    pMsg = (CoMsgHead *)OS_MallocMemory(SIZEOF_MSG_HEAD);
    pMsg->Sender    = TIMER_ISR_ID;
    pMsg->Primitive = WIND_WALL_TIMER_IND;
    pMsg->Length    = SIZEOF_MSG_HEAD;

    /* 将消息发入UI任务的消息队列 */
    OS_SendMessage(UI_QUEUE_ID, (void**)(&pMsg));
}

/* 窗口显示结束时间到 */
void WinTimerFunc(TArgument data)
{
    CoMsgHead * pMsg = 0;

    /* 把待发送数据打包成消息 */
    pMsg = (CoMsgHead *)OS_MallocMemory(SIZEOF_MSG_HEAD);
    pMsg->Sender    = TIMER_ISR_ID;
    pMsg->Primitive = WIND_TIMER_IND;
    pMsg->Length    = SIZEOF_MSG_HEAD;

    /* 将消息发入UI任务的消息队列 */
    OS_SendMessage(UI_QUEUE_ID, (void**)(&pMsg));
}

#endif

