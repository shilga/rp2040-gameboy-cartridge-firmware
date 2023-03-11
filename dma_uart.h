#include <stdint.h>
#include <sys/types.h>

void dma_uart_init();
void dma_uart_send(const uint8_t *data, size_t len);