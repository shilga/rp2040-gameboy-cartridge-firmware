#ifndef ROMSTORAGE_H_
#define ROMSTORAGE_H_

#include <lfs.h>
#include <stdint.h>

int RomStorage_init(lfs_t* lfs);

int RomStorage_StartNewRomTransfer(uint16_t num_banks, const char* name);

int RomStorage_TransferChunk(uint16_t bank, uint16_t chunk, const uint8_t data[32]);

#endif /* ROMSTORAGE_H_ */