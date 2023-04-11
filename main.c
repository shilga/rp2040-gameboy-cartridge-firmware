#include <hardware/address_mapped.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/regs/ssi.h>
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
#include <string.h>
#include <sys/_types.h>

#include <lfs.h>
#include <lfs_pico_hal.h>

#include "roms/GbBootloader.h"

#include "GlobalDefines.h"
#include "RomStorage.h"

#include "gameboy_bus.pio.h"
#include "ws2812.pio.h"

const volatile uint8_t *ram_base = NULL;
const volatile uint8_t *rom_low_base = NULL;
const volatile uint8_t *rom_high_base = NULL;

uint8_t memory[GB_ROM_BANK_SIZE * 2] __attribute__((aligned(GB_ROM_BANK_SIZE)));
uint8_t __attribute__((section(".noinit.")))
ram_memory[(GB_MAX_RAM_BANKS + 1) * GB_RAM_BANK_SIZE]
    __attribute__((aligned(GB_RAM_BANK_SIZE)));

struct ShortRomInfo g_shortRomInfos[MAX_ALLOWED_ROMS];
uint8_t g_numRoms = 0;
const uint8_t *g_loadedRomBanks[MAX_BANKS_PER_ROM];

uint32_t __attribute__((section(".noinit."))) _noInitTest;
uint32_t __attribute__((section(".noinit."))) _lastRunningGame;

static lfs_t _lfs = {};
static uint8_t _lfsFileBuffer[LFS_CACHE_SIZE];

uint8_t readRamBankCount(const uint8_t *gameptr);

uint8_t runGbBootloader();
void loadGame(uint8_t game);
void runNoMbcGame(uint8_t game);
void runMbc1Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);
void runMbc3Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);
void runMbc5Game(uint8_t game, uint16_t num_rom_banks, uint8_t num_ram_banks);

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

  RomStorage_init(&_lfs);

  if (_lastRunningGame < g_numRoms) {
    printf("Game %d was running before reset\n", _lastRunningGame);

    if (readRamBankCount(g_shortRomInfos[_lastRunningGame].firstBank) > 0) {
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

      lfs_err = lfs_file_write(
          &_lfs, &file, ram_memory,
          readRamBankCount(g_shortRomInfos[_lastRunningGame].firstBank) *
              GB_RAM_BANK_SIZE);
      printf("wrote %d bytes\n", lfs_err);

      lfs_file_close(&_lfs, &file);

      _lastRunningGame = 0xFF;

      pio_sm_put_blocking(pio0, SMC_WS2812, 0x150000 << 8);
    }
  }

  uint8_t game = runGbBootloader();

  (void)save_and_disable_interrupts();

  loadGame(game);

  // should only be reached in case there was an error loading the game
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
  for (size_t i = 0; i < g_numRoms; i++) {
    memcpy(&ram[(i * 16) + 1], &(g_shortRomInfos[i].name), 16);
  }
  ram[0] = (uint8_t)g_numRoms;
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
  num_ram_banks = readRamBankCount(gameptr);

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

  if(NULL == RomStorage_LoadRom(game))
  {
    printf("Error reading ROM\n");
    return; 
  }

  pio_sm_put_blocking(pio0, SMC_WS2812, 0);

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

uint8_t readRamBankCount(const uint8_t *gameptr) {
  static const uint8_t LOOKUP[] = {0, 0, 1, 4, 16, 8};
  const uint8_t value = gameptr[0x0149];

  if (value <= sizeof(LOOKUP)) {
    return LOOKUP[value];
  }

  return 0xFF;
}

void __attribute__((__noreturn__))
__assert_fail(const char *expr, const char *file, unsigned int line,
              const char *function) {
  printf("assert");
  while (1)
    ;
}
