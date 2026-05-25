#ifndef __SCOPE_H__
#define __SCOPE_H__

#include <stdbool.h>
#include <stdint.h>

#include "stm32g4xx.h"

#define ADC_BUFFER_SIZE 2

extern volatile uint16_t adcBuffer[ADC_BUFFER_SIZE];
extern bool scopeEnabled;
HAL_StatusTypeDef scopeInit(void);
#endif  // __SCOPE_H__