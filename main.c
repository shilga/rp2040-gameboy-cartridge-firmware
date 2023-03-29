#include <hardware/address_mapped.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/regs/ssi.h>
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
#include <string.h>
#include <sys/_types.h>

#include <lfs.h>
#include <lfs_pico_hal.h>

#include "roms/20y.h"
#include "roms/Alleyway.h"
#include "roms/DrMario.h"
#include "roms/GbBootloader.h"
#include "roms/Kirby.h"
#include "roms/Othello.h"
// #include "roms/PokemonBlue.h"
#include "roms/SuperMario.h"
#include "roms/SuperMario2.h"
#include "roms/Tetris.h"
#include "roms/YoshisCookie.h"
#include "roms/Zelda.h"
#include "roms/merken.h"
#include "roms/ph-t_02.h"
#include "roms/thebouncingball.h"

#include "gameboy_bus.pio.h"
#include "ws2812.pio.h"

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
#define GB_MAX_RAM_BANKS 8 /* 64K of RAM enough for MBC3*/

const volatile uint8_t *ram_base = NULL;
const volatile uint8_t *rom_low_base = NULL;
const volatile uint8_t *rom_high_base = NULL;

uint8_t memory[0x8000] __attribute__((aligned(GB_ROM_BANK_SIZE)));
uint8_t __attribute__((section(".noinit.")))
ram_memory[(GB_MAX_RAM_BANKS + 1) * GB_RAM_BANK_SIZE]
    __attribute__((aligned(GB_RAM_BANK_SIZE)));

static const uint8_t *_games[] = {
    (const uint8_t *)((size_t)Tetris + 0x03000000),
    (const uint8_t *)((size_t)Dr__Mario__World__gb + 0x03000000),
    (const uint8_t *)((size_t)Othello__Europe__gb + 0x03000000),
    (const uint8_t *)((size_t)pht_t_02_gb + 0x03000000),
    (const uint8_t *)((size_t)__20y_gb + 0x03000000),
    (const uint8_t *)((size_t)merken_gb + 0x03000000),
    (const uint8_t *)((size_t)Alleyway__World__gb + 0x03000000),
    (const uint8_t *)((size_t)Kirby_s_Dream_Land__USA__Europe__gb + 0x03000000),
    (const uint8_t *)((size_t)Yoshi_s_Cookie__USA__Europe__gb + 0x03000000),
    (const uint8_t *)((size_t)THEBOUNCINGBALL_GB + 0x03000000),
    (const uint8_t *)((size_t)Super_Mario_Land__World___Rev_A__gb + 0x03000000),
    (const uint8_t
         *)((size_t)Super_Mario_Land_2___6_Golden_Coins__UE___V1_2______gb +
            0x03000000),
    (const uint8_t
         *)((size_t)Legend_of_Zelda__The___Link_s_Awakening__USA__Europe__gb +
            0x03000000)};

size_t num_games = sizeof(_games) / sizeof(uint8_t *);

uint32_t __attribute__((section(".noinit."))) _noInitTest;
uint32_t __attribute__((section(".noinit."))) _lastRunningGame;

lfs_t _lfs = {};
uint8_t _lfsFileBuffer[LFS_CACHE_SIZE];

uint8_t readRamBankCount(const uint8_t *gameptr);

uint8_t runGbBootloader();
void loadGame(uint8_t game);
void runNoMbcGame(uint8_t game);
void runMbc1Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);

static void setup_read_dma_method2(PIO pio, unsigned sm, PIO pio_write_data,
                                   unsigned sm_write_data,
                                   const volatile void *read_base_addr) {
  unsigned dma1, dma2, dma3;
  dma_channel_config cfg;

  dma1 = dma_claim_unused_channel(true);
  dma2 = dma_claim_unused_channel(true);
  dma3 = dma_claim_unused_channel(true);

  // Set up DMA2 first (it's not triggered until DMA1 does so)
  cfg = dma_channel_get_default_config(dma2);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  // dreq defaults to DREQ_FORCE
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_trans_count(dma2, 1, false);
  dma_channel_set_write_addr(dma2, &(pio_write_data->txf[sm_write_data]),
                             false);
  channel_config_set_chain_to(&cfg, dma1);
  dma_channel_set_config(dma2, &cfg, false);

  // Set up DMA1 and trigger it
  cfg = dma_channel_get_default_config(dma1);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
  // transfer size defaults to 32
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_trans_count(dma1, 1, false);
  dma_channel_set_read_addr(dma1, &(pio->rxf[sm]), false);
  dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].read_addr), false);
  channel_config_set_chain_to(&cfg, dma3);
  dma_channel_set_config(dma1, &cfg, true);

  cfg = dma_channel_get_default_config(dma3);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  // dreq defaults to DREQ_FORCE
  // transfer size defaults to 32
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_trans_count(dma3, 1, false);
  dma_channel_set_read_addr(dma3, read_base_addr, false);
  dma_channel_set_write_addr(
      dma3, hw_set_alias_untyped(&(dma_hw->ch[dma2].al3_read_addr_trig)),
      false);
  // channel_config_set_chain_to(&cfg, dma2);
  dma_channel_set_config(dma3, &cfg, false);
}

static void setup_write_dma_method2(PIO pio, unsigned sm,
                                    const volatile void *write_base_addr) {
  unsigned dma1, dma2, dma3;
  dma_channel_config cfg;

  dma1 = dma_claim_unused_channel(true);
  dma2 = dma_claim_unused_channel(true);
  dma3 = dma_claim_unused_channel(true);

  // Set up DMA2 first (it's not triggered until DMA1 does so)
  cfg = dma_channel_get_default_config(dma2);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_chain_to(&cfg, dma1);
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_read_addr(dma2, &(pio->rxf[sm]), false);
  dma_channel_set_trans_count(dma2, 1, false);
  dma_channel_set_config(dma2, &cfg, false);

  // Set up DMA1 and trigger it
  cfg = dma_channel_get_default_config(dma1);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
  // transfer size defaults to 32
  channel_config_set_chain_to(&cfg, dma3);
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_trans_count(dma1, 1, false);
  dma_channel_set_read_addr(dma1, &(pio->rxf[sm]), false);
  dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].write_addr), false);
  dma_channel_set_config(dma1, &cfg, true);

  cfg = dma_channel_get_default_config(dma3);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  // dreq defaults to DREQ_FORCE
  // transfer size defaults to 32
  channel_config_set_high_priority(&cfg, true);
  dma_channel_set_trans_count(dma3, 1, false);
  dma_channel_set_read_addr(dma3, write_base_addr, false);
  dma_channel_set_write_addr(
      dma3, hw_set_alias_untyped(&(dma_hw->ch[dma2].al2_write_addr_trig)),
      false);
  // channel_config_set_chain_to(&cfg, dma2);
  dma_channel_set_config(dma3, &cfg, false);
}

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

  printf("Hello World!\n");

  printf("SSI->BAUDR: %x\n", *((uint32_t *)(XIP_SSI_BASE + SSI_BAUDR_OFFSET)));

  gpio_init(PIN_GB_RESET);
  gpio_set_dir(PIN_GB_RESET, true);
  gpio_put(PIN_GB_RESET, 1);

  // gpio_init(28);
  // gpio_set_dir(28, true);
  // pio_gpio_init(pio0, 28);

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
  uint offset_main = pio_add_program(pio1, &gameboy_bus_program);
  uint offset_detect_a14 =
      pio_add_program(pio1, &gameboy_bus_detect_a14_program);
  uint offset_ram = pio_add_program(pio1, &gameboy_bus_ram_program);

  uint offset_rom = pio_add_program(pio0, &gameboy_bus_rom_program);

  // Initialize all gameboy state machines
  gameboy_bus_program_init(pio1, SMC_GB_MAIN, offset_main);
  gameboy_bus_detect_a14_program_init(pio1, SMC_GB_DETECT_A14,
                                      offset_detect_a14);
  gameboy_bus_ram_read_program_init(pio1, SMC_GB_RAM_READ, offset_ram);
  gameboy_bus_ram_write_program_init(pio1, SMC_GB_RAM_WRITE, offset_ram);

  gameboy_bus_rom_low_program_init(pio0, SMC_GB_ROM_LOW, offset_rom);
  gameboy_bus_rom_high_program_init(pio0, SMC_GB_ROM_HIGH, offset_rom);
  gameboy_bus_write_to_data_program_init(pio0, SMC_GB_WRITE_DATA, offset_rom);

  // initialze base pointers with some default values before initialzizing the
  // DMAs
  ram_base = &ram_memory[GB_MAX_RAM_BANKS * GB_RAM_BANK_SIZE];
  rom_low_base = memory;
  rom_high_base = &memory[GB_ROM_BANK_SIZE];

  setup_read_dma_method2(pio1, SMC_GB_RAM_READ, pio0, SMC_GB_WRITE_DATA,
                         &ram_base);
  setup_write_dma_method2(pio1, SMC_GB_RAM_WRITE, &ram_base);
  setup_read_dma_method2(pio0, SMC_GB_ROM_HIGH, pio0, SMC_GB_ROM_HIGH,
                         &rom_high_base);
  setup_read_dma_method2(pio0, SMC_GB_ROM_LOW, pio0, SMC_GB_ROM_LOW,
                         &rom_low_base);

  // enable all gameboy state machines
  pio_sm_set_enabled(pio1, SMC_GB_MAIN, true);
  pio_sm_set_enabled(pio1, SMC_GB_DETECT_A14, true);
  pio_sm_set_enabled(pio1, SMC_GB_RAM_READ, true);
  pio_sm_set_enabled(pio1, SMC_GB_RAM_WRITE, true);

  pio_sm_set_enabled(pio0, SMC_GB_ROM_LOW, true);
  pio_sm_set_enabled(pio0, SMC_GB_ROM_HIGH, true);
  pio_sm_set_enabled(pio0, SMC_GB_WRITE_DATA, true);

  // load and start the WS8212 StateMachine to control the RGB LED
  uint offset = pio_add_program(pio0, &ws2812_program);
  ws2812_program_init(pio0, SMC_WS2812, offset, WS2812_PIN, 800000, false);

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

  if (_lastRunningGame < num_games) {
    printf("Game %d was running before reset\n", _lastRunningGame);

    if (readRamBankCount(_games[_lastRunningGame]) > 0) {
      lfs_file_t file;
      struct lfs_file_config fileconfig = {.buffer = _lfsFileBuffer};
      char filenamebuffer[40] = "saves/";
      strcpy(&filenamebuffer[strlen(filenamebuffer)],
             (const char *)&(_games[_lastRunningGame][0x134]));
      printf("Saving game RAM to file %s\n", filenamebuffer);

      lfs_err = lfs_file_opencfg(&_lfs, &file, filenamebuffer,
                                 LFS_O_WRONLY | LFS_O_CREAT, &fileconfig);

      if (lfs_err != LFS_ERR_OK) {
        printf("Error opening file %d\n", lfs_err);
      }

      lfs_err = lfs_file_write(&_lfs, &file, ram_memory,
                               readRamBankCount(_games[_lastRunningGame]) *
                                   GB_RAM_BANK_SIZE);
      printf("wrote %d bytes\n", lfs_err);

      lfs_file_close(&_lfs, &file);

      pio_sm_put_blocking(pio0, SMC_WS2812, 0x150000 << 8);
    }
  }

  uint8_t game = runGbBootloader();

  loadGame(game);

  // should never be reached
  while (1) {
    tight_loop_contents();
  }
}

uint8_t __no_inline_not_in_flash_func(runGbBootloader)() {
  uint8_t selectedGame = 0xFF;
  // use spare RAM bank to not overwrite potential save
  uint8_t *ram = &ram_memory[GB_MAX_RAM_BANKS * GB_RAM_BANK_SIZE];

  memcpy(memory, bootloader_gb, bootloader_gb_len);

  memset(ram, 0, GB_RAM_BANK_SIZE);
  // initialize RAM with information about roms
  for (size_t i = 0; i < num_games; i++) {
    memcpy(&ram[(i * 16) + 1], &(_games[i][0x134]), 16);
  }
  ram[0] = (uint8_t)num_games;
  ram[0x1000] = 0xFF;

  printf("Found %d games\n", num_games);

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

        if (addr == 0xB010) {
          reset_usb_boot(0, 0);
        }
      }
    }
  }

  // hold the gameboy in reset until we have loaded the game
  gpio_put(PIN_GB_RESET, 1);

  return selectedGame;
}

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
          // printf("mode_select %d\n", mode_select);
          break;
        case 0xA000: // write to RAM
          pio0->txf[SMC_WS2812] = 0x00150000; // switch on LED to red
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
          // set_x(pio0, SMC_GB_ROM_HIGH,
          //       ((unsigned)gameptr + (GB_ROM_BANK_SIZE * rom_bank)) >> 14);
          rom_high_base = &gameptr[GB_ROM_BANK_SIZE * rom_bank];

          // printf("bank %d a %x\n", rom_bank, ((unsigned)gameptr +
          // (GB_ROM_BANK_SIZE * rom_bank)));
        }

        if (ram_enabled != new_ram_enabled) {
          ram_enabled = new_ram_enabled;
          // printf("ram_enabled %d\n", ram_enabled);
        }

        ram_bank_local = ram_enabled ? ram_bank_new : GB_MAX_RAM_BANKS;

        if (ram_bank != ram_bank_local) {
          ram_bank = ram_bank_local;
          ram_base = &ram_memory[ram_bank * GB_RAM_BANK_SIZE];
        }
      }
      // else
      // {
      //   if(addr == 0x0000 || addr == 0x4000)
      //   {
      //     printf("addr %x was loaded\n", addr);
      //   }
      // }
      // dma_uart_send(".", 1);
    }
  }
}

void loadGame(uint8_t game) {
  uint8_t mbc = 0xFF;
  uint16_t num_rom_banks = 0;
  uint8_t num_ram_banks = 0;

  lfs_file_t file;
  struct lfs_file_config fileconfig = {.buffer = _lfsFileBuffer};
  char filenamebuffer[40] = "saves/";

  const uint8_t *gameptr = _games[game];

  printf("Loading selected game info:\n");

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
  }

  num_rom_banks = 1 << (gameptr[0x0148] + 1);
  num_ram_banks = readRamBankCount(gameptr);

  printf("MBC:       %d\n", mbc);
  printf("name:      %s\n", (const char *)&gameptr[0x134]);
  printf("rom banks: %d\n", num_rom_banks);
  printf("ram banks: %d\n", num_ram_banks);

  if (num_ram_banks > 0) {
    strcpy(&filenamebuffer[strlen(filenamebuffer)],
           (const char *)&(gameptr[0x134]));

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

  pio_sm_put_blocking(pio0, SMC_WS2812, 0);

  switch (mbc) {
  case 0x00:
    runNoMbcGame(game);
    break;
  case 0x01:
    runMbc1Game(game, num_rom_banks, 0);
    break;

  default:
    printf("Unsupported MBC!\n");
    break;
  }
}

uint8_t readRamBankCount(const uint8_t *gameptr) {
  static const uint8_t LOOKUP[] = {0, 0, 1, 4, 16, 8};
  const uint8_t value = gameptr[0x0149];

  if (value <= sizeof(LOOKUP)) {
    return LOOKUP[value];
  }

  return 0;
}

void __attribute__((__noreturn__))
__assert_fail(const char *expr, const char *file, unsigned int line,
              const char *function) {
  printf("assert");
  while (1)
    ;
}
