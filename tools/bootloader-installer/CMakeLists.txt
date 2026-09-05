cmake_minimum_required(VERSION 3.20)

set(P2000T_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
file(STRINGS "${P2000T_VERSION_FILE}" P2000T_VERSION LIMIT_COUNT 1)
if(NOT P2000T_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain three numeric components such as 0.12.0")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P2000T_VERSION_FILE}")

project(p2000t_cartridge_preparation_tool VERSION ${P2000T_VERSION} LANGUAGES CXX)

set(PREPARATION_TOOL_TARGET p2000t-cartridge-preparation-tool)
set(FACTORY_IMAGE_FILENAME
    "p2000t-programmable-cartridge-factory-image.hex")

add_executable(${PREPARATION_TOOL_TARGET} src/main.cpp)
set_target_properties(${PREPARATION_TOOL_TARGET} PROPERTIES
    OUTPUT_NAME "p2000t-programmable-cartridge-preparation-tool")
target_compile_features(${PREPARATION_TOOL_TARGET} PRIVATE cxx_std_17)
target_compile_options(${PREPARATION_TOOL_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(${PREPARATION_TOOL_TARGET} PRIVATE
    P2000T_VERSION="${P2000T_VERSION}"
    P2000T_FACTORY_IMAGE_FILENAME="${FACTORY_IMAGE_FILENAME}")

if(MINGW)
    target_link_options(${PREPARATION_TOOL_TARGET} PRIVATE
        -static -static-libgcc -static-libstdc++)
endif()

set(FIRMWARE_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/build/combined.hex")
add_custom_target(package_bundle
    COMMAND "${CMAKE_COMMAND}"
        -DINSTALLER=$<TARGET_FILE:${PREPARATION_TOOL_TARGET}>
        -DFIRMWARE_IMAGE=${FIRMWARE_IMAGE}
        -DPRODUCT_VERSION=${P2000T_VERSION}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageBundle.cmake"
    DEPENDS ${PREPARATION_TOOL_TARGET}
    VERBATIM)
