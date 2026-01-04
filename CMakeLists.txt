cmake_minimum_required(VERSION 3.15)
project(
    MMS
    VERSION 1.0.0
)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    set(CMAKE_CXX_FLAGS "-g -O0 -Werror -Wall -pipe")
    add_compile_definitions(DEBUG_BUILD)
else()
    set(CMAKE_CXX_FLAGS "-g -O2 -minline-all-stringops -pipe -Wall -Wextra -Werror -fno-strict-aliasing -fno-lto -Wno-restrict")
    add_compile_definitions(RELEASE_BUILD)
endif()

# rpath
set(CMAKE_SKIP_BUILD_RPATH FALSE)                 # 编译时加上RPATH  
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)          # 编译时RPATH不使用安装的RPATH  
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib")# 安装RPATH为空  
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)       # 安装的执行文件加上RPATH

# set compiler standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# include 3rd dependency lib
include(3rd_lib.cmake)
# install
set(CMAKE_INSTALL_PREFIX ${MMS_SOURCE_DIR})
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/live-server")