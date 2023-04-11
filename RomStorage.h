#ifndef ROMSTORAGE_H_
#define ROMSTORAGE_H_

#include "GlobalDefines.h"
#include <lfs.h>
#include <stdint.h>

struct RomInfo {
  uint16_t numRomBanks;
  uint16_t numRamBanks;
  uint8_t mbc;
  const uint8_t *romBanks[MAX_BANKS_PER_ROM];
};

int RomStorage_init(lfs_t *lfs);

int RomStorage_StartNewRomTransfer(uint16_t num_banks, const char *name);

int RomStorage_TransferChunk(uint16_t bank, uint16_t chunk,
                             const uint8_t data[32]);

uint16_t RomStorage_GetNumUsedBanks();

int RomStorage_DeleteRom(uint8_t rom);

const struct RomInfo *RomStorage_LoadRom(uint8_t rom);

#endif /* ROMSTORAGE_H_ */