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

#include <hardware/address_mapped.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/regs/ssi.h>
#include <hardware/structs/ssi.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/bootrom.h>
#include <pico/platform.h>
#include <pico/stdio.h>
#include <pico/stdio_uart.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types.h>

#include <lfs.h>
#include <lfs_pico_hal.h>

#include <git_commit.h>

#include "gb-bootloader/gbbootloader.h"

#include "GbDma.h"
#include "GlobalDefines.h"
#include "RomStorage.h"
#include "webusb.h"

#include "gameboy_bus.pio.h"

const volatile uint8_t *volatile ram_base = NULL;
const volatile uint8_t *volatile rom_low_base = NULL;
const volatile uint8_t *volatile rom_high_base = NULL;
volatile uint32_t rom_high_base_flash_direct = 0;

uint8_t memory[GB_ROM_BANK_SIZE * 2] __attribute__((aligned(GB_ROM_BANK_SIZE)));
uint8_t __attribute__((section(".noinit.")))
ram_memory[(GB_MAX_RAM_BANKS + 1) * GB_RAM_BANK_SIZE]
    __attribute__((aligned(GB_RAM_BANK_SIZE)));

struct ShortRomInfo g_shortRomInfos[MAX_ALLOWED_ROMS];
uint8_t g_numRoms = 0;
const uint8_t *g_loadedRomBanks[MAX_BANKS_PER_ROM];
uint32_t g_loadedDirectAccessRomBanks[MAX_BANKS_PER_ROM];

uint32_t __attribute__((section(".noinit."))) _noInitTest;
uint32_t __attribute__((section(".noinit."))) _lastRunningGame;

static lfs_t _lfs = {};
static uint8_t _lfsFileBuffer[LFS_CACHE_SIZE];

static uint _offset_main;
static uint16_t _mainStateMachineCopy
    [sizeof(gameboy_bus_double_speed_program_instructions) / sizeof(uint16_t)];

uint8_t runGbBootloader();
void loadGame(uint8_t game);
void runNoMbcGame(uint8_t game);
void runMbc1Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);
void runMbc3Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);
void runMbc5Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);

int main() {
  // bi_decl(bi_program_description("Sample binary"));
  // bi_decl(bi_1pin_with_name(LED_PIN, "on-board PIN"));

  {
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(266000, true);
    sleep_ms(2);
  }

  stdio_uart_init_full(uart0, 1000000, 28, -1);

  printf("Hello RP2040 Croco Cartridge %s-%s(%s)\n", git_Branch(),
         git_Describe(), git_AnyUncommittedChanges() ? "dirty" : "");

  printf("SSI->BAUDR: %x\n", *((uint32_t *)(XIP_SSI_BASE + SSI_BAUDR_OFFSET)));

  gpio_init(PIN_GB_RESET);
  gpio_set_dir(PIN_GB_RESET, true);
  gpio_put(PIN_GB_RESET, 1);

  // pio_gpio_init(pio1, 28);
  // gpio_init(PIN_UART_TX);
  // gpio_set_dir(PIN_UART_TX, true);

  for (uint pin = PIN_AD_BASE - 1; pin < PIN_AD_BASE + 25; pin++) {
    // gpio_init(pin);
    // gpio_set_dir(pin, false);
    // gpio_set_function(pin, GPIO_FUNC_PIO1);

    /* Disable schmitt triggers on GB Bus. The bus transceivers
     * already have schmitt triggers. */
    gpio_set_input_hysteresis_enabled(pin, false);
    /* Use fast slew rate for GB Bus. */
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
  }

  for (uint pin = PIN_DATA_BASE; pin < PIN_DATA_BASE + 8; pin++) {
    /* Initialise PIO0 pins. */
    pio_gpio_init(pio0, pin);
  }

  // Load the gameboy_bus programs into it's respective PIOs
  _offset_main = pio_add_program(pio1, &gameboy_bus_program);
  uint offset_detect_a14 =
      pio_add_program(pio1, &gameboy_bus_detect_a14_program);
  uint offset_ram = pio_add_program(pio1, &gameboy_bus_ram_program);

  uint offset_write_data =
      pio_add_program(pio0, &gameboy_bus_write_to_data_program);
  uint offset_rom_low = pio_add_program(pio0, &gameboy_bus_rom_low_program);
  uint offset_rom_high = pio_add_program(pio0, &gameboy_bus_rom_high_program);
  uint offset_a14_irqs =
      pio_add_program(pio0, &gameboy_bus_detect_a15_low_a14_irqs_program);

  // Initialize all gameboy state machines
  gameboy_bus_program_init(pio1, SMC_GB_MAIN, _offset_main);
  gameboy_bus_detect_a14_program_init(pio1, SMC_GB_DETECT_A14,
                                      offset_detect_a14);
  gameboy_bus_ram_read_program_init(pio1, SMC_GB_RAM_READ, offset_ram);
  gameboy_bus_ram_write_program_init(pio1, SMC_GB_RAM_WRITE, offset_ram);

  gameboy_bus_detect_a15_low_a14_irqs_init(pio0, SMC_GB_A15LOW_A14IRQS,
                                           offset_a14_irqs);
  gameboy_bus_rom_low_program_init(pio0, SMC_GB_ROM_LOW, offset_rom_low);
  gameboy_bus_rom_high_program_init(pio0, SMC_GB_ROM_HIGH, offset_rom_high);
  gameboy_bus_write_to_data_program_init(pio0, SMC_GB_WRITE_DATA,
                                         offset_write_data);

  // copy the gameboy main double speed statemachine to RAM
  for (size_t i = 0; i < (sizeof(_mainStateMachineCopy) / sizeof(uint16_t));
       i++) {
    _mainStateMachineCopy[i] = gameboy_bus_double_speed_program_instructions[i];
  }

  // initialze base pointers with some default values before initialzizing the
  // DMAs
  ram_base = &ram_memory[GB_MAX_RAM_BANKS * GB_RAM_BANK_SIZE];
  rom_low_base = memory;
  rom_high_base = &memory[GB_ROM_BANK_SIZE];

  GbDma_Setup();
  GbDma_SetupHigherDmaDirectSsi();

  // enable all gameboy state machines
  pio_sm_set_enabled(pio1, SMC_GB_MAIN, true);
  pio_sm_set_enabled(pio1, SMC_GB_DETECT_A14, true);
  pio_sm_set_enabled(pio1, SMC_GB_RAM_READ, true);
  pio_sm_set_enabled(pio1, SMC_GB_RAM_WRITE, true);

  pio_sm_set_enabled(pio0, SMC_GB_ROM_LOW, true);
  pio_sm_set_enabled(pio0, SMC_GB_WRITE_DATA, true);
  pio_sm_set_enabled(pio0, SMC_GB_A15LOW_A14IRQS, true);

  if (_noInitTest != 0xcafeaffe) {
    _noInitTest = 0xcafeaffe;
    _lastRunningGame = 0xFF;
    printf("NoInit initialized\n");
  }

  int lfs_err = lfs_mount(&_lfs, &pico_cfg);
  if (lfs_err != LFS_ERR_OK) {
    printf("Error mounting FS %d\n", lfs_err);
    printf("Formatting...\n");

    lfs_format(&_lfs, &pico_cfg);
    lfs_err = lfs_mount(&_lfs, &pico_cfg);
  }

  if (lfs_err != LFS_ERR_OK) {
    printf("Final error mounting FS %d\n", lfs_err);
  } else {
    printf("mounted\n");
  }

  lfs_err = lfs_mkdir(&_lfs, "/saves");
  if ((lfs_err != LFS_ERR_OK) && (lfs_err != LFS_ERR_EXIST)) {
    printf("Error creating saves directory %d\n", lfs_err);
  }

  RomStorage_init(&_lfs);

  if (_lastRunningGame < g_numRoms) {
    printf("Game %d was running before reset\n", _lastRunningGame);

    if (g_shortRomInfos[_lastRunningGame].numRamBanks > 0) {
      lfs_file_t file;
      struct lfs_file_config fileconfig = {.buffer = _lfsFileBuffer};
      char filenamebuffer[40] = "saves/";
      strcpy(&filenamebuffer[strlen(filenamebuffer)],
             (const char *)&(g_shortRomInfos[_lastRunningGame].name));
      printf("Saving game RAM to file %s\n", filenamebuffer);

      lfs_err = lfs_file_opencfg(&_lfs, &file, filenamebuffer,
                                 LFS_O_WRONLY | LFS_O_CREAT, &fileconfig);

      if (lfs_err != LFS_ERR_OK) {
        printf("Error opening file %d\n", lfs_err);
      }

      lfs_err = lfs_file_write(&_lfs, &file, ram_memory,
                               g_shortRomInfos[_lastRunningGame].numRamBanks *
                                   GB_RAM_BANK_SIZE);
      printf("wrote %d bytes\n", lfs_err);

      lfs_file_close(&_lfs, &file);

      _lastRunningGame = 0xFF;

      // pio_sm_put_blocking(pio0, SMC_WS2812, 0x150000 << 8);
    }
  }

  uint8_t game = runGbBootloader();
  // uint8_t game = 5; // 11: Zelda DX, 5: Repugnant, 8: Tetris, 9: Yoshi

  (void)save_and_disable_interrupts();

  loadGame(game);

  // should only be reached in case there was an error loading the game
  while (1) {
    tight_loop_contents();
  }
}

struct __attribute__((packed)) SharedGameboyData {
  uint32_t git_sha1;
  uint8_t git_status;
  uint8_t number_of_roms;
  uint8_t rom_names;
};

uint8_t __no_inline_not_in_flash_func(runGbBootloader)() {
  uint8_t selectedGame = 0xFF;
  // use spare RAM bank to not overwrite potential save
  uint8_t *ram = &ram_memory[GB_MAX_RAM_BANKS * GB_RAM_BANK_SIZE];
  struct SharedGameboyData *shared_data = (void *)ram;

  memcpy(memory, GB_BOOTLOADER, GB_BOOTLOADER_SIZE);
  memset(ram, 0, GB_RAM_BANK_SIZE);

  shared_data->git_sha1 = strtoul(git_Describe(), NULL, 16);
  shared_data->git_status = git_AnyUncommittedChanges();

  // initialize RAM with information about roms
  uint8_t *pRomNames = &shared_data->rom_names;
  for (size_t i = 0; i < g_numRoms; i++) {
    memcpy(&pRomNames[i * 16], &(g_shortRomInfos[i].name), 16);
  }
  shared_data->number_of_roms = g_numRoms;
  ram[0x1000] = 0xFF;

  printf("Found %d games\n", g_numRoms);

  usb_start();

  ram_base = ram;

  gpio_put(PIN_GB_RESET, 0); // let the gameboy start (deassert reset line)

  while (selectedGame == 0xFF) {
    if (!pio_sm_is_rx_fifo_empty(pio1, SMC_GB_MAIN)) {
      uint32_t rdAndAddr = *((uint32_t *)(&pio1->rxf[SMC_GB_MAIN]));
      bool write = rdAndAddr & 0x00000001;
      uint16_t addr = (rdAndAddr >> 1) & 0xFFFF;

      if (write) {
        uint8_t data = pio_sm_get_blocking(pio1, SMC_GB_MAIN) & 0xFF;

        if (addr == 0xB000) {
          printf("Selected Game: %d\n", data);
          selectedGame = data;
        }
        if (addr == 0xB001) {
          switch (data) {
          case 1:
            // pio_sm_put_blocking(pio0, SMC_WS2812, 0x001500 << 8);
            break;
          case 2:
            // pio_sm_put_blocking(pio0, SMC_WS2812, 0x150000 << 8);
            break;
          case 3:
            // pio_sm_put_blocking(pio0, SMC_WS2812, 0x000015 << 8);
            break;
          default:
            // pio_sm_put_blocking(pio0, SMC_WS2812, 0);
            break;
          }
        }

        if (addr == 0xB010) {
          sleep_ms(50);
          reset_usb_boot(0, 0);
        }
      }
    }

    usb_run();
  }

  usb_shutdown();

  // hold the gameboy in reset until we have loaded the game
  gpio_put(PIN_GB_RESET, 1);

  return selectedGame;
}

void loadGame(uint8_t game) {
  uint8_t mbc = 0xFF;
  uint16_t num_rom_banks = 0;
  uint8_t num_ram_banks = 0;

  lfs_file_t file;
  struct lfs_file_config fileconfig = {.buffer = _lfsFileBuffer};
  char filenamebuffer[40] = "saves/";

  const uint8_t *gameptr = g_shortRomInfos[game].firstBank;

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

  num_rom_banks = 1 << (gameptr[0x0148] + 1);
  num_ram_banks = g_shortRomInfos[game].numRamBanks;

  printf("MBC:       %d\n", mbc);
  printf("name:      %s\n", (const char *)&gameptr[0x134]);
  printf("rom banks: %d\n", num_rom_banks);
  printf("ram banks: %d\n", num_ram_banks);

  if (num_ram_banks > 0) {
    strcpy(&filenamebuffer[strlen(filenamebuffer)], g_shortRomInfos[game].name);

    int lfs_err = lfs_file_opencfg(&_lfs, &file, filenamebuffer, LFS_O_RDONLY,
                                   &fileconfig);

    if (lfs_err == LFS_ERR_OK) {
      printf("found save at %s\n", filenamebuffer);

      lfs_err = lfs_file_read(&_lfs, &file, ram_memory,
                              num_ram_banks * GB_RAM_BANK_SIZE);
      printf("read %d bytes\n", lfs_err);

      if (lfs_err >= 0) {
        lfs_file_close(&_lfs, &file);
      }
    }
  }

  _lastRunningGame = game;

  if (num_ram_banks > GB_MAX_RAM_BANKS) {
    printf("Game needs too much RAM\n");
    return;
  }

  if (NULL == RomStorage_LoadRom(game)) {
    printf("Error reading ROM\n");
    return;
  }

  // pio_sm_put_blocking(pio0, SMC_WS2812, 0);

  switch (mbc) {
  case 0x00:
    runNoMbcGame(game);
    break;
  case 0x01:
    runMbc1Game(game, num_rom_banks, num_ram_banks);
    break;
  case 0x03:
    runMbc3Game(game, num_rom_banks, num_ram_banks);
    break;
  case 0x05:
    runMbc5Game(game, num_rom_banks, num_ram_banks);
    break;
  default:
    printf("Unsupported MBC!\n");
    break;
  }
}

void __no_inline_not_in_flash_func(loadDoubleSpeedPio)() {
  pio_sm_set_enabled(pio1, SMC_GB_MAIN, false);

  // manually replace statemachine instruction in order to not use flash
  for (size_t i = 0; i < (sizeof(_mainStateMachineCopy) / sizeof(uint16_t));
       i++) {
    uint16_t instr = _mainStateMachineCopy[i];
    pio1->instr_mem[_offset_main + i] =
        pio_instr_bits_jmp != _pio_major_instr_bits(instr)
            ? instr
            : instr + _offset_main;
  }

  pio_sm_set_enabled(pio1, SMC_GB_MAIN, true);
}

void __no_inline_not_in_flash_func(setSsi8bit)() {
  __compiler_memory_barrier();

  ssi_hw->ssienr = 0; // disable SSI so it can be configured
  ssi_hw->ctrlr0 =
      (SSI_CTRLR0_SPI_FRF_VALUE_QUAD /* Quad I/O mode */
       << SSI_CTRLR0_SPI_FRF_LSB) |
      (7 << SSI_CTRLR0_DFS_32_LSB) |     /* 8 data bits */
      (SSI_CTRLR0_TMOD_VALUE_EEPROM_READ /* Send INST/ADDR, Receive Data */
       << SSI_CTRLR0_TMOD_LSB);

  ssi_hw->dmacr = SSI_DMACR_TDMAE_BITS | SSI_DMACR_RDMAE_BITS;
  ssi_hw->ssienr = 1; // enable SSI again
}

void __attribute__((__noreturn__))
__assert_fail(const char *expr, const char *file, unsigned int line,
              const char *function) {
  printf("assert");
  while (1)
    ;
}
