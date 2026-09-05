#include <QtTest/QtTest>

#include "flashthread.h"
#include "readthread.h"
#include "support/emulated_serial_transport.h"

#include <QSignalSpy>
#include <memory>

class WorkerThreadTest : public QObject {
    Q_OBJECT

    static SerialInterface::TransportFactory factory(const std::shared_ptr<FirmwareEmulatorBackend>& backend) {
        return [backend](const std::string&) { return backend->create_transport(); };
    }

private slots:
    void reads_complete_sst39sf020();
    void reads_single_bank();
    void erase_programs_complete_sst39sf020();
    void erase_programs_single_bank();
    void rejects_other_chips();
    void surfaces_serial_errors();
    void honours_read_cancellation();
    void erases_without_programming();
};

void WorkerThreadTest::reads_complete_sst39sf020()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0xFF));
    for(int i = 0; i < flash.size(); ++i) flash[i] = static_cast<char>(i * 13);
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    ReadThread thread(serial);
    QSignalSpy ready(&thread, &ReadThread::read_result_ready);
    QSignalSpy abort(&thread, &ReadThread::thread_abort);
    QSignalSpy banks(&thread, &ReadThread::read_bank_done);
    thread.start();
    QVERIFY(thread.wait(10000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(abort.count(), 0);
    QCOMPARE(banks.count(), NUMBANKS);
    QCOMPARE(thread.get_data(), flash);
    const auto history = backend->commandHistory();
    QCOMPARE(QString::fromStdString(history.front()), QString("DEVIDSST"));
    QCOMPARE(history.size(), static_cast<size_t>(NUMBANKS + 1));
    QCOMPARE(QString::fromStdString(history.back()), QString("RDBANK0F"));
}

void WorkerThreadTest::reads_single_bank()
{
    QByteArray flash(ROMSIZE, static_cast<char>(0xFF));
    for(int i = 0; i < BANKSIZE; ++i) flash[9 * BANKSIZE + i] = static_cast<char>(i * 7);
    auto backend = std::make_shared<FirmwareEmulatorBackend>(flash);
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    ReadThread thread(serial);
    thread.set_bank_range(9, 1);
    QSignalSpy ready(&thread, &ReadThread::read_result_ready);
    thread.start();
    QVERIFY(thread.wait(10000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(thread.get_data(), flash.mid(9 * BANKSIZE, BANKSIZE));
    const auto history = backend->commandHistory();
    QCOMPARE(history.size(), static_cast<size_t>(2));
    QCOMPARE(QString::fromStdString(history.back()), QString("RDBANK09"));
}

void WorkerThreadTest::erase_programs_complete_sst39sf020()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    FlashThread thread(serial);
    QByteArray image(ROMSIZE, static_cast<char>(0xA5));
    thread.set_complete_rom(image);
    QSignalSpy ready(&thread, &FlashThread::flash_result_ready);
    QSignalSpy abort(&thread, &FlashThread::thread_abort);
    QSignalSpy blocks(&thread, &FlashThread::flash_block_done);
    thread.start();
    QVERIFY(thread.wait(10000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(abort.count(), 0);
    QCOMPARE(blocks.count(), NUMBLOCKS);
    QCOMPARE(backend->flashContents(), image);
    const auto history = backend->commandHistory();
    QCOMPARE(QString::fromStdString(history[0]), QString("DEVIDSST"));
    QCOMPARE(QString::fromStdString(history[1]), QString("ERASEALL"));
    QCOMPARE(QString::fromStdString(history.back()), QString("WRBK03FF"));
}

void WorkerThreadTest::erase_programs_single_bank()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    FlashThread thread(serial);
    QByteArray image(BANKSIZE, static_cast<char>(0xA5));
    thread.set_bank(6, image);
    QSignalSpy ready(&thread, &FlashThread::flash_result_ready);
    QSignalSpy blocks(&thread, &FlashThread::flash_block_done);
    thread.start();
    QVERIFY(thread.wait(10000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(blocks.count(), BANKSIZE / BLOCKSIZE);
    QCOMPARE(backend->flashContents().mid(6 * BANKSIZE, BANKSIZE), image);
    QCOMPARE(backend->flashContents().left(6 * BANKSIZE), QByteArray(6 * BANKSIZE, '\0'));
    QCOMPARE(backend->flashContents().mid(7 * BANKSIZE), QByteArray(ROMSIZE - 7 * BANKSIZE, '\0'));
    const auto history = backend->commandHistory();
    QCOMPARE(QString::fromStdString(history[1]), QString("ERBANK06"));
    QCOMPARE(QString::fromStdString(history.back()), QString("WRBK01BF"));
}

void WorkerThreadTest::rejects_other_chips()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(), QByteArray(P2000T_BOARD_INFO), 0xBFB7);
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    ReadThread thread(serial);
    QSignalSpy ready(&thread, &ReadThread::read_result_ready);
    QSignalSpy abort(&thread, &ReadThread::thread_abort);
    thread.start();
    QVERIFY(thread.wait(2000));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(abort.count(), 1);
}

void WorkerThreadTest::surfaces_serial_errors()
{
    auto serial = std::make_shared<SerialInterface>("");
    FlashThread thread(serial);
    thread.set_complete_rom(QByteArray(ROMSIZE, '\0'));
    QSignalSpy abort(&thread, &FlashThread::thread_abort);
    thread.start();
    QVERIFY(thread.wait(2000));
    QCOMPARE(abort.count(), 1);
    QVERIFY(abort.takeFirst().at(0).toString().contains("No port has been set"));
}

void WorkerThreadTest::honours_read_cancellation()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>();
    auto serial = std::make_shared<SerialInterface>("emu", [backend](const std::string&) {
        return backend->create_transport(1);
    });
    ReadThread thread(serial);
    QSignalSpy cancelled(&thread, &ReadThread::thread_cancelled);
    QSignalSpy ready(&thread, &ReadThread::read_result_ready);
    thread.start();
    QVERIFY(thread.isRunning());
    thread.requestInterruption();
    QVERIFY(thread.wait(2000));
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(ready.count(), 0);
}

void WorkerThreadTest::erases_without_programming()
{
    auto backend = std::make_shared<FirmwareEmulatorBackend>(QByteArray(ROMSIZE, '\0'));
    auto serial = std::make_shared<SerialInterface>("emu", factory(backend));
    FlashThread thread(serial);
    thread.set_erase_bank(4);
    QSignalSpy erased(&thread, &FlashThread::erase_result_ready);
    QSignalSpy programmed(&thread, &FlashThread::flash_result_ready);
    thread.start();
    QVERIFY(thread.wait(2000));
    QCOMPARE(erased.count(), 1);
    QCOMPARE(programmed.count(), 0);
    QCOMPARE(backend->flashContents().mid(4 * BANKSIZE, BANKSIZE),
             QByteArray(BANKSIZE, static_cast<char>(0xFF)));
}

QTEST_MAIN(WorkerThreadTest)
#include "tst_worker_threads.moc"
