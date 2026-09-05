# P2000T SLOT1: 16-bank display test

Generates `firmware/build/bank-test/p2000t-bank-test-16x16k.bin`: exactly
262144 bytes, 16 banks of 16384 bytes each, numbered **00–15**. Individual
`bank-00.bin` to `bank-15.bin` and a SHA256/address manifest are generated
alongside it. All outputs live in the ignored firmware `build/` directory;
source and tests can be committed.

## Build

From the repository root, with WSL `z80asm` and Python 3 installed:

```sh
python3 roms/banktest/build.py
```

Each bank displays its decimal number, the logical A17..A14 bank-select bits,
and four labels from distinct 4 KiB portions of that bank. All four labels
should show the same bank number and quarter numbers 0, 1, 2, 3. This helps
expose address selection errors in addition to bank-selection errors.

Expected screen (example bank 05; the bank number uses double-height text):

```text
  P2000T SLOT1 BANK TEST

  BANK 05  (00-15)

  A17 A16 A15 A14 = 0101

  ROM QUARTER 0: BANK 05
  ROM QUARTER 1: BANK 05
  ROM QUARTER 2: BANK 05
  ROM QUARTER 3: BANK 05

  CHANGE BANK, THEN PRESS SOFT RESET
```

The combined image was flashed to the connected SST39SF020 on 2026-09-05.
All 262144 read-back bytes matched, with the SHA256 listed below. This replaced
the temporary random test pattern; the cartridge now contains this bank test.

## Test on a P2000T

Disconnect USB/USBasp, insert the cartridge in **port 1 with the P2000T off**,
then switch on. Set the DIP bank selection and press the cartridge soft-reset
button to redraw the newly selected bank. The image includes the monitor's
NMI entry at 0x1016; a power cycle also restarts it. No keyboard input is needed.

| Bank | A17 A16 A15 A14 | Image offset |
| --- | --- | --- |
| 00 | 0000 | 00000 |
| 01 | 0001 | 04000 |
| 02 | 0010 | 08000 |
| 03 | 0011 | 0C000 |
| 04 | 0100 | 10000 |
| 05 | 0101 | 14000 |
| 06 | 0110 | 18000 |
| 07 | 0111 | 1C000 |
| 08 | 1000 | 20000 |
| 09 | 1001 | 24000 |
| 10 | 1010 | 28000 |
| 11 | 1011 | 2C000 |
| 12 | 1100 | 30000 |
| 13 | 1101 | 34000 |
| 14 | 1110 | 38000 |
| 15 | 1111 | 3C000 |

The bit columns describe electrical address levels, not a presumed left/right
orientation of the physical DIP switch. The correct screen bank number must
match the selected electrical bits. Check every bank, and check that all four
quarter labels agree. Switching banks without reset deliberately leaves the
previous screen visible until the NMI restart.

## Implementation and verification

The header is the monitor-compatible normal cartridge signature 5C, zero
checksum length/checksum, and an 11-byte name. Bit 3 skips a second header;
bit 1 is clear to avoid requesting a disk boot. Normal execution enters at
0x1010; NMI enters at 0x1016. The program sets its stack to base RAM at 0x6200,
disables keyboard interrupts, clears the 2 KiB video RAM, and copies the bank's
messages to screen RAM at 0x5000 with the P2000T's 80-byte row stride. It halts
until reset/NMI. These conventions were checked against the local documented
monitor disassembly and the existing P2000T-IDE Hello World and tape-monitor
screen code.

Optional CPU execution tests use the MIT-licensed
[superzazu/z80 emulator](https://github.com/superzazu/z80) pinned to commit
`d64fe10a2274e5e40019b1086bf7d8990cbc5f23`:

```sh
git clone https://github.com/superzazu/z80.git /tmp/p2000-bank-z80
git -C /tmp/p2000-bank-z80 checkout d64fe10a2274e5e40019b1086bf7d8990cbc5f23
python3 roms/banktest/test.py --emulator-dir /tmp/p2000-bank-z80
```

The tests execute the assembled bytes for each bank from the monitor hand-off,
check screen RAM and ROM reads across all four quarters, then switch banks and
trigger NMI to check redraw. They do not emulate the complete P2000T boot ROM,
teletext video timing, or physical DIP/reset wiring. Those remain the purpose
of the actual P2000T test.

Image SHA256:
`226a2515b29281fc3d420a0a6f67fe9844da39a94c040c5396ff9799a895a80c`.
