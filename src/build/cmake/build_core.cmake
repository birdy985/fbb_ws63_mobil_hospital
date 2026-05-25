#===============================================================================
# @brief    build core - shared by in-tree root CMakeLists and out-of-tree
#           project.cmake. Provides prologue / epilogue macros that bracket
#           the literal project() call required by CMake.
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2026. All rights reserved.
#===============================================================================

# Caller contract:
#   - cmake_minimum_required already called
#   - ROOT_DIR already defined (in-tree: PROJECT_SOURCE_DIR; out-of-tree: project.cmake derives it)
#   - CHIP, BIN_NAME, CCFLAGS, BUILD_TYPE etc. already passed via -D
#
# Usage (in caller's CMakeLists.txt):
#   include("${ROOT_DIR}/build/cmake/build_core.cmake")
#   cfbb_build_prologue()
#   project(${CHIP}_CFBB C ASM CXX)
#   cfbb_build_epilogue()

# --------------------------------------------------------------------
# Prologue - runs before project(); sets up compile env, platform name,
# kconfig helpers, and includes the foundational cmake modules.
# --------------------------------------------------------------------
macro(cfbb_build_prologue)
    set(CMAKE_SYSTEM_NAME "Generic")

    if(DEFINED ENV{build_ws63_sdk_open})
        set(build_lib true)
    else()
        set(build_lib false)
    endif()

    if(NOT DEFINED CHIP)
        message(FATAL_ERROR "Chip is not defined ")
    endif()

    if(NOT DEFINED ROOT_DIR)
        message(FATAL_ERROR "ROOT_DIR must be defined before cfbb_build_prologue()")
    endif()
    # Windows shells may pass backslashes; CMake parses them as escape
    file(TO_CMAKE_PATH "${ROOT_DIR}" ROOT_DIR)

    set(Python3_EXECUTABLE ${PY_PATH})
    find_program(CCACHE_FOUND ccache)
    if(CCACHE_FOUND)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    endif(CCACHE_FOUND)

    # default hso module_name & AUTO_DEF_FILE_ID & AUTO_DEF_MODULE_ID, redefine these var in sub cmake file will
    # override these value just in sub directory but not others
    set(MODULE_NAME "pf")
    set(AUTO_DEF_FILE_ID TRUE)
    set(AUTO_DEF_MODULE_ID TRUE)

    set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
    set(CMAKE_DIR "${ROOT_DIR}/build/cmake")
    set(BIN_DIR   "${ROOT_DIR}/interim_binary")

    include("${CMAKE_DIR}/build_function.cmake")
    include("${CMAKE_DIR}/global_variable.cmake")
    include("${CMAKE_DIR}/build_script.cmake")
    include("${CMAKE_DIR}/build_command.cmake")
    include("${CMAKE_DIR}/build_hso_database.cmake")
    include("${CMAKE_DIR}/build_component.cmake")
    include("${CMAKE_DIR}/build_sdk.cmake")
    include_directories(${ROOT_DIR}/kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/targets/ws63/include)

    if(EXISTS "${ROOT_DIR}/kernel/liteos/liteos_v208.5.0/${TARGET_COMMAND}")
        set(build_ws63_sdk_open true)
    endif ()

    # UT / FUZZ early-exit paths: returning from a macro returns from caller's
    # scope (root CMakeLists.txt) — so project() and epilogue are skipped.
    if(${BUILD_TYPE} STREQUAL "UT")
        KCONFIG_GET_PARAMS("${ROOT_DIR}/build/menuconfig/test/platform/ut.config")
        include("${CMAKE_DIR}/build_ut.cmake")
        return()
    endif()

    if(${BUILD_TYPE} STREQUAL "FUZZ")
        KCONFIG_GET_PARAMS("${ROOT_DIR}/build/menuconfig/test/platform/ut.config")
        include("${CMAKE_DIR}/build_fuzz.cmake")
        return()
    endif()
endmacro()

# --------------------------------------------------------------------
# Epilogue - runs after project(); brings in all components, the linker,
# bin/elf post-processing, signing, fwpkg packaging.
# --------------------------------------------------------------------
macro(cfbb_build_epilogue)
    set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")

    if(EXISTS ${CMAKE_BINARY_DIR}/auto_gen_libfunc.lds)
        file(REMOVE ${CMAKE_BINARY_DIR}/auto_gen_libfunc.lds)
    endif()

    set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
    set(TARGET_NAME ${BIN_NAME})
    file(WRITE ${PROJECT_BINARY_DIR}/temp/__null___.c "int __null___(void) {return 0;}")
    add_executable(${BIN_NAME} ${PROJECT_BINARY_DIR}/temp/__null___.c)
    set_target_properties(${BIN_NAME} PROPERTIES RUNTIME_OUTPUT_NAME ${BIN_NAME}.elf)
    target_compile_options(${BIN_NAME} PRIVATE "${CCFLAGS}")

    include("${CMAKE_DIR}/build_rom_callback.cmake")
    if(${BUILD_ROM_CALLBACK})
        build_rom_callback()
    endif()

    set(KCONFIG_PATH "${ROOT_DIR}/build/config/target_config/${CHIP}/menuconfig/${CORE}/${BUILD_TARGET_NAME}.config")
    if(EXISTS ${KCONFIG_PATH})
        KCONFIG_GET_PARAMS(${KCONFIG_PATH})
        set(USE_KCONFIG True)
    endif()

    # SDK component tree: in-tree these are relative add_subdirectory() and
    # work because CMAKE_CURRENT_SOURCE_DIR == ROOT_DIR. Out-of-tree they
    # would silently no-op, so use the ROOT_DIR-anchored form unconditionally.
    foreach(_comp application bt bootloader kernel drivers middleware
                  open_source protocol test include vendor)
        if(EXISTS "${ROOT_DIR}/${_comp}/CMakeLists.txt")
            add_subdirectory("${ROOT_DIR}/${_comp}" "${CMAKE_BINARY_DIR}/${_comp}")
        endif()
    endforeach()

    # Out-of-tree: the user's project lives outside the SDK tree. Load its
    # main/ component and any components/ subdirs, and register the main
    # component name in RAM_COMPONENT so build_component() actually emits it.
    if(FBB_OUT_OF_TREE)
        if(EXISTS "${CMAKE_SOURCE_DIR}/main/CMakeLists.txt")
            list(APPEND RAM_COMPONENT "${FBB_PROJECT_COMPONENT_NAME}")
            add_subdirectory("${CMAKE_SOURCE_DIR}/main"
                             "${CMAKE_BINARY_DIR}/_fbb_project_main")
        endif()
        if(EXISTS "${CMAKE_SOURCE_DIR}/components")
            file(GLOB _proj_comps RELATIVE "${CMAKE_SOURCE_DIR}/components"
                 "${CMAKE_SOURCE_DIR}/components/*")
            foreach(_pc ${_proj_comps})
                if(EXISTS "${CMAKE_SOURCE_DIR}/components/${_pc}/CMakeLists.txt")
                    list(APPEND RAM_COMPONENT "${_pc}")
                    add_subdirectory("${CMAKE_SOURCE_DIR}/components/${_pc}"
                                     "${CMAKE_BINARY_DIR}/_fbb_project_${_pc}")
                endif()
            endforeach()
        endif()
        # Refresh the linker's notion of all components after appending project's.
        set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
    endif()

    include("${CMAKE_DIR}/open_source.cmake")
    include("${CMAKE_DIR}/middleware/hwsec_c.cmake")
    include("${CMAKE_DIR}/build_linker.cmake")

    if (NOT DEFINED ROM_COMPONENT)
    add_custom_target(GENERAT_BIN ALL
        COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .fls_loader_ram -R .logstr -R .ARM -R .ARM ${BIN_NAME}.elf ${BIN_NAME}.bin
        COMMENT "post_build:gen bin file"
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS ${TARGET_NAME}
    )
    else()
    add_custom_target(GENERAT_BIN ALL
        COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .logstr -R .ARM.exidx -R .ARM.extab -R .*_romtext ${TARGET_NAME}.elf ${TARGET_NAME}.bin
        COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -j .*_romtext ${TARGET_NAME}.elf ${TARGET_NAME}_rom.bin
        COMMENT "post_build:gen rom and ram bin file"
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS ${TARGET_NAME}
    )
    endif()

    if(DEFINED CONFIG_SFC_SUPPORT_RWE_INDEPENDENT)
    add_custom_target(GENERAT_FLASH_DRIVER_BIN ALL
        COMMAND ${CMAKE_OBJCOPY} -O srec --srec-len=0x20 --srec-forceS3 -S -j .fls_loader_ram ${BIN_NAME}.elf BOOTLOADERFlsDrv.signed.s19
        COMMENT "post_build:gen flash driver bin file"
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS ${TARGET_NAME}
    )
    endif()

    include("${CMAKE_DIR}/build_ssb.cmake")
    include("${CMAKE_DIR}/build_elf_info.cmake")
    include("${CMAKE_DIR}/build_sign.cmake")
    include("${CMAKE_DIR}/build_nv_bin.cmake")
    include("${CMAKE_DIR}/build_partition_bin.cmake")
    include("${CMAKE_DIR}/build_boot_bin_cp.cmake")

    create_hso_db()
    generate_project_file()
endmacro()
