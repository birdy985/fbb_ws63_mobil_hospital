#===============================================================================
# @brief    cmake file - Arduino compatibility layer
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
#===============================================================================

set(COMPONENT_NAME "arduino")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Print.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HardwareSerial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wiring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wiring_digital.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wiring_analog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wiring_interrupts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wiring_pulse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TimerClass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Servo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SPI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/I2SClass.cpp
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(PRIVATE_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ROOT_DIR}/include
    ${ROOT_DIR}/include/driver
    ${ROOT_DIR}/include/middleware/utils
    ${ROOT_DIR}/drivers/drivers/hal/uart
    ${ROOT_DIR}/drivers/drivers/hal/i2c
    ${ROOT_DIR}/drivers/drivers/hal/gpio
    ${ROOT_DIR}/drivers/drivers/hal/gpio/v150
    ${ROOT_DIR}/drivers/drivers/hal/spi
    ${ROOT_DIR}/drivers/drivers/hal/timer
    ${ROOT_DIR}/drivers/drivers/hal/sio
    ${ROOT_DIR}/drivers/chips/${CHIP}/porting/uart
    ${ROOT_DIR}/drivers/chips/${CHIP}/porting/i2c
    ${ROOT_DIR}/drivers/chips/${CHIP}/porting/gpio
    ${ROOT_DIR}/drivers/chips/${CHIP}/porting/sio
    ${ROOT_DIR}/drivers/chips/${CHIP}/porting/timer
    ${ROOT_DIR}/middleware/chips/${CHIP}/nv/nv_config/include
    ${ROOT_DIR}/middleware/utils/common_headers
    ${ROOT_DIR}/middleware/utils/common_headers/native
    ${ROOT_DIR}/middleware/utils/hwsec_c/include
    ${ROOT_DIR}/drivers/chips/${CHIP}/include
    ${ROOT_DIR}/drivers/boards/${CHIP}/evb/memory_config/include/product
    ${ROOT_DIR}/kernel/osal/include
    ${ROOT_DIR}/kernel/osal/include/lock
    ${ROOT_DIR}/kernel/osal/include/schedule
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/DHT
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/OneWire
)

set(PRIVATE_DEFINES
)

set(PUBLIC_DEFINES
    SUPPORT_CXX
)

# use this when you want to add ccflags like -include xxx
set(COMPONENT_PUBLIC_CCFLAGS
)

# REWRITE_CCFLAGS=true: use only COMPONENT_CCFLAGS, discard global CCFLAGS
# This prevents C-only flags (-std=gnu99, -Wstrict-prototypes, etc.) from being
# passed to the C++ compiler (cc1plus), which would otherwise cause errors.
set(REWRITE_CCFLAGS
    true
)

set(COMPONENT_CCFLAGS
    -mabi=ilp32f
    -march=rv32imfc
    -Wall
    -Werror
    -Wextra
    -Winit-self
    -Wpointer-arith
    -Wno-type-limits
    -fno-strict-aliasing
    -Os
    -fno-unwind-tables
    -fno-exceptions
    -fno-rtti
    -nostdinc++
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
    -Wno-sign-compare
    -fno-builtin-printf
)

set(WHOLE_LINK
    true
)

set(MAIN_COMPONENT
    false
)

# Link C++ standard library (required for C++ sources using new/delete, virtual functions, etc.)
set(LIBS
    supc++
)

build_component()