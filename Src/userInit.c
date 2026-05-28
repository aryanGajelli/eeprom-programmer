#include "userInit.h"

#include "Si5351.h"
#include "bsp.h"
#include "cli.h"
#include "clockProg.h"
#include "debug.h"
#include "scope.h"
#include "cpuDebug.h"
#include "eeprom.h"

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

    if (scopeInit() != HAL_OK) {
        handleError();
    }

    if (cpuDebugInit() != HAL_OK) {
        handleError();
    }

    if (eepromInit() != HAL_OK) {
        handleError();
    }

    printf("----------------------------------\nFinished User Init\n");
}
