#===============================================================================
# @brief    cmake entry
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026. All rights reserved.
#===============================================================================

cmake_minimum_required(VERSION 3.14.1)

# 项目名 -- `fbb create-project` 会重写此行。
# 必须在 include(build_project.cmake) 之前设置才生效。
set(PROJECT_NAME "sample_project")

# 包含封装的 build_project.cmake
include("${ROOT_DIR}/build/cmake/build_project.cmake")

# 项目构建
cfbb_build_project()
