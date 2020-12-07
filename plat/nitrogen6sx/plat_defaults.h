/* Copyright (C) 2020, Hensoldt Cyber GmbH */
/**
 * @file
 * @brief Platform configuration defaults for the Nitrogen6_SoloX
 *
*/

#pragma once

#include "../imx6/soc_defaults.h"

// Set the default port to SDHC2, as this is the port connected to the microSD
// slot on the Nitrogen6_SoloX.
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 2)

#define SdHostController_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 2)
