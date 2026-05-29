#include "cli.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "assert.h"
#include "bsp.h"
#include "debug.h"
#include "eeprom.h"
#include "stm32g4xx_hal.h"
#include "task.h"

// Send a CLI string to the uart to be printed. Only for use by the CLI
// buf must be of length PRINT_QUEUE_STRING_SIZE (this is always true for CLI
// output buffer)
#define CONSOLE_SEND(buf)                                            \
    do {                                                             \
        xQueueSend(printQueue, buf, PRINT_QUEUE_SEND_TIMEOUT_TICKS); \
        vTaskDelay(1);                                               \
    } while (0)

QueueHandle_t uartRxQueue;

// Buffer to receive uart characters from the DMA chunk
#define UART_RX_RECV_SIZE (50)
uint8_t uartDMA_rxBuffer[UART_RX_RECV_SIZE] = {'\000'};

typedef struct {
    uint16_t length;
    uint8_t data[UART_RX_RECV_SIZE];
} UartRxChunk_t;

static uint16_t uartLastRxSize = 0;

#define BULK_IMAGE_SIZE (32u * 1024u)

typedef enum {
    BULK_STATE_IDLE = 0,
    BULK_STATE_RECEIVING,
    BULK_STATE_READY_TO_PROGRAM,
    BULK_STATE_ERROR,
} BulkState_t;

typedef struct {
    volatile BulkState_t state;
    uint16_t targetAddr;
    uint32_t expectedLength;
    uint32_t receivedLength;
    uint32_t expectedCrc32;
    uint32_t runningCrc32;
} BulkTransfer_t;

static uint8_t bulkImage[BULK_IMAGE_SIZE];
static volatile BulkTransfer_t bulkTransfer = {
    .state = BULK_STATE_IDLE,
};

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

static uint32_t bulkCrc32Update(uint32_t crc, uint8_t data) {
    crc ^= (uint32_t)data;
    for (int i = 0; i < 8; ++i) {
        if ((crc & 1u) != 0u) {
            crc = (crc >> 1) ^ 0xEDB88320u;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static void bulkTransferReset(void) {
    bulkTransfer.state = BULK_STATE_IDLE;
    bulkTransfer.targetAddr = 0;
    bulkTransfer.expectedLength = 0;
    bulkTransfer.receivedLength = 0;
    bulkTransfer.expectedCrc32 = 0;
    bulkTransfer.runningCrc32 = 0xFFFFFFFFu;
}

static void bulkTransferBegin(uint16_t targetAddr, uint32_t expectedLength, uint32_t expectedCrc32) {
    bulkTransferReset();
    bulkTransfer.targetAddr = targetAddr;
    bulkTransfer.expectedLength = expectedLength;
    bulkTransfer.expectedCrc32 = expectedCrc32;
    bulkTransfer.runningCrc32 = 0xFFFFFFFFu;
    bulkTransfer.state = BULK_STATE_RECEIVING;
}

static void bulkTransferConsumeByte(uint8_t byte) {
    if (bulkTransfer.state != BULK_STATE_RECEIVING) {
        return;
    }

    if (bulkTransfer.receivedLength >= bulkTransfer.expectedLength) {
        bulkTransfer.state = BULK_STATE_ERROR;
        return;
    }

    bulkImage[bulkTransfer.receivedLength] = byte;
    bulkTransfer.receivedLength++;
    bulkTransfer.runningCrc32 = bulkCrc32Update(bulkTransfer.runningCrc32, byte);

    if (bulkTransfer.receivedLength == bulkTransfer.expectedLength) {
        uint32_t imageCrc = bulkTransfer.runningCrc32 ^ 0xFFFFFFFFu;
        if (imageCrc == bulkTransfer.expectedCrc32) {
            bulkTransfer.state = BULK_STATE_READY_TO_PROGRAM;
        } else {
            bulkTransfer.state = BULK_STATE_ERROR;
        }
    }
}

HAL_StatusTypeDef uartStartReceiving(UART_HandleTypeDef* huart) {
    if (huart == &DEBUG_UART_HANDLE) {
        __HAL_UART_FLUSH_DRREGISTER(huart);  // Clear the buffer to prevent overrun
        uartLastRxSize = 0;
        return HAL_UARTEx_ReceiveToIdle_DMA(huart, uartDMA_rxBuffer, UART_RX_RECV_SIZE);
    }
    return HAL_ERROR;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size) {
    if (huart != &DEBUG_UART_HANDLE) {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken;

    xHigherPriorityTaskWoken = pdFALSE;

    if (Size > UART_RX_RECV_SIZE) {
        return;
    }

    UartRxChunk_t rxChunk;
    rxChunk.length = 0;

    if (Size >= uartLastRxSize) {
        rxChunk.length = Size - uartLastRxSize;
        if (rxChunk.length > 0) {
            memcpy(rxChunk.data, &uartDMA_rxBuffer[uartLastRxSize], rxChunk.length);
        }
    } else {
        uint16_t firstChunkLength = UART_RX_RECV_SIZE - uartLastRxSize;
        uint16_t secondChunkLength = Size;

        rxChunk.length = firstChunkLength + secondChunkLength;
        if (firstChunkLength > 0) {
            memcpy(rxChunk.data, &uartDMA_rxBuffer[uartLastRxSize], firstChunkLength);
        }
        if (secondChunkLength > 0) {
            memcpy(&rxChunk.data[firstChunkLength], uartDMA_rxBuffer, secondChunkLength);
        }
    }

    uartLastRxSize = Size;

    if (rxChunk.length == 0) {
        return;
    }

    if (bulkTransfer.state == BULK_STATE_RECEIVING) {
        for (uint16_t i = 0; i < rxChunk.length; ++i) {
            bulkTransferConsumeByte(rxChunk.data[i]);
        }
        return;
    }

    xQueueSendFromISR(uartRxQueue, &rxChunk, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD();
    }
}

#define INPUT_BUFFER_SIZE (100)
#define OUTPUT_BUFFER_SIZE (configCOMMAND_INT_MAX_OUTPUT_SIZE)
static char rxString[INPUT_BUFFER_SIZE];
static int rxIndex = 0;
#ifdef BOARD_NAME
__weak char PS1[] = STRINGIZE(BOARD_NAME) " > ";
#else
__weak char PS1[] = "CLI > ";  // Can override this in project to change PS1
#endif

void cliTask(void* pvParameters) {
    BaseType_t xMoreDataToFollow;
    char* outputBuffer = FreeRTOS_CLIGetOutputBuffer();
    uprintf("CLI Started. Enter command, or help for more info\n");
    uprintf("%s", PS1);
    while (1) {
        if (bulkTransfer.state == BULK_STATE_READY_TO_PROGRAM) {
            uint16_t targetAddr = bulkTransfer.targetAddr;
            uint32_t expectedLength = bulkTransfer.expectedLength;

            uprintf("Bulk RX complete, programming %lu bytes at 0x%04x\r\n", expectedLength, targetAddr);
            if (eepromProgramBuffer(targetAddr, expectedLength, bulkImage) != HAL_OK) {
                uprintf("Bulk programming failed\r\n");
            } else {
                uprintf("Bulk programming complete\r\n");
            }
            bulkTransferReset();
            uprintf("%s", PS1);
            continue;
        }

        if (bulkTransfer.state == BULK_STATE_ERROR) {
            uprintf("Bulk transfer failed (overflow or crc mismatch)\r\n");
            bulkTransferReset();
            uprintf("%s", PS1);
            continue;
        }

        UartRxChunk_t rxChunk;
        if (xQueueReceive(uartRxQueue, &rxChunk, portMAX_DELAY) != pdTRUE) {
            uprintf("Error Receiving from UART Rx Queue\n");
            handleError();
        }
        for (uint16_t i = 0; i < rxChunk.length; i++) {
            char currentChar = (char)rxChunk.data[i];

            if (currentChar == '\n') {
                /* A newline character was received, so the input command string is
                complete and can be processed.  Transmit a line separator, just to
                make the output easier to read. */
                uprintf("\r\n");

                /* The command interpreter is called repeatedly until it returns
                pdFALSE.  See the "Implementing a command" documentation for an
                explanation of why this is. */
                do {
                    /* Send the command string to the command interpreter.  Any
                    output generated by the command interpreter will be placed in the
                    outputBuffer buffer. */
                    xMoreDataToFollow = FreeRTOS_CLIProcessCommand(
                        rxString,          /* The command string.*/
                        outputBuffer,      /* The output buffer. */
                        OUTPUT_BUFFER_SIZE /* The size of the output buffer. */
                    );

                    /* Write the output generated by the command interpreter to the
                    console. */
                    CONSOLE_SEND(outputBuffer);

                } while (xMoreDataToFollow != pdFALSE);

                uprintf("%s", PS1);

                /* All the strings generated by the input command have been sent.
                Processing of the command is complete.  Clear the input string ready
                to receive the next command. */
                rxIndex = 0;
                memset(rxString, 0x00, INPUT_BUFFER_SIZE);
                memset(outputBuffer, 0x00, OUTPUT_BUFFER_SIZE);
            } else {
                /* The if() clause performs the processing after a newline character
                is received.  This else clause performs the processing if any other
                character is received. */

                if (currentChar == '\r') {
                    /* Ignore carriage returns. */
                } else if (currentChar == '\b') {
                    /* Backspace was pressed.  Erase the last character in the input
                    buffer - if there are any. */
                    if (rxIndex > 0) {
                        rxIndex--;
                        rxString[rxIndex] = '\0';
                    }
                } else {
                    /* A character was entered.  It was not a new line, backspace
                    or carriage return, so it is accepted as part of the input and
                    placed into the input buffer.  When a \n is entered the complete
                    string will be passed to the command interpreter. */
                    if (rxIndex < INPUT_BUFFER_SIZE) {
                        rxString[rxIndex] = currentChar;
                        rxIndex++;
                    } else {
                        uprintf("Rx string buffer overflow\n");
                    }
                }
            }
        }
    }
}

#ifdef STATS_TIM_HANDLE
/*
 * Run time stats timer setup
 * A 16 bit timer with clock source APB1 should be configured in cube
 */

uint32_t counterVal = 0;  // store the counter value to help protect againts 16 bit overflow
uint16_t lastCounterVal = 0;

// stat timer frequency is this value times tick freqency
#define STAT_TIMER_TICK_FREQUENCY_MULTIPLIER 20

void configureTimerForRunTimeStats(void) {
    // Compute the right prescaler to set timer frequency
    RCC_ClkInitTypeDef clkconfig;
    uint32_t uwTimclock, uwAPB1Prescaler = 0U;
    uint32_t uwPrescalerValue = 0U;
    uint32_t timerFrequency;
    uint32_t pFLatency;

    /* Get clock configuration */
    HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

    /* Get APB1 prescaler */
    uwAPB1Prescaler = clkconfig.APB1CLKDivider;

    /* Compute timer clock */
    if (uwAPB1Prescaler == RCC_HCLK_DIV1) {
        uwTimclock = HAL_RCC_GetPCLK1Freq();
    } else {
        uwTimclock = 2 * HAL_RCC_GetPCLK1Freq();
    }

    timerFrequency = STAT_TIMER_TICK_FREQUENCY_MULTIPLIER * configTICK_RATE_HZ;

    /* Compute the prescaler value to have TIM5 counter clock equal to desired
     * freqeuncy*/
    uwPrescalerValue = (uint32_t)((uwTimclock / timerFrequency) - 1U);

    __HAL_TIM_SET_PRESCALER(&STATS_TIM_HANDLE, uwPrescalerValue);

    if (HAL_TIM_Base_Start(&STATS_TIM_HANDLE) != HAL_OK) {
        uprintf("Failed to start stats timer\n");
        Error_Handler();
    }
}

uint32_t getRunTimeCounterValue() {
    uint64_t curCounterVal;
    uint16_t val, elapsed;

    portDISABLE_INTERRUPTS();

    val = __HAL_TIM_GET_COUNTER(&STATS_TIM_HANDLE);

    elapsed = val - lastCounterVal;

    counterVal += elapsed;

    lastCounterVal = val;
    curCounterVal = counterVal;

    portENABLE_INTERRUPTS();

    return curCounterVal;
}
#endif

/*********************************************************************************************/
BaseType_t cmd_clearScreen(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    COMMAND_OUTPUT("\033[2J\033[1;1H");
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_heapUsage(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    BaseType_t paramLen;
    const char* param = FreeRTOS_CLIGetParameter(commandString, 1, &paramLen);

    if (STR_EQ(param, "cur", paramLen)) {
        COMMAND_OUTPUT("Current free heap (bytes): %d\n", xPortGetFreeHeapSize());
    } else if (STR_EQ(param, "min", paramLen)) {
        COMMAND_OUTPUT("Minimum free heap (bytes): %d\n", xPortGetMinimumEverFreeHeapSize());
    } else {
        COMMAND_OUTPUT("Unknown parameter\n");
    }

    return pdFALSE;
}
/*********************************************************************************************/
#define TASK_LIST_NUM_BYTES_PER_TASK 50
#define MAX_NUM_TASKS 50
char taskListBuffer[MAX_NUM_TASKS * TASK_LIST_NUM_BYTES_PER_TASK];
BaseType_t cmd_taskList(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    static char* currentStringPointer = NULL;

    if (currentStringPointer == NULL) {
        // We haven't created any output yet, so gather the stats to output
        vTaskList(taskListBuffer);

        // Init string pointer
        currentStringPointer = taskListBuffer;

        // Output the Column headers on the first call
        COMMAND_OUTPUT("Name\tState\tPriority\tFreeStack (min)\tNum\r\n");
        return pdTRUE;
    }

    int charWritten = snprintf(writeBuffer, writeBufferLength, "%s", currentStringPointer);

    if (charWritten < writeBufferLength) {
        // All the string has been written
        currentStringPointer = NULL;
        return pdFALSE;
    } else {
        // Only part of the string was written, advance pointer by write buffer
        // length, subtracting one for the null terminator
        currentStringPointer += (writeBufferLength - 1);
        return pdTRUE;
    }
}
/*********************************************************************************************/
#define STATS_LIST_NUM_BYTES_PER_TASK 50
char statsListBuffer[MAX_NUM_TASKS * STATS_LIST_NUM_BYTES_PER_TASK];
BaseType_t cmd_statsList(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    static char* currentStringPointer = NULL;

    if (currentStringPointer == NULL) {
        // We haven't created any output yet, so gather the stats to output
        vTaskGetRunTimeStats(statsListBuffer);

        // Init string pointer
        currentStringPointer = statsListBuffer;

        // Output the Column headers on the first call
        COMMAND_OUTPUT("%-*s\t%s\t%s\r\n\r\n", configMAX_TASK_NAME_LEN, "Name", "Ticks runtime", "CPU Usage");
        return pdTRUE;
    }

    int charWritten = snprintf(writeBuffer, writeBufferLength, "%s", currentStringPointer);

    if (charWritten < writeBufferLength) {
        // All the string has been written
        currentStringPointer = NULL;
        return pdFALSE;
    } else {
        // Only part of the string was written, advance pointer by write buffer
        // length, subtracting one for the null terminator
        currentStringPointer += (writeBufferLength - 1);
        return pdTRUE;
    }
}
/*********************************************************************************************/
BaseType_t cmd_reset(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    NVIC_SystemReset();
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_bulkLoad(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t targetAddr32 = 0;
    uint32_t length32 = 0;
    uint32_t crc32 = 0;

    if (bulkTransfer.state != BULK_STATE_IDLE) {
        COMMAND_OUTPUT("Bulk transfer already active\r\n");
        return pdFALSE;
    }

    if (!parseUnsignedParameter(commandString, 1, &targetAddr32) ||
        !parseUnsignedParameter(commandString, 2, &length32) ||
        !parseUnsignedParameter(commandString, 3, &crc32)) {
        COMMAND_OUTPUT("Usage: bulkLoad <flashAddr> <length> <crc32>\r\n");
        return pdFALSE;
    }

    if (targetAddr32 > UINT16_MAX || length32 == 0 || length32 > BULK_IMAGE_SIZE) {
        COMMAND_OUTPUT("Range error\r\n");
        return pdFALSE;
    }

    if ((targetAddr32 & 0x0FFFu) != 0u || (length32 & 0x0FFFu) != 0u) {
        COMMAND_OUTPUT("Address and length must be 4 kB aligned\r\n");
        return pdFALSE;
    }

    if (((uint32_t)targetAddr32 + length32) > ((uint32_t)EEPROM_SIZE + 1u)) {
        COMMAND_OUTPUT("Flash range out of bounds\r\n");
        return pdFALSE;
    }

    bulkTransferBegin((uint16_t)targetAddr32, length32, crc32);
    COMMAND_OUTPUT("READY: send %lu raw bytes now\r\n", length32);
    return pdFALSE;
}
/*********************************************************************************************/

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "heap",
        "heap <min|cur>:\r\n  Outputs the <min|cur> free heap space\r\n",
        cmd_heapUsage,
        1 /* Number of parameters */
    },
    {
        "stats",
        "stats:\r\n  Outputs the freeRTOS run time stats\r\n",
        cmd_statsList,
        0 /* Number of parameters */
    },
    {
        "taskList",
        "taskList:\r\n  Outputs the freeRTOS task list and stats\r\n",
        cmd_taskList,
        0 /* Number of parameters */
    },
    {
        "clear",
        "clear:\r\n  Clear the screen\r\n",
        cmd_clearScreen,
        0 /* Number of parameters */
    },
    {
        "reset",
        "reset:\r\n  Reset the processor\r\n",
        cmd_reset,
        0 /* Number of parameters */
    },
    {
        "bulkLoad",
        "bulkLoad <flashAddr> <length> <crc32>:\r\n  Receive raw bytes into RAM, verify CRC32, then program EEPROM\r\n",
        cmd_bulkLoad,
        3 /* Number of parameters */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }};

HAL_StatusTypeDef cliInit(void) {
    uartRxQueue = xQueueCreate(UART_RX_QUEUE_LENGTH, sizeof(UartRxChunk_t));
    if (!uartRxQueue) {
        return HAL_ERROR;
    }

    /* Register all commands */
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
        if (FreeRTOS_CLIRegisterCommand(&xCommandList[i]) != pdPASS) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}