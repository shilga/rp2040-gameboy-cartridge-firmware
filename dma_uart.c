#include "dma_uart.h"

#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/uart.h>
#include <hardware/gpio.h>
#include <pico/platform.h>
#include <stdint.h>

#include <stdatomic.h>
#include <string.h>

#define UART0_TX_PIN 28

#define BAUD_RATE 1000000
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define UART_TX_BUFFER_SIZE 256

uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE]
    __attribute__((aligned(UART_TX_BUFFER_SIZE)));

static int _tx_dma_chan = -1;

volatile size_t _tx_dma_current_len;

static atomic_ulong _head;
static atomic_ulong _tail;

void dma_tx_finished_handler();

static size_t get_ringbuffer_free() {
  size_t size, head, tail;

  head = atomic_load_explicit(&_head, memory_order_relaxed);
  tail = atomic_load_explicit(&_tail, memory_order_relaxed);

  if (head == tail) {
    size = UART_TX_BUFFER_SIZE;
  } else {
    size = (tail - head) & (UART_TX_BUFFER_SIZE - 1U);
  }

  // Buffer free size is always 1 less than actual size
  return size - 1;
}

static size_t get_ringbuffer_full() {
  size_t size, head, tail;

  head = atomic_load_explicit(&_head, memory_order_relaxed);
  tail = atomic_load_explicit(&_tail, memory_order_relaxed);

  if (head == tail) {
    size = 0U;
  } else {
    size = (head - tail) & (UART_TX_BUFFER_SIZE - 1U);
  }

  // Buffer free size is always 1 less than actual size
  return size;
}

void dma_uart_init() {
  // Set the TX and RX pins by using the function select on the GPIO
  // See datasheet for more information on function select
  gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);

  _head = 0U;
  _tail = 0U;

  uart_init(uart0, BAUD_RATE);
  uart_set_hw_flow(uart0, false, false);
  uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
  uart_set_fifo_enabled(uart0, true);

  _tx_dma_chan = dma_claim_unused_channel(true);

  dma_channel_config tx_dma_config =
      dma_channel_get_default_config(_tx_dma_chan);
  channel_config_set_read_increment(&tx_dma_config, true);
  channel_config_set_write_increment(&tx_dma_config, false);
  channel_config_set_dreq(&tx_dma_config, uart_get_dreq(uart0, true));
  channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
  channel_config_set_ring(&tx_dma_config, false, 8);
  channel_config_set_high_priority(&tx_dma_config, false);

  dma_channel_set_config(_tx_dma_chan, &tx_dma_config, false);
  dma_channel_set_read_addr(_tx_dma_chan, uart_tx_buffer, false);
  dma_channel_set_write_addr(_tx_dma_chan, &uart_get_hw(uart0)->dr, false);

  _tx_dma_current_len = 0U;

  irq_set_exclusive_handler(DMA_IRQ_0, dma_tx_finished_handler);
  irq_set_enabled(DMA_IRQ_0, true);

  // Tell the DMA to raise IRQ line 0 when the channel finishes a block
  dma_channel_set_irq0_enabled(_tx_dma_chan, true);
}

void start_dma_transfer() {
  if ((_tx_dma_current_len == 0) &&
      (_tx_dma_current_len = get_ringbuffer_full() > 0)) {
    dma_channel_set_trans_count(_tx_dma_chan, _tx_dma_current_len, true);
  }
}

void dma_uart_send(const uint8_t *data, size_t len) {
  size_t tocopy, free, head;

  free = get_ringbuffer_free();
  if (len > free) {
    return;
  }

  head = atomic_load_explicit(&_head, memory_order_acquire);

  // Write data to linear part of buffer
  tocopy = MIN(UART_TX_BUFFER_SIZE - head, len);
  memcpy(&uart_tx_buffer[head], data, tocopy);
  head += tocopy;
  len -= tocopy;

  // Write data to beginning of buffer (overflow part)
  if (len > 0) {
    memcpy(uart_tx_buffer, &data[tocopy], len);
    head = len;
  }

  // wrap head
  head = head & (UART_TX_BUFFER_SIZE - 1U);

  atomic_store_explicit(&_head, head, memory_order_release);

  start_dma_transfer();
}

void dma_tx_finished_handler() {
  // Clear the interrupt request.
  dma_hw->ints0 = 1u << _tx_dma_chan;

  size_t tail = atomic_load_explicit(&_tail, memory_order_acquire);
  tail += _tx_dma_current_len;

  // wrap tail
  tail = tail & (UART_TX_BUFFER_SIZE - 1U);

  atomic_store_explicit(&_tail, tail, memory_order_release);

  _tx_dma_current_len = 0U;

  start_dma_transfer();
}
