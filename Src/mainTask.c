#include "FreeRTOS.h"
#include "task.h"

void mainTask(void* pvParameters) {
    while (1) {
        vTaskDelay(10);
    }
}