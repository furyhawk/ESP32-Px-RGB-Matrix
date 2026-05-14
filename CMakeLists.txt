cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET AND DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET $ENV{IDF_TARGET})
endif()

if(DEFINED IDF_TARGET)
    set(_target_defaults "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.${IDF_TARGET}")
    if(EXISTS "${_target_defaults}")
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults.${IDF_TARGET}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32_Px_RGB_Matrix)
