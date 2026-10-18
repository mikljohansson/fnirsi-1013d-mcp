# fpga/al3 - handoff for agents and humans

Anlogic AL3A10LG144C7 FPGA of the owner's FNIRSI 1013D (JTAG IDCODE 0x18006C31, config flash Winbond
W25Q80DV, JEDEC `ef 40 14`). Read `CLAUDE.md` at the repo root first; its flashing rules override anything here.

State on 2026-10-18:
- The unit's FPGA flash holds Atlan4's AL3 bitstream (version word 0x1532), written and verified by read-back
  (`docs/hardware-status.md`). The stock image is backed up.
- TD 4.6.8 SP1 runs headless in WSL. `SRC_REV=a947e78 ./build.sh` rebuilds Atlan4's original **byte-identical**
  to that bitstream in about 15 s.
- `src/` now holds our design, version word **0x1533**: the external trigger on the calibration pin (roadmap item 2;
  commits 526c969, 4601dec, 2839258). It is built (`out/FPGA-ext-trigger.bin`, sha256 `a9656703...1216`), meets
  timing and passes the iverilog testbench. It is **not flashed**. Firmware support exists in fw 0.2.9 (commit bebf901,
  not benched): 0x153x decodes as AL3, caps EXT_TRIGGER, 0x28 in GET_STATUS byte 42. Next are the flash and the
  bench checks in `README.md` "Changes", with that firmware loaded first.

## What is here

| Path | What |
|---|---|
| `src/` | Atlan4's AL3 TD project with our changes: `zaklad.v` (top `fnirsi_1013D`), `zaklad.adc` (pins), `zaklad.sdc` (clocks), `FPGA.al` (TD project, holds `<UCode>`), `al_ip/` (PLL and sample-memory IP), `FPGA commands.txt` (command table, current). Upstream material, licence unstated, attributed to Atlan4. Unmodified original: git a947e78. |
| `tb/` | iverilog testbench: `run.sh [seed]` simulates `src/zaklad.v` against Atlan4's original from git (`REF_REV`), `tb_ext_trigger.v`, `sim_models.v` (behavioural PLL/BRAM). `DUT=<file>` for mutation runs. |
| `out/` | Named bitstreams for the bench (gitignored `*.bin`): `FPGA-ext-trigger.bin`. |
| `reference-stock/` | pcprogrammer (pecostm32) clean-room re-implementation of the stock FNIRSI design (0x1432), GPL-3.0. Same pin map. Use it for "what did stock do" questions. |
| `bitstreams/` | Prebuilt images, gitignored except `README.md` (provenance and sha256). |
| `build.sh`, `build.tcl` | Headless TD 4.6.8 build (replay of Atlan4's GUI command sequence). `SRC_REV=<rev>` builds `src/` from git. |
| `build_donwulff.tcl` | Donwulff's TD 5.x flow (`FLOW=donwulff`); crashes in `report_io` on 4.6.8. |
| `td/td_install.sh` | Headless TD install into `~/anlogic` with listing check, licence copy and GUI-lib stubs. |
| `td/td_fetch.sh` | Captcha download helper for TD 5.0.3 from Sipeed (needs a human). |
| `td/compare_bin.py` | Size, hash, header words and byte diff of two `.bin` files. |
| `license/Anlogic.lic` | Owner's licence, gitignored. FEATURE TD, expires 2027-09-30, HOST_ID ANY. Renew before then. |
| `build/` | Build outputs, gitignored. |
| `TD_SETUP.md` | Full install steps, installer hashes, install log, alternative routes. |
| `README.md` | Toolchain survey, zaklad.v vs stock table, pin map, rebuild results. |

Deep design analysis (all variants, command tables, timing, limits): `research/notes/fpga-verilog-analysis.md`.

## Build (WSL Ubuntu 24.04)

TD is installed at `~/anlogic/TD_4.6.8_SP1_116866_NL` (licence copied to its `license/Anlogic.lic`), stubs in
`~/anlogic/shim`. To reinstall from scratch follow `TD_SETUP.md`. The installer zip is
`C:\Users\miklj\Downloads\TD_4.6.8_SP1_116866_NL.zip`, sha256 `12f8c2b2...3f15`.

Run from Windows PowerShell. Git Bash mangles `/mnt` paths; put complex commands in a script file.

```powershell
wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build.sh
wsl -d Ubuntu -- python3 -I /mnt/d/src/fnirsi-1013d-ai/fpga/al3/td/compare_bin.py /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build/FPGA.bin /mnt/d/src/fnirsi-1013d-ai/fpga/al3/bitstreams/atlan4-FPGA-0x1532.bin
```

- `build.sh` copies `src/` to `~/al3-build` (WSL-local, fast), fills `build.tcl` with the TD path and the user
  code, runs `td flow.tcl`, then copies `FPGA.bin`, `FPGA.bit`, `FPGA_rtl.area`, `FPGA_gate.area`,
  `FPGA_phy.area`, `FPGA_phy.timing`, `FPGA_phy.tsm` and `build.log` to `fpga/al3/build/`.
- Wall clock is 11 to 16 s. On failure it exits non-zero and prints "BUILD FAILED" with the first errors.
- Overrides: `TD_HOME`, `TD_SHIM`, `FLOW` (`atlan` default, `donwulff`), `SRC_REV` (git revision of `src/`); the
  first argument is the work dir.
- Current `src/` (0x1533) gives `build/FPGA.bin` sha256 `a96567031872ae99bd40f0722a111a08da0f6381b6c710b70998af784a341216`.
- Testbench: install iverilog once as root (`wsl -d Ubuntu -u root -- apt-get install -y iverilog`, done 2026-10-18),
  then run `wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/tb/run.sh [seed]`. It takes about 4 s and
  ends with `RESULT: 38 checks, 0 failed -> PASS`. Any change to `zaklad.v` must keep it passing. The equivalence
  phase is what proves "CH1/CH2 unchanged".

Expected output for Atlan4's original (`SRC_REV=a947e78`; before 2026-10-18 this was plain `src/`):

| File | Bytes | sha256 |
|---|---|---|
| `build/FPGA.bin` | 282,898 | `0d4c829c231b3bbe18c6dd6e815523b6e168f787561376aae502db6c1f370fd2` (= Atlan4's shipped image) |
| same, padded with 0xFF to 1 MiB | 1,048,576 | `8a9d3559566fbe9653ad52612818d336ce7f905498f422f228bb165335c42177` (= what is in the unit's flash) |
| `build/FPGA.bit` | 285,341 | varies; only the ASCII `# Date:` line changes |

If an unchanged build does not reproduce `0d4c829c...`, the toolchain or flow has drifted. Fix that before
trusting any design change. Builds are deterministic, so a changed hash means a changed input.

## TD 4.6.8 gotchas

- **Real libSM/libICE.** `bin/td` needs `libsm6` and `libice6` from apt. An empty `libSM.so.6` stub fails at
  load with `undefined symbol: SmcInteractDone`.
- **GTK stubs.** `libgtk-x11-2.0.so.0` and `libgdk-x11-2.0.so.0` are empty `.so` stubs in `~/anlogic/shim`,
  put on `LD_LIBRARY_PATH` by `build.sh`. Batch mode never calls them. Do not install GTK2.
- **Nested sub-commands.** Atlan4's GUI log lists `legalize_phy_inst`, `start_timer` and a second `read_sdc`,
  but those run inside `optimize_gate`/`place`/`route`. Issued by hand they make `route` segfault (exit 139).
  Keep `build.tcl` to the top-level commands.
- **User code.** bitgen `-g ucode:` is 8 zero bits + `<UCode>` from `src/FPGA.al` (`01010110`) + 16 zero bits.
  It lands in the `.bin` header packet `c1000006 00560000` plus a CRC. A different value changes 3 header bytes.
  `build.sh` reads it from `FPGA.al`; edit `FPGA.al` if you need another value.
- **Donwulff's flow** (`read_verilog -lib al3_macro.v`, `map`, `pack`) runs through `route` on 4.6.8, then
  `report_io` asserts (`Design::GetFloorColumn: Assertion placer_ failed`). It is for TD 5.x only.
- `import_device al3_10.db -package LQFP144` is accepted on 4.6.8. Donwulff reports 5.0.3 rejects package strings.
- `sh: Bad fd number` lines in `build.log` come from TD running bashisms under dash. Ignore them.
- TD 5.0.3 (Linux) and 5.0.4 (Windows) give better timing (Donwulff: 278 to 289 MHz on clk_200MHz) but are
  captcha-gated on the Sipeed mirror. Their output will not match the shipped image byte for byte.

## Reading the reports

- `build/FPGA_phy.area`, section "Utilization Statistics": `#lut`, `#reg`, `#le`, `#bram` (9K blocks),
  `#bram32k`, `#pad`, `#oreg` (output registers packed into IOBs; affects encode pin skew).
- `build/FPGA_phy.timing`: one block per clock starting `Timing constraint: clock: <name>`, then
  `N errors detected : ... (TNS = ...)` and `Minimum period is X ns` (Fmax = 1000 / X MHz), then the worst
  paths. The corner is TT 1.10 V 85 C, typical silicon, not worst case.
- `build/FPGA_phy.tsm` is a machine-readable summary. `build.log` has every command with its run time.

Numbers for Atlan4's original (`SRC_REV=a947e78`, equal to the shipped bitstream). The 0x1533 numbers are in
`README.md` "Changes": 635 LUT / 486 reg, `clk_200MHz` 4.459 ns TNS 0, `clkc[1]` 8.394 ns / 6 errors,
`clkc[0]` 8.298 ns / 50 errors. The worst `clk_200MHz` path is now `sample_write_clock` (negedge) to
`sampling_triggered`, slack +0.541 ns.

| Item | Value |
|---|---|
| LUT / reg / LE | 623 / 476 / 661 of 8640 (7.2 %) |
| BRAM9K / BRAM32K | 32 of 48 / 0 of 2 |
| Pads, IOB output regs | 60 of 93, 14 |
| `clk_200MHz` | min period 4.485 ns = 223.0 MHz, 0 errors, TNS 0 |
| `main_clock/pll_inst.clkc[0]` | 8.485 ns, 52 setup errors, TNS -140.9 ns |
| `main_clock/pll_inst.clkc[1]` (`clk_ADC`, 90 degrees) | 8.014 ns, 6 setup errors, TNS -15.8 ns |

How to read the errors:
- `clkc[0]` is the PLL output that also carries `clk_200MHz`. The net-based `create_clock` in `zaklad.sdc` and
  TD's auto-derived PLL clock analyse the same paths twice with different waveforms. Treat `clk_200MHz` as the
  real number and the `clkc[0]` errors as an SDC artefact until the SDC is cleaned up (item 4 below).
- The 6 `clkc[1]` errors are structural. The 32-bit `sample_rate_counter` compare makes `sample_clock_enable`
  on the falling edge of the 200 MHz clock, and the CE pins of the `adc*_enc*` flops capture it on the falling
  edge of the 90-degree clock, a 1.25 ns window. At sample rate 0 (200 MSa/s) the enable is static and the path
  is inert. At divided rates a miss shifts an encode edge by 5 ns. Fix it in RTL by retiming the enable into the
  `clk_ADC` domain, not with placer effort.
- STA covers only the PLL clocks. `clk_50MHz`, `sample_write_clock` and `i_mcu_clk` are unconstrained and there
  are no I/O delays, so the ADC capture and the MCU bus are not timed at all.
- The 160.4 MHz / 620 LUT figures in `fpga-verilog-analysis.md` section 10 come from a stale report in Atlan4's
  folder (dated 2026-01-09, a month before his bitstream). Use the numbers above.

## Flashing this unit

Route: header J2 next to the 25Q80, Sipeed FT2232D debugger channel A via D2XX, `firmware/tools/spiflash_d2xx.py`.
Tool docs are in `firmware/tools/README-spiflash.md`. The measured J2 pinout and wiring are in
`docs/hardware-status.md` (2026-10-18). Scope on battery, no USB, adapter power pins unconnected.

1. **Never flash without the verified dump in `backups/`.** Stock image: `backups/2026-10-18_fpga-25q80-stock.bin`,
   sha256 `90f65f08...0d253`. The tool also takes its own double-read pre-write backup.
2. The MCU side must run bootloader >= 0.8 (on the SD card), which accepts 0x1432 and 0x1532. A design that
   reports a new version word needs firmware that knows it.
3. Pad the 282,898-byte `.bin` to 1 MiB with 0xFF and keep the padded file and its hash in `backups/`
   (the tool pads short files itself, but a stored file makes the write reproducible):
   ```bash
   python3 -I -c "import sys;b=open(sys.argv[1],'rb').read();open(sys.argv[2],'wb').write(b+b'\xff'*(1048576-len(b)))" fpga/al3/build/FPGA.bin backups/NAME_padded.bin
   ```
4. Run `python firmware/tools/spiflash_d2xx.py probe` and expect `ef 40 14`. Then run
   `python firmware/tools/spiflash_d2xx.py write -i backups/NAME_padded.bin --yes`. It asks for the JEDEC ID to be
   typed, so it runs only with the owner present. It erases and programs only changed sectors and verifies by read-back.
5. Power-cycle. The diagnostics screen shows the FPGA version word. Log hashes and the result in `docs/hardware-status.md`.
6. Recovery: write `backups/2026-10-18_fpga-25q80-stock.bin` back the same way. The scope CPU cannot reach this
   flash, so a bad bitstream cannot brick the MCU side; the scope just has no working FPGA until the flash is rewritten.

Agents do not flash on their own. Flashing needs the owner at the bench and an explicit request.

## MCU-FPGA protocol and design structure

References:
- `D:\src\Fnirsi1013D\Verzia 1.xxx\FPGA for 1.xxx\FPGA commands.txt`: Atlan4's command table (read and write
  commands, byte order). Its 0x06 entry still shows 0x14/0x32; this design returns 0x15/0x32.
- `research/notes/fpga-verilog-analysis.md`: command tables for all variants, bus timing, limits.
- `README.md` here: zaklad.v vs stock table, pin map. `docs/protocol.md`: the PC-side protocol of our firmware.

`src/zaklad.v` (968 lines), top to bottom:
- Helper modules `clockdivider`, `pwmcounter`, `freq_generator_dds_pwm` (32-bit phase accumulator; output is
  `phase_acc < duty_threshold`).
- `pll main_clock`: 50 MHz `i_xtal` to `clk_200MHz` (clk0) and `clk_ADC` (clk1, 200 MHz at 90 degrees).
  IP in `al_ip/pll.v`.
- Clock dividers: 50 MHz peripheral clock, PWM counters for the offsets (25 kHz) and the backlight.
- DDS generator `u_freq` drives `o_1khz_calib` (P133) when `gen_out_enable` is set, else hi-Z. Cmd 0x50 enable,
  0x51 phase increment (32 bit), 0x52 duty. Default 85899 = 1 kHz, clocked from 50 MHz.
- `sample_memory samples`: 8192 x 32 dual port (32 BRAM9K). Port A writes `{adc2B, adc2A, adc1B, adc1A}` on
  `sample_write_clock` at the 13-bit `sample_write_address`. Port B is read by the MCU at `sample_read_address`.
- ADC encode generation on negedge `clk_ADC` (A is the complement of B); sample-rate divider on negedge
  `clk_200MHz` (100/(N+1) MHz rule).
- Capture logic on `sample_write_clock`: write address, pretrigger (0x0B) and total (0x0C) counts, trigger
  compare on the raw ADC core A inputs (rising/falling), auto time-out, trigger address.
- MCU bus on `i_mcu_clk`: command latch (`i_mcu_dcs` = 1), write decoder (0x01, 0x0B-0x0F, 0x15-0x17, 0x1A,
  0x1B, 0x32-0x38, 0x3C, 0x50-0x52), read mux (0x05, 0x06, 0x0A, 0x14, 0x18, 0x20-0x28, 0x41).
  Multi-byte values are big-endian and stepped by `mcu_state`.
- External trigger (0x1533), block after the generator pin assign. The pad is read through `ext_sync` (2 flops,
  first in the IOB), then a 3-sample majority filter (`ext_level`), then the edge select per `trigger_edge`. The
  latched `ext_pending` fires into `sampling_triggered` after the next `sample_write_clock` rising edge. Cmd 0x15
  `data[1]` = `trigger_ext` (value 2 = EXT) gates the ADC detect off. Read 0x28 = {ext_activity, gen_out_enable,
  ext_level}. P133 has PULLDOWN in `zaklad.adc`.
- Command codes still free in the decoders (checked 2026-10-18): writes 0x02-0x0A, 0x10-0x14, 0x18, 0x19, 0x1C-0x1E,
  0x20-0x31, 0x39-0x3B, 0x3D-0x4F, 0x53 and up; reads 0x01-0x04, 0x07-0x09, 0x0B-0x13, 0x15-0x17, 0x19-0x1F, 0x29-0x40,
  0x42 and up. Stock firmware wrote 0x28 (roll mode) and Donwulff says stock used 0x24/0x26 differently, so prefer
  0x29 and up, or 0x53 and up, for new codes.

## Wanted next (priority order)

Each item changes the bitstream, so it also needs firmware support and a version word or capability bit the
firmware can detect. Rebuild unchanged `src/` first to confirm the toolchain, then work on a copy or a branch.
Do not edit `src/` in place without keeping Atlan4's original reproducible (the hash check above).

1. **External trigger input on P133**: designed, built and simulated (0x1533, `README.md` "Changes"). Open items:
   - firmware support for 0x153x and caps EXT_TRIGGER: done in fw 0.2.9 (bebf901), not benched;
   - flashing and the five bench checks, including the pin input impedance and the EXT-to-CH1 skew;
   - a fixed trigger-point correction, if the bench shows one is needed.
2. **Deeper sample memory.** 16 BRAM9K and both BRAM32K blocks are free (about 12k to 15.8k words possible).
   The address registers (`pretrigger_samples`, `total_samples`, `trigger_address`, `sample_read_offset`) are
   13 bit with the high byte taking `[4:0]`. Widen them to 16 bit (the wire format already carries 16 bits),
   replace power-of-two wrap with explicit end-address wrap, and update the firmware ring arithmetic.
   Pointers: `fpga-verilog-analysis.md` section 11; `src/al_ip/sample_memory.ipc`.
3. **Trigger on both ADC cores.** The trigger compares only core A of each channel, one sample per write clock.
   Comparing A and B halves the trigger time uncertainty at 200 MSa/s. Pointer: section 11.
4. **I/O and clock constraints.** Add generated clocks for `clk_50MHz` and `sample_write_clock`, declare
   `i_mcu_clk` asynchronous, add `set_input_delay` on the ADC buses relative to the encode outputs, de-duplicate
   `clk_200MHz` vs `clkc[0]`, fix the 40 % `-waveform {0 2}`. Do this before judging any timing change.
   Starter block: `D:\src\FNIRSI_1014D_Firmware\fpga\README.md`; analysis in `FPGA_NOTES.md` there.
5. **200 MHz DDS clock.** Clock `freq_generator_dds_pwm` from `clk_200MHz` instead of 50 MHz for 4x frequency
   resolution and lower jitter. The phase increment scale changes by 4, so the firmware's 0x51 computation must
   follow. Pipeline the 32-bit compare to meet 200 MHz.

Also open: the structural `clkc[1]` violation (retime `sample_clock_enable`), symmetric IOB packing on the four
encode outputs, and the 32-bit sample-rate counter as critical path (down-counter with registered terminal count).
