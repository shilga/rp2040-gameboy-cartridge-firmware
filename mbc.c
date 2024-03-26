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

#include "mbc.h"
#include "GbDma.h"
#include "GbRtc.h"
#include "GlobalDefines.h"
#include "ws2812b_spi.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/regs/clocks.h>
#include <hardware/structs/scb.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <pico/platform.h>

#include "gb-vblankhook/gbSaveGameVBlankHook.h"

static bool _ramDirty = false;
static uint16_t _numRomBanks = 0;
static uint8_t _numRamBanks = 0;
static bool _hasRtc = false;
static uint8_t _vBlankMode = 0;
static uint8_t *_bankWithVBlankOverride = &memory[2 * GB_ROM_BANK_SIZE];

void runNoMbcGame();
void runMbc1Game();
void runMbc3Game();
void runMbc5Game();

void detect_speed_change(uint16_t addr, bool isSpeedSwitchBank);
void process_vblank_hook(uint16_t addr);
void initialize_vblank_hook();
void storeCurrentlyRunningSaveGame();

void loadGame(uint8_t mode) {
  uint8_t mbc = 0xFF;

  const uint8_t *gameptr = g_loadedRomInfo.firstBank;

  printf("Loading selected game info at %p:\n", gameptr);

  switch (gameptr[0x0147]) {
  case 0x00:
    mbc = 0;
    break;
  case 0x01:
  case 0x02:
  case 0x03:
    mbc = 1;
    break;
  case 0x05:
  case 0x06:
  case 0x07:
    mbc = 2;
    break;
  case 0x0F:
  case 0x10:
    _hasRtc = true;
  case 0x11:
  case 0x12:
  case 0x13:
    mbc = 3;
    break;
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x1E:
    mbc = 5;
    break;
  }

  _numRomBanks = 1 << (gameptr[0x0148] + 1);
  _vBlankMode = mode;
  _numRamBanks = g_loadedRomInfo.numRamBanks;

  printf("MBC:       %d\n", mbc);
  printf("name:      %s\n", (const char *)&gameptr[0x134]);
  printf("rom banks: %d\n", _numRomBanks);
  printf("ram banks: %d\n", g_loadedRomInfo.numRamBanks);

  if (_hasRtc) {
    restoreRtcFromFile(&g_loadedRomInfo);
  }

  if (g_loadedRomInfo.numRamBanks > 0) {
    restoreSaveRamFromFile(&g_loadedRomInfo);
  } else {
    _vBlankMode = 0;
  }

  if (_vBlankMode == 0xFF) {
    _vBlankMode = 0;
  }

  if (g_loadedRomInfo.numRamBanks > GB_MAX_RAM_BANKS) {
    printf("Game needs too much RAM\n");
    return;
  }

  ws2812b_setRgb(0, 0, 0);

  ram_base = ram_memory;

  memcpy(memory, g_loadedRomBanks[0], GB_ROM_BANK_SIZE);
  if (_vBlankMode) {
    initialize_vblank_hook();
  }

  switch (mbc) {
  case 0x00:
    runNoMbcGame();
    break;
  case 0x01:
    runMbc1Game();
    break;
  case 0x03:
    runMbc3Game();
    break;
  case 0x05:
    runMbc5Game();
    break;
  default:
    printf("Unsupported MBC!\n");
    break;
  }
}

void __no_inline_not_in_flash_func(runNoMbcGame)() {
  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

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

  while (1) {
    tight_loop_contents();
  }
}

void __no_inline_not_in_flash_func(runMbc1Game)() {
  uint8_t rom_bank = 1;
  uint8_t rom_bank_new = 1;
  uint8_t rom_bank_high = 0;
  uint8_t rom_bank_low = 1;
  uint8_t ram_bank = 0;
  uint8_t ram_bank_new = 0;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  bool mode_select = 0;
  uint16_t rom_banks_mask = _numRomBanks - 1;

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
          if (!_ramDirty && ram_enabled) {
            ws2812b_setRgb(0x10, 0, 0); // switch on LED to red
            _ramDirty = true;
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
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
          if (ram_enabled) {
            GbDma_EnableSaveRam();
          } else {
            GbDma_DisableSaveRam();
          }
        }

        if (ram_bank != ram_bank_new) {
          ram_bank = ram_bank_new;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      } else { // read
        if (_vBlankMode) {
          process_vblank_hook(addr);
        }
      }
    }
  }
}

void __no_inline_not_in_flash_func(runMbc3Game)() {
  uint8_t rom_bank = 1;
  uint8_t rom_bank_new = 1;
  uint8_t ram_bank = 0;
  uint8_t ram_bank_new = 0;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  uint16_t rom_banks_mask = _numRomBanks - 1;
  uint16_t speedSwitchBank = 1U;
  uint64_t now, lastSecond = time_us_64();
  bool rtcLatch = false;

  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  if (g_loadedRomInfo.speedSwitchBank <= _numRomBanks) {
    speedSwitchBank = g_loadedRomInfo.speedSwitchBank;
  }

  memcpy(&memory[GB_ROM_BANK_SIZE], g_loadedRomBanks[speedSwitchBank],
         GB_ROM_BANK_SIZE);

  GbDma_DisableSaveRam(); // todo: should be disabled in general

  printf("MBC3 game loaded\n");
  printf("has RTC: %d\n", _hasRtc);
  printf("initial bank %d a %p\n", rom_bank, g_loadedRomBanks[1]);
  printf("speedSwitchBank %d\n", speedSwitchBank);

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
          ram_bank_new = data;
          break;

        case 0x6000:
          if (data) {
            if (!rtcLatch) {
              rtcLatch = true;
              memcpy((void *)&g_rtcLatched, (void *)&g_rtcReal,
                     sizeof(struct GbRtc));
            }
          } else {
            rtcLatch = false;
          }
          break;
        case 0xA000: // write to RAM
          if (ram_enabled) {
            if (ram_bank & 0x08) {
              GbRtc_WriteRegister(data);
            } else if (!_ramDirty) {
              ws2812b_setRgb(0x10, 0, 0); // switch on LED to red
              _ramDirty = true;
            }
          }
          break;
        default:

          break;
        }

        rom_bank_new = rom_bank_new & rom_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
          if (ram_enabled) {
            if (ram_bank & 0x08) {
              GbDma_EnableRtc();
            } else {
              GbDma_EnableSaveRam();
            }
          } else {
            GbDma_DisableSaveRam();
          }
        }

        if (ram_bank != ram_bank_new) {
          ram_bank = ram_bank_new;
          if (ram_bank & 0x08) {
            GbRtc_ActivateRegister(ram_bank & 0x07);
            if (ram_enabled) {
              GbDma_EnableRtc();
            }
          } else {
            ram_base = &ram_memory[(ram_bank & 0x03) * GB_RAM_BANK_SIZE];
            if (ram_enabled) {
              GbDma_EnableSaveRam();
            }
          }
        }
      } else { // read
        if (_vBlankMode) {
          process_vblank_hook(addr);
        }
        detect_speed_change(addr, rom_bank == speedSwitchBank);
      }
    }

    GbRtc_PerformRtcTick();
  } // endless loop
}

void __no_inline_not_in_flash_func(runMbc5Game)() {
  uint16_t rom_bank = 1;
  uint16_t rom_bank_new = 1;
  uint8_t ram_bank = 0;
  uint8_t ram_bank_new = 0;
  bool ram_enabled = 0;
  bool new_ram_enabled = 0;
  const uint16_t rom_banks_mask = _numRomBanks - 1;
  const uint8_t ram_banks_mask = _numRamBanks - 1;
  uint16_t speedSwitchBank = 1U;

  if (g_loadedRomInfo.speedSwitchBank <= _numRomBanks) {
    speedSwitchBank = g_loadedRomInfo.speedSwitchBank;
  }

  memcpy(&memory[GB_ROM_BANK_SIZE], g_loadedRomBanks[speedSwitchBank],
         GB_ROM_BANK_SIZE);

  rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[1];

  printf("MBC5 game loaded\n");
  printf("initial bank %d a %p\n", rom_bank, g_loadedRomBanks[1]);
  printf("speedSwitchBank %d\n", speedSwitchBank);

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
          if (!_ramDirty && ram_enabled) {
            ws2812b_setRgb(0x10, 0, 0); // switch on LED to red
            _ramDirty = true;
          }
          break;
        default:

          break;
        }

        rom_bank_new = rom_bank_new & rom_banks_mask;
        ram_bank_new = ram_bank_new & ram_banks_mask;

        if (rom_bank != rom_bank_new) {
          rom_bank = rom_bank_new;
          rom_high_base_flash_direct = g_loadedDirectAccessRomBanks[rom_bank];
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
          if (ram_enabled) {
            GbDma_EnableSaveRam();
          } else {
            GbDma_DisableSaveRam();
          }
        }

        if (ram_bank != ram_bank_new) {
          ram_bank = ram_bank_new;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      } else { // read
        if (_vBlankMode) {
          process_vblank_hook(addr);
        }
        detect_speed_change(addr, rom_bank == speedSwitchBank);
      }
    }
  }
}

enum eDETECT_SPEED_CHANGE_STATE {
  SPEED_CHANGE_IDLE,
  SPEED_CHANGE_LDH,
  SPEED_CHANGE_KEY1,
} _speedChangeState = SPEED_CHANGE_IDLE;

void __no_inline_not_in_flash_func(detect_speed_change)(
    uint16_t addr, bool isSpeedSwitchBank) {
  uint8_t data = 0;
  switch (addr & 0xC000) {
  case 0x0000:
    data = memory[addr & 0x3FFFU];
    break;
  case 0x4000:
    if (isSpeedSwitchBank) {
      data = memory[(addr & 0x3FFFU) + GB_ROM_BANK_SIZE];
    }
    break;
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
    }
    break;
  default:
    if (_speedChangeState == SPEED_CHANGE_LDH) {
      _speedChangeState = SPEED_CHANGE_IDLE;
    }
    break;
  }
}

enum eVBLANK_HOOK_STATE {
  VBLANK_HOOK_IDLE = 0,
  VBLANK_HOOK_INTERRUPT,
  VBLANK_HOOK_PROCESSING,
  VBLANK_HOOK_SAVE_TRIGGERED,
  VBLANK_HOOK_RETURNED
} volatile _vblankHookState = VBLANK_HOOK_IDLE;

void __no_inline_not_in_flash_func(process_vblank_hook)(uint16_t addr) {
  if (_vblankHookState == VBLANK_HOOK_IDLE) {
    if (addr == 0x40) {
      rom_low_base = memory_vblank_hook_bank;
      _vblankHookState = VBLANK_HOOK_INTERRUPT;
    }
  } else if (_vblankHookState == VBLANK_HOOK_INTERRUPT) {
    if (addr == 0x50) {
      rom_low_base = memory_vblank_hook_bank2;
      _vblankHookState = VBLANK_HOOK_PROCESSING;
    }
  } else if (_vblankHookState == VBLANK_HOOK_PROCESSING) {
    if (addr == 0x100) {
      _vblankHookState = VBLANK_HOOK_SAVE_TRIGGERED;

      storeCurrentlyRunningSaveGame();
      memory_vblank_hook_bank2[0x1FF] = 0xaa;
    } else if (addr == 0x40) {
      rom_low_base = memory;
      _vblankHookState = VBLANK_HOOK_RETURNED;
    }
  } else if (_vblankHookState == VBLANK_HOOK_RETURNED) {
    if ((addr & 0xFFF8) != 0x40) {
      _vblankHookState = VBLANK_HOOK_IDLE;
      rom_low_base = _bankWithVBlankOverride;
      memory_vblank_hook_bank2[0x1FF] = 0;
    }
  } else if (_vblankHookState == VBLANK_HOOK_SAVE_TRIGGERED) {
    if (addr == 0x40) {
      rom_low_base = memory;
      _vblankHookState = VBLANK_HOOK_RETURNED;
    }
  } else {
  }
}

void initialize_vblank_hook() {
  memcpy(memory_vblank_hook_bank, GB_VBLANK_HOOK, GB_VBLANK_HOOK_SIZE);

  if (_vBlankMode == 2) {
    memory_vblank_hook_bank[0x84] = 0x5F; // replace with opcode for bit 3, a
  }

  memcpy(memory_vblank_hook_bank2, memory_vblank_hook_bank,
         GB_VBLANK_HOOK_SIZE);
  memcpy(_bankWithVBlankOverride, memory, GB_ROM_BANK_SIZE);
  memcpy(&_bankWithVBlankOverride[0x40], &memory_vblank_hook_bank[0x40], 8);
  memcpy(&memory_vblank_hook_bank2[0x40], &memory[0x40], 8);
  memory_vblank_hook_bank2[0x1FF] = 0;

  rom_low_base = _bankWithVBlankOverride;
}

void __no_inline_not_in_flash_func(storeCurrentlyRunningSaveGame)() {

  // disable master SM while store is happening to prevent FIFO overflow.
  pio_set_sm_mask_enabled(pio1, (1 << SMC_GB_MAIN), false);

  setSsi32bit();
  __compiler_memory_barrier();

  storeSaveRamToFile(&g_loadedRomInfo);
  if(_hasRtc)
  {
    storeRtcToFile(&g_loadedRomInfo);
  }

  ws2812b_setRgb(0, 0x10, 0);

  setSsi8bit();
  __compiler_memory_barrier();

  _ramDirty = false;

  pio_set_sm_mask_enabled(pio1, (1 << SMC_GB_MAIN), true);
}
