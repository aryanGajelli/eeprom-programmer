#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <stdint.h>

#include "stm32g4xx_hal.h"

#define EEPROM_SIZE (1u << 16u)  // 64 kB
#define MAX_ADDRESS ((uint16_t)(EEPROM_SIZE - 1u))
#define SECTOR_SIZE (0x1000u)  // 4 kB

#define RESET(GPIOx, GPIO_Pin) (GPIOx->BRR = (uint32_t)GPIO_Pin)
#define SET(GPIOx, GPIO_Pin) (GPIOx->BSRR = (uint32_t)GPIO_Pin)
HAL_StatusTypeDef eepromInit(void);
HAL_StatusTypeDef eepromProgramBuffer(uint16_t startAddr, uint32_t length, const uint8_t* buffer);
void sectorErase(uint16_t sectorAddr);
void eepromWrite(const uint16_t addr, const uint8_t data);
HAL_StatusTypeDef eepromWriteSection(uint16_t startAddr, uint32_t length, const uint8_t* buffer);
void addressToOutput(void);
void dataToOutput(void);
void dataToInput(void);
uint8_t eepromRead(const uint16_t addr);
void eepromReadSection(const uint16_t startAddr, const uint16_t length, uint8_t* buffer);
#endif  // __EEPROM_H__