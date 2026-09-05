#ifndef SST39SF020_H
#define SST39SF020_H
#include <stdint.h>
#include <stdbool.h>
void sst_init(void);
void sst_read_id(uint8_t id[2]);
uint8_t sst_read_block(uint16_t block, uint8_t data[256]);
uint8_t sst_program_block(uint16_t block, const uint8_t data[256]);
uint8_t sst_erase(void);
uint8_t sst_erase_bank(uint8_t bank);
#endif
