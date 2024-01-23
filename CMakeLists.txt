cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2040-gb-cartridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR})
set(PICO_BOARD MyBoard)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

pico_sdk_init()

add_subdirectory(gb-bootloader)

add_subdirectory(libs/pico-littlefs)
target_compile_definitions(littlefs-lib PUBLIC LFS_NO_MALLOC LFS_YES_TRACE)

add_subdirectory(libs/git-commit-tracking)

add_executable(${PROJECT_NAME} 
    main.c
    GbDma.c
    mbc.c
    webusb.c
    usb_descriptors.c
    RomStorage.c
    GameBoyHeader.c
    ws2812b_spi.c
    )

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/gameboy_bus.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
# pico_set_printf_implementation(${PROJECT_NAME} none)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/linkerscript.ld)

add_dependencies(${PROJECT_NAME} BootloaderGb_hdr)

target_link_libraries(${PROJECT_NAME} 
    pico_stdlib
    pico_bootsel_via_double_reset
    hardware_dma
    hardware_uart
    hardware_i2c
    hardware_spi
    hardware_gpio
    hardware_pio
    hardware_clocks
    littlefs-lib
    tinyusb_device
    tinyusb_board
    cmake_git_commit_tracking
    )

pico_add_extra_outputs(${PROJECT_NAME})
