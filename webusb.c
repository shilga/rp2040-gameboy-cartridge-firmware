/* RP2040 GameBoy cartridge
 * Copyright (C) 2023 Sebastian Quilitz
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tusb.h>

#include <pico/stdio.h>
#include <pico/stdio_uart.h>

#include "GlobalDefines.h"
#include "device/usbd.h"
#include "usb_descriptors.h"

#include "RomStorage.h"

static bool web_serial_connected = false;

static uint8_t command_buffer[64];

#define URL "croco.x-pantion.de"

const tusb_desc_webusb_url_t desc_url = {.bLength = 3 + sizeof(URL) - 1,
                                         .bDescriptorType =
                                             3,        // WEBUSB URL type
                                         .bScheme = 1, // 0: http, 1: https
                                         .url = URL};

static void webserial_task(void);
static void handle_command(uint8_t command);
static int handle_new_rom_command(uint8_t buff[63]);
static int handle_rom_upload_command(uint8_t buff[63]);
static int handle_request_rom_info_command(uint8_t buff[63]);
static int handle_delete_rom_command(uint8_t buff[63]);
static int handle_request_savegame_download_command(uint8_t buff[63]);
static int handle_savegame_transmit_chunk_command(uint8_t buff[63]);
static int handle_request_savegame_upload_command(uint8_t buff[63]);
static int handle_savegame_received_chunk_command(uint8_t buff[63]);

void usb_start() { tusb_init(); }

void usb_shutdown() { tud_disconnect(); }

void usb_run() {
  tud_task();
  webserial_task();
}

void webserial_task(void) {
  if (web_serial_connected) {
    if (tud_vendor_available()) {
      uint8_t buf[1];
      uint32_t count = tud_vendor_read(buf, sizeof(buf));
      if (count) {
        printf("webserial: 0x%x\n", buf[0]);

        handle_command(buf[0]);
      }
    }
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {}

// Invoked when device is unmounted
void tud_umount_cb(void) {}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }

// Invoked when usb bus is resumed
void tud_resume_cb(void) {}

//--------------------------------------------------------------------+
// WebUSB use vendor class
//--------------------------------------------------------------------+

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage
// (setup/data/ack) return false to stall control endpoint (e.g unsupported
// request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP)
    return true;

  switch (request->bmRequestType_bit.type) {
  case TUSB_REQ_TYPE_VENDOR:
    switch (request->bRequest) {
    case VENDOR_REQUEST_WEBUSB:
      // match vendor request in BOS descriptor
      // Get landing page url
      return tud_control_xfer(rhport, request, (void *)(uintptr_t)&desc_url,
                              desc_url.bLength);

    case VENDOR_REQUEST_MICROSOFT:
      if (request->wIndex == 7) {
        // Get Microsoft OS 2.0 compatible descriptor
        uint16_t total_len;
        memcpy(&total_len, desc_ms_os_20 + 8, 2);
        printf("MS descriptor requested\n");

        return tud_control_xfer(rhport, request,
                                (void *)(uintptr_t)desc_ms_os_20, total_len);
      } else {
        return false;
      }

    default:
      break;
    }
    break;

  case TUSB_REQ_TYPE_CLASS:
    if (request->bRequest == 0x22) {
      // Webserial simulate the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to
      // connect and disconnect.
      web_serial_connected = (request->wValue != 0);
      printf("web_serial_connected %d\n", web_serial_connected);

      // response with status OK
      return tud_control_status(rhport, request);
    }
    break;

  default:
    break;
  }

  // stall unknown request
  return false;
}

static void handle_command(uint8_t command) {
  int response_length = 0;

  switch (command) {
  case 1:
    uint16_t usedBanks = RomStorage_GetNumUsedBanks();
    command_buffer[1] = g_numRoms;
    command_buffer[2] = (usedBanks >> 8) & 0xFF;
    command_buffer[3] = usedBanks & 0xFF;
    command_buffer[4] = (MAX_BANKS >> 8) & 0xFF;
    command_buffer[5] = MAX_BANKS & 0xFF;
    response_length = 5;
    break;
  case 2:
    response_length = handle_new_rom_command(&command_buffer[1]);
    break;
  case 3:
    response_length = handle_rom_upload_command(&command_buffer[1]);
    break;
  case 4:
    response_length = handle_request_rom_info_command(&command_buffer[1]);
    break;
  case 5:
    response_length = handle_delete_rom_command(&command_buffer[1]);
    break;
  case 6:
    response_length =
        handle_request_savegame_download_command(&command_buffer[1]);
    break;
  case 7:
    response_length =
        handle_savegame_transmit_chunk_command(&command_buffer[1]);
    break;
  case 8:
    response_length =
        handle_request_savegame_upload_command(&command_buffer[1]);
    break;
  case 9:
    response_length =
        handle_savegame_received_chunk_command(&command_buffer[1]);
    break;
  default:
    printf("webusb: unknown command\n");
    response_length = -1;
    break;
  }

  if (response_length < 0) {
    command_buffer[0] = 0xFF;
    response_length = 1;
  } else {
    command_buffer[0] = command;
    response_length += 1;
  }

  tud_vendor_write(command_buffer, response_length);
  tud_vendor_flush();
}

static int handle_new_rom_command(uint8_t buff[63]) {
  uint16_t num_banks;

  uint32_t count = tud_vendor_read(buff, 19);
  if (count != 19) {
    printf("wrong number of bytes for new rom command\n");
    return -1;
  }

  num_banks = (buff[0] << 8) + buff[1];
  buff[18] = 0; // force zero terminate received string

  buff[0] = 0;
  if (RomStorage_StartNewRomTransfer(num_banks,
                                     (char *)&buff[sizeof(uint16_t)]) < 0) {
    buff[0] = 1;
  }

  return 1;
}

static int handle_rom_upload_command(uint8_t buff[63]) {
  uint16_t bank, chunk;

  uint32_t count = tud_vendor_read(buff, 36);
  if (count != 36) {
    printf("wrong number of bytes for rom chunk\n");
    return -1;
  }

  bank = (buff[0] << 8) + buff[1];
  chunk = (buff[2] << 8) + buff[3];

  buff[0] = 0;
  if (RomStorage_TransferRomChunk(bank, chunk, &buff[sizeof(uint16_t) * 2]) <
      0) {
    buff[0] = 1;
  }

  return 1;
}

static int handle_request_rom_info_command(uint8_t buff[63]) {
  uint32_t count = tud_vendor_read(buff, 1);
  if (count != 1) {
    printf("wrong number of bytes for rom info command\n");
    return -1;
  }

  const uint8_t requestedRom = buff[0];

  if (requestedRom >= g_numRoms) {
    return -1;
  }

  memcpy(buff, &g_shortRomInfos[requestedRom].name, 17);
  buff[17] = g_shortRomInfos[requestedRom].numRamBanks;

  return 18;
}

static int handle_delete_rom_command(uint8_t buff[63]) {
  uint32_t count = tud_vendor_read(buff, 1);
  if (count != 1) {
    printf("wrong number of bytes for rom delete command\n");
    return -1;
  }

  const uint8_t requestedRom = buff[0];

  buff[0] = 0;
  if (RomStorage_DeleteRom(requestedRom) < 0) {
    buff[0] = 1;
  }

  return 1;
}

static int handle_request_savegame_download_command(uint8_t buff[63]) {
  uint32_t count = tud_vendor_read(buff, 1);
  if (count != 1) {
    printf("wrong number of bytes for rom delete command\n");
    return -1;
  }

  const uint8_t requestedRom = buff[0];

  buff[0] = 0;
  if (RomStorage_StartRamDownload(requestedRom) < 0) {
    buff[0] = 1;
  }

  return 1;
}

static int handle_savegame_transmit_chunk_command(uint8_t buff[63]) {
  uint16_t bank, chunk;

  if (RomStorage_GetRamDownloadChunk(&buff[4], &bank, &chunk) < 0) {
    return -1;
  }

  buff[0] = (bank >> 8) & 0xFFU;
  buff[1] = bank & 0xFFU;
  buff[2] = (chunk >> 8) & 0xFFU;
  buff[3] = chunk & 0xFFU;

  return 36;
}

static int handle_request_savegame_upload_command(uint8_t buff[63]) {
  uint32_t count = tud_vendor_read(buff, 1);
  if (count != 1) {
    printf("wrong number of bytes for savegame upload command\n");
    return -1;
  }

  const uint8_t requestedRom = buff[0];

  buff[0] = 0;
  if (RomStorage_StartRamUpload(requestedRom) < 0) {
    buff[0] = 1;
  }

  return 1;
}

static int handle_savegame_received_chunk_command(uint8_t buff[63]) {
  uint16_t bank, chunk;

  uint32_t count = tud_vendor_read(buff, 36);
  if (count != 36) {
    printf("wrong number of bytes for savegame chunk, got %u\n", count);
    return -1;
  }

  bank = (buff[0] << 8) + buff[1];
  chunk = (buff[2] << 8) + buff[3];

  buff[0] = 0;
  if (RomStorage_TransferRamUploadChunk(bank, chunk,
                                        &buff[sizeof(uint16_t) * 2]) < 0) {
    buff[0] = 1;
  }

  return 1;
}
