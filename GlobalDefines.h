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
#define GB_MAX_RAM_BANKS 8 /* 64K of RAM enough for MBC3 (32K) and most of MBC5*/

extern const volatile uint8_t *ram_base;
extern const volatile uint8_t *rom_low_base;
extern const volatile uint8_t *rom_high_base;

extern uint8_t memory[];
extern uint8_t ram_memory[];

extern const uint8_t *_games[];


#endif /* GLOBALDEFINES_H */