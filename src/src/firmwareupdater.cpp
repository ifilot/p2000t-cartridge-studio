#include "firmwareupdater.h"

#include <QList>
#include <QBitArray>
#include <QCryptographicHash>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {

uint8_t byte_at(const QByteArray& data, int offset)
{
    return static_cast<uint8_t>(data[offset]);
}

uint32_t little_u32(const QByteArray& data, int offset)
{
    return static_cast<uint32_t>(byte_at(data, offset))
        | (static_cast<uint32_t>(byte_at(data, offset + 1)) << 8)
        | (static_cast<uint32_t>(byte_at(data, offset + 2)) << 16)
        | (static_cast<uint32_t>(byte_at(data, offset + 3)) << 24);
}

uint32_t crc32(const QByteArray& data)
{
    uint32_t crc = 0xFFFFFFFFU;
    for(char value : data) {
        crc ^= static_cast<uint8_t>(value);
        for(int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

[[noreturn]] void malformed(int line, const char* detail)
{
    throw std::runtime_error("Invalid Intel HEX at line " + std::to_string(line) + ": " + detail);
}

} // namespace

QUrl FirmwareUpdater::latest_release_url()
{
    return QUrl(QStringLiteral(
        "https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/"
        "p2000t-programmable-cartridge-firmware.hex"));
}

QUrl FirmwareUpdater::latest_checksums_url()
{
    return QUrl(QStringLiteral(
        "https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/"
        "sha256sums.txt"));
}

void FirmwareUpdater::verify_release_checksum(const QByteArray& firmware,
                                              const QByteArray& checksum_file)
{
    static const QString filename = QStringLiteral("p2000t-programmable-cartridge-firmware.hex");
    const QString contents = QString::fromUtf8(checksum_file);
    const QRegularExpression entry(
        QStringLiteral("(?:^|\\n)([0-9A-Fa-f]{64})[ \\t]+\\*?%1(?:\\r?\\n|$)")
            .arg(QRegularExpression::escape(filename)));
    const QRegularExpressionMatch match = entry.match(contents);
    if(!match.hasMatch()) {
        throw std::runtime_error("Release checksum file has no entry for the application firmware");
    }
    const QByteArray expected = match.captured(1).toLatin1().toLower();
    const QByteArray actual = QCryptographicHash::hash(firmware, QCryptographicHash::Sha256).toHex();
    if(actual != expected) {
        throw std::runtime_error(QStringLiteral("Release SHA-256 mismatch: expected %1, calculated %2")
            .arg(QString::fromLatin1(expected), QString::fromLatin1(actual)).toStdString());
    }
}

FirmwareImageInfo FirmwareUpdater::validate_application_hex(const QByteArray& contents)
{
    FirmwareImageInfo info;
    uint32_t base_address = 0;
    bool eof_seen = false;
    int line_number = 0;
    QByteArray memory(static_cast<qsizetype>(BOOTLOADER_START), static_cast<char>(0xFF));
    QBitArray occupied(static_cast<qsizetype>(BOOTLOADER_START));

    for(QByteArray line : contents.split('\n')) {
        ++line_number;
        line = line.trimmed();
        if(line.isEmpty()) continue;
        if(eof_seen) malformed(line_number, "data follows the end-of-file record");
        if(line[0] != ':' || ((line.size() - 1) % 2) != 0) {
            malformed(line_number, "bad record framing");
        }
        for(int i = 1; i < line.size(); ++i) {
            const char ch = line[i];
            if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
                 (ch >= 'a' && ch <= 'f'))) {
                malformed(line_number, "record contains a non-hexadecimal character");
            }
        }

        const QByteArray record = QByteArray::fromHex(line.mid(1));
        if(record.size() < 5 || record.size() != byte_at(record, 0) + 5) {
            malformed(line_number, "record length does not match its byte count");
        }
        uint8_t checksum = 0;
        for(char value : record) checksum = static_cast<uint8_t>(checksum + static_cast<uint8_t>(value));
        if(checksum != 0) malformed(line_number, "checksum mismatch");

        const uint8_t count = byte_at(record, 0);
        const uint16_t address = (static_cast<uint16_t>(byte_at(record, 1)) << 8) | byte_at(record, 2);
        const uint8_t type = byte_at(record, 3);

        switch(type) {
            case 0x00: {
                const uint64_t start = static_cast<uint64_t>(base_address) + address;
                const uint64_t end = start + count;
                if(end > BOOTLOADER_START) {
                    throw std::runtime_error("Firmware data reaches the protected bootloader at 0x7000");
                }
                if(info.data_bytes > std::numeric_limits<int>::max() - count) {
                    malformed(line_number, "image is too large");
                }
                info.data_bytes += count;
                info.highest_address = std::max(info.highest_address, static_cast<uint32_t>(end));
                for(uint32_t offset = 0; offset < count; ++offset) {
                    const uint32_t absolute = static_cast<uint32_t>(start + offset);
                    if(occupied.testBit(static_cast<qsizetype>(absolute))) {
                        malformed(line_number, "data record overlaps an earlier record");
                    }
                    occupied.setBit(static_cast<qsizetype>(absolute));
                    memory[static_cast<qsizetype>(absolute)] = record[4 + static_cast<int>(offset)];
                }
                break;
            }
            case 0x01:
                if(count != 0 || address != 0) malformed(line_number, "bad end-of-file record");
                eof_seen = true;
                break;
            case 0x02:
                if(count != 2 || address != 0) malformed(line_number, "bad extended-segment record");
                base_address = static_cast<uint32_t>(
                    (static_cast<uint16_t>(byte_at(record, 4)) << 8) | byte_at(record, 5)) << 4;
                break;
            case 0x04:
                if(count != 2 || address != 0) malformed(line_number, "bad extended-linear record");
                base_address = static_cast<uint32_t>(
                    (static_cast<uint16_t>(byte_at(record, 4)) << 8) | byte_at(record, 5)) << 16;
                break;
            case 0x03:
            case 0x05:
                if(count != 4 || address != 0) malformed(line_number, "bad start-address record");
                break;
            default:
                malformed(line_number, "unsupported record type");
        }
    }

    if(!eof_seen) throw std::runtime_error("Intel HEX image has no end-of-file record");
    if(info.data_bytes == 0) throw std::runtime_error("Intel HEX image contains no application data");
    if(!occupied.testBit(0) || !occupied.testBit(1) ||
       (byte_at(memory, 0) == 0xFF && byte_at(memory, 1) == 0xFF)) {
        throw std::runtime_error("Intel HEX image has no valid reset vector at address 0x0000");
    }

    const QByteArray manifest = memory.mid(MANIFEST_ADDRESS, MANIFEST_SIZE);
    for(int offset = 0; offset < MANIFEST_SIZE; ++offset) {
        if(!occupied.testBit(MANIFEST_ADDRESS + offset)) {
            throw std::runtime_error("Firmware application manifest is incomplete");
        }
    }
    if(manifest.left(4) != QByteArrayLiteral("P2FW") || byte_at(manifest, 4) != 1) {
        throw std::runtime_error("Firmware image has no supported P2000T application manifest");
    }
    if(byte_at(manifest, 5) != 1 || byte_at(manifest, 6) != 0) {
        throw std::runtime_error("Firmware image uses an unsupported cartridge protocol version");
    }
    info.image_length = little_u32(manifest, 8);
    info.image_crc32 = little_u32(manifest, 12);
    if(info.image_length < 2 || info.image_length > MANIFEST_ADDRESS) {
        throw std::runtime_error("Firmware manifest contains an invalid application length");
    }
    for(uint32_t address = info.image_length; address < MANIFEST_ADDRESS; ++address) {
        if(byte_at(memory, static_cast<int>(address)) != 0xFF) {
            throw std::runtime_error("Firmware contains data outside its CRC-protected application length");
        }
    }
    const uint32_t actual_crc = crc32(memory.left(static_cast<qsizetype>(info.image_length)));
    if(actual_crc != info.image_crc32) {
        throw std::runtime_error(QStringLiteral("Firmware CRC32 mismatch: expected %1, calculated %2")
            .arg(info.image_crc32, 8, 16, QLatin1Char('0'))
            .arg(actual_crc, 8, 16, QLatin1Char('0')).toUpper().toStdString());
    }
    return info;
}
