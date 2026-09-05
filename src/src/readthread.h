#ifndef READTHREAD_H
#define READTHREAD_H

#include "ioworker.h"
#include "romsizes.h"

/** Read the complete 256 KiB SST39SF020 using 16 KiB bank reads. */
class ReadThread : public IOWorker {
    Q_OBJECT

public:
    ReadThread() = default;
    explicit ReadThread(const std::shared_ptr<SerialInterface>& serial_interface)
        : IOWorker(serial_interface) {}

    void set_bank_range(unsigned int first_bank, unsigned int bank_count);
    void run() override;

signals:
    void read_result_ready();
    void read_bank_start(unsigned int bank_id, unsigned int nr_banks);
    void read_bank_done(unsigned int bank_id, unsigned int nr_banks);

private:
    unsigned int first_bank = 0;
    unsigned int bank_count = NUMBANKS;
};

#endif // READTHREAD_H
