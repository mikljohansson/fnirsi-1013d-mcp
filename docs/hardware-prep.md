# Hardware preparation and backup guide (FNIRSI 1013D, AL3A10 variant)

Status: DRAFT for owner review. Nothing in this guide has been executed on the owner's unit yet.
Date written: 2026-10-18. Log every executed step in `docs/hardware-status.md`.

Citation style: `(file.md §x)` points to `research/notes/`. `[checked locally]` means the author ran a
command on the reference trees under `D:\src` while writing; it is not from a note. `[general knowledge]` is
standard tool behaviour that no note covers; verify it. Where sources conflict or are silent, the text says so.

## 0. Ground rules

1. Nothing is written to any flash chip, FPGA or SD sector until section 3 is complete and the checksums are in `docs/hardware-status.md` (CLAUDE.md "Rules").
2. Sections 1 to 4 are read-only. Section 5 changes hardware state. FEL (section 6) is RAM-only unless you use `spiflash-write`.
3. Three separate memories exist and must not be confused:

| Chip | Holds | Sits on | Backup method |
|---|---|---|---|
| Winbond 25Q80DVNIG (1 MB) | FPGA bitstream, loaded by the FPGA at power-up | FPGA config bus, 2x3 header J2 `FPGA_FLASH` | external SPI programmer (3a) |
| W25Q32JVSIQ (4 MB) | stock bootloader, stock firmware, stock calibration at 0x1FD000 | MCU SPI0 | on-scope backup tool or FEL (3b) |
| SD card | open firmware, calibration (sector 708), settings (709), display/touch config and boot byte (710) | MCU SDC0 | `dd` or Win32DiskImager (3c) |

(hack-repo-hardware-fpga.md §2.1, §2.2; hack-repo-software.md §1.1; repo-atlan4.md §4.1)

4. The scope CPU cannot reflash the FPGA config flash by itself: the config lines do not reach the F1C100s (repo-donwulff.md "Stock vs Atlan4 FPGA"). Atlan's on-scope updater route needs the FPGA flash wired to the MCU flash pads, with the MCU flash desoldered (thread-pages-121-155.md §3.3). This guide does not use that route.

## 1. Identify and verify the 2x3 header pinout

### 1.1 What the sources say

There is no single authoritative pinout. Candidates, in order of trust:

**Candidate A: pecostm32's reconstructed schematic, header J2 `FPGA_FLASH`.** The author derived these nets by tracing the wires in `D:\src\FNIRSI-1013D-1014D-Hack\Schematics\1013D\EASYEDA\SCH_Scope 1013D_2023-01-07.json`, sheet "Scope_Data_Acquisition" [checked locally]. The note files only say "header J2 `FPGA_FLASH`" without pin numbers (hack-repo-hardware-fpga.md §2.2). The schematic is a reverse-engineered reconstruction (hack-repo-hardware-fpga.md §2), so treat it as a hypothesis.

| Header pin | Net | Connects to W25Q80 pin | Notes from the schematic |
|---|---|---|---|
| 1 | `FPGA_Conf_nCE` | 1 /CS | |
| 2 | GND | 4 GND | |
| 3 | `FPGA_Conf_Data` | 2 DO | through R1, 33 ohm, in series |
| 4 | flash VCC node | 8 VCC, also 3 /WP and 7 /HOLD | fed from the 3V3 rail through D1 (1N5819 Schottky), so about 0.3 V below 3V3 |
| 5 | `FPGA_Conf_CLK` | 6 CLK | |
| 6 | `FPGA_Conf_ASDO` | 5 DI | |

In the schematic symbol, pins 1, 3, 5 form one row and pins 2, 4, 6 the other. Which end is pin 1 on the real board is not stated anywhere. Find it by continuity (1.3).

**Candidate B: the same schematic's J1, the MCU flash header (W25Q32).** Same pattern: 1 CS, 2 GND, 3 MISO, 4 3V3, 5 CLK, 6 MOSI (hack-repo-hardware-fpga.md §2.1). It suggests both 2x3 headers follow one convention. It is not the FPGA flash header; do not mix them up.

**Candidate C: "ded", thread page 142, post 3542.** ded reports a Winbond 25Q80DV on their board "with connector pinout differing from the forum's" (thread-pages-121-155.md §2.1). The notes do not record ded's pinout. The owner's chip is the same Winbond 25Q80DV part, so ded's board may be the same revision. Unresolved: someone should read post 3542 (EEVblog thread, page 142) before soldering.

**Also in the thread, not the FPGA flash header:**
- A separate 4-pin header on AL3 boards. It is the special-IC/EEPROM link (J3 `FPGA_EEPROM`: 1 GND, 2 VCC, 3 SCL, 4 SDA, derived from the same schematic [checked locally]). pcprogrammer says the board label "JTAG" is wrong and it is I2C to an anti-clone IC (thread-pages-121-155.md §2.1). Do not connect a flash programmer to it.
- "Another layout without recessed BNCs has SPI pins shuffled" (Atlan, page 149, post 3714; thread-pages-121-155.md §2.1).
- Older boards have a 10-pin JTAG header routed to unpopulated pads; newer ones show pads TDI, TCK, TMS, TDO, GND, 2V5 (thread-pages-031-060.md §FPGA; hack-repo-hardware-fpga.md §2.2). Anlogic JTAG is 2.5 V level there. Not needed for the AL3 SPI route.

### 1.2 Before touching the board

1. Power off, disconnect battery and USB.
2. Photograph the area around the 25Q80 and the pads. Mark which end of the footprint carries a pin-1 dot or square pad, if any.
3. Read the chip marking and note it in `docs/hardware-status.md` (expected Winbond 25Q80DVNIG, 1 MB). Other AL3 units carry ZB25VQ80, 25VQ16 or 25VQ32, which can need different flashrom handling (thread-pages-121-155.md §2.1, §8).

### 1.3 Continuity procedure (power off, battery out)

Solder the pins first (owner task). Then, with a multimeter, find each header pin's partner on the SOIC-8, using the standard pinout: 1 /CS, 2 DO, 3 /WP, 4 GND, 5 DI, 6 CLK, 7 /HOLD, 8 VCC.

1. Find SOIC-8 pin 1 (dot or bevel). Pins run counter-clockwise from there; pin 4 is at the other end of the same short side.
2. Continuity mode: put one probe on a SOIC-8 pin, move the other across all six header pins. Record the match.
3. Expected deviations if candidate A is right:
   - DO (pin 2) reads about 33 ohm, not 0, because of series resistor R1.
   - VCC (pin 8) reaches its header pin on the flash side of a Schottky diode. In diode mode expect a drop of roughly 0.2 to 0.3 V from the 3V3 rail, not a beep. Flash pins 3 and 7 tie to the same node.
   - GND beeps to any ground (shield, battery connector).
4. If a flash pin has no header partner, the header may connect through an unpopulated footprint. Stop and ask. Do not guess.
5. Check that no two header pins beep to each other and that header VCC to header GND is not near zero.
6. Fill the table below and photograph the board with the pin numbers marked.

| Header pin (your numbering; state where pin 1 is) | Beeps to SOIC-8 pin | Signal | Expected by candidate A | Match? |
|---|---|---|---|---|
| 1 | | | /CS (flash pin 1) | |
| 2 | | | GND (flash pin 4) | |
| 3 | | | DO (flash pin 2), about 33 ohm | |
| 4 | | | VCC (flash pin 8, also 3 and 7), diode drop | |
| 5 | | | CLK (flash pin 6) | |
| 6 | | | DI (flash pin 5) | |

Record the result in `docs/hardware-status.md` ("pinout verified"). Do not connect anything until every row is filled.

## 2. Programmers and in-circuit considerations

### 2.1 What can program the AL3 bitstream

On the AL3 the bitstream lives in the external SPI flash, so any 3.3 V SPI programmer works (repo-atlan4.md §4.1 Risks; repo-donwulff.md "Flashing routes"). Atlan's AL3 readme says "CH341 with 3.3V converter" (`FPGA AL3/readme`).

| Programmer | Fit | flashrom name [general knowledge] | Notes |
|---|---|---|---|
| FT232H / FT2232H breakout | Recommended purchase: 3.3 V logic natively | `ft2232_spi` | Not in the thread notes; chosen to avoid the CH341A voltage problem. |
| Bus Pirate | Bench-proven by Donwulff on the same flash family | `buspirate_spi` | Read worked with a recent flashrom, 3V3 pin unconnected, board powered (repo-donwulff.md "Flashing routes"). |
| Raspberry Pi SPI | Works in principle, 3.3 V native | `linux_spi` | Not covered by the notes. |
| CH341A | Only with a genuine 3.3 V signal mod or level converter | `ch341a_spi` | See 2.2. The thread's usual tool, so most community experience exists. |
| Generic ARM debuggers (J-Link, ST-Link, CMSIS-DAP) | Cannot program the Anlogic FPGA. Not usable as flashrom SPI programmers | none | Donwulff: "J-Link failed (Winbond-only SPI support)" (repo-donwulff.md "Flashing routes"). Anlogic JTAG needs Tang Dynasty (TD) plus a Blue Pill running anlogic-usbjtag (thread-pages-121-155.md §3.3). That is only for EF2 units or live JTAG loading, not for this unit. |

### 2.2 The CH341A 5 V problem

Some CH341A boards switch the VCC pin to 3.3 V but leave the SPI data lines at 5 V. pcprogrammer killed a flash that way (thread-pages-091-120.md §2 "RTC / SD / USB / misc"). He also advises reading in circuit with the scope powering the flash, not the programmer, because the CH341A may drive 5 V (thread-pages-121-155.md §3.3, post 3513).

The FPGA config pins share this bus, so 5 V on the lines endangers the FPGA as well. Before connecting a CH341A:
- Measure the idle level of CLK, MOSI and CS on the programmer's header with a multimeter, programmer plugged in, nothing attached. A level near 5 V means it needs a level shifter. [general knowledge]
- Fix: buffer CLK, MOSI and CS through a buffer powered from 3.3 V, such as the 74LVC125 that the Anlogic JTAG programmer in the thread uses (thread-pages-121-155.md §3.3). Flash MISO at 3.3 V is safe to feed a CH341A input. [general knowledge]
- Simpler: use a 3.3 V FT232H.

### 2.3 Powering, battery and the shared bus

What the sources establish:
- Bench-proven read: Bus Pirate and flashrom, programmer 3V3 pin left unconnected, scope board powered (repo-donwulff.md "Flashing routes").
- pcprogrammer: let the scope power the flash, not the programmer (thread-pages-121-155.md §3.3).
- Per the schematic, flash VCC is fed through a diode, so a programmer feeding header pin 4 would not back-feed the 3V3 rail. It would also leave the FPGA unpowered while its pins are tied to the flash bus. No source tests this. Do not use it.
- Bricking reports: mishail71 recovered with a CH341A with 3.3 V; engineer.r152 recovered with a CH341; the checks listed are protection bits, chip orientation and regulator failure (thread-pages-121-155.md §3.3). Atlan reports two failed uploads fixed by unplugging the flash and re-uploading (thread-pages-091-120.md §2).
- The notes do not say how the FPGA and its configuration bus behave after configuration while the board is on. Whether a read or write with the board powered can disturb a running FPGA is unresolved.

Recommended procedure (an inference from the above):
1. Programmer leads: GND, /CS, CLK, DI (programmer MOSI), DO (programmer MISO). Programmer 3.3 V output left unconnected.
2. Power the scope from the battery. Keep USB unplugged from the PC and from chargers while the programmer is attached (inference: avoids ground loops through the TP4056 charger path).
3. Use short leads. Use a low SPI speed where the programmer allows it.
4. Read twice and compare before any write. Reads cannot damage the flash contents.
5. If the board must be off during a write, tell the lead first: no source covers that mode.

## 3. Backups (mandatory)

Directory and naming. `backups/` is gitignored (CLAUDE.md). Layout:

```
backups/
  SHA256SUMS                         one file, regenerated after each backup
  2026-10-18_fpga-25q80_A.bin        <date>_<what>_<run letter>.bin
  2026-10-18_fpga-25q80_B.bin
  2026-10-18_mcu-w25q32_fwb.bin
  2026-10-18_sd_full.img
  2026-10-18_sd_sectors-0-2047.bin
  2026-10-18_sd_sector-708.bin, -709.bin, -710.bin
```

After creating files, from WSL (`wsl -d Ubuntu`):

```bash
cd /mnt/d/src/fnirsi-1013d-ai/backups
sha256sum *.bin *.img > SHA256SUMS
chmod a-w *.bin *.img
```

Copy the folder to a second drive too. Record each hash in `docs/hardware-status.md`.

### 3a. FPGA config flash (Winbond 25Q80), full 1 MB, twice

If the programmer is USB, attach it to WSL first (6.3 shows the usbipd commands; the CH341A is `1a86:5512` and the FT232H is `0403:6014` [general knowledge]). Install flashrom if missing: `wsl -d Ubuntu -u root -- apt install -y flashrom`.

1. Probe (read-only). Use the programmer name from the table in 2.1:

```bash
flashrom -p ch341a_spi
```

Expect a line naming a Winbond W25Q80 variant with 1024 kB. If flashrom lists several candidate names, pick the one that matches a 1024 kB Winbond part and repeat with `-c "<name>"` on every following command. If it finds nothing, stop and recheck wiring and power (step 4).

2. Dump twice (add `-c "<name>"` if step 1 needed it):

```bash
cd /mnt/d/src/fnirsi-1013d-ai/backups
flashrom -p ch341a_spi -r 2026-10-18_fpga-25q80_A.bin
flashrom -p ch341a_spi -r 2026-10-18_fpga-25q80_B.bin
ls -l 2026-10-18_fpga-25q80_*.bin          # both must be exactly 1048576 bytes
cmp 2026-10-18_fpga-25q80_A.bin 2026-10-18_fpga-25q80_B.bin && echo IDENTICAL
sha256sum 2026-10-18_fpga-25q80_*.bin
```

If the two dumps differ, stop. Suspect wiring, lead length, or a powered-board interaction. Reseat and repeat until two dumps match.

3. Compare with the known stock AL3 image. Two reference copies exist and they are byte-identical [checked locally; 1,048,576 bytes; SHA-256 `90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253`]:
   - Atlan4: `D:\src\Fnirsi1013D\Verzia 1.xxx\FPGA for 1.xxx\FPGA AL3\fpga-1013D-1-zb25vq80.bin` (also under `backup fpga\`). I did not find this file inside `research/atlan4-extracted`; use the `D:\src\Fnirsi1013D` path.
   - pecostm32: `D:\src\FNIRSI-1013D-1014D-Hack\Binaries\Original files\FPGA Flash\New version\W25Q80_FPGA.bin`.

   Structure (hack-repo-hardware-fpga.md §2.2; fpga-verilog-analysis.md bitstream table): 32 bytes of 0xFF, then sync word `cc55aa33` at offset 32, then `f0000006 18006c31 0bb7c200`, payload ending at byte 282,866, the rest erased. `18006c31` is the AL3-10 JTAG IDCODE, so it is the family proof for this unit.

```bash
REF="/mnt/d/src/FNIRSI-1013D-1014D-Hack/Binaries/Original files/FPGA Flash/New version/W25Q80_FPGA.bin"
xxd -s 32 -l 16 2026-10-18_fpga-25q80_A.bin
# expected: cc55 aa33 f000 0006 1800 6c31 0bb7 c200
sha256sum 2026-10-18_fpga-25q80_A.bin "$REF"
cmp -l 2026-10-18_fpga-25q80_A.bin "$REF" | wc -l      # number of differing bytes
python3 -I - <<'PY'
b=open('2026-10-18_fpga-25q80_A.bin','rb').read()
i=len(b)-1
while b[i]==0xff: i-=1
print('size',len(b),'last non-FF offset',i,'(reference: 282865)')
PY
```

   Interpretation: an identical hash means the unit carries the same stock bitstream as the reference unit. A different tail or a small diff means a different stock revision; record it, the dump is still a valid backup. A header other than `cc55aa33` at offset 32 means the unit is not an AL3 and must be re-identified (thread-pages-121-155.md §2.1: EF2 boards have no external flash; Altera boards use other images). A 2 MB image indicates the wrong source (thread-pages-121-155.md §3.3).

4. If probing fails, check in this order: the pin continuity table, the ground lead, that the scope is on, programmer voltage (2.2), chip orientation and protection bits (thread-pages-121-155.md §3.3). The 33 ohm series resistor on DO plus long leads can fail at high clock speed [inference].

### 3b. MCU flash (W25Q32), full 4 MB

Recommended: the on-scope backup tool by pecostm32, shipped identically by Atlan as `Firmware for create backup files/fnirsi_1013d_fwb.bin`. Both copies have SHA-256 `64bc81e6c0d499bb22a00d6e940071ea3324baaffcb9aacc59db734c6d3b11f6`, 180,296 bytes [checked locally]. Reason: no extra hardware, and it works with the stock firmware in place.

How it works (hack-repo-software.md §2.5; atlan4-history-and-docs.md §2.1; hack-repo-hardware-fpga.md §6):
- It is an eGON package written to the SD card at byte 8192 (`dd bs=1024 seek=8`, SD sector 16). The BROM boots it from the SD card instead of the SPI flash.
- On boot it shows progress and writes three files to the SD volume: `FNIRSI_1013D_full_flash_backup.bin` (the whole flash; read in 32 KB blocks when the JEDEC manufacturer is 0x20 or 0xEF and the capacity code is 0x16, i.e. a 4 MB part), `FNIRSI_1013D_tp_config.bin` (touch-panel config), and `FWB_FSI-1013.bin` (flash range 0x13000 to 0x1FE100 in update-file format, which includes calibration at 0x1FD000 and the hardware-check word at 0x1FE000).
- It then zeroes SD sector 16, so the next boot is normal, and exposes the card as USB mass storage. If the touch config is unusual it prints "!! SPECIAL TOUCH PANEL DETECTED !!"; the author asks for that file to be posted to the forum thread.
- Only 8 GB cards are tested. The card needs a FAT32 partition with 1 MiB free before it.

**This tool overwrites SD sector 16 onward.** Do 3c first, or use a spare card. If the card holds Atlan's bootloader, the tool destroys it (sector 16 is the bootloader).

Steps (Linux commands from the tool's readme; use WSL with the card in a USB reader attached via usbipd, or any Linux machine):
1. Complete 3c for the card you will use.
2. With the stock firmware, either start USB mass storage from its main menu, or use a reader. Identify the device with `lsblk`.
3. Make sure the partition leaves 1 MiB free at the start and is FAT32.
4. Unmount, then write the tool:

```bash
sudo umount /dev/sdX1
sudo dd if="/mnt/d/src/FNIRSI-1013D-1014D-Hack/Original firmware backup/fnirsi_1013d_fwb.bin" of=/dev/sdX bs=1024 seek=8
sudo umount /dev/sdX1
```

   `/dev/sdX` is a placeholder. Confirm the device before every `dd`; a wrong target destroys a disk.
5. Put the card in the scope and power it off and on. Wait until progress finishes and the card mounts.
6. Copy the three files into `backups/`. Power-cycle the scope and confirm it boots normally.
7. Verify:

```bash
cd /mnt/d/src/fnirsi-1013d-ai/backups
ls -l FNIRSI_1013D_full_flash_backup.bin           # expect 4194304 for a W25Q32
H="/mnt/d/src/FNIRSI-1013D-1014D-Hack/Binaries/Original files"
cmp -l FNIRSI_1013D_full_flash_backup.bin "$H/W25Q32_scope.bin" | wc -l
cmp -l FNIRSI_1013D_full_flash_backup.bin "$H/W25Q32.bin" | wc -l
xxd -l 32 FNIRSI_1013D_full_flash_backup.bin       # eGON.BT0 header at offset 0
```

   Expected stock layout (hack-repo-software.md §1.1): SPL `eGON.BT0` at 0x0000, update program at 0x6000, bitmap at 0x13000, main app at 0x27000, calibration block at 0x1FD000 (500 bytes, unit-specific), word 0x9086 at 0x1FE000. The calibration block and the main app revision may differ from the reference dumps; boot code and logo should match.

   Rename the file to `2026-10-18_mcu-w25q32_fwb.bin` and record its hash.

Restore status: the matching restore program is not validated on a newer scope ("Failed on my new scope", hack-repo-software.md §2.5). Treat the backup as the source for restoring through a hardware programmer or FEL, not as an on-scope restore guarantee.

Alternatives if the tool fails:
- Atlan's updater DOWNLOAD (SPI backup FLASH) copies the whole SPI flash to the SD card (repo-atlan4.md §4.1). It needs the Atlan bootloader and updater, so it belongs after step 5.3 of section 5, not before.
- FEL read with `sunxi-fel` (section 6): `sunxi-fel spiflash-read 0 4194304 out.bin` (hack-repo-software.md §2.2). Gives an independent second dump to cross-check. The thread's 2 MB figure is for the older W25Q16 boards (thread-pages-121-155.md §2.2).
- In-circuit read through the MCU flash header J1 (1 CS, 2 GND, 3 MISO, 4 3V3, 5 CLK, 6 MOSI per the schematic, hack-repo-hardware-fpga.md §2.1). The owner's description does not mention that header. Unconfirmed.

### 3c. SD card

If the card is the one the scope currently uses, image it before any other SD write.

1. Full image on Windows: Win32DiskImager in Read mode to `backups\2026-10-18_sd_full.img`. Needs free disk space equal to the card capacity.
2. Or in WSL: attach the card disk (`wsl --mount \\.\PHYSICALDRIVE<n> --bare` from an administrator PowerShell [general knowledge]), then:

```bash
lsblk
sudo dd if=/dev/sdX of=/mnt/d/src/fnirsi-1013d-ai/backups/2026-10-18_sd_full.img bs=4M status=progress conv=fsync
```

3. The reserved-area slice and the specific sectors, taken from the image:

```bash
cd /mnt/d/src/fnirsi-1013d-ai/backups
dd if=2026-10-18_sd_full.img of=2026-10-18_sd_sectors-0-2047.bin bs=512 count=2048
dd if=2026-10-18_sd_full.img of=2026-10-18_sd_sector-708.bin bs=512 skip=708 count=1
dd if=2026-10-18_sd_full.img of=2026-10-18_sd_sector-709.bin bs=512 skip=709 count=1
dd if=2026-10-18_sd_full.img of=2026-10-18_sd_sector-710.bin bs=512 skip=710 count=1
xxd 2026-10-18_sd_sector-710.bin | head -4
```

   Meaning (repo-atlan4.md §4.1; atlan4-history-and-docs.md §2.1, §2.3): 708 holds input calibration (checksum in words 0 and 1, data from byte 8), 709 holds settings, 710 holds display and touch config plus the boot byte. A valid 710 starts `aaaaaaaa 55555555`, then two timing words, then `cccccccc 33333333`. The boot byte is at offset 31 (repo-donwulff.md "Boot chain"); the thread says "byte 32" and "byte 31", likely 1-based versus 0-based (thread-pages-091-120.md §10).
   If the card never ran open firmware, these sectors may be zeros or hold FAT data. That is a valid result; record it.

4. Windows alternative for the three small items: Loader 1.1.3 (administrator rights) saves calibration (sector 708, `.cal`) and LCD/TP config (sector 710, `.cnf`) and shows the boot mode (atlan4-history-and-docs.md §2.2). It needs the scope in USB mode with open firmware, so skip it while the unit is stock.
5. "Device write access denied" in Loader means the partition offset is too small. The fix is `diskpart` and re-creating the partition (atlan4-history-and-docs.md §2.1). Do this only on a spare card, after imaging.

## 4. Current state assessment (read-only)

Goal: find out whether the unit runs stock or Atlan firmware, and which FPGA bitstream it has.

1. **Boot path.** The BROM tries the SD card at sector 16 first, then SPI flash (thread-pages-091-120.md §2 "SoC / boot"). Power on with no SD card. If the FNIRSI UI appears, the stock chain is intact. Then power on with the card inserted and see whether anything changes.
2. **Atlan bootloader and updater.** With open firmware on the SD card, the bootloader prints a banner `BOOT fw v0.x`, and a boot menu (PECO, FNIRSI, FEL) appears when the menu bit is set. The default starts after 2 to 3 s; touch extends it to 10 s (atlan4-history-and-docs.md §1.1 item 12; repo-atlan4.md §4.1). The updater screen shows `Updater firmware: v0.02x`. These strings exist in the binaries [checked locally]: v0.7 in Atlan's `bootloader v0.7.bin`; v0.8 with updater v0.02v in Donwulff's `bootloader_base.bin`.
3. **Scope firmware and FPGA version.** The open scope has a diagnostic screen showing "FPGA version", "fw: Fnirsi/PECO" and the chip name, reached from "Diagnostic view" in the main menu (atlan4-history-and-docs.md §1.4, §2.1). Version 1.00o5 reads the FPGA word: 0x1432 = FNIRSI original, 0x1532 = Atlan AL3, 0x1632 = Atlan EF2. Later versions use a scheme 0xZYYY (Z = 1 OEM, 2 EF2, 3 AL3, 4 EP4) that needs a matching new bitstream (thread-pages-121-155.md §3.1).
4. **Stock UI.** No source shows the FPGA version or the firmware version in the stock FNIRSI UI. The stock firmware reads the FPGA word only in hardware-check mode (hack-repo-hardware-fpga.md §1.3, row 0x06). Unresolved. Substitutes: the 3b dump compared with reference dumps, and the 3a hash. If the 3a hash equals `90f65f08...`, the bitstream is the same as the reference unit's, which reports 0x1432.
5. **Earliest FPGA readout.** After section 5 step 3, with the stock bitstream still in place (open scope 1.00o5 accepts 0x1432).
6. **Open firmware on the SD card.** The 3c image shows it: `eGON.BT0` at byte 8192, the updater at sector 80 and the scope at sector 750 (repo-atlan4.md §4.1).

## 5. Tier-1 upgrade procedure (execute only after the project confirms)

Do not start this section without the lead's go. Sections 1 to 4 must be complete.

### 5.1 What exists and what does not

| Item | Where | Status |
|---|---|---|
| Bootloader v0.8 plus updater v0.02v | `D:\src\FNIRSI_1014D_Firmware\fnirsi_101xd_scope\bootloader_base.bin`, 375,808 bytes, SHA-256 `628ba21b86617fcf4bd719b2c40b159f1fe6bf0971cb8fb26a304b634d9b497d` [checked locally] | Donwulff describes it as Atlan4's "BOOT fw v0.8", placed at sector 16 and spanning to sector 750 (repo-donwulff.md "Boot chain" and build notes). Binary only; no source in any tree (SYNTHESIS.md §4, §9). |
| Atlan scope 1.00x binary | not in the `D:\src\Fnirsi1013D` checkout | That checkout has source zips for 1.00g to 1.00o5. The folder `latest firmware version` holds only 0.026j and 0.26k4 [checked locally]. SYNTHESIS.md mentions a 1.00q2 binary; none was found. Build 1.00o5 from `firmware/scope` once that tree exists, or obtain Atlan's latest binary. |
| Atlan "first use" image | `Guide to firmware\Frst use (install) firmware\fnirsi_1013d.bin`, 695,368 bytes | Contains bootloader v0.7 and updater v0.02l (strings checked locally). v0.7 hangs on a custom FPGA, so it is not usable for the custom bitstream. |
| Migration image | `Guide to firmware\Transition ...\fnirsi_1013d_migration.bin` | Banner `BOOT fw 0.06`, updater v0.02g. Older still. |
| Loader | `D:\src\Fnirsi1013D\loader_v1.1.3.zip` and `Guide to firmware\loader_v1.1.3.zip` | Windows; writes raw sectors; needs the scope in USB mode (atlan4-history-and-docs.md §2.2). |
| Guide text | `Guide to firmware\FNIRSI 1013D  alternative firmware v1.00n3v ANG v1 – kópia.doc` | Digested in atlan4-history-and-docs.md §2.1, §2.2. |
| AL3 bitstream | `Verzia 1.xxx\FPGA for 1.xxx\FPGA AL3\FPGA.bin`, 282,898 bytes, SHA-256 `0d4c829c231b3bbe18c6dd6e815523b6e168f787561376aae502db6c1f370fd2` [checked locally] | Version word 0x1532 (SYNTHESIS.md §3). Same header and sync word as the stock image; 47,494 bytes differ (fpga-verilog-analysis.md bitstream table). |

The task brief points at "latest firmware version" and "Guide to firmware". The checkout does not carry bootloader >= 0.8 or a 1.00x scope binary there. Ask the lead which source to use. The rest of this section assumes a prepared SD image `tier1.img` with bootloader v0.8 at sector 16 (from `bootloader_base.bin`) and the scope 1.00x at sector 750 (repo-donwulff.md: scope overlaid on the bootloader base at offset 0x5BC00, which is sector 750).

### 5.2 Order matters

Gate (repo-donwulff.md "Flashing routes"): install and test bootloader >= 0.8 before writing the custom bitstream. Older loaders spin forever waiting for FPGA 0x1432. Bootloader v0.8 contains that wait but never calls it, per Donwulff's disassembly (repo-donwulff.md "Boot chain").

### 5.3 Steps

1. Use a spare SD card, not the backed-up card (Atlan: "safe approach: use a second SD card", atlan4-history-and-docs.md §2.2). FAT32 partition with at least 1 MiB free before it.
2. Write the image at byte 8192 as in the Atlan readme (atlan4-history-and-docs.md §2.2):

```bash
sudo umount /dev/sdX1
sudo dd if=tier1.img of=/dev/sdX bs=1024 seek=8
```

   Atlan's "ALL" images clear sectors 708 to 710 (thread-pages-121-155.md §2.2, §4.1), so expect to recalibrate (Base calibration, then input dividers; order in atlan4-history-and-docs.md §2.1). Restore the 3c sector dumps instead if the calibration should survive.
3. Boot the scope with this card, still with the stock FPGA. Expect the `BOOT fw v0.8` banner, then the scope. Open the diagnostic screen; it should report FPGA 0x1432 and "fw: Fnirsi" (1.00o5 handles the stock word: atlan4-history-and-docs.md §1.4). Note it in the status log. Run Base calibration with the BNCs disconnected (repo-donwulff.md "Flashing routes": first power-on with nothing on the BNCs, relay polarity unverified).
4. Confirm a return path: set the boot byte so the menu shows (bit 4 set, e.g. 5 = FNIRSI with menu), or confirm the menu appears at boot (atlan4-history-and-docs.md §2.1 "Other settings menu").
5. Power off and disconnect the battery. Prepare the padded bitstream. flashrom wants a file the size of the chip, and Atlan's `FPGA.bin` is 282,898 bytes, so pad with 0xFF to 1,048,576 bytes (the stock image's tail is erased 0xFF) [inference; flashrom's size rule is general knowledge]:

```bash
cd /mnt/d/src/fnirsi-1013d-ai/backups
python3 -I - <<'PY'
src='/mnt/d/src/Fnirsi1013D/Verzia 1.xxx/FPGA for 1.xxx/FPGA AL3/FPGA.bin'
b=open(src,'rb').read()
assert len(b)==282898
open('al3_atlan_0x1532_padded.bin','wb').write(b+b'\xff'*(1048576-len(b)))
PY
sha256sum al3_atlan_0x1532_padded.bin
# expected 8a9d3559566fbe9653ad52612818d336ce7f905498f422f228bb165335c42177 [checked locally]
```

6. Connect the programmer as verified in section 1 and powered as chosen in 2.3. Probe again, then dump once more and confirm it equals dump A from 3a. This guards against changed wiring or a flaky contact.
7. Write and verify (flashrom also verifies after writing by default):

```bash
flashrom -p ch341a_spi -w al3_atlan_0x1532_padded.bin
flashrom -p ch341a_spi -r readback.bin
cmp readback.bin al3_atlan_0x1532_padded.bin && echo VERIFIED
```

   Stop on any error. Do not power-cycle the scope after a failed write; reflash the stock dump first (5.4).
8. Remove the programmer and power-cycle. Expect backlight and scope UI. The FPGA takes about 900 ms to configure (hack-repo-software.md §2.4). The diagnostic screen should show 0x1532 (fw: PECO, AL3). Record it. The generator and memory-depth menus should now work (atlan4-history-and-docs.md §2.1).

### 5.4 Rollback

1. Restore the stock bitstream. This is the only way back; the scope cannot restore its stock FPGA:

```bash
flashrom -p ch341a_spi -w 2026-10-18_fpga-25q80_A.bin
flashrom -p ch341a_spi -r rb.bin && cmp rb.bin 2026-10-18_fpga-25q80_A.bin && echo VERIFIED
```

2. Always restore the FPGA first. Stock CPU firmware with a custom FPGA is not a valid state: Donwulff states that the loader's stock-firmware restore does not restore a stock FPGA (repo-donwulff.md "Flashing routes").
3. Then run the stock CPU firmware from the SPI flash (the W25Q32 is untouched by Tier 1), in either of two ways:
   - Remove the SD card, or zero the loader block: `sudo dd if=/dev/zero of=/dev/sdX bs=1024 seek=8 count=1`. The BROM falls back to SPI flash (hack-repo-software.md §2.4; repo-donwulff.md "Boot chain"). This runs the complete stock chain, including its display init.
   - Or set the sector-710 boot byte to 1 (FNIRSI): from the boot menu, from Loader 1.1.3's Boot Mode page, or by hand on a card image: `printf '\x01' | dd of=sdcard.img bs=1 seek=$((710*512+31)) conv=notrunc`. The Atlan bootloader then loads the stock main app from SPI 0x27000 (repo-atlan4.md §4.1). pcprogrammer notes this path skips the stock bootloader's display init, which can shift the picture (thread-pages-091-120.md §2 "Display / touch").
4. If the W25Q32 content was damaged, restore the 3b backup via FEL (`sunxi-fel -p spiflash-write 0 <file>`, hack-repo-software.md §2.2). That path has no documented validation on this board. Use it only when needed.

## 6. FEL mode

FEL is the F1C100s boot-ROM USB mode. Loading code into RAM touches no flash and no SD card, so it is safe. Only `spiflash-write` changes the MCU flash.

### 6.1 Entering FEL on the 1013D

What the sources state:
- Atlan's bootloader (>= 0.04) has three choices: 0 = PECO (SD firmware), 1 = FNIRSI (stock from SPI 0x27000), 2 = FEL. Choice 2 jumps to the BROM FEL entry at 0xFFFF0020 (repo-atlan4.md §4.1). The choice comes from the sector-710 boot byte at offset 31 or from the touch menu: 0, 1, 2 without menu; 4, 5, 6 with menu; default 0xFF (thread-pages-091-120.md §2; repo-donwulff.md "Boot chain").
- SD-card route from pecostm32: write `fel-sdboot.sunxi` (8,192 bytes) with `sudo dd if=fel-sdboot.sunxi of=/dev/sdX bs=1024 seek=8`. Reference copy: `D:\src\FNIRSI-1013D-1014D-Hack\sunxi_stuff\` (hack-repo-software.md §2.2). Remove it afterwards with `dd if=/dev/zero of=/dev/sdX bs=1024 seek=8 count=1`. This route works on a stock unit; use a spare card.
- The BROM also enters FEL when neither the SD card nor the SPI flash has a valid boot image (thread-pages-091-120.md §2). Do not use that route: it means damaging a boot image.
- There is no PC-triggered reset; FEL needs a power cycle (thread-pages-091-120.md §2).
- A hardware FEL-force is not documented. The hack repo marks two pads "GND HERE" on the old board, purpose presumed to be FEL, flagged as inference (hack-repo-hardware-fpga.md §2.1, §5). The thread says F1C100s pin 70 is RESET and "short to GND to free SPI bus" (thread-pages-001-030.md, "Test access" row). The 1013D has no boot jumper (repo-atlan4.md §4.1). Do not try either.
- Recovery from an accidentally chosen FEL default: zero the boot byte or reformat the card; pcprogrammer suggests writing 0xFF to sectors 16 to 800 (thread-pages-091-120.md §2, §8).

Limit: stock FEL does not initialise DRAM. Code that needs DRAM must start from a loader that already did it, such as the Atlan bootloader's FEL choice or pecostm32's `fnirsi_1013d_startup_with_fel.bin` (thread-pages-091-120.md §2; hack-repo-software.md §2.2; repo-donwulff.md "Boot chain"). The SPI-flash commands of `sunxi-fel` work without DRAM.

### 6.2 USB identity

The host sees USB ID `1f3a:efe8` ("Onda V972 tablet in flashing mode"; hack-repo-software.md §2.2). The scope has one USB-C port (SYNTHESIS.md §2). Use a data cable.

### 6.3 Windows driver and WSL attach

Two independent routes. Pick one.

**Route 1: usbipd-win into WSL, then `sunxi-fel`.** `sunxi-fel` is Linux only (thread-pages-091-120.md §2). All usbipd commands are [general knowledge].

```powershell
# one-time, elevated PowerShell
winget install --id dorssel.usbipd-win
# with the scope in FEL mode and connected:
usbipd list                                    # find the BUSID of 1f3a:efe8
usbipd bind --busid <BUSID>                    # elevated
usbipd attach --wsl Ubuntu --busid <BUSID>
```

```bash
wsl -d Ubuntu -u root
lsusb | grep 1f3a:efe8
cp "/mnt/d/src/FNIRSI-1013D-1014D-Hack/sunxi_stuff/sunxi-fel" /root/sunxi-fel && chmod +x /root/sunxi-fel
/root/sunxi-fel version
/root/sunxi-fel spiflash-info
```

The device vanishes and reappears at every power cycle and when the loader jumps, so re-attach each time (`usbipd attach --wsl Ubuntu --busid <BUSID> --auto-attach` helps). Do not install a Zadig driver on this device if you use usbipd; the two compete for the driver. The repo's `sunxi-fel` is an x86-64 Linux ELF with SPI-flash support from Icenowy's `f1c100s-spiflash` branch (hack-repo-software.md §2.1), SHA-256 `86891669b2d9062c3d9e9e19b17161a9fb12281bc65a4492eda53540c1695db4` [checked locally]. Copy it out of the reference tree and run it from the copy, as above.

**Route 2: native Windows with a Zadig WinUSB driver.** Run Zadig, enable "List all devices", select the device with USB ID 1f3a efe8 and install WinUSB. Then use a Windows FEL tool. The thread mentions `xfel.exe spinor read 0 2097152 flash_dump.bin`, posted for a 1014D (thread-pages-121-155.md §2.2). It is untested here, and a W25Q32 needs 4194304 bytes. [Zadig steps: general knowledge]

### 6.4 Commands (from hack-repo-software.md §2.2)

```bash
sudo ./sunxi-fel version
sudo ./sunxi-fel spiflash-info
sudo ./sunxi-fel spiflash-read 0 4194304 mcu_flash.bin        # read-only, safe
sudo ./sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000   # RAM only; DRAM must already be up
```

`version`, `spiflash-info`, `spiflash-read` and the RAM `write ... exe` are safe. `spiflash-write` rewrites the MCU flash and falls under section 0, rule 1.

## 7. Troubleshooting and recovery

| Symptom | Likely cause | Action |
|---|---|---|
| Hang after the loader banner, nothing else; custom FPGA installed | Bootloader older than v0.8 loops until the FPGA reports 0x1432. v0.4 and v0.7 both do (thread-pages-121-155.md §3.3; repo-atlan4.md §4.1; repo-donwulff.md "Boot chain") | Fastest: write bootloader v0.8 to the SD card with a reader, or reflash the stock bitstream (5.4). The SD route is safe because the BROM loads sector 16 before any FPGA wait. |
| Blank screen, no backlight | The FPGA drives the backlight PWM (command 0x38), and the stock unit loops if the FPGA is absent (thread-pages-121-155.md §2.1; hack-repo-hardware-fpga.md §2.1) | Check the FPGA is configured: re-read the flash and compare with dump A, programmer leads removed, flash seated. Brightness regressions were seen on AL3 builds after a dimming-frequency change (thread-pages-121-155.md §2.2). See the next two rows. |
| Image shifted, edge noise | Wrong display timing in sector 710 | `sudo dd if=2026-10-18_sd_sector-710.bin of=/dev/sdX bs=512 seek=710 count=1 conv=notrunc`, or Loader's LCD/TP tab, or the config sector images in the Atlan repo `dist` folder (atlan4-history-and-docs.md §2.3). |
| Touch dead or mirrored | Touch config lost (sector 710 swap bytes 28 and 29, or the panel itself) | Restore sector 710 as above. Updater `TP&screen.bin` upload and download is the other route (atlan4-history-and-docs.md §2.2). `FNIRSI_1013D_tp_config.bin` from the 3b backup can go back to the panel with the pecostm32/Atlan TP writer utilities (hack-repo-software.md §2.5; atlan4-history-and-docs.md §2.2). The stock firmware rewrites the panel config at start-up (thread-pages-091-120.md §2). |
| Scope boots into FEL with no menu | Boot byte 2 | Zero the boot byte or write 0xFF to sectors 16 to 800 (thread-pages-091-120.md §2, §8). |
| "SD ERROR", "boot sector not found", "Failed to open file" | No 1 MiB gap before the partition, missing `scope.bmp`, or a bad card (thread-pages-121-155.md §2.2, §4.3) | Re-create the partition (3c step 5). Copy `scope.bmp` and `updater.bmp` to the card root. |
| Calibration wrong or lost | Sector 708 cleared by an image write (thread-pages-121-155.md §4.1) | Write back the 3c sector 708 dump, or redo Base calibration (failure causes in thread-pages-121-155.md §4.1). |
| flashrom finds nothing, or reads differ | Wiring, voltage, lead length, protection bits (thread-pages-121-155.md §3.3) | Redo 1.3 and 3a step 4. |
| Dead scope after an FPGA write | Wrong or corrupted image | Reflash dump A (5.4). engineer.r152's unit was recovered with a CH341 (thread-pages-121-155.md §3.3). |

How the stock firmware can always be restored:
1. Stock CPU firmware: it lives in the W25Q32 and Tier 1 never touches it. Removing the SD card, or zeroing the sector-16 loader, boots it (hack-repo-software.md §2.4). The 3b backup is the safety copy.
2. Stock FPGA: the 3a dump A, written with the external programmer (5.4).
3. Calibration: the 3c sector 708 dump, and the stock block at flash 0x1FD000 inside the 3b dump.
4. No unrecoverable case is known for SD-only changes. A damaged W25Q32 is recoverable through FEL given a valid backup (6.4).

## 8. Unresolved items for the owner or lead

1. Pinout: ded's differing pinout (page 142, post 3542) is not in the notes. Verify by continuity (section 1) and read that post.
2. Where Atlan's bootloader v0.8 and a 1.00x scope binary should come from (5.1).
3. Behaviour of the FPGA and its configuration bus when the flash is read or written with the board powered. Only reads are bench-proven (2.3).
4. Whether the unit has an SD card holding open firmware now, and which stock firmware date it runs. The stock UI offers no documented version display.
5. Whether the owner's board has the MCU-flash header J1 (3b alternatives).
