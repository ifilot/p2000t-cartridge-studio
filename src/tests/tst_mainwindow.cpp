#include <QtTest/QtTest>

#include "config.h"
#include "aboutdialog.h"
#include "bankselector.h"
#include "firmwarecompatibility.h"
#include "mainwindow.h"
#include "hexviewwidget.h"
#include "support/emulated_serial_transport.h"

#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>

#include <algorithm>
#include <memory>

class MainWindowTest : public QObject {
    Q_OBJECT

    static MainWindow::SerialInterfaceFactory factory(const std::shared_ptr<FirmwareEmulatorBackend>& backend) {
        return [backend](const std::string& port) {
            return std::make_shared<SerialInterface>(port, [backend](const std::string&) { return backend->create_transport(); });
        };
    }
    static void addPort(MainWindow& window) {
        auto* combo = window.findChild<QComboBox*>("comboboxSerialPorts");
        QVERIFY(combo);
        combo->addItem("emu");
    }

private slots:
    void about_dialog_describes_supported_cartridge();
    void compatibility_matrix_accepts_previous_firmware();
    void exposes_only_supported_operations();
    void connects_compatible_previous_firmware();
    void connects_current_version_and_identifies_sst39sf020();
    void reads_selected_bank();
    void reads_complete_rom();
};

void MainWindowTest::about_dialog_describes_supported_cartridge()
{
    AboutDialog dialog;
    QVERIFY(dialog.findChild<QGroupBox*>("aboutCartridgeGroup"));
    QVERIFY(dialog.findChild<QGroupBox*>("aboutBuildGroup"));
    QVERIFY(dialog.findChild<QLabel*>("aboutLinks"));

    QString visible_text;
    for(const QLabel* label : dialog.findChildren<QLabel*>()) {
        visible_text += label->text() + QLatin1Char('\n');
    }
    const auto* credits = dialog.findChild<QTextBrowser*>("aboutCredits");
    QVERIFY(credits);
    visible_text += credits->toPlainText();

    QVERIFY(visible_text.contains("ATmega32U4"));
    QVERIFY(visible_text.contains("SST39SF020"));
    QVERIFY(visible_text.contains("16 banks of 16 KiB"));
    QVERIFY(visible_text.contains("03EB:2044"));
    QVERIFY(visible_text.contains(PROGRAM_VERSION));
    QVERIFY(visible_text.contains("firmware 0.1.0, 0.1.1"));
    QVERIFY(!visible_text.contains(QStringLiteral("certif") + QStringLiteral("ication"),
                                   Qt::CaseInsensitive));
    QVERIFY(!visible_text.contains(QStringLiteral("NL") + QStringLiteral("000020")));
}

void MainWindowTest::compatibility_matrix_accepts_previous_firmware()
{
    QVERIFY(!FirmwareCompatibility::supported_firmware_versions(PROGRAM_VERSION).isEmpty());
    QCOMPARE(FirmwareCompatibility::version_from_board_info("P2000T-FW v0.1.0"),
             QString("0.1.0"));
    QVERIFY(FirmwareCompatibility::version_from_board_info("not-a-cartridge").isEmpty());
    QCOMPARE(FirmwareCompatibility::supported_firmware_versions("0.1.1"),
             QStringList({"0.1.0", "0.1.1"}));
    QVERIFY(FirmwareCompatibility::is_supported("0.1.1", "0.1.0"));
    QVERIFY(FirmwareCompatibility::is_supported("0.1.1", "0.1.1"));
    QVERIFY(!FirmwareCompatibility::is_supported("0.1.1", "0.1.2"));
    QVERIFY(FirmwareCompatibility::supported_firmware_versions("0.1.2").isEmpty());
}

void MainWindowTest::exposes_only_supported_operations()
{
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs);
    QVERIFY(window.findChild<QPushButton*>("buttonIdentifyChip"));
    QVERIFY(window.findChild<QPushButton*>("buttonReadRom"));
    QVERIFY(window.findChild<QPushButton*>("buttonFlashRom"));
    QVERIFY(window.findChild<QPushButton*>("buttonEraseChip"));
    auto* install_firmware = window.findChild<QPushButton*>("buttonInstallFirmware");
    QVERIFY(install_firmware);
    QVERIFY(install_firmware->menu());
    QCOMPARE(install_firmware->menu()->objectName(), QString("menuInstallFirmware"));
    QCOMPARE(install_firmware->menu()->actions().size(), 2);
    auto* latest_firmware = window.findChild<QAction*>("actionInstallLatestFirmware");
    QVERIFY(latest_firmware);
    QCOMPARE(latest_firmware->toolTip(),
             QString("https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/"
                     "download/p2000t-programmable-cartridge-firmware.hex"));
    QVERIFY(window.findChild<QAction*>("actionInstallFirmwareFile"));
    auto* curated_roms = window.findChild<QPushButton*>("buttonChooseCuratedRom");
    QVERIFY(curated_roms);
    QVERIFY(curated_roms->menu());
    QCOMPARE(curated_roms->menu()->objectName(), QString("menuCuratedRoms"));
    QAction* teletekst = nullptr;
    for(QAction* action : curated_roms->menu()->actions()) {
        QVERIFY(!action->text().contains("Joystick", Qt::CaseInsensitive));
        if(action->text() == QString("P2000T Teletekst Cartridge")) teletekst = action;
    }
    QVERIFY(teletekst);
    QCOMPARE(teletekst->property("image_name").toString(),
             QString("https://github.com/ifilot/p2000t-teletekst-cartridge/releases/"
                     "latest/download/p2wp-cartridge.bin"));
    QVERIFY(window.findChild<QPushButton*>("buttonReadBank"));
    QVERIFY(window.findChild<QPushButton*>("buttonWriteBank"));
    QVERIFY(window.findChild<QPushButton*>("buttonEraseBank"));
    QVERIFY(window.findChild<QGroupBox*>("groupDeviceOperations"));
    QVERIFY(window.findChild<QGroupBox*>("groupBankOperations"));
    QVERIFY(window.findChild<QGroupBox*>("groupRomOperations"));
    auto* banks = window.findChild<BankSelector*>("bankSelector");
    QVERIFY(banks);
    QCOMPARE(banks->currentBank(), 0);
    QCOMPARE(banks->text(), QString("Bank 0"));
    QVERIFY(banks->menu());
    QCOMPARE(banks->menu()->objectName(), QString("menuBankSelector"));
    QCOMPARE(banks->menu()->actions().size(), 1);
    auto* grid_widget = window.findChild<QWidget*>("bankSelectorGrid");
    QVERIFY(grid_widget);
    auto* grid = qobject_cast<QGridLayout*>(grid_widget->layout());
    QVERIFY(grid);
    QCOMPARE(grid->count(), NUMBANKS);
    for(int bank = 0; bank < NUMBANKS; ++bank) {
        auto* item = grid->itemAtPosition(bank / 4, bank % 4);
        QVERIFY(item);
        auto* button = qobject_cast<QToolButton*>(item->widget());
        QVERIFY(button);
        QCOMPARE(button->text(), QString::number(bank));
        QCOMPARE(button->objectName(), QString("buttonSelectBank%1").arg(bank));
    }
    auto* bank_15 = window.findChild<QToolButton*>("buttonSelectBank15");
    QVERIFY(bank_15);
    bank_15->click();
    QCOMPARE(banks->currentBank(), 15);
    QCOMPARE(banks->text(), QString("Bank 15"));
    QVERIFY(!window.findChild<QPushButton*>("buttonReadCartridge"));
    QVERIFY(!window.findChild<QPushButton*>("buttonScanSlots"));
    for(QAction* top : window.menuBar()->actions()) {
        QVERIFY(top->menu());
        for(QAction* action : top->menu()->actions()) QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
    }
}

void MainWindowTest::reads_selected_bank()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0xFF));
    const QByteArray bank(BANKSIZE, static_cast<char>(0x3C));
    std::copy(bank.begin(), bank.end(), flash.begin() + 11 * BANKSIZE);
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs, nullptr, factory(backend));
    addPort(window);
    QMetaObject::invokeMethod(&window, "select_com_port", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "read_chip_id", Qt::DirectConnection);
    auto* banks = window.findChild<BankSelector*>("bankSelector");
    QVERIFY(banks);
    banks->setCurrentBank(11);
    QMetaObject::invokeMethod(&window, "read_bank", Qt::DirectConnection);
    QTRY_COMPARE_WITH_TIMEOUT(window.findChild<HexViewWidget*>("hexViewWidget")->get_data(), bank, 10000);
    QVERIFY(window.statusBar()->currentMessage().contains("bank 11"));
}

void MainWindowTest::connects_current_version_and_identifies_sst39sf020()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs, nullptr, factory(backend));
    addPort(window);
    QVERIFY(QMetaObject::invokeMethod(&window, "select_com_port", Qt::DirectConnection));
    QCOMPARE(window.findChild<QLabel*>("labelBoardId")->text(), QString("Board: ") + P2000T_BOARD_INFO);
    QVERIFY(QMetaObject::invokeMethod(&window, "read_chip_id", Qt::DirectConnection));
    QVERIFY(window.findChild<QLabel*>("labelChipType")->text().contains("SST39SF020"));
    QVERIFY(window.findChild<QPushButton*>("buttonReadRom")->isEnabled());
}

void MainWindowTest::connects_compatible_previous_firmware()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(
        QByteArray(), QByteArray("P2000T-FW v0.1.0"));
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs, nullptr, factory(backend));
    addPort(window);
    QVERIFY(QMetaObject::invokeMethod(&window, "select_com_port", Qt::DirectConnection));
    QCOMPARE(window.findChild<QLabel*>("labelBoardId")->text(),
             QString("Board: P2000T-FW v0.1.0"));
    QVERIFY(window.statusBar()->currentMessage().contains("compatible with Studio 0.1.1"));
    QVERIFY(window.findChild<QPushButton*>("buttonIdentifyChip")->isEnabled());
}

void MainWindowTest::reads_complete_rom()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0x5A));
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs, nullptr, factory(backend));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString output = directory.filePath("complete.bin");
    addPort(window);
    QMetaObject::invokeMethod(&window, "select_com_port", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "read_chip_id", Qt::DirectConnection);
    QVERIFY(QMetaObject::invokeMethod(&window, "start_read_rom", Qt::DirectConnection,
                                      Q_ARG(QString, output)));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 10000);
    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), flash);
    QVERIFY(window.statusBar()->currentMessage().contains("stored 256 KiB"));
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
