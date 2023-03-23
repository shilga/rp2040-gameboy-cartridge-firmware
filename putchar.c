#include <pico/platform.h>

#include "dma_uart.h"

void __no_inline_not_in_flash_func(putchar_)(char character) {
  dma_uart_send(&character, 1);
}