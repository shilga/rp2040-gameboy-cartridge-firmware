/* RP2040 GameBoy cartridge
 * Copyright (C) 2023 Sebastian Quilitz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef D05D89A4_C956_401B_9A8D_564BE51C4811
#define D05D89A4_C956_401B_9A8D_564BE51C4811

#include "GlobalDefines.h"
#include <lfs.h>
#include <stdint.h>

struct RomInfo {
  uint16_t numRomBanks;
  uint16_t numRamBanks;
  uint8_t mbc;
};

int RomStorage_init(lfs_t *lfs);

int RomStorage_StartNewRomTransfer(uint16_t num_banks, const char *name);

int RomStorage_TransferRomChunk(uint16_t bank, uint16_t chunk,
                                const uint8_t data[32]);

uint16_t RomStorage_GetNumUsedBanks();

int RomStorage_DeleteRom(uint8_t rom);

int RomStorage_StartRamUpload(uint8_t rom);

int RomStorage_GetRamDownloadChunk(uint8_t data[32], uint16_t *bank,
                                   uint16_t *chunk);

int RomStorage_StartRamDownload(uint8_t rom);

int RomStorage_TransferRamUploadChunk(uint16_t bank, uint16_t chunk,
                                      const uint8_t data[32]);

const struct RomInfo *RomStorage_LoadRom(uint8_t rom);

#endif /* D05D89A4_C956_401B_9A8D_564BE51C4811 */
