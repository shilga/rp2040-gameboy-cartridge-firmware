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

#ifndef A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5
#define A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5

#include <hardware/spi.h>

void ws2812b_spi_init(spi_inst_t *spi);
void ws2812b_setRgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5 */
