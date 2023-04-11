#ifndef GLOBALDEFINES_H
#define GLOBALDEFINES_H

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

#define SMC_WS2812 3

#define GB_RAM_BANK_SIZE 0x2000U
#define GB_ROM_BANK_SIZE 0x4000U
/* 8 banks = 64K of RAM enough for MBC3 (32K) and most of MBC5*/
#define GB_MAX_RAM_BANKS 8

#define ROM_STORAGE_FLASH_START_ADDR 0x00020000
#define MAX_ALLOWED_ROMS 16
#define MAX_BANKS 888
#define MAX_BANKS_PER_ROM 0x200

extern const volatile uint8_t *ram_base;
extern const volatile uint8_t *rom_low_base;
extern const volatile uint8_t *rom_high_base;

extern uint8_t memory[];
extern uint8_t ram_memory[];

struct ShortRomInfo {
  char name[17];
  const uint8_t *firstBank;
};

extern struct ShortRomInfo g_shortRomInfos[MAX_ALLOWED_ROMS];
extern uint8_t g_numRoms;

extern const uint8_t *g_loadedRomBanks[MAX_BANKS_PER_ROM];

#endif /* GLOBALDEFINES_H */