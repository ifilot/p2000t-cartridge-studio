# Studio and firmware compatibility

P2000T Cartridge Studio and the cartridge firmware share one product version,
but their version numbers do not need to be equal to communicate. Studio 0.2.2
accepts the legacy version-bearing `READINFO` response and the 0.2 protocol
identity plus `READVERS` metadata, then applies the explicit directional matrix
below.

| Studio version | Supported firmware versions | Notes |
| --- | --- | --- |
| 0.1.0 | 0.1.0 | Initial release required an exact version match. |
| 0.1.1 | 0.1.0, 0.1.1 | The serial command set is unchanged. |
| 0.1.2 | 0.1.0, 0.1.1, 0.1.2 | The serial command set is unchanged. |
| 0.2.0 | 0.1.0, 0.1.1, 0.1.2, 0.2.0 | The serial command set is unchanged; 0.2.0 adds host and firmware recovery safeguards. |
| 0.2.1 | 0.1.0, 0.1.1, 0.1.2, 0.2.0, 0.2.1 | The serial command set is unchanged; 0.2.1 adds bundled P2000M ROM images. |
| 0.2.2 | 0.1.0, 0.1.1, 0.1.2, 0.2.0, 0.2.1, 0.2.2 | The serial command set is unchanged; 0.2.2 fixes disconnection-warning layout. |

“Supported” means that the listed Studio release accepts and has been tested
against the listed firmware release. A future release must add its supported
combinations to the matrix in `src/src/firmwarecompatibility.cpp`; unknown
combinations fail closed with an error listing the supported firmware versions.

The preparation tool and AVR109 bootloader do not use the application’s serial
command protocol and are therefore outside this matrix. Studio 0.2.2 validates
the bootloader software identifier and MCU signature before invoking AVRDUDE.
