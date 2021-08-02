/* Copyright (C) 2021, HENSOLDT Cyber GmbH */

#include <mmc.h>

uint32_t mmc_get_voltage(mmc_card_t *card)
{
    // 3.3V VDD range: 2.7-3.6V
    uint32_t voltage = MMC_VDD_27_28 | MMC_VDD_28_29 | MMC_VDD_29_30 |
                       MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
                       MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36;
    if ((card->type != CARD_TYPE_MMC)
        && host_is_voltage_compatible(card, 3300)
        && (card->ocr & voltage)) {
        voltage |= (1 << 30); // HCS bit
    }
    return voltage;
}
