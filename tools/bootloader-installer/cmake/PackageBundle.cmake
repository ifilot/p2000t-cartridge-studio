cmake_minimum_required(VERSION 3.20)

foreach(required INSTALLER FIRMWARE_IMAGE SOURCE_DIR)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Missing ${required}: ${${required}}")
    endif()
endforeach()
if(NOT DEFINED PRODUCT_VERSION OR
   NOT PRODUCT_VERSION MATCHES "^[0-9]\\.[0-9]\\.[0-9]$")
    message(FATAL_ERROR "Missing or invalid PRODUCT_VERSION: ${PRODUCT_VERSION}")
endif()

set(AVRDUDE_VERSION 8.2)
set(AVRDUDE_BINARY_URL
    "https://github.com/avrdudes/avrdude/releases/download/v8.2/avrdude-v8.2-windows-x64.zip")
set(AVRDUDE_BINARY_SHA256
    e2a89a921e1b2fe675a319248ea968c7e28c7edc44c5fd836e2879a3e094c9cf)
set(AVRDUDE_SOURCE_URL
    "https://github.com/avrdudes/avrdude/archive/refs/tags/v8.2.tar.gz")
set(AVRDUDE_SOURCE_SHA256
    72fbe49d3e3ea2f48a750e7f2c16287b163a580e020f745af39d45ba68d9d6ae)
set(AVRDUDE_LICENSE_URL
    "https://raw.githubusercontent.com/avrdudes/avrdude/v8.2/COPYING")
set(AVRDUDE_LICENSE_SHA256
    8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643)

set(DIST_DIR "${SOURCE_DIR}/dist")
set(VERSION_FILE "${SOURCE_DIR}/../../VERSION")
if(NOT EXISTS "${VERSION_FILE}")
    message(FATAL_ERROR "Missing repository VERSION: ${VERSION_FILE}")
endif()
set(CACHE_DIR "${DIST_DIR}/download-cache")
set(PACKAGE_NAME
    "p2000t-programmable-cartridge-preparation-tool-windows-x64")
set(PACKAGE_DIR "${DIST_DIR}/${PACKAGE_NAME}")
set(FACTORY_IMAGE_FILENAME
    "p2000t-programmable-cartridge-factory-image.hex")
set(PROJECT_LICENSE "${SOURCE_DIR}/../../LICENSE")
if(NOT EXISTS "${PROJECT_LICENSE}")
    message(FATAL_ERROR "Missing project licence: ${PROJECT_LICENSE}")
endif()
file(MAKE_DIRECTORY "${CACHE_DIR}")

function(download_checked url destination digest)
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" actual_digest)
    endif()
    if(NOT actual_digest STREQUAL digest)
        file(DOWNLOAD "${url}" "${destination}"
            EXPECTED_HASH "SHA256=${digest}"
            TLS_VERIFY ON
            SHOW_PROGRESS)
    endif()
endfunction()

set(BINARY_ZIP "${CACHE_DIR}/avrdude-v${AVRDUDE_VERSION}-windows-x64.zip")
set(SOURCE_ARCHIVE "${CACHE_DIR}/avrdude-v${AVRDUDE_VERSION}-source.tar.gz")
set(LICENSE_FILE "${CACHE_DIR}/avrdude-copying.txt")
download_checked("${AVRDUDE_BINARY_URL}" "${BINARY_ZIP}" "${AVRDUDE_BINARY_SHA256}")
download_checked("${AVRDUDE_SOURCE_URL}" "${SOURCE_ARCHIVE}" "${AVRDUDE_SOURCE_SHA256}")
download_checked("${AVRDUDE_LICENSE_URL}" "${LICENSE_FILE}" "${AVRDUDE_LICENSE_SHA256}")

set(UNPACK_DIR "${CACHE_DIR}/avrdude-windows")
file(REMOVE_RECURSE "${UNPACK_DIR}" "${PACKAGE_DIR}")
file(MAKE_DIRECTORY "${UNPACK_DIR}" "${PACKAGE_DIR}")
file(ARCHIVE_EXTRACT INPUT "${BINARY_ZIP}" DESTINATION "${UNPACK_DIR}")

file(COPY "${INSTALLER}" DESTINATION "${PACKAGE_DIR}")
configure_file("${FIRMWARE_IMAGE}"
    "${PACKAGE_DIR}/${FACTORY_IMAGE_FILENAME}" COPYONLY)
configure_file("${VERSION_FILE}" "${PACKAGE_DIR}/version.txt" COPYONLY)
configure_file("${PROJECT_LICENSE}" "${PACKAGE_DIR}/license.txt" COPYONLY)
file(COPY "${UNPACK_DIR}/avrdude.exe" "${UNPACK_DIR}/avrdude.conf"
     DESTINATION "${PACKAGE_DIR}")
configure_file("${LICENSE_FILE}" "${PACKAGE_DIR}/avrdude-copying.txt" COPYONLY)
file(COPY "${SOURCE_ARCHIVE}" DESTINATION "${PACKAGE_DIR}")
configure_file("${SOURCE_DIR}/PACKAGE-README.txt"
    "${PACKAGE_DIR}/readme.txt" COPYONLY)

set(ARCHIVE "${DIST_DIR}/${PACKAGE_NAME}.zip")
file(REMOVE "${ARCHIVE}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ARCHIVE}" --format=zip "${PACKAGE_NAME}"
    WORKING_DIRECTORY "${DIST_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Created ${ARCHIVE}")
