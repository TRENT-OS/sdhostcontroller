/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Original file at https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/src/mmc.h
 *                  https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/include/sdhc/mmc.h
 */

#pragma once

#include <platsupport/io.h>
#include "sdhc.h"
#include "sdio.h"

/* MMC Standard Command Index,     Response Type */
#define MMC_GO_IDLE_STATE         0  //NONE
#define MMC_SEND_OP_COND          1  //R3
#define MMC_ALL_SEND_CID          2  //R2
#define MMC_SEND_RELATIVE_ADDR    3  //R1
#define MMC_SET_DSR               4  //NONE
#define MMC_IO_SEND_OP_COND       5  //R4
#define MMC_SWITCH                6  //R1
#define MMC_SELECT_CARD           7  //R1b
#define MMC_SEND_EXT_CSD          8  //R1
#define MMC_SEND_CSD              9  //R2
#define MMC_SEND_CID              10 //R2
#define MMC_READ_DAT_UNTIL_STOP   11 //R1
#define MMC_STOP_TRANSMISSION     12 //R1b
#define MMC_SEND_STATUS           13 //R1
#define MMC_GO_INACTIVE_STATE     15 //NONE
#define MMC_SET_BLOCKLEN          16 //R1
#define MMC_READ_SINGLE_BLOCK     17 //R1
#define MMC_READ_MULTIPLE_BLOCK   18 //R1
#define MMC_WRITE_DAT_UNTIL_STOP  20 //R1
#define MMC_WRITE_BLOCK           24 //R1
#define MMC_WRITE_MULTIPLE_BLOCK  25 //R1
#define MMC_PROGRAM_CID           26 //R1
#define MMC_PROGRAM_CSD           27 //R1
#define MMC_SET_WRITE_PROT        28 //R1b
#define MMC_CLR_WRITE_PROT        29 //R1b
#define MMC_SEND_WRITE_PROT       30 //R1
#define MMC_TAG_SECTOR_START      32 //R1
#define MMC_TAG_SECTOR_END        33 //R1
#define MMC_UNTAG_SECTOR          34 //R1
#define MMC_TAG_ERASE_GROUP_START 35 //R1
#define MMC_TAG_ERASE_GROUP_END   36 //R1
#define MMC_UNTAG_ERASE_GROUP     37 //R1
#define MMC_ERASE                 38 //R1b
#define MMC_FAST_IO               39 //R4
#define MMC_GO_IRQ_STATE          40 //R5
#define MMC_LOCK_UNLOCK           42 //R1b
#define MMC_IO_RW_DIRECT          52 //R5
#define MMC_IO_RW_EXTENDED        53 //R5
#define MMC_APP_CMD               55 //R1
#define MMC_GEN_CMD               56 //R1b
#define MMC_RW_MULTIPLE_REGISTER  60 //R1b
#define MMC_RW_MULTIPLE_BLOCK     61 //R1b

/* Application Specific Command(ACMD). */
#define SD_SET_BUS_WIDTH          6  //R1
#define SD_SD_STATUS              13 //R1
#define SD_SEND_NUM_WR_SECTORS    22 //R1
#define SD_SET_WR_BLK_ERASE_COUNT 23 //R1
#define SD_SD_APP_OP_COND         41 //R1
#define SD_SET_CLR_CARD_DETECT    42 //R1
#define SD_SEND_SCR               51 //R1

/* MMC Voltage Level */
#define MMC_VDD_33_34             (1 << 21)
#define MMC_VDD_32_33             (1 << 20)
#define MMC_VDD_31_32             (1 << 19)
#define MMC_VDD_30_31             (1 << 18)
#define MMC_VDD_29_30             (1 << 17)

/* Bus width */
#define MMC_MODE_8BIT       0x04
#define MMC_MODE_4BIT       0x02

typedef enum {
    MMC_RSP_TYPE_NONE = 0,
    MMC_RSP_TYPE_R1,
    MMC_RSP_TYPE_R1b,
    MMC_RSP_TYPE_R2,
    MMC_RSP_TYPE_R3,
    MMC_RSP_TYPE_R4,
    MMC_RSP_TYPE_R5,
    MMC_RSP_TYPE_R5b,
    MMC_RSP_TYPE_R6,
}
mmc_rsp_type_e;

typedef enum {
    CARD_TYPE_UNKNOWN = 0,
    CARD_TYPE_MMC,
    CARD_TYPE_SD,
    CARD_TYPE_SDIO,
}
mmc_card_type_e;

typedef enum {
    CARD_STS_ACTIVE = 0,
    CARD_STS_INACTIVE,
    CARD_STS_BUSY,
}
mmc_card_status_e;

typedef struct mmc_data_s {
    uintptr_t  pbuf;
    void      *vbuf;
    uint32_t   data_addr;
    uint32_t   block_size;
    uint32_t   blocks;
}
mmc_data_t;

typedef struct mmc_cmd_s {
    /* Data */
    uint32_t index;
    uint32_t arg;
    uint32_t response[4];
    mmc_data_t *data;
    /* Type */
    mmc_rsp_type_e rsp_type;
    /* For async handling */
    sdio_cb         cb;
    void           *token;
    /* For queueing */
    struct mmc_cmd_s *next;
    int complete;
}
mmc_cmd_t;

typedef struct cid_s {
    uint8_t reserved;
    uint8_t manfid;
    union {
        struct {
            uint8_t  bga;
            uint8_t  oemid;
            char     name[6];
            uint8_t  rev;
            uint32_t serial;
            uint8_t  date;
        } mmc_cid;
        struct {
            uint16_t oemid;
            char     name[5];
            uint8_t  rev;
            uint32_t serial;
            uint16_t date;
        } sd_cid;
    };
}
__attribute__((packed))
cid_t;

typedef struct csd_s {
    uint8_t structure;
    uint8_t tran_speed;
    uint8_t read_bl_len;
    uint32_t c_size;
    uint8_t  c_size_mult;
}
csd_t;

typedef struct mmc_card_s {
    uint32_t ocr;
    uint32_t raw_cid[4];
    uint32_t raw_csd[4];
    uint16_t raw_rca;
    uint32_t raw_scr[2];
    uint32_t type;
    uint32_t voltage;
    uint32_t version;
    uint32_t high_capacity;
    uint32_t status;
    const ps_dma_man_t *dalloc;
    sdio_host_dev_t *sdio;
}
mmc_card_t;

typedef void (*mmc_cb)(mmc_card_t *mmc_card, int status, size_t bytes_transferred, void *token);

//------------------------------------------------------------------------------
// MMC specific functions

static inline size_t mmc_block_size(mmc_card_t *mmc_card)
{
    return 512;
}

/** Initialise an MMC card
 * @param[in]  sdio_dev      An sdio device structure to bind the MMC driver to
 *                           probe
 * @param[in]  io_ops        Handle to a structure which provides IO
 *                           and DMA operations.
 * @param[out] mmc_card      On success, this will be filled with
 *                           a handle to the MMC card
 *                           associated with the provided id.
 * @return                   0 on success.
 */
int mmc_init(sdio_host_dev_t *sdio, ps_io_ops_t *io_ops, mmc_card_t **mmc_card);

/** Read blocks from the MMC
 * The client may use either physical or virtual address for the transfer depending
 * on the DMA requirements of the underlying driver. It is recommended to provide
 * both for rebustness.
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @param[in] start     the starting block number of the operation
 * @param[in] nblocks   The number of blocks to read
 * @param[in] vbuf      The virtual address of a buffer to read the data into
 * @param[in] pbuf      The physical address of a buffer to read the data into
 * @param[in] cb        A callback function to call when the transaction completes.
 *                      If NULL is passed as this argument, the call will be blocking.
 * @param[in] token     A token to pass, unmodified, to the provided callback function.

 * @return              The number of bytes read, negative on failure.
 */
long mmc_block_read(
    mmc_card_t *mmc_card,
    unsigned long start_block,
    int nblocks,
    void *vbuf,
    uintptr_t pbuf,
    mmc_cb cb,
    void *token
);

/** Write blocks to the MMC
 * The client may use either physical or virtual address for the transfer depending
 * on the DMA requirements of the underlying driver. It is recommended to provide
 * both for rebustness.
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @param[in] start     The starting block number of the operation
 * @param[in] nblocks   The number of blocks to write
 * @param[in] vbuf      The virtual address of a buffer that contains the data to be written
 * @param[in] pbuf      The physical address of a buffer that contains the data to be written
 * @param[in] cb        A callback function to call when the transaction completes.
 *                      If NULL is passed as this argument, the call will be blocking.
 * @param[in] token     A token to pass, unmodified, to the provided callback function.
 * @return              The number of bytes read, negative on failure.
 */
long mmc_block_write(
    mmc_card_t *mmc_card,
    unsigned long start_block,
    int nblocks,
    const void *vbuf,
    uintptr_t pbuf,
    mmc_cb cb,
    void *token
);

/**
 * Returns the nth IRQ that this underlying device generates
 * @param[in] mmc  A handle to an initialised MMC card
 * @param[in] n    Index of the desired IRQ.
 * @return         The IRQ number, or -1 if n is invalid
 */
int mmc_nth_irq(mmc_card_t *mmc, int n);

/**
 * Passes control to the IRQ handler of the MMC host controller
 * @param[in] mmc  A handle to an initialised MMC card
 * @param[in] irq  The IRQ number that was triggered.
 * @return         0 if an IRQ was handled
 */
int mmc_handle_irq(mmc_card_t *mmc, int irq);

/** Get card capacity
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @return              Card capacity in bytes, negative on failure.
 */
long long mmc_card_capacity(mmc_card_t *mmc_card);

//------------------------------------------------------------------------------
// Wrapper functions

static inline int host_send_command(
    mmc_card_t *card,
    mmc_cmd_t *cmd,
    sdio_cb cb,
    void *token
)
{
    return sdio_send_command(card->sdio, cmd, cb, token);
}

static inline int host_nth_irq(mmc_card_t *card, int n)
{
    return sdio_nth_irq(card->sdio, n);
}

static inline int host_handle_irq(mmc_card_t *card, int irq)
{
    return sdio_handle_irq(card->sdio, irq);
}

static inline int host_is_voltage_compatible(mmc_card_t *card, int mv)
{
    return sdio_is_voltage_compatible(card->sdio, mv);
}

static inline int host_reset(mmc_card_t *card)
{
    return sdio_reset(card->sdio);
}

static inline int host_set_operational(mmc_card_t *card)
{
    return sdio_set_operational(card->sdio);
}
