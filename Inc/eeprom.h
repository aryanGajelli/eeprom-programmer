#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <stdint.h>

#include "stm32g4xx_hal.h"

#define EEPROM_SIZE ((1u << 16u) - 1)

#define RESET(GPIOx, GPIO_Pin) (GPIOx->BRR = (uint32_t)GPIO_Pin)
#define SET(GPIOx, GPIO_Pin) (GPIOx->BSRR = (uint32_t)GPIO_Pin)
HAL_StatusTypeDef eepromInit(void);
HAL_StatusTypeDef eepromProgramBuffer(uint16_t startAddr, uint32_t length, const uint8_t* buffer);
#endif  // __EEPROM_H__