cmake_minimum_required(VERSION 3.30)
if(NOT DEFINED SdkRootDirPath)
    message(FATAL_ERROR "Use scripts/build.ps1, or supply SdkRootDirPath.")
endif()
include(${SdkRootDirPath}/cmake/extension/mcux.cmake)
project(rt1170_companion LANGUAGES C CXX ASM
    PROJECT_BOARD_PORT_PATH ${board_root}/${board}/wifi_examples/wifi_setup)
include(${SdkRootDirPath}/CMakeLists.txt)

set(wifi_example ${SdkRootDirPath}/examples/wifi_examples/wifi_setup)
configure_file(${wifi_example}/FreeRTOSConfig.h ${CMAKE_CURRENT_BINARY_DIR}/sdk_freertos_base.h COPYONLY)
configure_file(${wifi_example}/lwipopts/lwipopts.h ${CMAKE_CURRENT_BINARY_DIR}/sdk_lwipopts_base.h COPYONLY)
set(portal_output ${CMAKE_CURRENT_BINARY_DIR}/../portal)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html ${CMAKE_CURRENT_SOURCE_DIR}/web/jsQR.js
    ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/embed_portal.py)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/embed_portal.py
    ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html ${portal_output}
    ${SdkRootDirPath}/examples/lwip_examples/httpsrv_common/fs_data/webpage/NXP_logo.png
    ${CMAKE_CURRENT_SOURCE_DIR}/web/jsQR.js
    COMMAND_ERROR_IS_FATAL ANY)
mcux_add_source(SOURCES main.c companion_ui.c companion_net.c wifi_portal.c
    ../src/wifi_form.c)
mcux_add_source(BASE_PATH ${SdkRootDirPath} SOURCES
    ${board_root}/${board}/wifi_examples/common/pin_mux.c
    ${board_root}/${board}/wifi_examples/common/pin_mux.h)
mcux_add_include(INCLUDES . ../src)
mcux_add_include(BASE_PATH ${SdkRootDirPath} INCLUDES
    examples/wifi_examples/wifi_setup/wifi_config
    examples/wifi_examples/wifi_setup
    ${board_root}/${board}/wifi_examples/common)
target_include_directories(${MCUX_SDK_PROJECT_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/config ${CMAKE_CURRENT_BINARY_DIR} ${portal_output})
target_compile_definitions(${MCUX_SDK_PROJECT_NAME} PRIVATE
    WPL_WIFI_AP_IP_ADDR="192.168.4.1" WPL_DEBUG CACHE_MODE_WRITE_THROUGH=1)
mcux_add_macro(CC "-DUSE_RTOS=1 -DPRINTF_ADVANCED_ENABLE=1")
mcux_add_armgcc_configuration(LD "-Xlinker --defsym=__stack_size__=0x1000 -Xlinker --defsym=__heap_size__=0x400")
set(companion_linker_copy "C:/tmp/rt1170_companion.ld")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/linker/rt1170_companion.ld
    ${companion_linker_copy} COPYONLY)
mcux_add_armgcc_linker_script(BASE_PATH C:/tmp
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER rt1170_companion.ld)
include(${SdkRootDirPath}/${board_root}/${board}/wifi_examples/wifi_setup/reconfig.cmake)
mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/rt1170_companion.bin)

