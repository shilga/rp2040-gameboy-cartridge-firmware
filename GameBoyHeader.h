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

#ifndef B8D95B66_116E_427A_B1E9_B1BF5AFC49E5
#define B8D95B66_116E_427A_B1E9_B1BF5AFC49E5

#include <stdbool.h>
#include <stdint.h>

uint8_t GameBoyHeader_readRamBankCount(const uint8_t *gameptr);

bool GameBoyHeader_hasRtc(const uint8_t *gameptr);

uint8_t GameBoyHeader_readMbc(const uint8_t *gameptr);

#endif /* B8D95B66_116E_427A_B1E9_B1BF5AFC49E5 */
