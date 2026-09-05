#include <QtTest/QtTest>

#include "config.h"
#include "bankselector.h"
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
    void exposes_only_supported_operations();
    void connects_current_version_and_identifies_sst39sf020();
    void reads_selected_bank();
    void reads_complete_rom();
};

void MainWindowTest::exposes_only_supported_operations()
{
    auto logs = std::make_shared<QStringList>();
    MainWindow window(logs);
    QVERIFY(window.findChild<QPushButton*>("buttonIdentifyChip"));
    QVERIFY(window.findChild<QPushButton*>("buttonReadRom"));
    QVERIFY(window.findChild<QPushButton*>("buttonFlashRom"));
    QVERIFY(window.findChild<QPushButton*>("buttonEraseChip"));
    QVERIFY(window.findChild<QPushButton*>("buttonInstallFirmware"));
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
