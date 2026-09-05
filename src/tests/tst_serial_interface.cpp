#include <QtTest/QtTest>

#include "config.h"
#include "serial_interface.h"
#include "firmwareupdater.h"
#include "support/emulated_serial_transport.h"
#include "support/fault_injecting_transport.h"

#include <memory>
#include <stdexcept>
#include <QCryptographicHash>

namespace {

uint32_t test_crc32(const QByteArray& data)
{
    uint32_t crc = 0xFFFFFFFFU;
    for(char value : data) {
        crc ^= static_cast<uint8_t>(value);
        for(int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

QByteArray hex_record(uint16_t address, const QByteArray& data)
{
    QByteArray record;
    record.append(static_cast<char>(data.size()));
    record.append(static_cast<char>(address >> 8));
    record.append(static_cast<char>(address));
    record.append('\0');
    record.append(data);
    uint8_t sum = 0;
    for(char value : record) sum = static_cast<uint8_t>(sum + static_cast<uint8_t>(value));
    record.append(static_cast<char>(0U - sum));
    return ':' + record.toHex().toUpper() + '\n';
}

QByteArray valid_firmware_hex(bool corrupt_application = false)
{
    QByteArray application = QByteArray::fromHex("0c940000");
    const uint32_t crc = test_crc32(application);
    if(corrupt_application) application[2] ^= 1;

    QByteArray manifest("P2FW", 4);
    manifest.append('\1');
    manifest.append('\1');
    manifest.append('\0');
    manifest.append(static_cast<char>(0xFF));
    for(int shift = 0; shift < 32; shift += 8)
        manifest.append(static_cast<char>(application.size() >> shift));
    for(int shift = 0; shift < 32; shift += 8)
        manifest.append(static_cast<char>(crc >> shift));

    return hex_record(0x0000, application) + hex_record(0x6FF0, manifest) + ":00000001FF\n";
}

} // namespace

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
    void times_out_when_a_response_is_lost();
    void validates_application_firmware_hex();
    void validates_release_checksum();
    void validates_built_firmware_image();
};

void SerialInterfaceTest::reads_board_info_and_chip_id()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    SerialInterface serial("emu", factory(backend));
    serial.open_port();
    QCOMPARE(QString::fromStdString(serial.get_board_info()), QString("P2000T-FW v") + PROGRAM_VERSION);
    QCOMPARE(serial.get_chip_id(), static_cast<uint16_t>(0xBFB6));
    serial.close_port();
    QVERIFY(backend->commandHistory() == std::vector<std::string>({"READINFO", "READVERS", "DEVIDSST"}));
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
    QCOMPARE(FirmwareUpdater::latest_release_url().toString(),
             QString("https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/"
                     "download/p2000t-programmable-cartridge-firmware.hex"));
    QCOMPARE(FirmwareUpdater::latest_checksums_url().toString(),
             QString("https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/"
                     "download/sha256sums.txt"));
    const QByteArray valid = valid_firmware_hex();
    const FirmwareImageInfo info = FirmwareUpdater::validate_application_hex(valid);
    QCOMPARE(info.data_bytes, 20);
    QCOMPARE(info.highest_address, static_cast<uint32_t>(0x7000));
    QCOMPARE(info.image_length, static_cast<uint32_t>(4));
    QCOMPARE(info.image_crc32, test_crc32(QByteArray::fromHex("0c940000")));

    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":01700000008F\n:00000001FF\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":0400000001020304F3\n:00000001FF\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        ":0400000001020304F2\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        hex_record(0x0000, QByteArray::fromHex("0c940000")) + ":00000001FF\n"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        valid_firmware_hex(true)), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::validate_application_hex(
        hex_record(0x0100, QByteArray(1, '\x42')) + valid), std::runtime_error);
}

void SerialInterfaceTest::validates_release_checksum()
{
    const QByteArray firmware("firmware payload");
    const QByteArray digest = QCryptographicHash::hash(firmware, QCryptographicHash::Sha256).toHex();
    FirmwareUpdater::verify_release_checksum(
        firmware, digest + "  p2000t-programmable-cartridge-firmware.hex\n");
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::verify_release_checksum(
        firmware, QByteArray(64, '0') + "  p2000t-programmable-cartridge-firmware.hex\n"),
        std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(FirmwareUpdater::verify_release_checksum(
        firmware, digest + "  another-file.hex\n"), std::runtime_error);
}

void SerialInterfaceTest::validates_built_firmware_image()
{
#ifdef P2000T_TEST_FIRMWARE_IMAGE
    QFile image(QStringLiteral(P2000T_TEST_FIRMWARE_IMAGE));
    QVERIFY2(image.open(QIODevice::ReadOnly), qPrintable(image.errorString()));
    const FirmwareImageInfo info = FirmwareUpdater::validate_application_hex(image.readAll());
    QVERIFY(info.image_length > 1024);
    QVERIFY(info.highest_address <= FirmwareUpdater::BOOTLOADER_START);
#else
    QSKIP("No built application firmware was available when CMake configured the test");
#endif
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

void SerialInterfaceTest::times_out_when_a_response_is_lost()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    SerialInterface serial("emu", [backend](const std::string&) {
        return std::make_unique<FaultInjectingTransport>(
            backend->create_transport(), "READINFO",
            FaultInjectingTransport::FaultMode::SuppressResponse);
    });
    serial.open_port();
    QElapsedTimer timer;
    timer.start();
    QVERIFY_EXCEPTION_THROWN(serial.get_board_info(), std::runtime_error);
    QVERIFY(timer.elapsed() >= 2500);
    QVERIFY(timer.elapsed() < 5000);
    serial.close_port();
}

QTEST_MAIN(SerialInterfaceTest)
#include "tst_serial_interface.moc"
