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

#ifndef A6E4EABE_18C1_4BCB_A021_7C59DEE53104
#define A6E4EABE_18C1_4BCB_A021_7C59DEE53104

#include <stdint.h>

#define WS2812_PIN 27

#define PIN_GB_RESET 26

#define SMC_GB_MAIN 0
#define SMC_GB_DETECT_A14 1
#define SMC_GB_RAM_READ 2
#define SMC_GB_RAM_WRITE 3

#define SMC_GB_ROM_LOW 0
#define SMC_GB_ROM_HIGH 1
#define SMC_GB_WRITE_DATA 2
#define SMC_GB_A15LOW_A14IRQS 3

#define GB_MBC2_RAM_SIZE 0x200U
#define GB_RAM_BANK_SIZE 0x2000U
#define GB_ROM_BANK_SIZE 0x4000U
/* 16 banks = 128K of RAM enough for MBC3 (32K) and MBC5*/
#define GB_MAX_RAM_BANKS 16

#define ROM_STORAGE_FLASH_START_ADDR 0x00020000
#define MAX_BANKS 888
#define MAX_BANKS_PER_ROM 0x200

/*
 * With the current implementation the list of ROMs can
 * use 4k of the saveram. With 20 characters per string
 * that gives around 200 possible games. Use 180 to be
 * save as also other information is stored in that area.
 */
#define MAX_ALLOWED_ROMS 180

extern const volatile uint8_t *volatile ram_base;
extern const volatile uint8_t *volatile rom_low_base;
extern volatile uint32_t rom_high_base_flash_direct;

extern volatile uint8_t *_rtcLatchPtr;
extern volatile uint8_t *_rtcRealPtr;

extern uint8_t memory[];
extern uint8_t ram_memory[];
extern uint8_t memory_vblank_hook_bank[];
extern uint8_t memory_vblank_hook_bank2[];

struct RomInfo {
  const uint8_t *firstBank;
  uint16_t speedSwitchBank;
  uint8_t numRamBanks;
  uint8_t mbc;
  char name[17];
};

extern uint8_t g_numRoms;

extern const uint8_t *g_loadedRomBanks[MAX_BANKS_PER_ROM];
extern uint32_t g_loadedDirectAccessRomBanks[MAX_BANKS_PER_ROM];
extern struct RomInfo g_loadedRomInfo;

void setSsi8bit();
void setSsi32bit();
void loadDoubleSpeedPio();
void storeSaveRamToFile(const struct RomInfo *shortRomInfo);
void restoreSaveRamFromFile(const struct RomInfo *shortRomInfo);
int restoreRtcFromFile(const struct RomInfo *romInfo);
void storeRtcToFile(const struct RomInfo *romInfo);

struct __attribute__((packed)) GbRtc {
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;
  uint8_t days;
  union {
    struct {
      uint8_t days_high : 1;
      uint8_t reserved : 5;
      uint8_t halt : 1;
      uint8_t days_carry : 1;
    };
    uint8_t asByte;
  } status;
};
union GbRtcUnion {
  struct GbRtc reg;
  uint8_t asArray[5];
};

extern volatile union GbRtcUnion g_rtcReal;
extern volatile union GbRtcUnion g_rtcLatched;
extern uint64_t g_rtcTimestamp;
extern uint64_t g_globalTimestamp;
extern uint8_t g_flashSerialNumber[8];
extern char g_serialNumberString[];

/* taken from
 * https://github.com/tihmstar/libgeneral/blob/master/include/libgeneral/macros.h.in
 */
#define ASSURE(a)                                                              \
  do {                                                                         \
    if ((a) == 0) {                                                            \
      err = -(__LINE__);                                                       \
      goto error;                                                              \
    }                                                                          \
  } while (0)

#define PRINTASSURE(cond, errstr...)                                           \
  do {                                                                         \
    if ((cond) == 0) {                                                         \
      err = -(__LINE__);                                                       \
      printf(errstr);                                                          \
      goto error;                                                              \
    }                                                                          \
  } while (0)

#endif /* A6E4EABE_18C1_4BCB_A021_7C59DEE53104 */
