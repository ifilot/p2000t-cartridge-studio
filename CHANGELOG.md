# Changelog

All notable changes to P2000T Cartridge Studio and the matching programmable
cartridge firmware are documented here. Releases follow semantic versioning,
and the GUI, firmware, bootloader, protocol and preparation tool share one
version.

## [Unreleased]

## [0.1.1] - 2026-09-05

### Changed

- Migrated the desktop application and Windows build from Qt 5 to Qt 6 while
  retaining the existing layout and preferring the Windows Vista widget style.
- Bundled the complete matching MinGW runtime dependency chain so installed
  builds do not accidentally load incompatible DLLs from the Windows search
  path.
- Expanded the About dialog with supported-cartridge, compatibility, USB,
  build, project-link, and software-licensing details.
- Added an option to download, validate, install, and verify the latest
  application firmware from the stable GitHub release URL.
- Replaced the curated joystick image with the P2000T Teletekst Cartridge
  image from its stable GitHub release URL.

### Fixed

- Reduced the hex-view font size, restored true 16-byte rows, and added
  horizontal scrolling as a fallback for high-DPI displays.
- Replaced the exact GUI/firmware version check with an explicit compatibility
  matrix; Studio 0.1.1 supports firmware 0.1.0 and 0.1.1.

## [0.1.0] - 2026-09-05

### Added

- P2000T Cartridge Studio for reading, programming, erasing and verifying an
  SST39SF020 as a complete 256 KiB image or as sixteen individual 16 KiB banks.
- A 4×4 dropdown bank selector covering banks 0 through 15.
- ATmega32U4 application firmware and LUFA-based AVR109 bootloader using the
  synchronized V003-compatible product protocol.
- Faster block-oriented ROM reads over USB serial.
- Application-firmware installation through the protected USB bootloader.
- Windows cartridge preparation tool with repeated ISP probes, device and fuse
  validation, combined-image programming and post-flash verification.
- Emulator-tested bank-test ROM generator.
- MSYS2/MinGW and WSL build documentation.
- GitHub Actions builds, tests, portable artifacts and tag-triggered releases.
- An Inno Setup Windows installer with upgrade, shortcut and uninstall support.

### Changed

- Consolidated the desktop application, firmware, test ROMs and preparation
  utility into one software repository.
- Unified all software and protocol components under the root `VERSION` file.
- Standardized public release assets on stable lowercase filenames suitable
  for GitHub `/releases/latest/download/` links.
- Licensed project-owned software under GNU GPL version 3.
- Replaced the bundled Consolas font with the operating system's monospace
  font and removed obsolete, unlicensed ROM assets.

[Unreleased]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/ifilot/p2000t-cartridge-studio/releases/tag/v0.1.0
