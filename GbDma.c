#include <hardware/dma.h>
#include <hardware/pio.h>
#include <stdbool.h>
#include <stdint.h>

#include "GlobalDefines.h"
#include "hardware/address_mapped.h"

#define DMA_CHANNEL_CMD_EXECUTOR 0
#define DMA_CHANNEL_CMD_LOADER 1
#define DMA_CHANNEL_MEMORY_ACCESSOR 2

#define DMA_CHANNEL_ROM_LOWER_REQUESTOR 3
#define DMA_CHANNEL_RAM_READ_REQUESTOR 4
#define DMA_CHANNEL_RAM_WRITE_REQUESTOR 5

struct DmaCommand {
  const volatile void *read_addr;
  volatile void *write_addr;
};

volatile io_wo_32 *_txFifoWriteData = &(pio0->txf[SMC_GB_WRITE_DATA]);

/* clang-format off */

volatile uint32_t LOWER_ROM_READ_DMA_CTRL = (DMA_CHANNEL_ROM_LOWER_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS; 
struct DmaCommand LOWER_ROM_READ[] = {
  { &LOWER_ROM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) },
  { &_txFifoWriteData, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) },
  { &(pio0->rxf[SMC_GB_ROM_LOW]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].read_addr) },
  { &rom_low_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig)) },
  { NULL, NULL}
};
volatile void* _lowerRomReadCommands = &LOWER_ROM_READ[0];

volatile uint32_t RAM_READ_DMA_CTRL = (DMA_CHANNEL_RAM_READ_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS; 
struct DmaCommand RAM_READ[] = {
  { &RAM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) },
  { &_txFifoWriteData, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) },
  { &(pio1->rxf[SMC_GB_RAM_READ]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].read_addr) },
  { &ram_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig)) },
  { NULL, NULL}
};
volatile void* _ramReadCommands = &RAM_READ[0];

volatile io_ro_32 *_rxFifoRamWrite = &(pio1->rxf[SMC_GB_RAM_WRITE]);
volatile uint32_t RAM_WRITE_DMA_CTRL = (DMA_CHANNEL_RAM_WRITE_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS | (DREQ_PIO1_RX3 << DMA_CH2_CTRL_TRIG_TREQ_SEL_LSB); 
struct DmaCommand RAM_WRITE[] = {
  { &RAM_WRITE_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) },
  { &(pio1->rxf[SMC_GB_RAM_WRITE]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) },
  { &ram_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr)) },
  { &_rxFifoRamWrite, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig) },
  { NULL, NULL}
};
volatile void* _ramWriteCommands = &RAM_WRITE[0];

void GbDma_Setup() {
  dma_channel_claim(DMA_CHANNEL_CMD_EXECUTOR);
  dma_channel_claim(DMA_CHANNEL_CMD_LOADER);
  dma_channel_claim(DMA_CHANNEL_MEMORY_ACCESSOR);
  dma_channel_claim(DMA_CHANNEL_ROM_LOWER_REQUESTOR);
  dma_channel_claim(DMA_CHANNEL_RAM_READ_REQUESTOR);
  dma_channel_claim(DMA_CHANNEL_RAM_WRITE_REQUESTOR);

  dma_channel_config c = dma_channel_get_default_config(DMA_CHANNEL_CMD_LOADER);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, true);
  channel_config_set_ring(
      &c, true, 3); // wrap the write every 2 words (after each command)
  dma_channel_configure(DMA_CHANNEL_CMD_LOADER, &c,
                        &dma_hw->ch[DMA_CHANNEL_CMD_EXECUTOR]
                             .al2_read_addr, // Initial write address
                        NULL, // dummy will be replaced by requestor on dreq
                        2,    // Halt after each control block
                        false // Don't start yet
  );

  c = dma_channel_get_default_config(DMA_CHANNEL_CMD_EXECUTOR);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  // Trigger DMA_CHANNEL_CMD_LOADER needs to load the next command on finish
  channel_config_set_chain_to(&c, DMA_CHANNEL_CMD_LOADER);
  dma_channel_configure(DMA_CHANNEL_CMD_EXECUTOR, &c,
                        NULL, // will be set by CMD_LOADER
                        NULL, // will be set by CMD_LOADER
                        1,    // always transfer one word
                        false // Don't start yet.
  );

  dma_channel_set_trans_count(DMA_CHANNEL_MEMORY_ACCESSOR, 1, false);

  dma_channel_get_default_config(DMA_CHANNEL_ROM_LOWER_REQUESTOR);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio0, SMC_GB_ROM_LOW, false));
  dma_channel_configure(
      DMA_CHANNEL_ROM_LOWER_REQUESTOR, &c,
      &(dma_hw->ch[DMA_CHANNEL_CMD_LOADER].al3_read_addr_trig),
      &_lowerRomReadCommands,
      1,   // always transfer one word (pointer)
      true // trigger (wait for dreq)
  );

  dma_channel_get_default_config(DMA_CHANNEL_RAM_READ_REQUESTOR);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio1, SMC_GB_RAM_READ, false));
  dma_channel_configure(
      DMA_CHANNEL_RAM_READ_REQUESTOR, &c,
      &(dma_hw->ch[DMA_CHANNEL_CMD_LOADER].al3_read_addr_trig),
      &_ramReadCommands,
      1,   // always transfer one word (pointer)
      true // trigger (wait for dreq)
  );

  dma_channel_get_default_config(DMA_CHANNEL_RAM_WRITE_REQUESTOR);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio1, SMC_GB_RAM_WRITE, false));
  dma_channel_configure(
      DMA_CHANNEL_RAM_WRITE_REQUESTOR, &c,
      &(dma_hw->ch[DMA_CHANNEL_CMD_LOADER].al3_read_addr_trig),
      &_ramWriteCommands,
      1,   // always transfer one word (pointer)
      true // trigger (wait for dreq)
  );

}
