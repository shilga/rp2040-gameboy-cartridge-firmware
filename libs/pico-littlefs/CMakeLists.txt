project(littlefs-lib)

add_library(littlefs-lib STATIC lfs.c lfs_util.c lfs_pico_hal.c)

target_include_directories(littlefs-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(littlefs-lib PUBLIC hardware_flash)
