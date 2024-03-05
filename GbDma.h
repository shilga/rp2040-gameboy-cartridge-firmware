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

#ifndef D2C8524D_9F5F_4D9E_BD87_3A37DED846AC
#define D2C8524D_9F5F_4D9E_BD87_3A37DED846AC

#include <stdint.h>

void GbDma_Setup();
void GbDma_SetupHigherDmaDirectSsi();

void GbDma_StartDmaDirect();

void GbDma_EnableSaveRam();
void GbDma_DisableSaveRam();
void GbDma_EnableRtc();

#endif /* D2C8524D_9F5F_4D9E_BD87_3A37DED846AC */
