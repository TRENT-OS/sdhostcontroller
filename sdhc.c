/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Original file at https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/src/sdhc.c
 */

#include "sdhc.h"

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "services.h"
#include "mmc.h"

static inline sdhc_dev_t *sdio_get_sdhc(sdio_host_dev_t *sdio)
{
    return (sdhc_dev_t *)sdio->priv;
}

/** Print uSDHC registers. */
UNUSED static void print_sdhc_regs(sdhc_dev_t *host)
{
    int i;
    for (i = offsetof(sdhc_regs_t, ds_addr); i <= offsetof(sdhc_regs_t,host_version); i += 0x4) {
        ZF_LOGD("%x: %X", i, (*(volatile uint32_t*)(host->base + i)));
    }
}

static inline dma_mode_e get_dma_mode(sdhc_dev_t *host, mmc_cmd_t *cmd)
{
    if (cmd->data == NULL) {
        return DMA_MODE_NONE;
    }
    if (cmd->data->pbuf == 0) {
        return DMA_MODE_NONE;
    }
    /* Currently only SDMA supported */
    return DMA_MODE_SDMA;
}

static inline int cap_sdma_supported(sdhc_dev_t *host)
{
    uint32_t v = ((sdhc_regs_t *)host->base)->host_ctrl_cap;
    return !!(v & HOST_CTRL_CAP_DMAS);
}

static inline int cap_max_buffer_size(sdhc_dev_t *host)
{
    uint32_t v = ((sdhc_regs_t *)host->base)->host_ctrl_cap;
    v = ((v >> HOST_CTRL_CAP_MBL_SHF) & HOST_CTRL_CAP_MBL_MASK);
    return 512 << v;
}

static int sdhc_next_cmd(sdhc_dev_t *host)
{
    mmc_cmd_t *cmd = host->cmd_list_head;
    uint32_t val;
    uint32_t mix_ctrl;

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_CIE     | INT_STATUS_CEBE
           | INT_STATUS_CCE   | INT_STATUS_CTOE    | INT_STATUS_TC
           | INT_STATUS_CC);
    if (get_dma_mode(host, cmd) == DMA_MODE_NONE) {
        val |= INT_STATUS_BRR | INT_STATUS_BWR;
    }
    ((sdhc_regs_t *)host->base)->int_status_en = val;

    /* Check if the Host is ready for transit. */
    while (((sdhc_regs_t *)host->base)->pres_state & (SDHC_PRES_STATE_CIHB | SDHC_PRES_STATE_CDIHB));
    while (((sdhc_regs_t *)host->base)->pres_state & SDHC_PRES_STATE_DLA);

    /* Two commands need to have at least 8 clock cycles in between.
     * Lets assume that the hcd will enforce this. */
    //udelay(1000);

    /* Write to the argument register. */
    ZF_LOGD("CMD: %d with arg %x ", cmd->index, cmd->arg);
    ((sdhc_regs_t *)host->base)->cmd_arg = cmd->arg;

    if (cmd->data) {
        /* Use the default timeout. */
        val = ((sdhc_regs_t *)host->base)->sys_ctrl;
        val &= ~(0xffUL << 16);
        val |= 0xE << 16;
        ((sdhc_regs_t *)host->base)->sys_ctrl = val;

        /* Set the DMA boundary. */
        val = (cmd->data->block_size & BLK_ATT_BLKSIZE_MASK);
        val |= (cmd->data->blocks << BLK_ATT_BLKCNT_SHF);
        ((sdhc_regs_t *)host->base)->blk_att = val;

        /* Set watermark level */
        val = cmd->data->block_size / 4;
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
        mix_ctrl = MIX_CTRL_BCEN;
        if (cmd->data->blocks > 1) {
            mix_ctrl |= MIX_CTRL_MSBSEL;
        }
        if (cmd->index == MMC_READ_SINGLE_BLOCK) {
            mix_ctrl |= MIX_CTRL_DTDSEL;
        }

        /* Configure DMA */
        if (get_dma_mode(host, cmd) != DMA_MODE_NONE) {
            /* Enable DMA */
            mix_ctrl |= MIX_CTRL_DMAEN;
            /* Set DMA address */
            ((sdhc_regs_t *)host->base)->ds_addr = cmd->data->pbuf;
        }
        /* Record the number of blocks to be sent */
        host->blocks_remaining = cmd->data->blocks;
    }

    /* The command should be MSB and the first two bits should be '00' */
    val = (cmd->index & CMD_XFR_TYP_CMDINX_MASK) << CMD_XFR_TYP_CMDINX_SHF;
    val &= ~(CMD_XFR_TYP_CMDTYP_MASK << CMD_XFR_TYP_CMDTYP_SHF);
    if (cmd->data) {
        if (host->version == 2) {
            /* Some controllers implement MIX_CTRL as part of the XFR_TYP */
            val |= mix_ctrl;
        } else {
            ((sdhc_regs_t *)host->base)->mix_ctrl = mix_ctrl;
        }
    }

    /* Set response type */
    val &= ~CMD_XFR_TYP_CICEN;
    val &= ~CMD_XFR_TYP_CCCEN;
    val &= ~(CMD_XFR_TYP_RSPTYP_MASK << CMD_XFR_TYP_RSPTYP_SHF);
    switch (cmd->rsp_type) {
    case MMC_RSP_TYPE_R2:
        val |= (0x1 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R3:
    case MMC_RSP_TYPE_R4:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        break;
    case MMC_RSP_TYPE_R1:
    case MMC_RSP_TYPE_R5:
    case MMC_RSP_TYPE_R6:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R1b:
    case MMC_RSP_TYPE_R5b:
        val |= (0x3 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    default:
        break;
    }

    if (cmd->data) {
        val |= CMD_XFR_TYP_DPSEL;
    }

    /* Issue the command. */
    ((sdhc_regs_t *)host->base)->cmd_xfr_typ = val;
    return 0;
}

/** Pass control to the devices IRQ handler
 * @param[in] sd_dev  The sdhc interface device that triggered
 *                    the interrupt event.
 */
static int sdhc_handle_irq(sdio_host_dev_t *sdio, int irq UNUSED)
{
    sdhc_dev_t *host = sdio_get_sdhc(sdio);
    mmc_cmd_t *cmd = host->cmd_list_head;

    uint32_t int_status = ((sdhc_regs_t *)host->base)->int_status;
    if (!cmd) {
        /* Clear flags */
        ((sdhc_regs_t *)host->base)->int_status = int_status;
        return 0;
    }
    /** Handle errors **/
    if (int_status & INT_STATUS_TNE) {
        ZF_LOGE("Tuning error");
    }
    if (int_status & INT_STATUS_OVRCURE) {
        ZF_LOGE("Bus overcurrent"); /* (exl. IMX6) */
    }
    if (int_status & INT_STATUS_ERR) {
        ZF_LOGE("CMD/DATA transfer error"); /* (exl. IMX6) */
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_AC12E) {
        ZF_LOGE("Auto CMD12 Error");
        cmd->complete = -1;
    }
    /** DMA errors **/
    if (int_status & INT_STATUS_DMAE) {
        ZF_LOGE("DMA Error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_ADMAE) {
        ZF_LOGE("ADMA error");       /*  (exl. IMX6) */
        cmd->complete = -1;
    }
    /** DATA errors **/
    if (int_status & INT_STATUS_DEBE) {
        ZF_LOGE("Data end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DCE) {
        ZF_LOGE("Data CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DTOE) {
        ZF_LOGE("Data transfer error");
        cmd->complete = -1;
    }
    /** CMD errors **/
    if (int_status & INT_STATUS_CIE) {
        ZF_LOGE("Command index error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CEBE) {
        ZF_LOGE("Command end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CCE) {
        ZF_LOGE("Command CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CTOE) {
        ZF_LOGE("CMD Timeout...");
        cmd->complete = -1;
    }

    if (int_status & INT_STATUS_TP) {
        ZF_LOGD("Tuning pass");
    }
    if (int_status & INT_STATUS_RTE) {
        ZF_LOGD("Retuning event");
    }
    if (int_status & INT_STATUS_CINT) {
        ZF_LOGD("Card interrupt");
    }
    if (int_status & INT_STATUS_CRM) {
        ZF_LOGD("Card removal");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CINS) {
        ZF_LOGD("Card insertion");
    }
    if (int_status & INT_STATUS_DINT) {
        ZF_LOGD("DMA interrupt");
    }
    if (int_status & INT_STATUS_BGE) {
        ZF_LOGD("Block gap event");
    }

    /* Command complete */
    if (int_status & INT_STATUS_CC) {
        /* Command complete */
        switch (cmd->rsp_type) {
        case MMC_RSP_TYPE_R2:
            cmd->response[0] = ((sdhc_regs_t *)host->base)->cmd_rsp0;
            cmd->response[1] = ((sdhc_regs_t *)host->base)->cmd_rsp1;
            cmd->response[2] = ((sdhc_regs_t *)host->base)->cmd_rsp2;
            cmd->response[3] = ((sdhc_regs_t *)host->base)->cmd_rsp3;
            break;
        case MMC_RSP_TYPE_R1b:
            if (cmd->index == MMC_STOP_TRANSMISSION) {
                cmd->response[3] = ((sdhc_regs_t *)host->base)->cmd_rsp3;
            } else {
                cmd->response[0] = ((sdhc_regs_t *)host->base)->cmd_rsp0;
            }
            break;
        case MMC_RSP_TYPE_NONE:
            break;
        default:
            cmd->response[0] = ((sdhc_regs_t *)host->base)->cmd_rsp0;
        }

        /* If there is no data segment, the transfer is complete */
        if (cmd->data == NULL) {
            assert(cmd->complete == 0);
            cmd->complete = 1;
        }
    }
    /* DATA: Programmed IO handling */
    if (int_status & (INT_STATUS_BRR | INT_STATUS_BWR)) {
        volatile uint32_t *io_buf;
        uint32_t *usr_buf;
        assert(cmd->data);
        assert(cmd->data->vbuf);
        assert(cmd->complete == 0);
        if (host->blocks_remaining) {
            io_buf = (volatile uint32_t *)((void *)&((sdhc_regs_t *)host->base)->data_buff_acc_port);
            usr_buf = (uint32_t *)cmd->data->vbuf;
            if (int_status & INT_STATUS_BRR) {
                /* Buffer Read Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *usr_buf++ = *io_buf;
                }
            } else {
                /* Buffer Write Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *io_buf = *usr_buf++;
                }
            }
            host->blocks_remaining--;
        }
    }
    /* Data complete */
    if (int_status & INT_STATUS_TC) {
        assert(cmd->complete == 0);
        cmd->complete = 1;
    }
    /* Clear flags */
    ((sdhc_regs_t *)host->base)->int_status = int_status;

    /* If the transaction has finished */
    if (cmd != NULL && cmd->complete != 0) {
        if (cmd->next == NULL) {
            /* Shutdown */
            host->cmd_list_head = NULL;
            host->cmd_list_tail = &host->cmd_list_head;
        } else {
            /* Next */
            host->cmd_list_head = cmd->next;
            sdhc_next_cmd(host);
        }
        cmd->next = NULL;
        /* Send callback if required */
        if (cmd->cb) {
            cmd->cb(sdio, 0, cmd, cmd->token);
        }
    }

    return 0;
}

static int sdhc_is_voltage_compatible(sdio_host_dev_t *sdio, int mv)
{
    sdhc_dev_t *host = sdio_get_sdhc(sdio);
    uint32_t val = ((sdhc_regs_t *)host->base)->host_ctrl_cap;
    if (mv == 3300 && (val & HOST_CTRL_CAP_VS33)) {
        return 1;
    } else {
        return 0;
    }
}

static int sdhc_send_cmd(
    sdio_host_dev_t *sdio,
    mmc_cmd_t *cmd,
    sdio_cb cb,
    void *token
)
{
    sdhc_dev_t *host = sdio_get_sdhc(sdio);
    int ret;

    /* Initialise callbacks */
    cmd->complete = 0;
    cmd->next = NULL;
    cmd->cb = cb;
    cmd->token = token;
    /* Append to list */
    *host->cmd_list_tail = cmd;
    host->cmd_list_tail = &cmd->next;

    /* If idle, bump */
    if (host->cmd_list_head == cmd) {
        ret = sdhc_next_cmd(host);
        if (ret) {
            return ret;
        }
    }

    /* finalise the transacton */
    if (cb == NULL) {
        /* Wait for completion */
        while (!cmd->complete) {
            sdhc_handle_irq(sdio, 0);
        }
        /* Return result */
        if (cmd->complete < 0) {
            return cmd->complete;
        } else {
            return 0;
        }
    } else {
        /* Defer to IRQ handler */
        return 0;
    }
}

static void sdhc_enable_clock(volatile void *base_addr)
{
    uint32_t val = ((sdhc_regs_t *)base_addr)->sys_ctrl;
    val |= SYS_CTRL_CLK_INT_EN;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = val;

    do {
        val = ((sdhc_regs_t *)base_addr)->sys_ctrl;
    } while (!(val & SYS_CTRL_CLK_INT_STABLE));

    val |= SYS_CTRL_CLK_CARD_EN;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = val;

    return;
}

/* Set the clock divider and timeout */
static int sdhc_set_clock_div(
    volatile void *base_addr,
    divisor_e dvs_div,
    sdclk_frequency_select_e sdclks_div,
    data_timeout_counter_val_e dtocv)
{
    /* make sure the clock state is stable. */
    if (((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_SDSTB) {
        uint32_t val = ((sdhc_regs_t *)base_addr)->sys_ctrl;

        /* The SDCLK bit varies with Data Rate Mode. */
        if (((sdhc_regs_t *)base_addr)->mix_ctrl & MIX_CTRL_DDR_EN) {
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

static int sdhc_set_clock(volatile void *base_addr, clock_mode_e clk_mode)
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

/** Software Reset */
static int sdhc_reset(sdio_host_dev_t *sdio)
{
    sdhc_dev_t *host = sdio_get_sdhc(sdio);

    /* Reset the host */
    uint32_t val = ((sdhc_regs_t *)host->base)->sys_ctrl;
    val |= SYS_CTRL_RSTA;
    /* Wait until the controller is ready */
    ((sdhc_regs_t *)host->base)->sys_ctrl = val;
    do {
        val = ((sdhc_regs_t *)host->base)->sys_ctrl;
    } while (val & SYS_CTRL_RSTA);

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_BRR     | INT_STATUS_BWR
           | INT_STATUS_CIE   | INT_STATUS_CEBE    | INT_STATUS_CCE
           | INT_STATUS_CTOE  | INT_STATUS_TC      | INT_STATUS_CC);
    ((sdhc_regs_t *)host->base)->int_status_en = val;
    ((sdhc_regs_t *)host->base)->int_signal_en = val;

    /* Configure clock for initialization */
    sdhc_set_clock(host->base, CLOCK_INITIAL);

    /* TODO: Select Voltage Level */

    /* Set bus width */
    val = ((sdhc_regs_t *)host->base)->prot_ctrl;
    val |= MMC_MODE_4BIT;
    ((sdhc_regs_t *)host->base)->prot_ctrl = val;

    /* Wait until the Command and Data Lines are ready. */
    while ((((sdhc_regs_t *)host->base)->pres_state & SDHC_PRES_STATE_CDIHB) ||
           (((sdhc_regs_t *)host->base)->pres_state & SDHC_PRES_STATE_CIHB));

    /* Send 80 clock ticks to card to power up. */
    val = ((sdhc_regs_t *)host->base)->sys_ctrl;
    val |= SYS_CTRL_INITA;
    ((sdhc_regs_t *)host->base)->sys_ctrl = val;
    while (((sdhc_regs_t *)host->base)->sys_ctrl & SYS_CTRL_INITA);

    /* Check if a SD card is inserted. */
    val = ((sdhc_regs_t *)host->base)->pres_state;
    if (val & SDHC_PRES_STATE_CINST) {
        ZF_LOGD("Card Inserted");
        if (!(val & SDHC_PRES_STATE_WPSPL)) {
            ZF_LOGD("(Read Only)");
        }
    } else {
        ZF_LOGE("Card Not Present...");
    }

    return 0;
}

static int sdhc_get_nth_irq(sdio_host_dev_t *sdio, int n)
{
    sdhc_dev_t *host = sdio_get_sdhc(sdio);
    if (n < 0 || n >= host->nirqs) {
        return -1;
    } else {
        return host->irq_table[n];
    }
}

static uint32_t sdhc_get_present_state_register(sdio_host_dev_t *sdio)
{
    return ((sdhc_regs_t *)sdio_get_sdhc(sdio)->base)->pres_state;
}

static int sdhc_set_operational(sdio_host_dev_t *sdio)
{
    /*
     * Set the clock to a higher frequency for the operational state.
     *
     * As of now, there are no further checks to validate if the card and the
     * host controller could be driven with a higher rate, therefore the
     * operational clock settings are chosen rather conservative.
     */
    sdhc_dev_t *host = sdio_get_sdhc(sdio);
    return sdhc_set_clock(host->base, CLOCK_OPERATIONAL);
}

int sdhc_init(
    void *iobase,
    const int *irq_table,
    int nirqs,
    ps_io_ops_t *io_ops,
    sdio_host_dev_t *dev
)
{
    /* Allocate memory for SDHC structure */
    sdhc_dev_t *sdhc = (sdhc_dev_t *)malloc(sizeof(*sdhc));
    if (!sdhc) {
        ZF_LOGE("Not enough memory!");
        return -1;
    }
    /* Complete the initialisation of the SDHC structure */
    sdhc->base = iobase;
    sdhc->nirqs = nirqs;
    sdhc->irq_table = irq_table;
    sdhc->dalloc = &io_ops->dma_manager;
    sdhc->cmd_list_head = NULL;
    sdhc->cmd_list_tail = &sdhc->cmd_list_head;
    sdhc->version = ((((sdhc_regs_t *)sdhc->base)->host_version >> 16) & 0xff) + 1;
    ZF_LOGD("SDHC version %d.00", sdhc->version);
    /* Initialise SDIO structure */
    dev->handle_irq = &sdhc_handle_irq;
    dev->nth_irq = &sdhc_get_nth_irq;
    dev->send_command = &sdhc_send_cmd;
    dev->is_voltage_compatible = &sdhc_is_voltage_compatible;
    dev->reset = &sdhc_reset;
    dev->set_operational = &sdhc_set_operational;
    dev->get_present_state = &sdhc_get_present_state_register;
    dev->priv = sdhc;
    /* Clear IRQs */
    ((sdhc_regs_t *)sdhc->base)->int_status_en = 0;
    ((sdhc_regs_t *)sdhc->base)->int_signal_en = 0;
    ((sdhc_regs_t *)sdhc->base)->int_status = ((sdhc_regs_t *)sdhc->base)->int_status;
    return 0;
}
