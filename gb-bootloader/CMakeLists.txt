cmake_minimum_required(VERSION 3.13)

set(GBDK_LCC $ENV{GBDK_PATH}/bin/lcc)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bootloader.gb
    DEPENDS bootloader.c
    COMMAND ${GBDK_LCC} -Wa-l -Wl-m -Wl-j -Wm-p -Wm-yc -Wm-yt2 -Wm-ya1
        -o ${CMAKE_CURRENT_BINARY_DIR}/bootloader.gb
        ${CMAKE_CURRENT_LIST_DIR}/bootloader.c
        ${CMAKE_CURRENT_LIST_DIR}/giraffe_4color_data.c
        ${CMAKE_CURRENT_LIST_DIR}/giraffe_4color_map.c
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gbbootloader.h
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bootloader.gb
    COMMAND ${CMAKE_COMMAND} 
        -DSOURCE_FILE="${CMAKE_CURRENT_BINARY_DIR}/bootloader.gb"
        -DHEADER_FILE="${CMAKE_CURRENT_BINARY_DIR}/gbbootloader.h"
        -DVARIABLE_NAME="GB_BOOTLOADER"
        -P ${CMAKE_HELPERS_DIR}/executebin2h.cmake
)

add_custom_target(BootloaderGb ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gbbootloader.h
)
