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

#include "cartridgereadthread.h"

/**
 * @brief read the ROM from a cartridge
 *
 * This routine will be called when a thread containing this
 * class is runned
 */
void CartridgeReadThread::run() {
    try {
        this->serial_interface->open_port();

    const int numblocks = 0x4000 / BLOCKSIZE;

    // Legacy worker retained for source compatibility. The cartridge exposes only
    // 256-byte ROM blocks, so a 16 KiB view is assembled from 64 RDBK calls.
    for(int i=0; i < numblocks; i++) {
        emit(read_block_start(i, numblocks));
        auto blockdata = this->serial_interface->read_block(i);
        this->data.append(blockdata);
        emit(read_block_done(i, numblocks));
    }

        this->serial_interface->close_port();
        emit(read_result_ready());
    } catch(const std::exception& e) {
        emit(thread_abort(QString("Cartridge read operation failed: %1").arg(e.what())));
        try {
            this->serial_interface->close_port();
        } catch(...) {}
    }
}
