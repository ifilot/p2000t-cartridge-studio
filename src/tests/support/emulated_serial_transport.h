#ifndef EMULATED_SERIAL_TRANSPORT_H
#define EMULATED_SERIAL_TRANSPORT_H

#include "config.h"
#include "serial_transport.h"

#include <QByteArray>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class FirmwareEmulatorBackend;

/** In-memory emulator for the versioned cartridge firmware protocol. */
class EmulatedSerialTransport : public SerialTransport {
public:
    explicit EmulatedSerialTransport(const std::shared_ptr<FirmwareEmulatorBackend>& backend,
                                     int read_chunk_size = 0);

    bool open() override;
    void close() override;
    bool isOpen() const override;
    void setDataTerminalReady(bool ready) override;
    std::string errorString() const override;
    qint64 write(const char* data, qint64 max_size) override;
    qint64 write(const QByteArray& data) override;
    bool waitForBytesWritten(int msecs) override;
    bool waitForReadyRead(int msecs) override;
    QByteArray readAll() override;
    qint64 bytesAvailable() const override;

private:
    std::shared_ptr<FirmwareEmulatorBackend> backend;
    bool open_state = false;
    QByteArray write_buffer;
    QByteArray read_buffer;
    bool ready_read_pending = false;
    int read_chunk_size = 0;
    bool pending_block_write = false;
    unsigned int pending_block = 0;

    void process_write_buffer();
    void queue_response(const QByteArray& data);
};

class FirmwareEmulatorBackend : public std::enable_shared_from_this<FirmwareEmulatorBackend> {
public:
    explicit FirmwareEmulatorBackend(const QByteArray& initial_flash = QByteArray(),
                                     const QByteArray& board_id = QByteArray(P2000T_BOARD_INFO),
                                     uint16_t device_id = 0xBFB6);

    std::unique_ptr<SerialTransport> create_transport(int read_chunk_size = 0);
    QByteArray boardInfo() const;
    uint16_t chipId() const;
    QByteArray flashContents() const;
    QByteArray readRange(uint32_t offset, int length) const;
    std::vector<std::string> commandHistory() const;
    void recordCommand(const std::string& command);
    QByteArray commandEcho(const QByteArray& command);
    void corruptNextEcho();
    void corruptNextReadCrc();
    bool consumeCorruptNextReadCrc();
    bool programRange(uint32_t offset, const QByteArray& data);
    void eraseRange(uint32_t offset, int length);

private:
    mutable std::mutex mutex;
    QByteArray flash;
    QByteArray board_info;
    uint16_t chip_id = 0xBFB6;
    std::vector<std::string> command_history;
    bool corrupt_next_echo = false;
    bool corrupt_next_read_crc = false;
};

uint16_t crc16_xmodem_emulator(const QByteArray& data);

#endif // EMULATED_SERIAL_TRANSPORT_H
