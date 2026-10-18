# Reference binaries (not committed)

Prebuilt files from the Atlan4 repository (`D:\src\Fnirsi1013D`, extracted under
`research/atlan4-extracted/_dso1xxx/v1.00o5/fnirsi_1013d_scope/dist/Debug/GNU_ARM-Linux/`).
They are GPL-licensed and authored by Atlan4 and pecostm32. They are kept on disk only, for
comparison, and are excluded from git by the `firmware/**/*.bin` and `*.elf` rules in
`.gitignore`. Restore them by copying from that folder.

| file | upstream name | size | content |
|---|---|---|---|
| `fnirsi_1013d_v1.00o5_atlan.bin` | `fnirsi_1013d_scope.bin` | 346,112 | scope v1.00o5, eGON.EXE, for SD sector 750 or FEL |
| `fnirsi_1013d_v1.00o5_atlan.elf` | `fnirsi_1013d_scope.elf` | 1,343,700 | its ELF. GCC 10.3.1 `15:10.3-2021.07-4` |
| `fnirsi_1013d_v1.00o5_atlan_sdpackage.bin` | `fnirsi_1013d.bin` | 721,992 | SD package from sector 16: bootloader "BOOT fw v0.8" at +0, updater v0.02v at +0x8000, scope at +0x5BC00, and the 72-byte flashfilepacker trailer. Sectors 708-710 inside it are zeros. |

`firmware/scope` reproduces the first and last files byte for byte with `make check-reference`
and `make package` in the GCC 10 build. See `../README.md`.
