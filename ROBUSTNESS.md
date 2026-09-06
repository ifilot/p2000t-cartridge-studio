# Robustness and remaining hardware dependencies

Version 0.2.0 closes the software-only reliability findings from the GUI and
firmware audit. Two findings require a project or PCB decision and cannot be
fully solved in software.

## USB VID/PID ownership

The current application and bootloader enumerate as `03EB:2044` and
`03EB:204A`. These values originate from the LUFA/Atmel example space; they are
useful for development and compatibility but are not a product-specific USB
identity. Another device can legally present the same values, so VID/PID-only
selection is not a sufficient authorization check.

Studio 0.2.0 mitigates that risk by requiring the application `READINFO`
identity, tracking the USB serial number across the application-to-bootloader
transition, requiring a unique device, and checking the bootloader identifier
and ATmega32U4 signature before AVRDUDE runs. The firmware and bootloader now
both expose the MCU-derived USB serial number.

The durable resolution is to obtain permission to use a project-owned or
allocated VID/PID pair. A migration then needs coordinated descriptor and GUI
changes, with a temporary allow-list for existing cartridges. It should also
include Windows enumeration/driver testing and release documentation. No new
IDs should be invented or shipped without an allocation.

## Cartridge-slot hardware interlock

The current board does not give the ATmega32U4 a reliable signal proving that
the cartridge is removed from a powered P2000T. Software therefore cannot
prevent a user from starting a write or erase while two systems can contend
for the flash bus. Studio 0.2.0 displays a fail-closed warning with Cancel as
the default before every destructive operation, but this remains a procedural
safeguard.

A future PCB revision should provide an MCU-readable, electrically safe
`HOST_PRESENT` or `PROGRAM_ENABLE` signal. Prefer a hardware gate that prevents
the programmer from driving flash address/data/control lines while the host is
present, even if firmware crashes. The MCU should also read the gate state and
the protocol should reject write/erase commands unless programming is enabled.
Validate the design for unpowered back-feeding, bus transceiver direction,
reset defaults and the USB-connected/P2000T-powered combinations before
relying on it.

## Release scope

Code signing is intentionally outside the 0.2.0 scope. Release checksums and
the embedded application CRC protect against accidental corruption, but they
do not provide publisher authentication.
