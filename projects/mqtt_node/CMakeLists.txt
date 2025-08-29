cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Selección robusta del directorio de componentes "common"
set(_COMMON_DIR_UP "${CMAKE_CURRENT_LIST_DIR}/../common/components")
set(_COMMON_DIR_LOCAL "${CMAKE_CURRENT_LIST_DIR}/components")
if(EXISTS "${_COMMON_DIR_UP}")
    set(EXTRA_COMPONENT_DIRS "${_COMMON_DIR_UP}")
elseif(EXISTS "${_COMMON_DIR_LOCAL}")
    set(EXTRA_COMPONENT_DIRS "${_COMMON_DIR_LOCAL}")
else()
    message(FATAL_ERROR "No se encontró el componente 'common'. Monta projects/common/components en ./components o compila fuera de Docker.")
endif()

project(mqtt_node)
