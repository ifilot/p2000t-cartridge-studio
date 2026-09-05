/* Execute the actual assembled ROM with superzazu/z80 (MIT), not a model of
   the assembly source. Video RAM/port behaviour is limited to what this ROM uses. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80.h"
static unsigned char image[262144], ram[65536];
static unsigned bank, quarters;
static uint8_t read_memory(void *unused, uint16_t address) {
    (void)unused;
    if (address >= 0x1000 && address < 0x5000) {
        quarters |= 1u << ((address - 0x1000) / 4096);
        return image[bank * 16384 + address - 0x1000];
    }
    return ram[address];
}
static void write_memory(void *unused, uint16_t address, uint8_t value) {
    (void)unused;
    assert(address >= 0x5000 && address < 0x6200);
    ram[address] = value;
}
static uint8_t port_in(z80 *cpu, uint8_t port) { (void)cpu; (void)port; return 255; }
static void port_out(z80 *cpu, uint8_t port, uint8_t value) { (void)cpu; assert(port == 0x10 && value == 0); }
static void run(z80 *cpu) {
    unsigned steps = 0;
    do { z80_step(cpu); assert(++steps < 20000); } while (!cpu->halted);
    assert(!cpu->iff1 && cpu->sp == 0x6200 && quarters == 15);
}
static void screen_check(void) {
    char text[40];
    assert(!memcmp(ram + 0x50a3, "P2000T SLOT1 BANK TEST", 21));
    assert(ram[0x5192] == 7 && ram[0x5193] == 13);
    snprintf(text, sizeof text, "BANK %02u  (00-15)", bank);
    assert(!memcmp(ram + 0x5194, text, strlen(text)));
    for (unsigned q = 0; q < 4; ++q) {
        snprintf(text, sizeof text, "ROM QUARTER %u: BANK %02u", q, bank);
        assert(!memcmp(ram + 0x53c3 + q * 80, text, strlen(text)));
    }
    snprintf(text, sizeof text, "A17 A16 A15 A14 = %u%u%u%u", (bank>>3)&1,(bank>>2)&1,(bank>>1)&1,bank&1);
    assert(!memcmp(ram + 0x52d3, text, strlen(text)));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    assert(fread(image,1,sizeof image,file) == sizeof image && fgetc(file) == EOF); fclose(file);
    for (unsigned initial = 0; initial < 16; ++initial) {
        bank = initial; quarters = 0; memset(ram,0xcc,sizeof ram);
        /* Real P2000 monitor NMI vector: JP 1016. */
        ram[0x66]=0xc3; ram[0x67]=0x16; ram[0x68]=0x10;
        z80 cpu; z80_init(&cpu);
        cpu.read_byte=read_memory; cpu.write_byte=write_memory;
        cpu.port_in=port_in; cpu.port_out=port_out; cpu.pc=0x1010;
        run(&cpu); screen_check();
        bank = (initial + 1) % 16; quarters = 0;
        z80_gen_nmi(&cpu); run(&cpu); screen_check();
        printf("Bank %02u: entry/display PASS; live bank switch + NMI to %02u PASS\n",initial,bank);
    }
}
