#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"
#include "ruby.os.h"
#include "ruby.dev.h"
#include "gd32f20x.h"
/* define UART control constants for DATA */
static  unsigned char IO_DATA_RxBuf[2][IO_DATA_RXBUF_SIZE];
CoIODevCtrl UartDev;

void DevInitUart(void)
{
    UartDev.DATA.TxBufCount  = 0;
    UartDev.DATA.TxBufPtr    = 0;
    UartDev.DATA.RxBufCount  = 0;
    UartDev.DATA.RxBufRdPtr  = &(IO_DATA_RxBuf[0][0]);
    UartDev.DATA.RxBufWrPtr  = &(IO_DATA_RxBuf[1][0]);
}

#define UART_UART_TX_HW_FIFO_SIZE         1
#define UART_UART_RX_HW_FIFO_SIZE         1

/* 说明一下，IDLE的中断在串口无数据接收的情况下，是不会一直产生的，
   产生的条件是这样的，当清除IDLE标志位后，必须有接收到第一个数据后，才开始触发，
   一断接收的数据断流，没有接收到数据，即产生IDLE中断。*/
UINT32 IO_DataUartISR(UINT32 data)
{
    static UINT32 index = 0;
    UINT16 rxc;
    UINT8 txc;
    UINT32 Fifodepth;
    UINT8* temp;

#if 1
    /*******************************************************/
    /*               handle RX interrupt                   */
    /*******************************************************/
    /* Service RX data ready as long as there is data in RX FIFO */
    if( USART_GetIntBitState(USART1, USART_INT_RBNE) != RESET)
    {
         USART_INT_Set( USART1, USART_INT_IDLEF, ENABLE );
        /* Read data from RX UART1 DR and store in buffer */
        rxc = USART_DataReceive(USART1) & 0xff;
        UartDev.DATA.RxBufWrPtr[index] = (UINT8)rxc;
        UartDev.DATA.RxBufCount++;
        index++;
        USART_ClearIntBitState(USART1, USART_INT_RBNE);
    }

    if( USART_GetIntBitState(USART1, USART_INT_IDLEF) != RESET)
    {
         USART_INT_Set(USART1, USART_INT_IDLEF, DISABLE );
         USART_ClearIntBitState(USART1, USART_INT_IDLEF);
			
        index = 0;
        temp = UartDev.DATA.RxBufRdPtr;
        UartDev.DATA.RxBufRdPtr = UartDev.DATA.RxBufWrPtr;
        UartDev.DATA.RxBufWrPtr = temp;
        OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_RXIND_FLG);
		   
    }

    /*******************************************************/
    /*                  handle TX interrupt                */
    /*******************************************************/
    if( USART_GetIntBitState(USART1, USART_INT_TBE) != RESET)
    {
        USART_ClearIntBitState(USART1, USART_INT_TBE);

        /* Init FIFO count */
        Fifodepth = UART_UART_TX_HW_FIFO_SIZE;

        /* Add data to TX UART FIFO until it is full */
        while((UartDev.DATA.TxBufCount != 0) && (Fifodepth > 0))
        {
            /* Write data byte to UART TX FIFO */
            /* Write one byte to the transmit data register */
            txc = *(UartDev.DATA.TxBufPtr);
            USART_DataSend( USART1 , txc );
            UartDev.DATA.TxBufPtr++;

            /* Decrement Tx Serial count */
            UartDev.DATA.TxBufCount--;

            /* Decrement FIFO count */
            Fifodepth--;
        }

        /* if we have send all character in fifo */
        if (UartDev.DATA.TxBufCount == 0 )
        {
            /* disable the USART1 Transmoit interrupt */
            USART_INT_Set(USART1, USART_INT_TBE, DISABLE );
            OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_TXRSP_FLG);
        }
    }
#else

    /*******************************************************/
    /*               handle RX interrupt                   */
    /*******************************************************/
    /* Service RX data ready as long as there is data in RX FIFO */
    if( USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        USART_ITConfig( USART1, USART_IT_IDLE, ENABLE );
        /* Read data from RX UART1 DR and store in buffer */
        rxc = USART_ReceiveData(USART1) & 0xff;
        UartDev.DATA.RxBufWrPtr[index] = (UINT8)rxc;
        UartDev.DATA.RxBufCount++;
        index++;
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }

    if( USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        USART_ClearITPendingBit(USART1, USART_IT_IDLE);
        USART_ITConfig(USART1, USART_IT_IDLE, DISABLE );

        index = 0;
        temp = UartDev.DATA.RxBufRdPtr;
        UartDev.DATA.RxBufRdPtr = UartDev.DATA.RxBufWrPtr;
        UartDev.DATA.RxBufWrPtr = temp;
        OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_RXIND_FLG);
    }

    /*******************************************************/
    /*                  handle TX interrupt                */
    /*******************************************************/
    if( USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
    {
        USART_ClearITPendingBit(USART1, USART_IT_TXE);

        /* Init FIFO count */
        Fifodepth = UART_UART_TX_HW_FIFO_SIZE;

        /* Add data to TX UART FIFO until it is full */
        while((UartDev.DATA.TxBufCount != 0) && (Fifodepth > 0))
        {
            /* Write data byte to UART TX FIFO */
            /* Write one byte to the transmit data register */
            txc = *(UartDev.DATA.TxBufPtr);
            USART_SendData( USART1 , txc );
            UartDev.DATA.TxBufPtr++;

            /* Decrement Tx Serial count */
            UartDev.DATA.TxBufCount--;

            /* Decrement FIFO count */
            Fifodepth--;
        }

        /* if we have send all character in fifo */
        if (UartDev.DATA.TxBufCount == 0 )
        {
            /* Enable the USART1 Transmoit interrupt */
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE );
            OS_SetEvent(IO_EVENT_GROUP_ID, IO_UART_TXRSP_FLG);
        }
    }
#endif
    return 0;
}

#endif

