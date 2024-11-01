/* 
* Copyright (C) 2021-2024, HENSOLDT Cyber GmbH
* 
* SPDX-License-Identifier: GPL-2.0-or-later
*
* For commercial licensing, contact: info.cyber@hensoldt.net
*/

/**
 * @file
 * @brief Platform defaults for the BCM2711 (RPi4).
 *
*/

#pragma once

#define SDHC1_PADDR     0xfe340000
#define SDHC1_SIZE      0x1000
#define SDHC1_IRQ       158

#define MAILBOX_PADDR   0xfe00b000
#define MAILBOX_SIZE    0x1000

#define GPIO_PADDR      0xfe200000
#define GPIO_SIZE       0x1000


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
        dataport  Buf   mailboxBase; \
        dataport  Buf   gpioBase; \
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
        dataport  Buf               mailboxBase; \
        dataport  Buf               gpioBase; \
        consumes  IRQ               irq; \
        has       mutex             clientMux; \
        \
        provides  if_OS_Storage     storage_rpc; \
        dataport  Buf               storage_port; \
        \
        attribute int               peripheral_idx; \
        attribute int               dma_pool_paddr = 0x30000000; \
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
    connection  seL4HardwareMMIO \
        _inst_drv_ ## _mailboxBase_ ## _mmio( \
            from    _inst_drv_.mailboxBase, \
            to      _inst_hw_.mailboxBase \
        ); \
    \
    connection  seL4HardwareMMIO \
        _inst_drv_ ## _gpioBase_ ## _mmio( \
            from    _inst_drv_.gpioBase, \
            to      _inst_hw_.gpioBase \
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
 * @brief   Configures the SDHC HW component to the default sd card slot.
 *
 * @param   _inst_      - [in] Component's HW instance.
 */
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 1)


/**
 * @brief   Configures the SDHC driver component to the default sd card slot.
 *
 * @param   _inst_      - [in] Component's instance.
 */
#define SdHostController_INSTANCE_CONFIGURE_BY_DEFAULT(_inst_) \
    SdHostController_INSTANCE_CONFIGURE_BY_INDEX(_inst_, 1)


/**
 * @brief   Configures the SDHC HW component to the passed index value of
 *          the peripheral.
 *
 * @param   _inst_      - [in] Component's HW instance.
 * @param   _idx_       - [in] Index of the peripheral (sd card slot) to be
 *                             used.
 */
#define SdHostController_HW_INSTANCE_CONFIGURE_BY_INDEX( \
    _inst_, \
    _idx_) \
    \
    _inst_.regBase_paddr     = SDHC ## _idx_ ## _PADDR; \
    _inst_.regBase_size      = SDHC ## _idx_ ## _SIZE; \
    _inst_.mailboxBase_paddr = MAILBOX_PADDR; \
    _inst_.mailboxBase_size  = MAILBOX_SIZE; \
    _inst_.gpioBase_paddr    = GPIO_PADDR; \
    _inst_.gpioBase_size     = GPIO_SIZE; \
    _inst_.irq_irq_number    = SDHC ## _idx_ ## _IRQ;


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
    _inst_.peripheral_idx    = _idx_; \
    _inst_.dma_pool  = 4096;
