#ifndef FIRMWARECOMPATIBILITY_H
#define FIRMWARECOMPATIBILITY_H

#include <QString>
#include <QStringList>

namespace FirmwareCompatibility {

/** Extract X.Y.Z from the fixed READINFO response, or return an empty string. */
QString version_from_board_info(const QString& board_info);

/** Firmware releases explicitly supported by a particular Studio release. */
QStringList supported_firmware_versions(const QString& studio_version);

/** Check one directional Studio-to-firmware compatibility matrix entry. */
bool is_supported(const QString& studio_version, const QString& firmware_version);

} // namespace FirmwareCompatibility

#endif // FIRMWARECOMPATIBILITY_H
