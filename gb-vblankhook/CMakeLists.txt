cmake_minimum_required(VERSION 3.13)

set(RGBASM rgbasm)
set(RGBLINK rgblink)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.o
    DEPENDS gbSaveGameVBlankHook.asm
    COMMAND ${RGBASM} -L -o ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.o gbSaveGameVBlankHook.asm
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.gb
    DEPENDS gbSaveGameVBlankHook.o
    COMMAND ${RGBLINK} -x -o gbSaveGameVBlankHook.gb gbSaveGameVBlankHook.o
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.h
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.gb
    COMMAND ${CMAKE_COMMAND} 
        -DSOURCE_FILE="${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.gb"
        -DHEADER_FILE="${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.h"
        -DVARIABLE_NAME="GB_VBLANK_HOOK"
        -P ${CMAKE_HELPERS_DIR}/executebin2h.cmake
)

add_custom_target(gbSaveGameVBlankHook ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gbSaveGameVBlankHook.h
)
