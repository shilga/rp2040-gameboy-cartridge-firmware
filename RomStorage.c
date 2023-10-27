#include "RomStorage.h"

#include "lfs.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>

#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>

#include "GlobalDefines.h"
#include "lfs_pico_hal.h"

#define SetBit(A, k) (A[(k) / 32] |= (1 << ((k) % 32)))
#define ClearBit(A, k) (A[(k) / 32] &= ~(1 << ((k) % 32)))
#define TestBit(A, k) (A[(k) / 32] & (1 << ((k) % 32)))

#define RomBankToPointer(BANK)                                                 \
  ((const uint8_t *)((BANK * GB_ROM_BANK_SIZE) +                               \
                     ROM_STORAGE_FLASH_START_ADDR + 0x13000000))

#define TRANSFER_CHUNK_SIZE 32
#define CHUNKS_PER_BANK (GB_ROM_BANK_SIZE / TRANSFER_CHUNK_SIZE)

static lfs_t *_lfs = NULL;
static uint8_t _lfsFileBuffer[LFS_CACHE_SIZE];
struct lfs_file_config fileconfig = {.buffer = _lfsFileBuffer};

static uint32_t
    _usedBanksFlags[28]; // 888 banks, 32 per uint32, 1 bit for each bank
static uint16_t _usedBanks = 0;

struct __attribute__((__packed__)) RomInfoFile {
  uint32_t magic;
  char name[17];
  uint16_t numBanks;
  uint16_t banks[MAX_BANKS_PER_ROM];
};

static struct RomInfoFile _romInfoFile;
static struct RomInfo _romInfo;

static uint8_t _bankBuffer[GB_ROM_BANK_SIZE];
static uint16_t _lastTransferredBank = 0xFFFF;
static uint16_t _lastTransferredChunk = 0xFFFF;
static char _fileNameBuffer[25] = "/roms/";

static bool _romTransferActive = false;

int RomStorage_init(lfs_t *lfs) {
  int lfs_err = 0;
  _lfs = lfs;
  lfs_dir_t dir;
  lfs_file_t file;
  struct lfs_info lfsInfo;

  memset(_usedBanksFlags, 0, sizeof(_usedBanksFlags));
  memset(g_shortRomInfos, 0, sizeof(g_shortRomInfos));
  g_numRoms = 0;
  _usedBanks = 0;

  lfs_err = lfs_mkdir(_lfs, "/roms");
  if ((lfs_err != LFS_ERR_OK) && (lfs_err != LFS_ERR_EXIST)) {
    printf("Error creating roms directory %d\n", lfs_err);
    return -1;
  }

  lfs_err = lfs_dir_open(_lfs, &dir, "/roms");
  if (lfs_err != LFS_ERR_OK) {
    return -1;
  }

  lfs_err = lfs_dir_read(_lfs, &dir, &lfsInfo);
  while (lfs_err > 0) {
    memcpy(&_fileNameBuffer[6], lfsInfo.name, 17);
    printf("Reading %s\n", _fileNameBuffer);

    if (lfsInfo.type == LFS_TYPE_REG) {
      lfs_err = lfs_file_opencfg(_lfs, &file, _fileNameBuffer, LFS_O_RDONLY,
                                 &fileconfig);
      if (lfs_err != LFS_ERR_OK) {
        printf("Error opening file %d\n", lfs_err);
        return -1;
      }

      lfs_err = lfs_file_read(_lfs, &file, &_romInfoFile,
                              offsetof(struct RomInfoFile, banks));
      if (lfs_err != offsetof(struct RomInfoFile, banks)) {
        printf("Error reading header %d\n", lfs_err);
        return -1;
      }

      lfs_err = lfs_file_read(_lfs, &file, &_romInfoFile.banks,
                              _romInfoFile.numBanks * sizeof(uint16_t));
      if (lfs_err != _romInfoFile.numBanks * sizeof(uint16_t)) {
        printf("Error reading banks %d\n", lfs_err);
        return -1;
      }

      lfs_file_close(_lfs, &file);

      for (size_t i = 0; i < _romInfoFile.numBanks; i++) {
        SetBit(_usedBanksFlags, _romInfoFile.banks[i]);
        _usedBanks++;
      }

      printf("Added %d used banks\n", _romInfoFile.numBanks);

      memcpy(g_shortRomInfos[g_numRoms].name, lfsInfo.name, 16);
      g_shortRomInfos[g_numRoms].name[16] = 0;
      g_shortRomInfos[g_numRoms].firstBank =
          RomBankToPointer(_romInfoFile.banks[0]);
      g_numRoms++;
    }

    lfs_err = lfs_dir_read(_lfs, &dir, &lfsInfo);
  }

  printf("%d banks of 888 in use\n", _usedBanks);

  lfs_err = lfs_dir_close(_lfs, &dir);
  if (lfs_err != LFS_ERR_OK) {
    return -1;
  }

  return 0;
}

int RomStorage_StartNewRomTransfer(uint16_t num_banks, const char *name) {
  bool bank_allocated;
  size_t current_search_bank = 0;
  struct lfs_info lfsInfo;
  int lfs_err;

  if((g_numRoms + 1) >= MAX_ALLOWED_ROMS)
  {
    printf("That is more ROMs as can be handled\n");
    return -1;
  }

  if ((num_banks + _usedBanks) > MAX_BANKS) {
    printf("Not enough free banks for new ROM\n");
    return -1;
  }

  memcpy(&_fileNameBuffer[6], name, 17);
  lfs_err = lfs_stat(_lfs, _fileNameBuffer, &lfsInfo);
  if (lfs_err >= 0) {
    printf("%s already exists\n", _fileNameBuffer);
    return -1;
  }

  _romInfoFile.numBanks = num_banks;
  memcpy(_romInfoFile.name, name, sizeof(_romInfoFile.name) - 1);
  _romInfoFile.name[sizeof(_romInfoFile.name) - 1] = 0;

  for (size_t i = 0; i < num_banks; i++) {
    bank_allocated = false;
    while (!bank_allocated) {
      assert(current_search_bank < MAX_BANKS);

      if (!TestBit(_usedBanksFlags, current_search_bank)) {
        SetBit(_usedBanksFlags, current_search_bank);
        _romInfoFile.banks[i] = current_search_bank;
        bank_allocated = true;
      }
      current_search_bank++;
    }
  }

  printf("Allocated %d banks for new ROM %s\n", num_banks, name);

  for (size_t i = 0; i < num_banks; i++) {
    uint32_t flashAddr = (_romInfoFile.banks[i] * GB_ROM_BANK_SIZE) +
                         ROM_STORAGE_FLASH_START_ADDR;

    printf("Erasing bank %d @%x\n", _romInfoFile.banks[i], flashAddr);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flashAddr, GB_ROM_BANK_SIZE);
    restore_interrupts(ints);
  }

  _romTransferActive = true;
  _lastTransferredChunk = 0xFFFF;
  _lastTransferredBank = 0xFFFF;

  return 0;
}

int RomStorage_TransferChunk(uint16_t bank, uint16_t chunk,
                             const uint8_t data[32]) {

  lfs_file_t file;
  int lfs_err;

  if (!_romTransferActive) {
    return -1;
  }

  if (_lastTransferredBank == 0xFFFF) {
    if (bank != 0) {
      printf("transfer needs to start with first bank\n");
      return -1;
    }
  } else {
    if (_lastTransferredChunk == 0xFFFF) {
      if (bank != (_lastTransferredBank + 1)) {
        printf("bank out of order\n");
        return -1;
      }
    } else if (bank != _lastTransferredBank) {
      printf("new bank only allowed if old is finished\n");
      return -1;
    }
  }

  if (_lastTransferredChunk == 0xFFFF) {
    if (chunk != 0) {
      printf("transfer needs to start with first chunk\n");
      return -1;
    }
  } else if (chunk != (_lastTransferredChunk + 1)) {
    printf("chunk is out of order\n");
    return -1;
  }

  memcpy(&_bankBuffer[chunk * TRANSFER_CHUNK_SIZE], data, TRANSFER_CHUNK_SIZE);

  _lastTransferredBank = bank;
  _lastTransferredChunk = chunk;

  if (chunk == (CHUNKS_PER_BANK - 1)) {
    _lastTransferredChunk = 0xFFFF;
    printf("Transfer of bank %d completed\n", bank);

    uint32_t flashAddr = (_romInfoFile.banks[bank] * GB_ROM_BANK_SIZE) +
                         ROM_STORAGE_FLASH_START_ADDR;
    printf("Writing bank %d @%x\n", _romInfoFile.banks[bank], flashAddr);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flashAddr, _bankBuffer, GB_ROM_BANK_SIZE);
    restore_interrupts(ints);

    if (bank == (_romInfoFile.numBanks - 1)) {
      printf("Transfer of ROM completed\n");

      lfs_err = lfs_file_opencfg(_lfs, &file, _fileNameBuffer,
                                 LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL,
                                 &fileconfig);

      lfs_err = lfs_file_write(_lfs, &file, &_romInfoFile,
                               offsetof(struct RomInfoFile, banks));
      if (lfs_err < 0) {
        printf("Error writing header %d\n", lfs_err);
        return -1;
      }

      lfs_err = lfs_file_write(_lfs, &file, &_romInfoFile.banks,
                               _romInfoFile.numBanks * sizeof(uint16_t));
      if (lfs_err < 0) {
        printf("Error writing bank info %d\n", lfs_err);
        return -1;
      }

      lfs_err = lfs_file_close(_lfs, &file);
      if (lfs_err < 0) {
        printf("Error closing file %d\n", lfs_err);
        return -1;
      }

      RomStorage_init(_lfs); // reinit to reload ROM info

      _romTransferActive = false;
    }
  }

  return 0;
}

uint16_t RomStorage_GetNumUsedBanks() { return _usedBanks; }

int RomStorage_DeleteRom(uint8_t rom) {
  int lfs_err;

  if (rom >= g_numRoms) {
    return -1;
  }

  memcpy(&_fileNameBuffer[6], g_shortRomInfos[rom].name, 17);

  printf("Deleting ROM %d, %s\n", rom, _fileNameBuffer);

  lfs_err = lfs_remove(_lfs, _fileNameBuffer);
  if (lfs_err < 0) {
    printf("Error deleting ROM file %d\n", lfs_err);
    return -1;
  }

  RomStorage_init(_lfs); // reinit to reload ROM info

  return 0;
}

const struct RomInfo *RomStorage_LoadRom(uint8_t rom) {
  int lfs_err;
  lfs_file_t file;

  if (rom >= g_numRoms) {
    return NULL;
  }

  memcpy(&_fileNameBuffer[6], g_shortRomInfos[rom].name, 17);

  printf("Loading ROM %d, %s\n", rom, _fileNameBuffer);

  lfs_err =
      lfs_file_opencfg(_lfs, &file, _fileNameBuffer, LFS_O_RDONLY, &fileconfig);
  if (lfs_err != LFS_ERR_OK) {
    printf("Error opening file %d\n", lfs_err);
    return NULL;
  }

  lfs_err = lfs_file_read(_lfs, &file, &_romInfoFile,
                          offsetof(struct RomInfoFile, banks));
  if (lfs_err != offsetof(struct RomInfoFile, banks)) {
    printf("Error reading header %d\n", lfs_err);
    return NULL;
  }

  lfs_err = lfs_file_read(_lfs, &file, &_romInfoFile.banks,
                          _romInfoFile.numBanks * sizeof(uint16_t));
  if (lfs_err != _romInfoFile.numBanks * sizeof(uint16_t)) {
    printf("Error reading banks %d\n", lfs_err);
    return NULL;
  }

  lfs_file_close(_lfs, &file);

  for (size_t i = 0; i < _romInfoFile.numBanks; i++) {
    _romInfo.romBanks[i] = RomBankToPointer(_romInfoFile.banks[i]);
    g_loadedRomBanks[i] = RomBankToPointer(_romInfoFile.banks[i]);
  }

  return &_romInfo;
}