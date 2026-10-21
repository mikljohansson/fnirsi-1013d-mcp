# fpga/al3 - Anlogic AL3A10LG144C7 project

Status 2026-10-19: **TD 4.6.8 SP1 runs headless in WSL and rebuilds Atlan4's design byte-identical to his shipped
`.bin`** (see "Rebuild results", setup in `TD_SETUP.md`). `src/` carries our changes up to version word 0x1537:
external trigger (0x1533), decimation filter (0x1534), compensation FIR (0x1535), segmented capture and hardware
averaging (0x1536), deterministic record start and A/B order (0x1537); section "Changes" at the end. Atlan4's original stays buildable with `SRC_REV=a947e78`. Nothing
has been flashed from this folder.

## Contents

| Path | What |
|---|---|
| `src/` | Atlan4's AL3 TD project, sources only: `zaklad.v` (top `fnirsi_1013D`), `zaklad.adc`, `zaklad.sdc`, `FPGA.al`, `al_ip/` (PLL and sample-memory IP: `.v`, `.ipc`, sim models), plus `FPGA commands.txt` (Atlan4's command table, updated). Copied from `D:\path\to\Fnirsi1013D\Verzia 1.xxx\FPGA for 1.xxx\FPGA AL3\FPGA AL3S10\` (unmodified at commit a947e78, version 0x1532). `zaklad.v` and `zaklad.adc` now carry the changes listed in "Changes" (version 0x1533). Build outputs (`*.db`, `*.area`, `*.timing`, `*.bit`, `*.bin`) were left out. |
| `tb/` | iverilog testbenches and reference models: `run.sh` (nine steps, `ONLY=ext/decim/fir/long/seg/trigpos/contract/encphase`), `tb_ext_trigger.v`, `tb_decim.v`, `tb_fir.v`, `tb_long.v`, `tb_seg.v`, `tb_trigpos.v`, `tb_seqcontract.v` (+ its transcript `contract_0x1538.txt`), `tb_encphase.v` + `encphase.sh`, `edge_phase.py`, `cic_model.py`, `seg_model.py`, `mutate_seg.py`, `sim_models.v` (behavioural PLL, sample memory, `AL_LOGIC_BRAM`). See "Changes". |
| `out/` | Named bitstreams for the bench, gitignored (`*.bin`). Hashes are recorded in "Changes". |
| `reference-stock/` | pcprogrammer (pecostm32) clean-room re-implementation of the **stock** FNIRSI design (version 0x1432), TD 5.0.28716 project. Sources only (`source/`, `al_ip/`, `Original_1013D.al`); his prebuilt `.bit` was left out. Licence GPL-3.0 (`LICENSE-GPL3`, repo `pecostm32/Anlogic_AL3-10_Analyzing`). |
| `dist/` | The released bitstreams 0x1533 onwards, committed: `al3-0x15xx.bin` (raw bitgen output), `manifest.json` (sha256, padded-image sha256, source commit, minimum firmware, status), `BITSTREAMS.md` (the page attached to each release: which to use, how to flash, rollback). `tools/bitstreams.py check` runs in CI; `release.yml` packs them. Add a version there once it is pinned and benched. |
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

- **Verified route (maintainer, 2026-10-18, took a few minutes):** create an account at anlogic.com, then
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
  Atlan4 ships an STM32F103 clone of `anlogic-usbjtag` for EF2. The maintainer's unit reads JTAG IDCODE 0x18006C31.

### Recommendation

1. **Primary: TD 5.0.3 Linux under WSL Ubuntu with Donwulff's headless flow** (`build.sh` here). Agents can build without a
   GUI, the flow has a published end-to-end validation on this exact AL3 project, and WSL Ubuntu is already the project build host.
2. **Fallback: TD 5.0.4 Windows MSI** on this PC, GUI project `src/FPGA.al`. Donwulff also built the project this way.
   Batch use of the Windows `td.exe` with a Tcl script is plausible but I did not verify it.
3. Open tools: no.

Both options need a valid licence file, which is the only real blocker. Atlan4's exact TD 4.6.8 SP1 is not required:
Donwulff's TD 5.0.3 rebuild met timing on `clk_200MHz` where Atlan4's shipped 4.6.8 report did not, and produced a
same-size 282,898-byte `.bin`. Expect the `.bit` to differ by a few bytes, and the ADC encode skew to be build-dependent.

### What the maintainer must do personally (licence)

1. Register a personal account at `https://www.anlogic.com` (English site, Support -> Tools & Downloads) and complete the
   profile. Registration uses the maintainer's own contact details, so agents must not do it.
2. In Tools & Downloads open the "TD License" entry and download `license.zip`. Read the expiry date in the `.lic`
   text (the `FEATURE` line) and the `HOST_ID` line.
3. If the licence or the TD 5.x/6.x downloads show "permission is insufficient", email `web@anlogic.com` with the use case
   (hobby work on an AL3A10LG144C7, TD 5.0.x for Windows or Linux) and the registered phone number.
4. Optionally download TD 5.0.3 Linux and 5.0.4 Windows from the Sipeed station
   (`dl.sipeed.com/shareURL/TANG/Primer/IDE`); that needs no account.
5. Hand over the TD zip or MSI and the `.lic`; it goes to `<TD>/license/Anlogic.lic`. Note the expiry and renew in time.

Open question to settle with Anlogic: whether the licence covers AL3 devices, and for how long.

## Headless build flow (TD 5.0.3 draft; the working 4.6.8 flow is in TD_SETUP.md)

One-time setup in WSL Ubuntu (`wsl -d <distro>`), following Donwulff's recipe in `D:\path\to\FNIRSI_1014D_Firmware\FPGA_NOTES.md`:

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

Build, from `/mnt/d/path/to/fnirsi-1013d-mcp/fpga/al3` (use a WSL-local build directory, `/mnt/d` is slow):

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
`bitstreams/atlan4-FPGA-0x1532.bin` and follow the flashing gates in `AGENTS.md`.

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
| Trigger stop | Stops when triggered; auto mode stops on time-out | Stops when triggered and `addr_delta >= total - pretrigger`; auto time-out (untriggered only since 0x153B) sets the trigger address to pretrigger (0x1532..0x153A) or to W - total + pretrigger, W the address after the last word (0x153B, section 11) | Different amount of post-trigger data. |
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
- Local: `research/notes/fpga-verilog-analysis.md`, `research/notes/repo-donwulff.md`, `D:\path\to\FNIRSI_1014D_Firmware\FPGA_NOTES.md` (TD rebuild sections), `D:\path\to\FNIRSI_1014D_Firmware\fpga\AL3_1014D\build.sh` and `build.tcl`

## Rebuild results (2026-10-18, TD 4.6.8 SP1 V4.6.116866, WSL Ubuntu 24.04)

Command: `wsl -d <distro> -- bash /mnt/d/path/to/fnirsi-1013d-mcp/fpga/al3/build.sh`. Sources unchanged.
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
  EXT gives the same trigger_address. In auto mode the time-out gives trigger_address = pretrigger (0x153B: W - total +
  pretrigger, section 11). Source 3 acts as EXT.
- Phase 3: after the EXT tests, a resync capture and 10 more random captures. Again 0 mismatches.
- Result: 38 checks, 0 failed, on seeds 12345, 1, 2, 3, 777, 4242, 99991 and 31337.
- Mutation check: 4 deliberately broken variants all fail the testbench. They removed the source gate on EXT, the
  EXT gate on the ADC trigger, the write-clock alignment, and the glitch filter. A fifth changed the ADC channel bit
  only for sources 2 and 3. It is behaviourally equivalent and passes.

**Bench checks** (maintainer / bench session; flash only with firmware that accepts 0x1533):
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

**Measured** (bench session, 2026-10-18; fw 0.2.11, FPGA 0x1532, CH1 x1 probe on the post, DC coupling, generator on):

| Setting | Result |
|---|---|
| Generator at 0.1 Hz, 9 s window at 333 S/s (`backups/calcap_CH1_0.1Hz_div599999.json`) | Both levels hold for more than 4 s with +0.002 V droop. Low -1.09 V, high +2.01 V, swing 3.10 V, noise 0.05 V rms. |
| 1 kHz, DMM DC, post to probe ground | 0.31 V. The mean of -1.09/+2.01 V is 0.46 V. |
| Earlier, shorted-input comparison | The low level is about 70 codes below the shorted input. |

No series capacitor: anything below about 1000 µF into the 1 MΩ input would droop visibly in 4 s. The swing is the full
logic swing, offset by about -1.1 V relative to the probe ground.

**Schematic** (pecostm32 EasyEDA file `D:\path\to\FNIRSI-1013D-1014D-Hack\Schematics\1013D\EASYEDA\SCH_Scope 1013D_2023-01-07.json`,
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
0. **Maintainer's DMM tests** (already requested; predictions from the schematic):
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

**Bench checks** (maintainer / bench session; needs firmware that knows 0x1534, and never flash without the verified stock dump):
1. K = 0 regression: as for 0x1533, including EXT. The ADC capture path now has a 2-input mux in front of the BRAM, and
   the netlist was re-placed. Look for glitches on a 10 to 30 MHz sine at 200 MSa/s.
2. Capture window: K = 1, D = 1 (N = 2, 100 MSa/s) on a slow ramp or a 100 kHz sine. Compare out / 2 with the K = 0 A
   and B values; there must be no isolated spikes. Repeat with 0x53 bits 3 / 4 set and keep the cleanest setting.
3. Core order: K = 3, N = 2 or 4 on a 20 to 30 MHz sine. The amplitude should follow the droop table for swap = 0. A
   wrong order shows as excess attenuation and distortion; then set bit 2.
4. Alias rejection (F1): generator 50 MHz / 13 = 3.846 MHz square (0x51 = round(2^32 / 13) = 330382100), 1 MSa/s,
   K = 0 vs K = 1 vs K = 3. Measure the 153.8 kHz alias with the host FFT. The model gives +2.4 / -26 / -83 dBV
   for a 3.1 V p-p square; expect at least 15 dB less with K = 1. The ADC spurs and the front end set the floor
   long before -83 dBV.
   Use a frequency with 50 MHz / f an integer. Otherwise the DDS edges, quantised to its 20 ns clock, form a pattern
   that repeats at a low frequency, and the generator output then contains a real in-band tone. A 900 kHz square,
   for example, carries a 100 kHz component at about -37.5 dBV, which explained the bench residual of 2026-10-18
   (`tb/square_alias.py`). 3.9 MHz has the same problem.
5. Resolution: shorted input, K = 1 and 3 at N = 1000. The rms noise in ADC LSB should fall roughly as the noise-bits
   column predicts.

### 4. Design study: CIC compensation FIR (built as 0x1535, section 5)

Maintainer direction: the default acquisition mode should become CIC plus a compensation FIR. 0x1534 stays frozen; this
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
- **Overshoot against edge phase, and the "horns" that grow and snap back** (fpga-gen6, 2026-10-19, bench report: the
  1 kHz calibration square shows horns with any +FIR filter that grow and snap back at about 3 to 4 Hz, clean with the
  CIC alone).
  - **Method.** `tb/edge_phase.py step | beat | fft`, numpy, in WSL. It uses the bit-exact model of `cic_model.py`
    (boxcar^K CIC, shift and clamp, the 15 FIR taps with their rounding and clamp). The input is the 200 MSa/s ADC
    stream of an edge through a first-order front end (tau 5.3 ns, about 30 MHz), rounded to 8 bits. The edge is swept
    over one output period in 200 steps (5 ns at 1 MSa/s, 0.5 ns at 10 MSa/s).
  - **Overshoot.** The 64 -> 192 step shows the shape without clamping, in 8-bit codes:

| Filter | 1 MSa/s (N 200): overshoot over the phases | 10 MSa/s (N 20) | ringing below the top after the peak | settling to +-1 code |
|---|---|---|---|---|
| boxcar, CIC2, CIC3 | 0 at every phase | 0 | 0 | 1 output sample (CIC3: 2) |
| boxcar + FIR | 0.6 .. 6.7 % (0.8 .. 8.5 codes) | 0.6 .. 6.6 % | 0.4 .. 1.9 % | 3 output samples |
| CIC2 + FIR | 0.45 .. 6.6 % (0.6 .. 8.4 codes) | 0.5 .. 6.5 % | 0.3 .. 1.7 % | 3 |
| CIC3 + FIR | 0.45 .. 6.9 % (0.6 .. 8.8 codes) | 0.4 .. 6.8 % | 0.3 .. 1.8 % | 3 |

  - **Pre-shoot.** The same size appears below the base before the edge, because the FIR is symmetric. With a full-scale
    0 -> 255 step the pre-shoot clamps at code 0 (the FIR output clamp), and the overshoot is 0.46 .. 6.6 % (1.2 .. 16.8
    codes). The 16-bit word keeps that peak; the S + 1 headroom covers it.
  - **Phase dependence.** CIC2 + FIR at 1 MSa/s, overshoot % at edge phase 0, 1/8, ... 7/8 of the output period:
    3.1, 4.7, 5.8, 6.5, 6.6, 5.4, 2.4, 1.3. The 6-9 % above is the peak of the continuous step response; what a capture
    shows is that response sampled at the edge's phase on the output grid.
  - **Gibbs ringing, not a stage artefact.** The chain is linear and time-invariant up to the final decimation (CIC by
    N/2, then the FIR, then keeping every second FIR output). Output sample m is therefore exactly the composite step
    response at m T - te. The variation is the sampled ringing of that composite response. The CIC by N/2 has no
    overshoot of its own, and the decimate-by-2 adds no phase dependence beyond choosing the sampling instants. A CIC
    alone has a monotonic step response, so the edge only moves and no horn appears.
  - **The beat.** `fpga_set_generator_freq` writes inc = floor(f 2^32 / 50 MHz).
    - 1 kHz: inc 85,899, f = 999.995973 Hz, period 1,000,004.027 ns. Each period the edge advances 4.027 ns on the output
      grid, so it sweeps the whole 1 us grid of 1 MSa/s at **4.03 Hz**, in 20 ns steps of the DDS clock. Captured at any
      pass rate above about 8 /s, the horns grow over 0.25 s and snap back: the reported 3 to 4 Hz.
    - At 10 MSa/s the sweep is 40.3 Hz, seen aliased through the pass rate: for example 4.3 Hz at 12 passes/s, 0.27 Hz at
      10 or 20 passes/s.
    - A 10 kHz square sweeps a 1 MSa/s grid at 0.53 Hz, 100 kHz at 0.07 Hz.
  - **Spectrum.** 4096-point Hann FFT of the decimated square: 1 kHz at 1 MSa/s, and 10 kHz at 10 MSa/s (same harmonic
    geometry, because 4096 samples hold less than half a 1 kHz period at 10 MSa/s). 24 edge phases over one output
    period, 64 -> 192 codes. The table gives the spread of each odd harmonic's bin magnitude over the phases (median /
    90th percentile / max):

| Region | CIC2 | CIC2 + FIR |
|---|---|---|
| below fo/4 | 0.03 / 0.3 / 0.5 dB | 0.00 / 0.02 / 0.03 dB |
| fo/4 .. 3fo/8 | 1.4 / 2.9 / 3.4 dB | 0.1 / 0.4 / 0.6 dB |
| 3fo/8 .. fo/2 | 8.4 / 21 / 33 dB | 3.7 / 16 / 32 dB |

    (1 MSa/s and 10 MSa/s agree within 0.1 dB except the max near fo/2.) The hypothesis holds in form. fo / f_square is
    even (1000), so the image of odd harmonic h, at fo - h f, is itself an odd harmonic and lands in the same bin. Its
    phase relative to the wanted line rotates with the edge position, so the bin beats, most where the two have similar
    levels, near fo/2. **The CIC alone varies more than CIC + FIR** in every region: it lets more of the image through
    (alias rejection about 21 dB against about 34 dB with the FIR at the band edge). Below fo/4 the variation is
    negligible with either filter. The wag in the upper part of the spectrum is this image beat, sweeping at the same
    4.03 Hz on the 1 kHz DDS square at 1 MSa/s.
  - **For the default filter decision.**
    - +FIR gives a flatter passband and a steadier spectrum, but up to 6.9 % edge overshoot whose visible size depends
      on the edge phase.
    - The CIC alone gives clean edges and more image beat above fo/4.
    - A per-mode default could be: CIC for the time-domain view, CIC + FIR for the spectrum view. The 4.03 Hz flicker is
      specific to a DDS square at 1 MSa/s: its period is 4.03 ns longer than an exact multiple of the 1 us grid.
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
- Step 5, `tb_long.v` (about 5 minutes, `LONG_SCALE` percent to shorten): `decim_chan` and `fir_chan` free-running with a
  copy of the top-level strobe logic and an exactly periodic input (DC + tone + a noise table repeated every period).
  - Eight configurations, CIC and FIR, from N = 2 to the largest exact N. The bench case CIC3 at N = 200 runs
    100,000 outputs.
  - A second copy starts from random 48-bit state, with the first integrator just below 2^48 so that it wraps
    during the run.
  - Checks: the output repeats exactly every period, both copies agree, there is no saturation or clamp, and the
    period mean equals mean input times gain within 1 LSB16 (worst deviation seen: 0.8 LSB16).
  - A 47-bit wrap in one integrator and a saturating integrator are both caught. This was the 2026-10-18 check for
    the "wagging" CIC3 spectrum on the bench: no filter-state drift. That effect was the square's harmonic leakage
    and the DDS pattern tone (`tb/square_alias.py`).

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

**Bench checks** (maintainer / bench session; needs firmware that knows 0x1535 and 0x56; never flash without the verified stock
dump):
1. FIR off: repeat the 0x1534 checks 1 and 4 (section 3), plus 0x29 bits 6/5 = 0.
2. Passband: K = 3, N = 100 (2 MSa/s), FIR on. Sweep a sine from 50 kHz to 500 kHz (fo/4) and compare the amplitude
   with the FIR off: flat within 0.1 dB with the FIR, versus up to 2.7 dB droop without. The front end's own response
   cancels in the ratio.
3. Alias: N = 100 (2 MSa/s), generator 50 MHz / 26 = 1.923 MHz square (0x51 = round(2^32 / 26) = 165191050), K = 1,
   FIR off vs on. Read the 76.9 kHz alias with the host FFT. The model gives -25 dBV with the FIR off and -94 dBV with
   it on, so the bench should show the alias falling into the ADC's own spur floor.
   - Use 50 MHz / f an integer (see section 3, check 4). A 1.95 MHz square has a DDS pattern tone at 50 kHz, right on
     the alias.
   - The FIR adds rejection only for inputs near odd multiples of fo. Near even multiples (for example 3.85 MHz at
     1 MSa/s) the CIC's own N/2 nulls do the work, and the FIR and CIC paths give the same level.
4. Step: generator 1 kHz square at N = 200, K = 2, FIR on. Expect about 7 % overshoot and undershoot, symmetric
   around the edge (linear phase), with no clipping at the recommended shift.
5. EXT timing: EXT and CH1 driven from the same edge. The CH1 edge in the record should sit at trigger_address +
   0.5 + lag / Tout (+/- 0.5 sample) with the lag formula above.

### 6. Segmented capture and hardware averaging (version 0x1536, 2026-10-19)

Roadmap section 8 item 1. The design note with the reasoning and the firmware/MCP proposal is
`docs/plans/segmented-capture.md`. A sequencer restarts the unchanged capture logic after each record:
- **segment mode** stores each record in its own slot of the sample memory and logs a trigger offset and a
  40-bit timestamp per segment;
- **averaging mode** adds each triggered record (pretrigger included) into a 2048 x 64 accumulator.

With 0x57 = 0 (power-on), or 3, the design is cycle-identical to 0x1535; the testbench proves it against 0x1535
from git. A "word" below is one memory word, as for 0x0B/0x0C/0x14: one A/B pair per channel in legacy mode
(K = 0), one 16-bit sample per channel with K >= 1.

**Protocol** (also in `src/FPGA commands.txt`; multi-byte values high byte first). Write 0x57..0x5C while 0x01 = 1:
they are latched there, together with 0x0B, 0x0C and 0x53.

| Cmd | Dir | Meaning |
|---|---|---|
| 0x06 | read, 2 B | 0x15 0x36. |
| 0x57 | write, 1 B | bits 1..0 mode: 0 off (power-on), 1 segments, 2 averaging, 3 reserved (off). bit 2 circular segments. bit 3 raw averaging readout (live). Bits 7..4 write 0. |
| 0x58 | write, 2 B | segments (1 .. 8192 >> m) or averaging positions (1 .. 2048 / total), 12 bits. |
| 0x59 | write, 1 B | bits 3..0 m: segment slot = 2^m words, m = 5..12. |
| 0x5A | write, 2 B | N, records per position (1..65535, 0 = 65536). |
| 0x5B | write, 1 B | bits 4..0 S, averaging readout shift (live). |
| 0x5C | write, 4 B | hold-off, 5 ns units: wait after a segment's table write or a record's post-pass before the restart. |
| 0x5D | write, 1 B | bit 0 stop: no further restarts. An untriggered record in progress is abandoned, a triggered one completes. Cleared by 0x01 = 1. |
| 0x0A | read, 1 B | bit 0: after an arm in mode 1 or 2, the run is done (all segments, all averages, or stopped). Otherwise unchanged. |
| 0x2A | read, 8 B | status: byte 0 {7 done, 6 overflow, 5 running, 4 16-bit records, 3 raw, 2 circular, 1..0 mode of the run}; bytes 1..3 count; bytes 4..5 untriggered; bytes 6..7 {15..13 state, 11..0 next slot or position}. |
| 0x2B..0x32 | read, 1 B each | byte 0..7 of the 64-bit view word at the 0x1F read pointer (bits 10..0); each read advances the pointer, with the same one-strobe pipeline as 0x20..0x23 (dummy read after 0x1F). |

**Segment mode.**
- **Slots.** Slot s holds physical words s 2^m .. s 2^m + 2^m - 1. The capture logic runs on its usual virtual
  address. The slot is a ring of 2^m words, and the virtual address modulo 2^m is the word in the slot.
- **Per segment.** Each segment is a normal capture: pretrigger fill, trigger (CH1/CH2 level, EXT, or the auto
  time-out), post-trigger words. Then the sequencer:
  1. writes the table entry;
  2. waits the hold-off;
  3. restarts the capture logic at the next slot.
- **Rules.** `total <= 2^m - 2`, because 2 more words are written after the record window (measured in every run).
  `pre < total`. The hardware checks neither.
- **One-shot run:** done after 0x58 segments.
- **Circular run:** the slot index wraps until 0x5D. The overflow bit is set at the first wrap. At each restart
  the entry of the slot about to be refilled is cleared to 0 (invalid), so after a stop every valid entry
  describes complete data.
- **Capacity:** 2^m words x segments <= 8192.
  - In legacy mode: 128 segments of 128 samples per channel (m = 6) up to 4 of 4096 (m = 11).
  - In 16-bit mode: 64 of 128 (m = 7) up to 4 of 2048 (m = 11).
  - The full table for m = 5..12 is in the design note.
- **Table entry** (accumulator word s, read through 0x2B..0x32):

| Bits | Field |
|---|---|
| 39..0 | timestamp, 5 ns units: 2 (D + 1) per write clock edge (D = effective divider, 0 with K >= 1), from the first write clock edge after the release of 0x01 to the edge that latched trigger_address; 40 bit = 5,497 s |
| 52..40 | trigger offset in the slot: trigger_address mod 2^m (convention of 0x14) |
| 53 | 1 = triggered, 0 = the segment ended on the auto time-out (offset = pre mod 2^m up to 0x153A; 0x153B: (W - total + pre) mod 2^m, W the slot write address after the last word, so offset - pre .. + total is the newest total words) |
| 62..54 | 0 |
| 63 | valid |

- **Reading a segment.** Segment s, record index j (0 .. total - 1) is at physical word
  `s 2^m + ((offset - pre + j) mod 2^m)`.
- **Status.** The count is segments completed; untriggered counts the entries with bit 53 = 0.

**Averaging mode.**
- **Run structure.** Triggered record k goes to position k mod P (P = 0x58). It is added into accumulator words
  p total .. p total + total - 1.
  - After N records per position (0x5A), the run is done.
  - Untriggered records (auto time-out) are skipped and counted.
  - The first record of each position is written, not added, so no clear is needed.
  - Rules: P x total <= 2048, total <= 2046.
- **Lanes:**

| Records (0x53 at the arm) | Accumulator word | Exact up to | Readout lanes (16 bit each, bytes 0-1 / 2-3 / 4-5 / 6-7) |
|---|---|---|---|
| legacy (K = 0) | four 16-bit sums CH1A, CH1B, CH2A, CH2B, each clamping at 65535 | N = 257 | sum >> S in all four (S 0..15; 16+ gives 0) |
| 16-bit (K >= 1) | CH1 and CH2 as 32-bit sums; no overflow possible (65535 x 65536 < 2^32) | N = 65536 | L0 = min(65535, CH1 sum >> S), L2 = min(65535, CH2 sum >> S), L1 = L3 = 0 |

  - With 0x57 bit 3 (raw) the readout is the accumulator word itself.
  - S and raw act at readout time, so the record can be read again with another S.
- **Sample memory after the run.** It still holds the last record as a normal capture (0x14 its trigger_address).
  After a stop, the abandoned record keeps running in the background.

**Timing of a run** (measured by `tb_seg.v`; "word" = word period):
- **Re-arm time**, from the last word of a record to the trigger being enabled again, is about
  hold-off + pre x word + 1 to 3 write clocks + 50 ns.
  - Averaging: about total x 5 ns (the post-pass) + pre x word + 100 ns.
    Measured: total 20 / 400 / 500 at 10 ns words and pre 4 / 30 / 40 gave 0.24 / 2.4 / 3.0 us.
    Total 100 at 100 ns words and pre 10 gave 1.6 us.
  - Measured (hold-off 0 unless stated):

| Word | pre | hold-off | re-arm time |
|---|---|---|---|
| 10 ns (200 MSa/s legacy, or boxcar N = 2) | 6 | 0 | 130 ns |
| 10 ns | 30 | 1 us | 1.36 us |
| 20 ns | 4 / 12 / 20 | 2 us / 0 / 0 | 2.18 / 0.34 / 0.48 us |
| 40 ns | 10 / 20 | 0 | 0.56 / 0.96 us |
| 100 ns | 10 / 20 | 0 | 1.1-1.4 / 2.4 us |
| 320 ns (CIC3 + FIR, N = 64) | 6 | 0.5 us | 2.56 us |

- **Timestamp against the EXT edge.** The timestamp lies 22 to 32 ns after the EXT edge at 10 ns words (the
  EXT input latency of section 1). It lies up to word + 22 ns after the edge at slower rates, because the trigger
  latches on the next word edge. Seeds 12345 and 3, 56 EXT segments each.
- **Trigger offset against the write address at the edge:** that address - 1, +0 to +3 words. That is the 0x1533
  convention plus the EXT latency at fast rates.

**Design** (`src/zaklad.v`, blocks "Segmented capture and averaging", modules `seq_acc_ram`, `seq_mirror_ram`):
- **Restart.**
  - The sequencer runs on `clk_200MHz` and raises `seq_req`. Its sampled copy gives a one-write-clock pulse
    `seq_rst_c`.
  - `cap_rst = sample_system_reset | seq_rst_c` replaces the MCU reset in every capture register: write address,
    time base, write enable, trigger data, half-way, trigger read address, trigger address.
  - `cap_rst200` resets `sampling_triggered`, `trigger_detect_enable` and `ext_pending`. It acts on the
    `clk_200MHz` edge 7.5 ns after the restart edge, before the next write clock edge.
  - The decimator, FIR, divider, generator and `ext_activity` are never restarted.
- **Remap.** `sample_write_address_phys = (address & mask) | (slot base & ~mask)`. The mask is all ones unless the
  last arm was in segment mode. The slot base moves on at each restart, or goes back to 0 on the sequencer's
  wrap flag.
- **Timestamp.**
  - `ts_cnt` (40 bit) adds 2 (D + 1) per write clock edge.
  - `ts_cap` is taken one write clock late, from registered copies of the signals that latch trigger_address,
    so that `sampling_triggered` gains only one load.
- **Snapshots.** The sequencer reads write-clock signals only through snapshots taken 7.5 ns after a write-clock
  rise (`swc_rise_q`). They are settled by then (multicycle 2) and stable until the next rise.
- **Pipelined decisions.** Every sequencer decision uses registers only: increments and compares are precomputed
  every clock. That is what closes 200 MHz.
- **Record mirror.**
  - 2048 x 32 in the two EMB32K (`AL_LOGIC_BRAM`, `IMPLEMENT "32K"`). It takes the sample memory's write port
    signals (same `dia`, `cea`, address bits 10..0).
  - The post-pass reads it from `(trigger_address - pre) mod 2048`, one word per 5 ns, through a 7-stage pipeline
    into the accumulator: 16 EMB9K, 2048 x 64, registered read.
  - The legacy lanes saturate. The 16-bit lanes add the upper half with the carry one stage later.
  - The MCU reads the accumulator through the same port, address-muxed, and `seq_lat` latches it on every
    `i_mcu_clk` edge like the sample memory's read register.
- **MCU side.** 0x2B..0x32 reads also advance `sample_read_offset` (old firmware never reads them). 0x01 = 1 clears
  0x5D.

**Build** (TD 4.6.8 SP1, `./build.sh`; corner TT 1.10 V 85 C):

| | 0x1535 | 0x1536 |
|---|---|---|
| LUT / reg / LE | 3082 / 2716 / 3501 | 4445 / 3817 / 4959 (51 % of TD's 8640 LUT sites) |
| BRAM9K / BRAM32K | 32 of 48 / 0 of 2 | 48 of 48 / 2 of 2 |
| M18x18, pads, IOB regs | 2 of 3, 60, 13 OREG + 1 IREG | same |
| `clk_200MHz` | 4.676 ns, slack +0.324 ns | 4.718 ns, 0 errors, slack +0.282 ns. The worst path is the existing `sample_write_clock` toggle; the worst new path (post-pass address, sequencer counters) has +0.38 ns |
| `sample_write_clock` | 0 errors, +0.677 ns | 0 errors, +0.481 ns: the existing `sampling_triggered` crossing into trigger_address |
| `clkc[1]`, `clkc[0]` | 6 / 52 errors | 6 errors TNS -15.9 / 52 errors TNS -135.0: the same structural and SDC-artefact paths as before |

The first build had 81 `clk_200MHz` errors, down to -1.12 ns. They came from sequencer decisions computed
combinationally: wide compares, increments and state-decoded write muxes. Precomputing them fixed it.

**Bitstream** (`out/FPGA-seg.bin`; deterministic): two builds in different work dirs gave the same hash.

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-seg.bin` (= `build/FPGA.bin` of commit 27c0c75, = `backups/al3_seg_0x1536_4eaf6811.bin`) | 282,898 | `4eaf6811814c58e1f00e648b97e7a608bb5ca387fb036f21714e91667959b48b` |
| `backups/al3_seg_0x1536_4eaf6811_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `66866c84ad4f298c88cfc1072870d44e80832852b53fcbd926e01fcb473f7408` |

Header words as before (`cc55aa33 f0000006 18006c31`), user code 0x0056.

**Testbench** (`tb/run.sh`, step 6, `ONLY=seg`; `tb_seg.v` against 0x1535 from git, `REF1535_REV` e2aec2e; then
`seg_model.py check` on its log). Six steps now; the sixth takes 5 to 7 minutes (`SEG_P1=2` about 2 to 5).
- **Phase 1** (sequencer off): 12 random captures (`SEG_P1`).
  - Settings: K = 0..3, FIR eligible or not, trigger source CH1/CH2/EXT, generator and EXT noise. 0x57 is written
    with mode 0 or 3 and random flag bits, and 0x58..0x5D with random values. 0x2A is read (not compared).
  - Compared every ns: pins, capture registers, decimator, the memory write port (data, address, enable), every bus
    read (0x0A included), the whole memory after each capture. The sequencer must stay idle.
  - Result: 0 mismatches.
- **Phase 2**, 11 segment runs. Each logs every memory write (virtual and physical address), the restart edges, the
  EXT edges, the re-arm and record-end instants, the table read over the bus and the slots.
  - S1: legacy 10 MSa/s, 6 x 64 words.
  - S2: 200 MSa/s, 8 x 128, hold-off 1 us.
  - S3: hold-off 2 us against pulses every 0.6-0.75 us.
  - S4: circular 4 x 128, 7 pulses, stop.
  - S5: one-shot stopped after 3 of 8.
  - S6: CIC2 at N = 4.
  - S7: CIC2 + FIR at N = 20.
  - S8: CIC3 + FIR at N = 64.
  - S9: boxcar at N = 2, 16 x 32 words.
  - S10: auto time-out, 4 untriggered.
  - S11: CH1 level trigger. In every segment the record crosses the level at index pre + delta0, where delta0 is
    measured on a normal capture.
- **Phase 3**, 11 averaging runs. Each logs the record from the sample memory when its post-pass starts; the
  accumulator is read over the bus at several S, clamped and raw.
  - A1: N = 1, the mirror check.
  - A2: legacy N = 8.
  - A3: 3 positions x 5.
  - A4: a second run with other data.
  - A5: 300 x 255 clamps, 300 x 200 = 60000 exactly.
  - A6: CIC2 + FIR, 2 positions x 16.
  - A7: boxcar N = 2.
  - A8: auto mode, untriggered records skipped (3 to 5 per run).
  - A9: CH1 step: the averaged record crosses at pre + delta0.
  - A10: stop after 3 of 50.
- **Phase 4**: resync, 6 random captures, 0 mismatches.
- **Result:** 50 checks, 0 failed. `seg_model.py` checks all 22 runs (4,650 accumulator words read back, 67 table
  entries, about 10,100 logged writes) with 0 failures, on seeds 12345 and 3. Measured on the way:
  - exactly 2 words are written after a record window (hence total <= 2^m - 2 and total <= 2046);
  - trigger offset = write address at the EXT edge - 1, +0 to +3 words;
  - re-arm times as tabulated above;
  - averaging re-arm about total x 5 ns + pre x word + 100 ns.
- The four earlier steps pass unchanged against 0x1532, 0x1533 and 0x1534. Only their version checks moved to 0x1536,
  and `mkref` also renames `fir_chan`.

**Mutation runs** (`python3 -I tb/mutate_seg.py`, each mutant one textual change, `ONLY=seg SEG_P1=2`). 20 of 21 fail as they should:

| # | Mutation | Caught by |
|---|---|---|
| 1 | remap off (physical = virtual address) | CH1 trigger position per segment (S11), model remap |
| 2 | slot base does not advance | same |
| 3 | circular run never wraps | model remap (writes beyond the 4 slots) |
| 4 | trigger offset not masked to the slot | model table |
| 5 | timestamp taken at every edge, not only output edges | **not caught: equivalent** (see below) |
| 6 | timestamp without the one-edge delay of ts_d | model timestamp window (141 ns after the edge at 100 ns words) |
| 7 | `sampling_triggered` not reset at the restart | circular run never completes (S4) |
| 8 | `ext_pending` not reset at the restart | S4: segments trigger on a stale edge, run never completes |
| 9 | first record added to old contents | A5 (second run at 200 x 300 reads more than 60000) |
| 10 | legacy lanes wrap instead of clamping | A5 |
| 11 | 16-bit lanes without the carry into the upper half | model (16-bit sums) |
| 12 | post-pass start one word late | A9 trigger position in the averaged record, model |
| 13 | positions share one accumulator block | model (position base) |
| 14 | untriggered records accumulated | A8 |
| 15 | hold-off ignored | model hold-off check |
| 16 | slot about to be refilled not invalidated | model table, S4 |
| 17 | 0x0A = done for every arm (modes off unaffected, so equivalent there) | S1 status |
| 18 | 16-bit readout clamp missing | model (S = 0 readout) |
| 19 | post-pass ends before the pipeline drained | A5, model |
| 20 | 0x2B..0x32 reads do not advance the pointer | S4 table read |
| 21 | run ends after the first pass (N ignored) | A2 |

Mutation 5 is equivalent: with K >= 1 the trigger reaches `sampling_triggered` only right after an output edge (ADC and
EXT paths alike), so the last untriggered edge before a trigger is always an output edge. The `adv_d` term stays as
documentation of the intent. Mutants 7 and 8 do not fail a check directly; they hang their run until the poll limit,
which costs about 5 minutes each.

**Bench checks** (bench session with the maintainer; needs firmware that knows 0x1536; never flash without the verified
stock dump):
1. **Modes off.** Repeat the 0x1535 checks (sections 3 and 5): legacy at 200 MSa/s, CIC, FIR, EXT, plus
   0x2A byte 0 = 0. The sample memory address now goes through the remap LUT.
2. **Segments, generator loopback.**
   - Setup: generator 10 kHz square into CH1, trigger CH1 rising, legacy 200 MSa/s, m = 7, 32 segments, pre 16,
     total 120.
   - Every segment's record crosses the level at record index pre + 1 (or the 0x1533 convention found in T5).
   - Timestamp differences are 100 us +/- 10 ns.
3. **EXT segments.** EXT from a TTL source (section 2 wiring), 8 segments with hold-off 0. The timestamps match
   the source's pulse spacing. With a hold-off longer than the spacing, every other pulse is skipped.
4. **Circular and stop.** Circular run on a 1 kHz square, stop after about 1 s. The overflow bit is set, the count
   is about 1000, and the slot about to be refilled reads invalid.
5. **Averaging, noise.**
   - Shorted input, legacy 200 MSa/s, auto mode with the EXT pin pulsed, N = 1 and N = 256, S = 8.
   - The rms of the averaged record should fall by about 16 against N = 1, until the ADC's own patterns
     dominate.
   - The N = 1 averaged record must equal the sample memory's record word for word. That is the mirror capture
     check: the mirror BRAMs sit at a different place on the ADC buses.
6. **Averaging, 16-bit.**
   - CIC2 + FIR at N = 20, P = 2 positions, the generator square triggering CH1 on alternate edges (rising and
     falling through two runs).
   - The positions average separately, and the 16-bit readout at S = log2 N equals the single-shot shape.
7. **Pulse-induction case.** The detector's TX gate on EXT, 16-bit CIC at 20 MSa/s, total 1000, pre 50, N = 256.
   The run should take 256 pulse periods, with count 256 and untriggered 0. A longer run means pulses fell into
   the dead time (record + about 7.5 us here).

**Firmware support** (fw-seq, fw 0.2.58, protocol 1.17; `firmware/README.md` section 5
"Segmented capture and hardware averaging", `docs/protocol.md` section 14 row 56; not benched):
- caps bit 17 SEQ on 0x1536..0x153F; GET_STATUS bytes 44-45 report the features (EXT, DECIM, FIR, SEGMENTS, HW_AVERAGE).
- Arm order on 0x1536: 0x0B, 0x0C and 0x57..0x5C before 0x01 = 1 (fpga-gen6's finding: the write-clock registers
  load only on write-clock edges in reset), 0x0B / 0x0C again in the reset window; 0x57 = 0 whenever the FPGA may
  still hold another mode. Older bitstreams keep the old order.
- Host: SET_SEQ / GET_SEQ / READ_SEQ, READ_RAW addr_mode 3 (segment-relative), STOP = 0x5D then 0x1A = 0.
- UI: the acquisition menu's Average runs an averaging run of 2..64 records for legacy records that fit the
  accumulator (3000-sample depth); everything else keeps the software average.
- Bench plan with the flash session, the checks of this section and the rollback: `firmware/AGENTS.md` section 35.

**Firmware contract** (fpga-gen6, 2026-10-19; `tb/tb_seqcontract.v`, `run.sh` step 8, `ONLY=contract`). The testbench
replays, strobe by strobe, the sequencer byte sequences of fw-seg's protocol 1.17 firmware (fw 0.2.56+ branch, commit
65abebf; `pc_hal_arm`, `pc_seq_hw.c`, `h_read_seq`, `h_read_raw`) against the simulated FPGA. It writes every strobe to
a transcript, `tb/contract_0x1537.txt` (1,791 lines, deterministic; 0x1536 gives the same file except the version
byte). The firmware's host tests can log their HAL calls in the same format and diff them. Format, one strobe per line:
`C xx` command write, `W xx` data write, `R xx` data read (the byte a correct FPGA returns), `D xx` the discarded dummy
strobe after a pointer write, `# ...` step labels. Polling loops are labelled `# poll` and shown once, because their
length depends on timing. The sequences (multi-byte values high byte first):

| Step | Bytes |
|---|---|
| ARM 1 | K >= 1 or changed: 0x53, 0x56, 0x54, 0x55 as on 0x1535, then the settle wait |
| ARM 2-3 | 0x0F = 00; 0x1A = 01 (normal / single) or 00 (a sequencer run in auto mode: the FPGA's 0x0E time-out) |
| ARM 4 | 0x0B pre words, 0x0C total words, **before** the reset (avoids the 0x1532..0x1536 race of section 7) |
| ARM 5 | mode != 0: 0x57 = mode \| circular << 2, 0x58 count, 0x59 m, 0x5A N (65536 = 0000; segments: 0000, unused), 0x5B 00, 0x5C ceil(hold-off ns / 5). Mode 0: 0x57 = 00 only if the last value written was not 0 (the readout below writes 0x02) |
| ARM 6 | 0x01 = 01, poll 0x05 bit 0 (bounded) |
| ARM 7-8 | 0x0B, 0x0C again, 0x0E (4 B), 0x01 = 00 |
| Poll | 0x0A, then 0x18, every main-loop pass |
| Done | 0x2A (8 B), read again until two reads agree (at most 4); 0x0F = 01; 0x14 (2 B) |
| Table (READ_SEQ 0) | for b = 0..7: 0x1F = first & 0x7FF, cmd 0x2B + b, one dummy strobe, count reads (entry first + i, byte b) |
| READ_RAW addr_mode 3 | the entry: 8 x (0x1F = s, cmd 0x2B + b, dummy, 1 read); start = s 2^m + ((offset - pre + j) mod 2^m); per core 0x1F = start, cmd 0x20 + core, dummy, reads up to the slot end; on wrap 0x1F = s 2^m, cmd, dummy, the rest |
| Average (READ_SEQ 1) | 0x5B = S, 0x57 = 0x02 \| raw << 3 (outside the reset: S and raw are live, the mode of the run is latched), then per channel c one pass per byte: legacy 4c..4c+3, 16-bit 4c, 4c+1 (raw: 4c..4c+3) |
| STOP | 0x5D = 01, then 0x1A = 00, then poll 0x0A |

Scenarios and their checks (12 checks, all pass on 0x1536 and 0x1537):
- **S**: legacy segments at 20 MSa/s, CH1 rising, 4 segments, m = 6, pre 10, total 40. Expected: 0x2A byte 0 = 0x81,
  count 4, 0 untriggered; every entry valid and triggered; the step at record index pre + 1 in every slot; the last
  entry's offset = 0x14 mod 2^m.
- **SR**: READ_RAW addr_mode 3 of segment 2, both CH1 cores; equals the slot word for word.
- **A**: legacy averaging at 10 MSa/s, EXT, N = 4. 0x2A byte 0 = 0x82. Lane CH1A at S = 2 equals the record; raw = 4 x the
  record.
- **F**: 16-bit averaging, CIC2 + FIR at N = 20, CH1, 2 positions x 8. 0x2A byte 0 = 0x92, count 16. L0 at S = 3 equals the
  single record in both positions; the raw CH1 sum = 8 x the record.
- **P**: circular segments, stopped with 0x5D = 1 and 0x1A = 0. Overflow set, count 6, and the slot being refilled
  (entry 2) reads invalid.

Answer to fw-seg's question: rewriting 0x57 at readout time, outside the reset, is safe on 0x1536 and 0x1537. `q_mode` and
`q_mask` latch only while the synchronised reset is high; `seg_mask_s` loads only in the reset (0x1537: also on the first
write clock edge after a release). The readout view uses the latched mode and the live raw bit. The firmware must count
that write when it decides whether a later plain arm needs 0x57 = 00. Its tracker does.

### 7. Trigger position at decimated rates; version 0x1537 (fpga-gen6, 2026-10-19)

Task-queue item "Trigger position varies by run at decimated rates". The bench (2026-10-18, bitstream 0x1533, fw 0.2.21)
measured the CH1 edge against `pretrigger_samples` of host captures at 1 MSa/s (legacy, divider 199). EXT gave +11/+12 us
in the first 10 captures after EXT was selected, then -1.0 us in 17 captures. CH1 gave +2/+3 us in one run and -1.0 us
in another. Each value was stable within a run. Question: is the FPGA deterministic?

**Testbench** `tb/tb_trigpos.v` (`run.sh` step 7, `ONLY=trigpos`; `+runs=N` per case, `+pl=N` ADC pipeline, `+order=1`
writes 0x0B/0x0C before the reset).
- **ADC model.** A behavioural AD9288: each core samples on the rising edge of its encode pin and outputs the value `pl`
  encode cycles later (default 4, the datasheet's pipeline delay; `research/notes/hack-repo-hardware-fpga.md`). The
  testbench keeps each value's sampling instant, so every stored legacy sample has a known true time.
- **Stimulus.** A step on CH1 (40 -> 210, CH2 the complement) and the same step on the EXT pad (loopback), at a random
  instant after the pretrigger has filled.
- **MCU side.** The firmware's byte sequences: `pc_hal_arm`, `pc_dec_apply` with its settle wait, `pc_hal_acq_poll`,
  `pc_hal_acq_finish`, READ_RAW with the dummy strobe, and the readout without it. Strobe periods vary per capture
  (41, 97, 174, 298 ns).
- **Cases.** 27 cases in shuffled order: legacy at D = 0, 9, 199 and 399; K = 1..3 with and without the FIR at N = 200
  (1 MSa/s); CIC2 / CIC2+FIR / CIC3+FIR at N = 20 and N = 64; CIC2 at N = 4; boxcar at N = 2. Sources CH1 and EXT.
  Pretrigger 24, 36 or 48 words. Each run therefore starts at a random phase of the write clock, the decimation counter
  and the signal, and most runs follow a change of rate, filter or source.
- **Measured per run.** 0x14 over the bus against the register; the record read over the bus against the sample memory;
  the step's index in the record relative to the pretrigger written to 0x0B; for legacy, the edge time against core A's
  sample at word `pre`; for 16-bit, the time from the edge to the memory write of the crossing sample and the
  interpolated crossing.
- **Phase 2.** Directed checks of the configuration race (R1, R2 below).

**Results** (seed 12345, 8 runs per case, 216 captures; also seed 7 and `+pl=0`; all pass on 0x1537). Offsets are the
step's record index minus the trigger point: legacy in samples (2 per word), 16-bit in output samples. ns: legacy, edge
minus core A's sample at word `pre`; 16-bit, edge to the memory write of the crossing sample.

| Mode | N / rate | CH1 offset | CH1 ns | EXT offset | EXT ns |
|---|---|---|---|---|---|
| legacy | 200 MSa/s (D 0) | +1..+2 (A word +1) | 0.6..7.7 of 10 | +7 (word +4) | 30.8..34.3 |
| legacy | 20 MSa/s (D 9) | +1..+2 (word +1) | 5.6..76.5 of 100 | +9..+11 (word +5..+6) | 439..529 |
| legacy | 1 MSa/s (D 199) | +1..+2 (word +1) | 218..1765 of 2000 | +10..+11 (word +5..+6) | 9137..10945 |
| boxcar | 1 MSa/s (N 200) | +1 | 1004..1510 of 1000 | +1..+2 | 687..1626 |
| boxcar + FIR | N 200 | +1 | 4050..4941 | +5 | 4165..4968 |
| CIC2 | N 200 | +1 | 1195..2142 | +2..+3 | 1576..2116 |
| CIC2 + FIR | N 200 | +1 | 4318..5200 | +5..+6 | 4446..5102 |
| CIC3 | N 200 | +1 | 1829..2464 | +2..+3 | 1816..2547 |
| CIC3 + FIR | N 200 | +1 | 4594..5509 | +5..+6 | 4734..5461 |
| CIC2 / CIC2 + FIR | 10 MSa/s (N 20) | +1 / +1 | 268..354 / 705..783 | - / +7..+8 | - / 692..782 |
| CIC3 + FIR | N 64 | +1 | 1650..1962 | +6 | 1650..1929 |
| CIC2 | N 4 | +1 | 180..198 | +8..+9 | 180..197 |

With `+pl=0` (no ADC pipeline) legacy EXT moves to +2..+3 samples at 1 MSa/s and -1..-2 at 200 MSa/s. The 16-bit rows
change by under 0.1 sample.

**Findings.**
1. **The trigger detector, the decimator and the FIR are deterministic.** An ADC trigger on the 16-bit stream puts the
   crossing at the same record index (+1, the 0x14 convention) in every run, at every K, N and FIR setting. The time
   from the edge to the stored sample spreads by less than one output period. That is the sample-grid quantisation:
   the decimation counter is free-running and is never reset. EXT spreads by at most one output sample, for the same
   reason. The record start (0x14 - pre) is exact, and READ_RAW returns the record word for word.
2. **Record pointer race (0x1532..0x1536, fixed in 0x1537).** The trigger read address is preloaded from 0x0B only on
   write clock edges while 0x01 = 1. The firmware writes 0x0B/0x0C/0x0E after the 0x05 poll, then releases. If no write
   clock edge falls between the 0x0B write and the release, the previous pretrigger stays, and the record start is off
   by (old pre - new pre) words. A write clock period is 10 (D + 1) ns at K = 0: 2 us at 1 MSa/s, 20 us at 100 kSa/s.
   About 10 strobes lie between the 0x0B write and the release, so the race needs a changed pretrigger, a legacy
   divider and fast strobes. Phase 1 on 0x1536 hit it in 2 + 2 of 8 runs at D 199 (CH1 / EXT) and 6 of 8 at D 399; the record offsets
   ranged from -38 to +58 samples.
   - R1 (0x0B written right after a write clock edge, D 399, pre 30 then 50): on 0x1536 the step moved to pre - 5 and
     pre + 21; on 0x1537 it is at pre + 1 both times.
   - The segment slot mask (0x57/0x59) had the same race. R2 (segment run after a modes-off arm, configured in the same
     window): on 0x1536 segment 1 never reached slot 1; on 0x1537 it writes 47 words there.
   - Fix: `ssr_d` (the MCU reset one write clock later) also loads both on the first edge after the release.
   - Firmware workaround for older bitstreams: write 0x0B/0x0C (and 0x57..0x5C) before 0x01 = 1. fw-seg's protocol 1.17
     arm does so on 0x1536+.
3. **A/B core order flips (0x1532..0x1536, fixed in 0x1537).** The encode toggles (negedge `clk_ADC`) and the write clock
   toggle (negedge `clk_200MHz`, 1.25 ns earlier) sample the same combinational enable, `divider < counter`. A 0x0D or
   0x53 write that changes the compare between those two instants makes them disagree once. From then on, core B's
   sample of a word lies half a word before core A's instead of after it, until the next such accident. On 0x1536, in
   phase 1, the measured B - A offset flipped between +5 / -5 ns at 200 MSa/s, +-50 ns at 20 MSa/s and +-1000 ns at
   1 MSa/s from one configuration change to the next, and the legacy CH1 offset spread by 2 samples.
   - Consequence: the firmware's fixed interleave (B before A, PC_INTERLEAVE_ORDER 0) and the 0x53 swap bit are right
     only by chance after any rate or filter change. A wrong order gives a one-sample zigzag at edges and moves the edge
     by one sample. For K >= 2 it also mis-weights the polyphase prefilter.
   - Fix: the encodes take the complement of the write clock (`swc_enc_n`, a register copy) instead of their own
     complement. That is the same thing in steady state, so the pins are unchanged, and any disagreement is repaired
     at the next enable.
   - On 0x1537 the order is constant. In the model, core A is the earlier sample of each word (B - A = +half a word).
     That is the opposite of the 1.00o5 convention, so bench T4 should settle it on hardware.
   - Superseded by section 8: the register copy put the encode flops on a 1.25 ns path that the A flop missed by
     0.21 ns, and the B flops' met path gave the opposite order to 0x1535. 0x1538 restores B-first on both chips with
     timing met.
4. **The ADC pipeline delays legacy EXT captures by 4 words.** EXT triggers at the pin, while the data come out of the
   AD9288 4 encode cycles late. One encode cycle is one word at every legacy rate: 8 samples, 8 us at 1 MSa/s, 400 ns
   at 20 MSa/s. The firmware applies no EXT shift in legacy mode (`pc_glue_decim_pre_words` returns samples / 2 for
   K = 0, `pc_hal_scope.c:806-814`; `pc_decim_ext_shift` returns 0 for K = 0, `pc_decim.c:178`). So the edge lands
   +10..+11 samples after the trigger point instead of +1..+2. With the 16-bit filters the same 4 cycles are 40 ns at
   100 MSa/s per core, under 0.1 output sample. There the firmware's EXT shift leaves the edge at +1..+2, in line with
   the ADC trigger.
5. **The readout without the dummy strobe is one word late.** `fpga_read_decim_data` (`fpga_control.c:1591-1620`, the
   16-bit UI readout) and the 1.00o5 `fpga_read_sample_data` (`fpga_control.c:838-854`, the UI and READ_TRACE) return
   mem[S], mem[S], mem[S+1], ...: the first word twice, then everything one word late. READ_RAW (`pc_hal_read_core`,
   `pc_hal_scope.c:506-538`, dummy strobe, read_lag 0) is exact. This is checked in every run against the sim memory
   model, whose read port latches on `i_mcu_clk` like the TD block RAM; bench T3 confirms it on hardware.

**What explains the bench scatter** (inference; the FPGA side is settled by the simulation above, the attribution is not):
- **EXT +11/+12 us, then -1.0 us.** +11/+12 us is the legacy EXT position of finding 4 (+10..+11 samples at 1 MSa/s,
  plus 1 to 2 samples for the A/B order and the measurement convention). -1.0 us is where a CH1 level trigger puts the
  edge. A likely way the FPGA's source returned to CH1 while the host still assumed EXT is the 1.00o5 channel-enable
  handler. `fpga_swap_trigger_channel` (`fpga_control.c:454-484`, called from the channel menu, `statemachine.c:1846`)
  sets `triggerchannel` to 0 or 1 and writes 0x15 whenever only one channel is enabled. That overwrites EXT (2). A
  loopback square triggers on CH1 just as well, so the captures still looked fine.
- **CH1 +2/+3 us in one run, -1.0 in another.** Candidates:
  - the A/B order flip of finding 3 (one sample, constant until the next rate change);
  - the pretrigger race of finding 2 (whole words, when the pretrigger changed between a UI conversion and a host
    capture);
  - the one-word-late readout of finding 5, if the two runs used READ_TRACE and READ_RAW respectively.
  Each is stable within a run and changes between runs, as observed.
- **Re-measure on 0x1537** with GET_CAPTURE_INFO logged next to the measured edge (source, trigger_addr,
  capture_start_word, divider). Also log every 0x15 write.

**0x1537** (`src/zaklad.v`; the version word is the only interface change):
- `ssr_d`: 0x0B and the slot mask also load on the first write clock edge after the release of 0x01 (finding 2).
- The encode flops follow `swc_enc_n`, an inverted register copy of the write clock (finding 3). The copy keeps the
  write clock's own fanout, and with it the half-cycle path into `sampling_triggered`, as in 0x1536.
- Timing only, same behaviour:
  - `q_seg` registers `q_mode == 1`;
  - the post-pass start values (`pp_ma`, `pp_aa`, `pp_left`, `pp_z`) preload every clock while no pass runs, instead of
    loading on the Q_RUN exit. The first 0x1537 build missed `clk_200MHz` by 0.48 ns on that 36-fanout select.
- With configuration written outside the race window, 0x1537 is cycle-identical to 0x1536, except the encode pins
  after a reference bitstream's A/B flip. The equivalence steps (1, 2, 4, 6) therefore no longer compare the four
  encode pins (`out_cmp_mask`; their ADC stimulus does not depend on them). `tb_trigpos.v` checks the A/B order
  instead: core B half a word from core A, with the same sign in every legacy run. Tests:
  - `tb/run.sh` passes all steps on seeds 12345 and 3: the equivalence phases against 0x1532..0x1535 and the 0x1536
    segment and averaging checks with `seg_model.py`.
  - On 0x1536, `tb_trigpos.v` fails 36 of 882 checks at 6 runs per case: the A/B order in 23 legacy runs, 6 legacy
    cases' position spread, 5 records shifted by the pretrigger race, R1 and R2.

**Build** (TD 4.6.8 SP1, `./build.sh`, about 60 s; deterministic, two work dirs gave the same hash):

| | 0x1536 | 0x1537 |
|---|---|---|
| LUT / reg | 4445 / 3817 | 4569 / 3820 |
| BRAM9K / BRAM32K / M18x18 | 48 / 2 / 2 | same |
| `clk_200MHz` | +0.282 ns | 0 errors, +0.035 ns: the known half-cycle path `sample_write_clock` (negedge) to `sampling_triggered` (+0.03 to +0.7 ns across builds since 0x1533); next worst +0.316 ns |
| `sample_write_clock` | +0.481 ns | 0 errors (period 9.248 ns of 10) |
| `clkc[1]` / `clkc[0]` | 6 / 52 errors | 3 / 53 errors: the same structural enable crossing and SDC artefact |

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-seg-0x1537.bin` (= `build/FPGA.bin` of commit c15981e, = `backups/al3_seg_0x1537_426a3784.bin`) | 282,898 | `426a3784883b82c9c9f42870584e64d1d082cdcb38c8b62db143e161e2633b17` |
| `backups/al3_seg_0x1537_426a3784_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `febc92997ee6605090d5581aabc9c240986fa82defdc24d40aca01566dbfe1f1` |

Header words as before (`cc55aa33 f0000006 18006c31`), user code 0x0056. **Not flashed.** A firmware that knows 0x1536
runs it unchanged (AL3 family decode, protocol.md section 5.1).

**Bench checks** (with the maintainer; never flash without the verified stock dump). 0x1537 was flashed on 2026-10-19 and
showed the CH2 A fault of section 8; run these on 0x1538 together with section 8's checks:
1. T3 / T4 / T5:
   - the dummy-strobe lag;
   - the A/B order: core B earlier on 0x1538 (section 8); check that it stays the same across rate changes;
   - the CH1 edge position against GET_CAPTURE_INFO, at 1 MSa/s legacy and with CIC2 + FIR.
2. EXT loopback at 1 MSa/s legacy: the edge at about +10 samples (finding 4) in every capture; then toggle a channel in
   the UI and watch whether 0x15 leaves EXT.
3. Slow legacy rate (100 kSa/s), host captures alternating two pretriggers: the edge stays at pre + 1..2.

### 8. Encode timing of the A cores; version 0x1538 (fpga-gen7, 2026-10-19)

**Bench** (fw 0.2.60/0.2.61, 0x1537, READ_RAW 1000 samples per core at divider 0, inputs open):
- CH2 core A is bad in about 40 % of the host captures: std 14 to 35 codes, with +-60 excursions. CH1 and CH2 B are clean
  (std about 0.5).
- 1221 back-to-back captures over 60 s with no 0x0D / 0x53 / 0x56 write (GET_EVENT_LOG type 22) were bad in 26 to 43 of
  every 81. There was no time course and no dependence on any write: each arm is a coin toss.
- The CH2 A-B mean difference is -3.0 to -3.3 codes on 0x1537, in clean and bad captures alike, against about 0 on 0x1535.
  CH1 reads the same on both. The calibration wizard's zero step fails CH2.
- On 0x1535 it was CH1 core B that came and went (finding 3 of section 7).

**Cause** (TD's timing report of the 0x1537 build, re-run with `report_timing -ep_num 40` on `FPGA_pr.db`):
- 0x1537 loaded the encode flops (falling `clk_ADC`) from `swc_enc_n`, a flop on the falling `clk_200MHz` edge.
  `clk_ADC` lags by 90 degrees, so that path has 1.25 ns.
- The A flops take `~swc_enc_n` through a LUT. Endpoint `lt0/ucout|lt0/u31.mi[0]` has an arrival of 4.808 ns against a
  required 4.599 ns: **slack -0.209 ns** at TT 1.10 V 85 C. The B input (`mi[1]`, no LUT) passes with +0.45 ns.
- So the A encode loads the new value (designed), the value from one clock earlier (A sampling together with B), or
  goes metastable. Which one it does depends on the silicon, temperature and supply, and it settles anew from capture
  to capture.
- Which chip shows it depends on its encode route and capture margin. On this unit it is CH2: the A-B mean shift is the
  "with B" case, the noise the metastable case. That attribution is an inference; the report does not name the chip.
- The "write race" reading of the brief does not hold for 0x1537. `swc_enc_n` and `sample_write_clock` sit in one slice
  (`swc_enc_n_reg|sample_write_clock_reg`), and the encodes at divider 0 are reloaded every clock and keep no state of
  their own. The bench run without writes agrees.
- The CE input of the same flops (the 32-bit compare) arrived -2.45 ns late. It was always taken one `clk_ADC` edge
  later: that is the "structural" clkc[1] error listed since 0x1533.
- The order follows from the same report:
  - 0x1532..0x1536: the encodes toggle on that late CE. So in normal running they toggle one `clk_ADC` edge after the
    write clock, in phase with it. That is encode convention 1 below, and core B is the earlier sample (the 1.00o5
    convention). Only a write that hit the window between the two clocks flipped it (finding 3).
  - 0x1537: the B flops met their 1.25 ns path, so encB = NOT write clock. Core A was earlier on chip 1, as the 0x1537
    model said, and chip 2's A was late or metastable.

**Simulation** (`tb/tb_encphase.v`, `tb/encphase.sh`):
- The firmware's UI arm (0x0D, 0x53/0x56 when the cached mode changed, 0x0F, 0x57..0x5C, 0x01 = 1, the 0x05 poll,
  0x0B, 0x0C, 0x0E, 0x01 = 0) and host arm (raw 0x0D, 0x17, then the same). The first strobe of each pair is swept in
  0.1 ns steps over the 5 ns clock, strobes 41..298 ns.
- UI dividers 0, 1, 2, 3, 9 and 99 with K = 0 or 2, then the host at divider 0.
- After each arm, every rising `clk_200MHz` edge checks encB = write clock and encA = its complement.
- `encphase.sh` also builds variants whose encode flop inputs come through a delay (iverilog has no setup check, so a delay
  past the window means "the value from one clock earlier"). 50 phases each:

| Design | Result |
|---|---|
| 0x1535 (e2aec2e), zero delay | polarity changes in 22 of 50 host captures (the old A/B flip; the zero-delay model sees the CE in time, which the silicon does not) |
| 0x1537 (c15981e), zero delay | constant, encB = NOT write clock on both chips (convention 0) |
| 0x1537, CH2 A input 1.45 ns | CH2 A = CH2 B on every check (A samples with B): the bench's clean-but-shifted state |
| 0x1538, zero delay and all four inputs 3.4..3.5 ns | convention 1 on every check of every pin, 0x33 = 0xF6 after every capture |
| 0x1538, CH2 A input 3.9 ns (past its 3.75 ns window) | caught: 0x33 bit 0 set, pin checks fail |

**0x1538** (`src/zaklad.v`; RTL in the "Sample write clock" block, read 0x33 in the read mux):
- `sce_q`: the sample clock enable one clock ahead, as a register (`enable ? divider == 0 : divider <= counter`). It equals
  the combinational enable whenever the divider is stable.
- `swc_sh`: a copy of the write clock loaded from it (`swc ^ enable`), so a difference lasts one clock. It keeps the encode
  loads off the write clock; with them on it, the half-cycle path into `sampling_triggered` missed by 0.17..0.32 ns in
  four trial builds (one of them also failed 35 sequencer paths).
- `enc_nxt` / `enc_nxt_n` on the rising `clk_200MHz` edge: `swc_sh ^ sce_q` and its complement, i.e. the write clock after
  the next falling edge.
- The encode flops load them on every falling `clk_ADC` edge with no enable. Paths: 2.5 ns into `enc_nxt`, 3.75 ns into the
  encode flops, no LUT in front of them.
  - Result: encB = write clock and encA = its complement, 1.25 ns later, at every rate (encode convention 1).
  - No state of their own: a clock disturbed by an asynchronous 0x0D / 0x53 write (`sce_q` one clock behind) repairs
    itself on the next one.
- The write clock and the divider counter still use the combinational enable, so the capture logic is cycle-identical to
  0x1537. The equivalence steps already mask the encode pins.
- **Read 0x33**, one byte (write 0x33, the CH1 relays, is unchanged):

| Bit | Meaning |
|---|---|
| 0 | sticky: an encode check failed since the last 0x01 = 1 |
| 1 | the last write clock prediction was right (`swc_sh` = `enc_ref`) |
| 3:2 | 01 = encode convention 1 (B = write clock, A = complement; core B is the earlier sample of a word) |
| 7:4 | the last pin check {CH2 A, CH2 B, CH1 A, CH1 B}, 1 = right |

  - On every falling `clk_200MHz` edge, `enc_ref` (the `enc_nxt` of that edge) is compared with each encode flop (3.75 ns
    after it loaded) and with the write clock copy. A good capture reads **0xF6**.
  - An asynchronous 0x0D / 0x53 write can make one prediction miss, which is why bit 0 clears on 0x01 = 1. The firmware
    already writes those before the reset.
  - The check sees the flops inside the FPGA, not the board.
- Order the firmware must assume, per bitstream:

| Bitstream | Core order in a word |
|---|---|
| 0x1532..0x1536 | B earlier (convention 1) normally; a write in the race window can flip it until the next one |
| 0x1537 | chip 1 A earlier (convention 0); chip 2 undefined (A late / with B / metastable). Do not calibrate on 0x1537 |
| 0x1538 | B earlier on both chips, always. 0x33 bits 3:2 = 01 confirm it |

  - In `tb_trigpos.v` (seed 7), B - A = -5 ns at 200 MSa/s, -50 ns at D 9, -1000 ns at D 199 and -2000 ns at D 399, the
    same sign in every run.
  - Legacy CH1 offsets move from +1..+2 (0x1537 model) to +2..+3 samples, and EXT at 200 MSa/s from +7 to +6..+7. The
    16-bit rows do not change.
  - A calibration taken on 0x1535 fits 0x1538 again; one taken on 0x1537 should be redone.

**Tests.**
- `tb/run.sh` has a ninth step, `encphase.sh MODE=check`: 0x1538 plain, annotated at 3.4..3.5 ns, and the 3.9 ns CH2 A
  variant, which must be caught.
- The contract transcript is now `tb/contract_0x1538.txt`. Its byte sequence (every C / W / # line) equals 0x1537's. The
  R lines differ in three places:
  - the version byte;
  - the stale upper bits of the 0x05 / 0x0A / 0x18 reads (0x39 for 0x37);
  - 7 bytes of READ_RAW / READ_SEQ data at the step: the testbench's ADC model follows the encode pins, so the core order
    now matches 0x1538's.
- `tb/run.sh` passes all nine steps on seeds 12345 and 3 (about 26 to 28 minutes each):
  - 38 + 98 + 154 + 8 + 50 checks;
  - both `cic_model` checks and `seg_model` (22 runs);
  - tb_trigpos 882 checks;
  - the contract (12 checks, transcript equal);
  - encphase (400 checks each for the plain and the 3.5 ns variants; the 3.9 ns CH2 A variant fails, as it must, through
    0x33 and the pin checks).

**Build** (TD 4.6.8 SP1, `./build.sh`; two work dirs gave the same hash):

| | 0x1537 | 0x1538 |
|---|---|---|
| LUT / reg | 4569 / 3820 | 4634 / 3828 |
| `clk_200MHz` | +0.035 ns | 0 errors, min period 4.780 ns (+0.220 ns, the half-cycle path) |
| `sample_write_clock` | period 9.248 ns | 0 errors, 9.480 ns |
| `clkc[1]` | 3 errors (CE crossing -2.45 ns, A encode D -0.209 ns) | **0 errors**, 2.197 ns |
| `clkc[0]` | 53 errors | 58 errors, the same SDC artefact (the net-based `clk_200MHz` analysed against `clkc[0]`'s waveform) |

| File | Bytes | sha256 |
|---|---|---|
| `out/FPGA-seg-0x1538.bin` (= `build/FPGA.bin`, = `backups/al3_seg_0x1538_0c24ecf6.bin`) | 282,898 | `0c24ecf6ff55bc170ce6d7acd1ee4f8afbaf97d73b36e0b7a27a92639d1dbbbd` |
| `backups/al3_seg_0x1538_0c24ecf6_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `2fc7ea5b2fc925157c02f3f492ee3ac17b6154d7ed27e2c20c2d26e855ac1087` |

**Not flashed.** A firmware that knows 0x1536/0x1537 runs it unchanged (AL3 family decode). Reading 0x33 is optional.

**Bench checks** (with the maintainer; never flash without the verified stock dump):
1. 0x06 reads 0x1538; 0x33 reads 0xF6 after a capture at 200 MSa/s.
2. The bench run above (divider 0, 1000+ back-to-back captures, no writes): no capture with any core's std above 5,
   and 0x33 = 0xF6 after each one.
3. CH2 A-B mean back to about 0 (0x1535's values); the wizard's zero step passes CH2.
4. The same at 100 MSa/s (divider 1) and 20 MSa/s (divider 9), and after rate changes in the UI.
5. Core order: a fast edge (calibration pin) at 200 MSa/s shows no one-sample zigzag with the firmware's B-first
   interleave.
6. Warm the FPGA (hair dryer, a few seconds), then repeat 2. 0x1537's fault depended on the operating point; 0x1538
   must not.

### 9. MCU readout timing; version 0x1539 (fpga-gen8, 2026-10-19)

**Bench** (fw 0.2.67 diagnostics on 0x1538, nothing connected, 200 MSa/s legacy; `docs/hardware-status.md` 2026-10-19
18:25-18:35):
- With a core's mean near 128 the bytes read back contain outliers: CH2 core A up to 195 (std up to 17), CH2 core B at
  255 (37..564 of 6000 samples at 0 / 255), CH1 core A once 213. Ten codes either side of 128 every core reads clean.
- With the means at about 116 and 140 (the calibration's two-point zero) CH1 A reads [117..159] and CH2 B [115..159]
  (RMS 1.6..4.6), while CH1 B and CH2 A stay clean. 159 = 0x9F and 113 = 0x71 are mixes across the 0x8F / 0x90 and
  0x77 / 0x78 boundaries: any pair of successive bytes that differ in several bits can come back mixed, on the lanes
  whose pads are slowest.
- Gated 2 MSa/s (16-bit) captures read clean; an extra write strobe before each read changes nothing.

**Cause** (TD's report of the 0x1538 build with `i_mcu_clk` declared as a clock, `report_timing` on `FPGA_pr.db`):
- `o_mcu_data` was loaded on the rising edge of `i_mcu_clk`, the edge the MCU makes when it raises the clock, and
  `fpga_read_byte` reads the port right after that edge (`FPGA_PULSE_CLK()` then `FPGA_GET_DATA()`).
- From the clock pad to the data pads the byte takes **9.4 to 10.9 ns** (TT 1.10 V 85 C): 3.4 ns of clock insertion
  (pad, BUFG), the flop, about 2.5 ns of routing (the flops are not in the IOBs) and 4.7 ns of output buffer, different
  per bit. A port read sooner than that after the edge sees some bits of the new byte and the rest of the previous one.
  Which bits are late is a matter of placement, which is why 0x1535 passed the calibration and 0x1537 / 0x1538 fail it.
  The rate dependence is an inference: the 100 MHz write clock and the encodes switching at full rate slow the pads.
- Inside the FPGA the read path was a same-edge pipeline on an unconstrained clock: the sample memory's read port and
  `o_mcu_data` on the same rising edge (memory output from the previous strobe into `o_mcu_data`), and the read pointer
  (`sample_read_offset`) changing on the edge that the memory's address register samples it (hold +0.38 ns in the
  0x1538 report, the only margin). TD found no violation at TT, but nothing constrained it either.

**Fix in 0x1539** (`src/zaklad.v`):
- `o_mcu_data` loads on the **falling** edge of `i_mcu_clk`, the start of the strobe. The MCU sets `i_mcu_rws` /
  `i_mcu_dcs` before it pulls the clock low and reads after it raised it, so the byte is on the pads (8.7 to 10.0 ns
  after the falling edge in the 0x1539 build) before the rising edge and stays there until the next strobe starts. A
  read at any time after the rising edge returns the whole byte: the full byte changes at one edge that comes a whole
  low phase before the MCU looks.
- The memory's read port keeps the rising edge, its address is `sample_read_addr_n`, a copy of the read pointer taken
  on the falling edge. Every path in the read path now has half a strobe: pointer (rising) to address copy (falling),
  address copy to the read port (rising), memory output to `o_mcu_data` (falling).
- The byte values do not change: the falling edge of strobe k sees `mcu_state`, `mcu_command`, the read pointer and the
  memory output exactly as the rising edge of strobe k did before (all of them change on rising edges only). The read
  pointer still advances on the rising edge of each read strobe, the dummy strobe after 0x1F is still needed, and the
  1.00o5 readout without it is still one word late. The one difference: asynchronous values (0x05, 0x0A, 0x18, the
  live pins 0x24..0x27, 0x28, 0x2A, 0x33, 0x14 while a capture runs) are sampled at the start of the strobe instead of
  its end, a few tens of ns earlier.
- `zaklad.sdc` declares `i_mcu_clk` (40 ns, shorter than any firmware strobe) as its own asynchronous clock group.
- Two timing changes with no functional effect, made to close `clk_200MHz` again after the read path moved:
  - `swc_rise`, the write clock's rise for the EXT trigger and the sequencer snapshots, is now the register `swc_rose`,
    predicted on the rising `clk_200MHz` edge before the write clock rises (`~swc_sh & sce_q`, the same look-ahead
    `enc_nxt` uses, checked by read 0x33 bit 1). It has the value 0x1538's `sample_write_clock & ~swc_prev` had at
    every rising edge, and the half-cycle path from the write clock flop into `sampling_triggered` (the "lottery" of
    0x1534..0x1538) is gone.
  - The sequencer's table registers `tab_a` / `tab_d` load every clock, with `tab_z` marking NEXT's invalidating write
    (before, the state decode drove the enables of 75 registers), and the hold-off counter is 33-bit signed so that
    `q_hz` is its sign bit instead of a 31-bit compare.

**Tests** (`tb/run.sh`, ten steps):
- New step 10, `tb/mcuread.sh` with `tb/tb_mcuread.v`: the firmware's read sequences (READ_RAW with the dummy strobe,
  the 1.00o5 readout without it, 0x06) against a sample memory holding 0x7F / 0x80, a ramp through all 256 codes,
  0x00 / 0xFF and random bytes, with per-bit pad delays from TD's report (9.5..11.0 ns), the clock low for 12..150 ns
  and the port read 0..40 ns after the rising edge. 0x1539 returns every byte at every point. The 0x1538 reference
  (`REF1538_REV`, ce943bf) returns mixed bytes at every point read sooner than the slowest pad (the bench mechanism)
  and none after it.
- The equivalence steps compare against references whose read always block is moved to the falling edge too (`mkref`
  in `run.sh`, `REF_READ_EDGE=pos` to disable), so asynchronous status and live-pin reads are sampled at the same
  instant; everything else is compared exactly as before.
- The contract transcript `tb/contract_0x1539.txt` equals 0x1538's except the version byte (one line).
- `tb_decim.v` copies the write clock into the reference's `swc_prev` (0x1539 has no `swc_prev`); `mutate_seg.py`'s
  `no_invalidate` mutant now clears `tab_z`.

**Build** (TD 4.6.8 SP1, `./build.sh`; two work dirs gave the same hash):

| | 0x1538 | 0x1539 |
|---|---|---|
| LUT / reg | 4634 / 3828 | 4575 / 3833 |
| `clk_200MHz` | +0.220 ns | 0 errors, min period 4.740 ns (+0.260 ns, a sequencer path; the half-cycle path is gone) |
| `sample_write_clock` | 9.480 ns | 0 errors, 9.560 ns |
| `clkc[1]` | 0 errors | 0 errors, 2.420 ns |
| `i_mcu_clk` | not constrained | 0 errors (setup and hold); read path: memory to `o_mcu_data` +11.4 ns of 20, pointer to read address +16.8 ns |
| `clkc[0]` | 58 errors | 58 errors, the same SDC artefact |

| File | Bytes | sha256 |
|---|---|---|
| `build/FPGA.bin` (= `backups/al3_seg_0x1539_f36c1f7a.bin`; three work dirs, same hash) | 282,898 | `f36c1f7a0ada876311f5ad5d600f4cb44523165748e96c25bed160a66388ea64` |
| `backups/al3_seg_0x1539_f36c1f7a_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `22db7b9128e2a12149e5c4f44bc3cb5c22338b40b4e888e97fe9c1a3f571a6b8` |

**Firmware**: none needed. fw 0.2.62 and later decode 0x1539 as AL3 with the features of 0x1538 (`pc_cmds.c`,
`enc_heal.c`). Calibration taken on 0x1537 / 0x1538 with outliers in it should be redone on 0x1539.

**Bench checks** (with the maintainer; never flash without the verified stock dump):
1. 0x06 reads 0x1539; 0x33 reads 0xF6 after a 200 MSa/s capture.
2. The fw 0.2.67 diagnostics run that failed: means near 128 and at the two-point zero (about 116 / 140), every core,
   ranges 0..5: no outliers, RMS as the clean cores (0.5..1.4).
3. The calibration wizard passes on CH1 and CH2.
4. 1000+ back-to-back 200 MSa/s captures (as 0x1538 check 2): no capture with a core's std above 5.
5. A slow edge through 128 (the calibration pin through an RC, or a DC sweep with the offset): the samples step by one
   code, no 0x?F / 0x?0 mixes.

### 10. ADC capture window; version 0x153A (fpga-gen8 analysis, fpga-gen9 design, 2026-10-19)

**Bench** (`docs/hardware-status.md` 2026-10-19 17:50 to 20:51; fw 0.2.64..0.2.69):
- 0x1538: CH2 core A and B garbled (B at the rails, A up to 195) while their means lie in 126..133; CH1 A once.
- 0x1539 (the readout fixed): both A cores return rail values and old/new bit mixes whenever their samples lie in
  codes 128..145 (0x80..0x91: bit 7 set, bits 6..5 clear); the B cores clean; every core clean outside that band. The
  calibration wizard parks the zero at 128, the on-device Frequency response sweep ranges into the band: both wrong.
- 0x1535 was clean, 0x1537 CH2 A at random: the affected lanes changed with every build.

**Cause.** 0x1532..0x1539 take the four ADC buses straight from the pins into the write-clock domain (sample memory,
trigger compare, decimator), at an instant set by placement. Re-timing the placed 0x1538 and 0x1539 designs with zero
I/O delays (`tb/capture_report.sh` pattern; the numbers are TT 1.10 V 85 C):

| Build | Encode pads after the write clock edge R (B rising, A falling + T/2) | Sample memory needs the pad stable over (per core, worst bits) |
|---|---|---|
| 0x1538 | B 6.75 / 6.79, A 6.96 / 7.36 | CH1 A [R-2.1, R-0.1], CH1 B [R-2.6, R-0.4], CH2 A [R-2.1, R-0.2], CH2 B [R-2.1, R-0.1] |
| 0x1539 | B 6.73 / 6.98, A 7.35 / 7.85 | CH1 A [R-2.7, R-0.3], CH1 B [R-3.1, R-1.0], CH2 A [R-2.7, R-0.5], CH2 B [R-2.9, R-1.0] |

The AD9288 (datasheet rev. C, table 1, full temperature) holds each core's data from tPD max 6 ns after its encode
rising edge to tV min 2 ns after the next one: a 4 ns transition once per encode period T = 10 (D + 1) ns, in which
the bits settle one by one. With the pads 0.3 ns from the chip, the windows are: B [R - T + 13.3, R + 9.3]
(encoded on the rising write clock edge), A [R - T/2 + 13.1, R + T/2 + 9.1] (encoded on the falling one). At
T = 10 ns the A pins are in transition over [R - 1.9, R + 2.1]: the A cores above were sampled inside it on both
builds (0x1539 by 0.8 .. 1.4 ns, 0x1538 CH1 A by 0.4), and the B window ends at R - 0.7 .. - 1.0, which 0x1539 just
met (0.0 .. 0.3 ns) and 0x1538's CH2 B overran by 0.8 ns. That reproduces every lane of the bench with one set of
numbers, and the code dependence follows: the bits caught half-way are the ones that flip between neighbouring
codes, most of them at 0x7F / 0x80 and 0x8F / 0x90. (Fitting the four lane results more closely needs tV about 2.5 ns
and tPD about 5.2 ns at the bench's temperature; the datasheet limits remain the design basis.)

The A and B windows of a chip overlap for less than 1 ns at 200 MSa/s, so no single capture instant can serve both
cores, and the instant must not depend on placement. Two more things the fix had to respect:
- the A window moves with the sample rate (its encode is T/2 before R), so a fixed instant relative to the rising
  write clock edge that is right at 200 MSa/s lands in the A transition at 50 MSa/s (D = 3, in the firmware's rate
  table) and takes the wrong period's sample at 67 MSa/s; the A capture must be referenced to the falling edge;
- pecostm32's schematic notes the AD9288's S1 = S2 = high (data-align mode: the chip re-times the B data to the A
  encode). The lane pattern above fits only the align-off timing (B following its own encode), so that is what this
  unit does, but the design must not bet on it.

**Design** (`src/zaklad.v`, block "ADC data capture (0x153A)"):
- Every ADC pin goes into an IOB DDR input register (`AL_LOGIC_IDDR` on `clk_200MHz`; TD packs all 32: `#ireg 33`).
  Its sampling instant is the clock edge within -0.11 .. +0.12 ns on every pin (pad path 1.22 ns, clock insertion
  0.94 ns), fixed by the IOB, not by placement. q2 = the pin at the last falling edge, q1 = at the rising edge before
  it, plus two more rising-edge stages: a write clock edge E (either edge; both are falling `clk_200MHz` edges) sees
  the pins as they were E - 5, - 7.5, - 10, - 12.5, - 15 and - 17.5 ns (stages 0..5).
- Core A is captured against the falling write clock edge F (`sA`, then the pick `a_m`, both on F), core B against the
  rising edge R (`sB`, `b_m`), or, with 0x63 bit 12 / 13, also against F (`sBf`, `bf_m`, for data-align mode). `adc*_w`
  takes the picks on R and feeds the memory, the trigger compare, the decimator, the record mirror and the outlier
  counters as before. Paths: `clk_200MHz` chain to a stage register 2.5 ns, wire only; stage register to pick, one
  write clock period through the 6:1 mux; pick to `adc*_w` half a period, wire only (or one LUT for the reference
  select). The half-cycle pick of 19d9a5c (+0.12 ns) is gone.
- Each core's window relative to its own edge is [e + 6 - T, e + 2] (e = its encode pad delay after that edge, about
  7 ns), the same position at every T. Stages 3 and 4 (E - 12.5, E - 15) lie inside it at every rate; 2 and 5 only
  when the window has drifted towards them, with the same word association; 0 and 1 are in the transition.
- Word association: `adc1A_w` on edge R holds the pins at R - 1.5 T - 5 - 2.5 kA, `adc1B_w` the pins at
  R - 2 T - 5 - 2.5 kB. For kA, kB in {3, 4} these instants fall into the windows of A sample i and B sample j with
  i = j + 1 at every T (the two window conditions are the same interval shifted by T), and A sample j + 1 is encoded
  T/2 after B sample j: **B first by T/2 - 0.25 ns at every rate**, by construction. With the A-encode reference the
  chip has already paired them. `tb/tb_adccap.v` checks this against a model at every rate and both references.
- Command 0x63 (two bytes, high first): bits 2:0 CH1 A, 5:3 CH1 B, 8:6 CH2 A, 11:9 CH2 B stage, bit 12 / 13 CH1 B /
  CH2 B from the A-encode path. Command 0x64: outlier threshold (codes). Read 0x36: the 0x63 value and four 16-bit
  outlier counters (words whose sample differs from the core's previous word by more than the threshold, from the
  third word after the release of 0x01, while the capture writes; cleared by 0x01 = 1). Reads 0x24..0x27 return the
  IOB register (q1). 0x53 bits 3 / 4 are inert (the decimator's `na` / `nb` registers are gone), still read back.
- TD 4.6.8 ignores Verilog initial values: every flop configures to 0 (fpga-gen8's byte-identical builds with other
  values). `cap_sel` and `cap_thr` are therefore stored XOR their power-on value (`CAP_SEL_PO` = 0x08E4: CH1 A stage
  4, CH1 B 4, CH2 A 3, CH2 B 4, B from its own encode; `CAP_THR_PO` = 8). The other non-zero initial values in the
  file (`gen_out_enable`, the DDS divider and duty, `pretrigger_samples`, `total_samples`, `seq_count`, `seg_m`,
  `avg_n`, `seg_mask_s`, ...) are simulation-only; on the hardware those registers are 0 until the firmware writes
  them, which it does before it needs them (AGENTS.md "0x153A review" item 6).
- The post-pass lane select (`pp_xa*`) moved in front of its register (same cycle count) to take the 16-bit adder
  off the `clk_200MHz` critical path.

**Margins** (`tb/capture_report.sh <work dir>` re-times the placed design with `tb/capture_io.sdc` and runs
`tb/capture_margins.py`: tV 2 / tPD 6 / board 0.3 ns, the pin apertures from the report, start / end of the window
in ns, worst bit; exit 1 if a power-on stage has less than 1 ns). Three builds of this design (the placement, and with
it the encode route, changes with every edit of the source):

| Core (edge) | Encode pad after its edge, 3 builds | stage 3 | stage 4 | power-on |
|---|---|---|---|---|
| CH1 A (F) | 7.22 / 7.20 / 7.48 | 4.17/1.60, 4.19/1.58, 3.91/1.86 | 1.67/4.10, 1.69/4.08, 1.41/4.36 | 4 |
| CH1 B (R) | 7.34 / 7.16 / 7.56 | 4.06/1.72, 4.24/1.54, 3.83/1.95 | 1.56/4.22, 1.74/4.04, 1.33/4.45 | 4 |
| CH2 A (F) | 7.93 / 7.38 / 7.40 | 3.46/2.32, 4.01/1.76, 3.99/1.79 | 0.96/4.82, 1.51/4.26, 1.49/4.29 | 3 |
| CH2 B (R) | 7.09 / 7.19 / 7.25 | 4.30/1.47, 4.20/1.57, 4.14/1.63 | 1.80/3.97, 1.70/4.07, 1.64/4.13 | 4 |

The two allowed stages have at least 1.3 ns on every core in every build at TT against the worst-case window; which
of the two is the better one flips with the encode route (the crossover is at e = 7.25 ns), so the power-on choice
follows the average and the firmware's training settles it on the unit. The encode path (flop + 0.9 .. 1.0 ns route +
3.4 ns pad) is the only PVT-dependent term: +-1 ns of it leaves 0.3 .. 1 ns at the fixed default and >= 1.75 ns at the
better of the two stages for any window drift of -2.3 .. +2.7 ns. So the design meets goal (b) of the brief: the
capture phase is selectable per core and self-checked, and a wrong default garbles loudly (rails and jumps, counted)
instead of shifting data; it does not guarantee a worst-corner margin without training (goal (a)).

**Tests** (`tb/run.sh`, eleven steps; all pass on seeds 12345 and 3: 38 + 98 + 154 + 8 + 50 + 882 + 12 + 400 + 400 + 70 + 26 + 2274 checks, both cic_model checks and seg_model 22 runs; counts per step in AGENTS.md "0x153A status"):
- New step 11, `tb/tb_adccap.v`: a behavioural AD9288 per core (samples numbered, bus in random garbage from tV to tPD
  after the output edge, the chip's pipeline of 4, own-encode or data-align output timing, the encode pins through a
  pad delay). Phase 1: every rate D in {0, 1, 2, 3, 4, 9, 19}, stages 2..5, both B references, both align models:
  each core clean exactly where the window formula says, the 0x36 counters equal to the counts derived from the
  stored stream, and with the matching reference B first by T/2 in every word with consecutive samples in consecutive
  words. Phase 2: the pad delay swept 2 .. 12 ns at 200 MSa/s at the datasheet corner and at typical values, stages 3
  and 4: clean inside the predicted range, every dirty capture counted. Then the power-on stages at every rate. 2274
  checks (1414 with `+quick=1`).
- `tb_trigpos.v`: the sampling-instant monitor now follows the capture pipeline (a shadow of the IDDR, chain, stage
  registers, pick and `adc*_w` for the 64-bit time tags). 882 checks pass; the offsets below come from its summary.
- `tb_ext_trigger.v`: the EXT-vs-CH1 step check expects CH1 2 or 3 words later than EXT at divider 49 (the data
  latency, below). `tb_decim.v` / `tb_fir.v`: the configuration marker logs 0x53 bits 3 / 4 as 0 for `cic_model.py`
  (the bits are inert). `tb_long.v`: `decim_chan` lost the `neg_*` ports. The equivalence references get
  `dut.adc*_w` as pins, as fpga-gen8 arranged.
- Contract transcript `tb/contract_0x153A.txt`: equals 0x1539's except the version byte, the stale 0x05 / 0x0A / 0x18
  bits and the READ_RAW / READ_SEQ data bytes (the sampling instants moved).

**Latency.** The legacy data reach the memory 4 words later than on 0x1539 at 200 MSa/s and 3 words later at divided
rates (A: 2.5 T + 5 + 2.5 kA from the pins to the word, plus the write; 0x1539 took the pins about 1.5 ns before the
write). EXT is not delayed. `tb_trigpos.v` (seed 12345, `+pl=4`), crossing minus trigger point in samples:

| Case | 0x1538 / 0x1539 | 0x153A |
|---|---|---|
| legacy CH1 level, D 0 / 9 / 199 / 399 | +2..3 | +2..3 (trigger and data move together) |
| legacy EXT, D 0 | +6..7 | +14..15 |
| legacy EXT, D 9 | | +16..17 |
| legacy EXT, D 199 | | +17..18 |
| 16-bit (K >= 1) EXT | | +40 ns on the full-rate write clock: K2 D3 +10..11, K2 D19 +FIR +8, K1 D199 +1..2 |

The firmware's `pc_decim_ext_shift` (lag4) and the legacy EXT table must follow (firmware notes in AGENTS.md).

**Build** (TD 4.6.8 SP1, `./build.sh`):

| | 0x1539 | 0x153A |
|---|---|---|
| LUT / reg / LE | 4575 / 3833 | 4975 / 4403 / 5644; `#ireg` 33 (the IDDRs), `#oreg` 11 |
| `clk_200MHz` | +0.260 ns | 0 errors, min period 4.949 ns (+0.051: the 32-bit divider compare into `swc_sh`, Atlan4's path, present since 0x1532; it moved from +0.26 with the placement. A miss there shows in 0x33 bit 1, not in the data) |
| `sample_write_clock` | 9.560 ns | 0 errors, 9.513 ns |
| `clkc[1]` | 2.420 ns | 0 errors, 2.202 ns |
| `i_mcu_clk` | 0 errors | 0 errors, 21.09 ns |
| `clkc[0]` | 58 errors | 58 errors, the SDC artefact |
| margins | | `capture_report.sh`: every core >= 1.33 ns at the power-on stage (table above, third build) |

| File | Bytes | sha256 |
|---|---|---|
| `build/FPGA.bin` (= `dist/al3-0x153A.bin`) | 282,898 | `75e0ef21526248ecc2a7a47231abddf59b77d8075917c460a9630ee97cf3ddde` |
| `build/FPGA_padded.bin` (0xFF to 1 MiB, flash image) | 1,048,576 | `77672b5a38f2225e98cdecd7de012850121b6343525392d973e41d39b42719d0` |

**Not flashed.** A firmware that knows 0x1539 runs it unchanged apart from the EXT offsets; the capture registers are
optional (the power-on stages are the computed ones).

**Bench checks** (with the maintainer; never flash without the verified stock dump; fw 0.2.69 or later):
1. 0x06 reads 0x153A; 0x33 reads 0xF6 after a 200 MSa/s capture; 0x36 reads 0x08E4 then four zero counters after a
   capture with nothing connected.
2. The dcoffset sweep at 200 MSa/s, range 0 (the 20:25 test): every core clean at every mean, in particular 126..146
   (no 0 / 255, std about 1); the same at 100, 50 (D = 3) and 20 MSa/s.
3. The RC low-pass with the 1 kHz square (the 20:43 test): no rails, no jumps over 40 codes on any core; the on-device
   Frequency response sweep unflagged.
4. The calibration wizard with the zero point back at 128 (the maintainer's fail-fast decision) passes on both channels.
5. Training: at a quiet input at 128, for each core stages 3 and 4 give zero counters, 2 or 5 may, 0 and 1 give about
   every word; the trained value equals or neighbours 0x08E4. Also try bit 12 / 13: both B cores must count about
   every word with the A-encode reference (align-off confirmed) or be clean (align-on: then the schematic note was
   right and the default must change).
6. The legacy EXT offset at 200 MSa/s (calibration pin loopback): +14..15 samples; the 16-bit EXT lag +40 ns against
   the 0x1539 table.
7. Warm the FPGA (hair dryer, a few seconds) and repeat 2: the margins must hold; the counters flag it if not.

### 11. Records that end on the auto time-out; version 0x153B (fpga-gen11, 2026-10-20)

**Bench** (`docs/hardware-status.md` 2026-10-20 12:32 to 12:42, fw 0.2.88 / 0.2.89 on 0x153A; the 96 records in
`backups/bode/cells_1235.npz`, taken by `firmware/tools/bode_cells.py`): a host capture armed in auto mode with the
trigger level off the signal (level 255, pretrigger 0, CIC3+FIR, 2320 .. 3408 words) came back spliced from two captures
in 15 of 32 cases: the phase of the RC's sine jumps at one sample (806, 1774, 1464, 2012, 1889, 1980 of 2320 at
21.6 kHz; 816, 1889, 3296, 113 of 3408 at 14.7 kHz; 227, 1236, 2161, 2487, 3122 of 3162 at 31.6 kHz; none of 8 at
10 kHz), DFT leakage -3 .. -36 dB instead of below -60 dB, |H| off by up to 0.4 dB. Triggered records (CH1 rising at
code 128) were clean 64 of 64. The device's Frequency response engine, which arms FPGA auto with 0x0E = total and (since
fw 0.2.89) a CH1 trigger, still retook 5 .. 15 records per 25-point sweep as incoherent.

**Evidence.** A two-segment least-squares fit (odd harmonics 1 .. 9 of the known frequency, every split point) locates
each jump and gives the phase step across it: -82.8, -89.4 and +98.6 degrees at the three cells, equal within 0.1 ..
0.3 degrees to 8192 words x f / fs modulo one period (82.8, 89.4, -98.6). The words after the jump are exactly one ring
older than the words before it: the record is ring words 0 .. total - 1 read across the write pointer's stop
position W, with 0 .. W - 1 from the last pass and W .. total - 1 from the pass before. The jump positions are W.

**Cause (0x1532 .. 0x153A).** Atlan4's capture logic ends an auto-mode capture on the time-out (`time_base_cnt >
time_base_set`, both counting words from the sampling reset) and latches `trigger_address <= pretrigger_samples`; the
firmware's record start `0x14 - pretrigger` is then ring word 0. That is right only if the stop lands at W = 0x0E + 2
with total - 2 <= 0x0E <= 8190, i.e. in Atlan's own firmware, which writes 0x0E = total and lets the FPGA time out.
Our firmware's auto mode (fw 0.2.41, `pc_hal_scope.c` "Firmware auto mode", because 0x0E counts samples and 0x18 keeps
watching after the stop) arms **normal** and writes 0x1A = 0 at its deadline, max(100 ms, two records): the same
time-out branch then fires after the ring has wrapped 40 .. 300 times and W is wherever the force landed. The record is
spliced whenever W falls inside 0 .. total - 1: probability total / 8192 (28 .. 42 % for the sweep's records; the bench's
15 of 32 with the force timing quantised by the 1 ms poll). The UI's own auto mode (`pc_ui_auto_service`) and every host
ARM with mode 0 take this path, so the Scope view's untriggered traces at fast time bases carry the same splice (18 %
of them at 3000 legacy samples, 37 % at 3000 16-bit samples). Two more consequences of the convention: in FPGA auto mode
(the Bode engine, `bh_arm`: 0x0E = total) a trigger at word t > 2 is cut by the time-out at W = total + 2 before its
post-trigger words are complete, and the firmware reads t .. t + total - 1 across the stop (the engine's incoherent
records on 0.2.89, now that every record triggers); and an edge after the stop sets 0x18 (the 0.2.40 "flat records
flagged triggered"). The Verilog does what Atlan designed; the design is fragile because it encodes "the record is at
ring 0" instead of "the record ends at the write pointer".

**Change (0x153B).** Capture logic only; capture stages, decimator, FIR, readout, sequencer and the MCU decoder are
untouched (cycle-identical to 0x153A until a time-out stop; tb equivalence phases 0 mismatches).
1. On a time-out stop (auto mode, untriggered) `trigger_address <= sample_write_address + 1 - total_samples +
   pretrigger_samples`, latched on the stop edge (the last word is written on it at `sample_write_address`, so W =
   `sample_write_address` + 1) and frozen with the write enable. The firmware's record start `0x14 - pretrigger` is then
   W - total: the newest total words, contiguous, for a stop anywhere in the ring. With 0x0E = total the record is
   words 2 .. total + 1 of the capture (0x153A: 0 .. total - 1). Requirement: at least total words written since the
   reset (0x0E >= total - 2, or a forced stop later than that), else the record reaches before the reset.
2. The time-out ends only an untriggered capture (`~sampling_triggered` in the time-out term): a trigger before the
   time-out completes its post-trigger words as in normal mode. 0x0E = total with a trigger therefore gives the
   triggered record, not a cut one.
3. No trigger is accepted after the stop (`trigger_check_enable` gated by `sample_write_enable`): 0x18 describes the
   record. A trigger that lands on the stop edge's own clk_200MHz evaluation is still taken (one write clock window): the
   record stays the newest total words and 0x18 reads 1 with the time-out address.
4. `trigger_mode` (0x1A) is synchronised into the write clock domain (two flops), so the forced stop reaches the write
   enable, the read counter and the trigger address on the same edge; `trigger_read_address` and the trigger address
   freeze with the write enable, so a 0x0E or 0x1A write before the firmware reads 0x14 no longer moves them
   (0x1532..0x153A: `trigger_address` followed `trigger_read_address` again once the time-out compare went false).
5. The sequencer takes the new address through its `sn_ta` snapshot unchanged: an untriggered segment's table offset is
   (W - total + pre) mod 2^m (row 53 of the table in section 6; 0x1536..0x153A: pre mod 2^m), and the averaging post-pass
   start `sn_ta - q_pre` is the newest total words of the slot. Version word 0x153B.

**Firmware (prescribed, not implemented here; firmware/AGENTS.md gets the handoff).**
- On 0x153B+ the forced stop is safe: `pc_hal_force` / `pc_ui_auto_service` / the host poll can keep writing 0x1A = 0 at
  the deadline; the record read from `0x14 - pre_words` is contiguous. 0x18 is now the record's truth, so "triggered"
  no longer needs the `host_forced` override, and the firmware may go back to the FPGA time-out (0x0E = the deadline in
  words, 32 bit: 2^32 words is 43 s at 100 MHz) instead of the force. The Bode engine's records (0x0E = total, CH1
  trigger) are complete by change 2 without a firmware change.
- On 0x1532..0x153A (what the unit runs until 0x153B is flashed) the forced stop cannot be made exact from the MCU (W
  is not readable). Mitigation: arm FPGA auto (0x1A = 0) with 0x0E = 8192 k + total - 2 for the smallest k >= 1 that
  reaches the deadline, so the stop lands at ring word total and the record 0 .. total - 1 is contiguous; triggered :=
  0x18 and 0x14 != pretrigger (a late edge after the stop leaves 0x14 at pretrigger; a real trigger at exactly that ring
  word, 1 in 8192, is then reported untriggered with a correct record); a trigger inside the last total - pre words
  before the time-out is still cut (rare: it needs an edge that did not come in the k - 1 rings before). The deadline is
  quantised to 8192 words (41 us at 200 MSa/s, 8 ms at 1 MSa/s, 8 s at 1 kSa/s). The engine should arm normal
  (`pc_hal_arm(..., normal 1)`), since its CH1 trigger at 128 always fires on the generator's square wave, and keep its
  deadline as the safety net.
- Host test for `test_fpga_bus` (fpga_model): the model's time-out stop must latch 0x14 per bitstream version
  (pretrigger up to 0x153A, W - total + pre from 0x153B) and let the ring wrap; a forced stop after 1.3 rings with total
  2048 and pretrigger 0 / 200 must read a contiguous record (the model's words carry their write index) on 0x153B and a
  spliced one on 0x153A; FPGA auto with 0x0E = total and a trigger at word total / 2 must read the complete post-trigger
  region on 0x153B (cut on 0x153A).

**Testbench** (`tb/tb_timeout.v`, run.sh step 13, `ONLY=timeout`; `TIMEOUT_TOTAL` 2048, `TIMEOUT_RUNS` 4): a mirror of
the sample memory's write port (address, data, write order) and the firmware's exact byte sequences (arm as
`fpga_arm_reset`, the force, `pc_hal_acq_finish`, `fpga_read_start` with its dummy strobe, 16-bit and legacy lanes).
Every record is checked word by word: equal to the words written, contiguous in write order, the newest total words
(triggered: the post-trigger region complete, the stop 0 .. 3 words after it), 0x18 true for the record.
- F1: firmware auto mode, CIC3+FIR at divider 31 (N 32), pretrigger 0 and 200, forces landing at ring words total / 4,
  total / 2 + 7, 3 total / 4 and total + 500 in the second pass. F2: legacy at divider 9, pretrigger 100 words.
- A1: FPGA auto, 0x0E = total, no trigger. A2: FPGA auto, 0x0E = total, a CH1 step across the level at word total / 2:
  record complete, 0x18 = 1, the crossing at the pretrigger. A3: the step 500 words after the stop: 0x18 = 0. A4: FPGA
  auto with a 1.3-ring time-out whose stop lands at ring word 1000 + pretrigger.
- Results (seed 12345, 2048-word records, 330 s): **75 checks, 0 errors, PASS**: every record equals the mirror, is
  contiguous and ends at the stop (forces landing at ring words 517, 1553, 3093, 5646 of the F1 pre-0 runs: start = W -
  2048 each time; A1 start 2 with 0x0E = total; A2 triggered at 1029 with the crossing at record word 201, 2880 words
  written, the record complete; A3 0x18 = 0; A4 start = W - 2048 for the 1.3-ring time-out). The same testbench against
  the 0x153A source (`DUT=` the a2b6929 file): 30 errors: splices at record words 511, 1030, 1535 (F1, both pretriggers)
  and 255, 518, 767 (F2), the fourth force of each case (W outside the record) contiguous but 498 words short of the stop;
  A1 two words short; A2 cut at word 1221 (2051 words written, the record needs 3077); A3 0x18 = 1; A4 spliced at 1000 /
  1200: the bench fault, the engine's cut records and the late trigger flag, reproduced.
- Other steps: tb_ext_trigger, tb_decim, tb_fir and tb_seg compare `trigger_address`, `trigger_read_address` and
  `sampling_triggered` with their references only while the write runs (after the stop they differ by design) and check
  the random auto-mode captures' 0x14 against W - total + pretrigger; their directed time-out checks and tb_seg S10 /
  `seg_model.py` carry the new formula; `tb/contract_0x153B.txt` differs from the 0x153A transcript in the version byte
  only. Steps run on the final source (seed 12345): ext 39 checks / 0 mismatches in both equivalence phases, decim 98 +
  cic_model, trigpos 882, firstrec 38, contract 12 (= contract_0x153B.txt) all PASS. fir and seg (the two long steps,
  about 25 min each) needed seven testbench adjustments for the changed stop semantics and one for an older reference
  artefact, none a dut fault. fpga-gen11
  (acae8b8): the reference cuts a triggered auto capture at its time-out (change 2; skipped from the reference's stop
  on), the dut's extra post-time-out words stay in the memory until rewritten (skipped by the memory compare), the read
  register's stale bits after the uncompared 0x14 / 0x18 reads (a 0x41 refill), and a 0x1A = 0 written by the tb while a
  never-triggered normal capture still runs (the reference stops on the write, the dut two write clocks later: masked
  while the forced stop is in flight); that left fir 2458 / 3011 and seg 3223 / 6635 mismatch samples (phase 1 / total).
  fpga-gen12 (8edb754, 6285c19, 503c034) decoded the diagnostic lines: every one was `st` bit 13, `ext_pending` = 1 in the
  reference and 0 in the dut after a time-out stop until the next capture reset (not 10 ns windows: the first ten lines
  of a 2.4 us window; the "reference in reset with half-way set" reading of the seg line was a misdecode, its `st` has
  the same write address, half-way and state in both). Change 3 gates `trigger_check_enable` with `sample_write_enable`,
  which also gates the EXT edge latch, so an EXT edge after the stop no longer sets `ext_pending`. Benign: its only
  consumer is `sampling_triggered` (`trigger_detect_enable` feeds `trigger_rise_enable` / `trigger_fall_enable`, which
  drive nothing), it is not on the bus (0x28 reads `ext_activity`, unchanged), the dut's set condition is a subset of
  the reference's and both clear on the same `cap_rst200`; it is compared while the write runs like the other three.
  With that mask three signatures remained, none a dut fault: (a) 3 ns at a reset release (fir 3, seg 6
  samples): the reference's post-stop `sampling_triggered` is still 1 for the 2.5 ns between the write clock edge that
  raises `sample_write_enable` in the reset and the clk_200MHz edge on which `cap_rst200` clears it; no write clock
  edge falls in that window, so nothing consumes it; `sampling_triggered` and `ext_pending` are compared with
  `sample_write_enable & ~cap_rst200` now. (b) The 0x05 / 0x0A polls of phase 4 (fir 553, seg 292 bus reads): the
  resync before phase 4 refilled the read register's stale bits 7..1 with a 0x14 read, which differs by design after a
  time-out stop since 0x153B (tb_seg), or not at all after the FIR time-out check (tb_fir); both read 0x41 now. (c) Once
  (b) had shifted phase 4's timing, tb_fir showed a 210 ns window with `dec_out1` off by 3 LSB for one output, both
  instances stopped between captures, every decimator input, strobe and setting equal (read with the new
  `+vcd_from=<ns>` VCD window of tb_fir): the reference's CIC integrator `i1` carried a constant offset of 653 from
  phase 2's three FIR runs with 0x53 bits 3 / 4 set, which capture on the falling edge in the 0x1534 reference and are
  inert in the dut since 0x153A, so the reference's CIC saw other samples for those runs and its never-reset
  integrators remember it. The combs cancel a polynomial state offset of degree < K in steady state at every K; the
  offset shows for exactly one output after a K change between captures, when the comb delay registers still hold the
  old K's values with their other offset degree, and only when `c3 >> shift` resolves it (here after a long K = 3 run
  at shift 30). Older than 0x153B, invisible until this timing. Those three runs are tb_fir's phase 5 now, after the
  last equivalence phase (503c034); cic_model.py checks them as before. What
  the equivalence steps prove is unchanged for everything the firmware can observe: pins, decimator, memory, every
  compared bus read and every capture register equal the references while a capture writes; after the stop only the
  registers of changes 1 and 3 differ (trigger address, read counter, triggered flag, EXT latch), and tb_timeout checks
  those. **Results (fpga-gen12, source c173ed5 unchanged, tb 503c034): seed 12345: ext 39, decim 98 + cic_model 1876 samples, fir 163 + cic_model 1612, long 8, seg 53 + seg_model 22 runs, trigpos 882, contract 12 (transcript = contract_0x153B.txt), encphase PASS, mcuread PASS (70 + 26), adccap 2274, firstrec 38, timeout 75: **ALL PASS** (5107 s); seed 4242: 39, 98 + 1873, 165 + 1638, 8, 53 + 22, 882, 12, encphase, mcuread 70 + 26, 2274, 38, 75: **ALL PASS** (4926 s). Every equivalence phase 0 mismatches (the fir and seg check counts vary with the random phase's untriggered captures, which add a 0x14 check each). Four runs in parallel on the 24-core WSL; one full run alone is about 1 h.**

**Build** (TD 4.6.8 SP1, `build.sh`, b2 of fpga-gen11; source commit c173ed5): 4990 LUT (57.8 %), all block RAM as
0x153A. `clk_200MHz` 0 errors, min period 4.583 ns (the first build of the same source rolled 5.101 ns with 2 errors on
the sequencer's 33-bit hold-off decrement, the placement lottery of the 0x153A notes; a functionally neutral rewrite
sharing `total - pretrigger` between the stop compare and the time-out address re-rolled it); `sample_write_clock` 0 errors
(9.231 ns); `i_mcu_clk` 0 errors (20.236 ns); `clkc[1]` 0 errors (2.397 ns); `clkc[0]` 58 errors (the SDC artefact).
Capture margins (`tb/capture_report.sh`, TT): CH1 A 1.73 ns at stage 4 (stage 3 1.54), CH1 B 1.10 at stage 4 (stage 3
2.17), CH2 A 1.89 at stage 3, CH2 B 1.94 at stage 4; the encode pads moved by up to 0.3 ns against the 0x153A build, so
the wizard's training must be repeated after flashing (the unit's 0x153A value 0x08E3 may not be the best for this
build). `dist/al3-0x153B.bin` sha256 `fdc3c21b57341b0774e8a5e58df04e38d1d223fb7c354b6ed49f5892f711fc3d`, flash image
(`build/FPGA_padded.bin`, 1 MiB) `1391978c33379271789a9405f7b0fb9395a85e223ecc69a0430b9f2859a23b88`. **Not flashed**;
the bench session pins both in `backups/` first.

**Bench 2026-10-20 19:31-20:03 (fpga-gen13's reading of the evidence; `docs/hardware-status.md` 19:52).** 0x153B was flashed
(verified `1391978c...3b88`), the wizard retrained the stages to 0x08E4 with the inputs open, and the host checks that followed
were read as "every record spliced" (a seam ratio |x[n] - x[n + period]| / std of 1.8..3.7 in 100 of 100 captures, bode_cells
leak +14..+16 dB, every sweep point flagged). The saved records (`backups/bench-153b/`, six host records at 1 MSa/s, 40
bode cells, one record read three times) show something else: **no record carries a signal**. Every one sits at code
127.5..128.3 with a standard deviation of 0.1..0.6 code and a peak-to-peak under 2.2 codes, on CH1 and CH2 alike, in the
16-bit values (16540..16660 for code 128) as in the compensated codes; the 1 kHz fundamental is 0.005..0.08 code where the
generator's 3.1 V square at CH1's 17.4 mV/code would give about 113 codes (178 peak to peak); the strongest bin of each record
is the lowest one (a slow wander of 0.1..0.7 code). The seam ratio of white noise is 1.14 on average and 1.8 at its maximum by
construction (a smoothed |difference| of two independent samples over the standard deviation), 2..3.7 with the wander, and 0.04
at most for a clean square: the "seams" are the metric's reading of noise, at positions that mean nothing, and a ring splice
cannot be seen or ruled out in a record without a signal. The leak figure likewise describes noise against an absent tone
(`bode.py` flags that record `ch1_low` / `ch1_dead`; bode_cells.py printed only the leak until fpga-gen13). GET_CAPTURE_INFO
flags bit 5 is set in both saved captures (0x28 bit 2, an edge on the calibration pad during the capture): the generator was
toggling at the pin, and the records' DC level is 0 V within 0.1 V on both channels, where a probe on a *silent* post would
read -1.23 V (code 59) or +1.87 V (code 237) against the probe ground (the DMM values of section 2). So the probes were not on
the post during these captures (the wizard had just run with the inputs open), and nothing was measured about 0x153B's record
geometry. Nothing in the 0x153B source touches the data, readout, generator or front-end paths (the diff against 0x153A is the
capture logic of changes 1-4 and the version byte), the forced-stop and triggered paths of the host capture are what
tb_timeout's F1 / A2 cases model, and case B (fpga-gen13) adds that capture byte for byte (D 199 CIC3+FIR, pretrigger 3000,
total 6000, 16-bit, the force after 1.37 rings and a CH1 trigger, READ_RAW's split read at the ring end): contiguous, the
newest 6000 words, the trigger crossing at the pretrigger (85 checks, 0 errors on seeds 12345 and 4242). **The bitstream is unchanged; 0x153B stays unbenched** until the
checks below are run with the probe on the post. Check 0 below is the lesson.

**Bench checks** (maintainer with the bench session; FEL load of a firmware that detects 0x153B, flash per "Flashing this unit"
after pinning the images in `backups/`):
0. The signal is there before anything is judged: after the wizard (inputs open) put the probe back on the post, and for
   every record check the amplitude first (a 1 kHz square from the generator on CH1 at range 3 spans about 178 codes peak
   to peak; a record under 3 codes peak to peak holds no signal, whatever a seam, leak or phase metric says of it). Quick
   confirmations: the Scope view shows the square; GET_STATUS `ext_flags` (byte 42, 0x28: bit 0 the live pin level, bit 2 an
   edge since the arm) shows the generator toggling at the pin, but says nothing about the probe;
   bode_cells.py prints `NO TONE` for a toneless record.
1. Identity: `#VER` fpga=0x153B; the Scope view runs, triggers and averages as on 0x153A (the capture stages are the
   same; 0x36 self-check 0 on a quiet trace).
2. The bench fault: `python firmware/tools/bode_cells.py <port>` on the RC as on 2026-10-20 12:32, then the two-segment
   fit (fpga-gen11's `splice.py` pattern, or the host's leak figure): every "auto" record clean (leak below -60 dB,
   |H| within 0.01 dB of the "host" variant); on 0x153A 15 of 32 were spliced.
3. Scope view, auto mode, trigger marker above the signal, generator 1 kHz on CH1 at 1 MSa/s: 50 untriggered traces
   without a step in the waveform (on 0x153A about every fifth trace shows the two-pass seam at a random position; a
   host `s.capture()` with `mode=0, level=255` and `splice.py` on the record make it exact).
4. Frequency response, ten RUN sweeps on the RC as fw-eng2's walk-through: retried 0 .. 1 per sweep (0.2.89 on 0x153A:
   5 .. 15), 21.6 kHz -6.74 dB in 10 of 10.
5. Auto mode with a slow signal (0.5 Hz square from the generator, 10 MSa/s): the trace is untriggered until an edge
   arrives, then triggered at the marker; no "triggered" flag on a flat record (`s.capture()` flags).
6. Segments in auto mode with a sparse trigger (section 6 step 7 with the generator off): the untriggered segments
   read as flat records without a seam.

### 12. Trigger types and hold-off; version 0x153C, item C1 (fpga-c1, 2026-10-20)

Item C1 of `docs/plans/fpga-0x153c.md` (sections 2.1, 2.2, 2.4, 3, 9.1; the owner accepted the meaning change of 0x1B,
contract question 1): the trigger-type design of branch `gen8-features-wip` (commit 9de82ca, built and simulated on 0x1539
as "0x153A") rebased onto the 0x153B source, plus the trigger hold-off. Peak detect (C2) and the counter (C3) are separate
items of the same generation; the version byte becomes 0x3C at integration (C4). With the power-on values (0x5E = 0,
0x1B = 0) the capture is cycle-identical to 0x153B: every equivalence step of `tb/run.sh` passes unchanged.

#### Register map

Writes (latched like 0x15..0x17: any time before 0x01 = 0, also inside the 0x01 = 1 window after the 0x05 wait):

| Cmd | Bytes | Bits | Meaning |
|---|---|---|---|
| 0x16 | 1 | 0 | polarity, per type (below). Unchanged for the edge type |
| 0x1B | 4 | 31..0 | **trigger hold-off**, 5 ns units, 0 = off (power-on). Meaning change: Atlan4 stored it and never used it |
| 0x5E | 1 | 2..0 | **trigger type**: 0 edge (power-on), 1 pulse width, 2 Nth edge, 3 timeout; 4 runt and 5 window are 0x153E and act as edge here, like 6 and 7 |
| | | 3 | pulse width: 1 = longer than N, 0 = shorter than N (gen8 had this on bit 2) |
| | | 7..4 | reserved, write 0 (0x153E: bit 4 range, bit 5 both cores) |
| 0x5F | 3 | 23..0 | **N**, high byte first: pulse width and timeout in samples (1 .. 2^24 - 1); Nth edge: bits 15..0 (1 .. 65535, 0 acts as 1) |
| 0x60 | 3 | 23..0 | **idle time H** for the Nth edge, samples; 0 = count from the arm |
| 0x66 | 1 | 0 | 1: the next 0x01 = 1 also clears the hold-off timer (one-shot, consumed by that 0x01 = 1) |

Reads:

| Cmd | Bytes | Content |
|---|---|---|
| 0x34 | 3 | trigger qualifier status: byte 0 = { 0, 0, counting (Nth edge: an idle gap was seen, or H = 0), a sample off the pulse side was seen since the capture reset (pulse width), source EXT, longer, type bits 1..0 }; bytes 1..2 = edges still to count before the qualifying one (Nth edge). Asynchronous: read twice if it matters |
| 0x37 | 1 | { 0, 0, 0, hold-off running, type bit 2, both cores (0x153E, 0), trigger core (0x153E, 0), 0 } |

#### Trigger types

**Streams and units.** Each source has its own qualifier (module `trig_qual`):
- CH1 / CH2: the trigger stream the edge trigger compares, one sample per memory word. Legacy (K = 0): core A of the
  channel (`adc*_w` since 0x153A), one sample per `10 (D + 1)` ns; K >= 1: the 16-bit output samples (or the peak words
  of C2), one per `N_dec x 5` ns. N and H count these samples.
- EXT: the synchronised and filtered `ext_level`, one sample per 5 ns, at every sample rate. N and H count 5 ns clocks
  (N up to 83.9 ms).
- The level is the edge trigger's (0x17, or 0x55 with K >= 1), without hysteresis, as for the edge trigger.

**Definitions** (the side of the level a sample is on decides everything; the trigger always fires on an edge the edge
trigger could fire on, except the timeout):

| Type | 0x16 = 0 | 0x16 = 1 | Fires on |
|---|---|---|---|
| Pulse width | positive pulse (high run) | negative pulse (low run) | its trailing edge, when its width w (samples on the pulse side) is w < N, or w > N with 0x5E bit 3. The pulse must have begun after the capture reset: a sample off the pulse side was seen first |
| Nth edge | rising edges | falling edges | the Nth edge. H = 0: edges count from the arm (the pretrigger has filled). H > 0: the count restarts when the level has held for H samples (no edge of either direction), so the trigger is the Nth edge of each burst; H = 1 restarts on every edge |
| Timeout | high after a rising edge | low after a falling edge | the sample that makes the run N + 1 samples long (the level has held for more than N samples since the edge, or since the capture reset), no edge needed |

**Trigger position.** For CH1 / CH2 the trigger position is exactly the edge trigger's for the qualifying edge (the
timeout's for the qualifying sample): trigger_address = (memory word of the trigger sample) - 1, as for every edge trigger.
For EXT the qualified types decide one clock (5 ns) after an edge trigger on the same edge would, then apply on the next
write clock rise like any EXT trigger; the firmware's EXT latency correction stays the same within one 5 ns clock.

**How it works** (adds nothing to the timed trigger path):
- The edge trigger's compare (`trigger_previous` against `trigger_data`, the half-cycle path) is unchanged. For a type
  other than edge, `trigger_previous` is not loaded with the previous sample but with a value on the pre-detect side of
  the level when the qualifier says the next edge is the trigger, else one on the other side. The qualifier only looks
  at samples before the next one, so the compare fires exactly on the qualified edge, or on the sample after a held
  level for the timeout (the compare itself checks that the data sample is on the post-detect side).
- `trigger_edge` holds the detect direction: 0x16 for edge, Nth edge and timeout, its complement for the pulse width
  (whose trailing edge is the opposite of its polarity). `trigger_ext` (the EXT edge path) is set only for the edge type.
  Both are recomputed on writes of 0x15, 0x16 and 0x5E, so the edge type is bit-identical to 0x153B.
- EXT: the qualifier runs on `ext_level_prev` / `ext_level` at 200 MHz, and its registered event (`ext_qev_r`) sets
  `ext_pending`.
- Counters: 25-bit down counters with a frozen sign bit for the run lengths (no compare in the decision), a 16-bit down
  counter for the edges with registered "next is the Nth" and "the level has held for H" flags (`rz`, `gap_r`).
- Rebase changes against gen8 (contract 3.2): (1) the qualifier's `fire` into `trigger_previous` is gated with the
  0x153B stop (`sample_write_enable`), so no qualified type can latch a trigger after the write has stopped; (2) the ADC
  qualifier stays in reset until the first `swc_adv` edge after a capture reset (`q_hr_adc`: the edge that writes word 0
  and loads it into `trigger_data`), so 0x5E / 0x5F / 0x60 written inside the 0x01 = 1 window apply at every rate (the
  0x1537 rule for 0x0B; gen8's reset kept a stale N at slow rates) and the first sample it processes is word 0 of the
  capture, legacy and K >= 1 alike (gen8 processed the reset value of `trigger_data` first; the two differ only for a
  pulse that begins at word 0, before the pretrigger has filled, where no trigger can fire); (3) "longer" on 0x5E bit 3
  and a 3-bit type with 4..7 acting as edge (bit 2 read in 0x37).

**Firmware, per type** (in the arm, with the other trigger writes; contract 3.5):
- Edge: write 0x5E = 0 (also restores the behaviour of 0x153B at any time).
- Pulse width: 0x5E = 1 (+ 8 for "longer than"), 0x5F = N, 0x16 = polarity. N = round(t / sample period) (CH1 / CH2:
  the record's word period; EXT: 5 ns). The UI's "> t" / "< t" map to longer / shorter. N = 0 is rejected by the firmware.
- Nth edge: 0x5E = 2, 0x5F = N, 0x60 = idle time in samples (0: from the arm), 0x16 = counted edge.
- Timeout: 0x5E = 3, 0x5F = N >= 1, 0x16 = 0 (stays high) or 1 (stays low).
- Auto mode (0x1A = 0) and the auto time-out work as before for every type (0x153B's rules: no trigger after the stop,
  time-out only while untriggered). Segments and averaging use the type for every record (the qualifier restarts with
  each record).
- Read 0x34 to show "waiting for edge k of N" or to check the configuration; 0x37 for the hold-off and type bit 2.

#### Trigger hold-off (0x1B, contract 3.6)

- From the rise of `sampling_triggered` (the trigger of a record) the trigger check (`trigger_check_enable`) is masked
  for exactly 0x1B clocks of 200 MHz (5 ns .. 21.47 s): a 33-bit signed down counter loaded with 0x1B - 2, the mask ends
  on the clock after its sign sets (the sequencer's `q_hc` pattern; the counter's sign bit, a register, feeds the mask
  register, so the compare path is untouched and the only long path is the decrement chain).
- The timer is not reset by `cap_rst` (sequencer restarts) nor by 0x01 = 1 alone, so a train's records keep their spacing
  across segments and MCU re-arms. 0x66 bit 0 = 1 makes the next 0x01 = 1 clear it (a one-shot, consumed by that 0x01 =
  1; a toggle handshake carries it from the MCU clock into clk_200MHz), and 0x1B = 0 releases the mask at once (a running
  timer is ignored; the contract's "0 = off").
- An auto capture whose time-out falls inside the hold-off ends untriggered (0x18 = 0, 0x14 = W - total + pretrigger as
  on 0x153B); a normal capture waits for the first qualifying edge after the hold-off. The EXT path is masked the same
  way (the `ext_pending` latch takes `trigger_check_enable`), and the qualifier keeps counting during the hold-off (an
  Nth edge counts edges inside it).
- Firmware: write 0x1B in the arm (any time before 0x01 = 0; 0 = off); write 0x66 = 1 before the first arm of a session
  so a timer left by the previous session does not delay its first trigger; caps bit 2 HOLDOFF.

#### Tests

`tb/run.sh` step 14, `tb_trigtypes.v` (gen8's testbench rebased; `ONLY=trigtypes`, `TRIGTYPES_RUNS`): 7 cases (CH1 / CH2
legacy at 10 and 200 MSa/s, CH1 boxcar N = 20, CH2 CIC2 N = 2, EXT legacy and K = 1), both polarities, every type: pulse
width shorter / longer with clear widths (N +- 2.5..4) and random widths around N (N +- 1.5), Nth edge 3 from the arm and
4 after an idle time of 20, both also with the train running across the arm, Nth edge 1 against the edge type, the
timeout; the reserved types 4 and 7 against the edge calibration with 0x37 bit 3; the 0x34 / 0x37 read-back. The FPGA's
trigger sample must be the one a reference model finds on the stream the FPGA saw (rebuilt from the sample memory and the
arm flags, or for EXT from `ext_level`), at the edge type's position; the clear cases also against the stimulus (within
1.5 samples of the intended edge). New cases: the late write (divider 999, 0x5E / 0x5F written after the 0x05 wait right
before 0x01 = 0 after a stale N: the third edge, not the first), the hold-off on a 4 us square at 10 MSa/s (a 4-segment
run and MCU re-armed single captures trigger every fifth edge with a 4.5-period hold-off against every second without;
0x66 then 0x01 = 1 clears it and the one-shot is consumed; 0x1B = 0 releases it at once; an auto capture inside the
hold-off ends untriggered at W - total + pretrigger; EXT and the pulse-width type held off alike; 0x37 bit 4 follows the
timer), and the 0x153B stop gate for every type on CH1 and EXT (a time-out capture takes no trigger from a qualifying
event afterwards: 0x18, 0x14, `sampling_triggered`, `ext_pending` unchanged). **1293 checks, 0 failed on seeds 12345
and 4242** (about 200 s each). Mutants caught (`DUT=` runs): the hold-off mask removed (9 failures), the 0x66 clear not
consumed (2), "longer" on bit 2 (45), gen8's qualifier reset (1: the late write).

#### Build (TD 4.6.8 SP1, `build.sh`)

A synthesis check of the C1 branch alone (the version byte still 0x3B; C4 integrates and pins): 5721 LUT (66.2 %) /
4682 reg / 6314 LE, all block RAM and 2 M18x18 as 0x153B; `clk_200MHz` 0 errors, min period 4.739 ns (the worst path
+0.261 ns is the EXT qualifier's edge count `qual_ext/rem`, 5 levels); `sample_write_clock` 0 errors (9.576 ns);
`clkc[1]` (2.568 ns) and `i_mcu_clk` (22.152 ns) 0 errors; `clkc[0]` 58 errors (the SDC artefact).
Against 0x153B (4990 LUT, 4.583 ns): +731 LUT / +300 reg for the two qualifiers, the hold-off and the decoder entries
(the contract's estimate was about 350 + 60). The placement lottery of the 0x153A notes is real at this size: the same
logic rolled 5.218 ns (6 errors, the EXT qualifier's `gap` chain, fixed by registering `gap_r`), then 5.186 ns (2 errors on
0x153B's own `sampling_triggered` half-cycle path into the write-clock `trigger_address` enable and on the sequencer's
`pp_i` enable), 5.078 ns (1 error on the encode look-ahead `sce_q` into `enc_nxt_n`) and 6.247 ns for four order-only
rewrites of the same source, before the fifth (the hold-off block placed ahead of the write address counter) met timing;
the testbenches are indifferent to block order. `~/c1/b3` = `src/` as committed: `FPGA.bin` sha256 `95465a54...1730e`
(not a release image; nothing in `dist/`).

### 13. Hardware counter with min / max; version 0x153C, item C3 (fpga-c3, 2026-10-20)

Contract: `docs/plans/fpga-0x153c.md` sections 2.1 (0x61, 0x62), 2.2 (0x35), 5 and 9.1; the gen8 design (branch
`gen8-features-wip`, 9de82ca) rebased onto 0x153B with the min / max bytes added. The trigger types (C1) and peak detect
(C2) of the same generation are documented by their own agents; C4 integrates them and sets the version byte 0x3C. With
0x61 = 0 (power-on) nothing runs and the capture is cycle-identical to 0x153B (the equivalence steps of `tb/run.sh`).

#### Register map

| Cmd | Bytes | Bits | Meaning |
|---|---|---|---|
| 0x61 | 1 | 1..0 | **gate**: 0 off (power-on), 1 = 10 ms, 2 = 100 ms, 3 = 1 s (exactly 2e6 / 2e7 / 2e8 clocks of 200 MHz) |
| | | 3..2 | **source**: 0 the trigger source (0x15 as written: 0 CH1, 1 CH2, 2 and 3 EXT), 1 CH1, 2 CH2, 3 EXT. A 0x61 write that changes the value restarts the gate |
| 0x62 | 2 | | **level** (raw ADC code: high at code >= level), then **hysteresis** (low at code < level - hysteresis; hysteresis >= level never goes low). Power-on 0 / 0; the firmware writes 128 / 6 at boot and follows the trigger level |

Read 0x35, 16 bytes, one consistent snapshot:

| Byte | Content |
|---|---|
| 0 | { new, 0, EXT, CH2, 0, 0, gate[1:0] }: the source and gate the result was measured with; `new` = a gate ended since the previous 0x35 read |
| 1 | sequence number of the result (8 bits, +1 per gate) |
| 2..5 | E, u32 high byte first (28 bits used): periods between the first and the last rising edge in the gate; 0 = fewer than two edges |
| 6..9 | T: 5 ns clocks between those edges |
| 10..13 | W: 5 ns clocks the stream was high between them |
| 14 | min: the smallest raw code of the source's captured pair over the gate (both cores); 255 when no sample (EXT source, or nothing published yet) |
| 15 | max: the largest; 0 when no sample |

Derived: frequency = E / (T x 5 ns), period = T x 5 ns / E, positive width = W x 5 ns / E, duty = W / T, Vmin / Vmax from
min / max through the 8-bit per-core model (mean gain), Vpp = Vmax - Vmin. Resolution one clock over the gate (5e-7
relative at 10 ms, 5e-9 at 1 s, the 50 MHz crystal dominates); range about 1 Hz (two edges in a 1 s gate) to the front
end's 30 MHz (the logic takes 100 MHz: one high and one low clock); T and W are 28 bits (1 s = 2e8 clocks).

#### Design (`zaklad.v`, block "Hardware counter with min / max" after `seq_lat`; decoder entries 0x61 / 0x62, read 0x35)

- **Stream.** EXT: `ext_level` (the synchronised, majority-filtered pin), one bit per 5 ns. CH1 / CH2: the captured pair
  of every write clock, `adc*_w` (core B the earlier sample, core A the later one, T/2 apart; the same registers the
  memory, the trigger and the decimator take), through a Schmitt trigger evaluated on the write clock (`c_hie` /
  `c_loe` for the earlier sample, `c_hil` / `c_lol` for the later one). The clk_200MHz side takes the earlier sample's
  state on the clock after the write clock's rise (`swc_rise_q`, the sequencer's settled-rise flag) and the later
  sample's on the clock after its fall (`swc_fall_q`, predicted from `swc_sh & sce_q` exactly as `swc_rose` predicts
  the rise), so at 200 MSa/s (divider 0, or any K >= 1, where the decimator forces the full-rate write clock) the
  stream `k_st` changes every 5 ns and at slower legacy rates each sample is held for half a word. No half-cycle path:
  the write clock registers are consumed 1.5 clocks after their edge, and the next edge is >= 2 clocks away.
- **Counting** (`k_*`, as gen8): the gate counter `k_g` counts 2e6 / 2e7 / 2e8 - 2 down to -1 (bit 28 = the publish
  clock). From the first rising edge of `k_cs` in the gate `k_t` counts clocks, `k_w` high clocks, `k_e` edges; at
  every rising edge their values are latched (`k_tl`, `k_wl`, `k_el`), so at the publish clock the latched values are
  "first edge to last edge". The publish copies them into `k_rt` / `k_rw` / `k_re` with the configuration and
  increments `k_seq`; the next gate starts at once. `k_on` compares the configuration (source, gate) with the one
  registered a clock earlier, so a 0x61 write that changes it gives one clock of `k_on` = 0, which reloads the gate and
  clears the running values: the first result comes one gate after the write. The sample at the publish clock itself
  belongs to neither gate (one of 2e6).
- **Min / max** (new): on the write clock `c_mn` / `c_mx` = min / max of the pair; on the clk_200MHz side the running
  `k_mn` / `k_mx` fold them in on the same `swc_rise_q` clocks the Schmitt stage uses, except for the EXT source (no
  codes). Reset to 255 / 0 at every gate start; published with E, T, W. The published min is stored inverted
  (`k_rmn_n`; TD configures every flop to 0, `AGENTS.md` "0x153A review" item 6), so a unit that has published nothing
  reads 255 / 0, the "no sample" value, without an initial value. Cost: two 8-bit compares and two 8-bit muxes on the
  write clock, two 8-bit compares and 4 x 8 registers on clk_200MHz.
- **Trigger-source follow.** Source 0 follows a private copy of 0x15 as written (`k_t15_ext`, `k_t15_ch2`, loaded by an
  own always block on the 0x15 data strobe), not `trigger_ext` / `trigger_channel`: the trigger-type logic of C1 may
  redefine what those mean for the qualified types (gen8 made `trigger_ext` the edge type's EXT only), and the counter
  must follow the source the firmware named. 0x15 = 3 (the 0x153D pattern marker) reads as EXT here.
- **Snapshot** (`m_*`). The 0x35 command byte toggles `m_req` on `i_mcu_clk`; a two-flop synchroniser on clk_200MHz
  detects the toggle and copies `k_rt`, `k_rw`, `k_re`, `k_rmn_n`, `k_rmx`, `k_rcfg`, `k_seq` into `m_*` and sets
  `m_new` = (`k_seq` != the previous `m_seq`), 3 .. 4 clocks (15 .. 20 ns) after the command strobe's rising edge. The
  first data byte is loaded on the next strobe's falling edge, >= 50 ns later on any MCU timing (about 150 ns on the
  firmware's bus), so the copy is complete before it and a gate ending during the read can never tear the snapshot
  (gen8 copied on the `i_mcu_clk` edge itself, racing the 200 MHz publish once in about 2e6 reads). The 16 bytes are
  static until the next 0x35 command byte. A write command 0x35 (CH2 offset) takes the same snapshot, harmlessly.
- **Independence.** The counter reads `adc*_w`, `ext_level`, `swc_rise_q`, `swc_sh`, `sce_q` and the MCU bus; it writes
  nothing the capture, the decimator, the sequencer or the readout use. Reading 0x35 does not touch
  `sample_read_offset` (the pointer advances on 0x20..0x23 and 0x2B..0x32 only), so a READ_RAW interrupted by a 0x35
  read continues as after a 0x06 read (tb check).
- **Read cost.** One 0x35 read = the command strobe plus 16 data strobes = 17 bus strobes, about 5 us at 0.3 us per
  strobe (`docs/protocol.md` section 11), once per screen pass (about 15 ms); a 0x61 / 0x62 configuration = 2 + 3 = 5
  strobes.
- **Rate changes.** A 0x0D or 0x53 write while a gate runs puts a few pairs of mixed age into the stream (the cores'
  pipelines re-align at the new encode period; the AD9288 pipeline is 5.5 cycles), which adds or drops one edge in the
  gate that spans the change: tb runs without a restart read -7.5e-5 (divider 0 -> 9) and +7.3e-5 (9 -> 0, K = 1) on a
  1.29 MHz square, one of 12858 edges; on a 10-edge gate that is 10 % for one reading. The firmware should rewrite 0x61
  after its settle wait on a time base or filter change (0 then the value: a value change restarts the gate; 4
  strobes), or skip the first result after one (the sequence number). Not an RTL item: a restart pulse at the write
  would end before the transient at slow rates (up to 5 words of 41 us at divider 4095).

#### Deviations from the gen8 design and the contract

- Byte 0 bit 4 (CH2) is 0 whenever the source is EXT (gen8 could report EXT and CH2 together when 0x15 = 3 named
  channel 2 and EXT at once); the two source bits are exclusive.
- The gate restarts on any 0x61 value change, a gate-length change included (gen8 restarted on a source or on / off
  change only, so a 1 -> 2 change mid-gate published one result of a wrong length). A write of the same value does
  not restart it.
- The level and hysteresis power on at 0 / 0 (gen8's `cnt_lvl = 128` initial value was simulation-only; TD configures
  0), as the contract says; the firmware writes 128 / 6.
- The snapshot is copied on the 200 MHz side (above) instead of the `i_mcu_clk` edge: same bytes, no tear.
- Min / max for the EXT source read 255 / 0 (the contract's "no sample" value): EXT has no codes. The firmware takes
  Vmin / Vmax / Vpp from the counter only when the counter's source is the card's channel.

#### Tests (`tb/tb_counter.v`, run.sh step 16, `ONLY=counter`)

Thirteen 10 ms gates, 130 ms of simulated time, about 68 min of wall clock per seed (the 200 MHz design with every
block RAM is the cost; run it in parallel with the fir / seg steps). The reference model runs at every clk_200MHz edge
on the counter's input stream (`k_cs`) and, for min / max, on the pair the FPGA captured (`adc*_w` at every write clock
edge), folded on the counter's own `swc_rise_q` clocks; publish times from the gate counter.
1. Gate: consecutive results exactly 2,000,000 clocks apart; the 100 ms / 1 s reloads as constants.
2. Bookkeeping: E, T, W and min, max of every gate equal the model.
3. Accuracy: EXT 1234.5 ns square (810.044552 kHz) within 3e-6, duty 32.4 % within 0.2 %; CH1 777.7 ns square with
   +-2 codes of noise at 200 MSa/s and with K = 1 within 3e-6, at 10 MSa/s within 2e-5; min / max within the noise of
   the stimulus levels (60 / 200 on CH1, 55 / 195 on CH2).
4. Source select: src 1 = CH1 although 0x15 names CH2; src 2 = CH2 with 0x15 = CH1; src 0 follows 0x15 (CH2, EXT); the
   status byte names the source; EXT publishes min 255 / max 0.
5. Schmitt boundaries with the noise off (codes exactly 60 and 200): level 201 counts nothing, level 200 with
   hysteresis 139 counts, hysteresis 140 never goes low (code 60 is not below 60); a 20 us noisy triangle counts its
   noise with hysteresis 0 and 50.000 kHz within 1e-4 with 12.
6. MCU side: the power-on read (nothing published) gives 0 everywhere and min 255 / max 0; `new` set for a fresh
   result, clear on the second read of the same result, set again after the next gate; a 0x35 read inside a READ_RAW
   pass leaves the readout as a 0x06 read there does; 0x06 reads 0x153B (or 0x153C after C4).
Results: seed 12345 **115 checks, 0 failed, PASS** (4057 s); seed 4242 **115 checks, 0 failed, PASS** (4049 s).
Measured (seed 12345): EXT 810044.552 Hz exact and -4.5e-7 in the second gate, duty 0.3240; CH1 at 200 MSa/s +1.6e-7,
at 10 MSa/s -1.1e-6, with K = 1 +1.6e-7, duty 0.3858, min 58 / max 202; CH2 min 53 / max 197; the triangle 15,984
edges with hysteresis 0 (seed 4242: 15,924) against 499 with 12 (49999.9 Hz, -2e-6). The full `tb/run.sh` (16 steps)
on this source: **ALL PASS** on seeds 12345 (9252 s) and 4242 (9057 s), every equivalence phase 0 mismatches, the
contract transcript equal to `contract_0x153B.txt` (the counter adds no bus traffic to the firmware's sequences).

#### Build (TD 4.6.8 SP1, `build.sh`, this branch alone on 0x153B's source; C4 builds the merged 0x153C)

5337 LUT (61.8 %) / 4900 reg / 6095 LE, block RAM (48 + 2) and M18x18 (2) as 0x153B. 0x153B rebuilt with the same flow
for the delta (`FPGA.bin` = `dist/al3-0x153B.bin`, `fdc3c21b...fc3d`): 4990 / 4405 / 5658, so the counter with min / max
costs **+347 LUT, +495 reg, +437 LE** (the registers: 9 x 28-bit E / T / W stages, the 29-bit gate, the 3 x 28-bit
snapshot, min / max and thresholds). `clk_200MHz` 0 errors, min period 4.931 ns (+0.069 ns on the sequencer's
`q_untrig` saturating adder, zaklad.v 1478, not a counter path: the placement lottery of the 0x153A notes; 0x153B's
build had 4.583 ns); `sample_write_clock` 9.594 ns, `i_mcu_clk` 20.37 ns, `clkc[1]` 2.376 ns, all 0 errors; `clkc[0]`
58 errors (the SDC artefact). The contract's section 1.1 counts the counter inside gen8's +1115 LUT for three features
plus about 50 LUT for min / max; 347 is the counter's share. The merge with C1 (section 12) builds to 6079 LUT (70.4 %) /
5177 reg / 6822 LE and misses `clk_200MHz` by 0.763 ns on the sequencer's `q_pass` adder (8 setup errors, long fanout-1
routes; `sample_write_clock` 9.7 ns, the other clocks clean): the placement lottery at 70 % LUT, for the integration build
to re-roll (fpga/al3/AGENTS.md "0x153C C3 counter").

#### Bench checks (C4's bench, item 7 of the contract's section 9.2)

The generator's post at 1 kHz and 1 MHz on CH1 and on EXT: frequency within 1e-5 of phase_inc x 50 MHz / 2^32, duty
50 %; min / max equal the record's min / max of the raw codes (a 16-bit record's high bytes at K >= 1, or the legacy
samples); with the probe off the post, min / max read the noise band around 128 and E = 0 at a level outside it; a time
base change during a 1 s gate shows the one-edge blip of "Rate changes" unless the firmware restarts the gate.

### 14. Peak detect; version 0x153C, item C2 (fpga-c2, 2026-10-20)

Contract: `docs/plans/fpga-0x153c.md` sections 2.1 (0x56 bit 1), 2.2 (0x29), 4 and 9.1 (step 15); section 10 item C2.
Sections 12 (C1, trigger types and hold-off) and 13 (C3, counter) are the other two 0x153C items; C4 integrates, sets the
version byte and builds. With 0x56 bit 1 = 0 (power-on) the capture is cycle-identical to 0x153B: the equivalence steps of
`tb/run.sh` prove it (their random 0x56 writes keep bit 1 clear now, see "Tests").

#### Peak detect (0x56 bit 1)

**What it does.** With 0x56 bit 1 set, each 16-bit memory word per channel holds { max (high byte, read 0x21 / 0x23),
min (low byte, 0x20 / 0x22) } of the raw 8-bit codes of its window instead of a filter output. The window is exactly the
N = 2P raw samples (both cores; P = floor(D / 2) + 1 from 0x0D) that the K = 1 boxcar sums for the same output word, with
the boxcar's latency (12 write clocks from `adc*_w` to the memory; `tb_peak.v` measures the boxcar's latency first and
requires the peak words at the same one). The ADCs run at 200 MSa/s as for every 16-bit capture; the memory geometry, the
word counts of 0x0B / 0x0C / 0x0E / 0x14, the 0x1F / 0x20..0x23 readout and the sequencer's segments are the 16-bit
path's. In `decim_chan`: `pmx` / `pmn` are the captured pair's max and min (registered with the prefilter output `f`),
`pdx` / `pdn` the same one clock later (the integrator's alignment), `rmx` / `rmn` the running max and min of the window,
restarted on the edge where `dec_pc == 0` (`wstart`), which also hands the finished window to `pkw`; `pk0..pk4` follow the
comb pipeline (`sv[0..4]`) so the peak word reaches `out` on `sv[5]` like the filter output. Cost: 12 x 8-bit registers
and 4 compares per channel.

**One-register switch (the change against gen8).** On `gen8-features-wip` peak detect needed K >= 1 in 0x53. Here
`dec_on = (K != 0) | peak_en`: 0x56 bit 1 by itself selects the 16-bit path, forces the divider to 0 on the static side
of the rate compare (full-rate ADCs and write clock), runs `decim_chan` as K = 1 (order 0 already behaves as 1 in the
CIC, and only the peak pipeline's output is stored) and takes the window from 0x0D. 0x53 and 0x54 keep their values for
the way back: the device's Sample row writes 0x56 and nothing else, and 0x56 = 0 with 0x53 = 0 returns to the legacy
path at the divided rate. 0x29 byte 0 bit 7 then reads 1 with bits 1..0 = 0 (K as written); byte 1 bit 7 = peak active,
bit 6 = 0x56 bit 1 as written. The two bits are equal (peak is active whenever written, since `dec_on` carries it); both
stay for the contract's layout.

**FIR and shift.** `fir_ok` carries `~peak_en`: the FIR is off while peak detect is on (0x29 byte 0 bit 5 = 0 even with
bit 0 set at an eligible divider), and the shift 0x54 is not applied to the peak words.

**Trigger.** The 16-bit compare takes the window max for a rising detection and the window min for a falling one as the
high byte of the compared word (`dec_trig_sample = { trigger_edge ? min : max, 0x00 }`), so the firmware writes 0x55 =
level << 8: rising fires on the first window whose max reaches the level, falling on the first whose min drops below it.
The trigger position convention is unchanged (`trigger_address` = the word of the trigger sample - 1), and the trigger
types of C1 work on the same stream.

**Firmware (contract section 4; item C6).** SET_DECIM mode 4 "peak" writes 0x56 bit 1 (0x53 may stay as it is), 0x55 =
level << 8 at the arm, no shift. Records: READ_RAW layout 3 (per channel `count` min bytes then `count` max bytes),
GET_CAPTURE_INFO byte 79 bit 1; calibration with the 8-bit per-core model's mean gain (the two bytes are raw codes from
mixed cores), no gain16 / corr16. Averaging of peak words is refused (SET_SEQ mode 2 with peak on: E_STATE detail 7);
segments are fine. Settle after a 0x56 write as after any filter write, (K + 10) N x 5 ns + 1 us (K = 0 counts as 1),
before 0x01. Check 0x29 after the write: byte 0 bit 7 = 1 and bit 5 = 0, byte 1 bits 7..6 = 11.

**Resources and timing** (TD 4.6.8 SP1, `build.sh ~/c2/b1` on branch `fpga-c2` alone, source fb0cf59; a synthesis check,
not a release build): 5126 LUT / 4694 reg / 5893 LE against 0x153B's 4990 LUT (the +136 LUT and about +290 registers are
the two peak pipelines, 12 x 8-bit registers and four 8-bit compares per channel, plus the `dec_on` term), 48 EMB9K,
2 EMB32K, 2 of 3 M18x18, 33 IOB input registers. `sample_write_clock` 0 errors, 9.463 ns (0x153B: 9.231; the peak
pipeline lives in this domain), `i_mcu_clk` 19.852 ns and `clkc[1]` 2.528 ns with 0 errors, `clkc[0]` 60 errors (the SDC
artefact). `clk_200MHz`: **2 setup errors, minimum period 5.504 ns, worst slack -0.504 ns**, both on the sequencer's
post-pass enable (`q_st` / `tab_we` into `pp_i_reg.ce`, 62 % net: one 1.8 ns route on a fanout-1 net), a path peak detect
does not touch: its one `clk_200MHz`-side change, `dec_on` into the static side of the divider compare, is not among the
failing paths. The same class of roll hit 0x153B's first build (section 11 "Build": a neutral rewrite re-rolled it). The
merged 0x153C build (C4) decides; if the roll persists there, register `pp_i_reg`'s enable one clock earlier or re-roll.
`build/FPGA.bin` sha256 `4c4b0339f7c8183fa840382993ebf2117b8f3a7361dd05ec640041a8fcb4b891` (not pinned, not for flashing).

**Tests.** `tb/run.sh` step 15, `tb_peak.v` (gen8's, rebased onto the 0x153A capture: the monitor logs `adc*_w` at each
write clock edge, which is what the decimator latches): the K = 1 boxcar reference, then { max, min } for every word at
N = 2 / 8 / 20 / 200 with K = 1..3 (the FIR enabled at 20 and 200: it stays off), the same at K = 0 with 0x56 bit 1 alone,
the way back (0x56 = 0 at K = 0: legacy words again, 307 write clock edges for 306 words), the 0x29 read-backs, the
READ_RAW bytes, the alias demonstration (6 ns spikes on a flat signal at 5 MSa/s: sample skipping caught 15 of 420 spikes
on seed 12345 and 23 of 485 on 4242, peak detect 202 of 204 and 200 of 201, the missing ones at the record edges) and the
trigger on the window max (K = 1 + peak, rising at 150) and min (K = 0 + peak, falling at 128). **45 checks, 0 failed on
seeds 12345 and 4242** (15 s); every word of every peak capture matched its window at latency 12, the boxcar's. Steps
1-13 are unchanged in result (0x56 bit 1 = 0 in all of them); `tb_long.v` connects the new `decim_chan` inputs.

**Bench checks** (contract 9.2 item 6, with the owner): peak detect at 1 MSa/s on a 100 kHz square and on a narrow DDS
duty (or the 0x153D pattern generator's 6 ns spikes): every spike visible, the min / max envelope stable over 50 records;
0x29 reads byte 0 = 0x80 and byte 1 = 0xC0 with 0x53 = 0x54 = 0; a falling trigger at the envelope's low edge triggers
where the Scope view shows the band's lower edge crossing the marker.
### 15. Pulse pattern generator and pattern trigger engine; version 0x153D, item D2 (fpga-d2, 2026-10-21)

Contract: `docs/plans/fpga-0x153c.md` sections 2.1 (0x6C-0x6F, 0x76-0x7D, 0x50, 0x51 / 0x52 retired), 2.2 (0x39, 0x42),
2.4 (0x15 = 3), 7 and 9.1 step 18; item D2 of section 10. The DDS of 0x1532..0x153C (`freq_generator_dds_pwm`, 0x51 /
0x52) is removed; the pulse pattern generator (module `pat_gen`) **is the generator**: 0x50 = 1 drives the calibration pin
with its level, and the square wave of the generator menu, the wizard, the Bode engine and SET_GEN is a two-step continuous
pattern. The generator gets its own trigger engine (a third `trig_qual`). The logger (D1) is documented in section 14 by
its agent; D3 integrates and sets the version byte 0x3D. With 0x6D = 0 and 0x7D = 0 (power-on) the capture is unchanged
(the equivalence steps of `tb/run.sh`) and a firmware that writes only 0x50 drives the idle level 0 / hi-Z, which is what
the contract defines for the generator behind 0x50.

#### Register map

Writes (`pat_gen` takes the MCU-side registers over through toggles synchronised into clk_200MHz; a 0x6C entry is handed
over complete on its last byte, a 0x6D / 0x7D byte 3 .. 4 clocks after its strobe):

| Cmd | Bytes | Bits | Meaning |
|---|---|---|---|
| 0x15 | 1 | 1..0 | **3 = PATTERN**: the capture triggers on the pattern's marker (with 0x6D bit 3); CH1 / CH2 / EXT and the qualified types are off. Up to 0x153C value 3 acted as EXT |
| 0x50 | 1 | 0 | unchanged: 1 = the pin is driven (now with the pattern's level: a step's level, else the idle level), 0 = hi-Z (EXT input). Power-on 0 |
| 0x51, 0x52 | - | | **retired**: no decoder entry, the bytes are ignored (the DDS phase increment and duty) |
| 0x6C | 7 | byte 0 | { **end**, **mark**, **level**, index[4:0] }: index 0 .. 7 (8 .. 31 ignored), end = the last step of the burst (entry 7 acts as end), mark = the step raises the marker |
| | | bytes 1..4 | u32 **n**, the integer duration in 5 ns clocks, high byte first; **n >= 2** (the firmware enforces it: n < 2 gives a 1-clock step with a wrong carry) |
| | | bytes 5..6 | u16 **f**, the fraction in 1/65536 clock: the step lasts n + f / 65536 clocks on average (10 ns .. 21.47 s) |
| 0x6D | 1 | 1..0 | **mode**: 0 off (the pin holds the idle level; a running burst is abandoned at once), 1 one burst per start, 2 R bursts with a gap between them, 3 continuous (burst, gap, burst, ...; a write of 3 while idle is itself a start) |
| | | 2 | **idle level**: between bursts, in the gap and in mode 0 |
| | | 3 | **marker is a trigger source**: with 0x15 = 3 the capture triggers at a marked step (both needed; either alone never triggers) |
| | | 4 | **start now** (self-clearing): accepted from the MCU when bits 7..6 = 0 or 2, in modes 1 .. 3, while no burst or gap runs |
| | | 5 | **commit** (self-clearing): the shadow entries 0 / 1 become pending; taken at the next step boundary (a start counts) |
| | | 7..6 | **start source**: 0 the MCU (bit 4), 1 the pattern trigger engine, 2 both, 3 the engine |
| 0x6E | 2 | 15..0 | **R**, bursts per start in mode 2 (0 = 65536) |
| 0x6F | 4 | 31..0 | **gap** between bursts, 5 ns clocks (0 = none; mode 2: R - 1 gaps, none after the last burst; mode 3: a gap after every burst) |
| 0x76 .. 0x7C | | | the pattern trigger engine's configuration, the capture trigger's layouts field for field: 0x76 source as 0x15 (0 CH1, 1 CH2, 2 / 3 EXT), 0x77 polarity as 0x16, 0x78 level as 0x17, 0x79 (2 bytes) level as 0x55, 0x7A type and "longer" as 0x5E (types 4 .. 7 act as edge), 0x7B (3 bytes) N as 0x5F, 0x7C (3 bytes) H as 0x60 |
| 0x7D | 1 | 0 | **armed**: 1 = the engine watches (writing 1 restarts its qualifier, as 0x01 = 1 / 0 does for the capture's); one shot: the event clears it |
| | | 1 | **re-arm**: the qualifier is held in reset while the burst runs and restarts when it ends, so every event after a burst starts the next one |
| | | 2 | **clear the event flag** (0x42 bit 6), self-clearing |

Reads:

| Cmd | Bytes | Content |
|---|---|---|
| 0x39 | 4 | pattern status: byte 0 = { running (a burst or gap is in progress), in gap, **pending** (a commit not yet taken), 0, 0, step[2:0] }, bytes 1..2 = bursts completed since the last start (u16, wraps), byte 3 = { 0, 0, 0, 0, 0, 0, marker seen since 0x01 = 1, mode != 0 }. Asynchronous |
| 0x42 | 3 | pattern trigger engine status, laid out as 0x34: byte 0 = { **1 = own engine**, event seen since 0x7D bit 2, counting, pulse began, source EXT, longer, type[1:0] }, bytes 1..2 = edges still to count (Nth edge). Asynchronous |

The square wave (7.4): entry 0 = high (n_h, f_h), entry 1 = low with end (n_l, f_l), mode 3: the period is P16 = (n_h + n_l)
x 65536 + f_h + f_l in 1/65536 clocks, f = 200 MHz x 65536 / P16, resolution 2^-16 / P relative (7.6e-11 at 1 kHz); the
firmware's P16 / H16 arithmetic of the contract is reproduced in `tb_pattern.v` (`gen_calc`). Enable: 0x6D = 0, 0x6C index 0,
0x6C index 1, 0x6D = 0x33 (mode 3 | start | commit); a change on the run: index 0, index 1, 0x6D = 0x23 (18 strobes).

#### Design (`zaklad.v`: module `pat_gen` after `trig_qual`; the instance `u_pat` and the pin assign in place of the DDS; the engine block "Pattern trigger engine" after C1's `ext_qev_r`; the decoder / read-mux entries after C3's)

- **Clock domains.** Everything in `pat_gen` runs on clk_200MHz. The MCU side keeps a staged 0x6C entry (56 flops), the 0x6D
  byte, 0x6E, 0x6F and the engine's 0x76-0x7D; `pat_wr_t` toggles on the 7th byte of a 0x6C, `pat_ctl_t` on every 0x6D,
  `pt_ctl_t` on every 0x7D (three 3-flop synchronisers). The data a toggle refers to was written on the same or an earlier
  strobe, so it is settled when the toggle is seen (the next strobe is >= 50 ns away). A 0x6C entry is therefore written
  atomically (no half-written entry can be read by the engine); 0x6E / 0x6F are read when a run starts / a gap is prefetched
  (write them before the start).
- **Table.** `tab` = 8 x 51 bits { end, mark, level, n, f } as flops (TD would not find block RAM for it: none is free);
  entries 0 and 1 have shadows `sh0` / `sh1` where their 0x6C writes land. `pending` is set by the commit and cleared when
  the shadows are copied: at a step boundary or at a start. The prefetch reads the shadow instead of the table while a
  commit is pending (or arrives on the same clock), so a start always uses the committed entries from step 0.
- **Segments.** A segment is a table step, the gap (a pseudo-entry: n = 0x6F, f = 0, the idle level) or IDLE. The engine
  keeps the segment in progress (`cur_*`), the next one (`nx_*`, its entry prefetched at the previous boundary) and the kind
  and index of the one after it (`nn_*`, decided a clock after `nx_e` changes from end / mode / the burst count / gap != 0).
  Burst counting is an 18-bit signed down counter `d` = R - 1 - bursts started: its sign says the current burst is the last
  (mode 2), with the "d - 1" case when the next segment is step 0 of a new burst.
- **Durations.** `cnt` is a 33-bit down counter with a frozen sign: a boundary loads n - 2 + carry (one 33-bit add with a
  carry-in: `{1'b0, n} + {{32{1'b1}}, carry}`), the sign sets n + carry - 1 clocks later, the next boundary is the clock
  after that, so the step lasts exactly n + carry clocks. The fraction: `acc_sum = acc + nx_e.f` is registered during the
  step (the contract's pipelined form) and the boundary takes `acc <= acc_sum[15:0]`, carry = `acc_sum[16]`: step k lasts
  n_k + carry(acc + f_k), the Bresenham rule, so the cumulative actual time is floor(the cumulative ideal) and every edge
  lies 0 .. 1 clock early. `acc` is cleared at a start only (not at a pending take, not at the wrap), so a burst is
  reproducible edge for edge and the mean frequency stays exact across periods and across a frequency change. The prefetch
  is what makes the two-clock pipeline fit a 2-clock step: `nx_e` is loaded at the boundary before the step it describes,
  `acc_sum` one clock later, both ready at the earliest next boundary.
- **Double buffer, exactly.** A commit during step k: `pending` = 1; at the boundary into step k + 1 the shadows are copied
  into the table and `pending` clears, but step k + 1 was prefetched before (old entry); the prefetch made at that boundary
  reads the shadows, so the new entries run from step k + 2 (one step later than the contract's prototype wording "from the
  next step"; the firmware's view through `pending` is the same, and fw-d5's `fpga_model.c` models this rule). At a start:
  all new from step 0. Each step is always a complete old or new step: no glitch, phase-continuous. A second commit before
  the take replaces the first (the contract's handshake: poll bit 5 or pace the writes).
- **Starts and stops.** `go` = (0x6D bit 4 with source 0 / 2) | (0x6D mode 3 written) | (the engine's event with source
  1 / 2 / 3), accepted while not running and mode != 0: `cnt <= 0` (the sign sets after 2 clocks: step 0 begins then, 2
  clocks after the start is seen), `acc <= 0`, `bursts <= 0`, `d <= R - 1`, `nx_e <= entry 0`. A mode 0 write stops at once
  (level = the idle level). A start or mode-3 write while running is ignored (no restart). The marker is a one-clock pulse
  registered with the level change of a mark step; `mark_seen` is sticky until 0x01 = 1.
- **Pin.** `o_1khz_calib = gen_out_enable ? pat_level : 1'bZ`; `pat_level` is a clk_200MHz flop (the DDS's was on 50 MHz).
  0x28 bit 1 still reads `gen_out_enable`.
- **Marker into the capture.** The EXT block's `ext_pending` set term becomes `trigger_check_enable & (trig_src_pat ?
  (pat_marker & pat_mark_en) : (ext_edge | ext_qev))`: with 0x15 = 3 the pin's edges and the EXT qualifier are off and the
  marker takes the EXT path (applied on the next write clock rise: 0x14 = the last word written before the marker clock).
  `trig_src_pat` is an own copy of 0x15 = 3 on the 0x15 data strobe (as the counter's `k_t15_*`), so C1's 0x15 case is
  untouched: `trigger_ext` (bit 1) keeps the CH1 / CH2 compare off for the edge type, `trig_src_ext` keeps the ADC
  qualifier's `trigger_previous` on the non-firing side for the qualified types. The counter's source 0 reads 0x15 = 3 as
  EXT (unchanged); 0x34 bit 3 reads 1 for source 3 (bit 1 as written).
- **Pattern trigger engine** (own-engine variant, contract 7.5): `qual_pat`, a third `trig_qual` on clk_200MHz. CH1 / CH2:
  `pq_s` is registered on the write clock at every `swc_adv` from the channel's stream sample against the engine's level
  (legacy: core A `adc*_w` >= 0x78; K >= 1: `dec_out*` >= 0x79), i.e. the instant `trigger_data` loads, XNOR the detect
  direction (polarity, inverted for the pulse width); the qualifier takes it with `ce = swc_rise_q & adv_d` (the
  sequencer's settled crossing, 7.5 ns after the rise; the multicycle of `zaklad.sdc` covers it). EXT: `ce = 1`, `a =
  ext_level_prev ~^ direction`. Event: edge = `pq_ap & ~pq_a` (the previous sample pre-side, this one post-side), pulse
  width / Nth edge = the qualifier's `fire` registered at the previous ce and this sample post-side (what
  `trigger_previous` does for the capture), timeout = `fire`. `pq_take` = event & armed & ~running goes to `pat_gen` and
  sets the 0x42 flag; one shot clears `armed`; re-arm holds the qualifier in reset while the burst runs. The qualifier is
  also reset when 0x7D bit 0 = 1 is written. Nothing of it is on the capture's timed path.
- **Latency.** Step 0 begins 2 clocks after the event clock. CH1 / CH2: the event clock is 7.5 ns after the write edge of
  the qualifying sample, so the burst starts 17.5 ns after that edge, i.e. one word after the sample before it (the
  contract's "one write clock plus 3 to 4 cycles"); the ADC pipeline in front (encode, AD9288, capture stages, the K >= 1
  filter) is the capture's own. EXT: pad edge to burst = synchroniser (2) + majority (1) + `ext_level_prev` (1) + the
  event's `ap` (1) + 2 = 7 clocks, 35 ns (the contract estimated 20 ns, it did not count the EXT filter).

#### Deviations from the contract (all stated to fw-d5, modelled in `fpga_model.c`)

1. The committed entries apply from the second boundary after the commit (the prefetched step completes), not the first;
   `pending` clears at the first. At a start everything is new.
2. A mode-3 write while idle is a start (the contract's start list has the MCU bit and the engine only); a start bit or
   mode-3 write while running is ignored. The firmware's enable sequence (0x33) and change sequence (0x23) both work.
3. The marker triggers the capture only with 0x15 = 3 **and** 0x6D bit 3 (the contract lists both; this is the AND).
4. Entry 7 without an end bit acts as end; indices 8 .. 31 are ignored (the contract: "0 .. 7 in 0x153D").
5. EXT event latency about 35 ns, not 20 (above). The engine's N / H / level / type registers are read live (the firmware
   writes them before 0x7D = armed, as fw-d5's sequence does).
6. Own engine built (0x42 bit 7 = 1); the routed fallback of 7.5 was not needed (Build below).

#### Tests (`tb/tb_pattern.v`, run.sh step 18, `ONLY=pattern`)

`tb/run.sh` step 18, `tb_pattern.v` (`ONLY=pattern`, about 9 minutes: the 1 kHz and 999.5 Hz squares are the cost), one
instance with the AD9288 model and the bus sequences of tb_trigtypes.v, the pad watched for every edge. **10462 checks,
0 failed, PASS on seed 12345 (511 s)**; seed 4242 and the full 18-step run: see the handoff in `fpga/al3/AGENTS.md`.
- G: 0x50 alone from power-on: the pin hi-Z (`We0`), driven low (`St0`) with 0x50 = 1, unchanged by 0x51 / 0x52 writes,
  high with 0x6D bit 2, 0x28 bit 1 follows 0x50, 0x39 reads 0.
- S: the firmware's SET_GEN sequences (`gen_calc`: P16 / H16 of 7.4; enable 0x6D = 0x33, change 0x23). 10 MHz, 1 MHz,
  7.3 MHz (1000 periods each), 10 kHz 25 %, 1 kHz, 999.5 Hz (3 periods each), 3.5 + 2.25 clocks (1000, changed on the
  run and from a fresh start): every edge against the ideal time. Fresh starts: 0 .. 1 clock early exactly (10 MHz 0.000,
  3.5 + 2.25: -3.750 .. 0.000 ns); after a change on the run the deviations carry the accumulator's residual, their span
  stays under one clock (7.3 MHz: -2.135 .. +2.808 ns, span 4.943; 999.5 Hz: -0.750 .. -0.125). Mean period and mean
  high time equal P16 / 65536 and H16 / 65536 clocks within 1 / periods in every case (1 kHz exactly 1000000.0000 ns,
  999.5 Hz 1000500.0000 against 1000500.2501 ideal over 3 periods, 7.3 MHz 136.9850 against 136.9863 over 1000). The pad
  equalled `pat_level` on every clock while driven.
- B: 50 kHz square (2000 + 2000) changed to 1500 + 32768/65536 and 1200 + 16384/65536: pending reads 1 right after the
  commit and 0 after the next boundary; every step is a complete old or new step (the log lists them); the first new step
  lasts 1500 + carry(acc + 0x8000) with the accumulator as it was (acc 32768 -> 1501 clocks); a commit replaced by a
  second commit before the boundary: the first values (1700) never appear, the second (1300) run; mode 0 stops at once.
- P: a 5-step table (4, 7, 3, 12, 5 clocks, levels alternating, step 2 marked, end on step 4): mode 1 = one burst and
  the idle level, one marker pulse, 0x39 = not running / 1 burst / mode active; two starts give whole bursts; mode 2 R = 3
  gap 20 = 18 edges with the intervals 4 7 3 12 5 20 ... exact; gap 0 joins the bursts (the 5 + 4 clock levels merge);
  0x39 inside a long gap = running, in gap, 1 burst done; R = 2 ends with bursts = 2; mode 3 repeats, a second mode-3
  write does not restart it, mode 0 stops at once; idle level 1; an 8-step table without an end bit runs 8 steps once
  (entry 7 acts as end), index 9 ignored, a commit while stopped stays pending and is taken at the start; two bursts of
  six fractional steps are identical edge for edge (acc cleared at the start) and 0 .. 1 clock early (-4.688 .. -2.500).
- M: divider 49 (500 ns words), normal mode, 0x15 = 3, 0x6D bit 3, a 2 us / 2 us / marked pattern started 3 .. 4 us after
  the pretrigger filled: the capture triggers, 0x18 = 1, the marker clock lies between the writes of words 0x14 and
  0x14 + 1 (two runs: marker at write address 116, 0x14 = 115), 0x39 byte 3 bit 1 set and cleared by 0x01 = 1; without
  bit 3 an auto capture times out untriggered; with 0x15 = 2 the EXT path triggers on the burst's first edge, 4 us before
  the marker (the 0x1533 behaviour: the pin reads back the generator).
- E: a one-step burst (40 clocks high) started by the engine, every type (edge, pulse width shorter / longer, Nth edge 3,
  Nth edge 4 after an idle time of 20, timeout), both polarities, two runs each, on CH1 legacy 10 MSa/s, CH2 legacy, CH1
  CIC2 N = 20 and EXT: the model of tb_trigtypes.v run on the stream the engine saw (logged at its ce clocks) finds the
  event sample, and the burst begins 6 clocks (29.9 ns) after the clock that processed it, in all 96 runs. Stimulus
  (the signal's edge) to burst: 720 .. 870 ns on the legacy 10 MSa/s channels (the AD9288 pipeline, the capture stages
  and one word), 280 .. 430 ns with CIC2 N = 20, 51 .. 60 ns on EXT. One shot: one burst for five edges, disarmed, the
  0x42 flag set and cleared by 0x7D bit 2; re-arm: five bursts for five edges; start source 0: the event sets the flag
  but starts nothing, the MCU start runs; source 1: the MCU start is ignored; source 2: both start; the capture on CH1's
  first edge and the engine on CH2's third edge from the same stimulus, both fire (own engine).

#### Build (TD 4.6.8 SP1, `build.sh`, this branch alone on the merged C1 + C3 source; D3 builds the merged 0x153D)

Builds of this branch alone (the merged C1 + C3 source plus D2; the version byte still 0x3B; D3 builds the merged 0x153D
with the logger), `FPGA_phy.area` / `FPGA_phy.timing`, against the C1 + C3 merged build of 6079 LUT / 5177 reg / 6822 LE:

| Build | LUT | reg | LE | `clk_200MHz` | Note |
|---|---|---|---|---|---|
| b1, first RTL (one-clock 8:1 prefetch with the shadow bypass and the gap mux) | 7570 (87.6 %) | 6055 | 8279 | 532 errors, 7.751 ns | the prefetch mux missed by 2.75 ns (6 levels, 63 % routing) |
| b4, the three-stage pipeline with the one-hot read | 7167 (83.0 %) | 6125 | 8055 | 278 errors, 6.202 ns | the engine's event into the start (fanout 108) -1.2 ns; the rest routing |
| **b5, as committed** (start and event registered) | **7198 (83.3 %)** | **6127 (70.9 %)** | **8061 (93.3 %)** | **27 errors, 6.62 ns, TNS -17.5** | `sample_write_clock` 9.617 ns 0 errors, `i_mcu_clk` 0 errors, `clkc[1]` 0 errors; `clkc[0]` the SDC artefact |
| b5 without the pattern trigger engine (variant) | 6747 (78.1 %) | 5958 | 7621 | 54 errors, 5.624 ns | the engine costs **451 LUT / 169 reg** |
| b5 with `STEPS = 4` (variant) | 7026 (81.3 %) | 5919 | 7909 | 24 errors, 6.119 ns | entries 4 .. 7 cost **172 LUT / 208 reg** |

So D2 costs **+1119 LUT / +950 reg / +1239 LE**: the pattern core (table, shadows, pipeline, counters, control, the 0x39 /
0x42 read bytes, the DDS removed) 668 LUT / 781 reg, the engine 451 / 169. The contract (1.2) estimated +537 LUT for D2;
the difference is the pipelined prefetch (three 51-bit stages and the 357-LUT one-hot read), the 33-bit counter with its
adder and the engine's level compares, and the register count was not budgeted at all: at 8061 of 8640 LEs (93 %) the
placer no longer routes the design's old long paths. **b5 misses `clk_200MHz` on paths that are not the pattern's**: the
32-bit divider compare into `sce_q` / `swc_sh` (Atlan's, -3.44 ns on 3 levels), the sequencer's accumulator RAM data and
address (-2.96 ns on 1 level: routing distance to the block RAM), the counter's `k_mx` compare (-1.62). The worst pattern
path in b5 has positive slack. The no-engine variant at 78 % fails on the same paths (54 errors), so the routed fallback
of 7.5 alone would not close timing either; it was not built. The measured cut table for D3 / the owner: `STEPS = 4`
-172 / -208 (the `pat_gen` parameter; `tb_pattern.v` assumes 8), the engine -451 / -169 (its block, the 0x76-0x7D cases,
read 0x42 and `pq_take` into `pat_gen`), C0's cruft -78 / -19 and div20 -53 / -24 (the latter shortens the worst path).
`~/d2/b5` on the maintainer's WSL: `FPGA.bin` sha256 `12d7956c659e24d2fbdfbb644684e97e98dd25202c19963547a8564cea407d95` (not a release image; nothing in `dist/`).

#### Bench checks (D3's bench, items 10 and 10b of the contract's section 9.2)

The wizard's 1 kHz on the post (counter within 1e-7: the counter and the pattern share the crystal); 999.5 Hz reads
999.5000 Hz at the 1 s gate; a frequency change on the Scope view's square leaves no glitch in 50 traces; a 50 us pulse
every 1 ms x 10 bursts with the marker as trigger gives 10 segments 1 ms apart; the engine on CH2 Nth edge 3 starting a
20 us pulse while the capture triggers on CH1; EXT as the engine's source with 0x50 = 0.

## Device facts from the datasheet

Source: Anlogic "AL3 series FPGA datasheet" DS001 v1.3 (Sept 2017, Chinese, maintainer's download `AL3A10LG144.pdf`, 84 pages).
Page numbers are PDF pages. Details and the configuration-pin analysis: `docs/fpga-flash-from-mcu.md`, "Datasheet check".

| Item | AL3A10 (TQFP144 `LG144`) |
|---|---|
| LUTs / FFs | 10,368 / 8,640 (p.6) |
| Distributed RAM | 69,120 bits |
| Block RAM | 48 x EMB9K + 2 x EMB32K = 507,904 bits (our design uses 32 of the 48 EMB9K) |
| Multipliers | 3 x M18x18 (simple multiplier only, no full DSP) |
| PLLs / global clocks / I/O banks | 2 / 16 / 8 |
| User I/O on TQFP144 | 91, with 32 LVDS pairs (p.7); the 184 figure in table 1-1 is the BGA256 |
| IDCODE | not given in the datasheet; 0x18006C31 comes from prjtang and the maintainer's JTAG read |
| LVCMOS33 / LVTTL33 input | VIL max 0.8 V, VIH min 1.9 V, up to VCCIO+0.3 V (p.63); optional hysteresis 250 or 450 mV (p.62) |
| Overvoltage | absolute maximum input 3.75 V, recommended maximum 3.6 V; clamp figure only "PCI-clamp 25 mA" (p.59-60) |
| Weak pull-up / pull-down | 35 to 250 uA, so about 13 to 94 kOhm at 3.3 V [inference] (p.62) |
| Configuration | MSPI (MSEL 001 fast 0x0B, 010 standard 0x03), CCLK 2 to 64 MHz selectable in the bitstream; CCLK is a dedicated pin (pin 12), MOSI (6), MISO (13), SPICSN (8) are reusable as user I/O after configuration (p.51-54, p.69-70) |

The external-trigger notes in "Changes" section 2 now quote these VIH/VIL values:
the table above confirms 1.9 V / 0.8 V for LVCMOS33, and the 1 kOhm series path is within the input ratings for a 3.3 V source.
