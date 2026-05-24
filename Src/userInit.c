#include "userInit.h"

#include "bsp.h"
#include "clockProg.h"
#include "debug.h"
#include "cli.h"
#include "Si5351.h"

void userInit(void) {
    if (debugInit() != HAL_OK) {
        handleError();
    }

    if (cliInit() != HAL_OK) {
        handleError();
    }

    if (uartStartReceiving(&DEBUG_UART_HANDLE) != HAL_OK) {
        handleError();
    }

    if (clockProgInit() != HAL_OK) {
        handleError();
    }

    printf("----------------------------------\nFinished User Init\n");
}
