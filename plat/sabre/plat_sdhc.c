/* Copyright (C) 2021, HENSOLDT Cyber GmbH */

#include "../../mmc.h"
#include "../../sdhc.h"

static void sdhc_enable_clock(volatile void *base_addr)
{
    uint32_t val;

    val = ((sdhc_regs_t *)base_addr)->sys_ctrl;
    val |= SYS_CTRL_CLK_INT_EN;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = val;

    do {
        val = ((sdhc_regs_t *)base_addr)->sys_ctrl;
    } while (!(val & SYS_CTRL_CLK_INT_STABLE));

    val |= SYS_CTRL_CLK_CARD_EN;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = val;
}

/* Set the clock divider and timeout */
static int sdhc_set_clock_div(
    volatile void *base_addr,
    divisor_e dvs_div,
    sdclk_frequency_select_e sdclks_div,
    data_timeout_counter_val_e dtocv)
{
    /* make sure the clock state is stable. */
    if (((sdhc_regs_t *)base_addr)->pres_state  & SDHC_PRES_STATE_SDSTB) {
        uint32_t val = ((sdhc_regs_t *)base_addr)->sys_ctrl;

        /* The SDCLK bit varies with Data Rate Mode. */
        if (((sdhc_regs_t *)base_addr)->mix_ctrl  & MIX_CTRL_DDR_EN) {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= ((sdclks_div >> 1) << SYS_CTRL_SDCLKS_SHF);

        } else {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= (sdclks_div << SYS_CTRL_SDCLKS_SHF);
        }
        val &= ~(SYS_CTRL_DVS_MASK << SYS_CTRL_DVS_SHF);
        val |= (dvs_div << SYS_CTRL_DVS_SHF);

        /* Set data timeout value */
        val |= (dtocv << SYS_CTRL_DTOCV_SHF);
        ((sdhc_regs_t *)base_addr)->sys_ctrl = val;
    } else {
        ZF_LOGE("The clock is unstable, unable to change it!");
        return -1;
    }

    return 0;
}

int sdhc_set_clock(volatile void *base_addr, clock_mode_e clk_mode)
{
    int rslt = -1;

    const bool isClkEnabled = ((sdhc_regs_t *)base_addr)->sys_ctrl & SYS_CTRL_CLK_INT_EN;
    if (!isClkEnabled) {
        sdhc_enable_clock(base_addr);
    }

    /* TODO: Relate the clock rate settings to the actual capabilities of the
     * card and the host controller. The conservative settings chosen should
     * work with most setups, but this is not an ideal solution. According to
     * the RM, the default freq. of the base clock should be at around 200MHz.
     */
    switch (clk_mode) {
    case CLOCK_INITIAL:
        /* Divide the base clock by 512 */
        rslt = sdhc_set_clock_div(base_addr, DIV_16, PRESCALER_32, SDCLK_TIMES_2_POW_14);
        break;
    case CLOCK_OPERATIONAL:
        /* Divide the base clock by 8 */
        rslt = sdhc_set_clock_div(base_addr, DIV_4, PRESCALER_2, SDCLK_TIMES_2_POW_29);
        break;
    default:
        ZF_LOGE("Unsupported clock mode setting");
        rslt = -1;
        break;
    }

    if (rslt < 0) {
        ZF_LOGE("Failed to change the clock settings");
    }

    return rslt;
}
