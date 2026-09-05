#include "flashthread.h"

#include <QDebug>

#include <stdexcept>

void FlashThread::set_complete_rom(const QByteArray& _data)
{
    if(_data.size() != ROMSIZE) {
        throw std::invalid_argument("A complete SST39SF020 image must be exactly 256 KiB");
    }
    this->complete_rom = true;
    this->data = _data;
}

void FlashThread::set_bank(unsigned int _bank_index, const QByteArray& _data)
{
    if(_bank_index >= NUMBANKS) throw std::out_of_range("SST39SF020 bank index is outside 0-15");
    if(_data.size() != BANKSIZE) throw std::invalid_argument("A P2000T bank ROM must be exactly 16 KiB");
    this->complete_rom = false;
    this->bank_index = _bank_index;
    this->data = _data;
}

void FlashThread::run()
{
    try {
        const int required_size = this->complete_rom ? ROMSIZE : BANKSIZE;
        if(this->data.size() != required_size) throw std::runtime_error(
            this->complete_rom ? "A complete ROM image must be exactly 256 KiB"
                               : "A bank ROM image must be exactly 16 KiB");

        this->serial_interface->open_port();
        const uint16_t chip_id = this->serial_interface->get_chip_id();
        if(chip_id != 0xBFB6) {
            throw std::runtime_error(QStringLiteral("Unsupported flash chip 0x%1; expected SST39SF020 (0xBFB6)")
                .arg(chip_id, 4, 16, QLatin1Char('0')).toUpper().toStdString());
        }

        if(this->complete_rom) {
            qInfo() << "Erasing the complete SST39SF020";
            this->serial_interface->erase_chip();
        } else {
            qInfo() << "Erasing SST39SF020 bank" << this->bank_index;
            this->serial_interface->erase_bank(this->bank_index);
        }

        const unsigned int block_count = this->complete_rom ? NUMBLOCKS : BANKSIZE / BLOCKSIZE;
        const unsigned int first_block = this->complete_rom ? 0 : this->bank_index * (BANKSIZE / BLOCKSIZE);
        for(unsigned int block = 0; block < block_count; ++block) {
            emit(flash_block_start(block, block_count));
            this->serial_interface->burn_block(first_block + block,
                                               this->data.mid(block * BLOCKSIZE, BLOCKSIZE));
            emit(flash_block_done(block, block_count));
        }

        this->serial_interface->close_port();
        emit(flash_result_ready());
    } catch(const std::exception& e) {
        emit(thread_abort(QStringLiteral("Flash operation failed: %1").arg(e.what())));
        try { this->serial_interface->close_port(); } catch(...) {}
    }
}
