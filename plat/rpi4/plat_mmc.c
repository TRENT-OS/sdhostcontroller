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
    uint32_t host_voltage = 0;
    if(host_is_voltage_compatible(card,3300)){
        host_voltage |= MMC_VDD_32_33 | MMC_VDD_33_34;
    }
    if(host_is_voltage_compatible(card,3000)){
        host_voltage |= MMC_VDD_30_31 | MMC_VDD_31_32;
    }
    if(host_is_voltage_compatible(card,1800)){
        host_voltage |= MMC_VDD_165_195;
    }

    uint32_t acmd41_arg = host_voltage & card->ocr;
    if (acmd41_arg != 0) {
        // set HCS bit
        acmd41_arg |= (1 << 30);
    }
    return acmd41_arg;
}
