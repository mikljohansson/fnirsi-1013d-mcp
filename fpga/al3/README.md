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

### 2. Calibration-pin circuit and what EXT needs (2026-10-18)

Status: partly documented, partly inferred. The bench measurements below settle the open part.

**What the schematic shows.** Source: pecostm32's EasyEDA file, read-only reference `D:\src\FNIRSI-1013D-1014D-Hack\Schematics\1013D\EASYEDA\SCH_Scope 1013D_2023-01-07.json`, sheet "Scope_Data_Acquisition". I traced it with a netlist script, not by eye.
- FPGA U3 pin 133 connects to R21 (1K, 0805, pin 2). R21 pin 1 ends at the net port `1KHZ_CALIB_SIG`.
- That net port occurs **once** in the whole file, on no other sheet. The schematic does not draw the path from R21 to the front-panel terminal at all.
- On the schematic, R21 is the only part on the net. No capacitor, diode, TVS or pull resistor is drawn on the FPGA side of R21.
- R13 to R20 (1K) on pins 121 to 132 are the relay and AC/DC drives, so R21 is the ninth of the same series-1K row. The board photo `Pictures/New version/fpga and adc's.jpg` shows that row of nine 0805 parts beside the FPGA's top-right pins. I cannot read the markings in the photo.
- The terminal itself is the round metal post between the two BNC connectors (`Pictures/New version/Analog section new version.jpg`, right edge). That is an inference from the photo. The traces between the resistor row and the post run under the shield and cannot be followed in the photos.

**What the bench says.** Coordinator, 2026-10-18, generator on (0x1532, 1 kHz, 50 %):

| Instrument | Reading at the probe-comp terminal |
|---|---|
| DMM, DC | 0.31 V |
| Scope, DC coupling | -1.2 V to +1.9 V |

A DC-coupled 0/3.3 V square at 50 % duty would read about 1.65 V on a DMM and 0 to 3.3 V on the scope. The swing of about 3.1 V peak-to-peak is the full logic swing. But it is centred near 0.35 V instead of 1.65 V.

**Inference: there is a series coupling capacitor between R21 and the post.** The undrawn part of the net is the likely place for it. I see no other way a full-swing square loses its DC level while keeping its amplitude.
- At 1 kHz the plateaus look flat, so C times the load must be much longer than 0.5 ms. With a 1 MΩ scope input that means C is above about 10 nF. This is an estimate.
- The +0.35 V residual centre is not explained by a pure series C into 1 MΩ, which would centre at 0 V. Candidates are a bias network on the terminal side, diode leakage, or the scope's own offset error at that range. Unknown.

**(a) What an external signal must look like to register as EXT.** The pad is LVCMOS33. I assume the usual LVCMOS33 thresholds, about VIH 2.0 V and VIL 0.8 V, but I have not checked them in an AL3 datasheet. P133 now has the FPGA's internal pull-down; its value is not documented in TD's files and is probably tens of kΩ.
- **If the path is DC-coupled (no C).** The source drives the pad through R21 (1 kΩ) against the pull-down, so the loss is small. Any 3.3 V logic output with a source impedance up to a few kΩ registers. Levels and both edges work, and 0x28 bit0 follows the level.
- **If a series C exists (the likely case).** Only edges pass.
  - A rising step of V at the post lifts the pad by about V (C is much larger than the pad's few pF). The pad then decays to 0 V through the pull-down with time constant C times R_pulldown. With C = 10 nF and R_pd = 50 kΩ that is about 0.5 ms. These values are illustrative.
  - A falling step drives the pad below ground. The pad's clamp diode catches it, with R21 limiting the current. Then the pad sits at 0 V.
  - Consequences:
    1. **Rising-edge EXT works**, provided the step is at least 2 V plus margin. A 3.3 V logic edge qualifies.
    2. **Falling-edge EXT does not follow the source's falling edge.** It fires when the decay crosses VIL, about C x R_pd x ln(3.3 / 0.8) after the preceding rising edge, or not at all for short gaps. Use rising edge, and invert at the source if needed.
    3. **0x28 bit0 shows no static level.** For "EXT present", use bit2 (edge seen during the capture).
    4. Very slow edges, with rise times near C x R_pd, are attenuated.
  - The internal pull-down is what makes this work: it re-references the coupled node to 0 V. With `PULLTYPE NONE` the node would float, and edges would ride on an undefined DC level.
- **Source impedance.** Any logic output (tens of Ω) is fine. A source above a few kΩ loses edge amplitude into the pad capacitance and the pull-down.
- **Pulse width.** At least 10 ns at the pad (majority filter), plus whatever the coupling stretches or shortens.

**(b) ESD and overvoltage risk to the pad.**
- **R21 is the only drawn protection.** There is no TVS or external clamp. The pad's internal clamp diodes take any excursion beyond about -0.6 V or 3.3 V + 0.6 V, and R21 limits the current. These are estimates, assuming 0.6 V diode drops and ideal sources:
  - 5 V logic: about (5 - 3.9) / 1 kΩ = 1.1 mA DC through the upper clamp if the path is DC-coupled. A series C turns that into a short spike.
  - 12 V step: about 8 mA peak.
  - Generator on while an external source drives the pin: the two fight through R21, at most about 3.3 mA. Neither side should be damaged. The firmware turns the generator off when EXT is selected.
- **I found no AL3 limit for clamp current.** Most FPGA vendors allow single-digit mA per pin at most and forbid sustained injection. Treat anything above 3.3 V as out of spec, even through 1 kΩ.
- **The real hazard is high voltage.** A pulse-induction TX node can fly back to hundreds of volts. Never connect the probe-comp terminal to the coil, the MOSFET drain or the damping resistor. A series C would pass such an edge almost in full, and 1 kΩ is no protection at 300 V: about 300 mA peak into the clamp diode. The correct tap is the detector MCU's 3.3 V gate-drive or sync GPIO, before the gate driver.
- **ESD.** The post is exposed. 1 kΩ plus the internal diodes is modest protection against a human-body discharge. Touch a ground first and keep cables plugged in before power-up. An external 3.3 V TVS or a 1 kΩ + BAT54S clamp on the source side is cheap insurance for regular use. This is a recommendation, not something the board has.

**(c) Recommended bench tests**

0. **Circuit, before any flashing, scope off.** These need only a DMM, on the current bitstream.
   - Ohms from the post to GND and from the post to 3V3. MΩ or open points to a series C, with the DMM seeing only the C. A few kΩ points to a bleed resistor.
   - If the R21 end at the FPGA side can be reached, ohms from the post to that end. About 1 kΩ means DC-coupled; open means a series C.
   - If the DMM has a capacitance range, the capacitance from the post to the R21 FPGA-side pad.
1. **Coupling with the scope's own generator, 0x1532, generator on.**
   - Set the generator to 1 Hz and then 10 Hz at 50 %. Use cmd 0x51 = 86 for about 1 Hz; f = inc x 50 MHz / 2^32.
   - CH1 on the post, x1 probe, DC coupling, 1 V/div, 50 to 200 ms/div. A DC-coupled path shows flat 0 / 3.3 V plateaus. A series C shows a spike on each edge that decays toward the bias. The decay time constant is C x (R_load || R_bleed).
   - Repeat with the probe at x10 (10 MΩ). If the time constant grows about tenfold, the scope input is the only load and C = tau / 1 MΩ.
2. **EXT after flashing 0x1533 with fw 0.2.9, generator off.**
   - Source: the detector MCU's 3.3 V GPIO, toggling at 100 Hz and 1 kHz, 50 % duty. Wire it to the post and to CH1 through a 1 kΩ series resistor, with grounds joined.
   - If only the bench supply's TTL output is available and it is 5 V, put a 2:1 divider in front, for example 1 kΩ / 1 kΩ, so the post sees at most 2.5 V. That is still above VIH. Never connect 5 V directly.
   - Expected if DC-coupled: rising and falling EXT both trigger on the CH1 edge, and 0x28 bit0 follows a slow 1 Hz toggle.
   - Expected if a series C exists: rising EXT triggers on the CH1 rising edge. Falling EXT triggers late or not at all, as described in (a). 0x28 bit0 stays mostly 0, and bit2 is set after each capture.
   - Either way, record the skew between the trigger point and the CH1 edge at 200, 10 and 1 MSa/s. The FPGA side predicts 20 to 25 ns.
3. **Open post, EXT selected, normal mode, generator off.** Expect no triggers.

Firmware and MCP texts say "3.3 V logic". If test 0 or 1 confirms a series C, they should add "rising edge recommended, levels are not seen". That is a documentation change only; the FPGA design does not change.
