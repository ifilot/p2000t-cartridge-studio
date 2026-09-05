# Studio and firmware compatibility

P2000T Cartridge Studio and the cartridge firmware share one product version,
but their version numbers do not need to be equal to communicate. The Studio
uses the firmware identity returned by `READINFO` and applies the explicit
directional matrix below.

| Studio version | Supported firmware versions | Notes |
| --- | --- | --- |
| 0.1.0 | 0.1.0 | Initial release required an exact version match. |
| 0.1.1 | 0.1.0, 0.1.1 | The serial command set is unchanged. |

“Supported” means that the listed Studio release accepts and has been tested
against the listed firmware release. A future release must add its supported
combinations to the matrix in `src/src/firmwarecompatibility.cpp`; unknown
combinations fail closed with an error listing the supported firmware versions.

The preparation tool and AVR109 bootloader do not use the application’s serial
command protocol and are therefore outside this matrix.
