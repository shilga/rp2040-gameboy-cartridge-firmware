#ifndef LFS_PICO_HAL_H
#define LFS_PICO_HAL_H

#include "lfs.h"

#include <hardware/flash.h>

#define LFS_CACHE_SIZE (FLASH_SECTOR_SIZE / 4)

extern struct lfs_config pico_cfg;

#endif /* LFS_PICO_HAL_H */
