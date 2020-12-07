/* Copyright (C) 2020, Hensoldt Cyber GmbH */
/**
 * @file
 * @brief Platform configuration defaults for the BD-SL-i.MX6
 *
*/

#pragma once

#include "../imx6/soc_defaults.h"

// Set the default port to SDHC4. This port is connect to the microSD slot on
// the BD-SL-i.MX6. A possible alternative setting that can be chosen here is
// SDHC3, which targets the Standard SD Slot on the board.
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 4)

#define SdHostController_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 4)
