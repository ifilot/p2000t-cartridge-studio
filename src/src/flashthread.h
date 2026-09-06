#ifndef FLASHTHREAD_H
#define FLASHTHREAD_H

#include "ioworker.h"
#include "romsizes.h"

/** Erase and program either one 16 KiB bank or the complete SST39SF020. */
class FlashThread : public IOWorker {
    Q_OBJECT

public:
    FlashThread() = default;
    explicit FlashThread(const std::shared_ptr<SerialInterface>& serial_interface)
        : IOWorker(serial_interface) {}

    void set_complete_rom(const QByteArray& data);
    void set_bank(unsigned int bank_index, const QByteArray& data);
    void set_erase_complete();
    void set_erase_bank(unsigned int bank_index);
    void run() override;

signals:
    void flash_result_ready();
    void erase_result_ready();
    void flash_block_start(unsigned int block_id, unsigned int nr_blocks);
    void flash_block_done(unsigned int block_id, unsigned int nr_blocks);

private:
    bool complete_rom = true;
    bool erase_only = false;
    unsigned int bank_index = 0;
};

#endif // FLASHTHREAD_H
