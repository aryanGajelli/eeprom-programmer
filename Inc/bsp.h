#ifndef __BSP_H__
#define __BSP_H__

#include "adc.h"
#include "i2c.h"
#include "main.h"
#include "tim.h"
#include "usart.h"

#define F_CLK 170000000UL

#define OE_Pin CLK_Pin
#define OE_GPIO_Port CLK_GPIO_Port
#define WE_Pin RW_Pin
#define WE_GPIO_Port RW_GPIO_Port

#define A0_A1_A2_A3_A4_A8_A15_GPIO_Port GPIOC
#define A5_A6_A9_A10_A14_GPIO_Port GPIOA
#define A7_A11_A12_A13_GPIO_Port GPIOB
#define D0_D1_D2_D6_D7_GPIO_Port GPIOC

#define DEBUG_UART_HANDLE hlpuart1
#define CLOCK_PROG_I2C_HANDLE hi2c1
#define CLK_IN_TIM_HANDLE htim2
#define ONE_HZ_TIM_HANDLE htim3
#define STATS_TIM_HANDLE htim4
#define PWM_TIM_HANDLE htim5
#define ADC_HANDLE hadc1
#endif  // __BSP_H__