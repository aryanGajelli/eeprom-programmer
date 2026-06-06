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
#include "stm32g4xx_ll_gpio.h"
#include "task.h"

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

#define PIN_SHIFT(pin) (__builtin_ctz((unsigned)(pin)))
#define GPIO_MODER_PIN_MASK(pin) (0x3u << (PIN_SHIFT(pin) * 2))
#define GPIO_MODER_PIN_VALUE(pin, mode) ((mode) << (PIN_SHIFT(pin) * 2))

#define ADDRESS_GPIOC_MASK (A0_Pin | A1_Pin | A2_Pin | A3_Pin | A4_Pin | A8_Pin | A15_Pin)
#define ADDRESS_GPIOA_MASK (A5_Pin | A6_Pin | A9_Pin | A10_Pin | A14_Pin)
#define ADDRESS_GPIOB_MASK (A7_Pin | A11_Pin | A12_Pin | A13_Pin)
#define DATA_GPIOC_MASK (D0_Pin | D1_Pin | D2_Pin | D6_Pin | D7_Pin)
#define DATA_GPIOA_MASK (D5_Pin)
#define DATA_GPIOD_MASK (D3_Pin)
#define DATA_GPIOB_MASK (D4_Pin)

#define SET_RESET_ON_COND(cond, GPIOx, GPIO_Pin) (cond ? SET(GPIOx, GPIO_Pin) : RESET(GPIOx, GPIO_Pin))
#define ADDRESS_BIT_TO_PIN(addr, bit, pin) ((uint32_t)(0u - (((addr) >> (bit)) & 1u)) & (pin))
#define DATA_BIT_TO_PIN(data, bit, pin) ((uint32_t)(0u - (((data) >> (bit)) & 1u)) & (pin))
#define READ_PIN_TO_DATA(idr, pin, dataBit) ((uint8_t)((((idr) & (pin)) >> PIN_SHIFT(pin)) << (dataBit)))

#define MAX_POLLING_TIMEOUT_US 101000
#define MAX_SECTION_BUFFER 2048
#define BYTES_PER_LINE 16
#define MAX_SECTION_DATA 16
#define INVALID 0x55FF

#define DATA_GPIOC_MODER_MASK (GPIO_MODER_PIN_MASK(D0_Pin) | GPIO_MODER_PIN_MASK(D1_Pin) | \
                               GPIO_MODER_PIN_MASK(D2_Pin) | GPIO_MODER_PIN_MASK(D6_Pin) | \
                               GPIO_MODER_PIN_MASK(D7_Pin))
#define DATA_GPIOC_MODER_OUTPUT (GPIO_MODER_PIN_VALUE(D0_Pin, LL_GPIO_MODE_OUTPUT) | \
                                 GPIO_MODER_PIN_VALUE(D1_Pin, LL_GPIO_MODE_OUTPUT) | \
                                 GPIO_MODER_PIN_VALUE(D2_Pin, LL_GPIO_MODE_OUTPUT) | \
                                 GPIO_MODER_PIN_VALUE(D6_Pin, LL_GPIO_MODE_OUTPUT) | \
                                 GPIO_MODER_PIN_VALUE(D7_Pin, LL_GPIO_MODE_OUTPUT))
#define DATA_GPIOA_MODER_MASK (GPIO_MODER_PIN_MASK(D5_Pin))
#define DATA_GPIOA_MODER_OUTPUT (GPIO_MODER_PIN_VALUE(D5_Pin, LL_GPIO_MODE_OUTPUT))
#define DATA_GPIOD_MODER_MASK (GPIO_MODER_PIN_MASK(D3_Pin))
#define DATA_GPIOD_MODER_OUTPUT (GPIO_MODER_PIN_VALUE(D3_Pin, LL_GPIO_MODE_OUTPUT))
#define DATA_GPIOB_MODER_MASK (GPIO_MODER_PIN_MASK(D4_Pin))
#define DATA_GPIOB_MODER_OUTPUT (GPIO_MODER_PIN_VALUE(D4_Pin, LL_GPIO_MODE_OUTPUT))

#define PULSE_WRITE_PIN()        \
    RESET(WE_GPIO_Port, WE_Pin); \
    delay_cycles(7);             \
    SET(WE_GPIO_Port, WE_Pin)

typedef struct {
    uint32_t addrDir;
    uint32_t dataDir;
} EEPROMConfig_t;

EEPROMConfig_t eepromConfig = {
    .addrDir = MODE_INPUT,
    .dataDir = MODE_INPUT};

#define repeat(instruction, num)         \
    __asm volatile(                      \
        ".rept " #num "\n\t" instruction \
        "\n\t"                           \
        ".endr\n\t")
// delay for N cycles
#define delay_cycles(cycles) repeat("nop", cycles)

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

inline void dataToOutput(void) {
    D0_D1_D2_D6_D7_GPIO_Port->MODER = (D0_D1_D2_D6_D7_GPIO_Port->MODER & ~DATA_GPIOC_MODER_MASK) |
                                      DATA_GPIOC_MODER_OUTPUT;
    D5_GPIO_Port->MODER = (D5_GPIO_Port->MODER & ~DATA_GPIOA_MODER_MASK) | DATA_GPIOA_MODER_OUTPUT;
    D3_GPIO_Port->MODER = (D3_GPIO_Port->MODER & ~DATA_GPIOD_MODER_MASK) | DATA_GPIOD_MODER_OUTPUT;
    D4_GPIO_Port->MODER = (D4_GPIO_Port->MODER & ~DATA_GPIOB_MODER_MASK) | DATA_GPIOB_MODER_OUTPUT;

    eepromConfig.dataDir = MODE_OUTPUT;
}

inline void dataToInput(void) {
    D0_D1_D2_D6_D7_GPIO_Port->MODER &= ~DATA_GPIOC_MODER_MASK;
    D5_GPIO_Port->MODER &= ~DATA_GPIOA_MODER_MASK;
    D3_GPIO_Port->MODER &= ~DATA_GPIOD_MODER_MASK;
    D4_GPIO_Port->MODER &= ~DATA_GPIOB_MODER_MASK;

    eepromConfig.dataDir = MODE_INPUT;
}

static inline void setAddress(uint16_t addr) {
    // Pack the address into per-port bitmasks and update each port with one BSRR write.
    const uint32_t gpioCValue = ADDRESS_BIT_TO_PIN(addr, 0, A0_Pin) | ADDRESS_BIT_TO_PIN(addr, 1, A1_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 2, A2_Pin) | ADDRESS_BIT_TO_PIN(addr, 3, A3_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 4, A4_Pin) | ADDRESS_BIT_TO_PIN(addr, 8, A8_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 15, A15_Pin);
    const uint32_t gpioAValue = ADDRESS_BIT_TO_PIN(addr, 5, A5_Pin) | ADDRESS_BIT_TO_PIN(addr, 6, A6_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 9, A9_Pin) | ADDRESS_BIT_TO_PIN(addr, 10, A10_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 14, A14_Pin);
    const uint32_t gpioBValue = ADDRESS_BIT_TO_PIN(addr, 7, A7_Pin) | ADDRESS_BIT_TO_PIN(addr, 11, A11_Pin) |
                                ADDRESS_BIT_TO_PIN(addr, 12, A12_Pin) | ADDRESS_BIT_TO_PIN(addr, 13, A13_Pin);

    GPIOC->BSRR = gpioCValue | ((ADDRESS_GPIOC_MASK & ~gpioCValue) << 16);
    GPIOA->BSRR = gpioAValue | ((ADDRESS_GPIOA_MASK & ~gpioAValue) << 16);
    GPIOB->BSRR = gpioBValue | ((ADDRESS_GPIOB_MASK & ~gpioBValue) << 16);
}

static inline uint8_t eepromReadRaw() {
    uint32_t idrD = D3_GPIO_Port->IDR;
    uint32_t idrB = D4_GPIO_Port->IDR;
    uint32_t idrC = D0_D1_D2_D6_D7_GPIO_Port->IDR;
    uint32_t idrA = D5_GPIO_Port->IDR;
    return ((idrC & D0_Pin) ? 0x01 : 0x00) |
           ((idrC & D1_Pin) ? 0x02 : 0x00) |
           ((idrC & D2_Pin) ? 0x04 : 0x00) |
           ((idrD & D3_Pin) ? 0x08 : 0x00) |
           ((idrB & D4_Pin) ? 0x10 : 0x00) |
           ((idrA & D5_Pin) ? 0x20 : 0x00) |
           ((idrC & D6_Pin) ? 0x40 : 0x00) |
           ((idrC & D7_Pin) ? 0x80 : 0x00);
}

static inline void eepromWriteRaw(uint16_t addr, uint8_t data) {
    setAddress(addr);
    const uint32_t gpioCValue = DATA_BIT_TO_PIN(data, 0, D0_Pin) | DATA_BIT_TO_PIN(data, 1, D1_Pin) |
                                DATA_BIT_TO_PIN(data, 2, D2_Pin) | DATA_BIT_TO_PIN(data, 6, D6_Pin) |
                                DATA_BIT_TO_PIN(data, 7, D7_Pin);
    const uint32_t gpioAValue = DATA_BIT_TO_PIN(data, 5, D5_Pin);
    const uint32_t gpioDValue = DATA_BIT_TO_PIN(data, 3, D3_Pin);
    const uint32_t gpioBValue = DATA_BIT_TO_PIN(data, 4, D4_Pin);

    GPIOC->BSRR = gpioCValue | ((DATA_GPIOC_MASK & ~gpioCValue) << 16);
    GPIOA->BSRR = gpioAValue | ((DATA_GPIOA_MASK & ~gpioAValue) << 16);
    GPIOD->BSRR = gpioDValue | ((DATA_GPIOD_MASK & ~gpioDValue) << 16);
    GPIOB->BSRR = gpioBValue | ((DATA_GPIOB_MASK & ~gpioBValue) << 16);
}

void dataPolling(const uint8_t expectedData) {
    uint32_t startTime = DWT->CYCCNT;
    RESET(OE_GPIO_Port, OE_Pin);
    dataToInput();
    const uint32_t expectedBit = (expectedData & 0x80u) ? D7_Pin : 0u;
    while ((DWT->CYCCNT - startTime) < US_TO_CYCLES(MAX_POLLING_TIMEOUT_US) &&
           ((D7_GPIO_Port->IDR & D7_Pin) != expectedBit)) {
    }

    SET(OE_GPIO_Port, OE_Pin);
    dataToOutput();
}

/**
 * @brief Erase a 4Kb sector, only the A15-A12 address lines are used for sector selection, the rest are don't care. So the sectorAddr should be aligned to 0x1000 and only the bits A15-A12 are used.
 * @param sectorAddr The address of the sector to erase
 *                   should be aligned to 0x1000 and only the bits A15-A12 are used,
 *                   the rest are don't care and are stripped
 */
void sectorErase(uint16_t sectorAddr) {
    eepromWriteRaw(0x5555, 0xAA);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x2AAA, 0x55);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0x80);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0xAA);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x2AAA, 0x55);
    PULSE_WRITE_PIN();

    eepromWriteRaw(sectorAddr & 0xF000, 0x30);
    PULSE_WRITE_PIN();

    dataPolling(0x30);
    HAL_Delay(26);  // Sector erase can take up to 25ms according to the datasheet, add some extra margin
}

void chipErase(void) {
    eepromWriteRaw(0x5555, 0xAA);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x2AAA, 0x55);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0x80);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0xAA);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x2AAA, 0x55);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0x10);
    PULSE_WRITE_PIN();

    dataPolling(0x10);
    // IDK why the data polling isn't working
    HAL_Delay(101);  // Chip erase can take up to 100ms according to the datasheet, add some extra margin
}

void eepromWrite(const uint16_t addr, const uint8_t data) {
    eepromWriteRaw(0x5555, 0xAA);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x2AAA, 0x55);
    PULSE_WRITE_PIN();

    eepromWriteRaw(0x5555, 0xA0);
    PULSE_WRITE_PIN();

    eepromWriteRaw(addr, data);
    PULSE_WRITE_PIN();

    dataPolling(data);
}

uint8_t sectorBuffer[SECTOR_SIZE] = {0};

HAL_StatusTypeDef eepromProgramBuffer(uint16_t startAddr, uint32_t length, const uint8_t* buffer) {
    if (buffer == NULL) {
        return HAL_ERROR;
    }

    if (length == 0) {
        return HAL_OK;
    }

    if (((uint32_t)startAddr + length - 1u) > ((uint32_t)MAX_ADDRESS)) {
        return HAL_ERROR;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        addressToOutput();
    }

    uint32_t firstSector = (uint32_t)startAddr & 0xF000u;
    uint32_t lastSector = ((uint32_t)startAddr + length - 1u) & 0xF000u;

    if (length >= EEPROM_SIZE - SECTOR_SIZE) {
        if (eepromConfig.dataDir != MODE_OUTPUT) {
            dataToOutput();
        }
        uprintf("Erasing entire chip ... ");
        vTaskDelay(pdMS_TO_TICKS(2));  // Small delay to allow the print to flush before starting the long erase operation
        chipErase();
    } else {
        uprintf("Checking sectors to erase ... \n");
        vTaskDelay(pdMS_TO_TICKS(1));

        for (uint32_t sector = firstSector; sector <= lastSector; sector += SECTOR_SIZE) {
            dataToInput();

            eepromReadSection((uint16_t)sector, SECTOR_SIZE, sectorBuffer);
            uint32_t sectorStart = (sector < startAddr) ? startAddr : sector;
            uint32_t sectorEnd = ((sector + SECTOR_SIZE) > ((uint32_t)startAddr + length)) ? ((uint32_t)startAddr + length)
                                                                                           : (sector + SECTOR_SIZE);

            // Only erase if any byte in the overlapping range would need a 0->1 transition.
            for (uint32_t addr = sectorStart; addr < sectorEnd; ++addr) {
                uint32_t bufferIndex = addr - (uint32_t)startAddr;
                uint32_t sectorIndex = addr - sector;

                if ((sectorBuffer[sectorIndex] & buffer[bufferIndex]) != buffer[bufferIndex]) {
                    uprintf("Erasing sectors ... 0x%04lx\n", sector);
                    vTaskDelay(pdMS_TO_TICKS(1));  // Small delay to allow the print to flush before starting the long erase operation
                    dataToOutput();
                    sectorErase((uint16_t)sector);
                    break;
                }
            }
        }
    }
    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }
    uprintf("Done\nProgramming ... ");
    vTaskDelay(pdMS_TO_TICKS(2));  // Small delay to allow the print to flush before starting the long erase operation
    taskENTER_CRITICAL();
    uint32_t cycles = 0;
    uint32_t _mc_start = DWT->CYCCNT;

    for (uint32_t i = 0; i < length; ++i) {
        eepromWrite(startAddr + i, buffer[i]);
    }
    uint32_t _mc_end = DWT->CYCCNT;
    cycles = _mc_end - _mc_start;
    taskEXIT_CRITICAL();

    uprintf("Done (cycles: %lu = %.3f ms)\n", cycles, cycles / (F_CLK / 1000.0f));
    return HAL_OK;
}

uint8_t eepromRead(const uint16_t addr) {
    // Set the address
    setAddress(addr);
    RESET(OE_GPIO_Port, OE_Pin);
    // Enable output enable
    // small delay for output to stabilize at 170Mhz, 1 nop takes 1 cycle = 5.88ns, 7 nops = 41.16ns > 35ns which is T_OE
    delay_cycles(5);
    uint8_t data = eepromReadRaw();
    // Disable output enable
    SET(OE_GPIO_Port, OE_Pin);
    return data;
}

void eepromReadSection(const uint16_t startAddr, const uint16_t length, uint8_t* buffer) {
    RESET(OE_GPIO_Port, OE_Pin);
    for (uint16_t i = 0; i < length; i++) {
        setAddress(startAddr + i);
        delay_cycles(6);
        buffer[i] = eepromReadRaw();
    }
    SET(OE_GPIO_Port, OE_Pin);
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
/*********************************************************************************************/
BaseType_t cmd_eepromEn(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    addressToOutput();
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_eepromDis(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    MX_GPIO_Init();
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

    if (addr > MAX_ADDRESS) {
        COMMAND_OUTPUT("Address out of range, got %lu !< %u\r\n", addr, MAX_ADDRESS);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }

    uint8_t data = 0;
    taskENTER_CRITICAL();
    uint32_t cycles = 0;
    MEASURE_EXPR_CYCLES(
        eepromReadSection(addr, 1, &data),
        cycles);
    taskEXIT_CRITICAL();

    uprintf("%04lx = %02x  (cycles: %lu = %.3f us)\n", addr, data, cycles, cycles / (F_CLK / 1000000.0f));

    return pdFALSE;
}

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
    if (bytesToRead % BYTES_PER_LINE != 0) {
        bytesToRead += (BYTES_PER_LINE - (bytesToRead % BYTES_PER_LINE));
    }

    if (startAddr > MAX_ADDRESS || endAddr > MAX_ADDRESS) {
        COMMAND_OUTPUT("Address out of range, got %u or %u !< %u\r\n", startAddr, endAddr, MAX_ADDRESS);
        return pdFALSE;
    }

    if (bytesToRead > MAX_SECTION_BUFFER) {
        COMMAND_OUTPUT("Too many bytes to read, got %u, max is %u\r\n", bytesToRead, MAX_SECTION_BUFFER);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        taskENTER_CRITICAL();
        uint32_t cycles = 0;
        MEASURE_EXPR_CYCLES(
            dataToInput(),
            cycles);
        taskEXIT_CRITICAL();
        uprintf("dataToInput cycles: %lu = %.3f us\n", cycles, cycles / (F_CLK / 1000000.0f));
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

    if (addr > MAX_ADDRESS) {
        COMMAND_OUTPUT("Address out of range, got %u !< %u\r\n", addr, MAX_ADDRESS);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
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
        taskENTER_CRITICAL();
        uint32_t cycles = 0;
        MEASURE_EXPR_CYCLES(
            dataToOutput(),
            cycles);
        data = eepromRead(addr);
        taskEXIT_CRITICAL();
        uprintf("dataToOutput cycles: %lu = %.3f us\n", cycles, cycles / (F_CLK / 1000000.0f));
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
    uint32_t data32[MAX_SECTION_DATA] = {0};

    if (!parseUnsignedParameter(commandString, 1, &addr32)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    uint8_t dataCount = 0;
    for (int i = 0; i < MAX_SECTION_DATA; i++) {
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

    if ((addr + dataCount - 1u) > MAX_ADDRESS) {
        COMMAND_OUTPUT("Address out of range, got %u + %u - 1 !< %u\r\n", addr, dataCount, MAX_ADDRESS);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_INPUT) {
        dataToInput();
    }
    uint8_t currentData[MAX_SECTION_DATA] = {0};
    eepromReadSection(addr, dataCount, currentData);
    for (int i = 0; i < dataCount; i++) {
        uint8_t data = (uint8_t)data32[i];
        if (currentData[i] == data) {
            COMMAND_OUTPUT("Data at address %04x is already %02x, no need to write\r\n", addr + i, data);
            data32[i] = INVALID;
            continue;
        }

        // can only write 1 bit to 0 since this is flash
        if ((currentData[i] & data) != data) {
            COMMAND_OUTPUT("Cannot write 1 to 0 at address %04x, current value is %02x\r\n", addr + i, currentData[i]);
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
        // if (data32[i] == INVALID) {
        //     continue;
        // }
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
/*********************************************************************************************/
BaseType_t cmd_sectorErase(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t addr32 = 0;

    if (!parseUnsignedParameter(commandString, 1, &addr32)) {
        COMMAND_OUTPUT("Invalid address parameter\r\n");
        return pdFALSE;
    }

    uint16_t addr = (uint16_t)addr32;

    if (addr > MAX_ADDRESS) {
        COMMAND_OUTPUT("Address out of range, got %u !< %u\r\n", addr, MAX_ADDRESS);
        return pdFALSE;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }

    uprintf("Erasing sector at addresses %04x...%04x\n", addr & 0xF000, (addr & 0xF000) + 0x1000 - 1);
    vTaskDelay(pdMS_TO_TICKS(2));  // Small delay to allow the print to flush before starting the long erase operation
    uint32_t cycles = 0;
    taskENTER_CRITICAL();
    MEASURE_EXPR_CYCLES(
        sectorErase(addr),
        cycles);
    taskEXIT_CRITICAL();

    uprintf("cycles: %lu = %.3f us\n", cycles, cycles / (F_CLK / 1000000.0f));
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_chipErase(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    if (eepromConfig.addrDir != MODE_OUTPUT) {
        COMMAND_OUTPUT("EEPROM mode not initialized, call eepromEn first\r\n");
        return pdFALSE;
    }

    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }

    uprintf("Erasing chip...\n");
    uint32_t cycles = 0;
    taskENTER_CRITICAL();
    MEASURE_EXPR_CYCLES(
        chipErase(),
        cycles);
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
    {
        "sectorErase",
        "sectorErase <sectorAddress>:\r\n  Erase the specified sector, sectorAddress will be truncated with 0xF000\r\n"
        "  so only the bits A15-A12 are used for sector selection\r\n",
        cmd_sectorErase,
        1 /* Number of parameters */
    },
    {
        "chipErase",
        "chipErase:\r\n  Erase the entire chip\r\n",
        cmd_chipErase,
        0 /* Number of parameters */
    },
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

HAL_StatusTypeDef eepromWriteSection(uint16_t startAddr, uint32_t length, const uint8_t* buffer) {
    if (buffer == NULL) {
        return HAL_ERROR;
    }

    if (length == 0) {
        return HAL_OK;
    }

    if (((uint32_t)startAddr + length - 1u) > ((uint32_t)MAX_ADDRESS)) {
        return HAL_ERROR;
    }

    if (eepromConfig.addrDir != MODE_OUTPUT) {
        addressToOutput();
    }

    if (eepromConfig.dataDir != MODE_OUTPUT) {
        dataToOutput();
    }

    for (uint32_t i = 0; i < length; ++i) {
        eepromWrite((uint16_t)((uint32_t)startAddr + i), buffer[i]);
    }

    return HAL_OK;
}