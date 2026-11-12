# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 关闭 IDF 组件管理器：所有第三方组件已本地化到 components/ 下，
# 无需在 configure 阶段访问 esp-registry（避免网络抖动导致构建卡住/失败）。
# 必须在 include(project.cmake) 之前设置，project.cmake 会在此时读取该环境变量。
set(ENV{IDF_COMPONENT_MANAGER} "0")
# ESP-IDF 5.3 的 parttool.py 按系统默认编码读取分区表；Windows 上统一使用
# UTF-8，避免中文注释导致解码失败并生成空的应用烧录地址。
if(CMAKE_HOST_WIN32)
    set(ENV{PYTHONUTF8} "1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/hardware
    ${CMAKE_CURRENT_SOURCE_DIR}/function
    # ${CMAKE_CURRENT_SOURCE_DIR}/function/oldTemp
    ${CMAKE_CURRENT_SOURCE_DIR}/task
)
project(EVT_ESP32-S3)

# Keep compiler cache files inside the project build directory.
if(CCACHE_ENABLE AND CCACHE_FOUND)
    set(PROJECT_CCACHE_DIR "${CMAKE_BINARY_DIR}/ccache")
    file(MAKE_DIRECTORY "${PROJECT_CCACHE_DIR}/tmp")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
        "\"${CMAKE_COMMAND}\" -E env \"CCACHE_DIR=${PROJECT_CCACHE_DIR}\" \"CCACHE_TEMPDIR=${PROJECT_CCACHE_DIR}/tmp\" \"${CCACHE_FOUND}\"")
endif()
