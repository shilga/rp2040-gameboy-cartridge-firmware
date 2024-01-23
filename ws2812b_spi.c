#include "ws2812b_spi.h"

#include <hardware/resets.h>
#include <hardware/spi.h>
#include <stdint.h>

#define TARGET_BAUDRATE 2800000

#define WS2812_FILL_BUFFER(COLOR)                                              \
  {                                                                            \
    uint8_t mask01 = 0x40;                                                     \
    uint8_t mask10 = 0x80;                                                     \
    for (uint8_t mask = 0xc0; mask; mask >>= 2) {                              \
      if ((COLOR & mask) == mask) {                                            \
        *ptr++ = 0xEE;                                                         \
      } else if ((COLOR & mask) == mask01) {                                   \
        *ptr++ = 0x8E;                                                         \
      } else if ((COLOR & mask) == mask10) {                                   \
        *ptr++ = 0xE8;                                                         \
      } else {                                                                 \
        *ptr++ = 0x88;                                                         \
      }                                                                        \
      mask01 >>= 2;                                                            \
      mask10 >>= 2;                                                            \
    }                                                                          \
  }

static uint8_t _buffer[16];
spi_inst_t *_spi = NULL;

void ws2812b_spi_init(spi_inst_t *spi) {
  reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
  unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS
                                 : RESETS_RESET_SPI1_BITS);
  spi_set_baudrate(spi, TARGET_BAUDRATE);
  spi_set_format(spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS); // enable
  _spi = spi;
}

void __no_inline_not_in_flash_func(ws2812b_setRgb)(uint8_t r, uint8_t g,
                                                   uint8_t b) {
  uint8_t *ptr = (uint8_t *)&_buffer[0];
  WS2812_FILL_BUFFER(g);
  WS2812_FILL_BUFFER(r);
  WS2812_FILL_BUFFER(b);

  for (size_t i = 0; i < sizeof(_buffer); i += 2) {
    spi_get_hw(_spi)->dr =
        ((uint32_t)_buffer[i] << 8) | ((uint32_t)_buffer[i + 1]);
  }
}
