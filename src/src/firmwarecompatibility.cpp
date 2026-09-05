#include "firmwarecompatibility.h"

#include <QRegularExpression>

#include <array>

namespace {

struct CompatibilityPair {
    const char* studio_version;
    const char* firmware_version;
};

// This is intentionally explicit. Product versions identify releases; they do
// not by themselves determine whether the serial command set is compatible.
constexpr std::array<CompatibilityPair, 6> COMPATIBILITY_MATRIX{{
    {"0.1.0", "0.1.0"},
    {"0.1.1", "0.1.0"},
    {"0.1.1", "0.1.1"},
    {"0.1.2", "0.1.0"},
    {"0.1.2", "0.1.1"},
    {"0.1.2", "0.1.2"},
}};

} // namespace

namespace FirmwareCompatibility {

QString version_from_board_info(const QString& board_info)
{
    static const QString prefix = QStringLiteral("P2000T-FW v");
    static const QRegularExpression version_pattern(QStringLiteral("^[0-9]\\.[0-9]\\.[0-9]$"));

    if(!board_info.startsWith(prefix)) return {};
    const QString version = board_info.mid(prefix.size());
    return version_pattern.match(version).hasMatch() ? version : QString();
}

QStringList supported_firmware_versions(const QString& studio_version)
{
    QStringList versions;
    for(const CompatibilityPair& pair : COMPATIBILITY_MATRIX) {
        if(studio_version == QLatin1String(pair.studio_version)) {
            versions.append(QLatin1String(pair.firmware_version));
        }
    }
    return versions;
}

bool is_supported(const QString& studio_version, const QString& firmware_version)
{
    return supported_firmware_versions(studio_version).contains(firmware_version);
}

} // namespace FirmwareCompatibility
