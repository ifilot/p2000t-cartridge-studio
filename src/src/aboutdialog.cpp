#include "aboutdialog.h"

#include "config.h"
#include "firmwarecompatibility.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QLabel *selectable_value(const QString& text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QString compatible_firmware_text()
{
    const QStringList versions = FirmwareCompatibility::supported_firmware_versions(
        QString::fromLatin1(PROGRAM_VERSION));
    return versions.isEmpty() ? QStringLiteral("none") : versions.join(QStringLiteral(", "));
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("aboutDialog");
    setWindowTitle(tr("About %1").arg(PROGRAM_NAME));
    setWindowIcon(QIcon(PROGRAM_ICON));
    setModal(true);
    setMinimumWidth(620);
    resize(720, 680);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(14);

    auto *header_layout = new QHBoxLayout();
    auto *icon_label = new QLabel(this);
    icon_label->setObjectName("aboutApplicationIcon");
    constexpr int about_icon_size = 112;
    icon_label->setPixmap(QPixmap(PROGRAM_ICON).scaled(
        about_icon_size,
        about_icon_size,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
    icon_label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    icon_label->setFixedWidth(about_icon_size + 16);
    header_layout->addWidget(icon_label);

    auto *title_layout = new QVBoxLayout();
    auto *title_label = new QLabel(
        tr("<span style=\"font-size: 18pt; font-weight: bold;\">%1</span><br>"
           "<span style=\"font-size: 11pt;\">Version %2</span>")
            .arg(PROGRAM_NAME, PROGRAM_VERSION),
        this);
    title_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title_layout->addWidget(title_label);

    auto *description_label = new QLabel(
        tr("Manage the SST39SF020 flash memory in an ATmega32U4-based "
           "programmable data cartridge for the Philips P2000T."),
        this);
    description_label->setWordWrap(true);
    title_layout->addWidget(description_label);
    header_layout->addLayout(title_layout, 1);
    main_layout->addLayout(header_layout);

    auto *links_label = new QLabel(
        tr("<a href=\"https://github.com/ifilot/p2000t-cartridge-studio\">Source code</a>"
           " &nbsp;&middot;&nbsp; "
           "<a href=\"https://github.com/ifilot/p2000t-cartridge-studio/releases\">Releases</a>"
           " &nbsp;&middot;&nbsp; "
           "<a href=\"https://github.com/ifilot/p2000t-cartridge-studio/issues\">Report a problem</a>"),
        this);
    links_label->setObjectName("aboutLinks");
    links_label->setAlignment(Qt::AlignHCenter);
    links_label->setOpenExternalLinks(true);
    links_label->setWordWrap(true);
    main_layout->addWidget(links_label);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    main_layout->addWidget(separator);

    auto *cartridge_group = new QGroupBox(tr("Supported cartridge"), this);
    cartridge_group->setObjectName("aboutCartridgeGroup");
    auto *cartridge_layout = new QFormLayout(cartridge_group);
    cartridge_layout->addRow(
        tr("Controller:"), selectable_value(tr("ATmega32U4 with native USB serial"), cartridge_group));
    cartridge_layout->addRow(
        tr("Flash memory:"), selectable_value(tr("SST39SF020, 256 KiB (device ID BF B6)"), cartridge_group));
    cartridge_layout->addRow(
        tr("ROM organization:"), selectable_value(tr("16 banks of 16 KiB"), cartridge_group));
    cartridge_layout->addRow(
        tr("USB identifiers:"), selectable_value(tr("Application 03EB:2044; bootloader 03EB:204A"), cartridge_group));
    cartridge_layout->addRow(
        tr("Firmware compatibility:"),
        selectable_value(tr("Studio %1 supports firmware %2")
                             .arg(PROGRAM_VERSION, compatible_firmware_text()),
                         cartridge_group));
    main_layout->addWidget(cartridge_group);

    auto *build_group = new QGroupBox(tr("Software and build information"), this);
    build_group->setObjectName("aboutBuildGroup");
    auto *build_layout = new QFormLayout(build_group);
    build_layout->addRow(tr("Application version:"), selectable_value(PROGRAM_VERSION, build_group));
    build_layout->addRow(tr("Git revision:"), selectable_value(GIT_HASH, build_group));
    build_layout->addRow(tr("Build date:"), selectable_value(QStringLiteral(__DATE__), build_group));
    build_layout->addRow(tr("Qt runtime:"), selectable_value(QString::fromLatin1(qVersion()), build_group));
    main_layout->addWidget(build_group);

    auto *credits = new QTextBrowser(this);
    credits->setObjectName("aboutCredits");
    credits->setOpenExternalLinks(true);
    credits->setReadOnly(true);
    credits->setMinimumHeight(145);
    credits->setHtml(
        tr("<p><b>Developed by Ivo Filot</b><br>"
           "<a href=\"mailto:ivo@ivofilot.nl\">ivo@ivofilot.nl</a><br>"
           "Copyright &copy; 2023&ndash;2026 Ivo Filot</p>"
           "<p>The project-owned application, firmware, bootloader, and build tools "
           "are free software distributed under the "
           "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU GPL v3</a>. "
           "This application uses the "
           "<a href=\"https://www.qt.io/licensing/open-source-lgpl-obligations\">Qt 6 framework</a>. "
           "Bundled third-party software and artwork retain their respective licences; "
           "notices are included in the source and binary distributions.</p>"));
    main_layout->addWidget(credits);

    auto *button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy_button = button_box->addButton(tr("Copy system information"),
                                               QDialogButtonBox::ActionRole);
    copy_button->setObjectName("copySystemInformationButton");
    connect(copy_button, &QPushButton::clicked, this, [this, copy_button]() {
        QApplication::clipboard()->setText(system_information());
        copy_button->setText(tr("Copied"));
    });
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(button_box);
}

QString AboutDialog::system_information() const
{
    return tr("%1 %2\n"
              "Compatible firmware versions: %8\n"
              "Cartridge controller: ATmega32U4\n"
              "Flash memory: SST39SF020, 256 KiB (16 x 16 KiB)\n"
              "Application USB ID: 03EB:2044\n"
              "Bootloader USB ID: 03EB:204A\n"
              "Git revision: %3\n"
              "Build date: %4\n"
              "Qt version: %5\n"
              "Operating system: %6\n"
              "CPU architecture: %7")
        .arg(PROGRAM_NAME,
             PROGRAM_VERSION,
             GIT_HASH,
             QStringLiteral(__DATE__),
             QString::fromLatin1(qVersion()),
             QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture(),
             compatible_firmware_text());
}
