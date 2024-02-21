/* 
* Copyright (C) 2021-2024, HENSOLDT Cyber GmbH
* 
* SPDX-License-Identifier: GPL-2.0-or-later
*
* For commercial licensing, contact: info.cyber@hensoldt.net
*/


#include <mmc.h>
#include <sdhc.h>

/* Mixer Control Register */
#define MIX_CTRL_MSBSEL         (1 << 5)  //Multi/Single Block Select.
#define MIX_CTRL_DTDSEL         (1 << 4)  //Data Transfer Direction Select.
#define MIX_CTRL_DDR_EN         (1 << 3)  //Dual Data Rate mode selection
#define MIX_CTRL_AC12EN         (1 << 2)  //Auto CMD12 Enable
#define MIX_CTRL_BCEN           (1 << 1)  //Block Count Enable
#define MIX_CTRL_DMAEN          (1 << 0)  //DMA Enable

/* Watermark Level register */
#define WTMK_LVL_WR_WML_SHF     16        //Write Watermark Level
#define WTMK_LVL_RD_WML_SHF     0         //Read  Watermark Level

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

uint32_t sdhc_set_transfer_mode(sdhc_dev_t *host)
{
    /*
     * Specific registers of the iMX6 SoC are set (WATMK_LVL, MIX_CTRL). These
     * registers are used instead of the CMD_XFR_TYP register. Hence, 0 is
     * returned. This might be different for other SoCs.
     */
    mmc_cmd_t *cmd = host->cmd_list_head;

    /* Set watermark level */
    uint32_t val = cmd->data->block_size / 4;
    if (val > 0x80) {
        val = 0x80;
    }
    if (cmd->index == MMC_READ_SINGLE_BLOCK) {
        val = (val << WTMK_LVL_RD_WML_SHF);
    } else {
        val = (val << WTMK_LVL_WR_WML_SHF);
    }
    ((sdhc_regs_t *)host->base)->wtmk_lvl = val;

    /* Set Mixer Control */
    val = MIX_CTRL_BCEN;
    if (cmd->data->blocks > 1) {
        val |= MIX_CTRL_MSBSEL;
    }
    if (cmd->index == MMC_READ_SINGLE_BLOCK) {
        val |= MIX_CTRL_DTDSEL;
    }
    if (cmd->data != NULL && cmd->data->pbuf != 0) {
        val |= MIX_CTRL_DMAEN;
    }

    ((sdhc_regs_t *)host->base)->mix_ctrl = val;

    return 0;
}

void sdhc_set_voltage_level(sdhc_dev_t *host)
{
    return;
}

void sdhc_inter_command_delay(void)
{
    // nothing to do here
}
