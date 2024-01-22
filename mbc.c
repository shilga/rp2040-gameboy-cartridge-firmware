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

#include "GlobalDefines.h"
#include "hardware/regs/clocks.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/structs/scb.h>
#include <hardware/sync.h>
#include <pico/platform.h>

void detect_speed_change(uint16_t addr, uint16_t rom_bank);

void __no_inline_not_in_flash_func(runNoMbcGame)(uint8_t game) {
  memcpy(memory, g_loadedRomBanks[0], GB_ROM_BANK_SIZE);
  memcpy(&memory[GB_ROM_BANK_SIZE], g_loadedRomBanks[1], GB_ROM_BANK_SIZE);

  // rom_high_base_flash_direct =
  //     ((((uint32_t)g_loadedRomBanks[1]) - 0x13000000UL) << 8U) | 0xA0;

  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);

  // rom_high_base = &(_games[game][GB_ROM_BANK_SIZE]);

  // disable RAM access state machines, they are not needed without any MBC
  pio_set_sm_mask_enabled(pio1,
                          (1 << SMC_GB_MAIN) | (1 << SMC_GB_RAM_READ) |
                              (1 << SMC_GB_RAM_WRITE),
                          false);

  // Turn off all clocks when in sleep mode except for RTC
  // clocks_hw->sleep_en0 =
  //     CLOCKS_SLEEP_EN0_CLK_SYS_SRAM0_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_SRAM1_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_SRAM2_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_SRAM3_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_PIO1_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PIO0_BITS
  //     | CLOCKS_SLEEP_EN0_CLK_SYS_PLL_SYS_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_BUSFABRIC_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_BUSCTRL_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_DMA_BITS |
  //     CLOCKS_SLEEP_EN0_CLK_SYS_CLOCKS_BITS;
  // // clocks_hw->sleep_en1 =
  // //     CLOCKS_SLEEP_EN1_CLK_SYS_XOSC_MSB |
  // CLOCKS_SLEEP_EN1_CLK_SYS_XIP_BITS; clocks_hw->sleep_en1 =
  // CLOCKS_SLEEP_EN1_CLK_SYS_XOSC_MSB;

  printf("No MBC game loaded\n");

  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);

  __compiler_memory_barrier();
  setSsi8bit();
  GbDma_StartDmaDirect();

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  // uint save = scb_hw->scr;
  // // Enable deep sleep at the proc
  // scb_hw->scr = save | M0PLUS_SCR_SLEEPDEEP_BITS;

  // // Go to sleep (forever)
  // __wfi();

  // printf("awake\n");

  while (1)
    ;
}

void __no_inline_not_in_flash_func(runMbc1Game)(uint8_t game,
                                                uint16_t num_rom_banks,
                                                uint8_t num_ram_banks) {
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

  memcpy(memory, g_shortRomInfos[game].firstBank, GB_ROM_BANK_SIZE);

  rom_high_base = g_loadedRomBanks[1];
  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  printf("MBC1 game loaded\n");
  printf("initial bank %d a %p\n", rom_bank, g_loadedRomBanks[1]);

  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);

  __compiler_memory_barrier();
  setSsi8bit();
  GbDma_StartDmaDirect();

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = pio_sm_get_blocking(pio1, SMC_GB_MAIN);
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
        case 0xA000: // write to RAM
          if (!ram_dirty) {
            // pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
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
          rom_high_base = g_loadedRomBanks[rom_bank];
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
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
  uint8_t rom_bank = 1;
  uint8_t rom_bank_new = 1;
  uint8_t ram_bank = GB_MAX_RAM_BANKS;
  uint8_t ram_bank_new = 0;
  uint8_t ram_bank_local = GB_MAX_RAM_BANKS;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  bool ram_dirty = 0;
  uint16_t rom_banks_mask = num_rom_banks - 1;

  memcpy(memory, g_shortRomInfos[game].firstBank, GB_ROM_BANK_SIZE);

  rom_high_base = g_loadedRomBanks[1];
  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  printf("MBC3 game loaded\n");
  printf("initial bank %d a %p\n", rom_bank, g_loadedRomBanks[1]);

  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);

  __compiler_memory_barrier();
  setSsi8bit();
  GbDma_StartDmaDirect();

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = pio_sm_get_blocking(pio1, SMC_GB_MAIN);
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
        case 0xA000: // write to RAM
          if (!ram_dirty) {
            // pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
            ram_dirty = true;
          }
          break;
        default:

          break;
        }

        rom_bank_new = rom_bank_new & rom_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base = g_loadedRomBanks[rom_bank];
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
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

  memcpy(memory, g_shortRomInfos[game].firstBank, GB_ROM_BANK_SIZE);

  rom_high_base = g_loadedRomBanks[1];
  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  printf("MBC5 game loaded\n");
  printf("initial bank %d a %p\n", rom_bank, g_loadedRomBanks[1]);

  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);

  __compiler_memory_barrier();
  setSsi8bit();
  GbDma_StartDmaDirect();

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (1) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = pio_sm_get_blocking(pio1, SMC_GB_MAIN);
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
        case 0xA000: // write to RAM
          if (!ram_dirty) {
            // pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
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
          rom_high_base = g_loadedRomBanks[rom_bank];
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
        }

        ram_bank_local = ram_enabled ? ram_bank_new : GB_MAX_RAM_BANKS;

        if (ram_bank != ram_bank_local) {
          ram_bank = ram_bank_local;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      } else { // read
        detect_speed_change(addr, rom_bank);
      }
    }
  }
}

enum eDETECT_SPEED_CHANGE_STATE {
  SPEED_CHANGE_IDLE,
  SPEED_CHANGE_LDH,
  SPEED_CHANGE_KEY1,
} _speedChangeState = SPEED_CHANGE_IDLE;

void __no_inline_not_in_flash_func(detect_speed_change)(uint16_t addr,
                                                        uint16_t rom_bank) {
  uint8_t data = 0;
  switch (addr & 0xC000) {
  case 0x0000:
    data = memory[addr & 0x3FFFU];
    break;
  // case 0x4000:
  //   data = g_loadedRomBanks[rom_bank][addr & 0x3FFFU];
  //   break;
  default:
    break;
  }

  switch (data) {
  case 0xe0: // LDH
    if (_speedChangeState == SPEED_CHANGE_IDLE) {
      _speedChangeState = SPEED_CHANGE_LDH;
    }
    break;
  case 0x4d: // LDH KEY1,A
    if (_speedChangeState == SPEED_CHANGE_LDH) {
      _speedChangeState = SPEED_CHANGE_KEY1;
    }
    break;
  case 0x10: // STOP
    if (_speedChangeState == SPEED_CHANGE_KEY1) {
      // speed change is triggered
      _speedChangeState = SPEED_CHANGE_IDLE;
      loadDoubleSpeedPio();
      // ws2812b_setRgb(0, 0, 0x10);
    }
    break;
  default:
    if (_speedChangeState == SPEED_CHANGE_LDH) {
      _speedChangeState = SPEED_CHANGE_IDLE;
    }
    break;
  }
}