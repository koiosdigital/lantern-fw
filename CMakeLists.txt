# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

IF(DEFINED ENV{BUILD_VERSION})
set(PROJECT_VER $ENV{BUILD_VERSION})
set(BUILD_VARIANT $ENV{BUILD_VARIANT})
message("Building production project with version $ENV{BUILD_VERSION}")
add_compile_definitions(ENABLE_OTA)
add_compile_definitions(FIRMWARE_VARIANT="$ENV{BUILD_VARIANT}")
ELSE()
message("Building development project")
set(PROJECT_VER "0.0.1")
    IF(DEFINED ENV{BUILD_VARIANT})
        set(BUILD_VARIANT $ENV{BUILD_VARIANT})
        message("Using build variant $ENV{BUILD_VARIANT}")
        add_compile_definitions(FIRMWARE_VARIANT="$ENV{BUILD_VARIANT}")
    ELSE()
        set(BUILD_VARIANT "devel")
        add_compile_definitions(FIRMWARE_VARIANT="devel")
    ENDIF()
ENDIF()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(LANTERN-fw)