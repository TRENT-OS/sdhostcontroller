#
# SdHostController driver
#
# Copyright (C) 2020, Hensoldt Cyber GmbH
#

cmake_minimum_required(VERSION 3.17)

if (SDK_USE_CAMKES)

    # folder contains platform specific defaults
    CAmkESAddCPPInclude("${CMAKE_CURRENT_LIST_DIR}/plat/${PLATFORM}")

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
        C_FLAGS
            -Wall
            -Werror
        LIBS
            os_core_api
            lib_debug
            lib_compiler
            lib_util
            sdhcdrivers
    )

endfunction()
