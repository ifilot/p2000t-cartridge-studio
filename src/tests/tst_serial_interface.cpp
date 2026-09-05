#include <QtTest/QtTest>

#include "config.h"
#include "serial_interface.h"
#include "firmwareupdater.h"
#include "support/emulated_serial_transport.h"
#include "support/fault_injecting_transport.h"

#include <memory>
#include <stdexcept>

class SerialInterfaceTest : public QObject {
    Q_OBJECT

    static SerialInterface::TransportFactory factory(const std::shared_ptr<FirmwareEmulatorBackend>& backend,
                                                     int chunk_size = 0) {
        return [backend, chunk_size](const std::string&) { return backend->create_transport(chunk_size); };
    }

private slots:
    void reads_board_info_and_chip_id();
    void reads_crc_protected_block_in_fragments();
    void reads_crc_protected_bank_in_fragments();
    void writes_crc_protected_block();
    void erases_single_bank();
    void erases_complete_chip();
    void rejects_programming_without_erase();
    void validates_block_arguments();
    void rejects_corrupt_read_crc();
    void surfaces_status_and_transport_errors();
    void validates_application_firmware_hex();
};

void SerialInterfaceTest::reads_board_info_and_chip_id()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    QCOMPARE(QString::fromStdString(serial.get_board_info()), QString(P2000T_BOARD_INFO));
    QCOMPARE(serial.get_chip_id(), static_cast<uint16_t>(0xBFB6));
    serial.close_port();
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"READINFO", "DEVIDSST"}));
}

void SerialInterfaceTest::reads_crc_protected_block_in_fragments()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0xFF));
    for(int i = 0; i < BLOCKSIZE; ++i) flash[3 * BLOCKSIZE + i] = static_cast<char>(i);
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    SerialInterface serial("emu", factory(backend, 17));
    serial.open_port();
    QCOMPARE(serial.read_block(3), flash.mid(3 * BLOCKSIZE, BLOCKSIZE));
    serial.close_port();
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"RDBK0003"}));
}

void SerialInterfaceTest::reads_crc_protected_bank_in_fragments()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0xFF));
    for(int i = 0; i < BANKSIZE; ++i) flash[5 * BANKSIZE + i] = static_cast<char>(i * 11);
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    SerialInterface serial("emu", factory(backend, 17));
    serial.open_port();
    QCOMPARE(serial.read_bank(5), flash.mid(5 * BANKSIZE, BANKSIZE));
    serial.close_port();
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"RDBANK05"}));
}

void SerialInterfaceTest::writes_crc_protected_block()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    QByteArray block(BLOCKSIZE, '\0');
    for(int i = 0; i < block.size(); ++i) block[i] = static_cast<char>(i * 7);
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    serial.burn_block(0x12, block);
    QCOMPARE(serial.read_block(0x12), block);
    serial.close_port();
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"WRBK0012", "RDBK0012"}));
}

void SerialInterfaceTest::erases_single_bank()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    serial.erase_bank(7);
    serial.close_port();
    QCOMPARE(backend->flashContents().mid(7 * BANKSIZE, BANKSIZE),
             QByteArray(BANKSIZE, static_cast<char>(0xFF)));
    QCOMPARE(backend->flashContents().left(7 * BANKSIZE), QByteArray(7 * BANKSIZE, '\0'));
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"ERBANK07"}));
}

void SerialInterfaceTest::erases_complete_chip()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    serial.erase_chip();
    serial.close_port();
    QCOMPARE(backend->flashContents(), QByteArray(ROMSIZE, static_cast<char>(0xFF)));
}

void SerialInterfaceTest::rejects_programming_without_erase()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    QVERIFY_EXCEPTION_THROWN(serial.burn_block(0, QByteArray(BLOCKSIZE, static_cast<char>(0xFF))), std::runtime_error);
    serial.close_port();
}

void SerialInterfaceTest::validates_block_arguments()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    QVERIFY_EXCEPTION_THROWN(serial.read_block(NUMBLOCKS), std::out_of_range);
    QVERIFY_EXCEPTION_THROWN(serial.read_bank(NUMBANKS), std::out_of_range);
    QVERIFY_EXCEPTION_THROWN(serial.erase_bank(NUMBANKS), std::out_of_range);
    QVERIFY_EXCEPTION_THROWN(serial.burn_block(0, QByteArray(12, '\0')), std::invalid_argument);
    serial.close_port();
}

void SerialInterfaceTest::validates_application_firmware_hex()
{
    const QByteArray valid =
        ":020000040000FA\n"
        ":0400000001020304F2\n"
        ":00000001FF\n";
    const FirmwareImageInfo info = FirmwareUpdater::validate_application_hex(valid);
    QCOMPARE(info.data_bytes, 4);
    QCOMPARE(info.highest_address, static_cast<uint32_t>(4));

    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":01700000008F\n:00000001FF\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":0400000001020304F3\n:00000001FF\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":0400000001020304F2\n"), std::runtime_error);
}

void SerialInterfaceTest::rejects_corrupt_read_crc()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    backend->corruptNextReadCrc();
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    QVERIFY_EXCEPTION_THROWN(serial.read_bank(0), std::runtime_error);
    serial.close_port();
    QCOMPARE(crc16_xmodem_emulator(QByteArray("123456789")), static_cast<uint16_t>(0x31C3));
}

void SerialInterfaceTest::surfaces_status_and_transport_errors()
{
    {
        auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(), QByteArray(P2000T_BOARD_INFO), 0xBFB5);
        SerialInterface serial("emu", factory(backend));
        serial.open_port();
        QVERIFY_EXCEPTION_THROWN(serial.erase_chip(), std::runtime_error);
        serial.close_port();
    }
    {
        auto backend = std::make_shared<FirmwareEmulatorBackend>();
        backend->corruptNextEcho();
        SerialInterface serial("emu", factory(backend));
        serial.open_port();
        QVERIFY_EXCEPTION_THROWN(serial.get_board_info(), std::runtime_error);
        serial.close_port();
    }
}

QTEST_MAIN(SerialInterfaceTest)
#include "tst_serial_interface.moc"
