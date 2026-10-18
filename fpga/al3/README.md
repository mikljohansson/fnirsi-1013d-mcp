# fpga/al3 - Anlogic AL3A10LG144C7 project

Status 2026-10-18: **TD 4.6.8 SP1 runs headless in WSL and rebuilds Atlan4's design byte-identical to his shipped
`.bin`** (see "Rebuild results", setup in `TD_SETUP.md`). `src/` now carries our first change, the external trigger
on the calibration pin (version word 0x1533, section "Changes" at the end). Atlan4's original stays buildable with
`SRC_REV=a947e78`. Nothing has been flashed from this folder.

## Contents

| Path | What |
|---|---|
| `src/` | Atlan4's AL3 TD project, sources only: `zaklad.v` (top `fnirsi_1013D`), `zaklad.adc`, `zaklad.sdc`, `FPGA.al`, `al_ip/` (PLL and sample-memory IP: `.v`, `.ipc`, sim models), plus `FPGA commands.txt` (Atlan4's command table, updated). Copied from `D:\src\Fnirsi1013D\Verzia 1.xxx\FPGA for 1.xxx\FPGA AL3\FPGA AL3S10\` (unmodified at commit a947e78, version 0x1532). `zaklad.v` and `zaklad.adc` now carry the changes listed in "Changes" (version 0x1533). Build outputs (`*.db`, `*.area`, `*.timing`, `*.bit`, `*.bin`) were left out. |
| `tb/` | iverilog testbench: `run.sh`, `tb_ext_trigger.v`, `sim_models.v` (behavioural PLL and sample memory). See "Changes". |
| `out/` | Named bitstreams for the bench, gitignored (`*.bin`). Hashes are recorded in "Changes". |
| `reference-stock/` | pcprogrammer (pecostm32) clean-room re-implementation of the **stock** FNIRSI design (version 0x1432), TD 5.0.28716 project. Sources only (`source/`, `al_ip/`, `Original_1013D.al`); his prebuilt `.bit` was left out. Licence GPL-3.0 (`LICENSE-GPL3`, repo `pecostm32/Anlogic_AL3-10_Analyzing`). |
| `bitstreams/` | Prebuilt images, on disk only, with provenance and sha256 in `bitstreams/README.md`. Git ignores everything there except that README. |
| `build.sh`, `build.tcl` | Headless TD 4.6.8 flow for `src/` (replay of Atlan4's GUI command sequence). Verified, see "Rebuild results". `SRC_REV=<git rev>` builds `src/` as of that commit. |
| `build_donwulff.tcl` | Donwulff's `read_verilog`/`map`/`pack` flow for TD 5.x (`FLOW=donwulff`). Crashes in `report_io` on 4.6.8. |
| `td/` | `td_install.sh` (headless install), `td_fetch.sh` (Sipeed captcha download helper), `compare_bin.py`. |
| `TD_SETUP.md` | Replayable install and build steps with hashes. |
| `license/` | Personal `Anlogic.lic`, gitignored. |

`src/FPGA.al` was written by TD 4.6.116866 and sets no effort options. I found no licence file in Atlan4's tree, so treat
`src/` as upstream material that stays attributed to him.

## Toolchain survey

### 1. Vendor TD (Tang Dynasty)

What I verified online (2026-10-18):

- **Verified route (owner, 2026-10-18, took a few minutes):** create an account at anlogic.com, then
  Service Support -> Tools & Materials Download -> Software Tools -> **Licence** (download `license.zip`, it contains
  `Anlogic.lic`, valid about a year, `HOST_ID=ANY`), and Software Tools -> **TD Linux -> TD_4.6 ->
  `TD_4.6.8_SP1_116866_NL.zip`** (the exact version Atlan4's project was made with; the newer 5.x/6.x builds sat
  behind a manual approval). Put `Anlogic.lic` in `fpga/al3/license/` (gitignored) and run `td/td_install.sh`.
- `anlogic.com/en/support/tools-downloads` lists TD for Windows and Linux in versions 4.6, 5.6, 6.0 and 6.2.
  Downloads need an account with a completed profile. The page says to email `web@anlogic.com` with your
  requirements and registered phone number to request permissions. A "License" category exists, but I could not see
  its contents without logging in.
- The licence is a text file `Anlogic.lic` that goes into `<TD>/license/`. A Sipeed wiki issue (#690, July 2024)
  describes the working route: log in at anlogic.com, open tools-downloads, "TD License", download `license.zip`.
  The file in that report was valid for roughly six months (expiry 2025-02-01), so licences are short-lived and need renewal.
- A Chinese FAQ quoted in search results says the same: TD License sits under 工具与资料下载 -> 软件工具 -> TD License.
  A comparison article claims TD is free for small parts (EF3L40 and below). I did not confirm what applies to AL3.
  Treat "free" as probable but unverified.
- Node-locking: Donwulff's community file reads "NODELOCKED license for TD" but has `HOST_ID=ANY`, so it works on any
  machine. Whether an official file is bound to a host id is unknown; the Sipeed report does not mention one.
- Registration-free mirror: Sipeed's download station (`dl.sipeed.com/shareURL/TANG/Primer/IDE`, plus MEGA and Baidu links)
  carries TD 5.0.3 Linux (build 28716, 414 MB) and 5.0.4 Windows MSI (build 27252, 467 MB), per Donwulff. Large files are
  captcha-gated. I did not download them.
- Which TD supports the AL3A10: TD 4.6.8 SP1 build 116866 (Atlan4's choice; no public mirror) and TD 5.0.28716
  (pecostm32, Donwulff). Donwulff read TD 5.0.3's `arch/` and found `al3_10.db`, `al3_s10.db`, `al3_6.db` plus
  `al3_macro.v`, and built the vendored Atlan4 project end to end with 5.0.3 (Linux) and 5.0.4 (Windows).
  **Unverified:** whether TD 5.6 or 6.x still ship AL3 databases. AL3 is an old family; do not assume it.

Donwulff's claims, checked:

| Claim | Result |
|---|---|
| TD 5.0.3 Linux and 5.0.4 Windows are on the Sipeed mirror without registration | Consistent with the Sipeed wiki and the prjtang README, which both point at Sipeed's mirror. Not downloaded by me. |
| Anlogic's own 4.6/5.6/6.0 downloads are login-gated | Confirmed on the Anlogic page. |
| Community licence expires 2026-08-31 | Plausible; short expiry is normal (see above). Today is 2026-10-18, so it is dead either way. |
| Fresh licence via Anlogic customer service or a newer Sipeed file | Anlogic's route is the TD License page after login, plus `web@anlogic.com` for permissions. The Sipeed copy was stale in 2024, so do not count on it. |
| TD runs headless on Linux with two empty stub GTK libs | Cannot verify without TD. Plausible, and he reports a full validated build. |

### 2. Open-source toolchain: not usable for AL3A10

- `mmicko/prjtang`: its `devices.json` does contain `al3_6` and `al3_10` (AL3A10LG144C7, idcode 0x18006c31, 1075 frames of
  2056 bits) besides EG4 (Eagle), EF1 and EF2. It is a bitstream-format database and fuzzer toolkit. The README sections
  "Current Status" and "Development Boards" are empty. Last commit 2022-12-25. Building its database needs a TD install.
- `nextpnr`: upstream has no Anlogic architecture. Its himbaechel micro-architectures are gowin, gatemate, ng-ultra,
  xilinx and an example. mmicko's fork has a stale `gowin_anlogic` branch and nothing more. No place and route exists for Anlogic.
- `yosys`: `synth_anlogic` exists but targets Eagle (EG4) cells (`eagle_bb.v`, `cells_sim.v`). The AL3 uses different
  primitives (`AL_LOGIC_*` in TD's `al3_macro.v`). No AL3 mapping.
- pecostm32's `Anlogic_AL3-10_Analyzing` got as far as turning the stock bitstream into a gate-level netlist and Verilog.
  That is reverse engineering, not synthesis. His README says the project stalled on interconnect naming.

Conclusion: you cannot synthesize, place and route, or generate a bitstream for the AL3A10 with open tools today.
Donwulff's note that no open flow exists is correct.

### 3. Programming the config flash

The FPGA loads from a Winbond 25Q80 (1 MiB) that has a 2x3 header beside it. The project path is an external SPI
programmer plus `flashrom` (CH341A with a 3.3 V level shifter per Atlan4's readme; Bus Pirate or J-Link per Donwulff's
bench notes, not ours). The `.bin` from TD's `bitgen -bin` is the raw flash image. openFPGALoader is not needed:

- Its part table knows Eagle D20 and S20 (0x04014c35, 0x0a014c35) and EF2M45 (0x00004c37). I found no entry for the AL3A10
  id 0x18006c31, so JTAG detection would likely report an unknown device.
- Its documented Anlogic support is the Lichee Tang board (bit file to SRAM or SPI flash). "Anlogic cable" support is on
  its to-do list. Raw writes at a flash offset exist (`-o`), but only through cables it supports.
- TD's own JTAG programmer works (Donwulff: capped at 400 kbit/s) but needs an Anlogic-compatible USB-JTAG adapter.
  Atlan4 ships an STM32F103 clone of `anlogic-usbjtag` for EF2. The owner's unit reads JTAG IDCODE 0x18006C31.

### Recommendation

1. **Primary: TD 5.0.3 Linux under WSL Ubuntu with Donwulff's headless flow** (`build.sh` here). Agents can build without a
   GUI, the flow has a published end-to-end validation on this exact AL3 project, and WSL Ubuntu is already the project build host.
2. **Fallback: TD 5.0.4 Windows MSI** on this PC, GUI project `src/FPGA.al`. Donwulff also built the project this way.
   Batch use of the Windows `td.exe` with a Tcl script is plausible but I did not verify it.
3. Open tools: no.

Both options need a valid licence file, which is the only real blocker. Atlan4's exact TD 4.6.8 SP1 is not required:
Donwulff's TD 5.0.3 rebuild met timing on `clk_200MHz` where Atlan4's shipped 4.6.8 report did not, and produced a
same-size 282,898-byte `.bin`. Expect the `.bit` to differ by a few bytes, and the ADC encode skew to be build-dependent.

### What the owner must do personally (licence)

1. Register a personal account at `https://www.anlogic.com` (English site, Support -> Tools & Downloads) and complete the
   profile. Registration uses the owner's own contact details, so agents must not do it.
2. In Tools & Downloads open the "TD License" entry and download `license.zip`. Read the expiry date in the `.lic`
   text (the `FEATURE` line) and the `HOST_ID` line.
3. If the licence or the TD 5.x/6.x downloads show "permission is insufficient", email `web@anlogic.com` with the use case
   (hobby work on an AL3A10LG144C7, TD 5.0.x for Windows or Linux) and the registered phone number.
4. Optionally download TD 5.0.3 Linux and 5.0.4 Windows from the Sipeed station
   (`dl.sipeed.com/shareURL/TANG/Primer/IDE`); that needs no account.
5. Hand over the TD zip or MSI and the `.lic`; it goes to `<TD>/license/Anlogic.lic`. Note the expiry and renew in time.

Open question to settle with Anlogic: whether the licence covers AL3 devices, and for how long.

## Headless build flow (TD 5.0.3 draft; the working 4.6.8 flow is in TD_SETUP.md)

One-time setup in WSL Ubuntu (`wsl -d Ubuntu`), following Donwulff's recipe in `D:\src\FNIRSI_1014D_Firmware\FPGA_NOTES.md`:

```bash
mkdir -p ~/tools/anlogic-td && cd ~/tools/anlogic-td
unzip TD_5.0.3_28716_NL_Linux.zip                 # gives TD_5.0.3_28716_NL/
cp /path/to/Anlogic.lic TD_5.0.3_28716_NL/license/Anlogic.lic
# Stub GTK2 libs: empty shared objects whose sonames match what bin/td links.
mkdir -p shim && cd shim
echo 'void __stub(void){}' > stub.c
for so in libgtk-x11-2.0.so.0 libgdk-x11-2.0.so.0; do
  gcc -shared -fPIC -Wl,-soname,$so -o $so stub.c
done
# Check what td really wants and adjust the names if this list differs:
ldd ~/tools/anlogic-td/TD_5.0.3_28716_NL/bin/td | grep -i 'not found'
```

Build, from `/mnt/d/src/fnirsi-1013d-ai/fpga/al3` (use a WSL-local build directory, `/mnt/d` is slow):

```bash
TD_HOME=~/tools/anlogic-td/TD_5.0.3_28716_NL TD_SHIM=~/tools/anlogic-td/shim ./build.sh ~/al3-build
```

`build.tcl` runs `import_device al3_10.db`, `read_verilog -lib $TD/arch/al3_macro.v -file all.v -top fnirsi_1013D`,
`read_adc`, `optimize_rtl`, `read_sdc`, `map_macro`, `map`, `pack`, `place`, `route`, three reports, and
`bitgen -bit FPGA.bit -bin FPGA.bin`. Quirks from Donwulff: `read_verilog` takes exactly one `-file`, so the sources are
concatenated; the AL primitive models come in through `-lib`; `import_device` without `-package` defaults to LQFP144;
`sh: Bad fd number` log spam is harmless.

Expected on a first build (Donwulff's numbers for the same project): about 600 LUT, 460 registers, 32 BRAM9K, 60 IO;
`.bin` of 282,898 bytes starting with 32 bytes of 0xFF then `cc55aa33 f0000006 18006c31`; `clk_200MHz` timing met;
`clkc[1]` slightly negative (known, structural). Do not flash a first build. Compare it with
`bitstreams/atlan4-FPGA-0x1532.bin` and follow the flashing gates in `CLAUDE.md`.

## Atlan4 `zaklad.v` vs pcprogrammer stock re-implementation

Stock = `reference-stock/source/Original_1013d.v` (reports 0x1432). Atlan = `src/zaklad.v` (reports 0x1532).

**Identical:** top module name and port list; the pin assignments (`zaklad.adc` equals `Original_1013D.adc` apart from
blank lines); 50 MHz crystal to PLL to 200 MHz; the 50 MHz divider; ADC encode generation (negedge of the 200 MHz
clock, A is the complement of B); the 100/(N+1) MHz sample clock rule; offset PWM (25 kHz) and display PWM (LAST 59999);
relay encoding in 0x33/0x36 (`relay1 = bit0`, `relay2 = ~bit1`, `relay3 = ~bit2`, only when value < 6); the command
bus handshake; big-endian multi-byte writes; the meaning of 0x01, 0x0D, 0x0E, 0x15-0x17, 0x1A, 0x32-0x38, 0x3C, 0x41.

| Aspect | Stock re-implementation | Atlan4 AL3 | Migration risk |
|---|---|---|---|
| Version word (cmd 0x06) | 0x1432 | 0x1532 | Bootloader >= 0.8 accepts both. |
| Sample RAM | 4096 x 32, 12-bit address, `9K` | 8192 x 32, 13-bit address, `9K(FAST)` | Firmware must know the depth. Both use 32 BRAM9K. |
| PLL | one output, clk0 200 MHz | clk0 200 MHz and clk1 200 MHz at 90 degrees (`clk_ADC`) | Atlan's ADC encode flops run on the 90-degree clock, 1.25 ns after the write clock; stock has no shift. Sampling-edge alignment differs. Known timing violation on clkc[1]. |
| Pre/post-trigger | Fixed: read counter starts at 800, half-way when write address > 1600 | Programmable: cmd 0x0B pretrigger (default 750), 0x0C total (default 1500); half-way when write address >= pretrigger | New commands that stock firmware never sends. |
| Trigger source | Samples read back from the RAM output (`o_adc1A_d` / `o_adc2A_d`), previous sample stored inverted | Raw ADC input pins (`i_adc1A_d` / `i_adc2A_d`), strict compare, explicit rise and fall | Trigger point may shift by a sample or two. |
| Trigger stop | Stops when triggered; auto mode stops on time-out | Stops when triggered and `addr_delta >= total - pretrigger`; auto time-out forces the trigger address to pretrigger | Different amount of post-trigger data. |
| Hold-off cmd 0x1B | none | stored, never used | No functional change. |
| Read address | `sample_read_enable` (cmd 0x0F) muxes RAM read clock and address between the live trigger counter and the MCU pointer | Mux is degenerate: MCU pointer and `i_mcu_clk` always | Stock firmware's 0x0F sequencing is a no-op in Atlan's design; Donwulff notes roll mode does not work there. |
| Read pointer cmd 0x1F | high byte `[3:0]` into `[11:8]` | `[4:0]` into `[12:8]` | Width follows RAM depth. |
| Trigger address cmd 0x14 | 12 bit | 13 bit | Firmware needs the wider field. |
| Reads 0x24-0x27 | live ADC pins | live ADC pins | Same in these two designs. Donwulff says the original FNIRSI FPGA uses 0x24/0x26 for roll-mode buffer reads; neither design does that. |
| Calibration pin P133 | fixed 1 kHz square wave, always driven | DDS generator: 0x50 enable (pin tri-states when off), 0x51 phase increment, 0x52 duty; default 85899 = 1 kHz | New generator; pin is hi-Z unless enabled (default is enabled). |
| Multi-byte commands (`mcu_state`) | writes 0x0D, 0x0E, 0x1F, 0x32, 0x35, 0x38, 0x3C; reads 0x06, 0x14, 0x41 | adds 0x0B, 0x0C, 0x1B, 0x51, 0x52 | Needed for the new registers. |
| Sample-rate counter | resets to 0, enable when `divider < counter + 1` | resets to 1, enable when `divider < counter` | Same rate. |
| SDC | `clk_200MHz` 5 ns (odd 40 percent duty `{0 2}`) and `i_xtal` 20 ns | adds `set_clock_latency -source 1 i_xtal` and commented generated clocks | Both leave `sample_write_clock`, `clk_50MHz`, `i_mcu_clk` and all I/O unconstrained. |
| Origin | Clean-room from bitstream RE, GPL-3.0, TD 5.0.28716 | Derived from the stock RE (header credits Atlan and PECO), TD 4.6.116866 | Licence of `src/` is not stated. |

Net: a pin-compatible drop-in (same pad map, so the same board works) with different capture semantics. Moving a
stock-firmware unit to Atlan's design means adopting the 0x1532 firmware paths (13-bit addresses, 0x0B/0x0C, 0x50-0x52).
The risky parts are the 90-degree ADC phase and the trigger point. The pin-map comparison was done by diffing the two
constraint files after removing blank lines; the Verilog comparison is from reading both files, not simulation.

## Pin map

Identical in `src/zaklad.adc` and `reference-stock/source/Original_1013D.adc`. All LVCMOS33, LQFP144 pin numbers.
Bus rows list bit 0 first.

| Group | Signal | Pins | Dir | Constraint / note |
|---|---|---|---|---|
| MCU bus | `io_mcu_data[0..7]` | P135, P136, P137, P138, P141, P142, P143, P144 | inout | pull none |
| MCU bus | `i_mcu_clk` / `i_mcu_rws` / `i_mcu_dcs` | P1 / P2 / P3 | in | pull none; rws 1 = MCU writes, dcs 1 = command byte |
| Clock | `i_xtal` (50 MHz) | P23 | in | pull none |
| ADC 1, core A | `i_adc1A_d[0..7]` | P66, P65, P64, P60, P59, P58, P55, P54 | in | pull none |
| ADC 1, core B | `i_adc1B_d[0..7]` | P67, P68, P69, P70, P71, P72, P73, P74 | in | pull none |
| ADC 2, core A | `i_adc2A_d[0..7]` | P91, P90, P89, P88, P87, P86, P85, P84 | in | pull none |
| ADC 2, core B | `i_adc2B_d[0..7]` | P98, P99, P100, P101, P103, P104, P105, P106 | in | pull none |
| ADC 1 encode | `o_adc1_encA` / `o_adc1_encB` | P75 / P53 | out | SLEWRATE FAST |
| ADC 2 encode | `o_adc2_encA` / `o_adc2_encB` | P110 / P83 | out | SLEWRATE FAST |
| Relays ch1 | `o_relay1_1`, `_2`, `_3` | P121, P124, P126 | out | PULLUP |
| Relays ch2 | `o_relay2_1`, `_2`, `_3` | P128, P125, P127 | out | PULLUP |
| AC/DC relays | `o_ac_dc_1` / `o_ac_dc_2` | P129 / P132 | out | none |
| Offset PWM | `o_offset_1` / `o_offset_2` | P112 / P111 | out | 25 kHz, filtered on the board |
| Backlight PWM | `o_pwm_display` | P42 | out | 833 Hz in these designs |
| Calibration / generator / EXT trigger | `o_1khz_calib` | P133 | inout | SLEWRATE FAST; fixed 1 kHz in stock, DDS (hi-Z when disabled) in Atlan's; ours adds the EXT trigger input and PULLDOWN |

Atlan4's EF2 variant uses a different pin map and is not in this folder.

## Sources

- `https://www.anlogic.com/en/support/tools-downloads` (versions, login, `web@anlogic.com`)
- `https://github.com/sipeed/sipeed_wiki/issues/690` (TD License route, six-month validity, stale Sipeed file)
- `https://github.com/mmicko/prjtang` (`devices.json`, README), `https://github.com/YosysHQ/nextpnr` (himbaechel list), `https://github.com/YosysHQ/yosys` (`techlibs/anlogic`)
- `https://trabucayre.github.io/openFPGALoader/vendors/anlogic.html` and `src/part.hpp` in `trabucayre/openFPGALoader`
- Local: `research/notes/fpga-verilog-analysis.md`, `research/notes/repo-donwulff.md`, `D:\src\FNIRSI_1014D_Firmware\FPGA_NOTES.md` (TD rebuild sections), `D:\src\FNIRSI_1014D_Firmware\fpga\AL3_1014D\build.sh` and `build.tcl`

## Rebuild results (2026-10-18, TD 4.6.8 SP1 V4.6.116866, WSL Ubuntu 24.04)

Command: `wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build.sh`. Sources unchanged.
Synthesis, place and route, timing analysis and bitgen all succeed. Wall clock is 11 to 16 s for the whole flow.

| Output | Bytes | sha256 | vs Atlan4's shipped file |
|---|---|---|---|
| `build/FPGA.bin` | 282,898 | `0d4c829c231b3bbe18c6dd6e815523b6e168f787561376aae502db6c1f370fd2` | **Byte-identical** to `bitstreams/atlan4-FPGA-0x1532.bin` |
| `build/FPGA.bit` | 285,341 | changes per build (date line) | Same size; differs only in the 6 bytes of the ASCII `# Date:` header line |

Header words: 32 bytes of 0xFF, then `cc55aa33 f0000006 18006c31` (AL3A10 id), user-code packet
`c1000006 00560000` (UCode `01010110` from `FPGA.al`). The byte pair 0x15 0x32 does not occur literally in
either `.bin`; the version constant is encoded in LUT contents, not stored as a plain word.
The build is deterministic. Three replay builds gave identical configuration frames, so no placement seed is
needed. The only input that changed the output was the bitgen user code.

Utilisation and timing (report corner TT 1.10 V 85 C, `zaklad.sdc` unchanged):

| | This rebuild | `fpga-verilog-analysis.md` section 10 (AL3) | Donwulff TD 5.0.3 defaults |
|---|---|---|---|
| LUT / reg / LE | 623 / 476 / 661 | 620 / 471 / 658 | 602 / 458 / - |
| BRAM9K | 32 / 48 | 32 / 48 | 32 |
| Pads, output regs in IOB | 60, 14 | 60, 14 | 60, 1 |
| `clk_200MHz` min period | 4.485 ns (223.0 MHz), 0 errors, TNS 0 | 6.234 ns (160.4 MHz), 38 errors, TNS -21.1 | 277.9 MHz, TNS 0 |
| `clkc[0]` (alias of clk_200MHz) | 8.485 ns, 52 errors, TNS -140.9 | 10.234 ns, 71 errors, TNS -172.9 | - |
| `clkc[1]` (`clk_ADC`, 90 degrees) | 8.014 ns, 6 errors, TNS -15.8 | 9.671 ns, 6 errors, TNS -26.0 | 144.5 MHz, -3.71 |

The section 10 numbers come from the reports in Atlan4's folder, and those are stale. His `FPGA_phy.timing` is dated
2026-01-09, while his `.bin`/`.bit` are dated 2026-02-14. Our rebuild reproduces the shipped `.bin` exactly,
so the table's first column is the true report for the shipped bitstream. The structural `clkc[1]` violation
(enable crossing into the 90-degree clock) is present in every build.

Not done at that point: nothing was flashed, `zaklad.v` was unchanged, and the CPLD release zip was not installed.
`SRC_REV=a947e78 ./build.sh` still reproduces `0d4c829c...` after the changes below (checked 2026-10-18).

## Changes

### 1. External trigger on the calibration pin (version 0x1533, 2026-10-18)

Commits 526c969 (design) and 4601dec (testbench). This is roadmap item 2 (`docs/plans/roadmap.md`). Firmware support is
needed before flashing: firmware that matches 0x1532 exactly treats 0x1533 as an unknown FPGA.

**Protocol** (also in `src/FPGA commands.txt`):

| Cmd | Dir | Change |
|---|---|---|
| 0x06 | read, 2 B | Returns 0x15 0x33. The AL3 family is 0x153x; the minor nibble 3 means EXT trigger and 0x28 exist. Everything else is as in 0x1532. |
| 0x15 | write, 1 B | Trigger source `data[1:0]`: 0 = CH1, 1 = CH2 (both unchanged), 2 = EXT, 3 = reserved (acts as EXT). |
| 0x28 | read, 1 B | New. bit0 = EXT level (synchronised, filtered), bit1 = generator enable (0x50, the FPGA drives the pin), bit2 = an EXT edge in either direction since the last sampling reset (0x01 = 1 clears it), bits 7..3 = 0. On 0x1532 the byte is stale, so read it only on 0x1533 or later. Stock firmware wrote 0x28 for roll mode. That write still does nothing. |

EXT semantics: the edge comes from 0x16 (0 rising, 1 falling). Mode 0x1A, pretrigger 0x0B, total 0x0C and the auto
time-out 0x0E work unchanged, and the level 0x17 is not used. The ADC level triggers are off while EXT is selected.
trigger_address (0x14) has the same meaning as for an ADC trigger: the address of the last sample written before the
edge. The firmware's trigger-point arithmetic is therefore the same for all sources.

**Pin.** P133 is LVCMOS33. The input buffer is always read, so EXT only makes sense with the generator off (0x50 = 0,
pad hi-Z). With the generator on, EXT reads the generator's own output back, which triggers on our own generator.
`zaklad.adc` now sets `PULLTYPE = PULLDOWN` on P133, so an open input idles low instead of floating. Inputs above
3.3 V are not allowed. The board network between P133 and the probe-comp terminal is not measured
(`research/notes/hack-repo-hardware-fpga.md` lists the pin among the "1K series" controls).

**Design** (`src/zaklad.v`, block "External trigger input on the calibration pin"):
- `ext_sync`: two-flop synchroniser on `clk_200MHz`. TD packs the first stage into the P133 input register (IREG).
- `ext_level`: majority of three consecutive synchronised samples. A 5 ns (one-clock) glitch is rejected; 10 ns and longer pass.
- `ext_edge`: rising or falling edge of `ext_level` per `trigger_edge`, only when `trigger_ext` is set.
- `ext_pending`: the edge is latched while `trigger_check_enable` is set (past the pretrigger, not yet triggered).
  It is applied to `sampling_triggered` right after the next rising edge of `sample_write_clock`, detected in the
  200 MHz domain with `swc_prev`. ADC triggers are applied at that same point. Pulses shorter than a sample period
  still trigger at slow timebases.
- The trigger_address capture, `sampling_enable`, pretrigger/total and auto time-out logic are untouched.
- Latency from the pad to the filtered edge is 4 to 5 clocks (20 to 25 ns). That is under one sample at
  <= 10 MSa/s and about 4 to 5 samples at 200 MSa/s. The AD9288 pipeline delay on the ADC side was not modelled.
  Measure the EXT-to-CH1 skew on the bench before correcting either side.
- `trigger_rise_detect` / `trigger_fall_detect` gain a `~trigger_ext` term. Cmd 0x15 also writes `trigger_ext` from `data[1]`.

**Build** (TD 4.6.8 SP1, `./build.sh`, 14 s; `zaklad.sdc` unchanged; corner TT 1.10 V 85 C):

| | Atlan4 0x1532 | 0x1533 | Delta |
|---|---|---|---|
| LUT / reg / LE | 623 / 476 / 661 | 635 / 486 / 686 | +12 / +10 / +25 |
| BRAM9K, pads | 32, 60 | 32, 60 | 0 |
| IOB regs | 14 OREG, 0 IREG | 13 OREG, 1 IREG | P133 holds the synchroniser's first stage, so the generator's output flop moved to fabric. That adds a small fixed routing delay at the pin. STA does not report it; my estimate is about 1 ns. Frequency, duty and the 20 ns DDS jitter are unchanged. |
| `clk_200MHz` | 4.485 ns (223.0 MHz), 0 errors, TNS 0 | 4.459 ns (224.3 MHz), 0 errors, TNS 0 | Worst path is now the new half-cycle path `sample_write_clock` (negedge) to `sampling_triggered`, slack +0.541 ns. Before, it was the sample-rate counter, +0.515 ns. |
| `clkc[1]` (`clk_ADC`) | 8.014 ns, 6 errors, TNS -15.8 | 8.394 ns, 6 errors, TNS -18.3 | The same structural enable crossing as before (see `AGENTS.md`). The netlist change moved the placement. |
| `clkc[0]` (SDC alias) | 8.485 ns, 52 errors, TNS -140.9 | 8.298 ns, 50 errors, TNS -129.9 | Artefact of the duplicate clock definition. |

The build log is clean apart from the existing `TMR-5009` (3 clock nets without constraints). Build steps:
optimize_gate 2.3 s, place 2.7 s, route 5.3 s.

**Bitstream** (`out/FPGA-ext-trigger.bin`, not committed; rebuild with `./build.sh`, deterministic):

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-ext-trigger.bin` (= `build/FPGA.bin` of commit 526c969) | 282,898 | `a96567031872ae99bd40f0722a111a08da0f6381b6c710b70998af784a341216` |
| same, padded with 0xFF to 1 MiB (flash image) | 1,048,576 | `96159a871de3af57c2174fe1ec66ed14b10fbe2e845c44564e7bda9ed252acae` |

Header words are the same as Atlan4's: `cc55aa33 f0000006 18006c31`, user code 0x0056.

**Testbench** (`tb/run.sh [seed]` in WSL; iverilog 12.0, about 4 s per run). It simulates `src/zaklad.v` next to Atlan4's
0x1532 file. `run.sh` takes that file from git (`REF_REV`, default a947e78) and renames its modules. The PLL and
sample memory are behavioural models (`tb/sim_models.v`).
- Phase 1: 60 random CH1/CH2 captures with random level, edge, mode, rate, pretrigger, total, time-out, relays,
  offsets and generator settings. EXT is toggled at random while the generator is off. Every output pin, every MCU
  read byte and the capture registers (trigger_address, write/read addresses, flags) must match the reference
  continuously. Result: 0 mismatches in about 775,000 samples.
- Directed checks: 0x06 = 0x1533; the 1 MHz generator matches the reference, and the pin is hi-Z when off. 0x28
  bits 0, 1 and 2. EXT rising and falling at 1 MSa/s give trigger_address = edge address - 1, and a rising edge is
  ignored when falling is selected. At 200 MSa/s the address lands within the latency window (seen: +1 write
  address). A 3 ns glitch is rejected and a 12 ns pulse triggers. A 100 ns pulse at a 10 us write clock triggers
  with the right address. With source CH1 or CH2, EXT edges do not trigger. The same step through CH1 and through
  EXT gives the same trigger_address. In auto mode the time-out gives trigger_address = pretrigger. Source 3 acts as
  EXT.
- Phase 3: after the EXT tests, a resync capture and 10 more random captures. Again 0 mismatches.
- Result: 38 checks, 0 failed, on seeds 12345, 1, 2, 3, 777, 4242, 99991 and 31337.
- Mutation check: 4 deliberately broken variants all fail the testbench. They removed the source gate on EXT, the
  EXT gate on the ADC trigger, the write-clock alignment, and the glitch filter. A fifth changed the ADC channel bit
  only for sources 2 and 3. It is behaviourally equivalent and passes.

**Bench checks** (owner/coordinator; flash only with firmware that accepts 0x1533):
1. Measure the input impedance at the probe-comp terminal with the generator off (0x50 = 0): resistance to GND
   and to 3V3, and the series resistance to P133 if reachable. This tells how strongly an external source must
   drive the pin and whether a 5 V source needs a divider.
2. Generator unchanged: 1 kHz default and a 1 MHz setting, frequency and duty on CH1 against the 0x1532 numbers.
3. EXT from a second source, such as the detector MCU or a TTL output, into the probe-comp terminal, with the same
   signal on CH1. Check rising and falling, normal and auto mode, and 0x28 bit0/bit2.
4. Skew between the EXT trigger point and the CH1 edge at 200 MSa/s, 10 MSa/s and 1 MSa/s. The prediction is
   20 to 25 ns from the FPGA side, plus the AD9288 pipeline delay on the CH1 side.
5. Open input with EXT selected in normal mode: no triggers. This verifies the pull-down.

### 2. Calibration-pin circuit, the probe-ground offset, and what EXT needs (2026-10-18)

Summary: the calibration path is DC-coupled through 1 kΩ. The probe ground (BNC shell) is **not** the FPGA's ground: it
sits on the front end's virtual ground VRF1, about +1.24 V above it. An EXT source must therefore be referenced to the
scope's digital ground, or shifted down by about 1.2 V. This section replaces an earlier inference of a series coupling
capacitor, which the 0.1 Hz measurement disproved.

**Measured** (coordinator, 2026-10-18; fw 0.2.11, FPGA 0x1532, CH1 x1 probe on the post, DC coupling, generator on):

| Setting | Result |
|---|---|
| Generator at 0.1 Hz, 9 s window at 333 S/s (`backups/calcap_CH1_0.1Hz_div599999.json`) | Both levels hold for more than 4 s with +0.002 V droop. Low -1.09 V, high +2.01 V, swing 3.10 V, noise 0.05 V rms. |
| 1 kHz, DMM DC, post to probe ground | 0.31 V. The mean of -1.09/+2.01 V is 0.46 V. |
| Earlier, shorted-input comparison | The low level is about 70 codes below the shorted input. |

No series capacitor: anything below about 1000 µF into the 1 MΩ input would droop visibly in 4 s. The swing is the full
logic swing, offset by about -1.1 V relative to the probe ground.

**Schematic** (pecostm32 EasyEDA file `D:\src\FNIRSI-1013D-1014D-Hack\Schematics\1013D\EASYEDA\SCH_Scope 1013D_2023-01-07.json`,
traced with a netlist script):
- **Calibration path** (sheet Scope_Data_Acquisition): FPGA U3 pin 133, then R21 (1 kΩ, 0805), then the net port
  `1KHZ_CALIB_SIG`. The port occurs once, so the run from R21 to the front-panel post is not drawn. The 0.1 Hz result
  shows it is DC: a wire or a resistor. No C, diode or TVS is drawn.
- **Probe ground** (sheet Scope_Analog_Input):
  - The shells of both BNC connectors (RF1 and RF2, pins 1 to 4) join R71 (100 Ω) and 100 nF / 1 nF caps (C100, C101,
    C131, C132, C116). R71's other end is **VRF1**.
  - VRF1 is made by U21, a TL432 with cathode tied to REF, so it shunts at its 1.24 V reference. It is fed from 3V3
    through R81 (200 Ω) and reaches the VRF1 net through R82 (1 Ω). The anode is on GND, which is the same net as the
    FPGA, F1C100s, USB connector and SD-card cage grounds.
  - Two 1N5819 Schottky diodes, D11 to GND and D12 to 3V3, sit on VRF1, presumably as clamps; I did not check their
    orientation.
  - The input attenuators and opamps reference VRF1: R68/R74 1M8 and R66/R76 75K sit on that node.
- **So the BNC shell is VRF1 through 100 Ω**: about +1.24 V above the FPGA's ground. The post swings 0 to about 3.1 V
  against FPGA ground, which is about -1.24 V to +1.9 V against the probe ground. The bench numbers (-1.09/+2.01 V)
  match to within about 0.15 V. Inference: that residual is the scope's own DC offset error at that range.
- **Independent confirmation** (`research/notes`):
  - thread-pages-031-060: VRF1 measured 1.233 V. It is the virtual ground for the input stage.
  - thread-pages-121-155: Atlan names a "100 ohm resistor BNC to Vref1", and 1.25 V TLV431 parts.
  - web-sources: "Front-end ground lifted to ~1.25 V; USB to grounded PC shifts center ~-1.25 V" and "USB ground not
    at BNC ground -> Y-shift when PC connected".
- **Case (b) is ruled out.** The lead's alternative was a resistive level shift to a negative rail on the calibration path.
  No such element is drawn, and the -6V3 rail appears only on the power and processor sheets.

**(a) What an external signal must look like to register as EXT.** The pad is LVCMOS33: VIH min 1.9 V and VIL max 0.8 V against
FPGA ground, absolute maximum 3.75 V (AL3 datasheet DS001 v1.3, "Device facts from the datasheet" below). P133 has the internal
pull-down, value not documented, probably tens of kΩ. The path from the post to the pad is DC through R21 (1 kΩ).
- **The reference ground decides everything.** The scope has two grounds on its connectors:
  - digital GND: USB connector shell, USB cable ground and SD-card cage, the same as the FPGA's ground;
  - probe ground: the BNC shells and probe ground clips, which are VRF1, about +1.24 V, through 100 Ω.
- **Source referenced to digital GND** (its ground wired to the USB shell or the USB cable's ground): any 3.3 V logic
  output works directly. Levels and both edges register, 0x28 bit0 follows the level, and source impedance up to a few
  kΩ is fine.
  - Caveat: do **not** also clip a probe ground onto that circuit. Doing so ties the BNC shell to digital GND, so R71
    loads VRF1 with about 12 mA. R81 can supply only about 10 mA, so the TL432 drops out and VRF1 sags to about 1.1 V
    (3.3 V x 100 / 301). This is an inference from the schematic.
  - The whole front end then shifts, in the same way as the forum's "USB to a grounded PC" effect. Measurements on CH1
    or CH2 are offset until the link is removed. Nothing is damaged: R71 dissipates about 15 mW.
  - A probe tip without its ground clip is fine. It reads the signal 1.24 V lower.
- **Source referenced to the probe ground** (its ground on a probe clip or BNC shell, the natural setup when CH1/CH2 also
  measure the same circuit): its 0/3.3 V appears at the post as about +1.24/+4.54 V against FPGA ground.
  - **High is clamped:** the pad sits at about 3.9 V, with about 0.6 mA into the upper clamp diode through R21. That is
    out of spec but small.
  - **Low is at 1.24 V, between VIL and VIH.** A plain logic signal on the probe ground is therefore **not reliable**
    as EXT. It may never read low, or may chatter.
  - **Fix: shift it down with a series red LED or three 1N4148 diodes** between the source and the post, anode at the
    source. This is an inference, to be confirmed on the bench.
    - Low: the 1.24 V is below the LED's conduction threshold, so the internal pull-down holds the pad near 0 V.
    - High: 4.54 V minus about 1.5 V gives about 3.0 V at the pad, which is above VIH and needs no clamp current.
    - The source supplies only the pull-down current, tens of µA. The return current flows through R71 into VRF1, which
      the TL432 absorbs.
    - The rising edge is driven and fast. The falling edge is only the pull-down discharging the pad and the post
      wiring: estimated about 0.5 µs (50 kΩ x 10 pF). Use **rising-edge EXT** with this shifter.
  - Optocoupler alternative: the LED is driven from the detector side. The phototransistor runs on the scope side,
    emitter on digital GND, collector to the post, with a pull-up to a scope-side supply such as the USB VBUS through a
    divider. This is cleaner but needs parts and a digital-ground wire.
- **Pulse width:** at least 10 ns at the pad, from the majority filter.

**(b) ESD and overvoltage risk to the pad.**
- **R21 (1 kΩ) is the only drawn protection.** There is no TVS or external clamp. The pad's internal diodes take
  excursions beyond about -0.6 V and 3.9 V, with R21 limiting the current. Estimates against FPGA ground:
  - 3.3 V logic on the probe ground, unshifted: about 0.6 mA into the upper clamp, continuously while high.
  - 5 V logic on the probe ground: 6.24 V at the post, about 2.3 mA. **Do not.**
  - 5 V logic on digital GND: about 1.1 mA. Use a 1k/1k divider, or a 3.3 V source.
  - An external source fighting the generator: up to about 4.5 mA with a probe-ground-referenced 3.3 V high against the
    generator's 0 V. The firmware turns the generator off when EXT is selected.
- **I found no AL3 clamp-current limit.** Treat any pad voltage outside 0 to 3.3 V as out of spec.
- **The pulse-induction detector's flyback is the real hazard.** It reaches hundreds of volts. Never connect the post to
  the coil, the MOSFET drain or the damping resistor. 300 V through 1 kΩ is about 300 mA into the clamp. Tap the
  detector MCU's 3.3 V gate-drive or sync GPIO, before the gate driver.
- **Ground loops:** linking the probe ground and digital GND through an external circuit (or through a mains-grounded
  PC on USB plus an earthed device under test) loads VRF1 as described in (a). This is a measurement error, not damage.
  It applies to **all** scope measurements, not only EXT.
- **ESD:** the post is exposed. 1 kΩ plus the internal diodes is modest protection. Touch ground first, and keep the
  wiring connected before power-up. For regular use, put a TVS or a BAT54S clamp on the source side.

**(c) Recommended bench tests**
0. **Owner's DMM tests** (already requested; predictions from the schematic):
   - BNC shell to USB shell, scope on, on battery, USB unplugged: **+1.2 to +1.25 V** (VRF1, forum 1.233 V).
   - Post to BNC shell with the generator at 0.5 Hz: alternating about -1.24 V and about +1.9 to +2.0 V. Post to USB
     shell: about 0 and about 3.1 V.
1. **EXT on digital GND, after flashing 0x1533 with fw 0.2.9, generator off.**
   - Detector MCU or a 3.3 V GPIO toggling at 100 Hz and 1 kHz. GPIO to the post through 1 kΩ. MCU ground to the
     scope's USB shell or a USB breakout's GND.
   - CH1 probe **tip** on the GPIO with the **ground clip unconnected**. CH1 reads the GPIO about 1.24 V low; timing is
     unaffected.
   - Expect: rising and falling EXT both trigger at the CH1 edge, and 0x28 bit0 follows a 1 Hz toggle.
   - Record the trigger-point to CH1-edge skew at 200, 10 and 1 MSa/s. The FPGA side predicts 20 to 25 ns.
2. **EXT on the probe ground, same setup.** MCU ground on the CH1 ground clip; nothing on digital GND.
   - First without a shifter: expect missed or chattering triggers, and bit0 stuck at 1 or noisy. That confirms the
     1.24 V low.
   - Then with a red LED in series, anode at the GPIO: expect clean rising-edge triggers and bit0 following the level.
     Falling-edge EXT should be late by up to about 1 µs.
3. **Ground-loop check, both grounds linked.** Leave the scope on the shorted-input view and connect the MCU ground to
   both the USB shell and the CH1 clip. Expect the CH1 baseline to jump, the VRF1 sag. Remove the link afterwards.
4. **Open post, EXT selected, normal mode, generator off.** Expect no triggers; the pull-down holds the pad at 0 V.

**Text changes needed elsewhere:** "EXT is 3.3 V logic" is not enough. User-facing texts (firmware help, MCP tool
descriptions, `docs/agent-measurement-guide.md`) should say:
- reference the EXT source to the scope's USB ground, or shift it down about 1.2 V (series LED) when it shares the probe
  ground;
- never link the probe ground and USB ground, because the traces shift.

### 3. Decimation filter: boxcar and CIC (version 0x1534, 2026-10-18)

This is the FPGA half of the spectrum-analyser work (`docs/plans/dpos350p-features.md`, sections 2c and 2e, step F1).
Before 0x1534, slower timebases ran the ADC encodes at 200/(D+1) MSa/s and stored every sample. That gave no
anti-alias filter and no resolution gain. With the new mode on, the ADCs always convert at 200 MSa/s. The FPGA then
stores a boxcar sum or a CIC of order 2 or 3 over N samples as one 16-bit sample. With the mode off (the power-on
default), the design is functionally identical to 0x1533; the testbench proves it cycle by cycle.

**Protocol** (also in `src/FPGA commands.txt`; multi-byte values high byte first):

| Cmd | Dir | Meaning |
|---|---|---|
| 0x06 | read, 2 B | 0x15 0x34. AL3 with EXT trigger (0x28) and the decimation filter. |
| 0x53 | write, 1 B | bits 1..0 = K: 0 = sample skipping as 0x1533 (default), 1 = boxcar, 2 = CIC2, 3 = CIC3. bit 2 = swap: 0 means core B is the earlier sample of a pair, the interleave `fpga_read_sample_data` uses. bit 3 / bit 4 = capture the A / B cores on the falling write-clock edge, 5 ns later (bench knob, default 0). Bits 7..5 write 0. |
| 0x54 | write, 1 B | Output shift S, bits 5..0. |
| 0x55 | write, 2 B | 16-bit trigger level for K >= 1, compared with the stored sample of the 0x15 channel. 0x17 is ignored while K >= 1. |
| 0x29 | read, 2 B | byte 0 = {bit 7: 16-bit layout active (K >= 1), bits 6..5 = 0, bit 4 neg_b, bit 3 neg_a, bit 2 swap, bits 1..0 K}; byte 1 = S. |

**Decimation and scaling.** D is the 0x0D divider.
- N = 2 * (floor(D / 2) + 1); the output rate is 200 MSa/s / N. For odd D this is the K = 0 rate 200 / (D + 1), so the
  firmware's `sample_rate_settings` table works unchanged except for its entry 0.
- With K >= 1 and D = 0, N = 2 (100 MSa/s). Use K = 0 for 200 MSa/s.
- The filter output y has gain N^K. The stored sample is min(65535, y >> S).
- Recommended S = max(0, bitlen(255 * N^K) - 16). It never saturates, and full scale lands at 32,768 to 65,280.
- In ADC LSB: mean = out * 2^S / N^K. The 16-bit trigger level for an 8-bit level v is round(v * N^K / 2^S).
- The 48-bit integrators and combs are exact while 255 * N^K < 2^48:
  - K = 1: every divider.
  - K = 2: N <= 1,050,630, which covers the whole firmware table.
  - K = 3: N <= 10,334 (D <= 9999, 20 kSa/s and up). Beyond that the result is wrong; use K = 2.

**Memory layout and read-back.** With K >= 1 each 32-bit word is {CH2[15:0], CH1[15:0]}, one output sample per
channel. That gives 8192 samples per channel instead of 16,384 8-bit samples (one A/B pair per word).
- The read commands are unchanged. 0x20 / 0x21 return CH1 low / high and 0x22 / 0x23 return CH2 low / high. Each read
  still advances the read address by one word.
- A channel is read as two passes from the same 0x1F start: low bytes, then high bytes. That is the same bus cost as
  the A and B passes at K = 0.
- 0x0B (pretrigger), 0x0C (total), 0x0E (time-out) and 0x14 (trigger_address) count words, so they count output
  samples. They are still 13 bit.

**Design** (`src/zaklad.v`, module `decim_chan` and the block "Decimation filter control and data path"):
- **Clocking.** `sample_clock_enable` is forced on by putting divider 0 on the static side of the 32-bit compare, which
  keeps it off the timed counter path. The encodes and `sample_write_clock` then run at full rate (100 MHz, one A/B
  pair per edge).
- **Strobe.** `dec_pc` counts P = floor(D / 2) + 1 write clocks per output sample. The valid chain `dec_sv` follows
  the filter pipeline. Its last stage, `dec_wr`, marks the edge where a sample is written.
- **Capture gating.** `swc_adv = ~dec_on | dec_wr` gates the write address, `time_base_cnt`, `sample_write_enable`,
  `sampling_half_way`, `trigger_read_address`, `trigger_address`, the trigger data registers and the memory CE. With
  K = 0 it is constant 1. The reset branches are not gated, so a reset reaches the logic within one write clock at any N.
- **Filter.** A CIC of order K decimating by N = 2P is the polyphase prefilter (1 + z^-1)^K, evaluated once per pair
  (taps 1,1 / 1,2,1 / 1,3,3,1 over B(k-1) A(k-1) B(k) A(k)), followed by a CIC of order K that decimates the 100 MSa/s
  pair stream by P (noble identity). K = 1 is the plain boxcar sum of N consecutive samples.
- **Bypassed stages.** For K < 3 the unused integrator and comb stages are plain registers, so the latency is the same
  for every K.
- **Free-running.** The filter is never reset, so data stays valid across captures. After changing 0x0D, 0x53 or
  0x54, wait at least (K + 2) * N * 5 ns plus 1 us before 0x01.
- **Output stage.** A two-step barrel shift by S (8-bit steps, then 1-bit steps), then the saturation to 0xFFFF.
- **Capture.** Pins are captured on the rising write-clock edge (`pa`/`pb`) or, per core, on the falling edge
  (`na`/`nb`, 5 ns later), then registered into the pair.
- **Trigger on the 16-bit stream.** The result of `sample >= trigger_level16` goes into the existing 8-bit
  `trigger_data` register as 0xFF or 0x00, and the 8-bit compare runs against 0x80. The rise and fall rules
  (prev < L <= cur, prev >= L > cur) and the timed path into `sampling_triggered` therefore keep their 0x1533 structure.
- **EXT.** The EXT detect gains `swc_adv_q`, a register, so it is applied only right after an edge that wrote a sample.
  The term still fits one LUT4.
- **Latency.** 12 write clocks (120 ns) from the pin capture to the memory write, for every K and N. Measured by the
  testbench against the reference model.
  - A stored sample covers the input up to 120 ns before its write edge. Its centre lies K * (N - 1) / 2 input samples
    (K * (N - 1) * 2.5 ns) earlier.
  - ADC triggers run on the stored stream and keep the K = 0 convention, so no correction is needed.
  - For EXT, trigger_address is still the last sample written before the edge. The data lags the edge by
    120 ns + K * (N - 1) * 2.5 ns, and the firmware should shift the EXT trigger point by that amount.
- **Interleave.** (1 + z^-1)^K has a zero at 100 MHz, so the A/B core mismatch spur at fs/2 is removed for every K.

**Gain, resolution, alias rejection and droop** (from `tb/cic_model.py table`). Definitions:
- fo = output rate.
- Droop is measured at B = fo/8 and fo/4.
- Alias is the attenuation at fo - B, the nearest input that folds onto B.
- Noise bits = 0.5 * log2((sum h)^2 / sum h^2), the gain for white noise. The ADC has about 2 LSB of noise, so this is
  the expected resolution gain over 8 bits until the ADC's linearity and the front end dominate. That limit has not
  been measured.
- LSB = one 16-bit step in 8-bit ADC LSB, with the recommended S.

| K | N | fo | S | LSB | noise bits | droop fo/8 | droop fo/4 | alias fo/8 | alias fo/4 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 100 M | 0 | 0.5 | 0.50 | -0.17 dB | -0.69 dB | -14.2 dB | -8.3 dB |
| 1 | 10 | 20 M | 0 | 0.1 | 1.66 | -0.22 dB | -0.90 dB | -17.0 dB | -10.4 dB |
| 1 | 100 | 2 M | 0 | 0.01 | 3.32 | -0.22 dB | -0.91 dB | -17.1 dB | -10.5 dB |
| 1 | 1000 | 200 k | 2 | 0.004 | 4.98 | -0.22 dB | -0.91 dB | -17.1 dB | -10.5 dB |
| 1 | 10000 | 20 k | 6 | 0.0064 | 6.64 | -0.22 dB | -0.91 dB | -17.1 dB | -10.5 dB |
| 2 | 2 | 100 M | 0 | 0.25 | 0.71 | -0.34 dB | -1.38 dB | -28.4 dB | -16.7 dB |
| 2 | 10 | 20 M | 0 | 0.01 | 1.95 | -0.44 dB | -1.81 dB | -34.0 dB | -20.7 dB |
| 2 | 100 | 2 M | 6 | 0.0064 | 3.61 | -0.45 dB | -1.82 dB | -34.3 dB | -20.9 dB |
| 2 | 1000 | 200 k | 12 | 0.0041 | 5.28 | -0.45 dB | -1.82 dB | -34.3 dB | -20.9 dB |
| 2 | 10000 | 20 k | 19 | 0.0052 | 6.94 | -0.45 dB | -1.82 dB | -34.3 dB | -20.9 dB |
| 3 | 2 | 100 M | 0 | 0.125 | 0.84 | -0.51 dB | -2.06 dB | -42.6 dB | -25.0 dB |
| 3 | 10 | 20 M | 2 | 0.004 | 2.09 | -0.67 dB | -2.71 dB | -51.1 dB | -31.1 dB |
| 3 | 100 | 2 M | 12 | 0.0041 | 3.75 | -0.67 dB | -2.74 dB | -51.4 dB | -31.4 dB |
| 3 | 1000 | 200 k | 22 | 0.0042 | 5.41 | -0.67 dB | -2.74 dB | -51.4 dB | -31.4 dB |
| 3 | 10000 | 20 k | 32 | 0.0043 | 7.08 | -0.67 dB | -2.74 dB | -51.4 dB | -31.4 dB |

Sample skipping (K = 0) has 0 dB alias rejection everywhere.
- The plan's F1 acceptance case is a 3.9 MHz input at 1 MSa/s (N = 200), which aliases to 100 kHz. The model
  attenuates it by 32 dB (K = 1), 64 dB (K = 2) and 96 dB (K = 3) against skipping, with 0.14 / 0.29 / 0.43 dB droop
  at 100 kHz.
- A square wave from the generator carries harmonics, and the 8-bit ADC's own spurs (SFDR about 45 to 50 dBc) limit
  what the bench can see.
- Use K = 3 for spectra and K = 1 for time-domain views that must not ring. The CIC step response is monotonic but
  K * (N - 1) + 1 samples long.

**Build** (TD 4.6.8 SP1, `./build.sh`, about 35 s; corner TT 1.10 V 85 C):

| | 0x1533 | 0x1534 |
|---|---|---|
| LUT / reg / LE | 635 / 486 / 686 | 2304 / 1790 / 2488 (TD counts 8640 LUT sites: 26.7 %; the datasheet lists 10,368 LUT) |
| BRAM9K, pads | 32, 60 | 32, 60 |
| IOB regs | 13 OREG, 1 IREG | 13 OREG, 1 IREG. TD cannot pack the ADC pins into IREG because they feed registers on both write-clock edges; `PACKREG = ON` in the `.adc` has no effect (the bitstream is identical). |
| `clk_200MHz` | 4.459 ns, 0 errors | 4.899 ns, 0 errors, TNS 0. Worst path: the existing half-cycle `sample_write_clock` (negedge) to `sampling_triggered`, 2 LUT levels, slack +0.101 ns against the SDC's 40 % waveform (2.0 ns half period). Against the PLL's real 50 % duty that is about +0.6 ns. The sample-rate counter compare is not on this path any more. |
| `sample_write_clock` | not constrained | 100 MHz generated clock, 0 errors. Decimator internal worst slack +3.36 ns (6.64 ns, 151 MHz). clk_200MHz to write-clock crossings +1.40 ns. Write-clock to clk_200MHz paths: 2-cycle multicycle, +2.9 ns or more. |
| `clkc[1]` (`clk_ADC`) | 8.394 ns, 6 errors | 7.528 ns, 6 errors, TNS -11.7. The known structural enable crossing. It is inert at K >= 1 (enable constant) and at 200 MSa/s. |
| `clkc[0]` (SDC alias) | 8.298 ns, 50 errors | 8.431 ns, 51 errors, TNS -92.0. Artefact of the duplicate clock definition. |

**`zaklad.sdc` change.** `sample_write_clock` is declared a generated clock: `-edges {2 4 6}` on `clk_200MHz`, rising
on its falling edges, period 10 ns, the fastest rate.
- Paths from it into `clk_200MHz` get `set_multicycle_path -setup 2 / -hold 1`. Their sources hold for a whole write
  clock (>= 10 ns), so either of the next two 200 MHz edges takes them, and trigger_address is the same either way.
- Before 0x1534 all of these paths were unconstrained.
- With `-divide_by 2` instead of `-edges`, TD assumes the clock rises with `clk_200MHz` and reports 40 false hold
  errors.

**Bitstream** (`out/FPGA-decim.bin`, not committed; deterministic: four builds of the same source gave the same hash):

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-decim.bin` (= `build/FPGA.bin`) | 282,898 | `83140510dfbfef56cc5ed15d70866e6bc52c407a41fd0ea480e8993df5b820c1` |
| `backups/al3_decim_0x1534_83140510_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `c387a15ed11d209deab639d1de84dd3117419d800aed05e032b192650e13c0a0` |

Header words as before: `cc55aa33 f0000006 18006c31`, user code 0x0056.

**Testbench** (`tb/run.sh [seed]`, about 25 s). It now runs three steps; all must pass.
1. `tb_ext_trigger.v` against Atlan4's 0x1532, unchanged apart from the version check: 38 checks.
2. `tb_decim.v` against our 0x1533 from git (`REF1533_REV`, default 2839258): 98 checks.
   - Phase 1: 50 random K = 0 captures, trigger source CH1, CH2 or EXT, triangle or random ADC data, random values
     written to 0x53 (K = 0), 0x54 and 0x55. Pins, capture registers, the write-clock and EXT state, every bus read and
     the whole 8192-word memory after each capture must equal 0x1533. About 1.0 M samples, 0 mismatches.
   - Directed checks: 0x06 = 0x1534, 0x29 fields, and the MCU read-back of the 16-bit samples through 0x20 to 0x23.
   - Trigger position: a step through K = 1, 2 and 3 at N = 2, 10, 20, 64 and 100, rising and falling, CH1 and CH2,
     60 cases. In every case the first stored sample past the 16-bit level is at trigger_address + 1. The K = 0 run
     measured the same offset.
   - 0x17 is ignored with K >= 1.
   - EXT with K = 1 to 3 at N = 20 and 200: trigger_address = last sample written before the edge.
   - Auto time-out counts output samples.
   - Back to K = 0: the free-running divide-by-2 toggle phase is copied to the reference (arbitrary history in 0x1533
     too), then 10 more random captures with 0 mismatches.
3. `cic_model.py check`: every memory write of 18 logged decimated captures, about 1,870 samples, is recomputed with a
   direct-form FIR. The FIR is the N-tap boxcar convolved K times; for K = 1 it is also checked as the plain sum.
   - The captures cover all K, N from 2 to 2000, both core orders, all four capture-edge settings, recommended and
     too-small shifts (saturation), and even and odd dividers.
   - Every sample matches at latency 12 write clocks, and the writes are spaced exactly P edges apart.

Seeds 1 to 10, 77, 1234 and 99999 all pass. Mutation runs (`DUT=` override) fail as they should; the list of variants
is in `AGENTS.md`.

**Bench checks** (owner/coordinator; needs firmware that knows 0x1534, and never flash without the verified stock dump):
1. K = 0 regression: as for 0x1533, including EXT. The ADC capture path now has a 2-input mux in front of the BRAM, and
   the netlist was re-placed. Look for glitches on a 10 to 30 MHz sine at 200 MSa/s.
2. Capture window: K = 1, D = 1 (N = 2, 100 MSa/s) on a slow ramp or a 100 kHz sine. Compare out / 2 with the K = 0 A
   and B values; there must be no isolated spikes. Repeat with 0x53 bits 3 / 4 set and keep the cleanest setting.
3. Core order: K = 3, N = 2 or 4 on a 20 to 30 MHz sine. The amplitude should follow the droop table for swap = 0. A
   wrong order shows as excess attenuation and distortion; then set bit 2.
4. Alias rejection (F1): generator 3.9 MHz square, 1 MSa/s, K = 0 vs K = 1 vs K = 3. Measure the 100 kHz alias with the
   host FFT; expect at least 15 dB less with K = 1.
5. Resolution: shorted input, K = 1 and 3 at N = 1000. The rms noise in ADC LSB should fall roughly as the noise-bits
   column predicts.

### 4. Design study: CIC compensation FIR (built as 0x1535, section 5)

Owner direction: the default acquisition mode should become CIC plus a compensation FIR. 0x1534 stays frozen; this
section is a design study (`tb/fir_design.py report`, numpy only, 18-bit quantised taps).

**Where the FIR goes decides what it can do.**
- **A: a FIR at the output rate fo after the CIC.** It flattens the droop below 0.02 dB with 15 taps. It cannot remove
  aliases, which already folded in the CIC decimation; it raises the alias band by 1 to 7 dB.
- **B: CIC decimating by N/2, then a symmetric FIR at 2 fo that decimates by 2.** This flattens the passband and adds
  about 20 dB of alias rejection. Recommended.

**Results for B, whole chain:**
- Passband edge a = 0.5 means flat to fo/4. Ripple is 0.04 to 0.08 dB with T = 15 taps, and alias rejection is 17 / 34
  / 51 dB for K = 1 / 2 / 3, against 10.5 / 20.9 / 31.4 dB for the CIC alone.
- a = 0.8 (flat to 0.4 fo) needs T = 31 taps for 0.3 dB ripple, with alias rejection 12 / 24 / 36 dB (CIC alone: 6 /
  12 / 18 dB).
- Ripple and alias rejection at N = 32 and at N = 4096 agree within 0.5 dB, so one coefficient set per K and profile
  covers every N.

**Cost and limits:**
- Multiplier: one M18x18 per channel (2 of the 3). It is time-multiplexed with the symmetric pre-add, (T + 1) / 2
  multiply-accumulates per output sample. There are N / 2 write clocks per output, so the FIR engages for N >= 16 at
  T = 15 and N >= 32 at T = 31.
- N must be a multiple of 4, so that the pair-stream CIC decimates by P / 2. Below the minimum N, the 0x1534 path is
  used unchanged.
- Other logic: a T x 18-bit delay line per channel (one BRAM9K, or about 560 FF), a 16 x 18 coefficient ROM per K, and
  a 40-bit accumulator. The estimate is +300 to +600 LUT and +200 to +700 FF, against 2304 LUT used now. K = 3 fits;
  coefficients are per K, the datapath is shared.
- Scaling: FIR input 18 bit (the CIC output shifted to 17 bits). Coefficients sum to 2^17, and sum |h| is 1.35 to
  1.84. The output is acc >> 17, clamped to 0 .. 65535.
- Headroom: the step overshoot and undershoot of the chain is 6 to 9 %, so the FIR mode needs the recommended shift + 1.
- Group delay in output samples: (T - 1) / 4 for the FIR (3.5 at T = 15, 7.5 at T = 31) plus K (N / 2 - 1) / (2 N)
  (about K / 4) for the CIC, plus the pipeline latency in write clocks. The tb would report it the same way as the
  12-write-clock latency of 0x1534.

### 5. CIC compensation FIR (version 0x1535, 2026-10-18)

This is option B of section 4, built. With the FIR on, the CIC decimates by N/2, and a 15-tap symmetric FIR decimates
its output by 2. The result is flat to fo/4 and has about 17 dB more alias rejection per CIC order than 0x1534. With
0x56 = 0 (power-on), or at a divider where the FIR cannot engage, the design is functionally identical to 0x1534. The
testbench proves that cycle by cycle against 0x1534 from git.

**Protocol** (also in `src/FPGA commands.txt`):

| Cmd | Dir | Meaning |
|---|---|---|
| 0x06 | read, 2 B | 0x15 0x35. |
| 0x56 | write, 1 B | bit 0 = FIR enable. Bits 7..1 are reserved; write 0. Power-on 0. |
| 0x29 | read, 2 B | byte 0 bit 6 = 0x56 bit 0 as written; bit 5 = FIR active now. Other bits as 0x1534. Both bits are 0 with the FIR off, so 0x29 reads as on 0x1534. |

**When it engages.** The FIR is active when 0x56 bit 0 = 1, K (0x53) >= 1, N >= 16 and N is a multiple of 4.
- With D the 0x0D divider and N = 2 * (floor(D / 2) + 1), that is D >= 14 with D bit 1 set, so D mod 4 is 2 or 3.
- Examples: D = 14, 15 give N = 16; D = 18, 19 give N = 20; D = 99 gives N = 100; D = 16, 17 (N = 18) do not engage.
- In every other case the 0x1534 path runs, bit for bit. The firmware can read 0x29 bit 5 to see which one ran.
- In the firmware's `sample_rate_settings` table (`firmware/scope/variables.c`, all odd dividers, N = D + 1), the FIR
  engages at 10 MSa/s (N = 20) and every slower entry down to 500 Sa/s. 20 MSa/s (N = 10) and faster stay on the CIC
  path.
- K = 3 is exact down to 10 kSa/s with the FIR (N/2 <= 10,334), against 20 kSa/s on the CIC path.

**Scaling.**
- The CIC has gain (N/2)^K. Each CIC sample is min(65535, y >> S), and the FIR has DC gain 1:
  stored = clamp(0 .. 65535, (sum(h * x) + 2^16) >> 17).
- Recommended S = max(0, bitlen(255 * (N/2)^K) - 16) + 1. The + 1 keeps the CIC samples below 32,768. The largest
  FIR gain for any input is the sum of its positive taps, 1.17 / 1.18 / 1.21 for K = 1 / 2 / 3. So with this S the
  stored value cannot saturate, for any input.
- Step response: 6.6 to 6.9 % overshoot and the same undershoot, for every K and N. A step from near 0 to near full
  scale therefore clamps at 0 when the low level is within about 7 % of the step size of zero.
- ADC LSB: mean = out * 2^S / (N/2)^K. The 16-bit trigger level for an 8-bit level v is round(v * (N/2)^K / 2^S).
- Exact while 255 * (N/2)^K < 2^48. For K = 3 that means N/2 <= 10,334, so N <= 20,668.

**Response** (`tb/fir_design.py`, whole chain; passband 0 to fo/4; alias = worst gain of any input that folds into
the passband, relative to DC):

| K | ripple N = 16 | ripple N = 4096 | alias N = 16 | alias N = 4096 | CIC alone (0x1534), alias fo/4 |
|---|---|---|---|---|---|
| 1 | 0.075 dB | 0.076 dB | -16.8 dB | -17.1 dB | -10.5 dB |
| 2 | 0.058 dB | 0.060 dB | -33.5 dB | -34.1 dB | -20.9 dB |
| 3 | 0.041 dB | 0.045 dB | -50.2 dB | -51.1 dB | -31.4 dB |

The response varies by less than 0.1 dB in ripple and 1 dB in alias rejection between N = 16 and N = 4096, so one
tap set per K covers every N.
- Taps h[0..7] (h[14 - i] = h[i]), 18-bit signed, sum 2^17, from `fir_design.py coeffs K 15 0.5`:
  - K = 1: -82 682 1890 -2151 -9088 3022 40046 62434
  - K = 2: -231 504 2397 -1457 -10255 877 40852 65698
  - K = 3: -381 332 2919 -766 -11452 -1289 41675 68996
- The model check imports these from `fir_design.py`, so a passing `tb/run.sh` also proves that the RTL table equals
  the design script.

**Design** (`src/zaklad.v`, module `fir_chan` and the block "Decimation filter control and data path"):
- **Strobes.** `fir_act` is `fir_ok` registered in the write-clock domain.
  - With the FIR active, `dec_sv[0]` also fires at `dec_pc == fir_hp` (P/2, registered), half way through the P count.
    The unchanged `decim_chan` therefore produces CIC samples every P/2 write clocks.
  - `dec_t` tags the strobes from `dec_pc == 0`. The CIC sample they make is the newest one of an output pair.
- **FIR per channel** (`fir_chan`):
  - A 15 x 16-bit delay line shifts on every CIC sample (`dec_sv[6]`).
  - On the clock after a tagged shift (`dec_t[7]`), the 8 symmetric pre-added sums are loaded into a shift register.
  - 8 multiply-accumulates follow, one per write clock: the multiplier input registers take the shift-register head
    and the tap, then a product register, then a 37-bit accumulator that starts from the rounding constant 2^16.
  - Then the clamp and the output register. `done` replaces `dec_sv[6]` as `dec_wr`, still once per P write clocks.
  - P = 8 (N = 16) is the minimum: the next load lands on the clock of the last MAC step.
- **Multiplier.** TD 4.6.8 infers the M18x18 from the registered `ma * mb` (`#MACRO_MULT 2` in `FPGA_rtl.area`,
  `#dsp 2 of 3` in `FPGA_phy.area`).
- **Outputs.** `dec_out1/2 = fir_act ? fir_out : cic_out`; the memory mux, the 16-bit trigger compare and the capture
  logic are those of 0x1534.
- **Free-running.** Nothing is reset. After changing 0x0D, 0x53, 0x54 or 0x56, wait (K + 10) * N * 5 ns + 1 us
  before 0x01.

**Latency and EXT correction.**
- Pin capture to memory write: **25 write clocks** (250 ns) with the FIR, against 12 on the CIC path. The model check
  measures it on every FIR segment.
- Group delay: the FIR centre is 7 CIC samples back, that is (T - 1) / 4 = 3.5 output samples. The CIC centre adds
  K (N/2 - 1) / 2 input samples.
- So a stored sample represents the input this long before its write edge:
  - FIR path: lag = 250 + 17.5 N + 1.25 K (N - 2) ns.
  - CIC path (0x1534, K >= 1): lag = 120 + 2.5 K (N - 1) ns.
- ADC triggers run on the stored stream and need no correction; the testbench checks the same trigger convention as
  K = 0.
- EXT: trigger_address is still the last sample written before the (synchronised) edge. The EXT edge lies at record
  position trigger_address + phase + lag / Tout, where Tout = 5 N ns and phase (0 to 1) is when the edge came after
  the last write. Unknown to the firmware, so use 0.5.

Measured with `tb_fir.v` (ramp from the EXT edge; a linear-phase filter delays a ramp by exactly its group delay;
the stimulus adds about +1 ns, see the tb comment):

| Path | K | N = 20: formula / measured | N = 100 | N = 400 |
|---|---|---|---|---|
| FIR | 1 | 622.5 / 623.9 ns | 2122.5 / 2123.8 ns | 7747.5 / 7748.6 ns |
| FIR | 2 | 645.0 / 646.3 ns | 2245.0 / 2246.9 ns | 8245.0 / 8248.9 ns |
| FIR | 3 | 667.5 / 668.8 ns | 2367.5 / 2369.2 ns | 8742.5 / 8744.7 ns |
| CIC (0x1534) | 1 | 167.5 / 168.7 ns | 367.5 / 368.7 ns | 1117.5 / 1118.8 ns |
| CIC (0x1534) | 2 | 215.0 / 216.0 ns | 615.0 / 616.5 ns | 2115.0 / 2117.2 ns |
| CIC (0x1534) | 3 | 262.5 / 263.7 ns | 862.5 / 863.9 ns | 3112.5 / 3114.4 ns |

So the formulas hold to within 1 to 4 ns. This also confirms the 0x1534 EXT formula of section 3. In output samples, the
FIR lag is 6.2 (N = 20), 4.4 to 4.7 (N = 100) and 3.9 to 4.4 (N = 400); it approaches 3.5 + K / 4 for large N.

**Build** (TD 4.6.8 SP1, `./build.sh`; corner TT 1.10 V 85 C):

| | 0x1534 | 0x1535 |
|---|---|---|
| LUT / reg / LE | 2304 / 1790 / 2488 | 3082 / 2716 / 3501 (35.7 % of TD's 8640 LUT sites) |
| M18x18 (`#dsp`) | 0 of 3 | 2 of 3, inferred from `ma * mb` (`#MACRO_MULT 2`) |
| BRAM9K, pads, IOB regs | 32, 60, 13 OREG + 1 IREG | same |
| `clk_200MHz` | 4.899 ns, slack +0.101 ns | 4.676 ns, 0 errors, TNS 0, worst slack +0.324 ns (the same half-cycle `sample_write_clock` to `sampling_triggered` path; placement-sensitive) |
| `sample_write_clock` | 0 errors, decimator +3.36 ns | 0 errors. Worst +0.677 ns on the existing `sampling_triggered` crossing into the capture registers. Decimator internal +3.22 ns. No FIR register is among the 60 worst endpoints, so every FIR path has more than +3.59 ns. |
| `clkc[1]`, `clkc[0]` | 6 / 51 errors (known, section 3) | 6 errors TNS -12.3 / 52 errors TNS -128.6, same structural and SDC-artefact paths |

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-fir.bin` (= `build/FPGA.bin`, = `backups/al3_fir_0x1535_5040a2ae.bin`) | 282,898 | `5040a2aeede071019bca0ffee47d765b87d980e1e47ca01ac32cb875c839aa96` |
| `backups/al3_fir_0x1535_5040a2ae_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `5ade9e755211fe65c6ff492419f3a4a91ec08edc8c16305ce52751baf9d588a5` |

Deterministic: two builds in different work dirs gave the same hash. Header words as before (`cc55aa33 f0000006
18006c31`), user code 0x0056.

**Testbench** (`tb/run.sh [seed]`, about 6 to 12 minutes; the FIR step takes most of it). A fourth step, `tb_fir.v` against 0x1534 from git
(`REF1534_REV`, default 6d4c8ad), followed by `cic_model.py check` on its log.
- Phase 1: 40 random captures. K = 0..3, random core order, capture edges, shift and 16-bit level, dividers up to
  45, trigger source CH1/CH2/EXT, generator and EXT noise.
  - 0x56 gets random bytes. When bit 0 is set, the divider is one where the FIR does not engage.
  - Compared every ns: pins, capture registers, `dec_out1/2`, `dec_wr`, `swc_adv`, `trigger_data`, the memory data
    input, every bus read, and the whole memory after each capture. 0 mismatches.
- Directed: 0x06 = 0x1535, and the 0x29 bits 6/5 for eligible and ineligible dividers, K = 0 and the reserved bits.
- Phase 2: 21 logged FIR captures. All K; N = 16 to 2000 including odd dividers; both core orders; all capture-edge
  settings; a telegraph signal (0 / 255) with the shift one below the rule, which saturates and clamps at 0; and a
  random eligible case. `cic_model.py` recomputes every stored sample (CIC by N/2 as a direct-form FIR, 16-bit
  saturation, the 15 taps, rounding, clamp): about 1,540 samples, all exact, all at latency 25.
- Trigger position: K = 1..3 at N = 16, 20, 64, 100 and 200, rising and falling, CH1 and CH2, 60 cases. The first
  stored sample past the level is at trigger_address + 1, as for K = 0.
- EXT with the FIR (K = 1..3, N = 20 and 200): trigger_address = last sample written before the edge.
- EXT data lag, 18 cases (table above). Auto time-out with the FIR counts output samples.
- Phase 4: FIR off, one full-memory capture to resynchronise the memory, then 15 more random captures with
  0 mismatches.

**Mutation runs** (`DUT=<file> ONLY=fir tb/run.sh 3`, each with its own `SIM_WORK`). All 13 fail as they should:

| # | Mutation | Caught by |
|---|---|---|
| 1 | engages without the "N multiple of 4" test | phase 1 equivalence, 0x29 check |
| 2 | engages from divider 10 (N = 12) | 0x29 check |
| 3 | one K = 2 tap off by one | model (K = 2 segments) |
| 4 | half strobe one write clock late (`fir_hp` without + 1) | model (every FIR segment); the sim itself runs very long |
| 5 | pre-add load one clock early (`dec_t[6]`) | model, EXT lag |
| 6 | no rounding constant | model, EXT lag |
| 7 | no clamp at 0 | model (telegraph segments) |
| 8 | memory write one clock early | model latency, EXT lag |
| 9 | output mux on `fir_en` instead of `fir_act` | phase 1 and 4 equivalence |
| 10 | wrong centre-tap sample | model, EXT lag |
| 11 | 0x29 bits 6 and 5 swapped | 0x29 check |
| 12 | delay line shifts one clock early (`dec_sv[5]`) | model, EXT lag |
| 13 | half strobe gated by `fir_en` instead of `fir_act` | phase 1 and 4 equivalence |

**Bench checks** (owner/coordinator; needs firmware that knows 0x1535 and 0x56; never flash without the verified stock
dump):
1. FIR off: repeat the 0x1534 checks 1 and 4 (section 3), plus 0x29 bits 6/5 = 0.
2. Passband: K = 3, N = 100 (2 MSa/s), FIR on. Sweep a sine from 50 kHz to 500 kHz (fo/4) and compare the amplitude
   with the FIR off: flat within 0.1 dB with the FIR, versus up to 2.7 dB droop without. The front end's own response
   cancels in the ratio.
3. Alias: generator square at fo - 50 kHz (for N = 100: 1.95 MHz), K = 1, FIR off vs on. The 50 kHz alias should drop
   by about 6 dB more than the CIC alone gives at that frequency. Use the host FFT.
4. Step: generator 1 kHz square at N = 200, K = 2, FIR on. Expect about 7 % overshoot and undershoot, symmetric
   around the edge (linear phase), with no clipping at the recommended shift.
5. EXT timing: EXT and CH1 driven from the same edge. The CH1 edge in the record should sit at trigger_address +
   0.5 + lag / Tout (+/- 0.5 sample) with the lag formula above.

## Device facts from the datasheet

Source: Anlogic "AL3 series FPGA datasheet" DS001 v1.3 (Sept 2017, Chinese, owner's download `AL3A10LG144.pdf`, 84 pages).
Page numbers are PDF pages. Details and the configuration-pin analysis: `docs/fpga-flash-from-mcu.md`, "Datasheet check".

| Item | AL3A10 (TQFP144 `LG144`) |
|---|---|
| LUTs / FFs | 10,368 / 8,640 (p.6) |
| Distributed RAM | 69,120 bits |
| Block RAM | 48 x EMB9K + 2 x EMB32K = 507,904 bits (our design uses 32 of the 48 EMB9K) |
| Multipliers | 3 x M18x18 (simple multiplier only, no full DSP) |
| PLLs / global clocks / I/O banks | 2 / 16 / 8 |
| User I/O on TQFP144 | 91, with 32 LVDS pairs (p.7); the 184 figure in table 1-1 is the BGA256 |
| IDCODE | not given in the datasheet; 0x18006C31 comes from prjtang and the owner's JTAG read |
| LVCMOS33 / LVTTL33 input | VIL max 0.8 V, VIH min 1.9 V, up to VCCIO+0.3 V (p.63); optional hysteresis 250 or 450 mV (p.62) |
| Overvoltage | absolute maximum input 3.75 V, recommended maximum 3.6 V; clamp figure only "PCI-clamp 25 mA" (p.59-60) |
| Weak pull-up / pull-down | 35 to 250 uA, so about 13 to 94 kOhm at 3.3 V [inference] (p.62) |
| Configuration | MSPI (MSEL 001 fast 0x0B, 010 standard 0x03), CCLK 2 to 64 MHz selectable in the bitstream; CCLK is a dedicated pin (pin 12), MOSI (6), MISO (13), SPICSN (8) are reusable as user I/O after configuration (p.51-54, p.69-70) |

The external-trigger notes in "Changes" section 2 now quote these VIH/VIL values:
the table above confirms 1.9 V / 0.8 V for LVCMOS33, and the 1 kOhm series path is within the input ratings for a 3.3 V source.
