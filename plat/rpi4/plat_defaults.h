/* Copyright (C) 2020, Hensoldt Cyber GmbH */
/**
 * @file
 * @brief Platform defaults for the BCM2837 (RPi3 B+).
 *
*/

#pragma once

#define SDHC1_PADDR 0xfe340000
#define SDHC1_SIZE  0x1000
#define SDHC1_IRQ   158

#define MAILBOX_PADDR 0xfe00b000
#define MAILBOX_SIZE  0x1000

#define GPIO_PADDR 0xfe200000
#define GPIO_SIZE  0x1000

#define RASPPI          4

// Set the default port to SDHC1.
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 1)

#define SdHostController_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 1)
