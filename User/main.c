/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2024/01/01
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 *Multiprocessor communication mode routine:
 *Master:USART1_Tx(PD5)\USART1_Rx(PD6).
 *This routine demonstrates that USART1 receives the data sent by CH341 and inverts
 *it and sends it (baud rate 115200).
 *
 *Hardware connection:PD5 -- Rx
 *                     PD6 -- Tx
 *
 */

#include "debug.h"

/*
 *           CH32V005D6U6
 *          LED driver board for v1.3
 *
 *                 +- GND
 *           +-----+------+
 * NTC     --+ PA1   PD7  +-- SNT
 * VBOOST  --+ PA2   PD4  +-- FBO
 * SNTBP   --+ PD0   PD1  +-- SWD
 * 5.0v    --+ VDD   PC7  +-- MOON
 * BUCK_EN --+ PC0   PC6  +--
 * PWM     --+ PC3   PC4  +-- VBAT
 *           +------------+
 */


/* Global define */

#define PWM_PERIOD      200
#define PWM_OFF_DUTY    70
#define NIMH_THRES      (2100*4096/5000)         // NiMH judge (2.1V/5.0V)
#define TEMP_THRES      1200
#define LOOP_DELAY_MS   10

/* Global Variable */
vu8 val;

uint16_t current_pwm = PWM_PERIOD;

void OPAmp_Init()
{
    /*
     *      +------CH32V005----+
     *      |                  |
     *      |    | |            |
     *      |    |  |           |
     *  PD7-+----+ + |          |
     *      |    |    >----+----+--PD4
     *      |  +-+ - /     |    |
     *      |  | |  /      R1   |
     *      |  | | /       |    |
     *      |  +-----------+    |
     *      |              |    |
     *      |              R2   |
     *      |              |    |
     *      |             GND   |
     *      +-------------------+
     */
    OPA_InitTypeDef OPA_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_OPA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    RCC_AL_PeriphClockCmd(RCC_AL_Periph_OPA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    OPA_InitStructure.OPA_Mode = OPA_PGA_Mode;
    OPA_InitStructure.OPA_PGA_Gain = OPA_PGA_Gain_32;
    OPA_InitStructure.OPA_Input = OPA_Input_PD7;

    OPA_Init(&OPA_InitStructure);
    OPA_Cmd(ENABLE);
}

void TIM1_PWMOut_Init(uint16_t arr, uint16_t psc, uint16_t ccp) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOD | RCC_PB2Periph_TIM1, ENABLE);

    /* Keep PC3 High until PWM init */
    GPIO_SetBits(GPIOC ,GPIO_Pin_3);



    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = ccp;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC4Init(TIM1, &TIM_OCInitStructure);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);

    /* PC3 work as PWM here */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3; // PDC (PWM)
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

void ADC_Function_Init(void) {
    ADC_InitTypeDef ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_PB2PeriphClockCmd(RCC_PB2Periph_GPIOA | RCC_PB2Periph_GPIOC | RCC_PB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1; // PA1:TEMP (ADC_CH1)
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4; // PC4:BATT (ADC_CH2)
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; // PA2:V_BOOST
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA ,GPIO_Pin_2);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0; // PC0:BUCK EN
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_SetBits(GPIOC ,GPIO_Pin_0);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
    ADC_Cmd(ADC1, ENABLE);
}

uint16_t Get_ADC_Val(uint8_t ch) {
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_CyclesMode7);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    return ADC_GetConversionValue(ADC1);
}


/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    OPAmp_Init();
    ADC_Function_Init();

    TIM1_PWMOut_Init(PWM_PERIOD, 0, PWM_PERIOD);
Delay_Ms(3000);

    while(1)
    {
        uint16_t v_batt = Get_ADC_Val(ADC_Channel_2);
        uint16_t v_temp = Get_ADC_Val(ADC_Channel_1);

        uint16_t min_duty = 0;

        if (current_pwm > min_duty) current_pwm--;
        if (current_pwm < PWM_OFF_DUTY) current_pwm = PWM_OFF_DUTY;

        TIM_SetCompare3(TIM1, current_pwm);
        Delay_Ms(LOOP_DELAY_MS);
    }
}
