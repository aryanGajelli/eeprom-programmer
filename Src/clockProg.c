#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "Si5351.h"
#include "assert.h"
#include "bsp.h"
#include "debug.h"
#include "stm32g4xx_hal.h"
#include "task.h"
#include "tim.h"



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

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef* htim) {
    if (htim == &CLK_IN_TIM_HANDLE) {
        Frequency = __HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_1);
    }
}

/*********************************************************************************************/
BaseType_t cmd_clk_init(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    if (Si5351Init() != HAL_OK) {
        COMMAND_OUTPUT("Failed to initialize Si5351\n");
        return pdFALSE;
    }

    // Keep PLL VCO in the 600..900 MHz range (AN619): 25 MHz * 24 = 600 MHz.
    // Use PLLA multiplier 24 to get the lowest VCO allowed (600 MHz),
    // then set multisynth to 900 and R-divider to 128 to produce ~5.2 kHz.
    if (setupPLLInt(SI5351_PLL_A, 30) != HAL_OK) {
        COMMAND_OUTPUT("Failed to set up PLL A\n");
        return pdFALSE;
    }

    // CLK0: 15 MHz = (25MHz + small offset) * 30 / (49 + 999 / 1000)
    if (setupMultisynth(0, SI5351_PLL_A, 49, 999, 1000) != HAL_OK) {
        COMMAND_OUTPUT("Failed to set up multisynth 0\n");
        return pdFALSE;
    }

    // if (setupRdiv(0, SI5351_R_DIV_128) != HAL_OK) {
    //     COMMAND_OUTPUT("Failed to set up R-divider for CLK0\n");
    //     return pdFALSE;
    // }

    COMMAND_OUTPUT("Clocks initialized successfully\n");
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_enable_clocks(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    enableOutputs(true);
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_disable_clocks(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    enableOutputs(false);
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_frequency(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    if (Frequency < 1000) {
        COMMAND_OUTPUT("Frequency: %.3f Hz\n", Frequency * 1.0f);
    } else if (Frequency < 1000000) {
        COMMAND_OUTPUT("Frequency: %.3f kHz\n", Frequency / 1000.0f);
    } else {
        COMMAND_OUTPUT("Frequency: %.3f MHz\n", Frequency / 1000000.0f);
    }
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_set_pllA(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    BaseType_t paramLen;
    const char* prm_mul = FreeRTOS_CLIGetParameter(commandString, 1, &paramLen);
    const char* prm_num = FreeRTOS_CLIGetParameter(commandString, 2, &paramLen);
    const char* prm_div = FreeRTOS_CLIGetParameter(commandString, 3, &paramLen);

    uint32_t mul = atol(prm_mul);
    uint32_t num = atol(prm_num);
    uint32_t div = atol(prm_div);

    if (setupPLL(SI5351_PLL_A, mul, num, div) != HAL_OK) {
        COMMAND_OUTPUT("Failed to set PLL A configuration to %lu/%lu/%lu\n", mul, num, div);
    }

    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_set_multisynth(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    BaseType_t paramLen;
    const char* prm_ch = FreeRTOS_CLIGetParameter(commandString, 1, &paramLen);
    const char* prm_mul = FreeRTOS_CLIGetParameter(commandString, 2, &paramLen);
    const char* prm_num = FreeRTOS_CLIGetParameter(commandString, 3, &paramLen);
    const char* prm_div = FreeRTOS_CLIGetParameter(commandString, 4, &paramLen);

    uint32_t ch = atol(prm_ch);
    uint32_t mul = atol(prm_mul);
    uint32_t num = atol(prm_num);
    uint32_t div = atol(prm_div);

    if (setupMultisynth(ch, SI5351_PLL_A, mul, num, div) != HAL_OK) {
        COMMAND_OUTPUT("Failed to set multisynth %lu configuration to %lu/%lu/%lu\n", ch, mul, num, div);
    }

    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_start_pwm(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    BaseType_t paramLen;
    const char* prm_freq = FreeRTOS_CLIGetParameter(commandString, 1, &paramLen);
    uint32_t freq = atol(prm_freq);

    if (freq == 0) {
        HAL_TIM_PWM_Stop(&PWM_TIM_HANDLE, TIM_CHANNEL_1);
        return pdFALSE;
    }

    int32_t ARR = F_CLK / freq - 1;
    if (ARR < 0) {
        ARR = 0;
    }

    __HAL_TIM_SET_AUTORELOAD(&PWM_TIM_HANDLE, (uint32_t)ARR);
    __HAL_TIM_SET_COMPARE(&PWM_TIM_HANDLE, TIM_CHANNEL_1, (uint32_t)(ARR / 2));
    // __HAL_TIM_SET_COUNTER(&PWM_TIM_HANDLE, 0);
    HAL_TIM_GenerateEvent(&PWM_TIM_HANDLE, TIM_EVENTSOURCE_UPDATE);
    HAL_TIM_PWM_Start(&PWM_TIM_HANDLE, TIM_CHANNEL_1);
    return pdFALSE;
}
/*********************************************************************************************/
BaseType_t cmd_stop_pwm(char* writeBuffer, size_t writeBufferLength, const char* commandString) {
    HAL_TIM_PWM_Stop(&PWM_TIM_HANDLE, TIM_CHANNEL_1);
    return pdFALSE;
}
/*********************************************************************************************/

static const CLI_Command_Definition_t xCommandList[] = {
    {
        "clkInit",
        "clk_init:\r\n  Initialize the Si5351 and set to 10MHz on CLK0 and enable clocks\r\n",
        cmd_clk_init,
        0 /* Number of parameters */
    },
    {
        "clkEn",
        "clkEn:\r\n  Enable the clocks\r\n",
        cmd_enable_clocks,
        0 /* Number of parameters */
    },
    {
        "clkDis",
        "clkDis:\r\n  Disable the clocks\r\n",
        cmd_disable_clocks,
        0 /* Number of parameters */
    },
    {
        "freq",
        "freq:\r\n  Display the current frequency on PA0\r\n",
        cmd_frequency,
        0 /* Number of parameters */
    },
    {
        "pllA",
        "pllA <mul> <num> <den>:\r\n  PLL A = 25MHz * (mul + num / den)\r\n  PLL A must be in [600MHz, 900MHz]\r\n",
        cmd_set_pllA,
        3 /* Number of parameters */
    },
    {
        "synth",
        "synth <ch> <mul> <num> <den>:\r\n  CLK_ch = PLLA / (mul + num / den)\r\n  ch must be in [0, 2]\r\n",
        cmd_set_multisynth,
        4 /* Number of parameters */
    },
    {
        "startPwm",
        "startPwm <freq>:\r\n  Start the PWM output with freq\r\n",
        cmd_start_pwm,
        1 /* Number of parameters */
    },
    {
        "stopPwm",
        "stopPwm:\r\n  Stop the PWM output\r\n",
        cmd_stop_pwm,
        0 /* Number of parameters */
    },
    {
        .pcCommand = NULL /* simply used as delimeter for end of array*/
    }};

HAL_StatusTypeDef timerInit(void) {
    HAL_TIM_Base_Start(&ONE_HZ_TIM_HANDLE);
    HAL_TIM_IC_Start_IT(&CLK_IN_TIM_HANDLE, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&PWM_TIM_HANDLE, TIM_CHANNEL_1);
    return HAL_OK;
}

HAL_StatusTypeDef clockProgCliInit(void) {
    /* Register all commands */
    for (int i = 0; xCommandList[i].pcCommand != NULL; i++) {
        if (FreeRTOS_CLIRegisterCommand(&xCommandList[i]) != pdPASS) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}

HAL_StatusTypeDef clockProgInit(void) {
    ASSERT_STATUS(clockProgCliInit());
    ASSERT_STATUS(timerInit());

    return HAL_OK;
}