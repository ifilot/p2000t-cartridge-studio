#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#define COMMAND_SIZE 8
#define RESPONSE_MAX 267
#define COMMAND_TIMEOUT_MS 1000
#define PAYLOAD_TIMEOUT_MS 2000
typedef struct {
    void (*read_id)(uint8_t id[2]);
    uint8_t (*read_block)(uint16_t block, uint8_t data[256]);
    uint8_t (*program_block)(uint16_t block, const uint8_t data[256]);
    uint8_t (*erase)(void);
    uint8_t (*erase_bank)(uint8_t bank);
    void (*boot)(void);
} protocol_ops_t;
typedef struct {
    uint8_t command[8];
    uint8_t used;
    uint16_t idle_ms;
    uint16_t received, block;
    uint8_t bank;
    bool writing, failed, bank_read_pending;
    uint8_t payload[258];
} protocol_t;
void protocol_reset(protocol_t *parser);
/* True means a write-payload timeout: send status 7, require DTR reset. */
bool protocol_tick(protocol_t *parser, uint16_t elapsed_ms);
bool protocol_take_bank_read(protocol_t *parser, uint8_t *bank);
uint16_t protocol_crc_update(uint16_t crc, const uint8_t *data, uint16_t size);
uint16_t protocol_crc(const uint8_t *data, uint16_t size);
uint16_t protocol_feed(protocol_t *parser, uint8_t byte, uint8_t response[RESPONSE_MAX],
                       const protocol_ops_t *ops);
#endif
