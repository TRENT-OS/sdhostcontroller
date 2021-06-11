/*
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "../../services.h"
#include "../../mmc.h"
#include "../../sdhc.h"

// SD Clock Frequencies (in Hz)
#define SD_CLOCK_ID         400000
#define SD_CLOCK_NORMAL     25000000
#define SD_CLOCK_HIGH       50000000
#define SD_CLOCK_100        100000000
#define SD_CLOCK_208        208000000

// SD specification version code
#define HOST_SPEC_V1 		0x00
#define HOST_SPEC_V2		0x01
#define HOST_SPEC_V3		0x02

// Clock Control Register (0x2c)
#define SDHC_CLOCK_CONTROL_SCE 		(1u << 2) // SC Clock Enable
#define SDHC_CLOCK_CONTROL_ICS 		(1u << 1) // Internal Clock Stable
#define SDHC_CLOCK_CONTROL_ICE 		(1u << 0) // Internal Clock Enable

/*
 * Get clock divider
 *
 * Generally we start with calculating the theoretically closest divider
 * according to the following formula:
 * 		clock_freq = emmc_base_clock / divider;
 *
 * This clock divider can not be immediately used, but has to be modified in
 * order to comply with the SDHC specification. Depending on the SDHC version,
 * the clock divider is calculated differently.
 *
 * SDHC version < 3.00: 8-bit Divided Clock Mode
 * - 80h -> base clock divided by 256
 * - 40h -> base clock divided by 128
 * - 20h -> base clock divided by 64
 * - 10h -> base clock divided by 32
 * - 08h -> base clock divided by 16
 * - 04h -> base clock divided by 8
 * - 02h -> base clock divided by 4
 * - 01h -> base clock divided by 2
 * - 00h -> Base clock (10MHz-63MHz)
 *
 * In a nutshell this means that the actual divider is always a power of 2. But
 * the content that is set in the "SDCLK Frequency Select" field (bits 8-15) in
 * the "Clock Control Register" is always half of it. We need to find a value
 * that is a power of 2 and as close to the theoretically closest divider.
 *
 * SDHC version == 3.00: 10-bit Divided Clock Mode
 * - 3FFh -> 1/2046 Divided Clock
 * - ...  -> ...
 * - N    -> 1/2N Divided Clock (Duty 50%)
 * - ...  -> ...
 * - 002h -> 1/4 Divided Clock
 * - 001h -> 1/2 Divided Clock
 * - 000h -> Base Clock (10MHz-255MHz)
 *
 * In a nutshell this means that the actual divider is always an even number.
 * The value that is set in the "SDCLK Frequency Select" field (bits 8-15) must
 * be half of the actual divider.
 *
 * For more information, see: SDHC specification, ver 3.00, 2.2.14 Clock Control
 * 							  Register (0x2c)
 */
static uint32_t get_clock_divider(
    volatile void *base_addr,
    uint32_t base_clock,
    uint32_t target_freq
)
{
	uint32_t closest = base_clock / target_freq;
	uint32_t divider;

	// Get SDHC version from "Host Controller Version Register" (0xfe)
	int sdhc_version = ((((sdhc_regs_t *)base_addr)->host_version >> 16) & 0xff);
	if(sdhc_version < HOST_SPEC_V3)
	{
		// SDHC version 2.00
		for (size_t i = 0; i < 9; i++)
		{
			divider = (1u << i);
			if(2 * divider >= closest) break;
		}
	}else{
		// SDHC version 3.00
		if(closest % 2 == 0)
		{
			divider = closest / 2;
		}else{
			divider = (closest / 2) + 1;
		}

		if(divider > 0x3ff)
		{
			divider = 0x3ff;
		}
	}

	// divider bits to be set in the "Clock Control Register" (0x2c)
	uint32_t freq_select = (divider & 0xff);
	uint32_t upper_bits =  0;
	if (sdhc_version > HOST_SPEC_V2)
	{
		upper_bits = (divider >> 8) & 0x3;
	}
	uint32_t ret = (freq_select << 8) | (upper_bits << 6);
	return ret;
}

// See: SDHC specification, ver 3.00, section 3.2 SD Clock Control
int sdhc_set_clock(volatile void *base_addr, clock_mode_e clk_mode)
{
	/*
	 * Several forum posts claim that the SD frequency is always 41.6MHz on the
	 * Pi (https://github.com/raspberrypi/linux/issues/467). According to
	 * https://www.raspberrypi.org/forums/viewtopic.php?t=94133, "asking for the
	 * EMMC frequency from the mailbox is pointless on baremetal as all clocks
	 * returned from the mailbox interface are the base clocks for those
	 * peripherals, not the dividers applied using the perpiheral".
	 *
	 * The base clock returned by the mailbox interface is 200MHz. This value
	 * was verified by measuring the oscillation frequency on the CLK pin for
	 * the RPi3 B+. Our measurements confirm that in contrast to the claims
	 * above, we can trust the value returned by the mailbox interface and that
	 * there is no prescale value applied to it.
	 */
    uint32_t base_clock = bcm2837_get_clock_rate (&mbox, CLOCK_ID_EMMC);

	while ((((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CDIHB) ||
		   (((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CIHB));

	uint32_t control = 0;

	// Step 0: Turn off "SD Clock Enable" (bit 2) if SD clock is already enabled
	if(((sdhc_regs_t *)base_addr)->sys_ctrl & SDHC_CLOCK_CONTROL_SCE)
	{
		control = ((sdhc_regs_t *)base_addr)->sys_ctrl;
		control &= ~SDHC_CLOCK_CONTROL_SCE;
        ((sdhc_regs_t *)base_addr)->sys_ctrl = control;
		while ((((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CDIHB) ||
			   (((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CIHB));
	}

	// Step 1: calculate divisor
	uint32_t divider = (clk_mode == CLOCK_INITIAL) ? get_clock_divider(base_addr, base_clock, SD_CLOCK_ID)
										  		   : get_clock_divider(base_addr, base_clock, SD_CLOCK_NORMAL);

	// Step 2: 	Set "Internal Clock Enable" (bit 0) and "SDCLK Frequency
	// 			Select" (bit 8-15)
	control = ((sdhc_regs_t *)base_addr)->sys_ctrl;
	control &= ~0xffe0;	// clear existing divider values
	control |= SDHC_CLOCK_CONTROL_ICE;
	control |= divider;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = control;
	while ((((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CDIHB) ||
		   (((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CIHB));

	// Step 3: Check until "Internal Clock Stable" (bit 1)
	while(!(((sdhc_regs_t *)base_addr)->sys_ctrl & SDHC_CLOCK_CONTROL_ICS));

	// Step 4: Activate "SD Clock Enable" (bit 2)
	control = ((sdhc_regs_t *)base_addr)->sys_ctrl;
	control |= SDHC_CLOCK_CONTROL_SCE;
    ((sdhc_regs_t *)base_addr)->sys_ctrl = control;
	while ((((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CDIHB) ||
		   (((sdhc_regs_t *)base_addr)->pres_state & SDHC_PRES_STATE_CIHB));

    return 0;
}

uint32_t sdhc_set_transfer_mode(sdhc_dev_t *host)
{
    mmc_cmd_t *cmd = host->cmd_list_head;

    uint32_t trans_mode = CMD_XFR_TYP_BCEN;
    if (cmd->data->blocks > 1) {
        trans_mode |= CMD_XFR_TYP_MSBSEL;
    }
    if (cmd->index == MMC_READ_SINGLE_BLOCK) {
        trans_mode |= CMD_XFR_TYP_DTDSEL;
    }
    if (cmd->data != NULL && cmd->data->pbuf != 0) {
        trans_mode |= CMD_XFR_TYP_DMAEN;
    }

    return trans_mode;
}
