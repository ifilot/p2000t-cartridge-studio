# P2000T Cartridge Studio

[![build](https://github.com/ifilot/p2000t-cartridge-studio/actions/workflows/build.yml/badge.svg)](https://github.com/ifilot/p2000t-cartridge-studio/actions/workflows/build.yml)
[![version](https://img.shields.io/github/v/tag/ifilot/p2000t-cartridge-studio?sort=semver&label=version)](https://github.com/ifilot/p2000t-cartridge-studio/tags)
[![license](https://img.shields.io/github/license/ifilot/p2000t-cartridge-studio)](LICENSE)

Software for the ATmega32U4-based programmable P2000T cartridge with an
SST39SF020 flash ROM. The desktop application can install either a local
firmware image or the latest release downloaded from GitHub.

## Downloads

- [P2000T Cartridge Studio installer for Windows](https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/p2000t-cartridge-studio-windows-x64-setup.exe)
- [P2000T Cartridge Studio portable ZIP](https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/p2000t-cartridge-studio-windows-x64.zip)
- [Programmable Cartridge Preparation Tool for Windows](https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/p2000t-programmable-cartridge-preparation-tool-windows-x64.zip)
- [Application firmware](https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/p2000t-programmable-cartridge-firmware.hex)
- [Complete factory image](https://github.com/ifilot/p2000t-cartridge-studio/releases/latest/download/p2000t-programmable-cartridge-factory-image.hex)

## Repository layout

- `src/` — Qt 6 Windows desktop application for reading and programming complete
  flash images and individual 16 KiB banks, plus USB bootloader updates.
- `firmware/` — ATmega32U4 application firmware, LUFA AVR109 bootloader,
  serial protocol, native tests and AVR build tooling.
- `roms/banktest/` — 16-bank P2000T test ROM generator and emulator-backed
  tests.
- `tools/bootloader-installer/` — Windows USBasp preparation utility for the
  initial combined application-and-bootloader installation.
- `COMPATIBILITY.md` — directional Studio-to-firmware compatibility matrix.
- `ROBUSTNESS.md` — implemented safeguards and the remaining USB-ID and
  hardware-interlock decisions.

PCB, schematic, fabrication and enclosure sources remain in the separate
hardware repository.

## Build the firmware under WSL

Install `gcc-avr`, `avr-libc`, `binutils-avr`, `make`, `git`, Python 3 and a
native C compiler in WSL. From the repository root:

```sh
cd firmware
sh fetch-lufa.sh
make -j4 combined test
cd ..
```

Outputs are generated under `firmware/build/`. In particular,
`firmware/build/combined.hex` contains the application and bootloader for the
initial ISP installation. Firmware is compiled under WSL and flashed from
Windows; do not try to access a Windows USB programmer directly through this
build step.

## Build the Windows preparation utility

Cross-compile from WSL with MinGW-w64 and Ninja:

```sh
cmake -S tools/bootloader-installer \
  -B tools/bootloader-installer/build-cross \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build tools/bootloader-installer/build-cross \
  --target package_bundle
```

The portable Windows package is generated at
`tools/bootloader-installer/dist/p2000t-programmable-cartridge-preparation-tool-windows-x64.zip`.
Run the preparation tool on Windows with the cartridge connected through its
ISP header.

## Build the Windows application and installer

Build the Qt application in an MSYS2 MinGW64 shell as documented in
[`src/README.md`](src/README.md). After building under `dist/`, create the Inno
Setup installer with:

```sh
sh src/packaging/package-local.sh
```

The installer is generated at
`dist/p2000t-cartridge-studio-windows-x64-setup.exe`. Inno Setup 6 must be
installed in its standard Windows location.

## Continuous integration and releases

The [build workflow](.github/workflows/build.yml) compiles and tests the AVR
application, AVR109 bootloader, bank-test ROMs, Windows preparation tool and
Windows Qt 6 application on pushes and pull requests. Workflow artifacts use the
same stable names as release assets. Pushing a matching `vX.Y.Z` tag publishes
those tested outputs as a GitHub release and adds `sha256sums.txt`.

GitHub's `/releases/latest/download/` URLs address release assets rather than
per-run workflow artifacts. The tag job therefore republishes the tested
artifacts under the stable filenames listed above.

## Licence

The project-owned software is distributed under the
[GNU General Public License version 3](LICENSE). Third-party components retain
their original compatible licences and copyright notices; notably, AVRDUDE is
packaged as a separate GPLv2 program with its licence and corresponding source.
See [CHANGELOG.md](CHANGELOG.md) for release history.
