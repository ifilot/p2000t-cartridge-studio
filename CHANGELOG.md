# Changelog

All notable changes to P2000T Cartridge Studio and the matching programmable
cartridge firmware are documented here. Releases follow semantic versioning,
and the GUI, firmware, bootloader, protocol and preparation tool share one
version.

## [Unreleased]

## [0.2.3] - 2026-09-09

### Changed

- Show the four-position DIP-switch pattern alongside every bank selector
  entry and the currently selected bank, with aligned equipment-style bank
  labels, a content-sized selected control and a tightly fitted three-column
  popup.

### Fixed

- Disable firmware installation when neither an application-mode cartridge
  nor a USB bootloader target is connected.

## [0.2.2] - 2026-09-06

### Fixed

- Wrap the cartridge-disconnection warning so it remains readable in the
  connection panel.

## [0.2.1] - 2026-09-06

### Added

- Add bundled Philips Disk BASIC 24K, MCPM, and UCSD Pascal ROM images for
  using the same SLOT1 cartridge with the Philips P2000M.

### Changed

- Increase the initial application window height by 50 pixels.

## [0.2.0] - 2026-09-05

### Added

- Add a CRC32-protected application manifest that is generated during the
  firmware build and checked by both Studio and the bootloader.
- Add cancellation at safe bank/block boundaries for read, erase, program and
  verification workers.
- Add an application watchdog and bootloader transfer timeouts.
- Add bootloader identity, ATmega32U4 signature and USB serial matching checks
  before firmware installation.
- Add a fixed protocol identity plus `READVERS` metadata so product versions
  are no longer restricted to three single digits.

### Changed

- Run erase operations in worker threads and verify every byte after a
  complete-chip erase.
- Keep the 4 KiB AVR109 bootloader focused on application-flash operations;
  EEPROM and fuse/lock inspection remain ISP responsibilities.
- Bound and synchronize the diagnostic log shared by GUI and worker threads.
- Limit downloads to HTTPS, five redirects, 30 seconds and the expected size
  class while consuming responses incrementally.
- Require an explicit physical-safety acknowledgement before destructive
  operations because the current PCB has no MCU-readable slot interlock.
- Save ROM images and diagnostic logs atomically.

### Fixed

- Reject incomplete or overlapping Intel HEX images and never launch an
  application whose manifest or CRC is invalid.
- Detect completed serial writes and invalidate stale GUI connection state
  after communication failures or physical disconnects.
- Escape downloaded and local filenames before rendering them as rich text.

## [0.1.2] - 2026-09-05

### Changed

- Display compact MD5 ROM checksums in the data header so the complete value
  fits beside the filename and image size.
- Replace the generic cartridge application icon with artwork based on the
  characteristic shape of an original P2000T cartridge enclosure.
- Extend the compatibility matrix so Studio 0.1.2 accepts firmware 0.1.0,
  0.1.1 and 0.1.2.

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

[Unreleased]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/ifilot/p2000t-cartridge-studio/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/ifilot/p2000t-cartridge-studio/releases/tag/v0.1.0
