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

#include "GameBoyHeader.h"
#include <stdint.h>

uint8_t GameBoyHeader_readRamBankCount(const uint8_t *gameptr) {
  static const uint8_t LOOKUP[] = {0, 0, 1, 4, 16, 8};
  const uint8_t value = gameptr[0x0149];

  if (value <= sizeof(LOOKUP)) {
    return LOOKUP[value];
  }

  return 0xFF;
}

bool GameBoyHeader_hasRtc(const uint8_t *gameptr) {
  const uint8_t cartridgeType = gameptr[0x0147];

  if ((cartridgeType == 0x0F) || (cartridgeType == 0x10)) {
    return true;
  } else {
    return false;
  }
}

uint8_t GameBoyHeader_readMbc(const uint8_t *gameptr) {
  uint8_t mbc = 0xFF;

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

  return mbc;
}

bool GameBoyHeader_hasColorSupport(const uint8_t *gameptr) {
  const uint8_t cgbFlag = gameptr[0x0143];
  return (cgbFlag & 0x80) == 0x80;
}
