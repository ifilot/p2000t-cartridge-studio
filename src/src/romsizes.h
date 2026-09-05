/****************************************************************************
 *                                                                          *
 *   P2000T Cartridge Studio                                                *
 *   Copyright (C) 2023 Ivo Filot <ivo@ivofilot.nl>                         *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as                *
 *   published by the Free Software Foundation, either version 3 of the     *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public license      *
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>. *
 *                                                                          *
 ****************************************************************************/

#ifndef ROMSIZES_H
#define ROMSIZES_H

#define ROMSIZE 0x40000     // SST39SF020: 256 KiB
#define BLOCKSIZE 0x100     // Protocol block: 256 bytes
#define NUMBLOCKS (ROMSIZE/BLOCKSIZE)
#define BANKSIZE 0x4000     // P2000T DIP-switch bank: 16 KiB
#define NUMBANKS (ROMSIZE/BANKSIZE)

#endif // ROMSIZES_H
