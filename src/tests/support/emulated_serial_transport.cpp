#include "emulated_serial_transport.h"

#include "romsizes.h"

#include <algorithm>

namespace {

bool parse_block(const QByteArray& suffix, unsigned int* block)
{
    if(suffix.size() != 4) return false;
    for(char ch : suffix) {
        if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    bool ok = false;
    *block = suffix.toUInt(&ok, 16);
    return ok && *block < NUMBLOCKS;
}

bool parse_bank(const QByteArray& suffix, unsigned int* bank)
{
    if(suffix.size() != 2) return false;
    for(char ch : suffix) {
        if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    bool ok = false;
    *bank = suffix.toUInt(&ok, 16);
    return ok && *bank < NUMBANKS;
}

QByteArray status(uint8_t value)
{
    return QByteArray(1, static_cast<char>(value));
}

} // namespace

EmulatedSerialTransport::EmulatedSerialTransport(
    const std::shared_ptr<FirmwareEmulatorBackend>& _backend,
    int _read_chunk_size)
    : backend(_backend), read_chunk_size(_read_chunk_size) {}

bool EmulatedSerialTransport::open() { this->open_state = true; return true; }

void EmulatedSerialTransport::close()
{
    this->open_state = false;
    this->write_buffer.clear();
    this->read_buffer.clear();
    this->ready_read_pending = false;
    this->pending_block_write = false;
}

bool EmulatedSerialTransport::isOpen() const { return this->open_state; }
void EmulatedSerialTransport::setDataTerminalReady(bool) {}
std::string EmulatedSerialTransport::errorString() const { return {}; }

qint64 EmulatedSerialTransport::write(const char* data, qint64 max_size)
{
    if(!this->open_state) return -1;
    this->write_buffer.append(data, static_cast<qsizetype>(max_size));
    this->process_write_buffer();
    return max_size;
}

qint64 EmulatedSerialTransport::write(const QByteArray& data)
{
    return this->write(data.constData(), data.size());
}

bool EmulatedSerialTransport::waitForBytesWritten(int) { return false; }

bool EmulatedSerialTransport::waitForReadyRead(int)
{
    if(!this->ready_read_pending) return false;
    this->ready_read_pending = false;
    return true;
}

QByteArray EmulatedSerialTransport::readAll()
{
    qsizetype count = this->read_buffer.size();
    if(this->read_chunk_size > 0) {
        count = std::min(count, static_cast<qsizetype>(this->read_chunk_size));
    }
    const QByteArray result = this->read_buffer.left(count);
    this->read_buffer.remove(0, count);
    if(!this->read_buffer.isEmpty()) this->ready_read_pending = true;
    return result;
}

qint64 EmulatedSerialTransport::bytesAvailable() const
{
    return this->read_chunk_size > 0 ? std::min(this->read_buffer.size(),
                                                static_cast<qsizetype>(this->read_chunk_size))
                                    : this->read_buffer.size();
}

void EmulatedSerialTransport::process_write_buffer()
{
    while(true) {
        if(this->pending_block_write) {
            if(this->write_buffer.size() < BLOCKSIZE + 2) return;
            const QByteArray payload = this->write_buffer.left(BLOCKSIZE + 2);
            this->write_buffer.remove(0, BLOCKSIZE + 2);
            this->pending_block_write = false;

            const QByteArray data = payload.left(BLOCKSIZE);
            const uint16_t received_crc =
                (static_cast<uint16_t>(static_cast<uint8_t>(payload[BLOCKSIZE])) << 8)
                | static_cast<uint8_t>(payload[BLOCKSIZE + 1]);
            if(received_crc != crc16_xmodem_emulator(data)) {
                this->queue_response(status(1));
            } else if(this->backend->chipId() != 0xBFB6) {
                this->queue_response(status(3));
            } else if(!this->backend->programRange(this->pending_block * BLOCKSIZE, data)) {
                this->queue_response(status(6));
            } else {
                this->queue_response(status(0));
            }
            continue;
        }

        if(this->write_buffer.size() < 8) return;
        const QByteArray command = this->write_buffer.left(8);
        this->write_buffer.remove(0, 8);
        this->backend->recordCommand(command.toStdString());
        this->queue_response(this->backend->commandEcho(command));

        if(command == "READINFO") {
            this->queue_response(this->backend->boardInfo());
        } else if(command == "DEVIDSST") {
            const uint16_t id = this->backend->chipId();
            QByteArray response;
            response.append(static_cast<char>(id >> 8));
            response.append(static_cast<char>(id));
            this->queue_response(response);
        } else if(command == "ERASEALL") {
            if(this->backend->chipId() != 0xBFB6) {
                this->queue_response(status(3));
            } else {
                this->backend->eraseRange(0, ROMSIZE);
                this->queue_response(status(0));
            }
        } else if(command == "BOOTLOAD") {
            this->queue_response(status(0));
        } else if(command.startsWith("RDBANK")) {
            unsigned int bank = 0;
            if(!parse_bank(command.mid(6), &bank)) {
                this->queue_response(status(2));
            } else if(this->backend->chipId() != 0xBFB6) {
                this->queue_response(status(3));
            } else {
                const QByteArray data = this->backend->readRange(bank * BANKSIZE, BANKSIZE);
                uint16_t crc = crc16_xmodem_emulator(data);
                if(this->backend->consumeCorruptNextReadCrc()) crc ^= 0x0001;
                QByteArray response = status(0) + data;
                response.append(static_cast<char>(crc >> 8));
                response.append(static_cast<char>(crc));
                this->queue_response(response);
            }
        } else if(command.startsWith("ERBANK")) {
            unsigned int bank = 0;
            if(!parse_bank(command.mid(6), &bank)) {
                this->queue_response(status(2));
            } else if(this->backend->chipId() != 0xBFB6) {
                this->queue_response(status(3));
            } else {
                this->backend->eraseRange(bank * BANKSIZE, BANKSIZE);
                this->queue_response(status(0));
            }
        } else if(command.startsWith("RDBK")) {
            unsigned int block = 0;
            if(!parse_block(command.mid(4), &block)) {
                this->queue_response(status(2));
            } else if(this->backend->chipId() != 0xBFB6) {
                this->queue_response(status(3));
            } else {
                const QByteArray data = this->backend->readRange(block * BLOCKSIZE, BLOCKSIZE);
                uint16_t crc = crc16_xmodem_emulator(data);
                if(this->backend->consumeCorruptNextReadCrc()) crc ^= 0x0001;
                QByteArray response = status(0) + data;
                response.append(static_cast<char>(crc >> 8));
                response.append(static_cast<char>(crc));
                this->queue_response(response);
            }
        } else if(command.startsWith("WRBK")) {
            unsigned int block = 0;
            if(!parse_block(command.mid(4), &block)) {
                this->queue_response(status(2));
            } else {
                this->pending_block = block;
                this->pending_block_write = true;
                this->queue_response(status(0));
            }
        } else {
            this->queue_response(QByteArray("ERRORCMD", 8));
        }
    }
}

void EmulatedSerialTransport::queue_response(const QByteArray& data)
{
    this->read_buffer += data;
    this->ready_read_pending = true;
}

FirmwareEmulatorBackend::FirmwareEmulatorBackend(const QByteArray& initial_flash,
                                                 const QByteArray& _board_info,
                                                 uint16_t device_id)
    : flash(initial_flash.left(ROMSIZE)), board_info(_board_info.left(P2000T_BOARD_INFO_LENGTH)), chip_id(device_id)
{
    if(this->flash.size() < ROMSIZE) {
        this->flash.append(QByteArray(ROMSIZE - this->flash.size(), static_cast<char>(0xFF)));
    }
    if(this->board_info.size() < P2000T_BOARD_INFO_LENGTH) {
        this->board_info.append(QByteArray(P2000T_BOARD_INFO_LENGTH - this->board_info.size(), '\0'));
    }
}

std::unique_ptr<SerialTransport> FirmwareEmulatorBackend::create_transport(int read_chunk_size)
{
    return std::make_unique<EmulatedSerialTransport>(shared_from_this(), read_chunk_size);
}

QByteArray FirmwareEmulatorBackend::boardInfo() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->board_info;
}

uint16_t FirmwareEmulatorBackend::chipId() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->chip_id;
}

QByteArray FirmwareEmulatorBackend::flashContents() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->flash;
}

QByteArray FirmwareEmulatorBackend::readRange(uint32_t offset, int length) const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->flash.mid(static_cast<int>(offset), length);
}

std::vector<std::string> FirmwareEmulatorBackend::commandHistory() const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->command_history;
}

void FirmwareEmulatorBackend::recordCommand(const std::string& command)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->command_history.push_back(command);
}

QByteArray FirmwareEmulatorBackend::commandEcho(const QByteArray& command)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    QByteArray result = command;
    if(this->corrupt_next_echo && !result.isEmpty()) {
        result[0] = result[0] == 'X' ? 'Y' : 'X';
        this->corrupt_next_echo = false;
    }
    return result;
}

void FirmwareEmulatorBackend::corruptNextEcho()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->corrupt_next_echo = true;
}

void FirmwareEmulatorBackend::corruptNextReadCrc()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->corrupt_next_read_crc = true;
}

bool FirmwareEmulatorBackend::consumeCorruptNextReadCrc()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    const bool result = this->corrupt_next_read_crc;
    this->corrupt_next_read_crc = false;
    return result;
}

bool FirmwareEmulatorBackend::programRange(uint32_t offset, const QByteArray& data)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    if(offset + static_cast<uint32_t>(data.size()) > static_cast<uint32_t>(this->flash.size())) return false;
    for(qsizetype i = 0; i < data.size(); ++i) {
        const uint8_t old_value = static_cast<uint8_t>(this->flash[static_cast<qsizetype>(offset) + i]);
        const uint8_t new_value = static_cast<uint8_t>(data[i]);
        if((old_value & new_value) != new_value) return false;
    }
    std::copy(data.begin(), data.end(), this->flash.begin() + static_cast<qsizetype>(offset));
    return true;
}

void FirmwareEmulatorBackend::eraseRange(uint32_t offset, int length)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    const qsizetype start = static_cast<qsizetype>(offset);
    const qsizetype count = std::min(static_cast<qsizetype>(length), this->flash.size() - start);
    std::fill_n(this->flash.begin() + start, count, static_cast<char>(0xFF));
}

uint16_t crc16_xmodem_emulator(const QByteArray& data)
{
    uint16_t crc = 0;
    for(char value : data) {
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(value)) << 8;
        for(int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}
