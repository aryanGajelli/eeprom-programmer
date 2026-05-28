#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>

#include "main.h"

#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "queue.h"
#endif

#define DEBUG_UART_PRINT_TIMEOUT 1000

#ifdef USE_FREERTOS
#define PRINT_QUEUE_LENGTH 20
#define PRINT_QUEUE_STRING_SIZE 128
#define PRINT_QUEUE_SEND_TIMEOUT_TICKS 10

extern QueueHandle_t printQueue;
extern QueueHandle_t uartRxQueue;
#endif  // USE_FREERTOS

#ifdef USE_FREERTOS
extern char isDebugInitialized;
HAL_StatusTypeDef debugInit(void);
HAL_StatusTypeDef uartStartReceiving(UART_HandleTypeDef* huart);

#define uprintf(...)                                                  \
    do {                                                              \
        if (!printQueue || !isDebugInitialized) {                     \
            Error_Handler();                                          \
        }                                                             \
        char _buf[PRINT_QUEUE_STRING_SIZE] = {0};                     \
        snprintf(_buf, PRINT_QUEUE_STRING_SIZE, __VA_ARGS__);         \
        xQueueSend(printQueue, _buf, PRINT_QUEUE_SEND_TIMEOUT_TICKS); \
    } while (0)

#define uprintfISR(...)                                       \
    do {                                                      \
        if (!printQueue || !isDebugInitialized) {             \
            Error_Handler();                                  \
        }                                                     \
        char _buf[PRINT_QUEUE_STRING_SIZE] = {0};             \
        snprintf(_buf, PRINT_QUEUE_STRING_SIZE, __VA_ARGS__); \
        xQueueSendFromISR(printQueue, _buf, NULL);            \
    } while (0)

#endif  // USE_FREERTOS

/*
 * Receive and CLI functions and defines
 *
 */
// This is the size of the buffer used by FreeRTOS+CLI to write command output
// to
// this **NEEDS* to be the same as the print queue string size, as the console
// send function relies on this. This optimizes sending
#define configCOMMAND_INT_MAX_OUTPUT_SIZE PRINT_QUEUE_STRING_SIZE

#define UART_RX_QUEUE_LENGTH 100

#define STR_EQ(a, b, len) (strncmp(a, b, len) == 0)

// Output to the command output buffer, can only be called once per command
// function
#define COMMAND_OUTPUT(...)                                    \
    do {                                                       \
        snprintf(writeBuffer, writeBufferLength, __VA_ARGS__); \
    } while (0)

#define CONCAT(a, b) a##b

void _handleError(char* file, int line);
#define handleError() _handleError(__FILE__, __LINE__)

#endif  // __DEBUG_H__