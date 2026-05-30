#include "FreeRTOS.h"
#include "main.h"
#include "stm32g4xx.h"
#include "task.h"
#include "uart_bulk.h"

void mainTask(void* pvParameters) {
    while (1) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        if (uartBulk_isReceiving()) {
            /* faster blink while receiving */
            vTaskDelay(50);
        } else {
            vTaskDelay(250);
        }
    }
}