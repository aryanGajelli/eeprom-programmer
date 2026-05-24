#include "Si5351.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "assert.h"
#include "bsp.h"
#include "stm32g4xx_hal.h"

uint8_t lastRdivValue[3] = {0};

HAL_StatusTypeDef write8(uint8_t reg, uint8_t value) {
    return HAL_I2C_Mem_Write(&CLOCK_PROG_I2C_HANDLE, SI5351_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, HAL_MAX_DELAY);
}

HAL_StatusTypeDef writeN(uint8_t* reg_values, uint8_t len) {
    ASSERT(reg_values != NULL, HAL_ERROR);
    ASSERT(len > 1, HAL_ERROR);

    // Burst write: first byte is register address, remaining bytes auto-increment.
    return HAL_I2C_Master_Transmit(&CLOCK_PROG_I2C_HANDLE, SI5351_I2C_ADDRESS,
                                   reg_values, len, HAL_MAX_DELAY);
}

HAL_StatusTypeDef read8(uint8_t reg, uint8_t* value) {
    return HAL_I2C_Mem_Read(&CLOCK_PROG_I2C_HANDLE, SI5351_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, value, 1, HAL_MAX_DELAY);
}

HAL_StatusTypeDef Si5351Init(void) {
    // Initialize the Si5351 device
    /* Disable all outputs setting CLKx_DIS high */
    uint8_t value;
    ASSERT_STATUS(write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF));

    ASSERT_STATUS(read8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, &value));
    ASSERT(value == 0xFF, HAL_ERROR);

    /* Power down all output drivers */
    ASSERT_STATUS(write8(SI5351_REGISTER_16_CLK0_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_17_CLK1_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_18_CLK2_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_19_CLK3_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_20_CLK4_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_21_CLK5_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_22_CLK6_CONTROL, 0x80));
    ASSERT_STATUS(write8(SI5351_REGISTER_23_CLK7_CONTROL, 0x80));

    /* Do not gate outputs via the external OEB pin; control enable in register 3. */
    ASSERT_STATUS(write8(SI5351_REGISTER_9_OEB_PIN_ENABLE_CONTROL, 0x00));

    /* Use XTAL as PLL input source for PLLA/PLLB. */
    ASSERT_STATUS(write8(SI5351_REGISTER_15_PLL_INPUT_SOURCE, 0x00));

    /* Set the load capacitance for the XTAL */
    // Keep fixed lower bits set and only change XTAL_CL bits [7:6].
    ASSERT_STATUS(write8(SI5351_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE,
                         (uint8_t)(m_si5351Config.crystalLoad | 0x12U)));

    /* Disable spread spectrum output. */
    ASSERT_STATUS(enableSpreadSpectrum(false));

    /* Set interrupt masks as required (see Register 2 description in AN619).
       By default, ClockBuilder Desktop sets this register to 0x18.
       Note that the least significant nibble must remain 0x8, but the most
       significant nibble may be modified to suit your needs. */

    /* Reset the PLL config fields just in case we call init again */
    m_si5351Config.plla_configured = false;
    m_si5351Config.plla_freq = 0;
    m_si5351Config.pllb_configured = false;
    m_si5351Config.pllb_freq = 0;

    /* All done! */
    m_si5351Config.initialised = true;

    return HAL_OK;
}

/**************************************************************************/
/*!
    @brief  Enables or disables spread spectrum
    @param  enabled Whether spread spectrum output is enabled
    @return HAL_StatusTypeDef
*/
/**************************************************************************/
HAL_StatusTypeDef enableSpreadSpectrum(bool enabled) {
    uint8_t regval;
    ASSERT_STATUS(read8(SI5351_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS, &regval));
    if (enabled) {
        regval |= 0x80;
    } else {
        regval &= ~0x80;
    }
    ASSERT_STATUS(write8(SI5351_REGISTER_149_SPREAD_SPECTRUM_PARAMETERS, regval));

    return HAL_OK;
}

HAL_StatusTypeDef enableOutputs(bool enabled) {
    /* Make sure we've called init first */
    ASSERT(m_si5351Config.initialised, HAL_ERROR);

    /* Enabled desired outputs (see Register 3) */
    ASSERT_STATUS(write8(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, enabled ? 0x00 : 0xFF));

    return HAL_OK;
}

/**************************************************************************/
/*!
  @brief  Sets the multiplier for the specified PLL using integer values

  @param  pll   The PLL to configure, which must be one of the following:
                - SI5351_PLL_A
                - SI5351_PLL_B
  @param  mult  The PLL integer multiplier (must be between 15 and 90)
*/
/**************************************************************************/
HAL_StatusTypeDef setupPLLInt(Si5351PLL_t pll, uint8_t mult) {
    return setupPLL(pll, mult, 0, 1);
}

/**************************************************************************/
/*!
    @brief  Sets the multiplier for the specified PLL

    @param  pll   The PLL to configure, which must be one of the following:
                  - SI5351_PLL_A
                  - SI5351_PLL_B
    @param  mult  The PLL integer multiplier (must be between 15 and 90)
    @param  num   The 20-bit numerator for fractional output (0..1,048,575).
                  Set this to '0' for integer output.
    @param  denom The 20-bit denominator for fractional output (1..1,048,575).
                  Set this to '1' or higher to avoid divider by zero errors.

    @section PLL Configuration

    fVCO is the PLL output, and must be between 600..900MHz, where:

        fVCO = fXTAL * (a+(b/c))

    fXTAL = the crystal input frequency
    a     = an integer between 15 and 90
    b     = the fractional numerator (0..1,048,575)
    c     = the fractional denominator (1..1,048,575)

    NOTE: Try to use integers whenever possible to avoid clock jitter
    (only use the a part, setting b to '0' and c to '1').

    See: http://www.silabs.com/Support%20Documents/TechnicalDocs/AN619.pdf
*/
/**************************************************************************/
HAL_StatusTypeDef setupPLL(Si5351PLL_t pll, uint8_t mult, uint32_t num,
                           uint32_t denom) {
    uint32_t P1; /* PLL config register P1 */
    uint32_t P2; /* PLL config register P2 */
    uint32_t P3; /* PLL config register P3 */

    /* Basic validation */
    ASSERT(m_si5351Config.initialised, HAL_ERROR);
    ASSERT((mult > 14) && (mult < 91), HAL_ERROR); /* mult = 15..90 */
    ASSERT(denom > 0, HAL_ERROR);                  /* Avoid divide by zero */
    ASSERT(num <= 0xFFFFF, HAL_ERROR);             /* 20-bit limit */
    ASSERT(denom <= 0xFFFFF, HAL_ERROR);           /* 20-bit limit */

    /* Feedback Multisynth Divider Equation
     *
     * where: a = mult, b = num and c = denom
     *
     * P1 register is an 18-bit value using following formula:
     *
     * 	P1[17:0] = 128 * mult + floor(128*(num/denom)) - 512
     *
     * P2 register is a 20-bit value using the following formula:
     *
     * 	P2[19:0] = 128 * num - denom * floor(128*(num/denom))
     *
     * P3 register is a 20-bit value using the following formula:
     *
     * 	P3[19:0] = denom
     */

    /* Set the main PLL config registers */
    if (num == 0) {
        /* Integer mode */
        P1 = 128 * mult - 512;
        P2 = num;
        P3 = denom;
    } else {
        /* Fractional mode */
        P1 =
            (uint32_t)(128 * mult + floor(128 * ((float)num / (float)denom)) - 512);
        P2 = (uint32_t)(128 * num -
                        denom * floor(128 * ((float)num / (float)denom)));
        P3 = denom;
    }

    /* Get the appropriate starting point for the PLL registers */
    uint8_t baseaddr = (pll == SI5351_PLL_A ? 26 : 34);

    /* The datasheet is a nightmare of typos and inconsistencies here! */
    ASSERT_STATUS(write8(baseaddr, (P3 & 0x0000FF00) >> 8));
    ASSERT_STATUS(write8(baseaddr + 1, (P3 & 0x000000FF)));
    ASSERT_STATUS(write8(baseaddr + 2, (P1 & 0x00030000) >> 16));
    ASSERT_STATUS(write8(baseaddr + 3, (P1 & 0x0000FF00) >> 8));
    ASSERT_STATUS(write8(baseaddr + 4, (P1 & 0x000000FF)));
    ASSERT_STATUS(write8(baseaddr + 5,
                         ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16)));
    ASSERT_STATUS(write8(baseaddr + 6, (P2 & 0x0000FF00) >> 8));
    ASSERT_STATUS(write8(baseaddr + 7, (P2 & 0x000000FF)));

    /* Reset both PLLs */
    ASSERT_STATUS(write8(SI5351_REGISTER_177_PLL_RESET, (1 << 7) | (1 << 5)));

    /* Store the frequency settings for use with the Multisynth helper */
    if (pll == SI5351_PLL_A) {
        float fvco =
            m_si5351Config.crystalFreq * (mult + ((float)num / (float)denom));
        m_si5351Config.plla_configured = true;
        m_si5351Config.plla_freq = (uint32_t)floor(fvco);
    } else {
        float fvco =
            m_si5351Config.crystalFreq * (mult + ((float)num / (float)denom));
        m_si5351Config.pllb_configured = true;
        m_si5351Config.pllb_freq = (uint32_t)floor(fvco);
    }

    return HAL_OK;
}

/**************************************************************************/
/*!
    @brief  Configures the Multisynth divider using integer output.

    @param  output    The output channel to use (0..2)
    @param  pllSource	The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider for the Multisynth output,
                      which must be one of the following values:
                      - SI5351_MULTISYNTH_DIV_4
                      - SI5351_MULTISYNTH_DIV_6
                      - SI5351_MULTISYNTH_DIV_8
*/
/**************************************************************************/
HAL_StatusTypeDef setupMultisynthInt(uint8_t output, Si5351PLL_t pllSource,
                                     Si5351MultisynthDiv_t div) {
    return setupMultisynth(output, pllSource, div, 0, 1);
}

HAL_StatusTypeDef setupRdiv(uint8_t output, Si5351RDiv_t div) {
    ASSERT(output < 3, HAL_ERROR); /* Channel range */

    uint8_t Rreg, regval;

    if (output == 0)
        Rreg = SI5351_REGISTER_44_MULTISYNTH0_PARAMETERS_3;
    if (output == 1)
        Rreg = SI5351_REGISTER_52_MULTISYNTH1_PARAMETERS_3;
    if (output == 2)
        Rreg = SI5351_REGISTER_60_MULTISYNTH2_PARAMETERS_3;

    read8(Rreg, &regval);

    regval &= 0x0F;
    uint8_t divider = div;
    divider &= 0x07;
    divider <<= 4;
    regval |= divider;
    lastRdivValue[output] = divider;
    return write8(Rreg, regval);
}

/**************************************************************************/
/*!
    @brief  Configures the Multisynth divider, which determines the
            output clock frequency based on the specified PLL input.

    @param  output    The output channel to use (0..2)
    @param  pllSource	The PLL input source to use, which must be one of:
                      - SI5351_PLL_A
                      - SI5351_PLL_B
    @param  div       The integer divider for the Multisynth output.
                      If pure integer values are used, this value must
                      be one of:
                      - SI5351_MULTISYNTH_DIV_4
                      - SI5351_MULTISYNTH_DIV_6
                      - SI5351_MULTISYNTH_DIV_8
                      If fractional output is used, this value must be
                      between 8 and 900.
    @param  num       The 20-bit numerator for fractional output
                      (0..1,048,575). Set this to '0' for integer output.
    @param  denom     The 20-bit denominator for fractional output
                      (1..1,048,575). Set this to '1' or higher to
                      avoid divide by zero errors.

    @section Output Clock Configuration

    The multisynth dividers are applied to the specified PLL output,
    and are used to reduce the PLL output to a valid range (500kHz
    to 160MHz). The relationship can be seen in this formula, where
    fVCO is the PLL output frequency and MSx is the multisynth
    divider:

        fOUT = fVCO / MSx

    Valid multisynth dividers are 4, 6, or 8 when using integers,
    or any fractional values between 8 + 1/1,048,575 and 900 + 0/1

    The following formula is used for the fractional mode divider:

        a + b / c

    a = The integer value, which must be 4, 6 or 8 in integer mode (MSx_INT=1)
        or 8..900 in fractional mode (MSx_INT=0).
    b = The fractional numerator (0..1,048,575)
    c = The fractional denominator (1..1,048,575)

    @note   Try to use integers whenever possible to avoid clock jitter

    @note   For output frequencies > 150MHz, you must set the divider
            to 4 and adjust to PLL to generate the frequency (for example
            a PLL of 640 to generate a 160MHz output clock). This is not
            yet supported in the driver, which limits frequencies to
            500kHz .. 150MHz.

    @note   For frequencies below 500kHz (down to 8kHz) Rx_DIV must be
            used, but this isn't currently implemented in the driver.
*/
/**************************************************************************/
HAL_StatusTypeDef setupMultisynth(uint8_t output, Si5351PLL_t pllSource,
                                  uint32_t div, uint32_t num,
                                  uint32_t denom) {
    uint32_t P1; /* Multisynth config register P1 */
    uint32_t P2; /* Multisynth config register P2 */
    uint32_t P3; /* Multisynth config register P3 */

    /* Basic validation */
    ASSERT(m_si5351Config.initialised, HAL_ERROR);
    ASSERT(output < 3, HAL_ERROR);       /* Channel range */
    ASSERT(div > 3, HAL_ERROR);          /* Divider integer value */
    ASSERT(div < 2049, HAL_ERROR);       /* Divider integer value */
    ASSERT(denom > 0, HAL_ERROR);        /* Avoid divide by zero */
    ASSERT(num <= 0xFFFFF, HAL_ERROR);   /* 20-bit limit */
    ASSERT(denom <= 0xFFFFF, HAL_ERROR); /* 20-bit limit */

    /* Make sure the requested PLL has been initialised */
    if (pllSource == SI5351_PLL_A) {
        ASSERT(m_si5351Config.plla_configured, HAL_ERROR);
    } else {
        ASSERT(m_si5351Config.pllb_configured, HAL_ERROR);
    }

    /* Output Multisynth Divider Equations
     *
     * where: a = div, b = num and c = denom
     *
     * P1 register is an 18-bit value using following formula:
     *
     * 	P1[17:0] = 128 * a + floor(128*(b/c)) - 512
     *
     * P2 register is a 20-bit value using the following formula:
     *
     * 	P2[19:0] = 128 * b - c * floor(128*(b/c))
     *
     * P3 register is a 20-bit value using the following formula:
     *
     * 	P3[19:0] = c
     */

    /* Set the main PLL config registers */
    if (num == 0) {
        /* Integer mode */
        P1 = 128 * div - 512;
        P2 = 0;
        P3 = denom;
    } else if (denom == 1) {
        /* Fractional mode, simplified calculations */
        P1 = 128 * div + 128 * num - 512;
        P2 = 128 * num - 128;
        P3 = 1;
    } else {
        /* Fractional mode */
        P1 = (uint32_t)(128 * div + floor(128 * ((float)num / (float)denom)) - 512);
        P2 = (uint32_t)(128 * num -
                        denom * floor(128 * ((float)num / (float)denom)));
        P3 = denom;
    }

    /* Get the appropriate starting point for the PLL registers */
    uint8_t baseaddr = 0;
    switch (output) {
        case 0:
            baseaddr = SI5351_REGISTER_42_MULTISYNTH0_PARAMETERS_1;
            break;
        case 1:
            baseaddr = SI5351_REGISTER_50_MULTISYNTH1_PARAMETERS_1;
            break;
        case 2:
            baseaddr = SI5351_REGISTER_58_MULTISYNTH2_PARAMETERS_1;
            break;
    }

    /* Set the MSx config registers */
    /* Burst mode: register address auto-increases */
    uint8_t sendBuffer[9];
    sendBuffer[0] = baseaddr;
    sendBuffer[1] = (P3 & 0xFF00) >> 8;
    sendBuffer[2] = P3 & 0xFF;
    sendBuffer[3] = ((P1 & 0x30000) >> 16) | lastRdivValue[output];
    sendBuffer[4] = (P1 & 0xFF00) >> 8;
    sendBuffer[5] = P1 & 0xFF;
    sendBuffer[6] = ((P3 & 0xF0000) >> 12) | ((P2 & 0xF0000) >> 16);
    sendBuffer[7] = (P2 & 0xFF00) >> 8;
    sendBuffer[8] = P2 & 0xFF;
    ASSERT_STATUS(writeN(sendBuffer, 9));

    /* Configure the clk control and enable the output */
    /* TODO: Check if the clk control byte needs to be updated. */
    uint8_t clkControlReg = 0x0F; /* 8mA drive strength, MS0 as CLK0 source, Clock
                                     not inverted, powered up */
    if (pllSource == SI5351_PLL_B)
        clkControlReg |= (1 << 5); /* Uses PLLB */
    if (num == 0)
        clkControlReg |= (1 << 6); /* Integer mode */
    switch (output) {
        case 0:
            ASSERT_STATUS(write8(SI5351_REGISTER_16_CLK0_CONTROL, clkControlReg));
            break;
        case 1:
            ASSERT_STATUS(write8(SI5351_REGISTER_17_CLK1_CONTROL, clkControlReg));
            break;
        case 2:
            ASSERT_STATUS(write8(SI5351_REGISTER_18_CLK2_CONTROL, clkControlReg));
            break;
    }

    return HAL_OK;
}