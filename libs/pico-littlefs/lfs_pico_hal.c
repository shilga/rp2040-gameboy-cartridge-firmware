/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the BSD 3 clause license, which unfortunately
 * won't be written for another century.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * A little flash file system for the Raspberry Pico
 *
 */

#include <limits.h>
#include <stdint.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#if LIB_PICO_MULTICORE
#include "pico/mutex.h"
#endif

#include "lfs.h"
#include "lfs_pico_hal.h"

#define FS_SIZE (2 * 1024 * 1024)

#define LOOK_AHEAD_SIZE 32

uint8_t readBuffer[LFS_CACHE_SIZE];
uint8_t programBuffer[LFS_CACHE_SIZE];
uint8_t lookaheadBuffer[LOOK_AHEAD_SIZE] __attribute__((aligned(4)));

static int pico_hal_read(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size);
static int pico_hal_prog(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size);
static int pico_hal_erase(const struct lfs_config *c, lfs_block_t block);
static int pico_hal_sync(const struct lfs_config *c);

static int pico_lock(void);
static int pico_unlock(void);

// configuration of the filesystem is provided by this struct
// for Pico: prog size = 256, block size = 4096, so cache is 8K
// minimum cache = block size, must be multiple
struct lfs_config pico_cfg = {
    // block device operations
    .read = pico_hal_read,
    .prog = pico_hal_prog,
    .erase = pico_hal_erase,
    .sync = pico_hal_sync,
#if LIB_PICO_MULTICORE
    .lock = pico_lock,
    .unlock = pico_unlock,
#endif
    // block device configuration
    .read_size = 1,
    .prog_size = FLASH_PAGE_SIZE,
    .block_size = FLASH_SECTOR_SIZE,
    .block_count = FS_SIZE / FLASH_SECTOR_SIZE,
    .cache_size = LFS_CACHE_SIZE,
    .lookahead_size = LOOK_AHEAD_SIZE,
    .block_cycles = 500,
    .read_buffer = readBuffer,
    .prog_buffer = programBuffer,
    .lookahead_buffer = lookaheadBuffer};

// Pico specific hardware abstraction functions

// file system offset in flash
const char *FS_BASE = (char *)(PICO_FLASH_SIZE_BYTES - FS_SIZE);

static int pico_hal_read(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size) {
  assert(block < c->block_count);
  assert(off + size <= c->block_size);
  // read flash via XIP mapped space
  memcpy(buffer,
         FS_BASE + XIP_NOCACHE_NOALLOC_BASE + (block * c->block_size) + off,
         size);
  return LFS_ERR_OK;
}

static int pico_hal_prog(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size) {
  assert(block < c->block_count);
  // program with SDK
  uint32_t p = (uint32_t)FS_BASE + (block * c->block_size) + off;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_program(p, buffer, size);
  restore_interrupts(ints);
  return LFS_ERR_OK;
}

static int pico_hal_erase(const struct lfs_config *c, lfs_block_t block) {
  assert(block < c->block_count);
  // erase with SDK
  uint32_t p = (uint32_t)FS_BASE + block * c->block_size;
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(p, c->block_size);
  restore_interrupts(ints);
  return LFS_ERR_OK;
}

static int pico_hal_sync(const struct lfs_config *c)
{
  return LFS_ERR_OK;
}

#if LIB_PICO_MULTICORE

static recursive_mutex_t fs_mtx;

static int pico_lock(void) {
  recursive_mutex_enter_blocking(&fs_mtx);
  return LFS_ERR_OK;
}

static int pico_unlock(void) {
  recursive_mutex_exit(&fs_mtx);
  return LFS_ERR_OK;
}
#endif

// utility functions

const char *pico_errmsg(int err) {
  static const struct {
    int err;
    char *text;
  } mesgs[] = {{LFS_ERR_OK, "No error"},
               {LFS_ERR_IO, "Error during device operation"},
               {LFS_ERR_CORRUPT, "Corrupted"},
               {LFS_ERR_NOENT, "No directory entry"},
               {LFS_ERR_EXIST, "Entry already exists"},
               {LFS_ERR_NOTDIR, "Entry is not a dir"},
               {LFS_ERR_ISDIR, "Entry is a dir"},
               {LFS_ERR_NOTEMPTY, "Dir is not empty"},
               {LFS_ERR_BADF, "Bad file number"},
               {LFS_ERR_FBIG, "File too large"},
               {LFS_ERR_INVAL, "Invalid parameter"},
               {LFS_ERR_NOSPC, "No space left on device"},
               {LFS_ERR_NOMEM, "No more memory available"},
               {LFS_ERR_NOATTR, "No data/attr available"},
               {LFS_ERR_NAMETOOLONG, "File name too long"}};

  for (int i = 0; i < sizeof(mesgs) / sizeof(mesgs[0]); i++)
    if (err == mesgs[i].err)
      return mesgs[i].text;
  return "Unknown error";
}