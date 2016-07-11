/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                        www.trochili.com                                       *
 *************************************************************************************************/
#include "gd32f1x0.h"
#include "colibri_bsp_key.h"

void EvbKeyConfig(void)
{
    GPIO_InitPara GPIO_InitStructure;
    EXTI_InitPara EXTI_InitStructure;
    NVIC_InitPara NVIC_InitStructure;

    /* Enable the KEY Clock */
    RCC_AHBPeriphClock_Enable(RCC_AHBPERIPH_GPIOB, ENABLE);

    /* Configure KEY pin */
    GPIO_InitStructure.GPIO_Pin = GPIO_PIN_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PUPD_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_PIN_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_MODE_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PUPD_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Enable SYSCFG clock */
    RCC_APB2PeriphClock_Enable(RCC_APB2PERIPH_CFG, ENABLE);

    /* Connect EXTI Line6/7 to PB6/7 pin */
    SYSCFG_EXTILine_Config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN6);
    SYSCFG_EXTILine_Config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN7);

    /* Configure EXTI Line6/7 and its interrupt to the lowest priority*/
    EXTI_InitStructure.EXTI_LINE = EXTI_LINE6;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LINEEnable = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    EXTI_ClearIntBitState(EXTI_LINE6);


    /* Configure EXTI Line6/7 and its interrupt to the lowest priority*/
    EXTI_InitStructure.EXTI_LINE = EXTI_LINE7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LINEEnable = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    EXTI_ClearIntBitState(EXTI_LINE7);

    /* 1 bits for pre-emption priority and 3 bits for subpriority */
    NVIC_PRIGroup_Enable(NVIC_PRIGROUP_1);

    /* Enable and set EXTI4_15 Interrupt to the highest priority */
    NVIC_InitStructure.NVIC_IRQ = EXTI4_15_IRQn;
    NVIC_InitStructure.NVIC_IRQPreemptPriority = 0;
    NVIC_InitStructure.NVIC_IRQSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQEnable = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

int EvbKeyScan(void)
{
    int count = 0xffff;

    if (EXTI_GetIntBitState(EXTI_LINE6) != RESET)
    {
        while(count--);
        if (EXTI_GetIntBitState(EXTI_LINE6) != RESET)
        {
            EXTI_ClearIntBitState(EXTI_LINE6);
            return 1;
        }
    }

    if (EXTI_GetIntBitState(EXTI_LINE7) != RESET)
    {
        while(count--);
        if (EXTI_GetIntBitState(EXTI_LINE7) != RESET)
        {
            EXTI_ClearIntBitState(EXTI_LINE7);
            return 2;
        }
    }

    return 0;
}
