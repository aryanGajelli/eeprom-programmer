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

#include "uart_bulk.h"

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

// CRC helper now implemented in uart_bulk.c

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
        if (uartBulk_hasPendingEvent()) {
            xQueueSendFromISR(uartRxQueue, &rxChunk, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD();
            }
        }
        return;
    }
    if (uartBulk_on_rx(rxChunk.data, rxChunk.length)) {
        if (uartBulk_hasPendingEvent()) {
            UartRxChunk_t wakeChunk;
            wakeChunk.length = 0;
            xQueueSendFromISR(uartRxQueue, &wakeChunk, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD();
            }
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
        if (uartBulk_poll()) {
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
BaseType_t cmd_ping(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    COMMAND_OUTPUT("pong\n");
    return pdFALSE;
}
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

    if (((uint32_t)targetAddr32 + length32) > ((uint32_t)EEPROM_SIZE)) {
        COMMAND_OUTPUT("Flash range out of bounds\r\n");
        return pdFALSE;
    }

    if (!uartBulk_arm((uint16_t)targetAddr32, length32, crc32)) {
        COMMAND_OUTPUT("Unable to arm bulk transfer\r\n");
        return pdFALSE;
    }
    COMMAND_OUTPUT("READY: send %lu raw bytes now\r\n", length32);
    return pdFALSE;
}
/*********************************************************************************************/

BaseType_t cmd_bulkCommit(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    HAL_StatusTypeDef res = uartBulk_commit();
    if (res == HAL_OK) {
        COMMAND_OUTPUT("Bulk image committed to EEPROM\r\n");
    } else if (res == HAL_ERROR) {
        COMMAND_OUTPUT("No buffered image available to commit\r\n");
    } else {
        COMMAND_OUTPUT("Commit failed (HAL status %d)\r\n", (int)res);
    }
    return pdFALSE;
}
/*********************************************************************************************/

BaseType_t cmd_bulkVerify(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    uint32_t imgLen = 0;
    const uint8_t* img = uartBulk_getImage(&imgLen);
    if (img == NULL || imgLen == 0) {
        COMMAND_OUTPUT("No buffered image available to verify\r\n");
        return pdFALSE;
    }
    uint16_t target = uartBulk_getTargetAddr();
    if (target == 0xFFFFu) {
        COMMAND_OUTPUT("Buffered image has no associated target address\r\n");
        return pdFALSE;
    }

    const uint32_t chunk = 256;
    uint32_t mismatches = 0;
    uint32_t firstMismatchAddr = 0;
    uint8_t expected = 0, actual = 0;
    uint8_t readBuf[chunk];

    /* Ensure GPIOs are configured for readback */
    addressToOutput();
    dataToInput();

    for (uint32_t off = 0; off < imgLen; off += chunk) {
        uint16_t toRead = (uint16_t)((imgLen - off) > chunk ? chunk : (imgLen - off));
        eepromReadSection((uint16_t)(target + off), toRead, readBuf);
        for (uint16_t i = 0; i < toRead; ++i) {
            expected = img[off + i];
            actual = readBuf[i];
            if (expected != actual) {
                mismatches++;
                if (mismatches == 1) {
                    firstMismatchAddr = target + off + i;
                }
            }
        }
    }

    if (mismatches == 0) {
        COMMAND_OUTPUT("Verify OK: %lu bytes match at 0x%04x\r\n", imgLen, target);
    } else {
        COMMAND_OUTPUT("Verify FAILED: %lu mismatches, first at 0x%04lx (expected %02x got %02x)\r\n", mismatches,
                       (unsigned long)firstMismatchAddr, (unsigned)img[firstMismatchAddr - target], (unsigned)eepromRead((uint16_t)firstMismatchAddr));
    }
    return pdFALSE;
}

/*********************************************************************************************/

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "ping",
        "ping:\r\n  Responds with pong, used to test connection and latency\r\n",
        cmd_ping,
        0 /* Number of parameters */
    },
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
        "bulkCommit",
        "bulkCommit:\r\n  Commit previously received bulk image from RAM into EEPROM\r\n",
        cmd_bulkCommit,
        0 /* Number of parameters */
    },
    {
        "bulkVerify",
        "bulkVerify:\r\n  Read back EEPROM and compare to buffered image in RAM\r\n",
        cmd_bulkVerify,
        0 /* Number of parameters */
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