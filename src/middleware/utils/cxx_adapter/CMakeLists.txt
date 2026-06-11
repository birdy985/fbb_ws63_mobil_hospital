#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
#===============================================================================
set(COMPONENT_NAME "cxx_adapter")

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/cxx_adapter.c
)

set(WHOLE_LINK
    true
)

build_component()
