#include "debug.h"

#include <backtrace.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp.h"

#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "queue.h"
#endif

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char* pcTaskName) {
    for (int i = 0; i < 100; ++i) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        HAL_Delay(20);
    }
    // printf("Stack overflow for task %s\n", pcTaskName);
    _handleError((char*)pcTaskName, __LINE__);
}

// Note: printf should only be used for printing before RTOS starts, and error
// cases where rtos has probably failed. (If used after rtos starts, it may
// cause errors in calling non-reentrant hal functions)
int _write(int file, char* data, int len) {
    HAL_UART_Transmit(&DEBUG_UART_HANDLE, (uint8_t*)data, len, DEBUG_UART_PRINT_TIMEOUT);
    return len;
}

#ifdef USE_FREERTOS
QueueHandle_t printQueue;
#endif

char isDebugInitialized = 0;
HAL_StatusTypeDef debugInit(void) {
    isDebugInitialized = 1;
#ifdef USE_FREERTOS
    printQueue = xQueueCreate(PRINT_QUEUE_LENGTH, PRINT_QUEUE_STRING_SIZE);
    if (!printQueue) {
        return HAL_ERROR;
    }
#endif

    return HAL_OK;
}

#ifdef USE_FREERTOS
void printTask(void* pvParameters) {
    char buffer[PRINT_QUEUE_STRING_SIZE] = {0};

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        if (xQueueReceive(printQueue, (uint8_t*)buffer, portMAX_DELAY) == pdTRUE) {
            buffer[PRINT_QUEUE_STRING_SIZE - 1] = '\0';
            size_t len = strlen(buffer);
            HAL_UART_Transmit(&DEBUG_UART_HANDLE, (uint8_t*)buffer, len, DEBUG_UART_PRINT_TIMEOUT);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}
#endif

// Reset the debug uart
// This is done to clear the UART in case it is being used by the debug task,
// that way we can send an error message
HAL_StatusTypeDef resetUART() {
    if (HAL_UART_DeInit(&DEBUG_UART_HANDLE) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_UART_Init(&DEBUG_UART_HANDLE) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

void _handleError(char* file, int line) {
    const char errorStringFile[] = "Error!: File ";
    const char errorStringLine[] = " line ";
    char lineNumberString[10];

    const int BACKTRACE_SIZE = 3;
    backtrace_t backtrace[BACKTRACE_SIZE];
    backtrace_unwind(backtrace, BACKTRACE_SIZE);
    char backtraceString[20];

#ifdef USE_FREERTOS
    taskDISABLE_INTERRUPTS();
#endif

    while (resetUART() != HAL_OK);

    HAL_UART_Transmit(&DEBUG_UART_HANDLE, ((uint8_t*)errorStringFile), strlen(errorStringFile), DEBUG_UART_PRINT_TIMEOUT);
    HAL_UART_Transmit(&DEBUG_UART_HANDLE, ((uint8_t*)file), strlen(file), DEBUG_UART_PRINT_TIMEOUT);
    HAL_UART_Transmit(&DEBUG_UART_HANDLE, ((uint8_t*)errorStringLine), strlen(errorStringLine), DEBUG_UART_PRINT_TIMEOUT);
    snprintf(lineNumberString, sizeof(lineNumberString), "%d", line);
    HAL_UART_Transmit(&DEBUG_UART_HANDLE, ((uint8_t*)lineNumberString), strlen(lineNumberString), DEBUG_UART_PRINT_TIMEOUT);

    for (int i = 0; i < BACKTRACE_SIZE; ++i) {
        sprintf(backtraceString, "\n%p - %s@ %p", backtrace[i].function, backtrace[i].name, backtrace[i].address);
        HAL_UART_Transmit(&DEBUG_UART_HANDLE, ((uint8_t*)backtraceString), strlen(backtraceString), DEBUG_UART_PRINT_TIMEOUT);
    }
}