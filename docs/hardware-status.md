# Hardware status log (owner's FNIRSI 1013D)

Living document. Update after every hardware step (CLAUDE.md). Procedures are in `docs/hardware-prep.md`.
Hash entries use sha256 of files in `backups/` (gitignored).

## Unit identification

| Item | Value | Basis |
|---|---|---|
| Model | FNIRSI 1013D | owner |
| MCU | Allwinner F1C100s | board |
| FPGA | Anlogic AL3A10, unmarked LQFP144 | inferred: external 25Q80 beside it; not yet confirmed by a bitstream header or JTAG read |
| FPGA JTAG IDCODE | 0x18006C31 expected | taken from other AL3 units (CLAUDE.md); will appear at offset 40 of the 25Q80 dump |
| FPGA config flash | Winbond 25Q80DVNIG, 1 MB | owner |
| FPGA flash header | unpopulated 2x3, owner is soldering pins | owner |
| MCU flash | W25Q32JVSIQ, 4 MB, near the F1C100s | owner |
| Charger | TP4056 | owner |
| Parts on back side | none | CLAUDE.md |
| Stock FPGA version word | 0x1432 expected | not read on this unit |
| Firmware / bootloader now | unknown | owner to determine (hardware-prep.md section 4) |
| Programmers available | generic ARM JTAG debuggers (J-Link / ST-Link / CMSIS-DAP class); CH341A can be bought; WSL Ubuntu with flashrom | owner |
| Programmer chosen for SPI | not decided | recommend a 3.3 V FT232H, or a CH341A with verified 3.3 V signals |

## Checklist

- [ ] Header pins soldered
- [ ] Pinout verified by continuity (hardware-prep.md 1.3 table filled and photographed)
- [ ] Programmer signal levels measured (3.3 V on CLK, MOSI, CS)
- [ ] 25Q80 backed up twice, dumps identical, sha256 recorded
- [ ] 25Q80 dump compared with the stock reference (sha256 `90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253`), result noted
- [ ] W25Q32 backed up, sha256 recorded, compared with reference dumps
- [ ] SD card imaged (full image plus sectors 0-2047 and 708, 709, 710), sha256 recorded
- [ ] Backups copied to a second drive
- [ ] Current firmware version noted
- [ ] Current bootloader version noted (or "none, stock")
- [ ] FPGA version word noted
- [ ] Lead's go received for Tier 1 (bootloader >= 0.8, scope 1.00x source of binaries decided)
- [ ] Tier 1 step 5.3.3 done: open firmware booted with stock FPGA
- [ ] AL3 bitstream (0x1532) written and verified by read-back
- [ ] FEL path tested (usbipd attach, `sunxi-fel version`)

## Backup register

| File | Size | sha256 | Date | Notes |
|---|---|---|---|---|
| | | | | |

## Log

Newest entries at the bottom. Format: date, who, what, result.

- 2026-10-18: Guide `docs/hardware-prep.md` and this log created. No hardware action taken. Reference hashes (computed on the `D:\src` copies): stock AL3 flash image `90f65f08...0d253`; Atlan AL3 `FPGA.bin` `0d4c829c...0fd2`; padded AL3 image `8a9d3559...2177`; on-scope backup tool `64bc81e6...1f6`.
