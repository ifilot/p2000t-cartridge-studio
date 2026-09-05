#include <string.h>
#include "protocol.h"
#ifndef P2000T_VERSION
#error P2000T_VERSION must be supplied by the firmware build
#endif
#define BOARD_INFO "P2000T-FW p01.00"
_Static_assert(sizeof(BOARD_INFO) - 1 == 16, "READINFO must be exactly 16 bytes");
#if P2000T_VERSION_MAJOR > 255 || P2000T_VERSION_MINOR > 255 || P2000T_VERSION_PATCH > 255
#error Firmware semantic-version components must fit in one byte
#endif
void protocol_reset(protocol_t *p) { p->used = 0; p->idle_ms = 0; p->writing = false; p->failed = false; p->bank_read_pending = false; p->received = 0; }
bool protocol_tick(protocol_t *p, uint16_t ms)
{
    if (!p->used && !p->writing) return false;
    uint16_t limit = p->writing ? PAYLOAD_TIMEOUT_MS : COMMAND_TIMEOUT_MS;
    if (ms >= limit - p->idle_ms) {
        bool was_writing = p->writing;
        protocol_reset(p);
        p->failed = was_writing;
        return was_writing;
    }
    p->idle_ms += ms;
    return false;
}
bool protocol_take_bank_read(protocol_t *p, uint8_t *bank)
{
    if (!p->bank_read_pending) return false;
    *bank = p->bank;
    p->bank_read_pending = false;
    return true;
}
uint16_t protocol_crc_update(uint16_t crc, const uint8_t *data, uint16_t size)
{
    while (size--) {
        crc ^= (uint16_t)*data++ << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
uint16_t protocol_crc(const uint8_t *data, uint16_t size)
{
    return protocol_crc_update(0, data, size);
}
static bool block_number(const uint8_t *s, uint16_t *result)
{
    uint16_t v = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t c = s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | d;
    }
    *result = v;
    return v < 1024;
}
static bool bank_number(const uint8_t *s, uint8_t *result)
{
    uint8_t v = 0;
    for (uint8_t i = 0; i < 2; ++i) {
        uint8_t c = s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | d;
    }
    *result = v;
    return v < 16;
}
uint16_t protocol_feed(protocol_t *p, uint8_t byte, uint8_t out[RESPONSE_MAX], const protocol_ops_t *ops)
{
    if (p->failed) return 0;
    p->idle_ms = 0;
    if (p->writing) {
        p->payload[p->received++] = byte;
        if (p->received < 258) return 0;
        p->writing = false;
        uint16_t crc = ((uint16_t)p->payload[256] << 8) | p->payload[257];
        out[0] = crc == protocol_crc(p->payload, 256) ? ops->program_block(p->block, p->payload) : 1;
        return 1;
    }
    if (!p->used && (byte == '\r' || byte == '\n')) return 0;
    p->command[p->used++] = byte;
    if (p->used != 8) return 0;
    memcpy(out, p->command, 8);
    p->used = 0;
    if (!memcmp(out, "READINFO", 8)) { memcpy(out + 8, BOARD_INFO, sizeof(BOARD_INFO) - 1); return 8 + sizeof(BOARD_INFO) - 1; }
    if (!memcmp(out, "READVERS", 8)) {
        out[8] = P2000T_VERSION_MAJOR;
        out[9] = P2000T_VERSION_MINOR;
        out[10] = P2000T_VERSION_PATCH;
        return 11;
    }
    if (!memcmp(out, "DEVIDSST", 8)) { ops->read_id(out + 8); return 10; }
    if (!memcmp(out, "BOOTLOAD", 8)) { out[8] = 0; ops->boot(); return 9; }
    if (!memcmp(out, "ERASEALL", 8)) { out[8] = ops->erase(); return 9; }
    if (!memcmp(out, "RDBANK", 6)) {
        if (!bank_number(out + 6, &p->bank)) { out[8] = 2; return 9; }
        p->bank_read_pending = true;
        out[8] = 0;
        return 9;
    }
    if (!memcmp(out, "ERBANK", 6)) {
        uint8_t bank;
        if (!bank_number(out + 6, &bank)) { out[8] = 2; return 9; }
        out[8] = ops->erase_bank(bank);
        return 9;
    }
    bool write = !memcmp(out, "WRBK", 4);
    if (write || !memcmp(out, "RDBK", 4)) {
        if (!block_number(out + 4, &p->block)) { out[8] = 2; return 9; }
        if (write) {
            p->writing = true; p->received = 0; out[8] = 0; return 9;
        }
        out[8] = ops->read_block(p->block, out + 9);
        if (out[8]) return 9;
        uint16_t crc = protocol_crc(out + 9, 256);
        out[265] = crc >> 8; out[266] = crc;
        return 267;
    }
    memcpy(out + 8, "ERRORCMD", 8);
    return 16;
}
