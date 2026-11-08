# FPGA bitstreams for the FNIRSI 1013D, AL3A10 variant

These images are for the **Anlogic AL3A10** board only (unmarked 144-pin LQFP FPGA next to a Winbond 25Q80 config
flash with a 2x3 header, JTAG IDCODE 0x18006C31). Each one starts with 32 bytes of 0xFF, then the sync word
`cc55aa33` and the device-id packet `f0000006 18006c31`. Do not write them to an EF2L45 or EP4CE6 board: those
variants configure their FPGA differently (`docs/hardware-prep.md` section 3a).

Each firmware version supports one bitstream. The entry marked **recommended** here and in `manifest.json` is the one
the current firmware needs; on any other it shows a banner that names the one needed. The other entries are history:
**supported** ones were benched good with the firmware named in their row (and are a valid target for going back to
that firmware), **unbenched** ones were built and simulated but never flashed, **withdrawn** ones failed on the bench.
A release attaches only the recommended image; the others are in `fpga/al3/dist` in the repository. Flashing the FPGA
is a step of the install (`docs/guide/install.md`); it needs a 3.3 V SPI programmer on the config-flash header and a
verified backup first.

## Which one to use

| Version | Status | Adds (on top of the row above) | Firmware that uses it | Bench |
|---|---|---|---|---|
| 0x1532 | Atlan4's, not ours | 16,384 samples per channel, programmable pretrigger, the 32-bit DDS square-wave generator on the calibration pin | Atlan4's firmware; this project's up to 0.2.102 | Atlan4's release; on the maintainer's unit 2026-10-18 |
| 0x1533 | supported | external trigger input on the calibration pin | 0.2.9 to 0.2.102 | flashed 2026-10-18; EXT loopback 10 of 10 captures triggered |
| 0x1534 | supported | decimation filter before the decimator: boxcar, CIC2, CIC3; 16-bit samples | 0.2.30 to 0.2.102 | flashed 2026-10-18; decimation bench |
| 0x1535 | supported | compensation FIR after the CIC (boxcar+FIR, CIC2+FIR, CIC3+FIR at 10 MSa/s and slower) | 0.2.44 to 0.2.102 | flashed 2026-10-19; alias of a 892.857 kHz square at 1 MSa/s -76.1 dBV with CIC2+FIR |
| 0x1536 | unbenched | segmented capture and hardware averaging (sequencer in front of the 0x1535 capture logic) | 0.2.58 to 0.2.102 | built and simulated, never flashed |
| 0x1537 | withdrawn | deterministic record start and A/B order at divided rates | 0.2.58 to 0.2.102 | flashed 2026-10-19: CH2 core A garbled in about 40 % of full-rate captures |
| 0x1538 | withdrawn | stateless ADC encode: core order B-first guaranteed at every rate and after any write; self-check read 0x33 (0xF6 = good); fixes the encode-phase race of 0x1532-0x1536 and the marginal flop of 0x1537 | 0.2.58 to 0.2.102 (0.2.62 and later read 0x33) | flashed 2026-10-19 17:46: encode clean, but the MCU readout mixes bytes near multi-bit code boundaries; superseded by 0x1539 |
| 0x1539 | withdrawn | MCU readout byte loaded on the strobe start and held; fixes the bit mixing of 0x1537/0x1538 near multi-bit code boundaries; i_mcu_clk constrained | 0.2.62 to 0.2.102 | flashed 2026-10-19 20:06: CAL_PROBE passes, but both A cores still garbled at means 128..145 at 200 MS/s (the ADC data capture window, not the readout); superseded by 0x153A |
| 0x153A | supported | ADC data captured in IOB registers at a selectable stage per core against the edge the core is encoded on, B first by T/2 at every rate; capture self-check (read 0x36) and training registers 0x63 / 0x64; fixes the garbled cores near codes 128..145 of 0x1537-0x1539 | 0.2.81 to 0.2.102 (the wizard trains the capture stages; the power-on stages can be wrong per unit) | 2026-10-20: wizard trained 0x08E3, band sweep clean at all four rates, self-check 0 flagged of 18865 captures (docs/hardware-status.md 10:04-10:49); superseded by 0x153B at 20:42 (untriggered auto-mode records could be spliced) |
| 0x153B | supported (recommended 2026-10-20 to 2026-10-22) | records that end on the auto time-out or the firmware's forced stop report 0x14 at the write pointer (W - total + pretrigger), so untriggered records are the newest total words instead of ring 0 .. total - 1 spliced at the stop; the time-out no longer cuts a triggered record; no trigger after the stop (0x18 is the record's) | 0.2.97 to 0.2.103 (0.2.103 runs on 0x153B only; 0.2.104 and later show the banner) | flashed 2026-10-20 19:31 and verified; benched 20:40-20:49 on fw 0.2.102 with the probes on the post: 0 of 100 untriggered captures spliced (50 at 1 MSa/s, 50 at 200 MSa/s), 50 of the Scope view's auto-mode traces contiguous, ten Frequency response sweeps 100 Hz..1 MHz without a retake or a flagged point (README section 11) |
| 0x153C | supported | trigger types (pulse width, Nth edge, timeout on CH1 / CH2 / EXT) and the trigger hold-off (0x1B); peak-detect acquisition (0x56 bit 1); hardware counter with min / max (0x61, 0x62, read 0x35); the stock design's unused registers gone (write 0x3C / read 0x41, reads 0x24..0x27, write 0x0F, 0x53 bits 4..2) and the sample-rate divider taken on 20 bits | 0.2.104 only (0.2.105 and later show the banner) | built 2026-10-21 (fpga-c4): clk_200MHz 0 errors at 4.772 ns, tb/run.sh 16 steps ALL PASS on seeds 12345 and 4242; flashed 2026-10-21 17:35 and benched on fw 0.2.104: pulse width, timeout, hold-off, peak detect and Nth edge pass; the counter is exact with the ADCs at full rate (1 kHz to 1 MHz) and not valid with Filter Off at the divided rates; 2026-10-22 11:36, CH2 code errors (Filter Off): 0 % at 2 MSa/s, core A 1.01 % / B 0.86 % at 200 MSa/s |
| 0x153D | supported (recommended 2026-10-22 to 2026-11-04) | the streaming logger (0x69 / 0x6A, read 0x38: the sample ring free-runs as a FIFO the firmware drains over USB, LOG_START / LOG_READ / LOG_STOP) and an accurate fractional square generator in place of the DDS (0x6C entries 0 / 1, 0x6D, read 0x39: f = 200 MHz x 65536 / P16, every edge within 5 ns of ideal, glitch-free frequency changes; 0x51 / 0x52 retired); the ADC encodes as on 0x153C (gated by the divider with the filter off at slower rates, full rate with a filter, peak detect and at 200 MSa/s). The pulse pattern generator of the fpga-d2 branch was cut by the owner (86 % of the logic cells with the square, 96 % with the pattern) | 0.2.105 only (release 0.3.0 shows the banner) | built 2026-10-22 (fpga-d3gate): clk_200MHz 0 errors at 4.983 ns, every clock closed, 6429 LUT / 7449 LE (86.2 %), encode pads and capture margins equal to 0x153C's build within 0.03 ns; tb/run.sh (18 steps) ALL PASS on seeds 12345 and 4242. Flashed 2026-10-22 17:01 and benched on fw 0.2.105 (docs/hardware-status.md 17:03-19:52): the wizard trained CH1 A3 B4, CH2 A3 B3; CH2 code errors (Filter Off, 1 kHz square through an RC ladder, samples 4+ codes off a 3-sample median): 0 % on both cores at 2 MSa/s, as 0x153C; at 200 MSa/s core B 0.37 %, core A 3.32 %, concentrated just below the 0x5F/0x60 code carry where the square's low level sat (12.0 % in codes 0x58..0x5F, at most 0.29 % in any other 8-code band); accepted by the owner for 0.3.0 at 18:56; on the 1 kHz square the counter reads 999.996 Hz at 200 MSa/s and 1000.004 Hz with a filter at 500 kSa/s, and reports GATED (not valid) with Filter Off at the divided rates; 301 s soak 0 errors. The logger's three rate tiers (0 words lost) and roll mode were benched on the first 0x153D build (`0e2cdbce...150e`, 2026-10-22 08:47-09:02), which encoded the ADCs at full rate at every rate and was replaced after it showed 1-3 % CH2 code errors at every timebase (README section 17); this build keeps its logger and square logic unchanged |
| 0x153E | supported (recommended 2026-11-06 to 2026-11-07, never in a release) | the four ADC encode pins from IOB output registers on two new PLL outputs (B on C2, A 375 ps later on C3), so the encode timing is the same in every build (B 6.97 / A 7.34 ns at the pins in TD's model, where 0x153D's moved 6.9 .. 7.9 ns from build to build); every register and everything else as 0x153D | only the unnumbered development builds after 0.2.105 that expect 0x153E; no released firmware (release 0.3.0 shows the banner) | built 2026-11-04 (fpga-153e): every clock 0 errors, clk_200MHz 4.802 ns, 7454 LE (86.3 %); tb/run.sh (18 steps) on seeds 12345 and 4242: ALL PASS on both seeds, every step, no exception. The same logic with the 0x153D version word (the encode-phase image enc0) was benched on 2026-11-04 on fw 0.2.105 (docs/hardware-status.md 10:23-10:38): clean boot, encode self-check 0 bad, the wizard trained A3 on both channels, CH2 code errors in the release's range (fpga/al3/README.md section 19). The numbered image: flashed 2026-11-06 14:00-14:04 (pre-write = the 0x153D padded image aa252fb7..; the first pass failed its verify with one lost sector erase and one wrong byte, the second pass verified, independent double read back 2782e1b6..); benched 14:05-15:50 on fw 0.2.105 b278bc0c (main e242b53, built for 0x153E; docs/hardware-status.md): no mismatch notice; the wizard with the inputs open trained CH1 A3 B3, CH2 A3 B3 (0x06DB) cold and again warm; encode self-check 0 bad of 110232, capture self-check 0 flagged of 107674; CH2 code errors (Filter Off, 1 kHz square from the post through an RC ladder, samples 4+ codes above a 3-sample median): 0 % on both cores at 2 MSa/s; at 200 MSa/s the peak over the CH2 offset sweep (tools/enc_sweep.py: the square's low level moved across the 0x5F/0x60 carry in 0.25-code steps) A 5.17 % / 5.12 %, B 0.61 % / 0.65 % with the warm calibration 80 min after power-on (0x153D on 2026-11-04: 6.94 % / 0.66 %), and A 15.1 % / B 2.7 % 10 min after power-on: the share falls about 2.5 times as the unit warms up, so the sweep counts only 30 min or more after power-on; counter on CH1 1 kHz 1000.002 Hz valid at 200 MSa/s, GATED (not valid, by design) with Filter Off at 500 kSa/s; logger 1 ch RAW8 500 kS/s, 1 ch 16-bit 250 kS/s, 2 ch 16-bit 125 kS/s, 5 s each, 0 words lost; fw persisted to the card 15:23 and booted from it 15:49 (no notice, the trained stages in effect); accepted by the owner for 0.3.0 at 15:1x; recommended since 2026-11-06 |
| 0x153F | **recommended** since 2026-11-07: benched on the maintainer's unit | both ADC cores of a chip captured at one instant, so with Filter Off core B is half a word before core A at every rate and stage (0x153A to 0x153E took each core against its own encode edge and stored core B one conversion late at most rates on the maintainer's unit, whose ADCs release both buses after the B encode); one capture stage per chip (0x63 bits 2:0 / 8:6), used at 200 MSa/s and with the filters, none at the divided rates; everything else as 0x153E | release 0.3.0 | built 2026-11-06 (fpga-153f, roll q10 ea38475: every clock 0 errors, clk_200MHz 4.789 ns, 6401 LUT / 5673 reg / 7272 LE = 84.2 %, #ireg 33, #oreg 15, encode pads B 6.97 / A 7.34 ns on both chips as on 0x153E; a fresh rebuild of the committed source gives the same bytes); `tb/run.sh` (18 steps, `REF_SRC_DIR` with `da645d0.v` added) on seeds 12345 and 4242 (2026-11-07, the review of fpga-153f, snapshots of the committed `src` + `tb`): **ALL PASS on both seeds, every step, no exception** (12345 / 4242: ext 39; decim 107 + cic_model 1868 / 1882 samples; fir 168 + cic_model 1575 / 1610 samples; long 8; seg 54 / 56 + seg_model 22 runs 0 failures; trigpos 1252 (37 cases); contract 42 and the transcript = `contract_0x153F.txt`; encphase PASS (the late-CH2-A annotation fails through 0x33 as it must); mcuread 70 + 26; adccap 35952 strict + 6298 acceptance, STRICT ORDER PASS on the design and FAIL on da645d0; firstrec 38; timeout 85; trigtypes 1293; peak 45; counter 115; stream 757; square 14102). The author's runs of 2026-11-06/07 on the same RTL had one failing step per seed (tb_seg 12345, tb_fir 4242); the review traced both to the testbenches and fixed them there (a stimulus process that had inherited the write clock's 1 ps simulation delay, and the 0x1534 reference's swap bit written in a compared phase; README section 22 "Tests" and the section 20 addendum), the bitstream unchanged; not flashed. Flashed 2026-11-07 and benched on the firmware built for it (docs/hardware-status.md 09:43-11:33): the wizard trained stage 3 on both chips; the designed core delay at every rate tested from 200 MSa/s to 200 kSa/s on both channels; 1000.00 Hz on the hardware counter at 20 kSa/s Filter Off; the hardware battery 201 of 202 steps passed and 1 skipped; CH2 code errors 0 % at 2 MSa/s and, 80 minutes after power-on, a peak over the offset sweep of A 5.8 %, B 1.0 % at 200 MSa/s (0x153E: A 5.2 %, B 0.65 %), accepted by the owner for the right core order |

**Use the recommended image.** Release 0.3.0 runs only with 0x153F, firmware 0.2.105 only with 0x153D; on any other
bitstream, older or newer, they show the mismatch banner and do not acquire. The supported rows are kept in
`fpga/al3/dist` in the repository for going back to the firmware named in their row, or to a state someone reported
against; only the recommended image is attached to a release. 0x1532 to 0x1536 share an ADC encode-phase race: after a
sample-rate change one core of a channel can deliver garbled 200 MSa/s data. Firmware 0.2.61 to 0.2.102 checked every
full-rate capture on them after a rate change and retried, and showed their FPGA line in amber in Diagnostics
(`CHANGELOG.md` 0.2.61; `fpga/al3/README.md` "Changes" section 7).

The firmware of this release runs with the recommended row only.

**After changing the bitstream, calibrate again.** The calibration stores per-core values, and the core order can
differ between bitstreams. Diagnostics shows "FPGA changed since calibration" until you run the wizard
(Menu > Diagnostics > Calibrate; `firmware/README.md` section 6.4).

## Files and hashes

A release attaches the recommended version only; every version is in `fpga/al3/dist` as `al3-<version>.bin` (raw):

- `fnirsi_1013d-fpga-al3-<version>.bin`: the bitstream as TD's bitgen wrote it, 282,898 bytes.
- `fnirsi_1013d-fpga-al3-<version>-flash1m.bin`: the same, padded with 0xFF to 1,048,576 bytes, the size of the
  25Q80. Write this one; it is what the maintainer's unit was flashed with.

| Version | `.bin` sha256 | `-flash1m.bin` sha256 | Source commit |
|---|---|---|---|
| 0x1533 | `a96567031872ae99bd40f0722a111a08da0f6381b6c710b70998af784a341216` | `96159a871de3af57c2174fe1ec66ed14b10fbe2e845c44564e7bda9ed252acae` | 526c969 |
| 0x1534 | `83140510dfbfef56cc5ed15d70866e6bc52c407a41fd0ea480e8993df5b820c1` | `c387a15ed11d209deab639d1de84dd3117419d800aed05e032b192650e13c0a0` | 6d4c8ad |
| 0x1535 | `5040a2aeede071019bca0ffee47d765b87d980e1e47ca01ac32cb875c839aa96` | `5ade9e755211fe65c6ff492419f3a4a91ec08edc8c16305ce52751baf9d588a5` | e2aec2e |
| 0x1536 | `4eaf6811814c58e1f00e648b97e7a608bb5ca387fb036f21714e91667959b48b` | `66866c84ad4f298c88cfc1072870d44e80832852b53fcbd926e01fcb473f7408` | 27c0c75 |
| 0x1537 | `426a3784883b82c9c9f42870584e64d1d082cdcb38c8b62db143e161e2633b17` | `febc92997ee6605090d5581aabc9c240986fa82defdc24d40aca01566dbfe1f1` | c15981e |
| 0x1538 | `0c24ecf6ff55bc170ce6d7acd1ee4f8afbaf97d73b36e0b7a27a92639d1dbbbd` | `2fc7ea5b2fc925157c02f3f492ee3ac17b6154d7ed27e2c20c2d26e855ac1087` | ce943bfe8c4b |
| 0x1539 | `f36c1f7a0ada876311f5ad5d600f4cb44523165748e96c25bed160a66388ea64` | `22db7b9128e2a12149e5c4f44bc3cb5c22338b40b4e888e97fe9c1a3f571a6b8` | b4bd8b04a001 |
| 0x153A | `75e0ef21526248ecc2a7a47231abddf59b77d8075917c460a9630ee97cf3ddde` | `77672b5a38f2225e98cdecd7de012850121b6343525392d973e41d39b42719d0` | f735fae6db36 |
| 0x153B | `fdc3c21b57341b0774e8a5e58df04e38d1d223fb7c354b6ed49f5892f711fc3d` | `1391978c33379271789a9405f7b0fb9395a85e223ecc69a0430b9f2859a23b88` | c173ed5efb74 |
| 0x153C | `2580b090db3dd16f5609ddcb0d27576f9dfe6a4651dbafa85000d807a6bec7f2` | `07393bf5a728542b7de0738ca8aab7ccbf3b00e7267ca1efd8e66f8c2d361f77` | bd24bdfe0c8e |
| 0x153D | `fd0be7a2c715e3106b1501986640cdf64a1c8af64c534502701e5d836d32e96a` | `aa252fb7e94dca01f064a69d79d813d974024ab77763917a6f5dd9051a1e63cc` | e3a23c29dbec |
| 0x153E | `831dc3465e7bdddf67bfd263755f00f7fc95fcd5e487a9ca52ec49e2531602e1` | `2782e1b6246d6f1bf7603a2d011ff550e01d47f71953ccce4b6c25069f74d9d8` | da645d055a5a |
| 0x153F | `e581a67fcf9d8a21ba01b212212ec17d92c9f1d98e5c3ad082cd79f8d930ece0` | `d255ca3824786d80d507c94e7c21b2497e035c98753b5db1d2f395ab5052d5bc` | ea3847526567 |

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

This is the FPGA step of the install (`docs/guide/install.md`). It is the route used on the maintainer's unit for
every write above (`docs/hardware-status.md`). Any 3.3 V SPI programmer works; `docs/hardware-prep.md` section 2
covers the CH341A (needs a 3.3 V mod), flashrom and the pyftdi script. Run the tool from a checkout of the release
tag: it writes its backups to `backups/` in that checkout.

```
git clone --branch vX.Y.Z https://github.com/mikljohansson/fnirsi-1013d-mcp
cd fnirsi-1013d-mcp
python -m pip install --user ftd2xx
python firmware/tools/spiflash_d2xx.py wiring
```

1. **Find the header pinout on your board by continuity** before you connect anything (`docs/hardware-prep.md`
   section 1.3). On the maintainer's unit, numbering pin 1 top-left: 1 /CS, 2 DO (about 34 ohm in series), 3 CLK, 4 DI,
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

Any other image in the table is also a valid target for the programmer, but the firmware shows its banner on all of
them. To go back to the stock firmware completely, **restore the stock bitstream first**, then boot the stock firmware
(remove the card, or boot mode 1 in the boot menu). The stock firmware with a custom bitstream is not a valid
combination (`docs/hardware-prep.md` section 5.4). The scope cannot restore its FPGA flash by itself: the config flash is wired
only to the FPGA (`docs/fpga-flash-from-mcu.md`).
