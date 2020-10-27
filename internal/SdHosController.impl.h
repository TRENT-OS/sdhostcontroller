/* Copyright (C) 2020, HENSOLDT Cyber GmbH */

//------------------------------------------------------------------------------
// Component

/**
 * @brief   Declares the SDHC HW component.
 *
 * @note    Feel free to use the DECLARE_COMPONENT_SDHC wrapper to declare the
 *          pair of the Driver and the Hardware components.
 *
 * @param   _type_hw_ - [in] Component's type name.
 */
#define DECLARE_COMPONENT_SDHC_HW(_type_hw_) \
    \
    component _type_hw_ { \
        hardware; \
        \
        dataport  Buf   regBase; \
        emits     IRQ   irq; \
    }

/**
 * @brief   Declares the SDHC driver component.
 *
 * @note    Feel free to use the DECLARE_COMPONENT_SDHC wrapper to declare the
 *          pair of the Driver and Hw components.
 *
 * @param   _name_ - [in] Component's type name.
 */
#define DECLARE_COMPONENT_SDHC_DRV(_name_) \
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
 * @brief   Declares and connects the SDHC driver component to the HW part.
 *
 * @note    Feel free to use the DECLARE_AND_CONNECT_INSTANCE_SDHC wrapper to
 *          declare the pair of the Driver and Hw components.
 *
 * @param   _type_hw_   - [in] Hardware component's type name.
 * @param   _inst_hw_   - [in] Hardware component's instance name.
 * @param   _type_drv_  - [in] Component's type name.
 * @param   _inst_drv_  - [in] Component's instance name.
 */
#define DECLARE_AND_CONNECT_INSTANCE_SDHC_DRV_HW( \
    _type_hw_, \
    _inst_hw_, \
    _type_drv_, \
    _inst_drv_) \
    \
    component   _type_hw_   _inst_hw_; \
    component   _type_drv_  _inst_drv_; \
    \
    connection  seL4HardwareMMIO       _inst_drv_ ## _inst_hw_ ## _mmio( \
                from _inst_drv_.regBase, \
                to   _inst_hw_.regBase); \
    \
    connection  seL4HardwareInterrupt  _inst_drv_ ## _inst_hw_ ## _irq( \
                from _inst_hw_.irq, \
                to   _inst_drv_.irq);

//------------------------------------------------------------------------------
// Instance Configuration

/**
 * @brief   Configures the SDHC HW component.
 *
 * @note    Feel free to use the CONFIGURE_INSTANCE_SDHC wrapper so that HW part
 *          is not visible from the upper layers.
 *
 * @param   _inst_hw_   - [in] Hardware component's instance.
 * @param   _inst_      - [in] Component's instance.
 * @param   _idx_       - [in] Index of the peripheral (sd card slot) to be
 *                             used.
 */
#define CONFIGURE_INSTANCE_SDHC_HW( \
            _inst_hw_, \
            _inst_, \
            _idx_) \
    \
    _inst_hw_.regBase_paddr  = SDHC ## _idx_ ## _PADDR; \
    _inst_hw_.regBase_size   = SDHC ## _idx_ ## _SIZE; \
    _inst_hw_.irq_irq_number = SDHC ## _idx_ ## _IRQ; \
    \
    _inst_.peripheral_idx    = _idx_; \
