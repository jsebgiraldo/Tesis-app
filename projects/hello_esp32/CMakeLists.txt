cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Optional: try to mount shared components from ../common/components if present
set(_COMMON_DIR_UP "${CMAKE_CURRENT_LIST_DIR}/../common/components")
if(EXISTS "${_COMMON_DIR_UP}")
    set(EXTRA_COMPONENT_DIRS "${_COMMON_DIR_UP}")
endif()

project(hello_esp32)
