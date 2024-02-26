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

#include "RomStorage.h"

#include "GameBoyHeader.h"

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

#define RomBankToDirectSsi(BANK)                                               \
  ((((BANK * GB_ROM_BANK_SIZE) + ROM_STORAGE_FLASH_START_ADDR) << 8) | 0xA0)

#define TRANSFER_CHUNK_SIZE 32
#define CHUNKS_PER_BANK (GB_ROM_BANK_SIZE / TRANSFER_CHUNK_SIZE)

#define ROMINFO_FILE_MAGIC 0xCAFEBABE

static lfs_t *_lfs = NULL;
static uint8_t _lfsFileBuffer[LFS_CACHE_SIZE];
struct lfs_file_config _fileconfig = {.buffer = _lfsFileBuffer};
lfs_file_t _ramTransferFile;

static uint32_t
    _usedBanksFlags[28]; // 888 banks, 32 per uint32, 1 bit for each bank
static uint16_t _usedBanks = 0;

struct __attribute__((__packed__)) RomInfoFile {
  uint32_t magic;
  char name[17];
  uint16_t numBanks;
  uint16_t speedSwitchBank;
  uint16_t banks[MAX_BANKS_PER_ROM];
} _romInfoFile;

static uint8_t _bankBuffer[GB_ROM_BANK_SIZE];
static uint16_t _lastTransferredBank = 0xFFFF;
static uint16_t _lastTransferredChunk = 0xFFFF;
static char _fileNameBuffer[25] = "/roms/";
static char _filenamebufferSaves[40] = "saves/";

static size_t _ramBytesTransferred = 0;
static uint8_t _ramTransferCurrentRom = 0xFFU;

static bool _romTransferActive = false;
static bool _ramTransferActive = false;

static int readRomInfoFile(lfs_file_t *file) {
  int lfs_err = 0;
  lfs_err = lfs_file_read(_lfs, file, &_romInfoFile,
                          offsetof(struct RomInfoFile, speedSwitchBank));
  if (lfs_err != offsetof(struct RomInfoFile, speedSwitchBank)) {
    printf("Error reading header %d\n", lfs_err);
    return lfs_err;
  }

  if (_romInfoFile.magic == ROMINFO_FILE_MAGIC) {
    lfs_err = lfs_file_read(_lfs, file, &_romInfoFile.speedSwitchBank,
                            sizeof(uint16_t));
  } else {
    _romInfoFile.speedSwitchBank = 0xFFFFU;
  }

  lfs_err = lfs_file_read(_lfs, file, &_romInfoFile.banks,
                          _romInfoFile.numBanks * sizeof(uint16_t));
  if (lfs_err != _romInfoFile.numBanks * sizeof(uint16_t)) {
    printf("Error reading banks %d\n", lfs_err);
    return lfs_err;
  }

  return 0;
}

int RomStorage_init(lfs_t *lfs) {
  int lfs_err = 0;
  _lfs = lfs;
  lfs_dir_t dir;
  lfs_file_t file;
  struct lfs_info lfsInfo;
  const uint8_t *firstBankPointer;

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
                                 &_fileconfig);
      if (lfs_err != LFS_ERR_OK) {
        printf("Error opening file %d\n", lfs_err);
        return -1;
      }

      lfs_err = readRomInfoFile(&file);

      lfs_file_close(_lfs, &file);
      if (lfs_err != LFS_ERR_OK) {
        return -1;
      }

      for (size_t i = 0; i < _romInfoFile.numBanks; i++) {
        SetBit(_usedBanksFlags, _romInfoFile.banks[i]);
        _usedBanks++;
      }

      printf("Added %d used banks\n", _romInfoFile.numBanks);

      memcpy(g_shortRomInfos[g_numRoms].name, lfsInfo.name, 16);
      g_shortRomInfos[g_numRoms].name[16] = 0;
      firstBankPointer = RomBankToPointer(_romInfoFile.banks[0]);
      g_shortRomInfos[g_numRoms].firstBank = firstBankPointer;
      g_shortRomInfos[g_numRoms].numRamBanks =
          GameBoyHeader_readRamBankCount(firstBankPointer);
      g_shortRomInfos[g_numRoms].speedSwitchBank = _romInfoFile.speedSwitchBank;
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

int RomStorage_StartNewRomTransfer(uint16_t num_banks, uint16_t speedSwitchBank,
                                   const char *name) {
  bool bank_allocated;
  size_t current_search_bank = 0;
  struct lfs_info lfsInfo;
  int lfs_err;

  if ((g_numRoms + 1) > MAX_ALLOWED_ROMS) {
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

  _romInfoFile.magic = ROMINFO_FILE_MAGIC;
  _romInfoFile.numBanks = num_banks;
  _romInfoFile.speedSwitchBank = speedSwitchBank;
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
  printf("ROM uses bank %d for speed switch\n", speedSwitchBank);

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

int RomStorage_TransferRomChunk(uint16_t bank, uint16_t chunk,
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
                                 &_fileconfig);

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
  memcpy(&_filenamebufferSaves[6], g_shortRomInfos[rom].name, 17);

  printf("Deleting ROM %d, %s\n", rom, _fileNameBuffer);
  printf("Deleting savegame %s\n", _filenamebufferSaves);

  lfs_err = lfs_remove(_lfs, _filenamebufferSaves);
  if (lfs_err < 0) {
    printf("Error deleting savegame file %d\n", lfs_err);
  }

  lfs_err = lfs_remove(_lfs, _fileNameBuffer);
  if (lfs_err < 0) {
    printf("Error deleting ROM file %d\n", lfs_err);
    return -1;
  }

  RomStorage_init(_lfs); // reinit to reload ROM info

  return 0;
}

int RomStorage_StartRamDownload(uint8_t rom) {
  int lfs_err;

  if (_romTransferActive) {
    return -1;
  }

  if (_ramTransferActive) {
    return -2;
  }

  if (rom >= g_numRoms) {
    return -3;
  }

  if (g_shortRomInfos[rom].numRamBanks == 0) {
    return -3;
  }

  _ramTransferActive = true;
  _ramTransferCurrentRom = rom;

  memcpy(&_filenamebufferSaves[6], g_shortRomInfos[rom].name, 17);

  printf("Loading savefile %d, %s\n", rom, _filenamebufferSaves);

  lfs_err = lfs_file_opencfg(_lfs, &_ramTransferFile, _filenamebufferSaves,
                             LFS_O_RDONLY, &_fileconfig);

  if (lfs_err != LFS_ERR_OK) {
    printf("Error opening file %d\n", lfs_err);
    return -4;
  }

  _ramBytesTransferred = 0;

  return 0;
}

int RomStorage_GetRamDownloadChunk(uint8_t data[32], uint16_t *bank,
                                   uint16_t *chunk) {
  int lfs_err;

  lfs_err = lfs_file_read(_lfs, &_ramTransferFile, data, 32);

  if (lfs_err != 32) {
    printf("Error reading RAM chunk %d\n", lfs_err);
    return -4;
  }

  *bank = _ramBytesTransferred / GB_RAM_BANK_SIZE;
  *chunk = (_ramBytesTransferred - (*bank * GB_RAM_BANK_SIZE)) / 32;

  _ramBytesTransferred += 32;

  if (_ramBytesTransferred >=
      g_shortRomInfos[_ramTransferCurrentRom].numRamBanks * GB_RAM_BANK_SIZE) {
    printf("RAM transfer finished\n");

    lfs_file_close(_lfs, &_ramTransferFile);

    _ramBytesTransferred = 0;
    _ramTransferCurrentRom = 0xFFU;
    _ramTransferActive = false;
  }

  return 0;
}

int RomStorage_StartRamUpload(uint8_t rom) {
  int lfs_err;

  if (_romTransferActive) {
    return -1;
  }

  if (_ramTransferActive) {
    return -2;
  }

  if (rom >= g_numRoms) {
    return -3;
  }

  if (g_shortRomInfos[rom].numRamBanks == 0) {
    return -3;
  }

  _ramTransferActive = true;
  _ramTransferCurrentRom = rom;

  memcpy(&_filenamebufferSaves[6], g_shortRomInfos[rom].name, 17);

  printf("Opening savefile %d, %s\n", rom, _filenamebufferSaves);

  lfs_err = lfs_file_opencfg(_lfs, &_ramTransferFile, _filenamebufferSaves,
                             LFS_O_WRONLY | LFS_O_CREAT, &_fileconfig);

  if (lfs_err != LFS_ERR_OK) {
    printf("Error opening file %d\n", lfs_err);
    return -4;
  }

  _ramBytesTransferred = 0;

  return 0;
}

int RomStorage_TransferRamUploadChunk(uint16_t bank, uint16_t chunk,
                                      const uint8_t data[32]) {
  int lfs_err;

  uint16_t expected_bank = _ramBytesTransferred / GB_RAM_BANK_SIZE;
  uint16_t expected_chunk =
      ((_ramBytesTransferred - (expected_bank * GB_RAM_BANK_SIZE)) / 32) + 1;

  if (expected_chunk == GB_RAM_BANK_SIZE / 32) { // Here start the next bank
    expected_chunk = 0;
    expected_bank += 1;
  }

  if (expected_bank != bank) {
    printf("Wrong bank received, expected %d, got %d\n", expected_bank, bank);
  }

  if (expected_chunk != chunk) {
    printf("Wrong bank received, expected %d, got %d\n", expected_chunk, bank);
  }

  lfs_err = lfs_file_write(_lfs, &_ramTransferFile, data, 32);

  if (lfs_err != 32) {
    printf("Error reading RAM chunk %d\n", lfs_err);
    return -4;
  }

  _ramBytesTransferred += 32;

  if (_ramBytesTransferred >=
      g_shortRomInfos[_ramTransferCurrentRom].numRamBanks * GB_RAM_BANK_SIZE) {
    printf("RAM transfer finished\n");

    lfs_file_close(_lfs, &_ramTransferFile);

    _ramBytesTransferred = 0;
    _ramTransferCurrentRom = 0xFFU;
    _ramTransferActive = false;
  }

  return 0;
}

const int RomStorage_LoadRom(uint8_t rom) {
  int lfs_err;
  lfs_file_t file;

  if (rom >= g_numRoms) {
    return -1;
  }

  if (_romTransferActive) {
    return -2;
  }

  memcpy(&_fileNameBuffer[6], g_shortRomInfos[rom].name, 17);

  printf("Loading ROM %d, %s\n", rom, _fileNameBuffer);

  lfs_err = lfs_file_opencfg(_lfs, &file, _fileNameBuffer, LFS_O_RDONLY,
                             &_fileconfig);
  if (lfs_err != LFS_ERR_OK) {
    printf("Error opening file %d\n", lfs_err);
    return -3;
  }

  lfs_err = readRomInfoFile(&file);

  lfs_file_close(_lfs, &file);

  if (lfs_err != LFS_ERR_OK) {
    return -4;
  }

  for (size_t i = 0; i < _romInfoFile.numBanks; i++) {
    g_loadedRomBanks[i] = RomBankToPointer(_romInfoFile.banks[i]);
    g_loadedDirectAccessRomBanks[i] = RomBankToDirectSsi(_romInfoFile.banks[i]);
  }

  return 0;
}
