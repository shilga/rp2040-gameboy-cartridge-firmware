#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>
#include <pico/platform.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dma_uart.h"
#include "ws2812.pio.h"

const int LED_PIN = 25;

#define WS2812_PIN 16

uint8_t memory[0x4000];
const uint8_t __in_flash() huge_data_table[0x4000] = {42};
const uint8_t *const huge_data_table_uncached =
    (const uint8_t *)((size_t)huge_data_table + 0x03000000);

int main() {
  // bi_decl(bi_program_description("Sample binary"));
  // bi_decl(bi_1pin_with_name(LED_PIN, "on-board PIN"));

  dma_uart_init();

  printf("Hello World!");

  PIO pio = pio0;
  int sm = 0;
  uint offset = pio_add_program(pio, &ws2812_program);

  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);

  uint32_t count = 0;

  while (1) {
    pio_sm_put_blocking(pio0, 0, 0x0000FF << 8);

    uint64_t start = time_us_64();
    memcpy(memory, huge_data_table_uncached, 0x4000);
    uint64_t end = time_us_64();

    printf("%u: copy took %llu\n", count, end - start);
    count++;

    pio_sm_put_blocking(pio0, 0, 0);

    sleep_ms(1000);
  }
}
