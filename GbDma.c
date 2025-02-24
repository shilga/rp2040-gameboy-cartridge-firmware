/* RP2040 GameBoy cartridge
 * Copyright (C) 2024 Sebastian Quilitz
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

#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/structs/ssi.h>
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

int _dmaChannelRomHigherDirectSsiPioAddrLoader = -1;
int _dmaChannelRomHigherDirectSsiBaseAddrLoader = -1;
int _dmaChannelRomHigherDirectSsiFlashRequester = -1;
int _dmaChannelRomHigherDirectSsiPioDataLoader = -1;

/*
 * as the DMA needs to transfer the address of this register a variable(pointer)
 * is needed that holds the address of the register
 */
volatile io_wo_32 *_txFifoWriteData = &(pio0->txf[SMC_GB_WRITE_DATA]);
volatile io_ro_32 *_rxFifoRamWrite = &(pio1->rxf[SMC_GB_RAM_WRITE]);

/*
 * variable that can be used to let a DMA dummy transfer data
 */
volatile uint32_t _devNull;
volatile uint32_t *_devNullPtr = &_devNull;

struct DmaCommand {
  const volatile void *read_addr;
  volatile void *write_addr;
};

/* clang-format off */

static volatile uint32_t LOWER_ROM_READ_DMA_CTRL = (DMA_CHANNEL_ROM_LOWER_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS | (DMA_CH2_CTRL_TRIG_TREQ_SEL_VALUE_PERMANENT << DMA_CH2_CTRL_TRIG_TREQ_SEL_LSB); 
static struct DmaCommand LOWER_ROM_READ[] = {
  { &LOWER_ROM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // load the settings for this transfer into the MEMORY_ACCESOR_DMA control register
  { &_txFifoWriteData, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr of the tx-fifo of the write data PIO-SM into the write register of MEMORY_ACCESSOR_DMA
  { &(pio0->rxf[SMC_GB_ROM_LOW]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].read_addr) }, // load the addr from the rx-fifo of the PIO-SM triggering this transfer
  { &rom_low_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig)) }, // load the base addr, write it into the read-addr of the READ_DMA, or-ing it with the addr received and trigger the READ_DMA transfer
  { NULL, NULL}
};

static volatile uint32_t RAM_READ_DMA_CTRL = (DMA_CHANNEL_RAM_READ_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS | (DMA_CH2_CTRL_TRIG_TREQ_SEL_VALUE_PERMANENT << DMA_CH2_CTRL_TRIG_TREQ_SEL_LSB);
static struct DmaCommand RAM_READ[] = {
  { &RAM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // load the settings for this transfer into the MEMORY_ACCESOR_DMA control register
  { &_txFifoWriteData, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr of the tx-fifo of the write data PIO-SM into the write register of MEMORY_ACCESSOR_DMA
  { &(pio1->rxf[SMC_GB_RAM_READ]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].read_addr) }, // load the addr from the rx-fifo of the PIO-SM triggering this transfer
  { &ram_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig)) }, // load the base addr, write it into the read-addr of the MEMORY_ACCESSOR_DMA, or-ing it with the addr received and trigger the MEMORY_ACCESSOR_DMA transfer
  { NULL, NULL}
};

static volatile uint32_t RAM_WRITE_DMA_CTRL = (DMA_CHANNEL_RAM_WRITE_REQUESTOR << DMA_CH2_CTRL_TRIG_CHAIN_TO_LSB) | DMA_CH2_CTRL_TRIG_HIGH_PRIORITY_BITS | DMA_CH2_CTRL_TRIG_EN_BITS | (DREQ_PIO1_RX3 << DMA_CH2_CTRL_TRIG_TREQ_SEL_LSB);
static struct DmaCommand RAM_WRITE[] = {
  { &RAM_WRITE_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // setup MEMORY_ACCESSOR_DMA for this write to RAM transaction
  { &(pio1->rxf[SMC_GB_RAM_WRITE]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr from the rx-fifo of the PIO-SM triggering this transfer into the write addr of MEMORY_ACCESSOR_DMA
  { &ram_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr)) }, // load the base addr into the write addr of MEMORY_ACCESSOR_DMA, or-ing it with the addr already there (received from rx-fifo)
  { &_rxFifoRamWrite, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig) }, // load the addr of the rx-fifo which will have the data to be written to RAM into the read register of MEMORY_ACCESSOR_DMA and trigger it's transfer
  { NULL, NULL}
};

struct DmaCommand RAM_DUMMY_READ[] = {
  { &RAM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // load the settings for this transfer into the MEMORY_ACCESOR_DMA control register
  { &_devNullPtr, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr of the tx-fifo of the write data PIO-SM into the write register of MEMORY_ACCESSOR_DMA
  { &(pio1->rxf[SMC_GB_RAM_READ]), &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].read_addr) }, // load the addr from the rx-fifo of the PIO-SM triggering this transfer
  { &ram_base, hw_set_alias_untyped(&(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig)) }, // load the base addr, write it into the read-addr of the MEMORY_ACCESSOR_DMA, or-ing it with the addr received and trigger the MEMORY_ACCESSOR_DMA transfer
  { NULL, NULL}
};

static struct DmaCommand RAM_DUMMY_WRITE[] = {
  { &RAM_WRITE_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // setup MEMORY_ACCESSOR_DMA for this write to RAM transaction
  { &(pio1->rxf[SMC_GB_RAM_WRITE]), &_devNull }, // load the addr from the rx-fifo of the PIO-SM triggering this transfer into devNUll
  { &_devNullPtr, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr of _devNull into write addr of MEMORY_ACCESSOR_DMA
  { &_rxFifoRamWrite, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig) }, // load the addr of the rx-fifo which will have the data to be written to RAM into the read register of MEMORY_ACCESSOR_DMA and trigger it's transfer
  { NULL, NULL}
};

struct DmaCommand RTC_READ[] = {
  { &RAM_READ_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // load the settings for this transfer into the MEMORY_ACCESOR_DMA control register
  { &_txFifoWriteData, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the addr of the tx-fifo of the write data PIO-SM into the write register of MEMORY_ACCESSOR_DMA
  { &(pio1->rxf[SMC_GB_RAM_READ]), &_devNull }, // dummy read the addr from the rx-fifo of the PIO-SM triggering this transfer
  { &_rtcLatchPtr, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig) }, // load the current rtc register addr, write it into the read-addr of the MEMORY_ACCESSOR_DMA and trigger the MEMORY_ACCESSOR_DMA transfer
  { NULL, NULL}
};

static struct DmaCommand RTC_WRITE[] = {
  { &RAM_WRITE_DMA_CTRL, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al1_ctrl) }, // setup MEMORY_ACCESSOR_DMA for this write to RAM transaction
  { &(pio1->rxf[SMC_GB_RAM_WRITE]), &_devNull }, // dummy load the addr from the rx-fifo of the PIO-SM triggering this transfer
  { &_rtcRealPtr, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].write_addr) }, // load the current rtc register addr into the write addr of MEMORY_ACCESSOR_DMA
  { &_rxFifoRamWrite, &(dma_hw->ch[DMA_CHANNEL_MEMORY_ACCESSOR].al3_read_addr_trig) }, // load the addr of the rx-fifo which will have the data to be written to RAM into the read register of MEMORY_ACCESSOR_DMA and trigger it's transfer
  { NULL, NULL}
};

/* clang-format on */

static volatile void *_lowerRomReadCommands = &LOWER_ROM_READ[0];
static volatile void *_ramReadCommands = &RAM_READ[0];
static volatile void *_ramWriteCommands = &RAM_WRITE[0];

static void setup_read_dma_method2(PIO pio, unsigned sm, PIO pio_write_data,
                                   unsigned sm_write_data,
                                   const volatile void *read_base_addr);

void GbDma_Setup() {
  dma_channel_claim(DMA_CHANNEL_CMD_EXECUTOR);
  dma_channel_claim(DMA_CHANNEL_CMD_LOADER);
  dma_channel_claim(DMA_CHANNEL_MEMORY_ACCESSOR);
  // dma_channel_claim(DMA_CHANNEL_ROM_LOWER_REQUESTOR);
  dma_channel_claim(DMA_CHANNEL_RAM_READ_REQUESTOR);
  dma_channel_claim(DMA_CHANNEL_RAM_WRITE_REQUESTOR);

  /*
   * Setup the DMA which acts as the CMD_LOADER. It will be initially triggered
   * by one of the triggering DMAs. They are triggered by the PIO-SM DREQ and
   * will load the DmaCommand for the transaction into the the read_register of
   * this DMA. This DMA loads the current command into the CMD_EXECUTOR and
   * triggers it. It will be triggered again by the CMD_EXECUTOR until all
   * commands are executed (NULL-entry).
   */
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

  /*
   * Setup the DMA which acts as the CMD_EXECUTOR: IT will receive it's commands
   * and triggers from CMD_LOADER. Only it's read and write address changes with
   * each command. Those are always 32-bit wide and only one word long.
   * CMD_EXECUTOR chains to CMD_LOADER to trigger the loading of the next
   * command.
   */
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

  /*
   * The DMA wich acts as MEMORY_ACCESOR is completely setup and trigger by the
   * CMD_EXECUTOR based on the command chain received from CMD_LOADER. It's only
   * necessary the transfer count here, as everything else is done by the
   * commands.
   */
  dma_channel_set_trans_count(DMA_CHANNEL_MEMORY_ACCESSOR, 1, false);

  /*
   * Setup all the REQUESTOR DMAs for each PIO-SM the DMAs above serve. Each
   * REQUSTOR DMA is setup to wait for a DREQ from a PIO state machine. After it
   * gets it's DREQ it loads the command chain needed to serve this request into
   * the CMD_LOADER and triggers it. This starts the whole chain of commands
   * which will eventually lead to the serving of the necessary memory transfer
   * from an into the FIFOs of the state machine.
   */
  // dma_channel_get_default_config(DMA_CHANNEL_ROM_LOWER_REQUESTOR);
  // channel_config_set_read_increment(&c, false);
  // channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  // channel_config_set_dreq(&c, pio_get_dreq(pio0, SMC_GB_ROM_LOW, false));
  // dma_channel_configure(
  //     DMA_CHANNEL_ROM_LOWER_REQUESTOR, &c,
  //     &(dma_hw->ch[DMA_CHANNEL_CMD_LOADER].al3_read_addr_trig),
  //     &_lowerRomReadCommands,
  //     1,   // always transfer one word (pointer)
  //     true // trigger (wait for dreq)
  // );

  setup_read_dma_method2(pio0, SMC_GB_ROM_LOW, pio0, SMC_GB_WRITE_DATA,
                         &rom_low_base);

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

void GbDma_SetupHigherDmaDirectSsi() {
  _dmaChannelRomHigherDirectSsiPioAddrLoader = dma_claim_unused_channel(true);
  _dmaChannelRomHigherDirectSsiBaseAddrLoader = dma_claim_unused_channel(true);
  _dmaChannelRomHigherDirectSsiFlashRequester = dma_claim_unused_channel(true);
  _dmaChannelRomHigherDirectSsiPioDataLoader = dma_claim_unused_channel(true);

  dma_channel_config c = dma_channel_get_default_config(
      _dmaChannelRomHigherDirectSsiPioAddrLoader);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio0, SMC_GB_ROM_HIGH, false));
  channel_config_set_chain_to(&c, _dmaChannelRomHigherDirectSsiBaseAddrLoader);
  dma_channel_configure(_dmaChannelRomHigherDirectSsiPioAddrLoader, &c,
                        &(dma_hw->sniff_data), &(pio0->rxf[SMC_GB_ROM_HIGH]),
                        1,    // always transfer one word (pointer)
                        false // do not trigger yet, will be done after all the
                              // other DMAs are setup
  );

  c = dma_channel_get_default_config(
      _dmaChannelRomHigherDirectSsiBaseAddrLoader);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_sniff_enable(&c, true);
  channel_config_set_chain_to(&c, _dmaChannelRomHigherDirectSsiFlashRequester);
  dma_channel_configure(_dmaChannelRomHigherDirectSsiBaseAddrLoader, &c,
                        &_devNull, &rom_high_base_flash_direct,
                        1,    // always transfer one word (pointer)
                        false // will be triggered by PioAddrLoader
  );
  dma_sniffer_enable(_dmaChannelRomHigherDirectSsiBaseAddrLoader,
                     DMA_SNIFF_CTRL_CALC_VALUE_SUM, true);

  c = dma_channel_get_default_config(
      _dmaChannelRomHigherDirectSsiFlashRequester);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_chain_to(&c, _dmaChannelRomHigherDirectSsiPioDataLoader);
  dma_channel_configure(_dmaChannelRomHigherDirectSsiFlashRequester, &c,
                        &(ssi_hw->dr0), &(dma_hw->sniff_data),
                        1,    // always transfer one word (pointer)
                        false // will be triggered by BaseAddrLoader
  );

  c = dma_channel_get_default_config(
      _dmaChannelRomHigherDirectSsiPioDataLoader);
  channel_config_set_read_increment(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, DREQ_XIP_SSIRX);
  channel_config_set_chain_to(&c, _dmaChannelRomHigherDirectSsiPioAddrLoader);
  dma_channel_configure(_dmaChannelRomHigherDirectSsiPioDataLoader, &c,
                        &(pio0->txf[SMC_GB_WRITE_DATA]), &(ssi_hw->dr0),
                        1,    // always transfer one byte
                        false // will be triggered by FlashRequester
  );

  // Do not trigger the first stage dma yet. This will be done only after SSI is
  // in the correct mode
}

void __no_inline_not_in_flash_func(GbDma_StartDmaDirect)() {
  dma_hw->multi_channel_trigger = 1
                                  << _dmaChannelRomHigherDirectSsiPioAddrLoader;
}

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

void __no_inline_not_in_flash_func(GbDma_EnableSaveRam)() {
  _ramReadCommands = &RAM_READ[0];
  _ramWriteCommands = &RAM_WRITE[0];
}

void __no_inline_not_in_flash_func(GbDma_DisableSaveRam)() {
  _ramReadCommands = &RAM_DUMMY_READ[0];
  _ramWriteCommands = &RAM_DUMMY_WRITE[0];
}

void __no_inline_not_in_flash_func(GbDma_EnableRtc)() {
  _ramReadCommands = &RTC_READ[0];
  _ramWriteCommands = &RAM_DUMMY_WRITE[0]; // write needs special handling
}
