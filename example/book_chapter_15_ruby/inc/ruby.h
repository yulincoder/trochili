#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)

#ifndef COLIBRI_H
#define COLIBRI_H
#include "colibri207bsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* typedef data type  */
typedef unsigned int    UINT32;
typedef int             INT32;
typedef unsigned short  UINT16;
typedef short           INT16;
typedef unsigned char   UINT8;
typedef char            INT8;
typedef int             INT;
typedef unsigned int    UINT;



#define AIIO_UART_IND        (0xEA) /* 串口接收到数据     */
#define AIIO_UART_CNF        (0xEB)
#define AIIO_UART_REQ        (0xEC) /* 请求向串口发送数据 */
#define AIIO_UART_RSP        (0xED) 


#define ISR_KEY_UP           (0xAA)
#define ISR_KEY_DOWN         (0xAB)
#define ISR_KEY_LEFT         (0xAC)
#define ISR_KEY_RIGHT        (0xAD)
#define ISR_KEY_OK           (0xAE)
#define ISR_KEY_CANCEL       (0xAF)

/* 某个界面可能只需要显示一会儿时间，然后自动关闭。这时可以通过开启1个软件
   定时器，到时刻自动发出这个消息 */
#define WIND_TIMER_IND       (0xBA)

/* 屏幕上的时间显示，可以通过一个周期性的软件定时器，每秒发一个这个消息 */
#define WIND_WALL_TIMER_IND  (0xBB)

/* IO任务相关的事件标记 */
#define IO_UART_RXIND_FLG   (0x1<<0)  /* uart rx availibel       */
#define IO_UART_RXCNF_FLG   (0x1<<1)  /* uart rx done            */
#define IO_UART_TXREQ_FLG   (0x1<<2)  /* uart tx require         */
#define IO_UART_TXRSP_FLG   (0x1<<3)  /* uart tx done            */


/* Colibri框架的通用消息头 */
typedef struct
{
    TWord16 Sender;                    /* 消息来源 */
    TWord16 Primitive;                 /* 消息原语 */
    TWord16 Length;                    /* 消息长度 */
} CoMsgHead;
#define SIZEOF_MSG_HEAD (sizeof (CoMsgHead))

/* Colibri框架的通用RSP信息结构 */
typedef struct
{
    TWord16   MsgQId;                  /* RSP消息队列编号         */
    UINT8     Prim;                    /* RSP原语                 */
    UINT8     Send;                    /* 是否需要RSP本次消息标记 */
    UINT8*    Data;                    /* 数据内存地址            */
} CoMsgRsp;
#define SIZEOF_MSG_RSP (sizeof (CoMsgRsp))

/* Colibri框架的通用CNF信息结构 */
typedef struct
{
    TWord16   MsgQId;                  /* CNF消息队列编号         */
    UINT8     Prim;                    /* CNF原语                 */
    UINT8     Send;                    /* 是否需要CNF本次消息标记 */
    UINT8*    Data;                    /* 数据内存地址            */
} CoMsgCnf;
#define SIZEOF_MSG_CNF (sizeof (CoMsgCnf))

/* Colibri框架的通用IND消息结构 */
typedef struct
{
    CoMsgHead Head;                     /* 通用消息头结构         */
    CoMsgCnf  CnfInfo;                  /* CNF信息                */
    UINT8*    DataBuf;                  /* 数据内存地址           */
    UINT8     DataLen;                  /* 数据长度               */
} CoDataIndMsg;
#define SIZEOF_DATAIND_MSG  (sizeof (CoDataIndMsg))

/* Colibri框架的通用CNF消息结构 */
typedef struct
{
    CoMsgHead Head;                    /* 通用消息头结构          */
    UINT8*    DataBuf;                 /* 数据内存地址            */
} CoDataCnfMsg;
#define SIZEOF_DATACNF_MSG (sizeof(CoDataCnfMsg))

/* Colibri框架的通用REQ消息结构 */
typedef struct
{
    CoMsgHead Head;                    /* 通用消息头结构          */
    CoMsgRsp  RspInfo;                 /* RSP信息                 */
    UINT8*    DataBuf;                 /* 数据内存地址            */
    UINT32    DataLen;                 /* 数据长度                */
} CoDataReqMsg;
#define SIZEOF_DATAREQ_MSG  (sizeof(CoDataReqMsg))

/* Colibri框架的通用RSP消息结构 */
typedef struct
{
    CoMsgHead Head;                    /* 通用消息头结构          */
    UINT8*    DataBuf;                 /* 数据内存地址            */
} CoDataRspMsg;
#define SIZEOF_DATARSP_MSG (sizeof(CoDataRspMsg))


#endif
#endif
