# Programming the FPGA config flash from the MCU

Question: can the Winbond 25Q80 on the AL3A10 board (2x3 header J2 `FPGA_FLASH`) be programmed by the F1C100s instead of an
external SPI programmer? Read-only research, no hardware touched. `[I]` marks an inference, `[G]` general knowledge not
verified against an Anlogic document (none exists in the research trees).

## Short answer

- **Not on an unmodified board.** The config flash is wired only to the FPGA configuration pins. No stock, Atlan or own
  bitstream changes that, because the MCU has no connection to those lines.
- **Yes with a hardware mod**, and it already exists: Atlan's updater reflashes the FPGA flash from the scope once the FPGA
  flash is wired to the MCU's SPI0 pads. This costs the MCU flash (desoldered) and with it the stock firmware.
- **Possible without that mod only through a new FPGA SPI bridge** (section 4). This is feasible on paper, unproven on the
  AL3A10, and every failure mode ends in a programmer.

## 1. Hardware

- **FPGA flash is on dedicated config nets.** The EasyEDA schematic nets are `FPGA_Conf_nCE`, `FPGA_Conf_CLK`,
  `FPGA_Conf_Data`, `FPGA_Conf_ASDO`. Each name occurs only twice in the file (flash side and FPGA side), and none of them
  appears next to `SPI0_*` or `FLASH_*` (`D:\src\FNIRSI-1013D-1014D-Hack\Schematics\1013D\EASYEDA\SCH_Scope 1013D_2023-01-07.json`,
  grep checked locally). The schematic is a reconstruction (`research/notes/hack-repo-hardware-fpga.md:146`), so treat as strong
  evidence, not proof.
- **MCU flash is a separate chip** on SPI0 PC0 CLK, PC1 CS, PC2 MISO, PC3 MOSI with its own header J1
  (`hack-repo-hardware-fpga.md:138`). Old forum finding agrees: the FPGA flash "only connects to the FPGA"
  (`research/notes/thread-pages-001-030.md:33`; pcprogrammer on p148: FPGA flash cannot be written otherwise,
  `thread-pages-121-155.md:65`).
- **Header pinout (reconstructed):** 1 nCE, 2 GND, 3 Data (33 ohm series), 4 VCC via a Schottky, 5 CLK, 6 ASDO
  (`docs/hardware-prep.md:32-43`). Atlan's note says some layouts have the SPI pins shuffled
  (`thread-pages-121-155.md:21`).
- **Config pins on the 1013D FPGA:** ASDO pin 6, nCE pin 8, DATA0 pin 13 (`hack-repo-hardware-fpga.md:155`, naming borrowed
  from the Altera variant, since AL3 and EP4 share a pinout, `SYNTHESIS.md:14-15`). The clock pin is not listed.
- **Pin reuse after configuration.**
  - Neither the stock re-implementation (`D:\src\Anlogic_AL3-10_Analyzing\FNIRSI-1013D_FPGA\Original_1013D\source\Original_1013D.adc`)
    nor ours (`fpga/al3/src/zaklad.adc`) constrains any config pin. Both use only the CPU bus (P1-3, P135-144), ADC, relay,
    offset, backlight and calibration pins. The stock design therefore never touches the flash lines after configuration.
  - Whether the AL3A10 lets CCLK/nCE/ASDO/DATA0 become user I/O is **not documented in any file here**. No Anlogic
    datasheet or Tang Dynasty option dump is in the trees. [G] Anlogic Eagle parts offer dual-purpose config pins released
    to user I/O by a TD bitstream option; [I] this likely applies but must be confirmed from the TD manual or by a build.
    CCLK may be a dedicated pin with no user mode, which would block a bridge that must clock the flash itself.

## 2. Stock firmware and stock updater

- **The stock firmware update never touches the FPGA flash.** It looks for `UTX/FSI/DAN/YPK-1013.bin`, writes the MCU SPI
  flash from 0x13000 and verifies check dwords (`research/notes/hack-repo-software.md:65-67`). The FPGA bitstream is a
  separate flash that FNIRSI factory-programs; no FPGA command that writes flash is known. The FPGA command set
  (`fpga/al3/src/FPGA commands.txt`, `research/notes/fpga-verilog-analysis.md`) has no flash or SPI function, and the stock
  bitstream has no pins for one.
- **The owner's "FPGA flash" option is almost certainly Atlan's.** The updater menu text is `FPGA Flash - Fnirsi FPGA.bin
  ( Hardware modification required )` (`research/atlan4-extracted/updater 0.02o decrese spi speed/.../fnirsi_1013d_updater/update.c:191`)
  alongside `FPGA firmware - FPGA.bin ( Hardware modification required )` (`update.c:185`). Sleo's PC Loader handles SD
  sectors and the firmware image, not the FPGA flash (`thread-pages-061-090.md:78`).
- **Not found:** any FNIRSI-original PC tool, "DSO upgrader/uploader" or "DSO 1023", with an FPGA flash function. The notes
  never mention it. If the owner has that tool, identify it (file name, screenshot) before assuming it is the same thing.

## 3. Atlan's updater

- The `W25Q80/16/32/64/128, GD25Q, ZB25VQ80A` list is the JEDEC size table of the **one SPI0 flash the MCU can reach**
  (`repo-atlan4.md:189`, `spi_control.c:84-165`). It does include the ZB25VQ80A because that is a common FPGA flash part,
  but the code has no second bus.
- **FPGA flash writing is the same code path.** `statemachine.c:228` calls `write_file_to_spi_flash_with_progress("firmware
  Atlan & PECO FPGA/FPGA.bin", 0)` and `statemachine.c:323` does the same for `firmware_FNIRSI_FPGA/Fnirsi FPGA.bin`, both at
  SPI address 0 via SPI0 on PC0-3 (`spi_control.c:42`). Its "MCU flash" and "FPGA flash" buttons differ only in file and
  address.
- **Consequence:** the buttons write to whatever chip sits on SPI0. They reach the FPGA flash only after the mod in
  `thread-pages-121-155.md:65`: desolder the MCU flash, wire its pads to the FPGA flash header. The scope then boots stock
  firmware from nowhere, so Atlan requires the SD-card boot chain (bootloader >= 0.8) and says original firmware is gone
  (p148 #3690, `thread-pages-121-155.md:147`). Writes erase and program per 4 KB sector with read-back verify, SPI clock
  reduced, writes refused without a backup (`repo-atlan4.md:160`).
- Our `AGENTS.md` rule (keep stock firmware bootable) is incompatible with this mod. `docs/hardware-prep.md:24` already
  excludes it.

## 4. Conclusions

### (a) Is MCU-driven FPGA flash programming possible today?

| Bitstream | Unmodified board | With the SPI0-to-FPGA-flash rewire |
|---|---|---|
| Stock (0x1432) | No | Yes, updater works, stock MCU flash gone |
| Atlan 0x1532 | No | Yes, this is Atlan's documented route |
| Ours | No | Yes, same as Atlan |

### (b) A bitstream-level SPI bridge: constraints

[I] throughout. Nothing here is built or tested.

1. **Shape.** Add commands on the existing parallel bus (for example a chip-select, a byte-shift and a status command), with
   the FPGA driving the flash pins. MCU firmware then sends erase/program/read sequences. About 100 LUTs. The 8-bit bus at
   ~0.3 us per byte (`AGENTS.md`) is faster than the flash needs: a 283 KB image at tens of kbit/s per page-program step
   takes seconds to a minute.
2. **Pin legality is the blocker.** The bridge must drive CLK, nCE, ASDO and read DATA0 as user I/O after configuration.
   Needs: (i) a TD option to release the dual-purpose pins, and (ii) CCLK being usable as an output. Neither is confirmed
   (section 1). First step: read the TD manual or build a pin-assigned test design, then check on the bench that the
   flash can be probed (JEDEC ID) from the running FPGA.
3. **Electrical.** Flash VCC is fed via a Schottky from 3V3 (`docs/hardware-prep.md:38`), and pin 3 /WP and pin 7 /HOLD
   share that node, so write protect is not under software control. The flash is not powered down while the FPGA runs, so
   nothing special for contention. Config pins are 3V3 (`hack-repo-hardware-fpga.md:146`). A 33 ohm series resistor sits on
   Data. No timing issue: the flash tolerates any slow clock.
4. **Self-modification is safe by itself.** The FPGA loads its SRAM once at power-up, so rewriting the flash while the old
   design runs does not disturb it. The danger is only a bad or interrupted image.
5. **Bricking.** A failed or partial write leaves a flash that fails configuration at the next power cycle. The FPGA then has
   no bus, no ADC and no backlight PWM. The MCU (SD boot) still runs, but it can no longer reach the bridge, so nothing on
   board can repair it. Recovery needs an external programmer on J2 (what the owner is setting up now), or JTAG on the pads
   (2.5 V level, `hack-repo-hardware-fpga.md:146`; needs an Anlogic-compatible adapter, which Atlan lists for EF2 only,
   `repo-donwulff.md:213`).
6. **Mitigations.** Write to a staging area (the 1 MB flash holds the ~283 KB image three times), verify a CRC in the
   bridge, and only then erase and copy sector 0. This shrinks the window of vulnerability to one copy, it does not remove
   it. A true fallback image needs the FPGA to boot from a second offset. Anlogic multi-boot support is [G] unknown
   here and is not assumed.
7. **Bootstrapping.** The bridge must be in the bitstream that is first loaded with the external programmer. Every later
   change could then come from the MCU. The first bridge build is the highest-risk image.

## 5. Recommendation

1. **Use the external programmer for FPGA changes now.** The header is soldered and the programmer chosen
   (`docs/hardware-status.md`). Do not do Atlan's rewire: it removes the stock firmware path.
2. **Do not build a bridge yet.** It saves a clip-on lead and gains little compared with the risk and the unverified pin
   legality.
3. **Cheap de-risking, when the programmer is on the board:**
   - Read the stock image and keep the dump (already planned).
   - Search the Tang Dynasty manual / install tree for dual-purpose or `CCLK` user-I/O options. If it is absent, the bridge
     idea is dead and this document is closed.
4. **If the owner still wants iteration without clipping:** the faster route is JTAG SRAM loading of test bitstreams (no
   flash write, so no brick risk). That needs an Anlogic-compatible JTAG adapter and is a separate question.
5. **Ask the owner** which tool showed the "FPGA flash" option. If it is not Atlan's updater, send its name or a screenshot.
