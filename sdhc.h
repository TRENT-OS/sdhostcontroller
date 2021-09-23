/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Original file at https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/src/sdhc.h
 */

#pragma once

#include <platsupport/io.h>
#include <sdio.h>

typedef volatile struct sdhc_regs_s {
    uint32_t ds_addr;               //0x00 DMA System Address
    uint32_t blk_att;               //0x04 Block Attributes
    uint32_t cmd_arg;               //0x08 Command Argument
    uint32_t cmd_xfr_typ;           //0x0C Command Transfer Type
    uint32_t cmd_rsp0;              //0x10 Command Response0
    uint32_t cmd_rsp1;              //0x14 Command Response1
    uint32_t cmd_rsp2;              //0x18 Command Response2
    uint32_t cmd_rsp3;              //0x1C Command Response3
    uint32_t data_buff_acc_port;    //0x20 Data Buffer Access Port
    uint32_t pres_state;            //0x24 Present State
    uint32_t prot_ctrl;             //0x28 Protocol Control
    uint32_t sys_ctrl;              //0x2C System Control
    uint32_t int_status;            //0x30 Interrupt Status
    uint32_t int_status_en;         //0x34 Interrupt Status Enable
    uint32_t int_signal_en;         //0x38 Interrupt Signal Enable
    uint32_t autocmd12_err_status;  //0x3C Auto CMD12 Error Status
    uint32_t host_ctrl_cap;         //0x40 Host Controller Capabilities
    uint32_t wtmk_lvl;              //0x44 Watermark Level
    uint64_t mix_ctrl;              //0x48 Mixer Control
    uint32_t force_event;           //0x50 Force Event
    uint32_t adma_err_status;       //0x54 ADMA Error Status Register
    uint64_t adma_sys_addr;         //0x58 ADMA System Address
    uint32_t dll_ctrl;              //0x60 DLL (Delay Line) Control
    uint32_t dll_status;            //0x64 DLL Status
    uint64_t clk_tune_ctrl_status;  //0x68 CLK Tuning Control and Status
    uint32_t unused1[20];           //0x70-0xC0
    uint32_t vend_spec;             //0xC0 Vendor Specific Register
    uint32_t mmc_boot;              //0xC4 MMC Boot Register
    uint64_t vend_spec2;            //0xC8 Vendor Specific 2 Register
    uint32_t unused2[11];           //0xD0-0xFC
    uint32_t host_version;          //0xFC Host Version (0xFE adjusted for alignment)
}
sdhc_regs_t;

/* Block Attributes Register */
#define BLK_ATT_BLKCNT_SHF      16        //Blocks Count For Current Transfer
#define BLK_ATT_BLKCNT_MASK     0xFFFF    //Blocks Count For Current Transfer
#define BLK_ATT_BLKSIZE_SHF     0         //Transfer Block Size
#define BLK_ATT_BLKSIZE_MASK    0xFFF     //Transfer Block Size

/* Command Transfer Type Register */
#define CMD_XFR_TYP_CMDINX_SHF  24        //Command Index
#define CMD_XFR_TYP_CMDINX_MASK 0x3F      //Command Index
#define CMD_XFR_TYP_CMDTYP_SHF  22        //Command Type
#define CMD_XFR_TYP_CMDTYP_MASK 0x3       //Command Type
#define CMD_XFR_TYP_DPSEL       (1 << 21) //Data Present Select
#define CMD_XFR_TYP_CICEN       (1 << 20) //Command Index Check Enable
#define CMD_XFR_TYP_CCCEN       (1 << 19) //Command CRC Check Enable
#define CMD_XFR_TYP_RSPTYP_SHF  16        //Response Type Select
#define CMD_XFR_TYP_RSPTYP_MASK 0x3       //Response Type Select
#define CMD_XFR_TYP_MSBSEL      (1 << 5)  //Multi/Single Block Select.
#define CMD_XFR_TYP_DTDSEL      (1 << 4)  //Data Transfer Direction Select.
#define CMD_XFR_TYP_DDR_EN      (1 << 3)  //Dual Data Rate mode selection
#define CMD_XFR_TYP_AC12EN      (1 << 2)  //Auto CMD12 Enable
#define CMD_XFR_TYP_BCEN        (1 << 1)  //Block Count Enable
#define CMD_XFR_TYP_DMAEN       (1 << 0)  //DMA Enable

/* System Control Register */
#define SYS_CTRL_INITA          (1 << 27) //Initialization Active
#define SYS_CTRL_RSTD           (1 << 26) //Software Reset for DAT Line
#define SYS_CTRL_RSTC           (1 << 25) //Software Reset for CMD Line
#define SYS_CTRL_RSTA           (1 << 24) //Software Reset for ALL
#define SYS_CTRL_DTOCV_SHF      16        //Data Timeout Counter Value
#define SYS_CTRL_DTOCV_MASK     0xF       //Data Timeout Counter Value
#define SYS_CTRL_SDCLKS_SHF     8         //SDCLK Frequency Select
#define SYS_CTRL_SDCLKS_MASK    0xFF      //SDCLK Frequency Select
#define SYS_CTRL_DVS_SHF        4         //Divisor
#define SYS_CTRL_DVS_MASK       0xF       //Divisor
#define SYS_CTRL_CLK_INT_EN     (1 << 0)  //Internal clock enable (exl. IMX6)
#define SYS_CTRL_CLK_INT_STABLE (1 << 1)  //Internal clock stable (exl. IMX6)
#define SYS_CTRL_CLK_CARD_EN    (1 << 2)  //SD clock enable       (exl. IMX6)

/* Interrupt Status Register */
#define INT_STATUS_DMAE         (1 << 28) //DMA Error            (only IMX6)
#define INT_STATUS_TNE          (1 << 26) //Tuning Error
#define INT_STATUS_ADMAE        (1 << 25) //ADMA error           (exl. IMX6)
#define INT_STATUS_AC12E        (1 << 24) //Auto CMD12 Error
#define INT_STATUS_OVRCURE      (1 << 23) //Bus over current     (exl. IMX6)
#define INT_STATUS_DEBE         (1 << 22) //Data End Bit Error
#define INT_STATUS_DCE          (1 << 21) //Data CRC Error
#define INT_STATUS_DTOE         (1 << 20) //Data Timeout Error
#define INT_STATUS_CIE          (1 << 19) //Command Index Error
#define INT_STATUS_CEBE         (1 << 18) //Command End Bit Error
#define INT_STATUS_CCE          (1 << 17) //Command CRC Error
#define INT_STATUS_CTOE         (1 << 16) //Command Timeout Error
#define INT_STATUS_ERR          (1 << 15) //Error interrupt      (exl. IMX6)
#define INT_STATUS_TP           (1 << 14) //Tuning Pass
#define INT_STATUS_RTE          (1 << 12) //Re-Tuning Event
#define INT_STATUS_CINT         (1 << 8)  //Card Interrupt
#define INT_STATUS_CRM          (1 << 7)  //Card Removal
#define INT_STATUS_CINS         (1 << 6)  //Card Insertion
#define INT_STATUS_BRR          (1 << 5)  //Buffer Read Ready
#define INT_STATUS_BWR          (1 << 4)  //Buffer Write Ready
#define INT_STATUS_DINT         (1 << 3)  //DMA Interrupt
#define INT_STATUS_BGE          (1 << 2)  //Block Gap Event
#define INT_STATUS_TC           (1 << 1)  //Transfer Complete
#define INT_STATUS_CC           (1 << 0)  //Command Complete

/* Host Controller Capabilities Register */
#define HOST_CTRL_CAP_VS18      (1 << 26) //Voltage Support 1.8V
#define HOST_CTRL_CAP_VS30      (1 << 25) //Voltage Support 3.0V
#define HOST_CTRL_CAP_VS33      (1 << 24) //Voltage Support 3.3V
#define HOST_CTRL_CAP_SRS       (1 << 23) //Suspend/Resume Support
#define HOST_CTRL_CAP_DMAS      (1 << 22) //DMA Support
#define HOST_CTRL_CAP_HSS       (1 << 21) //High Speed Support
#define HOST_CTRL_CAP_ADMAS     (1 << 20) //ADMA Support
#define HOST_CTRL_CAP_MBL_SHF   16        //Max Block Length
#define HOST_CTRL_CAP_MBL_MASK  0x3       //Max Block Length

typedef enum {
    DMA_MODE_NONE = 0,
    DMA_MODE_SDMA,
    DMA_MODE_ADMA
}
dma_mode_e;

typedef enum {
    DIV_1   = 0x0,
    DIV_2   = 0x1,
    DIV_3   = 0x2,
    DIV_4   = 0x3,
    DIV_5   = 0x4,
    DIV_6   = 0x5,
    DIV_7   = 0x6,
    DIV_8   = 0x7,
    DIV_9   = 0x8,
    DIV_10  = 0x9,
    DIV_11  = 0xa,
    DIV_12  = 0xb,
    DIV_13  = 0xc,
    DIV_14  = 0xd,
    DIV_15  = 0xe,
    DIV_16  = 0xf,
}
divisor_e;

/* Selecting the prescaler value varies between SDR and DDR mode. When the
 * value is set, this is accounted for with a bitshift (PRESCALER_X >> 1) */
typedef enum {
    PRESCALER_1   = 0x0, //Only available in SDR mode
    PRESCALER_2   = 0x1,
    PRESCALER_4   = 0x2,
    PRESCALER_8   = 0x4,
    PRESCALER_16  = 0x8,
    PRESCALER_32  = 0x10,
    PRESCALER_64  = 0x20,
    PRESCALER_128 = 0x40,
    PRESCALER_256 = 0x80,
    PRESCALER_512 = 0x100, //Only available in DDR mode
}
sdclk_frequency_select_e;

typedef enum {
    CLOCK_INITIAL = 0,
    CLOCK_OPERATIONAL
}
clock_mode_e;

typedef enum {
    SDCLK_TIMES_2_POW_29 = 0xf,
    SDCLK_TIMES_2_POW_28 = 0xe,
    SDCLK_TIMES_2_POW_14 = 0x0,
}
data_timeout_counter_val_e;

typedef struct sdhc_dev_s {
    /* Device data */
    void *base;
    int version;
    int nirqs;
    const int *irq_table;
    /* Transaction queue */
    mmc_cmd_t *cmd_list_head;
    mmc_cmd_t **cmd_list_tail;
    int blocks_remaining;
    /* DMA allocator */
    const ps_dma_man_t *dalloc;
}
sdhc_dev_t;

int sdhc_init(
    void *iobase,
    const int *irq_table,
    int nirqs,
    ps_io_ops_t *io_ops,
    sdio_host_dev_t *dev
);

/**
 * Configure SDHC clock properly for a specific SoC/board.
 * @param[in] base_addr     Base address of the SDHC peripheral.
 * @param[in] clk_mode      Clock mode (init: 400kHz, trans: 25MHz)
 * @result Return 0 on success
 */
int sdhc_set_clock(volatile void *base_addr, clock_mode_e clk_mode);

/**
 * Return transfer bit mask for a specific SoC/board.
 * @param[in] host          A handle to an initialised host controller
 * @result Return transfer bit mask.
 */
uint32_t sdhc_set_transfer_mode(sdhc_dev_t *host);

/**
 * Set voltage level of SoC explicitly.
 * @param[in] host          A handle to an initialised host controller
 */
void sdhc_set_voltage_level(sdhc_dev_t *host);

/**
 * Inter-command delay.
 */
void sdhc_inter_command_delay(void);
