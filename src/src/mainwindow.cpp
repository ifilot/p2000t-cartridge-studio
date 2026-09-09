#include "mainwindow.h"

#include "aboutdialog.h"
#include "bankselector.h"
#include "config.h"
#include "filedownloader.h"
#include "firmwarecompatibility.h"
#include "firmwareupdater.h"
#include "hexviewwidget.h"
#include "logwindow.h"
#include "romsizes.h"
#include "settingswidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSerialPort>
#include <QSpacerItem>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace {

struct CartridgePort {
    QString name;
    QString serial;
};

QList<CartridgePort> cartridge_ports(uint16_t product_id)
{
    QList<CartridgePort> matches;
    for(const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
        if(port.hasVendorIdentifier() && port.hasProductIdentifier() &&
           port.vendorIdentifier() == 0x03EB && port.productIdentifier() == product_id) {
            matches.append({port.portName(), port.serialNumber()});
        }
    }
    return matches;
}

CartridgePort single_cartridge_port(uint16_t product_id)
{
    const QList<CartridgePort> matches = cartridge_ports(product_id);
    if(matches.size() > 1) {
        throw std::runtime_error(QStringLiteral("Multiple USB devices with ID 03EB:%1 are connected")
            .arg(product_id, 4, 16, QLatin1Char('0')).toUpper().toStdString());
    }
    return matches.value(0);
}

CartridgePort wait_for_cartridge_port(uint16_t product_id, int timeout_ms,
                                      const QString& expected_serial = {},
                                      const std::function<bool()>& cancelled = {})
{
    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < timeout_ms) {
        if(cancelled && cancelled()) return {};
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(100);
        const QList<CartridgePort> matches = cartridge_ports(product_id);
        if(!expected_serial.isEmpty()) {
            for(const CartridgePort& port : matches) {
                if(port.serial == expected_serial) return port;
            }
        } else if(matches.size() == 1) {
            return matches.front();
        } else if(matches.size() > 1) {
            throw std::runtime_error(QStringLiteral("Multiple USB devices with ID 03EB:%1 are connected")
                .arg(product_id, 4, 16, QLatin1Char('0')).toUpper().toStdString());
        }
    }
    return {};
}

QByteArray read_serial_exact(QSerialPort& port, qsizetype size, int timeout_ms)
{
    QByteArray result;
    QElapsedTimer timer;
    timer.start();
    while(result.size() < size && timer.elapsed() < timeout_ms) {
        if(port.bytesAvailable() == 0) port.waitForReadyRead(qMin(100, timeout_ms - static_cast<int>(timer.elapsed())));
        result.append(port.read(size - result.size()));
    }
    return result;
}

void validate_bootloader_identity(const CartridgePort& device, bool allow_legacy)
{
    QSerialPort port;
    port.setPortName(device.name);
    port.setBaudRate(57600);
    if(!port.open(QIODevice::ReadWrite)) {
        throw std::runtime_error(QStringLiteral("Could not open bootloader on %1: %2")
            .arg(device.name, port.errorString()).toStdString());
    }
    port.clear();
    if(port.write("S", 1) != 1 || !port.waitForBytesWritten(1000)) {
        throw std::runtime_error("Could not query the bootloader identity");
    }
    const QByteArray identifier = read_serial_exact(port, 7, 1500);
    const bool current = identifier == QByteArrayLiteral("P2KBOOT");
    const bool legacy = allow_legacy && identifier == QByteArrayLiteral("LUFACDC");
    if(!current && !legacy) {
        throw std::runtime_error(QStringLiteral("Device on %1 did not identify as the P2000T bootloader")
            .arg(device.name).toStdString());
    }
    if(port.write("s", 1) != 1 || !port.waitForBytesWritten(1000) ||
       read_serial_exact(port, 3, 1500) != QByteArray::fromHex("87951e")) {
        throw std::runtime_error("Bootloader MCU signature is not ATmega32U4 (87 95 1E)");
    }
}

QByteArray read_exact_image(const QString& filename, int required_size, const QString& description)
{
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("Could not open %1")
            .arg(QDir::toNativeSeparators(filename)).toStdString());
    }
    const QByteArray data = file.readAll();
    if(data.size() != required_size) {
        throw std::runtime_error(QStringLiteral("%1 must be exactly %2 bytes; the selected file contains %3 bytes")
            .arg(description).arg(required_size).arg(data.size()).toStdString());
    }
    return data;
}

bool confirm_destructive(QWidget* parent, const QString& title, const QString& action)
{
    QMessageBox box(QMessageBox::Warning, title, action, QMessageBox::NoButton, parent);
    box.setInformativeText(QObject::tr(
        "Safety check: this hardware revision has no MCU-readable cartridge-slot interlock. "
        "Remove the cartridge from the P2000T and connect it only by USB before continuing."));
    QPushButton* continue_button = box.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
    QPushButton* cancel_button = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel_button);
    box.setEscapeButton(cancel_button);
    box.exec();
    return box.clickedButton() == continue_button;
}

} // namespace

MainWindow::MainWindow(const std::shared_ptr<LogBuffer> _log_messages,
                       QWidget* parent,
                       SerialInterfaceFactory _serial_interface_factory)
    : QMainWindow(parent),
      log_messages(_log_messages),
      serial_interface_factory(std::move(_serial_interface_factory))
{
    Q_INIT_RESOURCE(resources);
    this->monitor_physical_port = !this->serial_interface_factory;
    if(!this->serial_interface_factory) {
        this->serial_interface_factory = [](const std::string& portname) {
            return std::make_shared<SerialInterface>(portname);
        };
    }

    this->log_window = std::make_unique<LogWindow>(this->log_messages);
    this->settings_widget = std::make_unique<SettingsWidget>();
    connect(this->settings_widget.get(), &SettingsWidget::signal_settings_update,
            this, &MainWindow::slot_update_settings);

    auto* container = new QWidget(this);
    auto* main_layout = new QHBoxLayout(container);
    this->setCentralWidget(container);

    auto* data_container = new QWidget(container);
    auto* data_layout = new QVBoxLayout(data_container);
    main_layout->addWidget(data_container, 1);

    this->label_data_descriptor = new QLabel(data_container);
    this->label_data_descriptor->setObjectName("labelDataDescriptor");
    data_layout->addWidget(this->label_data_descriptor);

    this->hex_widget = new HexViewWidget();
    this->hex_widget->setObjectName("hexViewWidget");
    this->hex_widget->setMinimumWidth(580);
    this->hex_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    data_layout->addWidget(this->hex_widget);

    this->button_reload_file = new QPushButton(tr("Reload file"), data_container);
    this->button_reload_file->setObjectName("buttonReloadFile");
    this->button_reload_file->setEnabled(false);
    data_layout->addWidget(this->button_reload_file);
    connect(this->button_reload_file, &QPushButton::released, this, &MainWindow::slot_reload_file);

    auto* scroll_area = new QScrollArea(container);
    scroll_area->setMinimumWidth(360);
    scroll_area->setWidgetResizable(true);
    scroll_area->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    main_layout->addWidget(scroll_area);

    auto* controls = new QWidget(scroll_area);
    auto* controls_layout = new QVBoxLayout(controls);
    controls_layout->setSizeConstraint(QLayout::SetMinimumSize);
    scroll_area->setWidget(controls);

    this->build_serial_interface_menu(controls_layout);
    this->build_rom_selection_menu(controls_layout);
    this->build_operations_menu(controls_layout);
    controls_layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Expanding));

    this->create_dropdown_menu();
    this->setMinimumSize(900, 750);
    this->setWindowIcon(QIcon(PROGRAM_ICON));
    this->setWindowTitle(PROGRAM_NAME);

    QFont hex_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    hex_font.setPointSize(9);
    hex_font.setStyleHint(QFont::TypeWriter);
    this->hex_widget->setFont(hex_font);
    QFont descriptor_font = hex_font;
    descriptor_font.setPointSize(8);
    descriptor_font.setStyleHint(QFont::TypeWriter);
    this->label_data_descriptor->setFont(descriptor_font);

    this->slot_update_settings();

    this->device_monitor = new QTimer(this);
    connect(this->device_monitor, &QTimer::timeout, this, &MainWindow::check_device_presence);
    this->device_monitor->start(1000);
}

MainWindow::~MainWindow()
{
    if(this->readerthread) { this->readerthread->requestInterruption(); this->readerthread->wait(); }
    if(this->flashthread) { this->flashthread->requestInterruption(); this->flashthread->wait(); }
}

void MainWindow::create_dropdown_menu()
{
    auto* menu_file = this->menuBar()->addMenu(tr("&File"));
    auto* menu_edit = this->menuBar()->addMenu(tr("&Edit"));
    auto* menu_help = this->menuBar()->addMenu(tr("&Help"));

    auto* open = menu_file->addAction(QIcon(":/assets/icon/bluecurve/document-open.png"), tr("Open"));
    open->setShortcuts(QKeySequence::Open);
    open->setIconVisibleInMenu(true);
    this->recent_files_menu = menu_file->addMenu(QIcon(":/assets/icon/bluecurve/folder.png"), tr("Recent files"));
    this->recent_files_menu->setObjectName("menuRecentFiles");
    this->recent_files_menu->menuAction()->setIconVisibleInMenu(true);
    auto* save = menu_file->addAction(QIcon(":/assets/icon/bluecurve/document-save.png"), tr("Save"));
    save->setShortcuts(QKeySequence::Save);
    save->setIconVisibleInMenu(true);
    auto* quit = menu_file->addAction(QIcon(":/assets/icon/bluecurve/application-exit.png"), tr("Quit"));
    quit->setShortcuts(QKeySequence::Quit);
    quit->setIconVisibleInMenu(true);

    auto* preferences = menu_edit->addAction(QIcon(":/assets/icon/bluecurve/preferences-system.png"), tr("Settings"));
    preferences->setIconVisibleInMenu(true);
    auto* about = menu_help->addAction(QIcon(":/assets/icon/bluecurve/help-about.png"), tr("About"));
    about->setIconVisibleInMenu(true);
    auto* debug_log = menu_help->addAction(QIcon(":/assets/icon/bluecurve/debug-log.png"), tr("Debug Log"));
    debug_log->setShortcut(Qt::Key_F2);
    debug_log->setIconVisibleInMenu(true);

    connect(open, &QAction::triggered, this, &MainWindow::slot_open);
    connect(save, &QAction::triggered, this, &MainWindow::slot_save);
    connect(quit, &QAction::triggered, this, &MainWindow::exit);
    connect(preferences, &QAction::triggered, this, &MainWindow::slot_settings_widget);
    connect(about, &QAction::triggered, this, &MainWindow::slot_about);
    connect(debug_log, &QAction::triggered, this, &MainWindow::slot_debug_log);
    this->update_recent_files_menu();
}

void MainWindow::build_serial_interface_menu(QVBoxLayout* target_layout)
{
    auto* group = new QGroupBox(tr("P2000T cartridge connection"));
    auto* vertical = new QVBoxLayout(group);
    auto* selector = new QHBoxLayout();
    selector->addWidget(new QLabel(tr("COM port"), group));
    this->combobox_serial_ports = new QComboBox(group);
    this->combobox_serial_ports->setObjectName("comboboxSerialPorts");
    selector->addWidget(this->combobox_serial_ports, 1);
    this->button_scan_ports = new QPushButton(tr("Scan"), group);
    this->button_scan_ports->setObjectName("buttonScanPorts");
    selector->addWidget(this->button_scan_ports);
    this->button_select_serial = new QPushButton(tr("Connect"), group);
    this->button_select_serial->setObjectName("buttonSelectSerial");
    this->button_select_serial->setEnabled(false);
    selector->addWidget(this->button_select_serial);
    vertical->addLayout(selector);

    this->label_serial = new QLabel(tr("Connect the cartridge by USB, then scan for it."), group);
    this->label_serial->setObjectName("labelSerial");
    this->label_serial->setWordWrap(true);
    vertical->addWidget(this->label_serial);
    this->label_board_id = new QLabel(group);
    this->label_board_id->setObjectName("labelBoardId");
    vertical->addWidget(this->label_board_id);
    target_layout->addWidget(group);

    connect(this->button_scan_ports, &QPushButton::released, this, &MainWindow::scan_com_devices);
    connect(this->button_select_serial, &QPushButton::released, this, &MainWindow::select_com_port);
}

void MainWindow::build_rom_selection_menu(QVBoxLayout* target_layout)
{
    this->rom_container = new QGroupBox(tr("ROM images"));
    auto* layout = new QVBoxLayout(this->rom_container);
    auto* choose = new QPushButton(tr("Choose a curated ROM"), this->rom_container);
    choose->setObjectName("buttonChooseCuratedRom");
    layout->addWidget(choose);

    const QList<QPair<QString, QString>> p2000t_images = {
        {"BASICNL v1.1", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/BASICNL1.1.bin"},
        {"Assembler v5.9", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/assembler%205.9.bin"},
        {"BASICNL Bootstrap for SD-CARD cartridge", "https://github.com/ifilot/p2000t-sdcard/releases/latest/download/BASICBOOTSTRAP.BIN"},
        {"Familiegeheugen v4", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/familiegeheugen%204.bin"},
        {"Flasher for SD-CARD cartridge", "https://github.com/ifilot/p2000t-sdcard/releases/latest/download/FLASHER.BIN"},
        {"Forth compiler", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/Forth.bin"},
        {"Maintenance cartridge", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/Maintenance%202.bin"},
        {"P2000T Teletekst Cartridge", "https://github.com/ifilot/p2000t-teletekst-cartridge/releases/latest/download/p2wp-cartridge.bin"},
        {"RAM expansion test", "https://github.com/ifilot/p2000t-ram-expansion-board/releases/latest/download/RAMTEST.BIN"},
        {"Word Processor v2", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/WordProcessor%202.bin"},
        {"Zemon assembler v1.4", "https://github.com/p2000t/software/raw/refs/heads/main/cartridges/Zemon%201.4.bin"}
    };
    const QList<QPair<QString, QString>> p2000m_images = {
        {"Philips Disk BASIC 24K", "p2000m-basic-24k.bin"},
        {"MCPM", "p2000m-cpm.bin"},
        {"UCSD Pascal", "p2000m-pascal.bin"}
    };

    auto* menu = new QMenu(choose);
    menu->setObjectName("menuCuratedRoms");
    const QIcon rom_icon(":/assets/icon/bluecurve/rom-file.png");
    const QIcon folder_icon(":/assets/icon/bluecurve/folder.png");
    auto add_submenu = [this, menu, &rom_icon, &folder_icon](
        const QString& title, const QString& object_name,
        const QList<QPair<QString, QString>>& images) {
        auto* submenu = menu->addMenu(folder_icon, title);
        submenu->setObjectName(object_name);
        for(const auto& image : images) {
            auto* action = submenu->addAction(rom_icon, image.first);
            action->setIconVisibleInMenu(true);
            action->setProperty("image_name", image.second);
            connect(action, &QAction::triggered, this, &MainWindow::load_default_image);
        }
    };
    add_submenu(tr("Philips P2000T"), "menuP2000TRoms", p2000t_images);
    add_submenu(tr("Philips P2000M"), "menuP2000MRoms", p2000m_images);
    choose->setMenu(menu);
    target_layout->addWidget(this->rom_container);
}

void MainWindow::build_operations_menu(QVBoxLayout* target_layout)
{
    auto* device_group = new QGroupBox(tr("Device and firmware"));
    device_group->setObjectName("groupDeviceOperations");
    auto* device_layout = new QVBoxLayout(device_group);
    this->label_chip_type = new QLabel(tr("ROM chip: not identified"), device_group);
    this->label_chip_type->setObjectName("labelChipType");
    device_layout->addWidget(this->label_chip_type);

    this->button_identify_chip = new QPushButton(tr("Identify SST39SF020"), device_group);
    this->button_identify_chip->setObjectName("buttonIdentifyChip");
    this->button_install_firmware = new QPushButton(tr("Install application firmware"), device_group);
    this->button_install_firmware->setObjectName("buttonInstallFirmware");
    auto* firmware_menu = new QMenu(this->button_install_firmware);
    firmware_menu->setObjectName("menuInstallFirmware");
    auto* install_latest = firmware_menu->addAction(tr("Install latest release from GitHub"));
    install_latest->setObjectName("actionInstallLatestFirmware");
    install_latest->setToolTip(FirmwareUpdater::latest_release_url().toString());
    auto* install_file = firmware_menu->addAction(tr("Install from file…"));
    install_file->setObjectName("actionInstallFirmwareFile");
    this->button_install_firmware->setMenu(firmware_menu);
    device_layout->addWidget(this->button_identify_chip);
    device_layout->addWidget(this->button_install_firmware);
    target_layout->addWidget(device_group);

    auto* bank_group = new QGroupBox(tr("Bank operations — 16 KiB"));
    bank_group->setObjectName("groupBankOperations");
    auto* bank_layout = new QVBoxLayout(bank_group);
    auto* bank_selector_layout = new QHBoxLayout();
    bank_selector_layout->addWidget(new QLabel(tr("Selected bank"), bank_group));
    this->bank_selector = new BankSelector(bank_group);
    bank_selector_layout->addWidget(this->bank_selector, 0, Qt::AlignLeft);
    bank_selector_layout->addStretch(1);
    bank_layout->addLayout(bank_selector_layout);
    this->button_read_bank = new QPushButton(tr("Read selected bank"), bank_group);
    this->button_read_bank->setObjectName("buttonReadBank");
    this->button_write_bank = new QPushButton(tr("Write loaded 16 KiB ROM to bank"), bank_group);
    this->button_write_bank->setObjectName("buttonWriteBank");
    this->button_erase_bank = new QPushButton(tr("Erase selected bank"), bank_group);
    this->button_erase_bank->setObjectName("buttonEraseBank");
    bank_layout->addWidget(this->button_read_bank);
    bank_layout->addWidget(this->button_write_bank);
    bank_layout->addWidget(this->button_erase_bank);
    target_layout->addWidget(bank_group);

    auto* rom_group = new QGroupBox(tr("Complete flash operations — 256 KiB"));
    rom_group->setObjectName("groupRomOperations");
    auto* rom_layout = new QVBoxLayout(rom_group);
    this->button_read_rom = new QPushButton(tr("Read complete flash to file…"), rom_group);
    this->button_read_rom->setObjectName("buttonReadRom");
    this->button_flash_rom = new QPushButton(tr("Flash complete 256 KiB image…"), rom_group);
    this->button_flash_rom->setObjectName("buttonFlashRom");
    this->button_erase_chip = new QPushButton(tr("Erase complete flash"), rom_group);
    this->button_erase_chip->setObjectName("buttonEraseChip");
    rom_layout->addWidget(this->button_read_rom);
    rom_layout->addWidget(this->button_flash_rom);
    rom_layout->addWidget(this->button_erase_chip);
    target_layout->addWidget(rom_group);

    this->progress_bar_load = new QProgressBar(rom_group);
    this->progress_bar_load->setObjectName("progressBarLoad");
    this->progress_bar_load->setRange(0, NUMBANKS);
    target_layout->addWidget(this->progress_bar_load);
    this->button_cancel_operation = new QPushButton(tr("Cancel current operation"), rom_group);
    this->button_cancel_operation->setObjectName("buttonCancelOperation");
    target_layout->addWidget(this->button_cancel_operation);

    this->set_operation_busy(false);
    connect(this->button_identify_chip, &QPushButton::released, this, &MainWindow::read_chip_id);
    connect(install_latest, &QAction::triggered, this, &MainWindow::install_latest_firmware);
    connect(install_file, &QAction::triggered, this, &MainWindow::select_firmware_file);
    connect(this->button_read_bank, &QPushButton::released, this, &MainWindow::read_bank);
    connect(this->button_write_bank, &QPushButton::released, this, &MainWindow::write_bank);
    connect(this->button_erase_bank, &QPushButton::released, this, &MainWindow::erase_bank);
    connect(this->button_read_rom, &QPushButton::released, this, &MainWindow::read_rom);
    connect(this->button_flash_rom, &QPushButton::released, this, &MainWindow::flash_rom);
    connect(this->button_erase_chip, &QPushButton::released, this, &MainWindow::erase_chip);
    connect(this->button_cancel_operation, &QPushButton::released, this, &MainWindow::cancel_operation);
}

void MainWindow::scan_com_devices()
{
    this->invalidate_connection();
    this->combobox_serial_ports->clear();
    this->bootloader_present = !cartridge_ports(0x204A).isEmpty();

    for(const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
        if(port.hasVendorIdentifier() && port.hasProductIdentifier()
           && port.vendorIdentifier() == 0x03EB && port.productIdentifier() == 0x2044) {
            this->combobox_serial_ports->addItem(port.portName(), port.serialNumber());
            qInfo() << "Found P2000T cartridge" << port.portName() << port.description();
        }
    }

    this->button_select_serial->setEnabled(this->combobox_serial_ports->count() > 0);
    this->set_operation_busy(false);
    if(this->combobox_serial_ports->count() == 1) {
        this->select_com_port();
    } else if(this->combobox_serial_ports->count() == 0) {
        QMessageBox::warning(this, tr("Cartridge not found"),
            tr("No P2000T cartridge application port (USB 03EB:2044) was found. "
               "Reconnect the cartridge and try again. If it is in bootloader mode, use Install application firmware."));
    }
}

void MainWindow::select_com_port()
{
    if(this->combobox_serial_ports->currentText().isEmpty()) return;

    this->board_connected = false;
    this->chip_identified = false;
    this->selected_device_serial.clear();
    try {
        this->serial_interface = this->serial_interface_factory(
            this->combobox_serial_ports->currentText().toStdString());
        this->serial_interface->open_port();
        const QString board_info = QString::fromStdString(this->serial_interface->get_board_info());
        this->serial_interface->close_port();

        const QString studio_version = QString::fromLatin1(PROGRAM_VERSION);
        const QString firmware_version = FirmwareCompatibility::version_from_board_info(board_info);
        const QStringList supported_versions =
            FirmwareCompatibility::supported_firmware_versions(studio_version);
        if(firmware_version.isEmpty()) {
            throw std::runtime_error(QStringLiteral("Invalid firmware identity '%1'")
                                     .arg(board_info).toStdString());
        }
        if(!FirmwareCompatibility::is_supported(studio_version, firmware_version)) {
            const QString supported = supported_versions.isEmpty()
                ? QStringLiteral("none (the compatibility matrix needs an entry for this Studio release)")
                : supported_versions.join(QStringLiteral(", "));
            throw std::runtime_error(
                QStringLiteral("Firmware %1 is not compatible with Studio %2; supported firmware versions: %3")
                    .arg(firmware_version, studio_version, supported).toStdString());
        }
        this->board_connected = true;
        this->selected_device_serial = this->combobox_serial_ports->currentData().toString();
        this->label_serial->setText(tr("Port: %1").arg(this->combobox_serial_ports->currentText()));
        this->label_board_id->setText(tr("Board: %1").arg(board_info));
        this->statusBar()->showMessage(
            tr("Connected to firmware %1 (compatible with Studio %2).")
                .arg(firmware_version, studio_version));
    } catch(const std::exception& e) {
        if(this->serial_interface) {
            try { this->serial_interface->close_port(); } catch(...) {}
        }
        this->invalidate_connection();
        this->raise_error_window(tr("Could not connect to the cartridge.\n\n%1").arg(e.what()));
    }
    this->set_operation_busy(false);
}

void MainWindow::check_device_presence()
{
    if(!this->monitor_physical_port || this->operation_busy) return;
    if(!this->board_connected) {
        this->bootloader_present = !cartridge_ports(0x204A).isEmpty();
        if(this->button_install_firmware) {
            this->button_install_firmware->setEnabled(this->bootloader_present);
        }
        return;
    }
    if(this->combobox_serial_ports->currentText().isEmpty()) return;
    const QString selected_port = this->combobox_serial_ports->currentText();
    for(const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
        if(port.portName() == selected_port &&
           (this->selected_device_serial.isEmpty() ||
            port.serialNumber() == this->selected_device_serial)) return;
    }
    this->invalidate_connection(
        tr("Cartridge disconnected or changed.\nScan and reconnect before continuing."));
}

void MainWindow::read_chip_id()
{
    try {
        this->verify_chip();
        this->chip_identified = true;
        this->label_chip_type->setText(tr("ROM chip: SST39SF020 (256 KiB, ID BF B6)"));
        this->statusBar()->showMessage(tr("Identified SST39SF020."));
    } catch(const std::exception& e) {
        this->chip_identified = false;
        this->label_chip_type->setText(tr("ROM chip: unsupported or unavailable"));
        this->raise_error_window(tr("The cartridge did not report an SST39SF020 (BF B6).\n\n%1").arg(e.what()));
    }
    this->set_operation_busy(false);
}

void MainWindow::select_firmware_file()
{
    QDir dir(this->settings.value("last_firmware_dir", QDir::homePath()).toString());
    if(!dir.exists()) dir.setPath(QDir::homePath());
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Select application firmware"), dir.absolutePath(),
        tr("Intel HEX firmware (*.hex);;All files (*)"));
    if(filename.isEmpty()) return;

    this->install_firmware_file(filename, QFileInfo(filename).fileName(), true);
}

void MainWindow::install_latest_firmware()
{
    const QUrl url = FirmwareUpdater::latest_release_url();
    QProgressDialog download_progress(tr("Downloading the latest application firmware…"),
                                      tr("Cancel"), 0, 0, this);
    download_progress.setWindowTitle(tr("Downloading firmware"));
    download_progress.setWindowModality(Qt::ApplicationModal);
    download_progress.show();
    QApplication::processEvents();

    FileDownloader downloader(url, this, 1024 * 1024);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&downloader, &FileDownloader::downloaded, &loop, &QEventLoop::quit);
    connect(&download_progress, &QProgressDialog::canceled, &downloader, &FileDownloader::cancel);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();
    download_progress.close();

    if(!timer.isActive()) {
        this->raise_error_window(tr("Downloading the latest firmware timed out after 30 seconds.\n\n%1")
                                 .arg(url.toString()));
        return;
    }
    timer.stop();
    if(!downloader.isSuccessful()) {
        this->raise_error_window(tr("Could not download the latest firmware.\n\n%1\n\n%2")
                                 .arg(url.toString(), downloader.errorMessage()));
        return;
    }

    const QByteArray contents = downloader.downloadedData();
    constexpr qsizetype MAX_FIRMWARE_DOWNLOAD_SIZE = 1024 * 1024;
    if(contents.isEmpty() || contents.size() > MAX_FIRMWARE_DOWNLOAD_SIZE) {
        this->raise_error_window(tr("The downloaded firmware has an invalid size (%1 bytes).")
                                 .arg(contents.size()));
        return;
    }

    const QUrl checksums_url = FirmwareUpdater::latest_checksums_url();
    QProgressDialog checksum_progress(tr("Verifying the release checksum…"),
                                      tr("Cancel"), 0, 0, this);
    checksum_progress.setWindowTitle(tr("Verifying firmware"));
    checksum_progress.setWindowModality(Qt::ApplicationModal);
    checksum_progress.show();
    FileDownloader checksum_downloader(checksums_url, this, 64 * 1024);
    connect(&checksum_downloader, &FileDownloader::downloaded, &loop, &QEventLoop::quit);
    connect(&checksum_progress, &QProgressDialog::canceled,
            &checksum_downloader, &FileDownloader::cancel);
    timer.start(30000);
    loop.exec();
    checksum_progress.close();
    if(!timer.isActive() || !checksum_downloader.isSuccessful()) {
        this->raise_error_window(tr("Could not obtain the release checksum.\n\n%1")
            .arg(checksum_downloader.errorMessage()));
        return;
    }
    timer.stop();
    try {
        FirmwareUpdater::verify_release_checksum(contents, checksum_downloader.downloadedData());
    } catch(const std::exception& e) {
        this->raise_error_window(tr("The downloaded firmware did not match the published release checksum.\n\n%1")
            .arg(e.what()));
        return;
    }

    QTemporaryFile temporary_file(
        QDir::tempPath() + QStringLiteral("/p2000t-cartridge-firmware-XXXXXX.hex"));
    if(!temporary_file.open() || temporary_file.write(contents) != contents.size() ||
       !temporary_file.flush()) {
        this->raise_error_window(tr("Could not create a temporary file for the downloaded firmware."));
        return;
    }
    const QString filename = temporary_file.fileName();
    temporary_file.close();
    this->install_firmware_file(filename, tr("latest GitHub release"), false);
}

void MainWindow::install_firmware_file(const QString& filename,
                                       const QString& display_name,
                                       bool remember_directory)
{

    FirmwareImageInfo image_info;
    try {
        QFile image(filename);
        if(!image.open(QIODevice::ReadOnly)) throw std::runtime_error("The firmware file could not be opened");
        image_info = FirmwareUpdater::validate_application_hex(image.readAll());
    } catch(const std::exception& e) {
        this->raise_error_window(tr("The selected firmware is not a safe application image.\n\n%1").arg(e.what()));
        return;
    }

    const QString tool_dir = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/avrdude");
    const QString avrdude = tool_dir + QStringLiteral("/avrdude.exe");
    const QString avrdude_config = tool_dir + QStringLiteral("/avrdude.conf");
    if(!QFileInfo::exists(avrdude) || !QFileInfo::exists(avrdude_config)) {
        this->raise_error_window(tr("The bundled firmware installation tool is missing.\n\nExpected:\n%1")
                                 .arg(QDir::toNativeSeparators(avrdude)));
        return;
    }

    if(!confirm_destructive(this, tr("Install application firmware"),
        tr("Install %1 (%2 data bytes, ending below 0x7000)?\n\n"
           "The USB bootloader remains protected, but the current application firmware will be erased.")
            .arg(display_name).arg(image_info.data_bytes))) return;

    if(remember_directory) {
        this->settings.setValue("last_firmware_dir", QFileInfo(filename).absolutePath());
    }
    this->set_operation_busy(true);
    QProgressDialog progress(tr("Entering the USB bootloader…"), tr("Cancel"), 0, 0, this);
    progress.setWindowTitle(tr("Installing firmware"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.show();
    QApplication::processEvents();

    try {
        CartridgePort boot_device;
        bool allow_legacy_bootloader = false;
        if(this->board_connected && this->serial_interface) {
            if(!cartridge_ports(0x204A).isEmpty()) {
                throw std::runtime_error("Another bootloader-mode device is already connected; disconnect it before updating the selected cartridge");
            }
            this->serial_interface->open_port();
            this->serial_interface->enter_bootloader();
            this->serial_interface->close_port();
            allow_legacy_bootloader = true;
            boot_device = wait_for_cartridge_port(0x204A, 15000, {},
                [&progress]() { return progress.wasCanceled(); });
            if(!this->selected_device_serial.isEmpty() && !boot_device.serial.isEmpty() &&
               boot_device.serial != this->selected_device_serial) {
                throw std::runtime_error("The bootloader USB serial number does not match the selected cartridge");
            }
        } else {
            boot_device = single_cartridge_port(0x204A);
        }
        if(progress.wasCanceled()) throw std::runtime_error("Firmware installation was cancelled; the bootloader remains available");
        if(boot_device.name.isEmpty()) throw std::runtime_error("No unambiguous P2000T USB bootloader was found");
        validate_bootloader_identity(boot_device, allow_legacy_bootloader);

        progress.setLabelText(tr("Programming and verifying %1 on %2…")
                              .arg(display_name, boot_device.name));
        QApplication::processEvents();

        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setProgram(avrdude);
        process.setArguments({QStringLiteral("-C"), avrdude_config,
                              QStringLiteral("-p"), QStringLiteral("atmega32u4"),
                              QStringLiteral("-c"), QStringLiteral("avr109"),
                              QStringLiteral("-P"), boot_device.name,
                              QStringLiteral("-b"), QStringLiteral("57600"),
                              QStringLiteral("-e"),
                              QStringLiteral("-U"), QStringLiteral("flash:w:%1:i").arg(filename)});
        process.start();
        if(!process.waitForStarted(5000)) {
            throw std::runtime_error("AVRDUDE could not be started: " + process.errorString().toStdString());
        }
        QElapsedTimer upload_timer;
        upload_timer.start();
        while(process.state() != QProcess::NotRunning && upload_timer.elapsed() < 120000) {
            process.waitForFinished(100);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if(progress.wasCanceled()) {
                process.kill();
                process.waitForFinished(3000);
                throw std::runtime_error("Firmware installation was cancelled; retry through the protected bootloader");
            }
        }
        if(process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
            throw std::runtime_error("Firmware installation timed out after two minutes; the bootloader remains available");
        }
        const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
        if(!output.isEmpty()) qInfo().noquote() << "AVRDUDE:" << output;
        if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            throw std::runtime_error(QStringLiteral("AVRDUDE failed with exit code %1.\n\n%2")
                .arg(process.exitCode()).arg(output.right(3000)).toStdString());
        }

        progress.setLabelText(tr("Firmware verified; waiting for the application…"));
        QApplication::processEvents();
        const CartridgePort application_device = wait_for_cartridge_port(
            0x2044, 15000, boot_device.serial);
        const QString application_port = application_device.name;
        progress.close();

        this->board_connected = false;
        this->bootloader_present = false;
        this->chip_identified = false;
        this->serial_interface.reset();
        this->selected_device_serial.clear();
        this->label_chip_type->setText(tr("ROM chip: not identified"));
        this->label_board_id->clear();
        this->combobox_serial_ports->clear();
        if(!application_port.isEmpty()) this->combobox_serial_ports->addItem(application_port, application_device.serial);
        this->label_serial->setText(application_port.isEmpty()
            ? tr("Firmware installed; no compatible application port appeared.")
            : tr("Firmware installed. Connect to %1 to inspect it.").arg(application_port));
        this->statusBar()->showMessage(tr("Application firmware installed and verified."));
        QMessageBox::information(this, tr("Firmware installed"), application_port.isEmpty()
            ? tr("AVRDUDE programmed and verified the image. The bootloader is intact, but an application port did not appear within 15 seconds.")
            : tr("AVRDUDE programmed and verified the image. The cartridge application is available on %1.")
                .arg(application_port));
    } catch(const std::exception& e) {
        progress.close();
        if(this->serial_interface) {
            try { this->serial_interface->close_port(); } catch(...) {}
        }
        this->invalidate_connection();
        this->raise_error_window(tr("Firmware installation failed. The protected USB bootloader can be used to retry.\n\n%1")
                                 .arg(e.what()));
    }
    this->set_operation_busy(false);
}

void MainWindow::verify_chip()
{
    if(!this->serial_interface) throw std::runtime_error("No cartridge is connected");
    try {
        this->serial_interface->open_port();
        const uint16_t chip_id = this->serial_interface->get_chip_id();
        this->serial_interface->close_port();
        if(chip_id != 0xBFB6) {
            throw std::runtime_error(QStringLiteral("Received chip ID %1 %2")
                .arg((chip_id >> 8) & 0xFF, 2, 16, QLatin1Char('0'))
                .arg(chip_id & 0xFF, 2, 16, QLatin1Char('0')).toUpper().toStdString());
        }
    } catch(...) {
        try { this->serial_interface->close_port(); } catch(...) {}
        this->invalidate_connection(tr("Cartridge communication failed; scan and reconnect before retrying."));
        throw;
    }
}

void MainWindow::read_rom()
{
    QDir dir(this->settings.value("last_save_dir", QDir::homePath()).toString());
    if(!dir.exists()) dir.setPath(QDir::homePath());
    const QString filename = QFileDialog::getSaveFileName(
        this, tr("Store complete 256 KiB flash image"),
        dir.absoluteFilePath(QStringLiteral("sst39sf020-full.bin")),
        tr("ROM images (*.bin *.rom);;All files (*)"));
    if(filename.isEmpty()) return;
    this->settings.setValue("last_save_dir", QFileInfo(filename).absolutePath());
    this->start_read_rom(filename);
}

void MainWindow::start_read_rom(const QString& filename)
{
    if(filename.isEmpty()) return;
    this->complete_read_filename = filename;
    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, NUMBANKS);
    this->progress_bar_load->setValue(0);
    this->statusBar()->showMessage(tr("Reading the complete SST39SF020..."));

    this->readerthread = std::make_unique<ReadThread>(this->serial_interface);
    connect(this->readerthread.get(), &ReadThread::read_result_ready, this, &MainWindow::read_result_ready);
    connect(this->readerthread.get(), &ReadThread::read_bank_start, this, &MainWindow::read_bank_start);
    connect(this->readerthread.get(), &ReadThread::read_bank_done, this, &MainWindow::read_bank_done);
    connect(this->readerthread.get(), &ReadThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->readerthread.get(), &ReadThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->readerthread->start();
}

void MainWindow::read_bank_start(unsigned int bank_id, unsigned int nr_banks)
{
    this->progress_bar_load->setRange(0, static_cast<int>(nr_banks));
    this->progress_bar_load->setValue(static_cast<int>(bank_id));
}

void MainWindow::read_bank_done(unsigned int bank_id, unsigned int nr_banks)
{
    this->progress_bar_load->setValue(static_cast<int>(bank_id + 1));
    const double elapsed = this->operation_timer.elapsed() / 1000.0;
    const double remaining = elapsed / (bank_id + 1) * (nr_banks - bank_id - 1);
    this->statusBar()->showMessage(tr("Reading bank %1 / %2 — about %3 seconds remaining")
        .arg(bank_id + 1).arg(nr_banks).arg(remaining, 0, 'f', 0));
}

void MainWindow::read_result_ready()
{
    this->readerthread->wait();
    const QByteArray data = this->readerthread->get_data();
    this->readerthread.reset();
    QSaveFile file(this->complete_read_filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        this->raise_error_window(tr("The complete flash was read, but could not be stored in:\n%1")
                                 .arg(QDir::toNativeSeparators(this->complete_read_filename)));
    } else {
        this->statusBar()->showMessage(tr("Read and stored 256 KiB in %1 seconds: %2")
            .arg(this->operation_timer.elapsed() / 1000.0, 0, 'f', 1)
            .arg(QDir::toNativeSeparators(this->complete_read_filename)));
    }
    this->complete_read_filename.clear();
    this->set_operation_busy(false);
}

void MainWindow::read_bank()
{
    this->active_bank = static_cast<unsigned int>(this->bank_selector->currentBank());
    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, 1);
    this->progress_bar_load->setValue(0);
    this->statusBar()->showMessage(tr("Reading bank %1…").arg(this->active_bank));

    this->readerthread = std::make_unique<ReadThread>(this->serial_interface);
    this->readerthread->set_bank_range(this->active_bank, 1);
    connect(this->readerthread.get(), &ReadThread::read_result_ready,
            this, &MainWindow::bank_read_result_ready);
    connect(this->readerthread.get(), &ReadThread::read_bank_start, this, &MainWindow::read_bank_start);
    connect(this->readerthread.get(), &ReadThread::read_bank_done, this, &MainWindow::read_bank_done);
    connect(this->readerthread.get(), &ReadThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->readerthread.get(), &ReadThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->readerthread->start();
}

void MainWindow::bank_read_result_ready()
{
    this->readerthread->wait();
    const QByteArray data = this->readerthread->get_data();
    this->readerthread.reset();
    this->hex_widget->set_data(data);
    this->current_filename.clear();
    this->button_reload_file->setEnabled(false);
    this->show_data(tr("Bank %1 read-back").arg(this->active_bank), data);
    this->statusBar()->showMessage(tr("Read bank %1 (16 KiB) in %2 seconds.")
        .arg(this->active_bank)
        .arg(this->operation_timer.elapsed() / 1000.0, 0, 'f', 1));
    this->set_operation_busy(false);
}

void MainWindow::flash_rom()
{
    QDir dir(this->settings.value("last_open_dir", QDir::homePath()).toString());
    if(!dir.exists()) dir.setPath(QDir::homePath());
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Select complete 256 KiB flash image"), dir.absolutePath(),
        tr("ROM images (*.bin *.rom);;All files (*)"));
    if(filename.isEmpty()) return;

    QByteArray source;
    try {
        source = read_exact_image(filename, ROMSIZE, tr("A complete flash image"));
        this->verify_chip();
    } catch(const std::exception& e) {
        this->raise_error_window(tr("Cannot program the ROM.\n\n%1").arg(e.what()));
        return;
    }

    if(!confirm_destructive(this, tr("Erase and program ROM"),
        tr("Programming erases the entire SST39SF020, writes the selected 256 KiB image, "
           "and reads it back for verification."))) {
        return;
    }

    this->settings.setValue("last_open_dir", QFileInfo(filename).absolutePath());
    this->flash_data = source;
    this->flash_scope = FlashScope::CompleteRom;
    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, NUMBLOCKS);
    this->progress_bar_load->setValue(0);
    this->statusBar()->showMessage(tr("Erasing and programming the SST39SF020..."));

    this->flashthread = std::make_unique<FlashThread>(this->serial_interface);
    this->flashthread->set_complete_rom(this->flash_data);
    connect(this->flashthread.get(), &FlashThread::flash_result_ready, this, &MainWindow::flash_result_ready);
    connect(this->flashthread.get(), &FlashThread::flash_block_start, this, &MainWindow::flash_block_start);
    connect(this->flashthread.get(), &FlashThread::flash_block_done, this, &MainWindow::flash_block_done);
    connect(this->flashthread.get(), &FlashThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->flashthread.get(), &FlashThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->flashthread->start();
}

void MainWindow::write_bank()
{
    const QByteArray source = this->hex_widget->get_data();
    if(source.size() != BANKSIZE) {
        this->raise_error_window(tr("Open or read a ROM containing exactly 16384 bytes first."));
        return;
    }
    this->active_bank = static_cast<unsigned int>(this->bank_selector->currentBank());
    try {
        this->verify_chip();
    } catch(const std::exception& e) {
        this->raise_error_window(tr("Cannot program bank %1.\n\n%2").arg(this->active_bank).arg(e.what()));
        return;
    }
    if(!confirm_destructive(this, tr("Erase and program bank"),
        tr("This erases bank %1 only, writes the loaded 16 KiB ROM, and reads the bank back "
           "for verification.").arg(this->active_bank))) return;

    this->flash_data = source;
    this->flash_scope = FlashScope::Bank;
    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, BANKSIZE / BLOCKSIZE);
    this->progress_bar_load->setValue(0);
    this->statusBar()->showMessage(tr("Erasing and programming bank %1…").arg(this->active_bank));

    this->flashthread = std::make_unique<FlashThread>(this->serial_interface);
    this->flashthread->set_bank(this->active_bank, this->flash_data);
    connect(this->flashthread.get(), &FlashThread::flash_result_ready, this, &MainWindow::flash_result_ready);
    connect(this->flashthread.get(), &FlashThread::flash_block_start, this, &MainWindow::flash_block_start);
    connect(this->flashthread.get(), &FlashThread::flash_block_done, this, &MainWindow::flash_block_done);
    connect(this->flashthread.get(), &FlashThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->flashthread.get(), &FlashThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->flashthread->start();
}

void MainWindow::flash_block_start(unsigned int block_id, unsigned int nr_blocks)
{
    this->progress_bar_load->setRange(0, static_cast<int>(nr_blocks));
    this->progress_bar_load->setValue(static_cast<int>(block_id));
}

void MainWindow::flash_block_done(unsigned int block_id, unsigned int nr_blocks)
{
    this->progress_bar_load->setValue(static_cast<int>(block_id + 1));
    const double elapsed = this->operation_timer.elapsed() / 1000.0;
    const double remaining = elapsed / (block_id + 1) * (nr_blocks - block_id - 1);
    this->statusBar()->showMessage(tr("Programming block %1 / %2 — about %3 seconds remaining")
        .arg(block_id + 1).arg(nr_blocks).arg(remaining, 0, 'f', 0));
}

void MainWindow::flash_result_ready()
{
    this->flashthread->wait();
    this->operation_timer.restart();
    this->progress_bar_load->setValue(0);
    if(this->flash_scope == FlashScope::CompleteRom) {
        this->statusBar()->showMessage(tr("Programming complete; reading back all 256 KiB for verification..."));
    } else {
        this->statusBar()->showMessage(tr("Programming complete; reading back bank %1 for verification...")
                                       .arg(this->active_bank));
    }

    this->readerthread = std::make_unique<ReadThread>(this->serial_interface);
    if(this->flash_scope == FlashScope::Bank) this->readerthread->set_bank_range(this->active_bank, 1);
    connect(this->readerthread.get(), &ReadThread::read_result_ready, this, &MainWindow::verify_result_ready);
    connect(this->readerthread.get(), &ReadThread::read_bank_start, this, &MainWindow::read_bank_start);
    connect(this->readerthread.get(), &ReadThread::read_bank_done, this, &MainWindow::read_bank_done);
    connect(this->readerthread.get(), &ReadThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->readerthread.get(), &ReadThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->readerthread->start();
}

void MainWindow::verify_result_ready()
{
    this->readerthread->wait();
    const QByteArray actual = this->readerthread->get_data();
    this->readerthread.reset();
    this->flashthread.reset();

    if(actual == this->flash_data) {
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(actual, QCryptographicHash::Sha256).toHex());
        if(this->flash_scope == FlashScope::CompleteRom) {
            this->statusBar()->showMessage(tr("Complete-flash programming and verification succeeded."));
            QMessageBox::information(this, tr("Programming complete"),
                tr("The complete SST39SF020 was programmed and all 262144 bytes verified.\n\nSHA-256: %1").arg(sha));
        } else {
            this->statusBar()->showMessage(tr("Bank %1 programming and verification succeeded.")
                                           .arg(this->active_bank));
            QMessageBox::information(this, tr("Bank programmed"),
                tr("Bank %1 was programmed and all 16384 bytes verified.\n\nSHA-256: %2")
                    .arg(this->active_bank).arg(sha));
        }
    } else {
        int mismatch = 0;
        const int compared = std::min(actual.size(), this->flash_data.size());
        while(mismatch < compared && actual[mismatch] == this->flash_data[mismatch]) ++mismatch;
        const int absolute_address = this->flash_scope == FlashScope::Bank
            ? static_cast<int>(this->active_bank * BANKSIZE) + mismatch : mismatch;
        this->raise_error_window(tr("Read-back verification failed at flash address 0x%1.")
            .arg(absolute_address, 5, 16, QLatin1Char('0')).toUpper());
    }
    this->progress_bar_load->reset();
    this->set_operation_busy(false);
}

void MainWindow::erase_chip()
{
    if(!confirm_destructive(this, tr("Erase complete ROM"),
        tr("This permanently erases all 256 KiB of the SST39SF020."))) return;

    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, 0);
    this->statusBar()->showMessage(tr("Erasing and verifying the complete SST39SF020…"));
    this->flash_scope = FlashScope::CompleteRom;
    this->flashthread = std::make_unique<FlashThread>(this->serial_interface);
    this->flashthread->set_erase_complete();
    connect(this->flashthread.get(), &FlashThread::erase_result_ready, this, &MainWindow::erase_result_ready);
    connect(this->flashthread.get(), &FlashThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->flashthread.get(), &FlashThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->flashthread->start();
}

void MainWindow::erase_bank()
{
    this->active_bank = static_cast<unsigned int>(this->bank_selector->currentBank());
    if(!confirm_destructive(this, tr("Erase bank"),
        tr("This permanently erases bank %1 (16 KiB) without changing the other banks.")
            .arg(this->active_bank))) return;

    this->set_operation_busy(true, true);
    this->operation_timer.start();
    this->progress_bar_load->setRange(0, 0);
    this->statusBar()->showMessage(tr("Erasing and verifying bank %1…").arg(this->active_bank));
    this->flash_scope = FlashScope::Bank;
    this->flashthread = std::make_unique<FlashThread>(this->serial_interface);
    this->flashthread->set_erase_bank(this->active_bank);
    connect(this->flashthread.get(), &FlashThread::erase_result_ready, this, &MainWindow::erase_result_ready);
    connect(this->flashthread.get(), &FlashThread::thread_abort, this, &MainWindow::thread_abort);
    connect(this->flashthread.get(), &FlashThread::thread_cancelled, this, &MainWindow::thread_cancelled);
    this->flashthread->start();
}

void MainWindow::erase_result_ready()
{
    this->flashthread->wait();
    this->flashthread.reset();
    this->progress_bar_load->reset();
    if(this->flash_scope == FlashScope::CompleteRom) {
        this->statusBar()->showMessage(tr("The complete SST39SF020 was erased and verified in %1 seconds.")
            .arg(this->operation_timer.elapsed() / 1000.0, 0, 'f', 1));
        QMessageBox::information(this, tr("Erase complete"), tr("All 262144 ROM bytes were verified as FF."));
    } else {
        this->statusBar()->showMessage(tr("Bank %1 was erased and verified.").arg(this->active_bank));
        QMessageBox::information(this, tr("Bank erased"),
            tr("All 16384 bytes in bank %1 were verified as FF.").arg(this->active_bank));
    }
    this->set_operation_busy(false);
}

void MainWindow::cancel_operation()
{
    this->button_cancel_operation->setEnabled(false);
    this->statusBar()->showMessage(tr("Cancellation requested; waiting for a safe transfer boundary…"));
    if(this->readerthread) this->readerthread->requestInterruption();
    if(this->flashthread) this->flashthread->requestInterruption();
}

void MainWindow::thread_cancelled(const QString& message)
{
    if(this->readerthread) { this->readerthread->wait(); this->readerthread.reset(); }
    if(this->flashthread) { this->flashthread->wait(); this->flashthread.reset(); }
    this->progress_bar_load->reset();
    this->complete_read_filename.clear();
    this->set_operation_busy(false);
    this->statusBar()->showMessage(message);
    QMessageBox::warning(this, tr("Operation cancelled"), message);
}

void MainWindow::thread_abort(const QString& error)
{
    if(this->readerthread) { this->readerthread->wait(); this->readerthread.reset(); }
    if(this->flashthread) { this->flashthread->wait(); this->flashthread.reset(); }
    this->progress_bar_load->reset();
    this->invalidate_connection();
    this->set_operation_busy(false);
    this->raise_error_window(tr("The operation stopped. Reconnect the cartridge before retrying if communication was interrupted.\n\n%1")
                             .arg(error));
}

void MainWindow::invalidate_connection(const QString& reason)
{
    if(this->serial_interface) {
        try { this->serial_interface->close_port(); } catch(...) {}
    }
    this->serial_interface.reset();
    this->board_connected = false;
    this->bootloader_present = false;
    this->chip_identified = false;
    this->selected_device_serial.clear();
    if(this->combobox_serial_ports) this->combobox_serial_ports->clear();
    if(this->label_board_id) this->label_board_id->clear();
    if(this->label_chip_type) this->label_chip_type->setText(tr("ROM chip: not identified"));
    if(this->label_serial) {
        this->label_serial->setText(reason.isEmpty() ? tr("Not connected") : reason);
    }
    if(!reason.isEmpty()) {
        this->statusBar()->showMessage(reason);
    }
    this->set_operation_busy(this->operation_busy);
}

bool MainWindow::open_file(const QString& filename)
{
    QFile file(filename);
    if(!file.exists()) {
        QMessageBox::warning(this, tr("File not found"), tr("The file no longer exists:\n%1").arg(QDir::toNativeSeparators(filename)));
        return false;
    }
    if(file.size() != BANKSIZE) {
        QMessageBox::warning(this, tr("Unsupported ROM size"),
            tr("A P2000T bank ROM must contain exactly 16384 bytes."));
        return false;
    }
    if(!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Could not open file"), tr("The file could not be read:\n%1").arg(QDir::toNativeSeparators(filename)));
        return false;
    }

    const QByteArray data = file.readAll();
    this->current_filename = QFileInfo(file).absoluteFilePath();
    this->hex_widget->set_data(data);
    this->button_reload_file->setEnabled(true);
    this->show_data(QFileInfo(file).fileName(), data);
    this->statusBar()->showMessage(tr("Opened %1.").arg(QDir::toNativeSeparators(this->current_filename)));
    return true;
}

void MainWindow::slot_open()
{
    QDir dir(this->settings.value("last_open_dir", QDir::homePath()).toString());
    if(!dir.exists()) dir.setPath(QDir::homePath());
    const QString filename = QFileDialog::getOpenFileName(this, tr("Open 16 KiB bank ROM"), dir.absolutePath(), tr("ROM images (*.bin *.rom);;All files (*)"));
    if(filename.isEmpty()) return;
    if(this->open_file(filename)) {
        this->settings.setValue("last_open_dir", QFileInfo(filename).absolutePath());
        this->add_recent_file(filename);
    }
}

void MainWindow::slot_reload_file()
{
    if(!this->current_filename.isEmpty()) this->open_file(this->current_filename);
}

void MainWindow::slot_save()
{
    const QByteArray data = this->hex_widget->get_data();
    if(data.isEmpty()) return;
    QDir dir(this->settings.value("last_save_dir", QDir::homePath()).toString());
    if(!dir.exists()) dir.setPath(QDir::homePath());
    const QString filename = QFileDialog::getSaveFileName(this, tr("Save 16 KiB bank ROM"), dir.absolutePath(), tr("ROM images (*.bin *.rom)"));
    if(filename.isEmpty()) return;
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        this->raise_error_window(tr("Could not save %1.").arg(QDir::toNativeSeparators(filename)));
        return;
    }
    this->settings.setValue("last_save_dir", QFileInfo(filename).absolutePath());
    this->statusBar()->showMessage(tr("Saved %1.").arg(QDir::toNativeSeparators(filename)));
}

void MainWindow::add_recent_file(const QString& filename)
{
    const QString path = QDir::cleanPath(QFileInfo(filename).absoluteFilePath());
    QStringList files = this->settings.value("recent_files").toStringList();
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    for(int i = files.size() - 1; i >= 0; --i) {
        if(QDir::cleanPath(files[i]).compare(path, sensitivity) == 0) files.removeAt(i);
    }
    files.prepend(path);
    while(files.size() > 5) files.removeLast();
    this->settings.setValue("recent_files", files);
    this->settings.sync();
    this->update_recent_files_menu();
}

void MainWindow::update_recent_files_menu()
{
    if(!this->recent_files_menu) return;
    this->recent_files_menu->clear();
    const QStringList files = this->settings.value("recent_files").toStringList();
    if(files.isEmpty()) {
        auto* empty = this->recent_files_menu->addAction(QIcon(":/assets/icon/bluecurve/rom-file.png"), tr("No recent files"));
        empty->setEnabled(false);
        empty->setIconVisibleInMenu(true);
        return;
    }
    for(const QString& path : files) {
        auto* action = this->recent_files_menu->addAction(QIcon(":/assets/icon/bluecurve/rom-file.png"), QFileInfo(path).fileName());
        action->setData(path);
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setIconVisibleInMenu(true);
        connect(action, &QAction::triggered, this, &MainWindow::slot_open_recent_file);
    }
}

void MainWindow::slot_open_recent_file()
{
    const auto* action = qobject_cast<QAction*>(sender());
    if(!action) return;
    const QString filename = action->data().toString();
    if(this->open_file(filename)) {
        this->settings.setValue("last_open_dir", QFileInfo(filename).absolutePath());
        this->add_recent_file(filename);
    } else if(!QFileInfo::exists(filename)) {
        QStringList files = this->settings.value("recent_files").toStringList();
        files.removeAll(filename);
        this->settings.setValue("recent_files", files);
        this->update_recent_files_menu();
    }
}

void MainWindow::load_default_image()
{
    const QString image_name = sender()->property("image_name").toString();
    if(!image_name.startsWith("http://") && !image_name.startsWith("https://")) {
        QFile file(":/assets/roms/" + image_name);
        if(!file.open(QIODevice::ReadOnly)) {
            this->raise_error_window(tr("The bundled ROM image could not be opened."));
            return;
        }
        const QByteArray data = file.readAll();
        if(data.size() != BANKSIZE) {
            this->raise_error_window(tr("The bundled ROM image was not a valid 16 KiB bank image."));
            return;
        }
        this->hex_widget->set_data(data);
        this->current_filename.clear();
        this->button_reload_file->setEnabled(false);
        this->show_data(image_name + tr(" (bundled)"), data);
        this->statusBar()->showMessage(tr("Loaded bundled ROM %1.").arg(image_name));
        return;
    }

    const QString url_text = image_name;
    QProgressDialog progress(tr("Downloading ROM image..."), tr("Cancel"), 0, 0, this);
    progress.setWindowTitle(tr("Please wait"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.show();
    QApplication::processEvents();

    auto* downloader = new FileDownloader(QUrl(url_text), this, BANKSIZE);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(downloader, &FileDownloader::downloaded, &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, downloader, &FileDownloader::cancel);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(10000);
    loop.exec();
    progress.hide();

    const QByteArray data = downloader->downloadedData();
    if(!timer.isActive() || !downloader->isSuccessful() || data.size() != BANKSIZE) {
        this->raise_error_window(tr("The ROM download failed or was not a valid 16 KiB bank image.\n\n%1")
                                 .arg(downloader->errorMessage()));
        downloader->deleteLater();
        return;
    }
    const QString name = QUrl(url_text).fileName();
    this->hex_widget->set_data(data);
    this->current_filename.clear();
    this->button_reload_file->setEnabled(false);
    this->show_data(name + tr(" (download)"), data);
    this->statusBar()->showMessage(tr("Downloaded %1.").arg(name));
    downloader->deleteLater();
}

void MainWindow::show_data(const QString& name, const QByteArray& data)
{
    const QString md5 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
    this->label_data_descriptor->setText(tr("<b>%1</b> | Size: %2 KiB | MD5: %3")
        .arg(name.toHtmlEscaped()).arg(data.size() / 1024.0, 0, 'f', 1).arg(md5));
}

void MainWindow::set_operation_busy(bool busy, bool cancellable)
{
    this->operation_busy = busy;
    if(this->button_identify_chip) this->button_identify_chip->setEnabled(!busy && this->board_connected);
    if(this->button_install_firmware) {
        this->button_install_firmware->setEnabled(
            !busy && (this->board_connected || this->bootloader_present));
    }
    if(this->button_read_bank) this->button_read_bank->setEnabled(!busy && this->chip_identified);
    if(this->button_write_bank) this->button_write_bank->setEnabled(!busy && this->chip_identified);
    if(this->button_erase_bank) this->button_erase_bank->setEnabled(!busy && this->chip_identified);
    if(this->bank_selector) this->bank_selector->setEnabled(!busy);
    if(this->button_read_rom) this->button_read_rom->setEnabled(!busy && this->chip_identified);
    if(this->button_flash_rom) this->button_flash_rom->setEnabled(!busy && this->chip_identified);
    if(this->button_erase_chip) this->button_erase_chip->setEnabled(!busy && this->chip_identified);
    if(this->button_scan_ports) this->button_scan_ports->setEnabled(!busy);
    if(this->button_select_serial) this->button_select_serial->setEnabled(!busy && this->combobox_serial_ports->count() > 0);
    if(this->button_cancel_operation) this->button_cancel_operation->setEnabled(busy && cancellable);
}

void MainWindow::raise_error_window(const QString& message)
{
    QMessageBox box(QMessageBox::Critical, tr("P2000T Cartridge Studio"), message, QMessageBox::Ok, this);
    box.setWindowIcon(QIcon(PROGRAM_ICON));
    box.exec();
}

void MainWindow::slot_about() { AboutDialog(this).exec(); }
void MainWindow::slot_debug_log() { this->log_window->show(); }
void MainWindow::slot_settings_widget() { this->settings_widget->show(); }
void MainWindow::slot_update_settings()
{
    this->rom_container->setVisible(this->settings.value("SHOW_RETROROMS", true).toBool());
    this->hex_widget->viewport()->repaint();
}
void MainWindow::exit() { QApplication::quit(); }
