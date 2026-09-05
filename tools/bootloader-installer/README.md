# P2000T Programmable Cartridge Preparation Tool

Windows command-line utility for preparing the P2000T Programmable Cartridge by
installing its combined ATmega32U4 application and bootloader image through
USBasp. It checks the ISP connection several times before allowing a write and
verifies the result afterwards.
Its version is read from the repository's root `VERSION` file, shared with the
desktop application, firmware and protocol.

## Build with MSYS2/MinGW

From the root of this repository in an MSYS2 UCRT64 terminal with CMake, Ninja
and GCC installed:

```sh
cmake -S tools/bootloader-installer \
  -B tools/bootloader-installer/build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build tools/bootloader-installer/build
```

The resulting program is
`tools/bootloader-installer/build/p2000t-programmable-cartridge-preparation-tool.exe`.

To create a ready-to-use ZIP containing the preparation tool, factory image,
AVRDUDE and its corresponding GPL source and licence:

```sh
cmake --build tools/bootloader-installer/build --target package_bundle
```

The package is written to
`tools/bootloader-installer/dist/p2000t-programmable-cartridge-preparation-tool-windows-x64.zip`.
The package target reads the combined firmware image from
`firmware/build/combined.hex`, so build the firmware under WSL before packaging
from Windows/MSYS2. The product version is included in `version.txt`.

## Use

Keep the cartridge out of the P2000T and do not connect its USB port while the
USBasp powers the ISP header contacts.

```text
p2000t-programmable-cartridge-preparation-tool.exe
p2000t-programmable-cartridge-preparation-tool.exe check
p2000t-programmable-cartridge-preparation-tool.exe flash D:\path\to\factory-image.hex
```

Launching without arguments displays an interactive menu. The menu returns
after every check or flash operation so multiple cartridges can be prepared in
one session. In the packaged version, preparing a cartridge requires no paths
or command-line options.

`flash` always runs the connection check first. By default, the utility performs
five independent reads and requires an ATmega32U4 signature plus identical fuse
and lock values each time. It then flashes and verifies the image, sets the high
fuse to `98` and extended fuse to `FB`, and checks the device three more times.
It never writes the low fuse or lock byte.

The packaged version includes AVRDUDE beside the preparation tool. A standalone
build also searches `PATH` and an Arduino installation under
`%LOCALAPPDATA%\Arduino15`. Override discovery when needed:

```text
p2000t-programmable-cartridge-preparation-tool.exe --avrdude C:\path\avrdude.exe --config C:\path\avrdude.conf check
```

Run `p2000t-programmable-cartridge-preparation-tool.exe --help` for all options.
