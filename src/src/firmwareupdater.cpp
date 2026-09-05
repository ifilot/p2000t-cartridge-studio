#include "firmwareupdater.h"

#include <QList>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {

uint8_t byte_at(const QByteArray& data, int offset)
{
    return static_cast<uint8_t>(data[offset]);
}

[[noreturn]] void malformed(int line, const char* detail)
{
    throw std::runtime_error("Invalid Intel HEX at line " + std::to_string(line) + ": " + detail);
}

} // namespace

FirmwareImageInfo FirmwareUpdater::validate_application_hex(const QByteArray& contents)
{
    FirmwareImageInfo info;
    uint32_t base_address = 0;
    bool eof_seen = false;
    int line_number = 0;

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
    return info;
}
