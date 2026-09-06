cmake_minimum_required(VERSION 3.16)

set(P2000T_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
file(STRINGS "${P2000T_VERSION_FILE}" P2000T_VERSION LIMIT_COUNT 1)
if(NOT P2000T_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain three numeric components such as 0.12.0")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${P2000T_VERSION_FILE}")

# Set the project name
project(p2000t-cartridge-studio VERSION ${P2000T_VERSION} LANGUAGES CXX)
if(PROJECT_VERSION_MAJOR GREATER 255 OR PROJECT_VERSION_MINOR GREATER 255 OR
   PROJECT_VERSION_PATCH GREATER 255)
    message(FATAL_ERROR "VERSION components must fit in one byte (0-255)")
endif()

configure_file(src/version.h.in generated/version.h @ONLY)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# get Git HASH
execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()
add_compile_definitions(GIT_HASH="${GIT_HASH}")

# Find Qt 6 packages
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets SerialPort Network Test)

add_library(cartridge_studio_core STATIC
    src/firmwarecompatibility.cpp
    src/firmwareupdater.cpp
    src/flashthread.cpp
    src/ioworker.cpp
    src/readthread.cpp
    src/serial_interface.cpp
    src/serial_transport_qt.cpp
)

target_include_directories(cartridge_studio_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(cartridge_studio_core PUBLIC Qt6::Core Qt6::SerialPort)

add_library(cartridge_studio_ui STATIC
    src/aboutdialog.cpp
    src/bankselector.cpp
    src/filedownloader.cpp
    src/hexviewwidget.cpp
    src/logbuffer.cpp
    src/logwindow.cpp
    src/mainwindow.cpp
    src/picoflasherapplication.cpp
    src/settingswidget.cpp
    resources.qrc
)

target_include_directories(cartridge_studio_ui PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(cartridge_studio_ui PUBLIC cartridge_studio_core Qt6::Widgets Qt6::Network)

add_executable(p2000t-cartridge-studio
    src/main.cpp
)
set_target_properties(p2000t-cartridge-studio PROPERTIES
    OUTPUT_NAME "p2000t-cartridge-studio")

if(WIN32)
    target_sources(p2000t-cartridge-studio PRIVATE
        appicon.rc
    )
    set_property(SOURCE appicon.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/assets/icon/p2000t-cartridge-studio.ico"
    )
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
    set_target_properties(p2000t-cartridge-studio PROPERTIES
        MACOSX_BUNDLE TRUE
    )
endif()

target_link_libraries(p2000t-cartridge-studio PRIVATE cartridge_studio_ui)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/tools/avrdude/avrdude.exe")
    add_custom_command(TARGET p2000t-cartridge-studio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:p2000t-cartridge-studio>/tools/avrdude"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/tools/avrdude/avrdude.exe"
                "$<TARGET_FILE_DIR:p2000t-cartridge-studio>/tools/avrdude/avrdude.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/tools/avrdude/avrdude.conf"
                "$<TARGET_FILE_DIR:p2000t-cartridge-studio>/tools/avrdude/avrdude.conf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/tools/avrdude/COPYING.txt"
                "$<TARGET_FILE_DIR:p2000t-cartridge-studio>/tools/avrdude/copying.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/tools/avrdude/THIRD_PARTY_NOTICES.md"
                "$<TARGET_FILE_DIR:p2000t-cartridge-studio>/tools/avrdude/third-party-notices.md"
    )
endif()

set(P2000T_FIRMWARE_IMAGE "${CMAKE_CURRENT_LIST_DIR}/../firmware/build/cartridge.hex")
if(WIN32 AND EXISTS "${P2000T_FIRMWARE_IMAGE}")
    set(P2000T_PACKAGED_FIRMWARE
        "${CMAKE_BINARY_DIR}/firmware/p2000t-programmable-cartridge-firmware.hex")
    add_custom_target(p2000t_firmware_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/firmware"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${P2000T_FIRMWARE_IMAGE}"
                "${P2000T_PACKAGED_FIRMWARE}"
        BYPRODUCTS "${P2000T_PACKAGED_FIRMWARE}"
        DEPENDS "${P2000T_FIRMWARE_IMAGE}"
        VERBATIM
    )
    add_dependencies(p2000t-cartridge-studio p2000t_firmware_bundle)
endif()

set_target_properties(p2000t-cartridge-studio PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(BUILD_TESTING)
    add_executable(tst_serial_interface
        tests/tst_serial_interface.cpp
        tests/support/emulated_serial_transport.cpp
        tests/support/fault_injecting_transport.cpp
    )
    target_include_directories(tst_serial_interface PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/tests
    )
    target_link_libraries(tst_serial_interface PRIVATE cartridge_studio_core Qt6::Test)
    if(EXISTS "${P2000T_FIRMWARE_IMAGE}")
        target_compile_definitions(tst_serial_interface PRIVATE
            P2000T_TEST_FIRMWARE_IMAGE="${P2000T_FIRMWARE_IMAGE}")
    endif()
    add_test(NAME tst_serial_interface COMMAND tst_serial_interface)

    add_executable(tst_worker_threads
        tests/tst_worker_threads.cpp
        tests/support/emulated_serial_transport.cpp
    )
    target_include_directories(tst_worker_threads PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/tests
    )
    target_link_libraries(tst_worker_threads PRIVATE cartridge_studio_core Qt6::Test)
    add_test(NAME tst_worker_threads COMMAND tst_worker_threads)

    add_executable(tst_mainwindow
        tests/tst_mainwindow.cpp
        tests/support/emulated_serial_transport.cpp
        tests/support/fault_injecting_transport.cpp
    )
    target_include_directories(tst_mainwindow PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/tests
    )
    target_link_libraries(tst_mainwindow PRIVATE cartridge_studio_ui Qt6::Test)
    add_test(NAME tst_mainwindow COMMAND tst_mainwindow)

    if(UNIX AND NOT APPLE)
        set_tests_properties(tst_mainwindow PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()
