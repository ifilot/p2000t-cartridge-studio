#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include <QByteArray>
#include <QString>

#include "romsizes.h"
#include "serial_transport.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/** Host-side implementation of the version-matched P2000T cartridge USB CDC protocol. */
class SerialInterface {
public:
    using TransportFactory = std::function<std::unique_ptr<SerialTransport>(const std::string&)>;

    SerialInterface(const std::string& portname,
                    TransportFactory transport_factory = nullptr);

    const std::string& get_port() const { return this->portname; }

    void open_port();
    void close_port();

    std::string get_board_info();
    uint16_t get_chip_id();
    QByteArray read_block(unsigned int block_index);
    QByteArray read_bank(unsigned int bank_index);
    void burn_block(unsigned int block_index, const QByteArray& data);
    void erase_bank(unsigned int bank_index);
    void erase_chip();
    void enter_bootloader();

private:
    static constexpr int COMMAND_SIZE = 8;
    static constexpr int DEFAULT_TIMEOUT_MS = 3000;
    static constexpr int BANK_READ_TIMEOUT_MS = 5000;
    static constexpr int ERASE_TIMEOUT_MS = 5000;

    std::string portname;
    std::unique_ptr<SerialTransport> port;
    TransportFactory transport_factory;
    QByteArray receive_buffer;

    void send_command(const std::string& command);
    QByteArray send_command_capture_response(const std::string& command,
                                             int response_size,
                                             int timeout_ms = DEFAULT_TIMEOUT_MS);
    void write_bytes(const QByteArray& data, int timeout_ms = DEFAULT_TIMEOUT_MS);
    QByteArray wait_for_response(int size, int timeout_ms = DEFAULT_TIMEOUT_MS);
    void flush_buffer();
    void require_success(uint8_t status, const std::string& operation) const;
    static uint16_t crc16_xmodem(const QByteArray& data);
};

#endif // SERIAL_INTERFACE_H
