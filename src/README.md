# P2000T Cartridge Studio

Qt 6 desktop application for the ATmega32U4-based P2000T multi-cartridge. It
supports the SST39SF020 only and requires a compatible firmware release.

The GUI accepts firmware versions according to the repository's explicit
[`COMPATIBILITY.md`](../COMPATIBILITY.md) matrix; product versions do not need
to be identical when the serial command set is compatible.

P2000T ROM images are exactly 16 KiB and can be read, erased or written to any
of the sixteen cartridge banks. Complete 256 KiB flash images can separately
be read to a file, erased, or programmed and verified. Application-only Intel
HEX firmware can be installed through the protected USB AVR109 bootloader.
Firmware images are checksum-validated and rejected if any data reaches the
bootloader region beginning at address `0x7000`. The Windows package includes
the official AVRDUDE 8.2 executable and configuration under `tools/avrdude`.
The firmware menu can either select a local HEX file or download the latest
release directly from the project's stable GitHub `releases/latest` URL before
running the same validation, programming, and verification flow.

## MSYS2 / MinGW build

From the repository root in an MSYS2 MinGW64 shell:

```sh
/mingw64/bin/cmake.exe -S src -B dist -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
/mingw64/bin/cmake.exe --build dist
/mingw64/bin/ctest.exe --test-dir dist --output-on-failure
/mingw64/bin/windeployqt6.exe --release --compiler-runtime --no-opengl-sw --no-translations --include-plugins qmodernwindowsstyle --dir dist dist/p2000t-cartridge-studio.exe
bash src/packaging/deploy-mingw-dependencies.sh dist
```

With Inno Setup 6 installed in its standard Windows location, build the
installer from the repository root:

```sh
sh src/packaging/package-local.sh
```

This produces
`dist/p2000t-cartridge-studio-windows-x64-setup.exe`.
