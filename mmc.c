/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Original file at https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/src/mmc.c
 */

#include <mmc.h>
#include <services.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define CSD_VERSION_1       0
#define CSD_VERSION_2_AND_3 1

typedef struct mmc_completion_token_s {
    mmc_card_t *card;
    mmc_cb cb;
    void *token;
}
mmc_completion_token_t;

static uint32_t slice_bits(uint32_t *val, int start, int size)
{
    int idx;
    int high, low;
    uint32_t ret = 0;

    /* Can not return more than 32 bits. */
    assert(size <= 32);

    idx = start / 32;
    low = start % 32;
    high = (start + size) % 32;

    if (high == 0 && low == 0) {
        ret = val[idx];
    } else if (high == 0 && low != 0) {
        ret = val[idx] >> low;
    } else {
        if (high > low) {
            ret = val[idx] & ((1U << high) - 1);
            ret = ret >> low;
        } else {
            ret = val[idx] >> low;
            ret |= (val[idx + 1] & ((1U << high) - 1)) << (32 - low);
        }

    }

    return ret;
}

static int mmc_decode_cid(mmc_card_t *mmc_card, cid_t *cid)
{
    if (mmc_card == NULL || cid == NULL) {
        return -1;
    }

    if (mmc_card->type == CARD_TYPE_SD) {
        cid->manfid         = slice_bits(mmc_card->raw_cid, 120,  8);
        cid->sd_cid.oemid   = slice_bits(mmc_card->raw_cid, 104, 16);
        cid->sd_cid.name[0] = slice_bits(mmc_card->raw_cid,  96,  8);
        cid->sd_cid.name[1] = slice_bits(mmc_card->raw_cid,  88,  8);
        cid->sd_cid.name[2] = slice_bits(mmc_card->raw_cid,  80,  8);
        cid->sd_cid.name[3] = slice_bits(mmc_card->raw_cid,  72,  8);
        cid->sd_cid.name[4] = slice_bits(mmc_card->raw_cid,  64,  8);
        cid->sd_cid.rev     = slice_bits(mmc_card->raw_cid,  56,  8);
        cid->sd_cid.serial  = slice_bits(mmc_card->raw_cid,  24, 32);
        cid->sd_cid.date    = slice_bits(mmc_card->raw_cid,   8, 12);

        ZF_LOGD("manfid(%x), oemid(%x), name(%c%c%c%c%c), rev(%x), serial(%x), date(%x)",
                cid->manfid, cid->sd_cid.oemid,
                cid->sd_cid.name[0], cid->sd_cid.name[1], cid->sd_cid.name[2],
                cid->sd_cid.name[3], cid->sd_cid.name[4],
                cid->sd_cid.rev, cid->sd_cid.serial, cid->sd_cid.date);
    } else {
        ZF_LOGD("Not Implemented!");
        return -1;
    }

    return 0;
}

static int mmc_decode_csd(mmc_card_t *mmc_card, csd_t *csd)
{
    if (mmc_card == NULL || csd == NULL) {
        return -1;
    }

#define CSD_BITS(start, size) \
    slice_bits(mmc_card->raw_csd, start, size)

    csd->structure = CSD_BITS(126, 2);

    if (csd->structure == CSD_VERSION_1) {
        ZF_LOGV("CSD Version 1.0");
        csd->c_size      = CSD_BITS(62, 12);
        csd->c_size_mult = CSD_BITS(47,  3);
        csd->read_bl_len = CSD_BITS(80,  4);
        csd->tran_speed  = CSD_BITS(96,  8);
    } else if (csd->structure == CSD_VERSION_2_AND_3) {
        ZF_LOGV("CSD Version 2.0");
        csd->c_size      = CSD_BITS(48, 22);
        csd->c_size_mult = 0;
        csd->read_bl_len = CSD_BITS(80,  4);
        csd->tran_speed  = CSD_BITS(96,  8);
    } else {
        ZF_LOGE("Unknown CSD version!");
        return -1;
    }

    return 0;
}

static mmc_cmd_t *mmc_cmd_new(uint32_t index, uint32_t arg, int rsp_type)
{
    mmc_cmd_t *cmd;
    cmd = malloc(sizeof(*cmd));
    if (cmd) {
        /* Command */
        cmd->index = index;
        cmd->arg = arg;
        cmd->rsp_type = rsp_type;
        cmd->data = NULL;
        /* Transaction maintenance */
        cmd->cb = NULL;
        cmd->token = NULL;
        cmd->next = NULL;
        cmd->complete = 0;
    }
    return cmd;
}

static int mmc_cmd_add_data(
    mmc_cmd_t *cmd,
    void *vbuf,
    uintptr_t pbuf,
    uint32_t addr,
    uint32_t block_size,
    uint32_t blocks
)
{
    mmc_data_t *d;
    assert(cmd->data == NULL);
    d = (mmc_data_t *)malloc(sizeof(*d));
    if (d) {
        d->pbuf = pbuf;
        d->vbuf = vbuf;
        d->data_addr = addr;
        d->block_size = block_size;
        d->blocks = blocks;
        cmd->data = d;
        return 0;
    } else {
        return -1;
    }
}

static void mmc_cmd_destroy(mmc_cmd_t *cmd)
{
    if (cmd->data) {
        free(cmd->data);
    }
    free(cmd);
}

static mmc_completion_token_t *mmc_new_completion_token(
    mmc_card_t *mmc_card,
    mmc_cb cb,
    void *token
)
{
    mmc_completion_token_t *t;
    t = (mmc_completion_token_t *)malloc(sizeof(*t));
    if (t) {
        t->card = mmc_card;
        t->cb = cb;
        t->token = token;
    }
    return t;
}

static void mmc_completion_token_destroy(mmc_completion_token_t *t)
{
    free(t);
}

/**
 * MMC/SD/SDIO card registry.
 */
static int mmc_card_registry(mmc_card_t *card)
{
    mmc_cmd_t cmd = {.data = NULL};
    int ret;

    /* Get card ID */
    cmd.index = MMC_ALL_SEND_CID;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R2;
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (ret) {
        ZF_LOGE("No response!");
        card->status = CARD_STS_INACTIVE;
        return -1;
    } else {
        card->status = CARD_STS_ACTIVE;
    }

    /* Left shift the response by 8. Consult SDHC manual. */
    cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
    cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
    cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
    cmd.response[0] = (cmd.response[0] << 8);
    memcpy(card->raw_cid, cmd.response, sizeof(card->raw_cid));

    cid_t card_id;
    mmc_decode_cid(card, &card_id);

    /* Retrieve RCA number. */
    cmd.index = MMC_SEND_RELATIVE_ADDR;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R6;
    host_send_command(card, &cmd, NULL, NULL);
    card->raw_rca = (cmd.response[0] >> 16);
    ZF_LOGD("New Card RCA: %x", card->raw_rca);

    /* Read CSD, Status */
    cmd.index = MMC_SEND_CSD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R2;
    host_send_command(card, &cmd, NULL, NULL);

    /* Left shift the response by 8. Consult SDHC manual. */
    cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
    cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
    cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
    cmd.response[0] = (cmd.response[0] << 8);
    memcpy(card->raw_csd, cmd.response, sizeof(card->raw_csd));

    cmd.index = MMC_SEND_STATUS;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    host_send_command(card, &cmd, NULL, NULL);

    /* Select the card */
    cmd.index = MMC_SELECT_CARD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R1b;
    host_send_command(card, &cmd, NULL, NULL);

    /**
     * The default bus width of the card after power up or GO_IDLE (CMD0) is
     * 1 bit. As the HostController is initialzed to 4-bit bus width,
     * the card also needs to switch to 4-bit mode.
     */
    cmd.index = MMC_APP_CMD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    host_send_command(card, &cmd, NULL, NULL);
    cmd.index = SD_SET_BUS_WIDTH;
    cmd.arg = MMC_MODE_4BIT;
    host_send_command(card, &cmd, NULL, NULL);

    /* Set read/write block length for byte addressed standard capacity cards */
    if (!card->high_capacity) {
        cmd.index = MMC_SET_BLOCKLEN;
        cmd.arg = mmc_block_size(card);
        cmd.rsp_type = MMC_RSP_TYPE_R1;
        ret = host_send_command(card, &cmd, NULL, NULL);
    }

    return 0;
}

/**
 * Card voltage validation.
 */
static int mmc_voltage_validation(mmc_card_t *card)
{
    mmc_cmd_t cmd = {.data = NULL};
    int ret;

    /* Send CMD55 to issue an application specific command. */
    cmd.index = MMC_APP_CMD;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (!ret) {
        /* It is a SD card. */
        cmd.index = SD_SD_APP_OP_COND;
        cmd.arg = 0;
        cmd.rsp_type = MMC_RSP_TYPE_R3;
        card->type = CARD_TYPE_SD;
    } else {
        /* It is a MMC card. */
        // cmd.index = MMC_SEND_OP_COND;
        // cmd.arg = 0;
        // cmd.rsp_type = MMC_RSP_TYPE_R3;
        // card->type = CARD_TYPE_MMC;
        ZF_LOGE("MMC card detected - not supported!");
        return -1;
    }
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (ret) {
        card->type = CARD_TYPE_UNKNOWN;
        /* TODO: Be nicer */
        assert(0);
    }
    card->ocr = cmd.response[0];

    uint32_t acmd41_arg = mmc_get_voltage(card);

    /* Wait until the voltage level is set. */
    int attempts = 10;
    do {
        if (card->type == CARD_TYPE_SD) {
            cmd.index = MMC_APP_CMD;
            cmd.arg = 0;
            cmd.rsp_type = MMC_RSP_TYPE_R1;
            host_send_command(card, &cmd, NULL, NULL);
        }

        cmd.index = SD_SD_APP_OP_COND;
        cmd.arg = acmd41_arg;
        cmd.rsp_type = MMC_RSP_TYPE_R3;
        host_send_command(card, &cmd, NULL, NULL);
        udelay(100000);
    } while (attempts-- >= 0 && !(cmd.response[0] & (1U << 31)));

    if(attempts < 0){
        ZF_LOGE("Card does not reply -> Could not do the voltage change!");
        return -1;
    }
    card->ocr = cmd.response[0];

    /* Check CCS bit */
    if (card->ocr & (1 << 30)) {
        ZF_LOGD("SDHC/SDXC");
        card->high_capacity = 1;
    } else {
        ZF_LOGD("SDSC Ver2.00 or Ver3.00");
        card->high_capacity = 0;
    }

    // ToDo: Voltage Switch

    ZF_LOGD("Voltage set!");

    return 0;
}

static int mmc_reset(mmc_card_t *card)
{
    /* Reset the card with CMD0 */
    mmc_cmd_t cmd = {.data = NULL};
    cmd.index = MMC_GO_IDLE_STATE;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_NONE;
    host_send_command(card, &cmd, NULL, NULL);

    /* TODO: review this command. */
    cmd.index = MMC_SEND_EXT_CSD;
    cmd.arg = 0x1AA;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    int status = host_send_command(card, &cmd, NULL, NULL);

    if( status == INT_STATUS_DATA_TIMEOUT_ERROR ||
        status == INT_STATUS_CMD_TIMEOUT_ERROR
    ){
        ZF_LOGE("Card does not respond!");
        ZF_LOGE("SDSC v1.01/v1.10 detected or Not SD card (MMC) detected - not supported");
        return -1;
    }

    /* Check response R7 to CMD8 */
    //fields to check in argument
    uint8_t vhs_arg = (cmd.arg >> 8) & 0xf;
    uint8_t check_pattern_arg = cmd.arg & 0xff;

    //fields to check in response
    uint8_t vca_rsp = (cmd.response[0] >> 8) & 0xf;
    uint8_t check_pattern_rsp = cmd.response[0] & 0xff;

    if( status == INT_STATUS_DATA_CRC_ERROR ||
        status == INT_STATUS_CMD_CRC_ERROR ||
        vca_rsp != vhs_arg ||
        check_pattern_rsp != check_pattern_arg
    ){
        ZF_LOGE("Unusable card!");
        return -1;
    }

    if(status != 0){
        ZF_LOGE("Errors detected when sending command CMD%d!",cmd.index);
        return -1;
    }

    return 0;
}

static void mmc_blockop_completion_cb(
    sdio_host_dev_t *sdio,
    int stat,
    mmc_cmd_t *cmd,
    void *token
)
{
    mmc_completion_token_t *t;
    size_t bytes;

    t = (mmc_completion_token_t *)token;
    if (stat == 0) {
        bytes = cmd->data->block_size * cmd->data->blocks;
    } else {
        bytes = 0;
    }
    /* Call the registered function */
    t->cb(t->card, stat, bytes, t->token);
    /* Free memory */
    mmc_cmd_destroy(cmd);
    mmc_completion_token_destroy(t);
}

int mmc_init(sdio_host_dev_t *sdio, ps_io_ops_t *io_ops, mmc_card_t **mmc_card)
{
    // Note: Currently, we do not support
    //      * legacy card version 1.x,
    //      * MMC cards,
    //      * SDIO cards.
    // Effectively, this means we only do steps 1-4,19-27,32-33 of
    // section 3.6 Card Initialization and Identification in document
    // PartA2_SD_Host_Controller_Simplified_Specification_Ver3.00.
    mmc_card_t *mmc;

    /* Allocate the mmc card structure */
    mmc = (mmc_card_t *)malloc(sizeof(*mmc));
    assert(mmc);
    if (!mmc) {
        return -1;
    }
    mmc->dalloc = &io_ops->dma_manager;
    mmc->sdio = sdio;

    /* Reset the host controller */
    if (host_reset(mmc)) {
        ZF_LOGE("Failed to reset host controller");
        free(mmc);
        return -1;
    }

    // PartA2_SD_Host_Controller_Simplified_Specification_Ver3.00:
    //      3.6 Card Initialization and Identification
    /* Initialise the card */
    // Steps 1-4
    if (mmc_reset(mmc)) {
        ZF_LOGE("Failed to reset SD/MMC card");
        free(mmc);
        return -1;
    }

    // Skip Steps 5-10: SDIO specific
    // Skip Steps 12-18: Legacy cards, not SD cards

    /* Voltage validation */
    // Steps: 19-25/26/27 (assume flag F8=1)
    if (mmc_voltage_validation(mmc)) {
        ZF_LOGE("Failed to perform voltage validation");
        free(mmc);
        return -1;
    }

    /* Register the card */
    // Steps: 32-33
    if (mmc_card_registry(mmc)) {
        ZF_LOGE("Failed to register card");
        free(mmc);
        return -1;
    }

    /* Switch host controller to operational settings */
    if (host_set_operational(mmc)) {
        ZF_LOGE("Failed to switch the host controller to the operational mode");
        free(mmc);
        return -1;
    }

    *mmc_card = mmc;
    assert(mmc);
    return 0;
}

static
long transfer_data(
    mmc_card_t *mmc_card,
    unsigned long start,
    int nblocks,
    void *vbuf,
    uintptr_t pbuf,
    mmc_cb cb,
    void *token,
    uint32_t command)
{
    mmc_cmd_t *cmd;
    const int block_size = mmc_block_size(mmc_card);

    /* Determine command argument */
    const uint32_t arg = (mmc_card->high_capacity)
                         ? start
                         : (start * block_size);

    /* Allocate command structure
     *
     * Please note that `cmd` is dynamically allocated, so it must be destroyed.
     *
     * Clean up will be done prior to exiting this function OR in the
     * `mmc_blockop_completion_cb` if callback was given.
     *
     * In case of an unexpected error, there will be a jump to
     * `exit_transfer_data` label, so that memory leak can be avoided.
     */
    cmd = mmc_cmd_new(command, arg, MMC_RSP_TYPE_R1);
    if (cmd == NULL) {
        // `cmd` was NOT allocated, so we are exiting without destroying it.
        return -1;
    }

    long ret = -1;
    mmc_completion_token_t *mmc_token = NULL;

    /* Add a data segment */
    ret = mmc_cmd_add_data(cmd, vbuf, pbuf, start, block_size, nblocks);
    if (ret < 0) {
        goto exit_transfer_data;
    }

    if (cb) {
        mmc_token = mmc_new_completion_token(mmc_card, cb, token);

        if (NULL == mmc_token) {
            ret = -1;
            goto exit_transfer_data;
        }
    }

    ret = host_send_command(
              mmc_card,
              cmd,
              cb ? &mmc_blockop_completion_cb : NULL,
              cb ? mmc_token : NULL);

exit_transfer_data:
    ;
    const bool is_success = (0 == ret);

    // Clean up usually will happen during the callback, so we only clean up
    // here if no callback was given or failure has been encountered.
    if (!cb || !is_success) {
        if (mmc_token) {
            mmc_completion_token_destroy(mmc_token);
        }
        if (cmd)       {
            mmc_cmd_destroy(cmd);
        }
    }

    const size_t bytes_transferred = cb ? 0 : (block_size * nblocks);
    return is_success ? bytes_transferred : ret;
}

long mmc_block_read(
    mmc_card_t *mmc_card,
    unsigned long start,
    int nblocks,
    void *vbuf,
    uintptr_t pbuf,
    mmc_cb cb,
    void *token
)
{
    return transfer_data(
               mmc_card,
               start,
               nblocks,
               vbuf,
               pbuf,
               cb,
               token,
               MMC_READ_SINGLE_BLOCK);
}

long mmc_block_write(
    mmc_card_t *mmc_card,
    unsigned long start,
    int nblocks,
    const void *vbuf,
    uintptr_t pbuf,
    mmc_cb cb,
    void *token
)
{
    // vbuf's `const` gets dropped during the cast as the underlying layer
    // accepts only non-const buffer, however it is ok, as we are sending the
    // write command, what quarantees that the buffer won't be overwritten.
    return transfer_data(
               mmc_card,
               start,
               nblocks,
               (void *)vbuf,
               pbuf,
               cb,
               token,
               MMC_WRITE_BLOCK);
}

long long mmc_card_capacity(mmc_card_t *mmc_card)
{
    int ret;
    csd_t csd;

    ret = mmc_decode_csd(mmc_card, &csd);
    if (ret) {
        return -1;
    }

    long long c_size = (long long)csd.c_size;
    switch (csd.structure) {
    case CSD_VERSION_1: {
        return (c_size + 1) * (1U << (csd.c_size_mult + 2))
               * (1U << csd.read_bl_len);
    }
    case CSD_VERSION_2_AND_3: {
        return (c_size + 1) * 512 * 1024;
    }
    default: {
        return -1;
    }
    }
}


int mmc_nth_irq(mmc_card_t *mmc, int n)
{
    return host_nth_irq(mmc, n);
}

int mmc_handle_irq(mmc_card_t *mmc, int irq)
{
    return host_handle_irq(mmc, irq);
}
