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
| Adapters on hand (per lead) | FT232RL (no MPSSE, unusable), Sipeed USB-JTAG/TTL RISC-V debugger (genuine FT2232D, verified by the lead: D2XX type 4, serials A/B, COM8/COM9, MPSSE sync OK on channel A), J-Link V9 clone (not recommended) | lead; hardware-prep.md 2.4 |
| Programmer chosen for SPI | primary: `firmware/tools/spiflash_d2xx.py` on the Sipeed adapter (Windows, D2XX). Alternatives: pyftdi script `spiflash_ftdi.py` (WSL), pico-serprog, CH341A with 3.3 V mod | hardware-prep.md 2.4 |
| Sipeed adapter type | genuine FT2232D | lead's D2XX check |

## Checklist

- [x] Header pins soldered
- [x] Pinout verified by continuity (hardware-prep.md 1.3 table filled and photographed)
- [x] Programmer chosen and its pin mapping to the header written down
- [ ] Programmer signal levels measured (3.3 V on CLK, MOSI, CS)
- [x] `spiflash_d2xx.py probe` (or flashrom probe) returns a valid JEDEC ID
- [x] 25Q80 backed up twice, dumps identical, sha256 recorded
- [x] 25Q80 dump compared with the stock reference (sha256 `90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253`), result noted
- [ ] W25Q32 backed up, sha256 recorded, compared with reference dumps
- [ ] SD card imaged (full image plus sectors 0-2047 and 708, 709, 710), sha256 recorded
- [ ] Backups copied to a second drive
- [x] Current firmware version noted
- [x] Current bootloader version noted (or "none, stock")
- [x] FPGA version word noted
- [x] Lead's go received for Tier 1
- [x] Tier 1 SD card prepared with `make_sd_image.py` (bootloader v0.8, updater, scope; boot mode 4), written to a spare card
- [x] Tier 1 step 5.3.3 done: open firmware booted with stock FPGA
- [x] AL3 bitstream (0x1532) written and verified by read-back
- [x] FEL path tested (usbipd attach, `sunxi-fel version`)

## Backup register

| File | Size | sha256 | Date | Notes |
|---|---|---|---|---|
| | | | | |

## Log

Newest entries at the bottom. Format: date, who, what, result.

- 2026-10-18: Guide `docs/hardware-prep.md` and this log created. No hardware action taken. Reference hashes (computed on the `D:\src` copies): stock AL3 flash image `90f65f08...0d253`; Atlan AL3 `FPGA.bin` `0d4c829c...0fd2`; padded AL3 image `8a9d3559...2177`; on-scope backup tool `64bc81e6...1f6`.
- 2026-10-18: Guide updated after the firmware bootstrap. Bootloader v0.8, updater v0.02v and scope v1.00o5 exist in Atlan's v1.00o5 package (721,992 bytes), which `make package` in `firmware/scope` reproduces. SD cards are to be written with `firmware/tools/make_sd_image.py`, not with Atlan's `dd` line, which zeroes sectors 708 to 710. Added programmer options and `firmware/tools/spiflash_ftdi.py` (tested only against a simulated flash). No hardware action taken.
- 2026-10-18: Owner's Sipeed adapter confirmed as a genuine FT2232D. `spiflash_d2xx.py` (Windows, D2XX) is now the primary SPI route in hardware-prep.md 2.4; `spiflash_ftdi.py` (pyftdi) is the WSL alternative and is untested on hardware. No hardware action taken on the flash.

### 2026-10-18 (lead, with owner at the bench)
- Header J2 (FPGA flash) soldered. Measured pinout by continuity (owner numbering, pin 1 top-left):
  1 -> /CS (flash 1), 2 -> DO (flash 2, 34 ohm series), 3 -> CLK (flash 6), 4 -> DI (flash 5), 5 -> VCC (flash 3,7,8), 6 -> GND (flash 4).
  Differs from the schematic guess (candidate A) in physical order; electrically complete.
- Programmer: Sipeed FT2232D channel A via D2XX. Wiring: TMS->1 /CS, TDO->2 DO, TCK->3 CLK, TDI->4 DI, GND->6; header 5 and debugger 3V3 unconnected. Scope on battery, no USB.
- `spiflash_d2xx.py probe`: JEDEC ef 40 14, W25Q80DV, status 0x00 (no block protect). SPI clock 1 MHz, ~117 KiB/s reads.
- Backup: `backups/2026-10-18_fpga-25q80-stock.bin`, two reads identical, sha256 90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253.
  Payload (282,866 bytes, header cc55aa33 f0000006 18006c31 0bb7c200) byte-identical to Atlan's zb25vq80 dump and pcprogrammer's W25Q80_FPGA.bin. Unit is confirmed stock AL3A10 bitstream (0x1432).
- Spare SD card: 480 MB USB mass-storage card, originally FAT32 from sector 32 (would collide with firmware sectors); repartitioned with diskpart, FAT32 from 4 MiB (sector 8192).
- Tier-1 SD card written (WSL /dev/sdg via usbipd busid 8-4): bootloader v0.8 @16, updater v0.02v @80, scope v1.00o5 (GCC 10 build, byte-identical to Atlan's reference) @750,
  standard display config @710 with boot mode 4 (menu, scope preselected); sectors 708/709 blank (calibrate on first boot). Image: `backups/sdcard/card-1.00o5-stockfpga.img`
  sha256 83a1ff773c766f1c04586996b6a22dd601828162e9806f573a912ab62442f012. Read-back identical. FAT32 partition from sector 8192, label FNIRSI.
  Not yet booted on the scope.
- First boot of the tier-1 card OK: BOOT fw v0.8 banner, boot menu, scope v1.00o5 runs with stock FPGA; diagnostics shows FPGA 0x1432.
  Standard display config correct (no shift, touch OK). Base calibration done; input-divider calibration done against bench supply + DMM
  (first attempt failed with probes on 10x: the routine needs 1x). Updater's USB mass-storage mode gave "Cannot recognize device" on Windows (unresolved, cable suspected).
- FEL verified: boot menu -> FEL, usbipd bind --hardware-id 1f3a:efe8, attach busid 9-3 to WSL, `sunxi-fel version` -> AWUSBFEX soc=00001663(F1C100s) ver=0001.
- FEL RAM load proven: `sunxi-fel -p write 0x7FFFFFE0 scope.bin exe 0x80000000` (346 kB in 1.7 s) started the scope UI from RAM.
- FPGA flash written with Atlan's AL3 bitstream (0x1532): `backups/al3_atlan_0x1532_padded.bin` sha256 8a9d3559566fbe9653ad52612818d336ce7f905498f422f228bb165335c42177;
  pre-write backup `backups/20261018-131536-pre-write.bin` (= stock, sha256 90f65f08...); 68 sectors erased, 1080 pages programmed, read-back verified. Board powered from battery during write.
  Power-cycled: diagnostics screen shows FPGA 0x1532. Tier 1 complete.
- Protocol firmware bring-up (image build-gcc10-pc/fnirsi_1013d.bin sha256 da53cc74..., GCC 10, PC_PROTOCOL=1, loaded over FEL):
  step 2 enumeration OK (Windows COM10, USB\VID_1209&PID_0001A3D-0001, usbser, no driver install);
  step 3 text probe OK: `#VER` -> `#OK,FNIRSI-1013D-AI,fw=0.1.0,proto=1.0,fpga=0x1532`, `#FOO` -> `#ERR,UNKNOWN`.
  step 4 identify (LOCAL) OK: GET_INFO variant 2, ring 8192, max_total 16382, caps 0x1803, read_lag 0 (unverified), fw 0.1.0;
  PING x500 random 0..492 B: 0 mismatches, 52.8 ms avg round trip (slow: main-loop poll cadence suspected);
  GET_STATUS/BATTERY/CHANNEL/CAL/TIMEBASE/TRIGGER/GEN all answer; CH1 adc comp -2/+2, CH2 0/0; input_calibration values from the bench calibration read back.
