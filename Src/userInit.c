#include "userInit.h"

#include "bsp.h"
#include "clockProg.h"
#include "debug.h"

void userInit(void) {
    if (debugInit() != HAL_OK) {
        handleError();
    }

    if (uartStartReceiving(&DEBUG_UART_HANDLE) != HAL_OK) {
        handleError();
    }

    if (timerInit() != HAL_OK) {
        handleError();
    }

    printf("----------------------------------\nFinished User Init\n");
}
