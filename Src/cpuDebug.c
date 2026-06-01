

#include "cpuDebug.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "debug.h"
#include "main.h"
#include "scope.h"
#include "stm32g4xx.h"
#include "task.h"

volatile bool cpuDebugEnabled = false;

GPIO_TypeDef* addrPorts[] = {
    A0_GPIO_Port, A1_GPIO_Port, A2_GPIO_Port, A3_GPIO_Port,
    A4_GPIO_Port, A5_GPIO_Port, A6_GPIO_Port, A7_GPIO_Port,
    A8_GPIO_Port, A9_GPIO_Port, A10_GPIO_Port, A11_GPIO_Port,
    A12_GPIO_Port, A13_GPIO_Port, A14_GPIO_Port, A15_GPIO_Port};

uint16_t addrPins[] = {
    A0_Pin, A1_Pin, A2_Pin, A3_Pin,
    A4_Pin, A5_Pin, A6_Pin, A7_Pin,
    A8_Pin, A9_Pin, A10_Pin, A11_Pin,
    A12_Pin, A13_Pin, A14_Pin, A15_Pin};

GPIO_TypeDef* dataPorts[] = {
    D0_GPIO_Port, D1_GPIO_Port, D2_GPIO_Port, D3_GPIO_Port,
    D4_GPIO_Port, D5_GPIO_Port, D6_GPIO_Port, D7_GPIO_Port};

uint16_t dataPins[] = {
    D0_Pin, D1_Pin, D2_Pin, D3_Pin,
    D4_Pin, D5_Pin, D6_Pin, D7_Pin};

volatile uint32_t lastClockTime = 0;
volatile uint16_t count = 0;
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == CLK_Pin) {
        // if (HAL_GPIO_ReadPin(CLK_GPIO_Port, CLK_Pin) == GPIO_PIN_RESET) return;
        if (!cpuDebugEnabled) return;
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        // HAL_Delay(1);
        
        uint16_t addr = 0;
        uint8_t data = 0;
        uint8_t rw = (HAL_GPIO_ReadPin(RW_GPIO_Port, RW_Pin) == GPIO_PIN_SET) ? 'r' : 'W';
        for (int i = 0; i < 16; i++) {
            if (HAL_GPIO_ReadPin(addrPorts[i], addrPins[i]) == GPIO_PIN_SET) {
                addr |= (1 << i);
            }
        }
        for (int i = 0; i < 8; i++) {
            if (HAL_GPIO_ReadPin(dataPorts[i], dataPins[i]) == GPIO_PIN_SET) {
                data |= (1 << i);
            }
        }
        count++;
        // if (HAL_GetTick() - lastClockTime < 200) return;
        // lastClockTime = HAL_GetTick();
        if (count == 0)
            uprintfISR("\n%d %04x %c %02x\n", count, addr, rw, data);
        else
            uprintfISR("%d %04x %c %02x\n", count, addr, rw, data);
    }
}

/*********************************************************************************************/
BaseType_t cmd_cpuDebugEn(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    scopeEnabled = false;
    count = 0;
    cpuDebugEnabled = true;

    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_cpuDebugDis(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    cpuDebugEnabled = false;
    return pdFALSE;
}
/*********************************************************************************************/

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "cpuDebugEn",
        "cpuDebugEn:\r\n  Enable the cpu debug\r\n",
        cmd_cpuDebugEn,
        0 /* Number of parameters */
    },
    {
        "cpuDebugDis",
        "cpuDebugDis:\r\n  Disable the cpu debug\r\n",
        cmd_cpuDebugDis,
        0 /* Number of parameters */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }};

HAL_StatusTypeDef cpuDebugInit(void) {
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
        if (FreeRTOS_CLIRegisterCommand(&xCommandList[i]) != pdPASS) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}