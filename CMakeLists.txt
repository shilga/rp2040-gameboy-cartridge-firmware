cmake_minimum_required(VERSION 3.13)

include(FetchContent)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

FetchContent_Declare(printf_library
    GIT_REPOSITORY https://github.com/eyalroz/printf.git
    GIT_TAG v6.1.0
)
FetchContent_MakeAvailable(printf_library)



project(rp2040-gb-cartridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${PROJECT_NAME} main.c dma_uart.c putchar.c)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/gameboy_bus.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_set_printf_implementation(${PROJECT_NAME} none)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/linkerscript.ld)

target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_dma hardware_uart hardware_gpio hardware_pio hardware_clocks printf::printf)

pico_add_extra_outputs(${PROJECT_NAME})
