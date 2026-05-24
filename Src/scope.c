
#include "scope.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "assert.h"
#include "bsp.h"
#include "debug.h"
#include "stm32g4xx.h"
#include "task.h"

volatile uint16_t adcBuffer[ADC_BUFFER_SIZE] = {0};

#define R1 965.
#define R2 1935.
#define RATIO (R2 / (R1 + R2))

#define VREF 3.33

#define normalise(x) ((x) * VREF / (1 << 12) * 1.0 / RATIO)

bool scopeEnabled = false;
void scopeTask(void* pvParameters) {
    while (1) {
        if (scopeEnabled) {
            uprintf("%d,%.d\n", adcBuffer[0], adcBuffer[1]);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*********************************************************************************************/
BaseType_t cmd_scopeEn(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    HAL_ADC_Start_DMA(&ADC_HANDLE, (uint32_t*)adcBuffer, ADC_BUFFER_SIZE);
    scopeEnabled = true;
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_scopeDis(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    HAL_ADC_Stop_DMA(&ADC_HANDLE);
    scopeEnabled = false;
    return pdFALSE;
}
/*********************************************************************************************/

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "scopeEn",
        "scopeEn:\r\n  Enable the scope adc print\r\n",
        cmd_scopeEn,
        0 /* Number of parameters */
    },
    {
        "scopeDis",
        "scopeDis:\r\n  Disable the scope adc print\r\n",
        cmd_scopeDis,
        0 /* Number of parameters */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }};

HAL_StatusTypeDef scopeInit(void) {
    ASSERT_STATUS(HAL_ADCEx_Calibration_Start(&ADC_HANDLE, ADC_SINGLE_ENDED));

    for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
        if (FreeRTOS_CLIRegisterCommand(&xCommandList[i]) != pdPASS) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}