/* 
* Copyright (C) 2021-2024, HENSOLDT Cyber GmbH
* 
* SPDX-License-Identifier: GPL-2.0-or-later
*
* For commercial licensing, contact: info.cyber@hensoldt.net
*/

#include <mmc.h>

uint32_t mmc_get_voltage(mmc_card_t *card)
{
    // The "Capabilities Register" (0x40) is not working for the RPi3, so we can
    // not check what voltage ranges are supported. For now, we simply assume a
    // 3.0 V and 3.3 V range.
    uint32_t host_voltage = MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33
                            | MMC_VDD_33_34;

    uint32_t acmd41_arg = host_voltage & card->ocr;
    if (acmd41_arg != 0) {
        // set HCS bit
        acmd41_arg |= (1 << 30);
    }
    return acmd41_arg;
}