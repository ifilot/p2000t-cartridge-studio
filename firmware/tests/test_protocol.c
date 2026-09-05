#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

#define BOARD_INFO "P2000T-FW p01.00"

static unsigned id_calls;
static void identify(uint8_t id[2]) { ++id_calls; id[0] = 0xbf; id[1] = 0xb6; }
static unsigned writes, erases, bank_erases, boots;
static uint8_t read_block(uint16_t block, uint8_t data[256]) { assert(block < 1024); memset(data, 0x5a, 256); return 0; }
static uint8_t program(uint16_t block, const uint8_t data[256]) { assert(block == 1023 && data[0] == 0x5a); ++writes; return 0; }
static uint8_t erase(void) { ++erases; return 0; }
static uint8_t erase_bank(uint8_t bank) { assert(bank < 16); ++bank_erases; return 0; }
static void boot(void) { ++boots; }
static const protocol_ops_t ops = { identify, read_block, program, erase, erase_bank, boot };
static protocol_t parser;
static uint8_t response[RESPONSE_MAX];

static uint16_t send(const char *bytes, size_t size)
{
    uint16_t result = 0;
    for (size_t i = 0; i < size; ++i) {
        uint16_t count = protocol_feed(&parser, bytes[i], response, &ops);
        if (count) { assert(!result); result = count; }
    }
    return result;
}

int main(void)
{
    /* Fragmentation at every possible USB packet split must preserve framing. */
    for (unsigned split = 1; split < 8; ++split) {
        protocol_reset(&parser);
        assert(send("DEVIDSST", split) == 0);
        protocol_tick(&parser, 50);
        assert(send("DEVIDSST" + split, 8 - split) == 10);
        assert(!memcmp(response, "DEVIDSST\xbf\xb6", 10));
    }
    assert(id_calls == 7);
    assert(send("\r\nREADINFO\r\n", 12) == 24);
    assert(!memcmp(response, "READINFO" BOARD_INFO, 24));
    assert(send("READVERS", 8) == 11);
    assert(!memcmp(response, "READVERS", 8));
    assert(response[8] == P2000T_VERSION_MAJOR &&
           response[9] == P2000T_VERSION_MINOR &&
           response[10] == P2000T_VERSION_PATCH);
    assert(send("BADCMAND", 8) == 16);
    assert(!memcmp(response, "BADCMANDERRORCMD", 16));
    assert(id_calls == 7); /* unsupported/destructive commands never touch flash */

    /* Invalid characters count as bytes; cannot be filtered into DEVIDSST. */
    assert(send("DEV!DSST", 8) == 16);
    assert(!memcmp(response + 8, "ERRORCMD", 8));
    assert(send("DEVI", 4) == 0);
    protocol_tick(&parser, 999);
    assert(parser.used == 4);
    protocol_tick(&parser, 1);
    assert(parser.used == 0);
    assert(send("DEVIDSST", 8) == 10);
    assert(send("DEVI", 4) == 0);
    protocol_reset(&parser); /* disconnect / DTR drop */
    assert(send("READINFO", 8) == 24);

    /* Back-to-back commands in one stream need no delimiters. */
    unsigned responses = 0;
    const char stream[] = "READINFODEVIDSSTDEVIDSST";
    for (unsigned i = 0; i < sizeof(stream) - 1; ++i)
        if (protocol_feed(&parser, stream[i], response, &ops)) ++responses;
    assert(responses == 3 && id_calls == 10);
    assert(protocol_crc((const uint8_t*)"123456789", 9) == 0x31c3);
    assert(protocol_crc_update(protocol_crc((const uint8_t*)"1234", 4),
                               (const uint8_t*)"56789", 5) == 0x31c3);
    uint8_t bank;
    assert(send("RDBANK0F", 8) == 9 && response[8] == 0);
    assert(protocol_take_bank_read(&parser, &bank) && bank == 15);
    assert(!protocol_take_bank_read(&parser, &bank));
    assert(send("RDBANK10", 8) == 9 && response[8] == 2);
    assert(!protocol_take_bank_read(&parser, &bank));
    assert(send("RDBANK0G", 8) == 9 && response[8] == 2);
    assert(send("ERBANK0F", 8) == 9 && response[8] == 0 && bank_erases == 1);
    assert(send("ERBANK10", 8) == 9 && response[8] == 2 && bank_erases == 1);
    assert(send("WRBK0400", 8) == 9 && response[8] == 2);
    assert(send("WRBK00G0", 8) == 9 && response[8] == 2);
    assert(send("RDBK03FF", 8) == 267 && response[8] == 0);
    assert(response[9] == 0x5a && response[264] == 0x5a);
    uint8_t payload[258]; memset(payload, 0x5a, 256);
    uint16_t crc = protocol_crc(payload, 256);
    payload[256] = crc >> 8; payload[257] = crc;
    assert(send("WRBK03FF", 8) == 9 && response[8] == 0);
    assert(send((char*)payload, 257) == 0 && writes == 0);
    assert(send((char*)payload + 257, 1) == 1 && response[0] == 0 && writes == 1);
    payload[257] ^= 1;
    assert(send("WRBK03FF", 8) == 9);
    assert(send((char*)payload, 258) == 1 && response[0] == 1 && writes == 1);
    assert(send("WRBK03FF", 8) == 9);
    assert(send((char*)payload, 50) == 0);
    assert(protocol_tick(&parser, 2000));
    assert(send("ERASEALL", 8) == 0 && erases == 0); /* poisoned binary stream */
    protocol_reset(&parser);
    assert(send("ERASEALL", 8) == 9 && erases == 1);
    assert(send("BOOTLOAD", 8) == 9 && boots == 1);
    puts("Protocol tests passed (framing, invalid commands, timeout, reconnect).");
}
