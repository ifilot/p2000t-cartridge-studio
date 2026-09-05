#ifndef FIRMWAREUPDATER_H
#define FIRMWAREUPDATER_H

#include <QByteArray>

#include <cstdint>

struct FirmwareImageInfo {
    int data_bytes = 0;
    uint32_t highest_address = 0;
};

/** Validation helpers for application-only ATmega32U4 Intel HEX images. */
class FirmwareUpdater {
public:
    static constexpr uint32_t BOOTLOADER_START = 0x7000;

    /**
     * Validate checksums, record structure and the protected bootloader bound.
     * Throws std::runtime_error when the image is unsafe or malformed.
     */
    static FirmwareImageInfo validate_application_hex(const QByteArray& contents);
};

#endif // FIRMWAREUPDATER_H
