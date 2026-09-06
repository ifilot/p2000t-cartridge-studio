# P2000T cartridge firmware

ATmega32U4 native USB serial firmware for the SST39SF020 cartridge. The
application supports USB bootloader entry, flash identification, chip erase,
256-byte programming and read-back. Flash operations are for the cartridge
**outside the P2000T**, with USB supplying the PCB (ST=0). ST is not connected
to an MCU input.

## Build

Install the build tools in WSL:

```sh
sudo apt update
sudo apt install gcc-avr avr-libc binutils-avr build-essential make git python3
```

Then, from the root of this repository, build and test the firmware:

```sh
cd firmware
sh fetch-lufa.sh
make -j4 combined test
```

The generated files are written to `build/`: `cartridge.hex`, `bootloader.hex`,
and the combined ISP image `combined.hex`.

## Bootloader layout

Initial bootloader installation uses an ISP programmer on J3: MISO,
USB_5V_RAW, SCK, MOSI, RESET, GND. Do not connect two power sources to the
shared USB_5V_RAW rail. The installation image contains both the application
and bootloader; use the Windows preparation utility in
`tools/bootloader-installer` for its programming workflow.

| Setting | Before bootloader | With bootloader |
| --- | --- | --- |
| Low fuse | 5E | 5E (16 MHz crystal; CKDIV8 overridden in software) |
| High fuse | 99 | 98 (BOOTRST enabled; ISP still enabled) |
| Extended fuse | FB | FB (unconnected HWB disabled) |
| Lock byte | FF | FF (unlocked) |
| Application region | starts at 0000 | 0000–6FFF (28 KiB) |
| Bootloader region | unused | 7000–7FFF (4 KiB) |

ISP remains a recovery option. Do not upload an application-only HEX using a
chip-erasing ISP command once the bootloader is installed; the combined image
is intended for ISP recovery.

## Subsequent firmware updates: USB only

After bootloader installation, normal updates use the USB serial bootloader.
Host tooling must validate that the application HEX remains below `0x7000`, send
`BOOTLOAD`, wait for the AVR109 bootloader, program with AVRDUDE using
`-c avr109`, verify the result, then wait for the application to re-enumerate.
P2000T Cartridge Studio in this repository owns this host workflow. Do not use
a chip-erasing ISP command with an application-only image after bootloader
installation.

Application USB ID: `03EB:2044`, product `P2000T Cartridge Serial`.
Bootloader USB ID: `03EB:204A`, product `P2000T USB Boot`.

The size-constrained bootloader intentionally implements AVR109 flash access,
not EEPROM or fuse/lock reads. Studio invokes AVRDUDE only for application
flash erase, write and verification; ISP remains the fuse and factory-recovery
path.

The application uses 64-byte CDC bulk endpoints. Sequential block reads latch
A8..A17 once and update only A0..A7 for each byte; PORTD is returned to input
before flash output is enabled because the low-address latch and flash data bus
share that port.
These are LUFA demo IDs for this bench prototype, not project-owned IDs for
distribution. Both use Windows' built-in usbser driver; do not apply the
USBasp's WinUSB driver to either serial device. COM numbers can differ.

On power-up/reset, the bootloader offers approximately 5.2 seconds for a host
to connect, then starts an application only when its manifest and CRC32 are
valid. BOOTLOAD explicitly requests
bootloader mode with no timeout. Receiving a bootloader command also cancels
the startup window; a stalled command transfer resets after about two seconds.
A blank or invalid application keeps the bootloader active. AVRDUDE exits it
through a watchdog reset after programming. If an application crashes and
cannot accept BOOTLOAD, reset/power-cycle the board and connect during the boot
window, or use ISP. USB alone cannot remotely reset an entirely unresponsive
application; the boot window is the recovery path. A partially uploaded app is
not considered valid by a checksum, so use this recovery path after interrupted
uploads rather than relying on automatic startup.

## Program and verify SST39SF020

The host GUI identifies the cartridge, reads its SST ID, and performs
erase/program/read-back operations through the USB application protocol.

**Complete-flash programming erases the entire SST39SF020.** Studio requires an
exact 262144-byte complete image; bank programming requires exactly 16384
bytes. It does not silently pad either input. No old ROM contents are backed
up, so read the flash to a file first when a backup is required.

Host tooling identifies BF B6, erases, transmits 256-byte blocks with
CRC16-XMODEM, checks every operation's status, then independently reads all
262144 bytes, validates each read CRC and compares every byte with the padded
input. It reports the first mismatching address or a successful full-image
SHA256.
Firmware checks CRC before writing, rejects zero-to-one transitions before
modifying a block, polls program completion with a finite bound, and checks the
programmed bytes. Chip erase has a finite completion poll and then blank-checks
all 262144 bytes; USB is serviced while operations run. A failure/disconnect can leave a partially programmed
ROM; rerun the complete erase/write/verify operation.

## Serial protocol

Eight-byte ASCII commands followed by the exact eight-byte echo. No unsolicited
text, NUL terminators or newlines. Assert DTR. Commands may span USB packets;
CR/LF is ignored only at frame boundaries. One request at a time is recommended.
`READINFO` reports a fixed protocol identity. `READVERS` separately reports the
semantic product version from the repository's root `VERSION` file, so product
versions are no longer constrained by the fixed-width identity or USB BCD
descriptor.

| Command | After echo |
| --- | --- |
| READINFO | 16 ASCII bytes: `P2000T-FW p01.00` (protocol 1.0) |
| READVERS | 3 raw bytes: semantic-version major, minor and patch (0–255 each) |
| DEVIDSST | 2 raw bytes: manufacturer BF, device B6 |
| BOOTLOAD | status byte, then USB disconnect/re-enumeration |
| ERASEALL | status after completion |
| RDBKhhhh | status, then on success 256 data bytes and 2 CRC bytes |
| WRBKhhhh | ready status; host then sends 256 data bytes + 2 CRC bytes; device returns final status (no second echo) |
| RDBANKbb | status, then on success 16384 data bytes and 2 CRC bytes |
| ERBANKbb | status after erasing and blank-checking the complete 16 KiB bank |
| Unknown command | 8 ASCII bytes: ERRORCMD |

`hhhh` is uppercase hexadecimal block index 0000–03FF, address = index * 256.
`bb` is uppercase hexadecimal bank index 00–0F, address = index * 16384.
CRC16-XMODEM: polynomial 0x1021, initial 0, no reflection/final xor; high byte
first. The `RDBANKbb` CRC covers the complete 16 KiB payload. Status: 0
success, 1 CRC mismatch, 2 invalid address, 3 wrong chip,
4 operation timeout, 5 verification failure, 6 needs erase, 7 payload timeout.
Errors on reads contain only status, no data or CRC. Each write is buffered
fully before programming. After a two-second gap in a binary write payload,
status 7 is returned and further bytes are ignored until DTR drops or USB
reconnects; this prevents late payload bytes being treated as commands. Partial
ASCII commands expire after one second. Transport errors should be recovered
by closing/reopening the port before retrying. Bank erase uses four native 4 KiB
sector-erase operations and verifies every byte in the bank is `FF`.

BF B6 is a manufacturer/model ID, not a unique serial number per chip.

## Wiring and protocol review

The [PICO protocol](https://github.com/ifilot/pico-sst39sf0x0-programmer/tree/8032ec93e67729c8ebcb56f8ca86b993c33f13a5/firmware)
is a good basis: fixed commands, command echo, known-length binary payloads.
This implementation adds stricter framing, explicit status, bounds, timeouts
and a consistent CRC. New read/write responses are not byte-for-byte compatible
with the PICO programmer; hosts must use the current layouts above.

Checked the current schematic/PCB, not the stale exported XML netlist:
PORTD is D0–D7 and feeds both SST data and all three 74HC573 latch inputs.
PF0=LE1/U7/A0–A7, PF1=LE2/U8/A8–A15, PF4=LE3/U6/A16–A17.
PB7=OE_ADDR#, PB4=PROG#/WE#, PB5=CS_PROG#, PB6=OE_PROG#.
ST=0 routes these through U4/U5/U10. Firmware disables JTAG because PF4 is LE3.
All control lines are set inactive before enabling output drivers. Each address
load disables SST outputs, drives the shared data bus into the three latches,
then enables addresses. Reads release the bus before enabling flash outputs.
Idle disables CE/OE/WE/address outputs and releases PORTD without pull-ups.

Sources: [SST datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/SST39SF010A-SST39SF020A-SST39SF040-Data-Sheet-DS20005022.pdf),
[ATmega32U4 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7766-8-bit-AVR-ATmega16U4-32U4_Datasheet.pdf).

## Validation

Native parser tests cover fragmented/consecutive commands, invalid frames,
reconnects/timeouts, the CRC golden vector, incremental bank CRC, bank/block
address bounds, complete write buffering, CRC rejection and binary timeout
isolation. Host CRC and HEX checks also pass, including rejection of application
images overlapping the bootloader, missing/corrupt manifests and data outside
the CRC-protected application extent. CI also runs the parser with address and
undefined-behavior sanitizers.
Earlier firmware hardware tests read BF B6 repeatedly and tested serial framing.
The 0.1.x protocol layout was validated on hardware on 2026-09-05:

- Combined bootloader installation verified; fuses 5E/98/FB, lock FF.
- USB-only firmware upload through COM33 verified all 6026 bytes, followed by
  successful application enumeration/READINFO on COM32.
- Deterministic SST test pattern programmed and all 262144 bytes read back and
  compared successfully. SHA256:
  `d09f79573a77cd8491450b7478947212e9cad885ceda6151e9ffd31cccf4d26f`.
- Hardware CRC rejection, address bounds, binary payload timeout/isolation and
  erase-precondition tests passed with the checked block unchanged.

The 0.2.0 source and generated images pass native, sanitizer, emulator and host
tests. Before release, repeat the USB-only update (including a deliberately
interrupted upload and boot-window recovery), full-chip blank check, hot-unplug
and application-watchdog tests on physical hardware. Also exercise the bundled
AVRDUDE 8.2 against the size-constrained 4032-byte bootloader.

The [16-bank P2000T display test](../roms/banktest/README.md) generates a 256 KiB image
with bank numbers 00–15. All bank entry/display and bank-switch/NMI CPU execution
tests passed. It is intended for physical P2000T verification of DIP bank
selection and all four 4 KiB ROM quarters.

The 16-bank display image was subsequently flashed and verified over all
262144 bytes, SHA256
`226a2515b29281fc3d420a0a6f67fe9844da39a94c040c5396ff9799a895a80c`.
It is the image currently installed in the SST39SF020, ready for the user's
physical P2000T test.
