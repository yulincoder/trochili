/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                        www.trochili.com                                       *
 *************************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "gd32f1x0.h"
#include "colibri_bsp_uart.h"

void EvbUart2Config(void)
{
    /* Configure the GPIO ports */
    GPIO_InitPara  GPIO_InitStructure;
    USART_InitPara  USART_InitStructure;

    /* Enable GPIO clock */
    RCC_AHBPeriphClock_Enable(RCC_AHBPERIPH_GPIOA, ENABLE);

    /* Enable USART APB clock */
    RCC_APB1PeriphClock_Enable(RCC_APB1PERIPH_USART2, ENABLE);

    /* Connect PXx to USARTx_Tx */
    GPIO_PinAFConfig(GPIOA, GPIO_PINSOURCE2, GPIO_AF_1);

    /* Connect PXx to USARTx_Rx */
    GPIO_PinAFConfig(GPIOA, GPIO_PINSOURCE3, GPIO_AF_1);

    /* Configure USART Rx/Tx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin     = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStructure.GPIO_Mode    = GPIO_MODE_AF;
    GPIO_InitStructure.GPIO_Speed   = GPIO_SPEED_50MHZ;
    GPIO_InitStructure.GPIO_OType   = GPIO_OTYPE_PP;
    GPIO_InitStructure.GPIO_PuPd    = GPIO_PUPD_NOPULL;
    GPIO_Init(GPIOA , &GPIO_InitStructure);

    USART_DeInit( USART2 );

    USART_InitStructure.USART_BRR                 = 115200;
    USART_InitStructure.USART_WL                  = USART_WL_8B;
    USART_InitStructure.USART_STBits              = USART_STBITS_1;
    USART_InitStructure.USART_Parity              = USART_PARITY_RESET;
    USART_InitStructure.USART_HardwareFlowControl = USART_HARDWAREFLOWCONTROL_NONE;
    USART_InitStructure.USART_RxorTx              = USART_RXORTX_RX | USART_RXORTX_TX;
    USART_Init(USART2, &USART_InitStructure);

    /* USART enable */
    USART_Enable(USART2, ENABLE);
}


/*************************************************************************************************
 *  功能：向串口2发送一个字符                                                                    *
 *  参数：(1) 需要被发送的字符                                                                   *
 *  返回：                                                                                       *
 *  说明：                                                                                       *
 *************************************************************************************************/
void EvbUart2WriteByte(char c)
{
    USART_DataSend(USART2, c);
    while (USART_GetBitState(USART2, USART_FLAG_TBE) == RESET)
        ;
}

/*************************************************************************************************
 *  功能：向串口2发送一个字符串                                                                  *
 *  参数：(1) 需要被发送的字符串                                                                 *
 *  返回：                                                                                       *
 *  说明：                                                                                       *
 *************************************************************************************************/
void EvbUart2WriteStr(const char* str)
{

    while (*str)
    {
        USART_DataSend(USART2, * str++);
        while (USART_GetBitState(USART2, USART_FLAG_TBE) == RESET)
            ;
    }
}


/*************************************************************************************************
 *  功能：从串口2接收一个字符                                                                    *
 *  参数：(1) 存储接收到的字符                                                                   *
 *  返回：                                                                                       *
 *  说明：                                                                                       *
 *************************************************************************************************/
void EvbUart2ReadByte(char* c)
{
    while (USART_GetBitState(USART2, USART_FLAG_RBNE) == RESET)
        ;
    *c = (USART_DataReceive(USART2));
}


static char _buffer[256];
void EvbUart2Printf(char* fmt, ...)
{
    int i;
    va_list ap;
    va_start(ap, fmt);
    vsprintf(_buffer, fmt, ap);
    va_end(ap);

    for (i = 0; _buffer[i] != '\0'; i++)
    {
        EvbUart2WriteByte(_buffer[i]);
    }
}

