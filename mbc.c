#include "GlobalDefines.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hardware/pio.h>
#include <pico/platform.h>

void __no_inline_not_in_flash_func(runNoMbcGame)(uint8_t game) {
  memcpy(memory, _games[game], GB_ROM_BANK_SIZE);

  rom_high_base = &(_games[game][GB_ROM_BANK_SIZE]);

  // disable RAM access state machines, they are not needed anymore
  pio_set_sm_mask_enabled(
      pio1, (1 << SMC_GB_RAM_READ) | (1 << SMC_GB_RAM_WRITE), false);

  printf("No MBC game loaded\n");

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = *((uint32_t *)(&pio1->rxf[SMC_GB_MAIN]));
      bool write = rdAndAddr & 0x00000001;
      uint16_t addr = (rdAndAddr >> 1) & 0xFFFF;

      if (write) {
        uint8_t data = pio_sm_get_blocking(pio1, SMC_GB_MAIN) & 0xFF;
      }
    }
  }
}

void __no_inline_not_in_flash_func(runMbc1Game)(uint8_t game,
                                                uint16_t num_rom_banks,
                                                uint8_t num_ram_banks) {
  const uint8_t *gameptr = _games[game];
  uint8_t rom_bank = 1;
  uint8_t rom_bank_new = 1;
  uint8_t rom_bank_high = 0;
  uint8_t rom_bank_low = 1;
  uint8_t ram_bank = GB_MAX_RAM_BANKS;
  uint8_t ram_bank_new = 0;
  uint8_t ram_bank_local = GB_MAX_RAM_BANKS;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  bool mode_select = 0;
  bool ram_dirty = 0;
  uint16_t rom_banks_mask = num_rom_banks - 1;

  memcpy(memory, gameptr, GB_ROM_BANK_SIZE);

  rom_high_base = &gameptr[GB_ROM_BANK_SIZE];

  printf("MBC1 game loaded\n");
  printf("initial bank %d a %x\n", rom_bank,
         ((unsigned)gameptr + (GB_ROM_BANK_SIZE * rom_bank)));

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = *((uint32_t *)(&pio1->rxf[SMC_GB_MAIN]));
      bool write = rdAndAddr & 0x00000001;
      uint16_t addr = (rdAndAddr >> 1) & 0xFFFF;

      if (write) {
        uint8_t data = pio_sm_get_blocking(pio1, SMC_GB_MAIN) & 0xFF;

        switch (addr & 0xE000) {
        case 0x0000:
          new_ram_enabled = ((data & 0x0F) == 0x0A);
          break;

        case 0x2000:
          rom_bank_low = (data & 0x1F);
          if ((rom_bank_low & 0x1F) == 0x00)
            rom_bank_low++;
          break;

        case 0x4000:
          if (mode_select) {
            ram_bank_new = data & 0x03;
          } else {
            rom_bank_high = data & 0x03;
          }
          break;

        case 0x6000:
          mode_select = (data & 1);
          break;
        case 0xA000:                          // write to RAM
          if(!ram_dirty)
          {
            pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
            ram_dirty = true;
          }
          break;
        default:

          break;
        }

        if (mode_select == 0) {
          rom_bank_new = (rom_bank_high << 5) | rom_bank_low;
        } else {
          rom_bank_new = rom_bank_low;
        }
        rom_bank_new = rom_bank_new & rom_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base = &gameptr[GB_ROM_BANK_SIZE * rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
        }

        ram_bank_local = ram_enabled ? ram_bank_new : GB_MAX_RAM_BANKS;

        if (ram_bank != ram_bank_local) {
          ram_bank = ram_bank_local;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      }
    }
  }
}

void __no_inline_not_in_flash_func(runMbc3Game)(uint8_t game,
                                                uint16_t num_rom_banks,
                                                uint8_t num_ram_banks) {
  const uint8_t *gameptr = _games[game];
  uint8_t rom_bank = 1;
  uint8_t rom_bank_new = 1;
  uint8_t ram_bank = GB_MAX_RAM_BANKS;
  uint8_t ram_bank_new = 0;
  uint8_t ram_bank_local = GB_MAX_RAM_BANKS;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  bool ram_dirty = 0;
  uint16_t rom_banks_mask = num_rom_banks - 1;

  memcpy(memory, gameptr, GB_ROM_BANK_SIZE);

  rom_high_base = &gameptr[GB_ROM_BANK_SIZE];

  printf("MBC3 game loaded\n");
  printf("initial bank %d a %x\n", rom_bank,
         ((unsigned)gameptr + (GB_ROM_BANK_SIZE * rom_bank)));

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = *((uint32_t *)(&pio1->rxf[SMC_GB_MAIN]));
      bool write = rdAndAddr & 0x00000001;
      uint16_t addr = (rdAndAddr >> 1) & 0xFFFF;

      if (write) {
        uint8_t data = pio_sm_get_blocking(pio1, SMC_GB_MAIN) & 0xFF;

        switch (addr & 0xE000) {
        case 0x0000:
          new_ram_enabled = ((data & 0x0F) == 0x0A);
          break;

        case 0x2000:
          rom_bank_new = (data & 0x7F);
          if (rom_bank_new == 0x00)
            rom_bank_new++;
          break;

        case 0x4000:
          ram_bank_new = data & 0x03;
          break;

        case 0x6000:

          break;
        case 0xA000:                          // write to RAM
          if(!ram_dirty)
          {
            pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
            ram_dirty = true;
          }
          break;
        default:

          break;
        }

        rom_bank_new = rom_bank_new & rom_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base = &gameptr[GB_ROM_BANK_SIZE * rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
        }

        ram_bank_local = ram_enabled ? ram_bank_new : GB_MAX_RAM_BANKS;

        if (ram_bank != ram_bank_local) {
          ram_bank = ram_bank_local;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      }
    }
  }
}

void __no_inline_not_in_flash_func(runMbc5Game)(uint8_t game,
                                                uint16_t num_rom_banks,
                                                uint8_t num_ram_banks) {
  const uint8_t *gameptr = _games[game];
  uint16_t rom_bank = 1;
  uint16_t rom_bank_new = 1;
  uint8_t ram_bank = GB_MAX_RAM_BANKS;
  uint8_t ram_bank_new = 0;
  uint8_t ram_bank_local = GB_MAX_RAM_BANKS;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  bool ram_dirty = 0;
  const uint16_t rom_banks_mask = num_rom_banks - 1;
  const uint8_t ram_banks_mask = num_ram_banks - 1;

  memcpy(memory, gameptr, GB_ROM_BANK_SIZE);

  rom_high_base = &gameptr[GB_ROM_BANK_SIZE];

  printf("MBC5 game loaded\n");
  printf("initial bank %d a %x\n", rom_bank,
         ((unsigned)gameptr + (GB_ROM_BANK_SIZE * rom_bank)));

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = *((uint32_t *)(&pio1->rxf[SMC_GB_MAIN]));
      bool write = rdAndAddr & 0x00000001;
      uint16_t addr = (rdAndAddr >> 1) & 0xFFFF;

      if (write) {
        uint8_t data = pio_sm_get_blocking(pio1, SMC_GB_MAIN) & 0xFF;

        switch (addr & 0xF000) {
        case 0x0000:
        case 0x1000:
          new_ram_enabled = ((data & 0x0F) == 0x0A);
          break;

        case 0x2000:
          rom_bank_new = (rom_bank & 0x0100) | data;
          break;

        case 0x3000:
          rom_bank_new = (rom_bank & 0x00FF) | ((data << 8) & 0x0100);
          break;
        case 0x4000:
          ram_bank_new = data & 0x0F;
          break;

        case 0x6000:

          break;
        case 0xA000:                          // write to RAM
          if(!ram_dirty)
          {
            pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
            ram_dirty = true;
          }
          break;
        default:

          break;
        }

        rom_bank_new = rom_bank_new & rom_banks_mask;
        ram_bank_new = ram_bank_new & ram_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base = &gameptr[GB_ROM_BANK_SIZE * rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
        }

        ram_bank_local = ram_enabled ? ram_bank_new : GB_MAX_RAM_BANKS;

        if (ram_bank != ram_bank_local) {
          ram_bank = ram_bank_local;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      }
    }
  }
}
