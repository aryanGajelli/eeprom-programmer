#include <stdbool.h>

#include "FreeRTOS.h"
#include "Si5351.h"
#include "bsp.h"
#include "debug.h"
#include "stm32g4xx_hal.h"
#include "task.h"
#include "tim.h"

#define F_CLK 170000000UL

volatile uint32_t Frequency = 0;

Si5351Config_t m_si5351Config = {
    .initialised = false,
    .crystalFreq = SI5351_CRYSTAL_FREQ_25MHZ,
    .crystalLoad = SI5351_CRYSTAL_LOAD_10PF,
    .crystalPPM = 30,
    .plla_configured = false,
    .plla_freq = 0,
    .pllb_configured = false,
    .pllb_freq = 0};

void reg_print(uint8_t reg) {
    uint8_t value;
    read8(reg, &value);
    uprintf("Register 0x%02X: 0x%02X\n", reg, value);
}

void clockProgTask(void* pvParameters) {
    uprintf("Starting clockProgTask\n");
    HAL_StatusTypeDef status = Si5351Init();
    if (status != HAL_OK) {
        uprintf("Si5351Init failed: %d\n", status);
        vTaskDelete(NULL);
    }
    // reg_print(SI5351_REGISTER_0_DEVICE_STATUS);

    // Keep PLL VCO in the 600..900 MHz range (AN619): 25 MHz * 24 = 600 MHz.
    // Use PLLA multiplier 24 to get the lowest VCO allowed (600 MHz),
    // then set multisynth to 900 and R-divider to 128 to produce ~5.2 kHz.
    status = setupPLLInt(SI5351_PLL_A, 30);
    // status = setupPLLInt(SI5351_PLL_B, 36);
    if (status != HAL_OK) {
        uprintf("setupPLLInt failed: %d\n", status);
        vTaskDelete(NULL);
    }

    // Configure CLK0..CLK2 to use a large multisynth divider (900)
    // and an R-divider of 128 to reach the lowest practical output.
    status = setupMultisynth(0, SI5351_PLL_A, 75, 1, 3500);
    if (status != HAL_OK) {
        uprintf("setupMultisynth(0) failed: %d\n", status);
        vTaskDelete(NULL);
    }

    status = setupMultisynth(1, SI5351_PLL_A, 450, 0, 1);
    if (status != HAL_OK) {
        uprintf("setupMultisynth(1) failed: %d\n", status);
        vTaskDelete(NULL);
    }

    status = setupMultisynth(2, SI5351_PLL_A, 90, 1, 2950);
    if (status != HAL_OK) {
        uprintf("setupMultisynth(2) failed: %d\n", status);
        vTaskDelete(NULL);
    }

    // // Apply maximum R-divider to drop the freq further (~128x)
    // status = setupRdiv(0, SI5351_R_DIV_128);
    // if (status != HAL_OK) {
    //     uprintf("setupRdiv(0) failed: %d\n", status);
    //     vTaskDelete(NULL);
    // }
    // status = setupRdiv(1, SI5351_R_DIV_128);
    // if (status != HAL_OK) {
    //     uprintf("setupRdiv(1) failed: %d\n", status);
    //     vTaskDelete(NULL);
    // }
    // status = setupRdiv(2, SI5351_R_DIV_128);
    // if (status != HAL_OK) {
    //     uprintf("setupRdiv(2) failed: %d\n", status);
    //     vTaskDelete(NULL);
    // }

    // Enable outputs and keep them on continuously.
    bool outputsEnabled = true;
    enableOutputs(outputsEnabled);
    while (1) {
        // outputsEnabled = !outputsEnabled;
        // Keep LED lit to show outputs are enabled.
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);

        // if (Frequency < 1000) {
        //     uprintf("Frequency: %.3f Hz\n", Frequency * 1.0f);
        // } else if (Frequency < 1000000) {
        //     uprintf("Frequency: %.3f kHz\n", Frequency / 1000.0f);
        // } else {
        //     uprintf("Frequency: %.6f MHz\n", Frequency / 1000000.0f);
        // }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

HAL_StatusTypeDef timerInit(void) {
    HAL_TIM_Base_Start(&ONE_HZ_TIM_HANDLE);
    HAL_TIM_IC_Start_IT(&CLK_IN_TIM_HANDLE, TIM_CHANNEL_1);

    return HAL_OK;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef* htim) {
    if (htim == &CLK_IN_TIM_HANDLE) {
        Frequency = __HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_1);
    }
}