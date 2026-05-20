#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026. All rights reserved.
#===============================================================================
if(NOT DEFINED ROOT_DIR)
    get_filename_component(ROOT_DIR "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
    message(STATUS "ROOT_DIR is not defined, using default: ${ROOT_DIR}")
else()
    message(STATUS "ROOT_DIR is defined as: ${ROOT_DIR}")
endif()

set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")

set(Python3_EXECUTABLE ${PY_PATH})

# 调试信息
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# 路径计算
set(CMAKE_DIR "${ROOT_DIR}/build/cmake")
set(BIN_DIR   "${ROOT_DIR}/interim_binary")
set(OHOS_DIR  "${ROOT_DIR}/ohos")

# 包含必要的依赖项
include("${CMAKE_DIR}/build_function.cmake")
include("${CMAKE_DIR}/global_variable.cmake")
include("${CMAKE_DIR}/build_script.cmake")
include("${CMAKE_DIR}/build_command.cmake")
include("${CMAKE_DIR}/build_hso_database.cmake")
include("${CMAKE_DIR}/build_component.cmake")
include("${CMAKE_DIR}/build_sdk.cmake")

# 设置项目名称并调用 project() 以触发工具链包含
if(NOT DEFINED PROJECT_NAME)
    set(PROJECT_NAME "${CHIP}_CFBB")
endif()

# 调用 project() 以触发工具链包含
project(${PROJECT_NAME} C ASM CXX)
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")

# 项目构建主函数
function(__cfbb_build_project)
    # 关键设置：与根目录保持一致
    set(CMAKE_SYSTEM_NAME "Generic")
    
    # 设置默认模块名称和自动定义标志，参考根目录 CMakeLists.txt
    set(MODULE_NAME "pf")
    set(AUTO_DEF_FILE_ID TRUE)
    set(AUTO_DEF_FILE_NAME TRUE)
    set(AUTO_DEF_MODULE_ID TRUE)
    set(USE_LLD 0)
    
    # 初始化.rsp文件相关变量
    if(NOT DEFINED ALL_INCLUDES_STRING)
        set(ALL_INCLUDES_STRING "")
    endif()
    if(NOT DEFINED ALL_DEFINES_STRING)
        set(ALL_DEFINES_STRING "")
    endif()
    # 设置默认项目名称（如果未指定）
    if(NOT DEFINED PROJECT_NAME)
        set(PROJECT_NAME "${CHIP}_CFBB" PARENT_SCOPE)
    endif()
    
    # 项目声明已移至全局范围
    
    message(STATUS "cfbb Project Building: ${PROJECT_NAME}")
    
    # 清理旧的链接脚本
    if(EXISTS ${CMAKE_BINARY_DIR}/auto_gen_libfunc.lds)
        file(REMOVE ${CMAKE_BINARY_DIR}/auto_gen_libfunc.lds)
    endif()
    
    # 设置目标组件和名称
    set(TARGET_COMPONENT "${RAM_COMPONENT}" "${ROM_COMPONENT}")
    set(TARGET_NAME ${BIN_NAME})
    
    # ROM 检查处理
    if(${ROM_CHECK})
        set(BIN_NAME "${BIN_NAME}_romcheck")
    endif()
    
    # 创建虚拟源文件（参考根目录的实现）
    file(WRITE ${PROJECT_BINARY_DIR}/temp/__null___.c "int __null___(void) {return 0;}")
    
    # 创建可执行目标（核心逻辑）
    add_executable(${TARGET_NAME} ${PROJECT_BINARY_DIR}/temp/__null___.c)
    set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_NAME ${BIN_NAME}.elf)
    
    # 设置编译选项
    if(DEFINED CCFLAGS)
        target_compile_options(${TARGET_NAME} PRIVATE "${CCFLAGS}")
    endif()
    
    # 创建初始.rsp文件（与根目录CMakeLists.txt保持一致）
    file(WRITE ${PROJECT_BINARY_DIR}/include_all_public.rsp "${ALL_INCLUDES_STRING}")
    file(WRITE ${PROJECT_BINARY_DIR}/define_all_public.rsp "${ALL_DEFINES_STRING}")
    # ROM 回调构建
    include("${CMAKE_DIR}/build_rom_callback.cmake")
    if(${BUILD_ROM_CALLBACK})
        build_rom_callback()
        # 第一遍编译不带-g, 提高编译速度
        list(REMOVE_ITEM CCFLAGS "-g")
        list(REMOVE_ITEM ROM_CCFLAGS "-g")
    endif()
    
    # KConfig 配置
    set(KCONFIG_PATH "${ROOT_DIR}/build/config/target_config/${CHIP}/menuconfig/${CORE}/${BUILD_TARGET_NAME}.config")
    if(EXISTS ${KCONFIG_PATH})
        KCONFIG_GET_PARAMS(${KCONFIG_PATH})
        set(USE_KCONFIG True)
    endif()
    
    # 添加所有组件目录（使用唯一的二进制目录避免冲突）
    __cfbb_add_components()
    
    # 包含额外的构建模块
    include("${CMAKE_DIR}/open_source.cmake")
    include("${CMAKE_DIR}/middleware/hwsec_c.cmake")
    include("${CMAKE_DIR}/build_linker.cmake")
    message(STATUS "ALL_PUBLIC_HEADER:${ALL_PUBLIC_HEADER}")
    # Windows系统特定的.rsp文件处理逻辑
    #if(BUILD_SYSTEM STREQUAL "windows")
        # 追加include路径到.rsp文件
        if(DEFINED ALL_PUBLIC_HEADER)
            foreach(inc ${ALL_PUBLIC_HEADER})
                string(APPEND ALL_INCLUDES_STRING "-I${inc} ")
            endforeach()
            file(APPEND ${PROJECT_BINARY_DIR}/include_all_public.rsp "${ALL_INCLUDES_STRING}")
        endif()
        # 追加define定义到.rsp文件
        if(DEFINED ALL_PUBLIC_DEFINES)
            foreach(def ${ALL_PUBLIC_DEFINES})
                string(REPLACE "-D" "" def "${def}")
                string(REPLACE "\"" "\\\"" def ${def})
                string(APPEND ALL_DEFINES_STRING "-D${def} ")
            endforeach()
            file(APPEND ${PROJECT_BINARY_DIR}/define_all_public.rsp "${ALL_DEFINES_STRING}")
        endif()
    #endif()
    
    # 生成 bin 文件（后处理步骤）
    __cfbb_generate_bin_files()
    
    # 包含其他构建模块
    include("${CMAKE_DIR}/build_ssb.cmake")
    include("${CMAKE_DIR}/build_sdk_lib.cmake")
    include("${CMAKE_DIR}/build_elf_info.cmake")
    
    # 签名处理
    if(${CHIP} STREQUAL "sw21")
        include("${ROOT_DIR}/build/config/target_config/sw21/cmake/build_sign_sw21.cmake")
    else()
        include("${CMAKE_DIR}/build_sign.cmake")
    endif()
    
    include("${CMAKE_DIR}/build_nv_bin.cmake")
    include("${CMAKE_DIR}/build_partition_bin.cmake")
    include("${CMAKE_DIR}/build_boot_bin_cp.cmake")
    
    create_hso_db()
    generate_project_file()
    
    message(STATUS "cfbb Project ${PROJECT_NAME} build configuration completed")
endfunction()

# 搜索路径配置
set(SEARCH_PATHS)
if(EXISTS "${PROJECT_SOURCE_DIR}/components")
    list(APPEND SEARCH_PATHS "${PROJECT_SOURCE_DIR}/components")
endif()
if(DEFINED EXTRA_COMPONENT_DIR AND EXTRA_COMPONENT_DIR AND EXISTS "${EXTRA_COMPONENT_DIR}")
    list(APPEND SEARCH_PATHS "${EXTRA_COMPONENT_DIR}")
elseif(DEFINED EXTRA_COMPONENT_DIR AND EXTRA_COMPONENT_DIR)
    message(WARNING "EXTRA_COMPONENT_DIR 路径不存在: ${EXTRA_COMPONENT_DIR}")
endif()

# 添加组件的辅助函数（解决二进制目录冲突问题）
function(__cfbb_add_components)
    # 组件目录列表
    set(COMPONENT_DIRS
        application bt bootloader kernel drivers
        middleware open_source protocol test include vendor        
    )
    # 为每个组件创建唯一的二进制目录
    foreach(comp_dir IN LISTS COMPONENT_DIRS)
        #message(STATUS "__cfbb_add_components: ${ROOT_DIR}/${comp_dir}/CMakeLists.txt")
        if(EXISTS "${ROOT_DIR}/${comp_dir}/CMakeLists.txt")
            # 创建唯一的二进制目录名
            set(binary_dir "${CMAKE_BINARY_DIR}/${comp_dir}")
            message(STATUS "Adding component: ${comp_dir} -> ${binary_dir}")
            
            # 使用唯一的二进制目录
            add_subdirectory("${ROOT_DIR}/${comp_dir}" "${binary_dir}")
        endif()
    endforeach()

    # 添加当前项目源目录下的组件
    if(EXISTS "${PROJECT_SOURCE_DIR}/source/CMakeLists.txt")
        set(binary_dir "${CMAKE_BINARY_DIR}/source")
        message(STATUS "Adding component: source -> ${binary_dir}")
        message(STATUS "Adding component: source, ccflags: ${CCFLAGS}")
        add_subdirectory("${PROJECT_SOURCE_DIR}/source" "${binary_dir}")
    endif()
    
    # 条件添加 ohos 相关目录
    if(DEFINED CONFIG_SUPPORT_OH)
        if(EXISTS "${ROOT_DIR}/build/cmake/ohos_build/CMakeLists.txt")
            add_subdirectory("${ROOT_DIR}/build/cmake/ohos_build" "${CMAKE_BINARY_DIR}/ohos")
        endif()
    elseif("ENABLE_UIKIT" IN_LIST DEFINES)
        if(EXISTS "${ROOT_DIR}/ohos/CMakeLists.txt")
            add_subdirectory("${ROOT_DIR}/ohos" "${CMAKE_BINARY_DIR}/ohos")
        endif()
    endif()

    # ==============================
    # 4. 递归扫描所有包含 build_component 的组件
    # ==============================
    message(STATUS "开始扫描组件目录...")

    # 检查 RAM_COMPONENT 是否已设置
    if(NOT DEFINED RAM_COMPONENT)
        message(WARNING "RAM_COMPONENT 未定义，将不会加载任何组件")
        set(RAM_COMPONENT "")  # 设置为空列表避免错误
    endif()

    # 如果 SEARCH_PATHS 为空，跳过扫描
    if(NOT SEARCH_PATHS)
        message(STATUS "SEARCH_PATHS 为空，跳过组件扫描")
        set(COMPONENT_DIRS "")
        return()
    endif()

    # 收集所有包含 build_component 的目录
    set(COMPONENT_DIRS "")

    foreach(path ${SEARCH_PATHS})
        if(EXISTS ${path})
            # 限制递归深度，避免性能问题
            message(STATUS "扫描路径: ${path}")
            file(GLOB_RECURSE cmake_files "${path}/CMakeLists.txt")
            message(STATUS "cmake_files: ${cmake_files}")

            # 检查文件数量，避免处理过多文件
            list(LENGTH cmake_files num_files)
            if(num_files GREATER 500)
                message(WARNING "扫描到过多CMakeLists.txt文件 (${num_files})，限制处理数量")
                list(SUBLIST cmake_files 0 500 cmake_files)
            endif()

            foreach(cmake_file ${cmake_files})
                # 使用更高效的方式检查是否包含 build_component()
                file(STRINGS ${cmake_file} lines)
                set(has_build_component FALSE)
                foreach(line IN LISTS lines)
                    if(line MATCHES "build_component\\(\\)")
                        set(has_build_component TRUE)
                        break()
                    endif()
                endforeach()

                if(has_build_component)
                    file(READ ${cmake_file} content)
                    get_filename_component(component_dir ${cmake_file} DIRECTORY)
                    list(APPEND COMPONENT_DIRS ${component_dir})
                endif()
            endforeach()
        else()
            message(STATUS "路径不存在: ${path}")
        endif()
    endforeach()

    # ==============================
    # 5. 为每个组件目录添加子目录
    # ==============================
    message(STATUS "开始添加组件子目录...")

    foreach(component_dir ${COMPONENT_DIRS})
        # 为组件生成安全的二进制目录路径
        file(RELATIVE_PATH rel_path ${ROOT_DIR} ${component_dir})
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" safe_path ${rel_path})
        set(component_binary_dir "${BIN_DIR}/components/${safe_path}")

        # 确保二进制目录存在
        file(MAKE_DIRECTORY ${component_binary_dir})

        message(STATUS "添加组件目录: ${component_dir} -> ${component_binary_dir}")

        # 添加子目录（使用自定义二进制目录）
        add_subdirectory(${component_dir} ${component_binary_dir})
    endforeach()

endfunction()

# 生成 bin文件的辅助函数
function(__cfbb_generate_bin_files)
    if(NOT DEFINED ROM_COMPONENT)
        add_custom_target(GENERAT_BIN ALL
            COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .fls_loader_ram -R .logstr -R .ARM -R .ARM ${BIN_NAME}.elf ${BIN_NAME}.bin
            COMMENT "post_build:gen bin file"
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            DEPENDS ${TARGET_NAME}
        )
        
        if(BIN_WITH_ELFHEADER)
            add_custom_target(GENERAT_ELFHBIN ALL
                COMMAND ${CMAKE_OBJCOPY} -S ${BIN_NAME}.elf ${BIN_NAME}_elfh.bin
                COMMENT "post_build:gen bin file with elf header"
                WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
                DEPENDS ${TARGET_NAME}
            )
        endif()
    else()
        add_custom_target(GENERAT_BIN ALL
            COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -R .logstr -R .ARM.exidx -R .ARM.extab -R .*_romtext ${BIN_NAME}.elf ${BIN_NAME}.bin
            COMMAND ${CMAKE_OBJCOPY} --gap-fill 0xFF -O binary -j .*_romtext ${BIN_NAME}.elf ${BIN_NAME}_rom.bin
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
endfunction()

# 快速构建入口
function(cfbb_build_project)
    # 设置默认值
    if(NOT DEFINED CHIP)
        message(FATAL_ERROR "CHIP must be defined")
    endif()
    
    if(NOT DEFINED PROJECT_NAME)
        set(PROJECT_NAME "${CHIP}_CFBB")
    endif()
    
    # 设置默认BUILD_SYSTEM值
    if(NOT DEFINED BUILD_SYSTEM)
        set(BUILD_SYSTEM "generic")
    endif()
    message(STATUS "Build system: ${BUILD_SYSTEM}")
    # 直接调用构建
    __cfbb_build_project()
endfunction()
