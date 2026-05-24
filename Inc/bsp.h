#ifndef __BSP_H__
#define __BSP_H__

#include "adc.h"
#include "i2c.h"
#include "main.h"
#include "tim.h"
#include "usart.h"

#define DEBUG_UART_HANDLE hlpuart1
#define CLOCK_PROG_I2C_HANDLE hi2c1
#define CLK_IN_TIM_HANDLE htim2
#define ONE_HZ_TIM_HANDLE htim3
#define STATS_TIM_HANDLE htim4
#define ADC_HANDLE hadc1
#endif  // __BSP_H__