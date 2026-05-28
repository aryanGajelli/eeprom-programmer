#include "eeprom.h"

#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "bsp.h"
#include "debug.h"
#include "gpio.h"
#include "main.h"
#include "stm32g4xx_hal.h"
#include "task.h"

typedef struct {
    uint32_t ctrlDir;
    uint32_t addrDir;
    uint32_t dataDir;
} EEPROMConfig_t;

EEPROMConfig_t eepromConfig = {
    .ctrlDir = MODE_INPUT,
    .addrDir = MODE_INPUT,
    .dataDir = MODE_INPUT};

// Generic macro to measure any expression (call with args, multiple args, etc.)
// Usage: uint32_t cycles; MEASURE_EXPR_CYCLES(my_func(a,b), cycles);
#define MEASURE_EXPR_CYCLES(expr, out_cycles) \
    do {                                      \
        __DSB();                              \
        __ISB();                              \
        uint32_t _mc_start = DWT->CYCCNT;     \
        do {                                  \
            (expr);                           \
        } while (0);                          \
        __DSB();                              \
        __ISB();                              \
        uint32_t _mc_end = DWT->CYCCNT;       \
        (out_cycles) = _mc_end - _mc_start;   \
    } while (0)

#define US_TO_CYCLES(us) ((us) * (F_CLK) / 1000000)
#define delayUs(us) delay_cycles(US_TO_CYCLES(us))
// delay for N cycles
static inline void delay_cycles(uint32_t cycles) {
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
        __asm volatile("nop");
    }
}

void controlToOutput(void) {
    // disable the interrupt that was set on the clk pin of the cpu
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(OE_GPIO_Port, OE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WE_GPIO_Port, WE_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = OE_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OE_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = WE_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(WE_GPIO_Port, &GPIO_InitStruct);

    eepromConfig.ctrlDir = MODE_OUTPUT;
}

void addressToOutput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(A0_A1_A2_A3_A4_A8_A15_GPIO_Port, A0_Pin | A1_Pin | A15_Pin | A4_Pin | A8_Pin | A3_Pin | A2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(A5_A6_A9_A10_A14_GPIO_Port, A5_Pin | A6_Pin | A9_Pin | A10_Pin | A14_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(A7_A11_A12_A13_GPIO_Port, A7_Pin | A11_Pin | A12_Pin | A13_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = A0_Pin | A1_Pin | A15_Pin | A4_Pin | A8_Pin | A3_Pin | A2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(A0_A1_A2_A3_A4_A8_A15_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = A10_Pin | A9_Pin | A14_Pin | A6_Pin | A5_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(A5_A6_A9_A10_A14_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pins : PBPin PBPin PBPin PBPin
                             PBPin */
    GPIO_InitStruct.Pin = A13_Pin | A11_Pin | A12_Pin | A7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(A7_A11_A12_A13_GPIO_Port, &GPIO_InitStruct);

    eepromConfig.addrDir = MODE_OUTPUT;
}

void dataToOutput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = D5_Pin | D6_Pin | D7_Pin | D0_Pin | D1_Pin | D2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(D0_D1_D2_D5_D6_D7_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : PtPin */
    GPIO_InitStruct.Pin = D3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(D3_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pins : PBPin PBPin PBPin PBPin
                             PBPin */
    GPIO_InitStruct.Pin = D4_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(D4_GPIO_Port, &GPIO_InitStruct);

    eepromConfig.dataDir = MODE_OUTPUT;
}

void dataToInput(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = D5_Pin | D6_Pin | D7_Pin | D0_Pin | D1_Pin | D2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(D0_D1_D2_D5_D6_D7_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : PtPin */
    GPIO_InitStruct.Pin = D3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(D3_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pins : PBPin PBPin PBPin PBPin
                             PBPin */
    GPIO_InitStruct.Pin = D4_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(D4_GPIO_Port, &GPIO_InitStruct);

    eepromConfig.dataDir = MODE_INPUT;
}

#define ADDRESS_GPIOC_MASK (A0_Pin | A1_Pin | A2_Pin | A3_Pin | A4_Pin | A8_Pin | A15_Pin)
#define ADDRESS_GPIOA_MASK (A5_Pin | A6_Pin | A9_Pin | A10_Pin | A14_Pin)
#define ADDRESS_GPIOB_MASK (A7_Pin | A11_Pin | A12_Pin | A13_Pin)
#define DATA_GPIOC_MASK (D0_Pin | D1_Pin | D2_Pin | D5_Pin | D6_Pin | D7_Pin)
#define DATA_GPIOD_MASK (D3_Pin)
#define DATA_GPIOB_MASK (D4_Pin)
#define SET_RESET_ON_COND(cond, GPIOx, GPIO_Pin) (cond ? SET(GPIOx, GPIO_Pin) : RESET(GPIOx, GPIO_Pin))
#define ADDRESS_BIT_TO_PIN(addr, bit, pin) ((uint32_t)(0u - (((addr) >> (bit)) & 1u)) & (pin))
#define DATA_BIT_TO_PIN(data, bit, pin) ((uint32_t)(0u - (((data) >> (bit)) & 1u)) & (pin))
#define PIN_SHIFT(pin) (__builtin_ctz((unsigned)(pin)))
#define READ_PIN_TO_DATA(idr, pin, dataBit) ((uint8_t)((((idr) & (pin)) >> PIN_SHIFT(pin)) << (dataBit)))

void setAddress(uint16_t addr) {
    // Pack the address into per-port bitmasks and update each port with one BSRR write.
    const uint32_t addr32 = addr;

    const uint32_t gpioCValue = ADDRESS_BIT_TO_PIN(addr32, 0, A0_Pin) | ADDRESS_BIT_TO_PIN(addr32, 1, A1_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 2, A2_Pin) | ADDRESS_BIT_TO_PIN(addr32, 3, A3_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 4, A4_Pin) | ADDRESS_BIT_TO_PIN(addr32, 8, A8_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 15, A15_Pin);
    const uint32_t gpioAValue = ADDRESS_BIT_TO_PIN(addr32, 5, A5_Pin) | ADDRESS_BIT_TO_PIN(addr32, 6, A6_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 9, A9_Pin) | ADDRESS_BIT_TO_PIN(addr32, 10, A10_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 14, A14_Pin);
    const uint32_t gpioBValue = ADDRESS_BIT_TO_PIN(addr32, 7, A7_Pin) | ADDRESS_BIT_TO_PIN(addr32, 11, A11_Pin) |
                                ADDRESS_BIT_TO_PIN(addr32, 12, A12_Pin) | ADDRESS_BIT_TO_PIN(addr32, 13, A13_Pin);

    GPIOC->BSRR = gpioCValue | ((ADDRESS_GPIOC_MASK & ~gpioCValue) << 16);
    GPIOA->BSRR = gpioAValue | ((ADDRESS_GPIOA_MASK & ~gpioAValue) << 16);
    GPIOB->BSRR = gpioBValue | ((ADDRESS_GPIOB_MASK & ~gpioBValue) << 16);
}

uint8_t eepromReadRaw() {
    uint32_t idrD = D3_GPIO_Port->IDR;
    uint32_t idrB = D4_GPIO_Port->IDR;
    uint32_t idrC = D0_D1_D2_D5_D6_D7_GPIO_Port->IDR;
    return READ_PIN_TO_DATA(idrC, D0_Pin, 0) | READ_PIN_TO_DATA(idrC, D1_Pin, 1) |
           READ_PIN_TO_DATA(idrC, D2_Pin, 2) | READ_PIN_TO_DATA(idrD, D3_Pin, 3) |
           READ_PIN_TO_DATA(idrB, D4_Pin, 4) | READ_PIN_TO_DATA(idrC, D5_Pin, 5) |
           READ_PIN_TO_DATA(idrC, D6_Pin, 6) | READ_PIN_TO_DATA(idrC, D7_Pin, 7);
}

void eepromWriteRaw(uint16_t addr, uint8_t data) {
    setAddress(addr);
    const uint32_t data32 = data;
    const uint32_t gpioCValue = DATA_BIT_TO_PIN(data32, 0, D0_Pin) | DATA_BIT_TO_PIN(data32, 1, D1_Pin) |
                                DATA_BIT_TO_PIN(data32, 2, D2_Pin) | DATA_BIT_TO_PIN(data32, 5, D5_Pin) |
                                DATA_BIT_TO_PIN(data32, 6, D6_Pin) | DATA_BIT_TO_PIN(data32, 7, D7_Pin);
    const uint32_t gpioDValue = DATA_BIT_TO_PIN(data32, 3, D3_Pin);
    const uint32_t gpioBValue = DATA_BIT_TO_PIN(data32, 4, D4_Pin);

    GPIOC->BSRR = gpioCValue | ((DATA_GPIOC_MASK & ~gpioCValue) << 16);
    GPIOD->BSRR = gpioDValue | ((DATA_GPIOD_MASK & ~gpioDValue) << 16);
    GPIOB->BSRR = gpioBValue | ((DATA_GPIOB_MASK & ~gpioBValue) << 16);
}

/**
 * @brief Erase a 4Kb sector, only the A15-A12 address lines are used for sector selection, the rest are don't care. So the sectorAddr should be aligned to 0x1000 and only the bits A15-A12 are used.
 * @param sectorAddr The address of the sector to erase
 *                   should be aligned to 0x1000 and only the bits A15-A12 are used,
 *                   the rest are don't care and are stripped
 */
void sectorErase(uint16_t sectorAddr) {
    eepromWriteRaw(0x5555, 0xAA);
    eepromWriteRaw(0x2AAA, 0x55);
    eepromWriteRaw(0x5555, 0x80);
    eepromWriteRaw(0x5555, 0xAA);
    eepromWriteRaw(0x2AAA, 0x55);
    eepromWriteRaw(sectorAddr & 0xF000, 0x30);
}

#define MAX_POLLING_TIMEOUT_US 20
void dataPolling(uint8_t expectedData) {
    uint32_t startTime = DWT->CYCCNT;
    uint32_t prevDataDir = eepromConfig.dataDir;
    if (prevDataDir != MODE_INPUT) {
        dataToInput();
    }
    uint8_t data;

    while ((DWT->CYCCNT - startTime) < US_TO_CYCLES(MAX_POLLING_TIMEOUT_US)) {
        data = (D7_GPIO_Port->IDR & D7_Pin) ? 0x80 : 0x00;
        if (data == (expectedData & 0x80)) {
            break;
        }
    }

    if (prevDataDir == MODE_OUTPUT) {
        dataToOutput();
    }
}

void eepromWrite(uint16_t addr, uint8_t data) {
    eepromWriteRaw(0x5555, 0xAA);
    RESET(WE_GPIO_Port, WE_Pin);
    delay_cycles(US_TO_CYCLES(3) + 85);
    SET(WE_GPIO_Port, WE_Pin);

    eepromWriteRaw(0x2AAA, 0x55);
    delay_cycles(10);

    RESET(WE_GPIO_Port, WE_Pin);
    delay_cycles(US_TO_CYCLES(3) + 85);
    SET(WE_GPIO_Port, WE_Pin);
    eepromWriteRaw(0x5555, 0xA0);
    delay_cycles(10);

    RESET(WE_GPIO_Port, WE_Pin);
    delay_cycles(US_TO_CYCLES(3) + 85);
    SET(WE_GPIO_Port, WE_Pin);
    eepromWriteRaw(addr, data);
    delay_cycles(10);

    RESET(WE_GPIO_Port, WE_Pin);
    delay_cycles(US_TO_CYCLES(3) + 85);
    SET(WE_GPIO_Port, WE_Pin);
    dataPolling(data);
}

uint8_t eepromRead(uint16_t addr) {
    // Set the address
    setAddress(addr);
    RESET(OE_GPIO_Port, OE_Pin);
    // Enable output enable
    // small delay for output to stabilize at 170Mhz, 1 nop takes 1 cycle = 5.88ns, 7 nops = 41.16ns > 35ns which is T_OE
    delay_cycles(650);
    uint8_t data = eepromReadRaw();
    // Disable output enable
    SET(OE_GPIO_Port, OE_Pin);
    return data;
}

static bool parseUnsignedParameter(const char* commandString, UBaseType_t parameterIndex, uint32_t* valueOut) {
    BaseType_t parameterLength = 0;
    const char* parameter = FreeRTOS_CLIGetParameter(commandString, parameterIndex, &parameterLength);
    char* endPtr = NULL;

    if (parameter == NULL) {
        return false;
    }

    *valueOut = strtoul(parameter, &endPtr, 0 /* Auto detect base */);

    return endPtr != parameter && endPtr == parameter + parameterLength;
}

void eepromReadSection(uint16_t startAddr, uint16_t length, uint8_t* buffer) {
    RESET(OE_GPIO_Port, OE_Pin);
    // Enable output enable
    // small delay for output to stabilize at 170Mhz, 1 nop takes 1 cycle = 5.88ns, 7 nops = 41.16ns > 35ns which is T_OE
    delay_cycles(650);
    for (uint16_t i = 0; i < length; i++) {
        setAddress(startAddr + i);
        delay_cycles(1000);
        buffer[i] = eepromReadRaw();
    }
    SET(OE_GPIO_Port, OE_Pin);
}
/*********************************************************************************************/
BaseType_t cmd_eepromEn(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    controlToOutput();
    addressToOutput();
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_eepromDis(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    MX_GPIO_Init();
    eepromConfig.ctrlDir = MODE_INPUT;
    eepromConfig.addrDir = MODE_INPUT;
    eepromConfig.dataDir = MODE_INPUT;
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_readAddr(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t addr = 0;

    if (!parseUnsignedParameter(commandString, 1, &addr)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    if (addr > EEPROM_SIZE) {
        COMMAND_OUTPUT("Address out of range, got %lu !< %u\r\n", addr, EEPROM_SIZE);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT || eepromConfig.ctrlDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }

    taskENTER_CRITICAL();
    uint8_t data = eepromRead(addr);
    taskEXIT_CRITICAL();

    uprintf("%04lx = %02x\n", addr, data);

    return pdFALSE;
}

#define MAX_SECTION_BUFFER 512
uint8_t sectionBuffer[MAX_SECTION_BUFFER] = {0};
/*********************************************************************************************/
BaseType_t cmd_readSection(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t startAddr32 = 0;
    uint32_t endAddr32 = 0;

    if (!parseUnsignedParameter(commandString, 1, &startAddr32) || !parseUnsignedParameter(commandString, 2, &endAddr32)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    uint16_t startAddr = (uint16_t)startAddr32;
    uint16_t endAddr = (uint16_t)endAddr32;
    uint16_t bytesToRead = (endAddr - startAddr + 1);
// make sure bytesToRead is mod 16 for better display formatting
#define BYTES_PER_LINE 16
    if (bytesToRead % BYTES_PER_LINE != 0) {
        bytesToRead += (BYTES_PER_LINE - (bytesToRead % BYTES_PER_LINE));
    }

    if (startAddr > EEPROM_SIZE || endAddr > EEPROM_SIZE) {
        COMMAND_OUTPUT("Address out of range, got %u or %u !< %u\r\n", startAddr, endAddr, EEPROM_SIZE);
        return pdFALSE;
    }

    if (bytesToRead > MAX_SECTION_BUFFER) {
        COMMAND_OUTPUT("Too many bytes to read, got %u, max is %u\r\n", bytesToRead, MAX_SECTION_BUFFER);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT || eepromConfig.ctrlDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }

    taskENTER_CRITICAL();
    uint32_t cycles = 0;
    MEASURE_EXPR_CYCLES(
    eepromReadSection(startAddr, bytesToRead, sectionBuffer),
    cycles);
    taskEXIT_CRITICAL();
    
    uprintf("cycles: %lu = %.3f us\n", cycles, cycles / (F_CLK / 1000000.0f));
    uprintf("\n      00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f\n");
    for (uint32_t addr = startAddr; addr <= endAddr; addr += BYTES_PER_LINE) {
        uint16_t offset = addr - startAddr;
        uprintf("%04lx: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n", addr,
                sectionBuffer[0 + offset], sectionBuffer[1 + offset], sectionBuffer[2 + offset], sectionBuffer[3 + offset],
                sectionBuffer[4 + offset], sectionBuffer[5 + offset], sectionBuffer[6 + offset], sectionBuffer[7 + offset],
                sectionBuffer[8 + offset], sectionBuffer[9 + offset], sectionBuffer[10 + offset], sectionBuffer[11 + offset],
                sectionBuffer[12 + offset], sectionBuffer[13 + offset], sectionBuffer[14 + offset], sectionBuffer[15 + offset]);
    }

    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_writeDataToAddr(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t addr32 = 0;
    uint32_t data32 = 0;

    if (!parseUnsignedParameter(commandString, 1, &addr32)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    if (!parseUnsignedParameter(commandString, 2, &data32) || data32 > UINT8_MAX) {
        COMMAND_OUTPUT("Invalid data parameter\r\n");
        return pdFALSE;
    }

    uint16_t addr = (uint16_t)addr32;
    uint8_t data = (uint8_t)data32;

    if (addr > EEPROM_SIZE) {
        COMMAND_OUTPUT("Address out of range, got %u !< %u\r\n", addr, EEPROM_SIZE);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT || eepromConfig.ctrlDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }

    uint8_t currentData = eepromRead(addr);
    if (currentData == data) {
        COMMAND_OUTPUT("Data at address is already %02x, no need to write\r\n", data);
        return pdFALSE;
    }

    // can only write 1 bit to 0 since this is flash
    if ((currentData & data) != data) {
        COMMAND_OUTPUT("Cannot write 1 to 0, current value at address is %02x\r\n", currentData);
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }

    taskENTER_CRITICAL();
    uint32_t cycles = 0;
    MEASURE_EXPR_CYCLES(
        eepromWrite(addr, data),
        cycles);
    data = eepromRead(addr);
    taskEXIT_CRITICAL();

    uprintf("%04x = %02x (cycles: %lu = %.3f us)\n", addr, data, cycles, cycles / (F_CLK / 1000000.0f));
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_writeSection(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t addr32 = 0;
    uint32_t data32[16] = {0};

    if (!parseUnsignedParameter(commandString, 1, &addr32)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    uint8_t dataCount = 0;
    for (int i = 0; i < 16; i++) {
        if (!parseUnsignedParameter(commandString, 2 + i, &data32[i])) {
            if (i == 0) {
                COMMAND_OUTPUT("Invalid data parameter\r\n");
                return pdFALSE;
            } else {
                // No more data parameters, stop parsing
                break;
            }
        }
        if (data32[i] > UINT8_MAX) {
            COMMAND_OUTPUT("Invalid data parameter at index %d\r\n", i);
            return pdFALSE;
        }
        dataCount++;
    }

    uint16_t addr = (uint16_t)addr32;

    if (addr + dataCount > EEPROM_SIZE) {
        COMMAND_OUTPUT("Address out of range, got %u + %u !< %u\r\n", addr, dataCount, EEPROM_SIZE);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT || eepromConfig.ctrlDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }
#define INVALID 0x55FF
    for (int i = 0; i < dataCount; i++) {
        uint8_t currentData = eepromRead(addr + i);
        uint8_t data = (uint8_t)data32[i];
        if (currentData == data) {
            COMMAND_OUTPUT("Data at address %04x is already %02x, no need to write\r\n", addr + i, data);
            data32[i] = INVALID;
            continue;
        }

        // can only write 1 bit to 0 since this is flash
        if ((currentData & data) != data) {
            COMMAND_OUTPUT("Cannot write 1 to 0 at address %04x, current value is %02x\r\n", addr + i, currentData);
            data32[i] = INVALID;
            continue;
        }
    }

    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }
    uint32_t cycles = 0;
    taskENTER_CRITICAL();
    __DSB();
    __ISB();
    uint32_t _mc_start = DWT->CYCCNT;
    addr = (uint16_t)addr32;
    for (int i = 0; i < dataCount; i++) {
        if (data32[i] == INVALID) {
            continue;
        }
        eepromWrite(addr + i, (uint8_t)data32[i]);
    }
    __DSB();
    __ISB();
    uint32_t _mc_end = DWT->CYCCNT;
    cycles = _mc_end - _mc_start;
    taskEXIT_CRITICAL();

    uprintf("cycles: %lu = %.3f us\n", cycles, cycles / (F_CLK / 1000000.0f));
    return pdFALSE;
}

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "eepromEn",
        "eepromEn:\r\n  Enable the eeprom mode. GPIOs are input/output\r\n",
        cmd_eepromEn,
        0 /* Number of parameters */
    },
    {
        "eepromDis",
        "eepromDis:\r\n  Disable the eeprom mode. GPIOs are input\r\n",
        cmd_eepromDis,
        0 /* Number of parameters */
    },
    {
        "readAddr",
        "readAddr <address>:\r\n  Read a value from the specified EEPROM address\r\n",
        cmd_readAddr,
        1 /* Number of parameters */
    },
    {
        "readSection",
        "readSection <startAddress> <endAddress>:\r\n  Read values from the specified EEPROM address range\r\n",
        cmd_readSection,
        2 /* Number of parameters */
    },
    {
        "writeAddr",
        "writeAddr <address> <data>:\r\n  Write a value to the specified EEPROM address\r\n",
        cmd_writeDataToAddr,
        2 /* Number of parameters */

    },
    {
        "writeSection",
        "writeSection <startAddress> <data1> <data2> ... <dataN>:\r\n  Write values starting at the specified EEPROM address (max 16)\r\n",
        cmd_writeSection,
        -1 /* Variable number of parameters, at least 2 */
    },
    // {
    //     "sectorErase",
    //     "sectorErase <sectorAddress>:\r\n  Erase the specified sector, sectorAddress will be truncated with 0xF000\r\n"
    //     "  so only the bits A15-A12 are used for sector selection\r\n",
    //     cmd_sectorErase,
    //     1 /* Number of parameters */
    // },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }};

HAL_StatusTypeDef eepromInit(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
        if (FreeRTOS_CLIRegisterCommand(&xCommandList[i]) != pdPASS) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}