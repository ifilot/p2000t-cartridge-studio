#include "readthread.h"

#include <stdexcept>

void ReadThread::set_bank_range(unsigned int _first_bank, unsigned int _bank_count)
{
    if(_bank_count == 0 || _first_bank >= NUMBANKS || _first_bank + _bank_count > NUMBANKS) {
        throw std::out_of_range("Requested SST39SF020 bank range is outside 0-15");
    }
    this->first_bank = _first_bank;
    this->bank_count = _bank_count;
}

void ReadThread::run()
{
    try {
        this->data.clear();
        this->data.reserve(static_cast<int>(this->bank_count * BANKSIZE));
        this->serial_interface->open_port();

        const uint16_t chip_id = this->serial_interface->get_chip_id();
        if(chip_id != 0xBFB6) {
            throw std::runtime_error(QStringLiteral("Unsupported flash chip 0x%1; expected SST39SF020 (0xBFB6)")
                .arg(chip_id, 4, 16, QLatin1Char('0')).toUpper().toStdString());
        }

        for(unsigned int offset = 0; offset < this->bank_count; ++offset) {
            emit(read_bank_start(offset, this->bank_count));
            this->data.append(this->serial_interface->read_bank(this->first_bank + offset));
            emit(read_bank_done(offset, this->bank_count));
        }

        this->serial_interface->close_port();
        emit(read_result_ready());
    } catch(const std::exception& e) {
        emit(thread_abort(QStringLiteral("Read operation failed: %1").arg(e.what())));
        try { this->serial_interface->close_port(); } catch(...) {}
    }
}
