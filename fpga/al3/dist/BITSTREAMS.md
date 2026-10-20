# FPGA bitstreams for the FNIRSI 1013D, AL3A10 variant

These images are for the **Anlogic AL3A10** board only (unmarked 144-pin LQFP FPGA next to a Winbond 25Q80 config
flash with a 2x3 header, JTAG IDCODE 0x18006C31). Each one starts with 32 bytes of 0xFF, then the sync word
`cc55aa33` and the device-id packet `f0000006 18006c31`. Do not write them to an EF2L45 or EP4CE6 board: those
variants configure their FPGA differently (`docs/hardware-prep.md` section 3a).

The firmware works without any of these. On the stock bitstream (0x1432) or Atlan4's 0x1532 it runs the legacy
path; the features in the table below appear when the bitstream reports them. Flashing the FPGA needs a 3.3 V SPI
programmer on the config-flash header and a verified backup first.

## Which one to use

| Version | Status | Adds (on top of the row above) | Firmware that uses it | Bench |
|---|---|---|---|---|
| 0x1532 | Atlan4's, not ours | 16,384 samples per channel, programmable pretrigger, the 32-bit DDS square-wave generator on the calibration pin | every version | Atlan4's release; on the owner's unit 2026-10-18 |
| 0x1533 | supported | external trigger input on the calibration pin | 0.2.9 and later | flashed 2026-10-18; EXT loopback 10 of 10 captures triggered |
| 0x1534 | supported | decimation filter before the decimator: boxcar, CIC2, CIC3; 16-bit samples | 0.2.30 and later | flashed 2026-10-18; decimation bench |
| 0x1535 | supported | compensation FIR after the CIC (boxcar+FIR, CIC2+FIR, CIC3+FIR at 10 MSa/s and slower) | 0.2.44 and later | flashed 2026-10-19; alias of a 892.857 kHz square at 1 MSa/s -76.1 dBV with CIC2+FIR |
| 0x1536 | unbenched | segmented capture and hardware averaging (sequencer in front of the 0x1535 capture logic) | 0.2.58 and later | built and simulated, never flashed |
| 0x1537 | withdrawn, not attached | deterministic record start and A/B order at divided rates | 0.2.58 and later | flashed 2026-10-19: CH2 core A garbled in about 40 % of full-rate captures |
| 0x153B | **recommended** since 2026-10-20: benched on the maintainer's unit with the firmware's capture checks (needs fw 0.2.97+) | records that end on the auto time-out or the firmware's forced stop report 0x14 at the write pointer (W - total + pretrigger), so untriggered records are the newest total words instead of ring 0 .. total - 1 spliced at the stop; the time-out no longer cuts a triggered record; no trigger after the stop (0x18 is the record's) | 0.2.81 and later detect it (the 0x153A..0x153F range); the firmware notes in README section 11 for the full benefit | built and simulated (tb_timeout 75 checks + case B, the 19:52 host capture byte for byte; the 0x153A source fails 30; the full 13-step run ALL PASS on seeds 12345 and 4242); flashed 19:31 and verified; the 19:52 host checks read every record as spliced, but every saved record is flat at code 128 +- 1 on both channels (probes off the post after the open-input wizard; README section 11 "Bench 2026-10-20 19:31-20:03"): nothing measured; README section 11 bench checks 0-6 with the probe on the post |
| 0x153A | supported (flashed and benched 2026-10-20 10:03..18:00; superseded by 0x153B at 20:42: untriggered auto-mode records could be spliced, fw 0.2.97+ works around it) | ADC data captured in IOB registers at a selectable stage per core against the edge the core is encoded on, B first by T/2 at every rate; capture self-check (read 0x36) and training registers 0x63 / 0x64; fixes the garbled cores near codes 128..145 of 0x1537-0x1539 | 0.2.81 (the wizard trains the capture stages; the power-on stages can be wrong per unit) | 2026-10-20: wizard trained 0x08E3, band sweep clean at all four rates, self-check 0 flagged of 18865 captures (docs/hardware-status.md 10:04-10:49) |
| 0x1539 | withdrawn (flashed 2026-10-19, A cores garbled at codes 128..145) | MCU readout byte loaded on the strobe start and held; fixes the bit mixing of 0x1537/0x1538 near multi-bit code boundaries; i_mcu_clk constrained | 0.2.62 | flashed 2026-10-19 20:06: CAL_PROBE passes, but both A cores still garbled at means 128..145 at 200 MS/s (the ADC data capture window, not the readout); superseded by 0x153A |
| 0x1538 | withdrawn (flashed 2026-10-19, MCU readout mixed bytes near code boundaries) | stateless ADC encode: core order B-first guaranteed at every rate and after any write; self-check read 0x33 (0xF6 = good); fixes the encode-phase race of 0x1532-0x1536 and the marginal flop of 0x1537 | 0.2.58 (0.2.62 reads 0x33) | flashed 2026-10-19 17:46: encode clean, but the MCU readout mixes bytes near multi-bit code boundaries; superseded by 0x1539 |

**Use the recommended image**, which is the newest one. The older images are attached for comparison and for
going back to a state someone reported against. 0x1532 to 0x1536 share an ADC encode-phase race: after a sample-rate
change one core of a channel can deliver garbled 200 MSa/s data. Firmware 0.2.61 and later checks every full-rate
capture after a rate change and retries, and Diagnostics shows the FPGA line of these versions in amber
(`CHANGELOG.md` 0.2.61; `fpga/al3/README.md` "Changes" section 7).

Every firmware from 0.2.9 on recognises 0x1533 to 0x153F as the AL3 family (`docs/protocol.md` section 5.1), so an
older card firmware still boots on a newer bitstream; it just does not use the newer features. The firmware of this
release supports every row of the table.

**After changing the bitstream, calibrate again.** The calibration stores per-core values, and the core order can
differ between bitstreams. Diagnostics shows "FPGA changed since calibration" until you run the wizard
(Menu > Diagnostics > Calibrate; `firmware/README.md` section 6.4).

## Files and hashes

Release assets, per version (0x1537 is not attached):

- `fnirsi_1013d-fpga-al3-<version>.bin`: the bitstream as TD's bitgen wrote it, 282,898 bytes.
- `fnirsi_1013d-fpga-al3-<version>-flash1m.bin`: the same, padded with 0xFF to 1,048,576 bytes, the size of the
  25Q80. Write this one; it is what the owner's unit was flashed with.

| Version | `.bin` sha256 | `-flash1m.bin` sha256 | Source commit |
|---|---|---|---|
| 0x1533 | `a96567031872ae99bd40f0722a111a08da0f6381b6c710b70998af784a341216` | `96159a871de3af57c2174fe1ec66ed14b10fbe2e845c44564e7bda9ed252acae` | 526c969 |
| 0x1534 | `83140510dfbfef56cc5ed15d70866e6bc52c407a41fd0ea480e8993df5b820c1` | `c387a15ed11d209deab639d1de84dd3117419d800aed05e032b192650e13c0a0` | 6d4c8ad |
| 0x1535 | `5040a2aeede071019bca0ffee47d765b87d980e1e47ca01ac32cb875c839aa96` | `5ade9e755211fe65c6ff492419f3a4a91ec08edc8c16305ce52751baf9d588a5` | e2aec2e |
| 0x1536 | `4eaf6811814c58e1f00e648b97e7a608bb5ca387fb036f21714e91667959b48b` | `66866c84ad4f298c88cfc1072870d44e80832852b53fcbd926e01fcb473f7408` | 27c0c75 |
| 0x1537 | `426a3784883b82c9c9f42870584e64d1d082cdcb38c8b62db143e161e2633b17` | `febc92997ee6605090d5581aabc9c240986fa82defdc24d40aca01566dbfe1f1` | c15981e |
| 0x153B | `fdc3c21b57341b0774e8a5e58df04e38d1d223fb7c354b6ed49f5892f711fc3d` | `1391978c33379271789a9405f7b0fb9395a85e223ecc69a0430b9f2859a23b88` | c173ed5efb74 |
| 0x153A | `75e0ef21526248ecc2a7a47231abddf59b77d8075917c460a9630ee97cf3ddde` | `77672b5a38f2225e98cdecd7de012850121b6343525392d973e41d39b42719d0` | f735fae6db36 |
| 0x1539 | f36c1f7a0ada876311f5ad5d600f4cb44523165748e96c25bed160a66388ea64 | 22db7b9128e2a12149e5c4f44bc3cb5c22338b40b4e888e97fe9c1a3f571a6b8 | b4bd8b04a001 |
| 0x1538 | 0c24ecf6ff55bc170ce6d7acd1ee4f8afbaf97d73b36e0b7a27a92639d1dbbbd | 2fc7ea5b2fc925157c02f3f492ee3ac17b6154d7ed27e2c20c2d26e855ac1087 | ce943bfe8c4b |

`SHA256SUMS` on the release page lists the same values. Check before you write anything:

```
sha256sum -c SHA256SUMS --ignore-missing
```

Atlan4's 0x1532 is not attached; it is in his repository (`Verzia 1.xxx/FPGA for 1.xxx/FPGA AL3/FPGA.bin`, sha256
`0d4c829c231b3bbe18c6dd6e815523b6e168f787561376aae502db6c1f370fd2`, padded 1 MiB image
`8a9d3559566fbe9653ad52612818d336ce7f905498f422f228bb165335c42177`).

## Provenance

The images are built from `fpga/al3/src` at the source commit in the table, with Anlogic TD 4.6.8 SP1
(V4.6.116866) and the headless flow `fpga/al3/build.sh` (`fpga/al3/TD_SETUP.md`). The flow is deterministic: it
rebuilds Atlan4's shipped 0x1532 byte for byte, and `SRC_REV=<commit> ./build.sh` reproduces each image here
(0x1533 to 0x1537 rebuilt byte-identical from their source commits on 2026-10-19).
TD needs a vendor download and a licence, so CI cannot build them; the images are committed under
`fpga/al3/dist/` and `tools/bitstreams.py check` (run by CI) verifies their size, header and hashes against
`manifest.json`. The source is GPL-3.0 like the rest of the repository; it derives from Atlan4's AL3 project,
which he publishes under GPL-3.0. Design notes, testbenches and bench results per version: `fpga/al3/README.md`
section "Changes".

## How to flash (Sipeed FT2232D and `spiflash_d2xx.py`)

This is the route used on the owner's unit for every write above (`docs/hardware-status.md`). Any 3.3 V SPI
programmer works; `docs/hardware-prep.md` section 2 covers the CH341A (needs a 3.3 V mod), flashrom and the pyftdi
script. Run the tool from a checkout of the release tag: it writes its backups to `backups/` in that checkout.

```
git clone --branch vX.Y.Z https://github.com/mikljohansson/fnirsi-1013d-mcp
cd fnirsi-1013d-mcp
python -m pip install --user ftd2xx
python firmware/tools/spiflash_d2xx.py wiring
```

1. **Find the header pinout on your board by continuity** before you connect anything (`docs/hardware-prep.md`
   section 1.3). On the owner's unit, numbering pin 1 top-left: 1 /CS, 2 DO (about 34 ohm in series), 3 CLK, 4 DI,
   5 VCC, 6 GND. That differs from the reconstructed schematic, so measure yours.
2. **Wire** (3.3 V logic only): TMS to /CS, TDO to DO, TCK to CLK, TDI to DI, GND to GND. Leave the header's VCC
   and the debugger's 3.3 V and 5 V pins unconnected. Scope on its battery, USB unplugged. The header comes without
   pins; [this photo](../../../docs/photos/fpga-flash-header.jpg) shows it with pins fitted, soldered from the back of
   the board, beside the 8-pin flash chip.
3. **Probe**: `python firmware/tools/spiflash_d2xx.py probe` must print the JEDEC ID `ef 40 14` (W25Q80DV;
   `5e 60 14` for a ZB25VQ80). 00 or FF means wiring: stop.
4. **Back up, twice**, and keep the file somewhere safe. This is your only way back:

   ```
   python firmware/tools/spiflash_d2xx.py read -o backups/fpga-flash-before.bin --twice
   ```

   The two reads must agree. The first time, also check the header: 32 bytes of `ff`, then `cc 55 aa 33` at offset
   32; a stock unit's payload ends at byte 282,866 and reports 0x1432.
5. **Write** the padded image. The tool asks you to type the JEDEC ID, saves another double-read backup to
   `backups/<timestamp>-pre-write.bin`, erases and programs only what changes, and reads the whole chip back:

   ```
   python firmware/tools/spiflash_d2xx.py write -i fnirsi_1013d-fpga-al3-<version>-flash1m.bin --yes
   python firmware/tools/spiflash_d2xx.py verify -i fnirsi_1013d-fpga-al3-<version>-flash1m.bin
   ```

   On any error do not power-cycle the scope: write the backup from step 4 back first.
6. **Disconnect the programmer and power-cycle.** The firmware's Diagnostics page (Menu > Diagnostics, Status) shows
   the FPGA version and its features; over USB, `#VER` answers with `fpga=0x15xx`. A bootloader older than v0.8 hangs
   on any custom bitstream; the SD package of this release has v0.8.
7. **Calibrate** (see above).

## Rollback

Write the backup from step 4 the same way, then verify:

```
python firmware/tools/spiflash_d2xx.py write -i backups/fpga-flash-before.bin --yes
python firmware/tools/spiflash_d2xx.py verify -i backups/fpga-flash-before.bin
```

Any other image in the table is also a valid target, and the firmware runs on all of them. To go back to the stock
firmware completely, **restore the stock bitstream first**, then boot the stock firmware (remove the card, or boot
mode 1 in the boot menu). The stock firmware with a custom bitstream is not a valid combination
(`docs/hardware-prep.md` section 5.4). The scope cannot restore its FPGA flash by itself: the config flash is wired
only to the FPGA (`docs/fpga-flash-from-mcu.md`).
