# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(bmmsc4kg2_control C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(bmmsc4kg2_control
    main.cc
    usb_network.c
    usb_descriptors.c
    dhcpserver/dhcpserver.c
    ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
    ../Pico-LCD-1.14/c/lib/LCD/LCD_1in14.c
    ../Pico-LCD-1.14/c/lib/GUI/GUI_Paint.c
    ../Pico-LCD-1.14/c/lib/Config/DEV_Config.c
    ../Pico-LCD-1.14/c/lib/Fonts/font24.c
    ../Pico-LCD-1.14/c/lib/Fonts/font16.c
    ../Pico-LCD-1.14/c/lib/Fonts/font8.c
)


add_executable(bmmsc4kg2_twobutton
    two_button.cc
    usb_network.c
    usb_descriptors.c
    dhcpserver/dhcpserver.c
    ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
)


pico_set_program_name(bmmsc4kg2_control "bmmsc4kg2_control")
pico_set_program_version(bmmsc4kg2_control "0.1")

pico_set_program_name(bmmsc4kg2_twobutton "bmmsc4kg2_twobutton")
pico_set_program_version(bmmsc4kg2_twobutton "0.1")


# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(bmmsc4kg2_control 1)
pico_enable_stdio_usb(bmmsc4kg2_control 0)

pico_enable_stdio_uart(bmmsc4kg2_twobutton 1)
pico_enable_stdio_usb(bmmsc4kg2_twobutton 0)

target_include_directories(bmmsc4kg2_control PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/hardware_i2c/include/
    ${PICO_SDK_PATH}/src/rp2_common/hardware_spi/include/
    ${PICO_SDK_PATH}/src/rp2_common/hardware_pwm/include/
    ../Pico-LCD-1.14/c/lib/Config
    ../Pico-LCD-1.14/c/lib/GUI
    ../Pico-LCD-1.14/c/lib/LCD
    ../Pico-LCD-1.14/c/lib/Fonts/
)

# Add libraries to the build
target_link_libraries(bmmsc4kg2_control
    pico_stdlib
    tinyusb_device
    pico_lwip
    pico_lwip_nosys
    pico_lwip_mdns
    pico_unique_id
    pico_lwip_netif

    hardware_spi
    hardware_gpio
    hardware_i2c
)

target_link_libraries(bmmsc4kg2_twobutton
    pico_stdlib
    tinyusb_device
    pico_lwip
    pico_lwip_nosys
    pico_lwip_mdns
    pico_unique_id
    pico_lwip_netif
)


# Add include directories to the build
target_include_directories(bmmsc4kg2_control PRIVATE
    ${PICO_TINYUSB_PATH}/lib/networking # for rndis_protocol.h
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

target_include_directories(bmmsc4kg2_twobutton PRIVATE
    ${PICO_TINYUSB_PATH}/lib/networking # for rndis_protocol.h
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)


pico_add_extra_outputs(bmmsc4kg2_control)
pico_add_extra_outputs(bmmsc4kg2_twobutton)

