#include <avr/io.h>
#include <util/delay.h>
#include "sst39sf020.h"

/* Current PCB: PORTD is D0..D7, shared by flash data and all latch inputs.
   ST=0 (USB supply selected) routes programmer signals through U4/U5/U10.
   Operate on the bench with the cartridge disconnected from the P2000T. */
#define WE _BV(PB4)
#define CE _BV(PB5)
#define OE _BV(PB6)
#define ADDR_OE _BV(PB7)
#define LE_LOW _BV(PF0)
#define LE_HIGH _BV(PF1)
#define LE_UPPER _BV(PF4)
#define LATCHES (LE_LOW | LE_HIGH | LE_UPPER)

static void idle(void)
{
    PORTB |= WE | CE | OE | ADDR_OE;
    _delay_us(1);             /* flash outputs must release before MCU drives */
    DDRD = 0;
    PORTD = 0;                /* no data-bus pull-ups */
}

void sst_init(void)
{
    /* Set inactive levels BEFORE enabling output drivers, avoiding WE pulses. */
    PORTB |= WE | CE | OE | ADDR_OE;
    DDRB |= WE | CE | OE | ADDR_OE;
    PORTF &= ~LATCHES;
    DDRF |= LATCHES;          /* JTAG must already be disabled for PF4 */
    idle();
}

static void latch(uint8_t enable, uint8_t value)
{
    PORTD = value;
    _delay_us(1);
    PORTF |= enable;
    _delay_us(1);
    PORTF &= ~enable;
    _delay_us(1);
}

static void address(uint32_t value)
{
    PORTB |= WE | CE | OE | ADDR_OE;
    _delay_us(1);
    DDRD = 0xff;
    latch(LE_UPPER, (value >> 16) & 3); /* U6: A16..A17 */
    latch(LE_HIGH, value >> 8);        /* U8: A8..A15 */
    latch(LE_LOW, value);             /* U7: A0..A7 */
    PORTB &= ~ADDR_OE;
    _delay_us(1);
}

/* SST command/data write cycle. */
static void command_write(uint32_t addr, uint8_t data)
{
    address(addr);
    PORTD = data;
    _delay_us(1);
    PORTB &= ~CE;
    _delay_us(1);
    PORTB &= ~WE;
    _delay_us(1);
    PORTB |= WE;
    _delay_us(1);
    PORTB |= CE;
}

static uint8_t read_byte(uint32_t addr)
{
    address(addr);
    DDRD = 0;
    PORTD = 0;
    PORTB &= ~CE;
    PORTB &= ~OE;
    _delay_us(1);
    uint8_t result = PIND;
    PORTB |= OE | CE;
    _delay_us(1);
    return result;
}

static void id_command(uint8_t command)
{
    command_write(0x5555, 0xaa);
    command_write(0x2aaa, 0x55);
    command_write(0x5555, command);
    _delay_us(150);          /* conservative ID-mode entry/exit settling */
}

void sst_read_id(uint8_t id[2])
{
    id_command(0x90);
    id[0] = read_byte(0);
    id[1] = read_byte(1);
    id_command(0xf0);       /* always restore normal read mode before replying */
    idle();
}

/* Serviced by main.c during long operations; no incoming commands execute here. */
extern void sst_service(void);

static bool correct_chip(void)
{
    uint8_t id[2];
    sst_read_id(id);
    return id[0] == 0xbf && id[1] == 0xb6;
}

uint8_t sst_read_block(uint16_t block, uint8_t data[256])
{
    if (block >= 1024) return 2;

    /* A0..A7 share PORTD with the flash data bus, but A8..A17 remain
       constant throughout a block. Latch the full start address once, then
       update only the low latch for each byte. This removes two latch cycles
       and the address-output toggle from every sequential read. */
    address((uint32_t)block << 8);
    DDRD = 0;
    PORTD = 0;
    PORTB &= ~CE;

    for (uint16_t i = 0; i < 256; ++i) {
        /* Release the flash before PORTD drives the shared latch bus. The
           AVR instructions already exceed the SST39SF020 output-disable
           interval before DDRD becomes an output. */
        PORTB |= OE;
        DDRD = 0xff;
        latch(LE_LOW, (uint8_t)i);

        /* Return PORTD to input before enabling flash output. */
        DDRD = 0;
        PORTD = 0;
        PORTB &= ~OE;
        _delay_us(1);
        data[i] = PIND;
    }

    PORTB |= OE | CE;
    idle();
    return 0;
}

uint8_t sst_program_block(uint16_t block, const uint8_t data[256])
{
    if (block >= 1024) return 2;
    if (!correct_chip()) return 3;
    uint32_t start = (uint32_t)block << 8;
    /* Reject zero-to-one transitions before modifying any byte in this block. */
    for (uint16_t i = 0; i < 256; ++i) {
        if ((read_byte(start + i) & data[i]) != data[i]) { idle(); return 6; }
    }
    for (uint16_t i = 0; i < 256; ++i) {
        if (read_byte(start + i) == data[i]) continue;
        command_write(0x5555, 0xaa);
        command_write(0x2aaa, 0x55);
        command_write(0x5555, 0xa0);
        command_write(start + i, data[i]);
        /* DQ7 data polling, followed by full-byte comparison once ready.
           100 attempts is comfortably above the specified 20 us maximum. */
        uint8_t attempts = 100;
        while (((read_byte(start + i) ^ data[i]) & 0x80) && --attempts) _delay_us(1);
        if (!attempts) { idle(); return 4; }
        _delay_us(1);
        if (read_byte(start + i) != data[i]) { idle(); return 5; }
        sst_service();
    }
    idle();
    return 0;
}

uint8_t sst_erase(void)
{
    if (!correct_chip()) return 3;
    command_write(0x5555, 0xaa);
    command_write(0x2aaa, 0x55);
    command_write(0x5555, 0x80);
    command_write(0x5555, 0xaa);
    command_write(0x2aaa, 0x55);
    command_write(0x5555, 0x10);
    /* Chip erase maximum is bounded here by one second; keep USB serviced. */
    uint16_t attempts = 1000;
    while (!(read_byte(0) & 0x80) && --attempts) {
        _delay_ms(1);
        sst_service();
    }
    if (!attempts) { idle(); return 4; }

    /* Completion polling at one address does not prove that every sector was
       erased. Blank-check the complete device before reporting success. */
    uint8_t data[256];
    for (uint16_t block = 0; block < 1024; ++block) {
        if (sst_read_block(block, data)) { idle(); return 5; }
        for (uint16_t offset = 0; offset < sizeof(data); ++offset)
            if (data[offset] != 0xff) { idle(); return 5; }
        sst_service();
    }
    idle();
    return 0;
}

static uint8_t erase_sector(uint32_t start)
{
    command_write(0x5555, 0xaa);
    command_write(0x2aaa, 0x55);
    command_write(0x5555, 0x80);
    command_write(0x5555, 0xaa);
    command_write(0x2aaa, 0x55);
    command_write(start, 0x30);

    uint16_t attempts = 1000;
    while (!(read_byte(start) & 0x80) && --attempts) {
        _delay_ms(1);
        sst_service();
    }
    return attempts ? 0 : 4;
}

uint8_t sst_erase_bank(uint8_t bank)
{
    if (bank >= 16) return 2;
    if (!correct_chip()) return 3;
    uint32_t start = (uint32_t)bank << 14;
    for (uint8_t sector = 0; sector < 4; ++sector) {
        uint8_t status = erase_sector(start + ((uint32_t)sector << 12));
        if (status) { idle(); return status; }
    }
    for (uint16_t offset = 0; offset < 0x4000; ++offset) {
        if (read_byte(start + offset) != 0xff) { idle(); return 5; }
        if (!(offset & 0xff)) sst_service();
    }
    idle();
    return 0;
}
