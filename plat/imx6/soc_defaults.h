/* 
* Copyright (C) 2020-2024, HENSOLDT Cyber GmbH
* 
* SPDX-License-Identifier: GPL-2.0-or-later
*
* For commercial licensing, contact: info.cyber@hensoldt.net
*/

/**
 * @file
 * @brief Platform defaults for the i.MX6.
 *
*/

#pragma once

#define SDHC1_PADDR 0x02190000
#define SDHC2_PADDR 0x02194000
#define SDHC3_PADDR 0x02198000
#define SDHC4_PADDR 0x0219C000

#define SDHC1_SIZE  0x1000
#define SDHC2_SIZE  0x1000
#define SDHC3_SIZE  0x1000
#define SDHC4_SIZE  0x1000

#define SDHC1_IRQ   54
#define SDHC2_IRQ   55
#define SDHC3_IRQ   56
#define SDHC4_IRQ   57

/**
 * @brief   Declares the SDHC HW component.
 *
 * @param   _name_ - [in] Component's type name.
 */
#define SdHostController_HW_COMPONENT_DEFINE( \
    _name_) \
    \
    component _name_ { \
        hardware; \
        \
        dataport  Buf   regBase; \
        emits     IRQ   irq; \
    }


/**
 * @brief   Declares the SDHC driver component.
 *
 * @param   _name_ - [in] Component's type name.
 */
#define SdHostController_COMPONENT_DEFINE( \
    _name_) \
    \
    component _name_ { \
        dataport  Buf               regBase; \
        consumes  IRQ               irq; \
        has       mutex             clientMux; \
        \
        provides  if_OS_Storage     storage_rpc; \
        dataport  Buf               storage_port; \
        \
        attribute int               peripheral_idx; \
    }


//------------------------------------------------------------------------------
// Instance Connection

/**
 * @brief   Connects a SDHC driver instance to a HW instance.
 *
 * @param   _inst_drv_  - [in] Component's instance name.
 * @param   _inst_hw_   - [in] Hardware component's instance name.
 */
#define SdHostController_INSTANCE_CONNECT( \
    _inst_drv_, \
    _inst_hw_) \
    \
    connection  seL4HardwareMMIO \
        _inst_drv_ ## _inst_hw_ ## _mmio( \
            from    _inst_drv_.regBase, \
            to      _inst_hw_.regBase \
        ); \
    \
    connection  seL4HardwareInterrupt \
        _inst_drv_ ## _inst_hw_ ## _irq( \
            from    _inst_hw_.irq, \
            to      _inst_drv_.irq \
        );

//------------------------------------------------------------------------------
// Instance Configuration

/**
 * @brief   Configures the SDHC driver component to the passed index value of
 *          the peripheral.
 *
 * @param   _inst_      - [in] Component's instance.
 * @param   _idx_       - [in] Index of the peripheral (sd card slot) to be
 *                             used.
 */
#define SdHostController_INSTANCE_CONFIGURE_BY_INDEX( \
    _inst_, \
    _idx_) \
    \
    _inst_.peripheral_idx    = _idx_;


/**
 * @brief   Configures the SDHC HW component to the passed index value of
 *          the peripheral.
 *
 * @param   _inst_      - [in] Component's instance.
 * @param   _idx_       - [in] Index of the peripheral (sd card slot) to be
 *                             used.
 */
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX( \
    _inst_, \
    _idx_) \
    \
    _inst_.regBase_paddr  = SDHC ## _idx_ ## _PADDR; \
    _inst_.regBase_size   = SDHC ## _idx_ ## _SIZE; \
    _inst_.irq_irq_number = SDHC ## _idx_ ## _IRQ;
