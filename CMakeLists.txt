#
# SdHostController driver
# 
# Copyright (C) 2021-2024, HENSOLDT Cyber GmbH
# 
# SPDX-License-Identifier: GPL-2.0-or-later
#
# For commercial licensing, contact: info.cyber@hensoldt.net
#

cmake_minimum_required(VERSION 3.17)

if (SDK_USE_CAMKES)

    # folder contains platform specific defaults
    CAmkESAddCPPInclude("plat/${PLATFORM}")

endif()

#-------------------------------------------------------------------------------
#
# Declare SdHostController CAmkES Component
#
function(SdHostController_DeclareCAmkESComponent
    name
)

    DeclareCAmkESComponent(
        ${name}
        SOURCES
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SdHostController.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mmc.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/sdhc.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plat/${PLATFORM}/plat_sdio.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plat/${PLATFORM}/plat_mmc.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plat/${PLATFORM}/plat_sdhc.c
        INCLUDES
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plat/${PLATFORM}
        C_FLAGS
            -Wall
            -Werror
        LIBS
            os_core_api
            lib_debug
            lib_compiler
            lib_utils
    )

endfunction()
