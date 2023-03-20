#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/platform.h>
#include <pico/time.h>
#include <pico/stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hardware/dma.h>

#include "Tetris.h"
#include "dma_uart.h"

#include "gameboy_bus.pio.h"
#include "ws2812.pio.h"

#define WS2812_PIN 27

#define PIN_GB_RESET 26

#define SMC_GB_MAIN 0
#define SMC_GB_WRITE_DATA 1

uint8_t memory[0x8000] __attribute__((aligned(32768U)));

const uint8_t *const tetris_uncached =
    (const uint8_t *)((size_t)Tetris + 0x03000000);

static void set_x(PIO pio, unsigned smc, unsigned x) {
  pio_sm_put_blocking(pio, smc, x);
  pio_sm_exec_wait_blocking(pio, smc, pio_encode_pull(false, false));
  pio_sm_exec_wait_blocking(pio, smc, pio_encode_mov(pio_x, pio_osr));
}

static void setup_read_dma(PIO pio, unsigned sm, unsigned sm_write_data) {
  unsigned dma1, dma2;
  dma_channel_config cfg;

  dma1 = dma_claim_unused_channel(true);
  dma2 = dma_claim_unused_channel(true);

  // Set up DMA2 first (it's not triggered until DMA1 does so)
  cfg = dma_channel_get_default_config(dma2);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  // dreq defaults to DREQ_FORCE
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  dma_channel_set_trans_count(dma2, 1, false);
  dma_channel_set_write_addr(dma2, &(pio->txf[sm_write_data]), false);
  channel_config_set_chain_to(&cfg, dma1);
  dma_channel_set_config(dma2, &cfg, false);

  // Set up DMA1 and trigger it
  cfg = dma_channel_get_default_config(dma1);
  channel_config_set_read_increment(&cfg, false);
  // write increment defaults to false
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
  // transfer size defaults to 32
  dma_channel_set_trans_count(dma1, 1, false);
  dma_channel_set_read_addr(dma1, &(pio->rxf[sm]), false);
  dma_channel_set_write_addr(dma1, &(dma_hw->ch[dma2].al3_read_addr_trig),
                             false);
  dma_channel_set_config(dma1, &cfg, true);
}

int main() {
  // bi_decl(bi_program_description("Sample binary"));
  // bi_decl(bi_1pin_with_name(LED_PIN, "on-board PIN"));

  dma_uart_init();

  printf("Hello World!");

  {
    /* The value for VCO set here is meant for least power
     * consumption. */
    const unsigned vco = 532000000; /* 266MHz/133MHz */
    const unsigned div1 = 2, div2 = 1;

    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(2);
    //set_sys_clock_khz(266000, true);
   //  set_sys_clock_pll(vco, div1, div2);
   //  sleep_ms(2);
  }

  memcpy(memory, tetris_uncached, sizeof(memory));

  gpio_init(PIN_GB_RESET);
  gpio_set_dir(PIN_GB_RESET, true);
  gpio_put(PIN_GB_RESET, 1);

  gpio_init(28);
  gpio_set_dir(28, true);
  gpio_set_function(28, GPIO_FUNC_PIO1);

  for (uint pin = PIN_AD_BASE-1; pin < PIN_AD_BASE + 25; pin++) {
    // gpio_init(pin);
    // gpio_set_dir(pin, false);
    // gpio_set_function(pin, GPIO_FUNC_PIO1);

    /* Disable schmitt triggers on GB Bus. The bus transceivers
     * already have schmitt triggers. */
    gpio_set_input_hysteresis_enabled(pin, false);
    /* Use fast slew rate for GB Bus. */
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
    /* Initialise PIO0 pins. */
    pio_gpio_init(pio1, pin);
  }

  // Load the gameboy_bus program that's shared by the read and write state
  // machines
  uint offset_main = pio_add_program(pio1, &gameboy_bus_program);
  uint offset_write_data = pio_add_program(pio1, &gameboy_bus_write_to_data_program);

  // Initialize the read state machine (handles read accesses)
  gameboy_bus_program_init(pio1, SMC_GB_MAIN, offset_main);
  gameboy_bus_write_to_data_program_init(pio1, SMC_GB_WRITE_DATA, offset_write_data);
  pio_sm_set_enabled(pio1, SMC_GB_MAIN, true);
  pio_sm_set_enabled(pio1, SMC_GB_WRITE_DATA, true);

  set_x(pio1, SMC_GB_MAIN, ((unsigned)memory) >> 15);

  PIO pio = pio0;
  int sm = 0;
  uint offset = pio_add_program(pio, &ws2812_program);

  setup_read_dma(pio1, SMC_GB_MAIN, SMC_GB_WRITE_DATA);

  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);

  uint32_t count = 0;

  gpio_put(PIN_GB_RESET, 0);

  while (1) {
      pio_sm_put_blocking(pio0, 0, 0x0000FF << 8);
      sleep_ms(1);
      pio_sm_put_blocking(pio0, 0, 0);

      sleep_ms(999);
  }

  //   while (1) {
  //     pio_sm_put_blocking(pio0, 0, 0x0000FF << 8);

  //     uint64_t start = time_us_64();
  //     memcpy(memory, huge_data_table_uncached, 0x4000);
  //     uint64_t end = time_us_64();

  //     //printf("%u: copy took %llu\n", count, end - start);
  //     count++;

  //     pio_sm_put_blocking(pio0, 0, 0);

  //     sleep_ms(1000);
  //   }
}
