#include "serial_interface.h"
#include "serial_transport_qt.h"
#include "version.h"

#include <QElapsedTimer>
#include <QDebug>

#include <stdexcept>
#include <utility>

namespace {

QString status_description(uint8_t status)
{
    switch(status) {
        case 0: return QStringLiteral("success");
        case 1: return QStringLiteral("CRC mismatch");
        case 2: return QStringLiteral("invalid block or bank address");
        case 3: return QStringLiteral("wrong flash chip (expected SST39SF020, BF B6)");
        case 4: return QStringLiteral("flash operation timed out");
        case 5: return QStringLiteral("flash verification failed");
        case 6: return QStringLiteral("block needs a chip erase before programming");
        case 7: return QStringLiteral("write payload timed out; reconnect the cartridge");
        default: return QStringLiteral("unknown status");
    }
}

} // namespace

SerialInterface::SerialInterface(const std::string& _portname,
                                 TransportFactory _transport_factory)
    : portname(_portname),
      transport_factory(std::move(_transport_factory))
{
    if(!this->transport_factory) {
        this->transport_factory = [](const std::string& name) {
            return std::make_unique<QtSerialTransport>(name);
        };
    }
}

void SerialInterface::open_port()
{
    if(this->portname.empty()) {
        throw std::runtime_error("No port has been set");
    }
    if(this->port && this->port->isOpen()) {
        return;
    }

    this->port = this->transport_factory(this->portname);
    if(!this->port || !this->port->open()) {
        const std::string detail = this->port ? this->port->errorString() : "transport was not created";
        throw std::runtime_error("Failed to open COM port " + this->portname + ": " + detail);
    }

    this->port->setDataTerminalReady(true);
    qDebug() << "Opening COM port:" << QString::fromStdString(this->portname);
}

void SerialInterface::close_port()
{
    if(this->port) {
        if(this->port->isOpen()) {
            this->port->setDataTerminalReady(false);
            this->port->close();
        }
        this->port.reset();
    }
    this->receive_buffer.clear();
    qDebug() << "Closing COM port:" << QString::fromStdString(this->portname);
}

std::string SerialInterface::get_board_info()
{
    static_assert(sizeof(P2000T_BOARD_INFO) - 1 == P2000T_BOARD_INFO_LENGTH,
                  "READINFO board identity must remain exactly 16 bytes");
    this->flush_buffer();
    const QByteArray response = this->send_command_capture_response("READINFO", P2000T_BOARD_INFO_LENGTH);
    if(response.startsWith("P2000T-FW v")) {
        return response.toStdString();
    }
    if(response != QByteArrayLiteral(P2000T_BOARD_INFO)) {
        throw std::runtime_error("The selected port is not a P2000T cartridge (unexpected READINFO response)");
    }
    const QByteArray version = this->send_command_capture_response("READVERS", 3);
    return QStringLiteral("P2000T-FW v%1.%2.%3")
        .arg(static_cast<uint8_t>(version[0]))
        .arg(static_cast<uint8_t>(version[1]))
        .arg(static_cast<uint8_t>(version[2])).toStdString();
}

uint16_t SerialInterface::get_chip_id()
{
    const QByteArray response = this->send_command_capture_response("DEVIDSST", 2);
    return (static_cast<uint16_t>(static_cast<uint8_t>(response[0])) << 8)
         | static_cast<uint8_t>(response[1]);
}

QByteArray SerialInterface::read_block(unsigned int block_index)
{
    if(block_index >= NUMBLOCKS) {
        throw std::out_of_range("SST39SF020 block index is outside 0000-03FF");
    }

    const std::string command = QStringLiteral("RDBK%1")
        .arg(block_index, 4, 16, QLatin1Char('0')).toUpper().toStdString();
    this->send_command(command);

    const uint8_t status = static_cast<uint8_t>(this->wait_for_response(1)[0]);
    this->require_success(status, command);

    const QByteArray response = this->wait_for_response(BLOCKSIZE + 2);
    const QByteArray data = response.left(BLOCKSIZE);
    const uint16_t received_crc =
        (static_cast<uint16_t>(static_cast<uint8_t>(response[BLOCKSIZE])) << 8)
        | static_cast<uint8_t>(response[BLOCKSIZE + 1]);
    const uint16_t expected_crc = crc16_xmodem(data);
    if(received_crc != expected_crc) {
        throw std::runtime_error(QStringLiteral("CRC mismatch reading block %1: received %2, expected %3")
            .arg(block_index, 4, 16, QLatin1Char('0'))
            .arg(received_crc, 4, 16, QLatin1Char('0'))
            .arg(expected_crc, 4, 16, QLatin1Char('0')).toUpper().toStdString());
    }
    return data;
}

QByteArray SerialInterface::read_bank(unsigned int bank_index)
{
    if(bank_index >= NUMBANKS) {
        throw std::out_of_range("SST39SF020 bank index is outside 00-0F");
    }

    const std::string command = QStringLiteral("RDBANK%1")
        .arg(bank_index, 2, 16, QLatin1Char('0')).toUpper().toStdString();
    this->send_command(command);

    const uint8_t status = static_cast<uint8_t>(this->wait_for_response(1)[0]);
    this->require_success(status, command);

    const QByteArray response = this->wait_for_response(BANKSIZE + 2, BANK_READ_TIMEOUT_MS);
    const QByteArray data = response.left(BANKSIZE);
    const uint16_t received_crc =
        (static_cast<uint16_t>(static_cast<uint8_t>(response[BANKSIZE])) << 8)
        | static_cast<uint8_t>(response[BANKSIZE + 1]);
    const uint16_t expected_crc = crc16_xmodem(data);
    if(received_crc != expected_crc) {
        throw std::runtime_error(QStringLiteral("CRC mismatch reading bank %1: received %2, expected %3")
            .arg(bank_index, 2, 16, QLatin1Char('0'))
            .arg(received_crc, 4, 16, QLatin1Char('0'))
            .arg(expected_crc, 4, 16, QLatin1Char('0')).toUpper().toStdString());
    }
    return data;
}

void SerialInterface::burn_block(unsigned int block_index, const QByteArray& data)
{
    if(block_index >= NUMBLOCKS) {
        throw std::out_of_range("SST39SF020 block index is outside 0000-03FF");
    }
    if(data.size() != BLOCKSIZE) {
        throw std::invalid_argument("A protocol write payload must contain exactly 256 bytes");
    }

    const std::string command = QStringLiteral("WRBK%1")
        .arg(block_index, 4, 16, QLatin1Char('0')).toUpper().toStdString();
    this->send_command(command);
    this->require_success(static_cast<uint8_t>(this->wait_for_response(1)[0]), command + " ready");

    QByteArray payload = data;
    const uint16_t crc = crc16_xmodem(data);
    payload.append(static_cast<char>((crc >> 8) & 0xFF));
    payload.append(static_cast<char>(crc & 0xFF));
    this->write_bytes(payload);

    this->require_success(static_cast<uint8_t>(this->wait_for_response(1)[0]), command);
}

void SerialInterface::erase_bank(unsigned int bank_index)
{
    if(bank_index >= NUMBANKS) {
        throw std::out_of_range("SST39SF020 bank index is outside 00-0F");
    }
    const std::string command = QStringLiteral("ERBANK%1")
        .arg(bank_index, 2, 16, QLatin1Char('0')).toUpper().toStdString();
    this->send_command(command);
    this->require_success(static_cast<uint8_t>(this->wait_for_response(1, ERASE_TIMEOUT_MS)[0]),
                          command);
}

void SerialInterface::erase_chip()
{
    this->send_command("ERASEALL");
    this->require_success(static_cast<uint8_t>(this->wait_for_response(1, ERASE_TIMEOUT_MS)[0]),
                          "ERASEALL");
}

void SerialInterface::enter_bootloader()
{
    this->send_command("BOOTLOAD");
    this->require_success(static_cast<uint8_t>(this->wait_for_response(1)[0]), "BOOTLOAD");
}

void SerialInterface::send_command(const std::string& command)
{
    if(!this->port || !this->port->isOpen()) {
        throw std::runtime_error("Serial port is not open");
    }
    if(command.size() != COMMAND_SIZE) {
        throw std::invalid_argument("Protocol commands must contain exactly eight bytes");
    }

    this->write_bytes(QByteArray(command.data(), COMMAND_SIZE));
    const QByteArray echo = this->wait_for_response(COMMAND_SIZE);
    if(echo != QByteArray(command.data(), COMMAND_SIZE)) {
        throw std::runtime_error("Invalid command echo received for " + command);
    }
}

QByteArray SerialInterface::send_command_capture_response(const std::string& command,
                                                          int response_size,
                                                          int timeout_ms)
{
    this->send_command(command);
    return this->wait_for_response(response_size, timeout_ms);
}

void SerialInterface::write_bytes(const QByteArray& data, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();
    qsizetype offset = 0;
    while(offset < data.size()) {
        const qint64 written = this->port->write(data.constData() + offset,
                                                data.size() - offset);
        if(written < 0) {
            throw std::runtime_error("Serial write failed: " + this->port->errorString());
        }
        offset += written;
        if(written == 0 && !this->port->waitForBytesWritten(25) &&
           timer.elapsed() >= timeout_ms) {
            throw std::runtime_error("Serial write timed out while queueing the payload");
        }
    }

    while(this->port->bytesToWrite() > 0) {
        const int remaining = timeout_ms - static_cast<int>(timer.elapsed());
        if(remaining <= 0 || !this->port->waitForBytesWritten(qMin(remaining, 25))) {
            if(timer.elapsed() >= timeout_ms) {
                throw std::runtime_error("Serial write timed out before the payload was transmitted");
            }
        }
    }
}

QByteArray SerialInterface::wait_for_response(int size, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();

    while(this->receive_buffer.size() < size && timer.elapsed() < timeout_ms) {
        this->receive_buffer += this->port->readAll();
        if(this->receive_buffer.size() >= size) {
            break;
        }
        const int remaining = timeout_ms - static_cast<int>(timer.elapsed());
        this->port->waitForReadyRead(qMin(remaining, 25));
    }

    if(this->receive_buffer.size() < size) {
        this->receive_buffer += this->port->readAll();
    }
    if(this->receive_buffer.size() < size) {
        throw std::runtime_error(QStringLiteral("Serial response timed out (%1 of %2 bytes received)")
                                 .arg(this->receive_buffer.size()).arg(size).toStdString());
    }

    const QByteArray response = this->receive_buffer.left(size);
    this->receive_buffer.remove(0, size);
    return response;
}

void SerialInterface::flush_buffer()
{
    if(!this->port || !this->port->isOpen()) {
        return;
    }
    QByteArray discarded = this->receive_buffer;
    this->receive_buffer.clear();
    discarded += this->port->readAll();
    while(this->port->waitForReadyRead(10)) {
        discarded += this->port->readAll();
    }
    if(!discarded.isEmpty()) {
        qDebug() << "Discarded stale serial data:" << discarded.toHex(' ');
    }
}

void SerialInterface::require_success(uint8_t status, const std::string& operation) const
{
    if(status == 0) {
        return;
    }
    throw std::runtime_error(QStringLiteral("%1 failed with status %2 (%3)")
        .arg(QString::fromStdString(operation))
        .arg(status)
        .arg(status_description(status)).toStdString());
}

uint16_t SerialInterface::crc16_xmodem(const QByteArray& data)
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
