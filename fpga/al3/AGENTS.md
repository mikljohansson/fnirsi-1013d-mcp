# fpga/al3 - handoff for agents and humans

Anlogic AL3A10LG144C7 FPGA of the owner's FNIRSI 1013D (JTAG IDCODE 0x18006C31, config flash Winbond
W25Q80DV, JEDEC `ef 40 14`). Read `CLAUDE.md` at the repo root first; its flashing rules override anything here.

State on 2026-10-19:
- `src/` holds version word **0x1539** (fpga-gen8): 0x1538 with the MCU read byte loaded on the falling edge of
  `i_mcu_clk` (the bench's mixed-byte readout fault), `i_mcu_clk` constrained, two timing refactors. Section "0x1539
  status" below; `README.md` "Changes" section 9. Built as `build/FPGA.bin` sha256 `f36c1f7a...ea64`, pinned in
  `backups/al3_seg_0x1539_f36c1f7a.bin` and `..._padded.bin` (`22db7b91...a6b8`). **Not flashed.**
- 0x1538 (fpga-gen7, the previous `src/`): 0x1537 with the encode flops on timing-met, stateless paths and the
  new read 0x33 (encode self-check). Section "0x1538 status" below; `README.md` "Changes" section 8.
  - Built as `out/FPGA-seg-0x1538.bin`, sha256 `0c24ecf6...dbbbd`, pinned in `backups/al3_seg_0x1538_0c24ecf6.bin`;
    padded flash image `backups/al3_seg_0x1538_0c24ecf6_padded.bin`, sha256 `2fc7ea5b...1087`. **Not flashed.**
- The unit's FPGA flash holds **0x1537** since 2026-10-19 (`docs/hardware-status.md`). It boots, but CH2 core A is bad in
  about 40 % of the 200 MSa/s captures and chip 2's cores are out of order (section "0x1538 status").
- 0x1537 (fpga-gen6, the previous `src/`): 0x1536 with two determinism fixes and no interface change. Section
  "0x1537 status" below; `README.md` "Changes" section 7.
  - Built as `out/FPGA-seg-0x1537.bin`, sha256 `426a3784...3b17`, pinned in `backups/al3_seg_0x1537_426a3784.bin`;
    padded flash image `backups/al3_seg_0x1537_426a3784_padded.bin`, sha256 `febc9299...e1f1`. **Not flashed.** A
    firmware that knows 0x1536 runs it unchanged.
- 0x1536 (fpga-gen5, the previous `src/`): 0x1535 plus segmented capture and hardware averaging (cmds 0x57-0x5D,
  reads 0x2A-0x32; `README.md` "Changes" section 6, design note `docs/plans/segmented-capture.md`). With 0x57 = 0 it is
  cycle-identical to 0x1535 (git e2aec2e, the tb's `REF1535_REV`).
  - Built as `out/FPGA-seg.bin`, sha256 `4eaf6811...b48b`, pinned in `backups/al3_seg_0x1536_4eaf6811.bin`; padded flash
    image `backups/al3_seg_0x1536_4eaf6811_padded.bin`, sha256 `66866c84...7408`. **Not flashed**; needs firmware that
    knows 0x1536 (a 0x153x-aware firmware that never writes 0x57 sees 0x1535).
  - Uses every block RAM (48 EMB9K, 2 EMB32K): the "deeper memory" item below now needs a trade (section "0x1536 status").
- The unit's FPGA flash holds Atlan4's AL3 bitstream (version word 0x1532), written and verified by read-back
  (`docs/hardware-status.md`). The stock image is backed up.
- TD 4.6.8 SP1 runs headless in WSL. `SRC_REV=a947e78 ./build.sh` rebuilds Atlan4's original **byte-identical**
  to that bitstream in about 15 s.
- `src/` now holds our design, version word **0x1535** (fpga-gen4): 0x1534 plus the CIC compensation FIR (cmd 0x56,
  read-back 0x29 bits 6/5; `README.md` "Changes" section 5). With 0x56 = 0, or at a divider the FIR does not engage at,
  it is cycle-identical to 0x1534 (git 6d4c8ad, the tb's `REF1534_REV`).
  - Built as `out/FPGA-fir.bin`, sha256 `5040a2ae...aa96`, pinned in `backups/al3_fir_0x1535_5040a2ae.bin`; padded
    flash image `backups/al3_fir_0x1535_5040a2ae_padded.bin`, sha256 `5ade9e75...88a5`. **Not flashed**; needs
    firmware that knows 0x1535 (a 0x153x-aware firmware that never writes 0x56 sees 0x1534).
- 0x1534 (fpga-gen3, frozen) adds the decimation filter (boxcar, CIC2,
  CIC3; cmds 0x53-0x55, read 0x29) for the spectrum analyser, on top of 0x1533, the external trigger on the
  calibration pin (commits 526c969, 4601dec, 2839258; source rev 2839258 is the tb's 0x1533 reference).
  - With 0x53 = 0 (the default) it is cycle-identical to 0x1533.
  - Built as `out/FPGA-decim.bin`, sha256 `83140510...20c1`, pinned in `backups/al3_decim_0x1534_83140510.bin`;
    padded flash image `backups/al3_decim_0x1534_83140510_padded.bin`, sha256 `c387a15e...c0a0`.
  - Meets timing (with the new `sample_write_clock` constraint) and passes `tb/run.sh`.
  - Interface, design, gain/alias table and bench checks: `README.md` "Changes" section 3. The firmware spec went to
    the coordinator on 2026-10-18.
  - **Not flashed.** It needs firmware that knows 0x1534. A 0x153x-aware firmware that does not use 0x53 sees 0x1533
    behaviour.
- The FPGA in the unit has not changed: it still holds 0x1532, not the 0x1533 design described below.
- Bench status of 0x1533:
  - Pinned copy: `backups/al3_ext-trigger_0x1533_a9656703.bin`.
  - **Not flashed.** It needs fw 0.2.9 or later on the MCU first. 0.2.9 (commit bebf901) decodes 0x153x as AL3,
    sets caps EXT_TRIGGER and reports 0x28 in GET_STATUS byte 42.
  - Flashing waits for the owner at the bench with the FT2232D programmer, following "Flashing this unit" below.
    The verified stock dump `backups/2026-10-18_fpga-25q80-stock.bin` must be at hand; never flash without it.
- Calibration pin: the owner's DMM confirmed -1.23 / +1.87 V on the post against the probe ground (2026-10-18). That
  matches VRF1, the probe ground, sitting about +1.24 V above FPGA/USB ground (`README.md` "Changes" section 2).
- Pending with the owner: bench tests 1 to 4 of that section.
  1. EXT referenced to USB ground.
  2. EXT on the probe ground, without and with a series LED.
  3. Ground-loop check.
  4. Open post.
  Then the EXT-to-CH1 skew at 200, 10 and 1 MSa/s, and `docs/hardware-status.md` updated after each step.

## What is here

| Path | What |
|---|---|
| `src/` | Atlan4's AL3 TD project with our changes: `zaklad.v` (top `fnirsi_1013D`), `zaklad.adc` (pins), `zaklad.sdc` (clocks), `FPGA.al` (TD project, holds `<UCode>`), `al_ip/` (PLL and sample-memory IP), `FPGA commands.txt` (command table, current). Upstream material, licence unstated, attributed to Atlan4. Unmodified original: git a947e78. |
| `tb/` | iverilog testbench, `run.sh [seed]`, nine steps (7: `tb_trigpos.v`, trigger position determinism with an AD9288 pipeline model; 8: `tb_seqcontract.v`, the firmware's sequencer byte sequences, transcript `contract_0x1538.txt`; 9: `encphase.sh` with `tb_encphase.v`, the encode pins against the write clock after the firmware's arm sequences, with delay-annotated variants; `edge_phase.py`: FIR overshoot / DDS beat / spectrum versus edge phase; `REF_SRC_DIR` for worktrees; the sixth: `tb_seg.v` against our 0x1535, `REF1535_REV`, checked by `seg_model.py`; `mutate_seg.py` runs its 21 mutants), first four: `tb_ext_trigger.v` against Atlan4's 0x1532 (`REF_REV`); `tb_decim.v` against our 0x1533 (`REF1533_REV`), which logs decimated captures; `cic_model.py check` (direct-form reference model incl. the FIR; `cic_model.py table` prints the gain/alias/droop table); `tb_fir.v` against our 0x1534 (`REF1534_REV`), FIR captures checked by `cic_model.py` too. `tb_long.v`: long free-running periodicity/wrap test of the filter modules (step 5). `square_alias.py`: alias bin for a DDS square through the filter models. `fir_design.py` designs the FIR taps. `sim_models.v` holds the behavioural PLL/BRAM. `DUT=<file>` for mutation runs, `ONLY=ext`, `decim`, `fir`, `long` or `seg` for one step; `tb_fir.v` takes `+p1=N` (phase 1 iterations) for quick runs, `tb_seg.v` the same through `SEG_P1`. |
| `out/` | Named bitstreams for the bench (gitignored `*.bin`): `FPGA-ext-trigger.bin` (0x1533), `FPGA-decim.bin` (0x1534), `FPGA-fir.bin` (0x1535), `FPGA-seg.bin` (0x1536). |
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
- Wall clock is 11 to 16 s (about 35 s for 0x1534). On failure it exits non-zero and prints "BUILD FAILED" with the first errors.
- Overrides: `TD_HOME`, `TD_SHIM`, `FLOW` (`atlan` default, `donwulff`), `SRC_REV` (git revision of `src/`); the
  first argument is the work dir.
- Current `src/` (0x1537) gives `build/FPGA.bin` sha256 `426a3784883b82c9c9f42870584e64d1d082cdcb38c8b62db143e161e2633b17`
  (two builds in different work dirs, same hash; about 60 s). 0x1536 (`SRC_REV=27c0c75`) gave `4eaf6811814c58e1f00e648b97e7a608bb5ca387fb036f21714e91667959b48b`. 0x1535 (`SRC_REV=e2aec2e`) gave
  `5040a2aeede071019bca0ffee47d765b87d980e1e47ca01ac32cb875c839aa96`. 0x1534 (`SRC_REV=6d4c8ad`) gave `83140510dfbfef56cc5ed15d70866e6bc52c407a41fd0ea480e8993df5b820c1`.
  0x1533 (`SRC_REV=2839258`) gave `a96567031872ae99bd40f0722a111a08da0f6381b6c710b70998af784a341216`.
- Testbench: install iverilog once as root (`wsl -d Ubuntu -u root -- apt-get install -y iverilog`, done 2026-10-18),
  then run `wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/tb/run.sh [seed]`. It takes about 17 minutes with step 6 (seed 12345: 1038 s)
  (the first three steps about 25 s, `tb_fir.v` the rest) and ends with `ALL PASS (seed N)`: 38 checks (tb_ext_trigger),
  98 checks (tb_decim), about 1,870 samples in `cic_model: ... -> PASS`, 154 checks (tb_fir) and about 1,540 FIR
  samples in the second `cic_model` line, then 8 checks (tb_long, about 5 minutes more), then `tb_seg.v` (50 checks,
  5 to 7 minutes) and `seg_model: 22 runs, 0 failures -> PASS`. `ONLY=fir` runs only the FIR step, `ONLY=seg` only the
  0x1536 step (`SEG_P1=2` for a quick run, about 2 to 5 minutes). Any change to `zaklad.v` must keep it passing. The equivalence phases prove that CH1/CH2
  match 0x1532 and that K = 0 (including EXT) matches 0x1533.
- Run testbench work from PowerShell (`wsl -d Ubuntu -- bash <script>`). Git Bash rewrites `/mnt/...` arguments.
  Concurrent runs need different `SIM_WORK` directories.

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
`clkc[0]` 8.298 ns / 50 errors. The worst `clk_200MHz` path is `sample_write_clock` (negedge) to
`sampling_triggered`, slack +0.541 ns.

0x1534 (section 3):
- 2304 LUT / 1790 reg / 2488 LE, 32 BRAM9K.
- `clk_200MHz` 4.899 ns, 0 errors. The worst path is the same half-cycle path, at +0.101 ns against the SDC's 40 %
  waveform. It is placement-sensitive: it varied between +0.03 and +0.7 ns across the builds made while developing
  0x1534.
- `sample_write_clock` is constrained now (generated clock plus multicycle into `clk_200MHz`), 0 errors. The decimator
  has +3.36 ns of a 10 ns budget.
- To list more than 3 endpoints, re-run `report_timing` on `~/al3-build/FPGA_pr.db` (`import_device`, `import_db`,
  `read_sdc`, `report_timing -ep_num N`). That does not change the bitstream.

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

`src/zaklad.v` (about 1260 lines), top to bottom:
- Helper modules `clockdivider`, `pwmcounter`, `freq_generator_dds_pwm` (32-bit phase accumulator; output is
  `phase_acc < duty_threshold`), and `decim_chan` (0x1534 filter per channel: capture on either write-clock edge,
  polyphase (1+z^-1)^K prefilter, 3 integrators, decimation strobe, 3 combs, shift and saturation, all 48 bit),
  and `fir_chan` (0x1535 FIR per channel: 15 x 16-bit delay line, symmetric pre-add into an 8-entry shift register,
  one inferred M18x18 with input and product registers, 37-bit accumulator, rounding, clamp; taps per K in `tap()`).
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
  0x1B, 0x32-0x38, 0x3C, 0x50-0x56), read mux (0x05, 0x06, 0x0A, 0x14, 0x18, 0x20-0x29, 0x41).
  Multi-byte values are big-endian and stepped by `mcu_state`.
- External trigger (0x1533), block after the generator pin assign. The pad is read through `ext_sync` (2 flops,
  first in the IOB), then a 3-sample majority filter (`ext_level`), then the edge select per `trigger_edge`. The
  latched `ext_pending` fires into `sampling_triggered` after the next `sample_write_clock` rising edge. Cmd 0x15
  `data[1]` = `trigger_ext` (value 2 = EXT) gates the ADC detect off. Read 0x28 = {ext_activity, gen_out_enable,
  ext_level}. P133 has PULLDOWN in `zaklad.adc`.
- Decimation filter (0x1534), block after the sample-rate divider. `dec_on` (K != 0):
  - forces divider 0 on the static side of the rate compare, so the ADCs run at full rate;
  - drives `dec_pc` (P counter) and `dec_sv` (valid chain), whose `dec_wr` stage gives `swc_adv`, the clock enable of
    every capture register (the reset branches stay ungated);
  - muxes `{dec_out2, dec_out1}` into the memory;
  - turns the 16-bit trigger compare into 0xFF/0x00 in `trigger_data`, compared against level 0x80.
  - `swc_adv_q` gates the EXT apply.
- FIR (0x1535), same block: `fir_ok` (0x56 bit 0, K >= 1, divider >= 14 with bit 1 set) registered as `fir_act`. With
  `fir_act`, `dec_sv[0]` also fires at `dec_pc == fir_hp` (P/2), `dec_t` tags the `dec_pc == 0` strobes, the FIR loads
  on `dec_t[7]`, `dec_wr` = `fir_done1` and `dec_out1/2` = the FIR outputs. With `fir_act` = 0 all of these reduce to
  0x1534.
- Sequencer (0x1536), blocks "Segmented capture and averaging" after the trigger-address capture:
  - write-clock side: the restart pulse `seq_rst_c` (in `cap_rst`, which replaced `sample_system_reset` in every
    capture register), the segment slot remap `sample_write_address_phys`, the 40-bit timestamp `ts_cnt` / `ts_cap`;
  - `clk_200MHz` side: the state machine `q_st` (reads write-clock signals only through the `sn_*` snapshots taken on
    `swc_rise_q`), the post-pass pipeline `pp_*` from the record mirror `seq_mirror_ram` (2 EMB32K) into
    `seq_acc_ram` (16 EMB9K), the table writes `tab_*`, and `cap_rst200` for `sampling_triggered` / `ext_pending`;
  - MCU side: 0x57-0x5D, 0x2A status, 0x2B-0x32 through `seq_lat` (latched on every `i_mcu_clk` edge like the sample
    memory's read register; the reads also advance `sample_read_offset`); 0x0A reports the whole run.
- Command codes still free in the decoders (checked 2026-10-19, after 0x1536): writes 0x02-0x0A, 0x10-0x14, 0x18,
  0x19, 0x1C-0x1E, 0x20-0x31, 0x39-0x3B, 0x3D-0x4F, 0x5E and up; reads 0x01-0x04, 0x07-0x09, 0x0B-0x13, 0x15-0x17,
  0x19-0x1F, 0x33-0x40, 0x42 and up. Stock firmware wrote 0x28 (roll mode) and Donwulff says stock used 0x24/0x26
  differently, so prefer 0x2A and up, or 0x57 and up, for new codes.

## Wanted next (priority order)

Each item changes the bitstream, so it also needs firmware support and a version word or capability bit the
firmware can detect. Rebuild unchanged `src/` first to confirm the toolchain, then work on a copy or a branch.
Do not edit `src/` in place without keeping Atlan4's original reproducible (the hash check above).

1. **External trigger input on P133**: designed, built and simulated (0x1533, `README.md` "Changes"). Open items:
   - firmware support for 0x153x and caps EXT_TRIGGER: done in fw 0.2.9 (bebf901), not benched;
   - flashing and the five bench checks, including the pin input impedance and the EXT-to-CH1 skew;
   - a fixed trigger-point correction, if the bench shows one is needed;
   - the calibration-pin path is DC: P133 to R21 (1 kΩ) to the post, measured with no droop at 0.1 Hz. The probe
     ground (BNC shell) is VRF1, a TL432 virtual ground about +1.24 V above FPGA/USB ground, through R71 (100 Ω).
     An EXT source must be referenced to USB/digital ground, or shifted down about 1.2 V (series red LED) when it
     shares the probe ground. Linking the two grounds sags VRF1 and shifts all traces. `README.md` "Changes"
     section 2 has the trace, limits and bench tests 0 to 4.
2. **Deeper sample memory.** Since 0x1536 every block RAM is in use (accumulator 16 EMB9K, record mirror 2 EMB32K).
   Deeper memory now needs one of: drop the mirror and re-clock the sample memory's read port to `clk_200MHz` with an
   `i_mcu_clk` output register (frees both EMB32K, about 2k more words; the post-pass then reads the sample memory),
   or halve the accumulator (frees 8 EMB9K, 2k words). Before 0x1536: 16 BRAM9K and both BRAM32K were free.
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

Decimation filter (0x1534), open items:
- firmware support (0x53-0x55, the 0x29 check, 16-bit read-back, the EXT latency correction) and flashing;
- bench checks 1 to 5 of `README.md` section 3. Check 2 (capture window) decides the 0x53 bit 3/4 defaults, and check 3
  confirms the B-before-A order. If the bench shows bit errors in both capture settings, try `INDEL` on the ADC pins
  in `zaklad.adc` (TD user guide chapter 9, IOB input delay).
- Possible extensions: a deeper record in 16-bit mode (item 2 above; 16 free BRAM9K hold another 4k words); and CIC3
  for N > 10,334 with Hogenauer pruning or wider registers.
- Mutation runs made for 0x1534 (`DUT=` override): the list is in commit 65417a8. A tenth (K = 2 comb bypass
  broken) was still running at hand-over.
- Next design: 0x1535, a CIC compensation FIR as the intended default mode (owner direction): built, see below and
  `README.md` section 5. 0x1534 is accepted by the coordinator (2026-10-18) and is
  being flashed with fw 0.2.33.

## 0x1539 status (fpga-gen8, 2026-10-19)

Brief (coordinator, priority change): the bench readout fault on 0x1538 (fw 0.2.67 diagnostics): bytes read near code 128,
and across any boundary where several bits flip (0x8F / 0x90, 0x77 / 0x78), came back as mixes of two successive bytes on
CH1 A and CH2 B. Fix it first and ship it alone; the trigger / peak / counter features follow as 0x153A. Everything is in
`README.md` "Changes" section 9.

**Root cause** (TD report of the 0x1538 build with `i_mcu_clk` declared, `report_timing` on `FPGA_pr.db`, scratch scripts
of the `t5.sh` pattern below):
- `o_mcu_data` loaded on the rising edge of `i_mcu_clk`, and `fpga_read_byte` reads the port right after that edge.
- Clock pad to data pads 9.4..10.9 ns at TT 85 C (3.4 ns clock insertion, flop, 2.5 ns routing, 4.7 ns output buffer),
  different per bit: a read sooner than that gets a mix. Placement decides the slow lanes.
- In the FPGA the read path was a same-edge pipeline on an unconstrained clock (memory read port and `o_mcu_data` on one
  edge; the read pointer into the memory's address register, hold +0.38 ns). No violation at TT, nothing constrained.

**Fix**: `o_mcu_data` on the falling edge (strobe start), the memory's read address through `sample_read_addr_n` (falling
edge copy of the pointer), `i_mcu_clk` declared in `zaklad.sdc` as its own asynchronous group. Byte values, pointer
advance, dummy strobe and the 1.00o5 lag are unchanged; asynchronous status reads sample at the strobe start. Two timing
refactors with no functional change: `swc_rise` is the predicted register `swc_rose` (`~swc_sh & sce_q` on the rising
edge), which removes the half-cycle path into `sampling_triggered` (the 0x1534..0x1538 lottery); the sequencer's table
registers load every clock (`tab_z` marks NEXT's zero write) and the hold-off counter is 33-bit signed (`q_hz` = sign).

**Build**: 4575 LUT / 3833 reg. `clk_200MHz` 0 errors (+0.260 ns, a sequencer path), `sample_write_clock`, `clkc[1]`
and `i_mcu_clk` (setup and hold) 0 errors, `clkc[0]` 58 (SDC artefact). `build/FPGA.bin` sha256 `f36c1f7a...ea64`, the
same from three work dirs; pinned `backups/al3_seg_0x1539_f36c1f7a.bin` and `..._padded.bin` (`22db7b91...a6b8`).
**Not flashed.** fw 0.2.62+ runs it unchanged (AL3 0x153x decode, caps as 0x1538).

**Tests**: `tb/run.sh` has a tenth step, `mcuread.sh` / `tb_mcuread.v` (swept MCU strobe timing, per-bit pad delays,
all 256 codes; 0x1539 clean everywhere, the 0x1538 reference mixed whenever read before the slowest pad). The equivalence
steps' references get the falling-edge read block (`mkref`, `REF_READ_EDGE`). Contract `tb/contract_0x1539.txt` = 0x1538's
except the version byte. All ten steps pass on seeds 12345 (2317 s) and 3 (1622 s): 38 + 98 + 154 + 8 + 50 checks, both cic_model checks, seg_model 22 runs, tb_trigpos 882, contract 12 (transcript equal), encphase (plain and 3.5 ns 400 each, the late-A variant caught), mcuread 70 + 26.

**Manifest entry for `fpga/al3/dist`** (the coordinator adds it):

```json
{
 "version": "0x1539",
 "file": "al3-0x1539.bin",
 "size": 282898,
 "sha256": "f36c1f7a0ada876311f5ad5d600f4cb44523165748e96c25bed160a66388ea64",
 "flash_image_sha256": "22db7b9128e2a12149e5c4f44bc3cb5c22338b40b4e888e97fe9c1a3f571a6b8",
 "source_commit": "b4bd8b04a001d9107444344cc45266e9afe95e07",
 "built_with": "Anlogic TD 4.6.8 SP1 (V4.6.116866), fpga/al3/build.sh",
 "min_firmware": "0.2.62",
 "features": ["external trigger", "decimation filter", "CIC compensation FIR", "segmented capture",
              "hardware averaging", "encode self-check", "settled MCU readout"],
 "status": "unbenched",
 "bench": ""
}
```

**Open**: flashing and `README.md` section 9's bench checks 1..5 (the fw 0.2.67 diagnostics run, the calibration wizard,
a slow edge through 128); `docs/hardware-status.md` after flashing; redo calibrations taken on 0x1537 / 0x1538.

**Tool notes**:
- The isolation guard of a worktree agent refuses Bash commands it cannot parse (heredocs, `cd` + git, long pipes, even
  `cat >> file`); write scripts and edit helpers to the scratchpad with the Write tool and run them. PowerShell mangles
  `$`, `|` and quotes passed to `wsl -- bash -c`: put everything in a script file.
- TD 4.6.8: `report_timing` has no `-hold` option (hold checks are in the same report); the PLL-derived clocks
  (`main_clock/pll_inst.clkc[0]`) cannot be named in an SDC (`get_clocks` fails at `read_sdc`), so use
  `set_clock_groups -asynchronous -group [get_clocks i_mcu_clk]`, which covers them.
- Timing of a placed design with another SDC: copy `FPGA_pr.db`, `import_device`, `import_db`, `read_sdc`,
  `report_timing -ep_num 400 -path_num 1`; `set_output_delay -clock i_mcu_clk 0 [get_ports {io_mcu_data[*]}]` gives the
  clock-pad-to-data-pad delays.

## 0x1538 status (fpga-gen7, 2026-10-19)

Brief: the bench fault on 0x1537 (fw 0.2.60/0.2.61): at 200 MSa/s CH2 core A is bad (std 14..35 codes) in about 40 % of
the captures, with chip 2's A-B mean shifted by -3 codes. Everything is in `README.md` "Changes" section 8.

**Root cause** (from TD's report, `report_timing -ep_num 40` on the 0x1537 `FPGA_pr.db`):
- 0x1537's encode flops (falling `clk_ADC`, 1.25 ns after falling `clk_200MHz`) loaded `swc_enc_n` across that 1.25 ns.
- The A input (through an inverter LUT) had slack -0.209 ns at TT 85 C. The B input had +0.45 ns.
- So an A encode was right, one clock late (sampling with B: the mean shift) or metastable (the noise), changing from
  arm to arm. CH2 is the chip that shows it; that is an inference, since the report does not name the chip.
- The B flops met timing, so 0x1537 ran encB = NOT write clock: the opposite order to 0x1535.
- 0x1535's encode CE arrived about 2 ns late (the "structural" clkc[1] error), so it toggled one `clk_ADC` edge after the
  write clock: encB = write clock, core B earlier, as 1.00o5 assumes.

**It was not a write race.** The coordinator's 60 s run without writes (fw 0.2.61 event log) was bad in about 40 % of the
captures throughout. The brief's "0 ms clean / 50 ms bad" was chance at 20 trials.

**Fix in 0x1538**:
- `sce_q` registers the enable one clock ahead.
- `swc_sh` is a write clock copy loaded from the write clock.
- `enc_nxt` / `enc_nxt_n` on the rising `clk_200MHz` edge hold the write clock after the next falling edge.
- The encode flops load them with no enable, on 3.75 ns paths. They keep no state of their own, encB = write clock and
  encA = its complement, at every rate. The capture logic is cycle-identical to 0x1537.
- Read 0x33: sticky self-check bit 0 (cleared by 0x01 = 1), prediction ok bit 1, convention bits 3:2 = 01, last pin
  check bits 7:4. A good capture reads 0xF6.

**Commits**: RTL + tb + docs on the gen7 branch (see `git log`).

**Tests**:
- `tb/run.sh` passes all nine steps on seeds 12345 and 3 (about 26 to 28 minutes each):
  - 38 + 98 + 154 + 8 + 50 checks;
  - both `cic_model` checks and `seg_model` (22 runs);
  - tb_trigpos 882 checks;
  - the contract (12 checks, transcript equal);
  - encphase (400 checks each for the plain and the 3.5 ns variants; the 3.9 ns CH2 A variant fails, as it must, through
    0x33 and the pin checks).
- `tb_trigpos.v` seed 7 also passes. B - A = -5 / -50 / -1000 / -2000 ns at D 0 / 9 / 199 / 399.

**Build**:
- 4634 LUT / 3828 reg.
- 0 errors on `clk_200MHz` (+0.220 ns), `sample_write_clock` and `clkc[1]`. `clkc[0]` has 58 errors (the SDC artefact).
- Deterministic: two work dirs gave the same hash.

**Timing closure is a lottery around the half-cycle path** (`sample_write_clock` negedge to `sampling_triggered`, 2 ns in
the SDC's 40 % waveform). Variants built while closing 0x1538:
- encodes loading straight from the write clock, check on the write clock: -0.26 ns;
- the same with the check through `enc_ref`: 35 errors, mostly sequencer paths, down to -0.67 ns;
- without the prediction check: -0.32 ns;
- without `enc_nxt_n`: -0.28 ns;
- both of those: -0.19 ns;
- `swc_sh` without the prediction check: -0.34 ns;
- `swc_sh` with the prediction on the real write clock: 31 errors;
- the chosen `swc_sh` + prediction: +0.22 ns.

A real fix for the lottery is item 4 of "Wanted next" (SDC cleanup: the PLL clocks are 50 %, so that path really has
2.5 ns) or retiming `sampling_triggered`. Variant scripts: the gen7 scratch `mkvar.py` / `variants.sh` pattern (a copy of
`build.sh`, `build.tcl` and `src/` per variant, built in parallel in separate work dirs, about 3 minutes for three).

**Open**:
- Flash 0x1538 and run section 8's bench checks 1..6 (0x33 = 0xF6, no bad core over 1000+ captures, CH2 A-B mean back
  to 0x1535's values, order B-first).
- Firmware:
  - optionally read 0x33 after each capture and re-arm on bit 0;
  - treat bits 3:2 = 01 as "B-first guaranteed";
  - redo calibration taken on 0x1537.
- `docs/hardware-status.md` after flashing.

**Tool notes**:
- `sim_models.v`'s PLL is right: clk1 lags clk0 by 1.25 ns. The first toggle of the `forever` loop comes 2.5 ns after the
  initial delay. Do not "fix" it.
- iverilog `-g2012` reserves `expect`, like `cross`.
- Python `open(..., 'w')` on Windows writes CRLF. A bash script with CRLF fails with `set: -: invalid option`.
- An isolated worktree agent cannot run `git -C` or `cd` + git in compound commands, and WSL git cannot read a Windows
  worktree. Export reference revisions with Windows git into a directory and pass `REF_SRC_DIR`.
- Full timing for one clock: `report_timing -mode FINAL -net_info -ep_num 40 -path_num 1` after
  `import_device` / `import_db FPGA_pr.db` / `read_sdc zaklad.sdc`.

## 0x1537 status (fpga-gen6, 2026-10-19)

Brief: the bench's trigger position scatter at decimated rates (task queue), a firmware contract testbench for 0x1536
(fw-seg, protocol 1.17), and the +FIR edge-horn / spectrum-wag study. Everything is in `README.md`: section 7 (findings,
fix, build, bench checks), section 6 "Firmware contract", and section 4 after the 6-9 % headroom bullet (edge phase).

**Commits** (branch of 9cc599c): tb_trigpos d3c548e, RTL f114734 + c15981e (timing), contract c5039be, edge_phase.py
0727b8f, docs in the last commit.

**Findings.**
- The FPGA trigger detector, decimator and FIR are deterministic.
- Two 0x1532..0x1536 faults are fixed in 0x1537:
  - **The config race.** 0x0B and the slot mask loaded only on write clock edges inside the reset. Fix: `ssr_d` also
    loads them on the first edge after the release.
  - **The A/B order flip.** The encode and write clock toggles sampled the async-written divider compare at different
    edges. Fix: the encodes follow `swc_enc_n`, a register copy of the write clock.
- Firmware-side findings, sent to fw-seg and recorded in the task queue:
  - legacy EXT is +10..+11 samples late: the AD9288 pipeline is 4 words, and no shift is applied for K = 0;
  - `fpga_swap_trigger_channel` overwrites EXT;
  - the readouts without the dummy strobe are one word late.

**Tests.**
- `tb/run.sh` has eight steps now. 7 = `tb_trigpos.v`, about 3 minutes at 6 runs per case. 8 = `tb_seqcontract.v`,
  under a minute; its transcript had to equal `tb/contract_0x1537.txt` (now `contract_0x1538.txt`, version byte 0x38).
- All pass on seeds 12345 and 3. `tb_trigpos.v` also passes with `+seed=7` and `+pl=0`.
- On 0x1536, tb_trigpos fails 36 of 882 checks (A/B order, legacy spread, R1, R2). The contract transcript of 0x1536 equals 0x1537's except the
  version byte.
- **Timing.**
  - Two things moved the first 0x1537 build out of `clk_200MHz` timing: the encode flops on the write clock's own net,
    and the sequencer's 36-fanout Q_RUN select (-0.48 ns).
  - `/* synthesis keep */` on a plain copy made it worse: +130 LUT, 20 errors.
  - The inverted copy `swc_enc_n` (packed with `sample_write_clock` in one slice), plus `q_seg` and the post-pass
    preload, give 0 errors. The worst path is the old half-cycle path at +0.035 ns.

**Edge study** (`tb/edge_phase.py`, numpy):
- +FIR overshoot is 0.45..6.9 % depending on the edge phase; the CIC alone has none.
- The 1 kHz DDS square (inc 85,899, 999.995973 Hz) sweeps the 1 MSa/s grid at 4.03 Hz: the owner's 3-4 Hz horns.
- It is pure sampled Gibbs ringing of the composite filter.
- The spectrum's bin beat above fo/4 comes from the odd-harmonic images, and it is larger with the CIC alone than with
  +FIR.

**Open.**
- Flashing 0x1537 and the bench checks of section 7: T3/T4/T5, EXT loopback with 0x15 logging, alternating pretriggers at
  100 kSa/s.
- Firmware:
  - legacy EXT shift (4 words in the model; confirm on the bench);
  - interleave order and the 0x53 swap bit from T4 on 0x1537 (core A earlier in the model);
  - keep EXT in `fpga_swap_trigger_channel`;
  - the arm order on 0x1532..0x1535;
  - a dummy strobe in the 16-bit / UI readouts, or a documented one-word shift.
- Filter defaults per mode, a coordinator decision with the edge study as input.

**Tool notes.**
- WSL git cannot read a worktree created by Windows git (`.git` points at `D:/...`). `run.sh` takes `REF_SRC_DIR=<dir>`
  with `<rev>.v` files exported by Windows git (`git show <rev>:fpga/al3/src/zaklad.v`).
- `cross` is a SystemVerilog keyword (iverilog -g2012): do not use it as a variable name.
- Do not edit `tb/run.sh` while a `run.sh` is running: bash reads the script as it goes.
- `tb_trigpos.v` `+order=1` shows the firmware workaround on 0x1536. It removes the 0x0B race, not the A/B flip.

## 0x1536 status (fpga-gen5, 2026-10-19)

Roadmap section 8 item 1, segmented capture and hardware averaging.

**Commits.**
- Design note `docs/plans/segmented-capture.md`: bcccd92.
- RTL: 27c0c75.
- Testbench: e66c884.
- Docs: `README.md` "Changes" section 6 and `src/FPGA commands.txt`.

**Results.**
- `tb/run.sh` passes. `ONLY=seg` passes for seeds 12345 and 3.
- `mutate_seg.py`: 20 of 21 mutants are caught. The 21st (`ts_every_edge`) is equivalent: with K >= 1 the trigger is
  only applied right after an output edge, so the timestamp capture does not need the `adv_d` term.
- Build: 4445 LUT / 3817 reg, every block RAM. `clk_200MHz` +0.282 ns (worst new path +0.38 ns), `sample_write_clock`
  +0.48 ns, the known `clkc[0]` / `clkc[1]` paths unchanged. Deterministic hash `4eaf6811...`.

**What the firmware agent needs** (register map: `README.md` section 6; proposed protocol: design note section 7):
- **Detect.** Version 0x1536..0x153F, proposed caps bit 17 SEQ.
- **Arm.**
  - The usual sequence. Inside the 0x01 = 1 window, after 0x0B/0x0C/0x0E, write 0x57, 0x58, 0x59 (segments), 0x5A
    and 0x5B (averaging), and 0x5C.
  - 0x01 = 0 starts the run. Every record of the run uses the trigger, mode, divider and filter settings as for a
    single capture.
- **Done and stop.**
  - 0x0A = 1 means the whole run is done.
  - STOP / FORCE: write 0x5D = 1. Add 0x1A = 0 if a waiting record must end now. Then wait for 0x0A.
  - The firmware's own auto timeout (v0.2.41) must not write 0x1A = 0 between records of a run.
- **Progress.** 0x2A, 8 bytes: done, overflow, running, count, untriggered, state and next index. Read it twice
  while running.
- **Readout.** Write 0x1F, do one dummy strobe, then read 0x2B + b for byte b of each 64-bit word. Each read advances
  the pointer, exactly like 0x20..0x23. One pass per byte, so a 16-bit lane costs 2 passes.
  - Segment table entry s: [39:0] timestamp (5 ns), [52:40] trigger offset in the slot, [53] triggered, [63] valid.
  - Averaging: legacy lanes L0..L3 = CH1A, CH1B, CH2A, CH2B, each sum >> S. 16-bit: L0 = CH1, L2 = CH2, each
    min(65535, sum >> S). Raw (0x57 bit 3): the sums. S and raw can be changed after the run and the record read
    again.
- **Segment data.** Segment s, record index j is sample memory word `s 2^m + ((offset - pre + j) mod 2^m)`. Read it
  with the existing 0x1F / 0x20..0x23 path. Layouts as for single captures: legacy A/B pairs, or 16-bit.
- **Limits** the firmware must enforce:
  - segments: m = 5..12, count <= 8192 >> m, total <= 2^m - 2, pre < total;
  - averaging: positions x total <= 2048, total <= 2046; N <= 257 for exact legacy sums (above that the lanes clamp
    at 65535).
- **Conversions.**
  - Legacy average code = L x 2^S / N.
  - 16-bit average = L x 2^S / N, then the existing 16-bit rule (gain from GET_DECIM, corr16).
  - Timestamp seconds = ts x 5e-9. Timestamps are taken on the write-clock edge that latched trigger_address: 22 to
    30 ns after an EXT edge at 200 MSa/s, up to one word later at slower rates.
- **EXT latency shift** (`pc_decim_ext_shift`) applies per record as for single captures. It is part of the
  pretrigger value written to 0x0B.

**Open.**
- Firmware support: built by fw-seq (fw 0.2.58 / protocol 1.17, both provisional; firmware/AGENTS.md section 33),
  host-tested, not benched. MCP support: spec handed to the mcp agent (same section).
- Flashing, and the bench checks of `README.md` section 6. Check 5, the N = 1 average against the sample memory, is
  the one hardware risk the testbench cannot cover: the mirror samples the raw ADC buses in other BRAMs.

**Possible next steps.**
- Per-position trigger edge (bipolar TX: alternate rising and falling).
- A stop that also halts the abandoned record.
- Wider legacy lanes: 24 bit would need 96-bit words and does not fit the RAM.
- Ideas for a later generation:
  - with the deeper-memory trade (item 2), records of 4k words in 16-bit mode;
  - the third M18x18 is still free.

**Tool notes from this generation.**
- Git Bash `sed -i` strips CR: `tb/tb_ext_trigger.v` and `README.md` are CRLF. Edit them with Python on bytes and
  check with `file`.
- PowerShell mangles `|` in arguments passed to `wsl`. Long heredocs with quotes fail in Git Bash; write a script
  with the Write tool instead.
- More timing endpoints: re-run `report_timing -ep_num 120` on `~/al3-build/FPGA_pr.db`, as described under "Reading
  the reports". TD names many registers `regNN_bM`; the net names in the path (for example `q_pass[1]`) tell which
  ones they are.

## 0x1535 status (fpga-gen4, 2026-10-18)

- Done as briefed below: RTL (e2aec2e), testbench (74fb8a3), docs in `README.md` section 5 and `src/FPGA commands.txt`.
  The interface delta went to team-lead before the build.
- `tb/run.sh` passes for seeds 12345, 1 and 77. All 13 FIR mutations are caught (list in `README.md` section 5).
- Build: 3082 LUT / 2716 reg, 2 of 3 M18x18 (inferred), `clk_200MHz` +0.324 ns, deterministic hash `5040a2ae...`.
- Open: firmware support (0x56, the shift + 1 rule, (N/2)^K scaling, the EXT lag formula), flashing, and the bench checks
  of section 5. Possible next steps: a T = 31 profile (flat to 0.4 fo, needs N >= 32 or a second multiplier per
  channel, and the third M18x18 is the only one left); a deeper 16-bit record.
- The `tb_fir.v` phase 1 dominates the runtime. Use `+p1=3` (edit `run.sh` or call vvp directly) for quick iterations.
  The mutation driver used for 0x1535 was a sed list over `src/zaklad.v` with `DUT=` and `SIM_WORK=` per variant, run in
  parallel. The half-strobe mutation makes the sim very slow; its model check fails long before the sim ends.

## Handoff to fpga-gen4: 0x1535 (written by fpga-gen3, 2026-10-18)

Brief from team-lead (accepted):
- CIC decimating by N/2, then a 15-tap symmetric FIR decimating by 2, flat to fo/4.
- One M18x18 per channel.
- Engages at N >= 16 with N a multiple of 4; otherwise the 0x1534 path is used.
- A FIR enable/profile bit in a new command (free writes 0x56 and up) plus its read-back (0x29 byte 0 bits 6..5 are
  free, or a new read 0x2A).
- K = 0..3 semantics unchanged when the bit is 0. Version word 0x1535.
- Document the group delay and the shift + 1 rule in the latency section. Testbench against the Python model,
  including mutations.

Design notes:
- **Taps.** `python3 -I tb/fir_design.py coeffs K 15 0.5` prints the 15 quantised taps per K. They are 18-bit signed and
  sum to 2^17, centre tap about 62k to 69k. A symmetric pre-add needs 8 multiply-accumulates per output sample.
- **CIC by N/2.** The pair-stream CIC decimates by P/2, so P must be even; that is where "N multiple of 4" comes from.
  - The simplest change is a second strobe in `decim_chan`: `dec_pc` counts P/2 when the FIR is on.
  - The comb, shift and saturation stages then produce CIC samples at 2 fo, and the FIR consumes pairs of them.
  - `dec_wr` (the memory write and `swc_adv`) must then fire once per FIR output, every P write clocks as before.
    The capture logic needs no change.
- **FIR input.** Use the 16-bit CIC output after the shift. The firmware picks S = rec_shift(K, N/2) + 1 for headroom,
  because the step overshoot is 6 to 9 %.
- **Accumulator.** 18 x 18 products into about 40 bits. Output = acc >> 17, clamped to 0..65535.
- **Multiplier timing.** There are N/2 >= 8 write clocks per output, so one multiplier per channel is enough. Check how
  TD 4.6.8 infers the M18x18 from `a * b` (signed). Register the inputs and outputs (DSP pipeline) to meet 100 MHz.
  The `#dsp` line in `FPGA_phy.area` shows whether it was inferred.
- **Delay line.** 15 x 16 bit per channel in registers (about 240 FF) is fine. A BRAM is not needed at T = 15.
- **Latency.** In write clocks it is no longer constant: it now includes the FIR. Measure it with `cic_model.py` (extend
  it with the FIR stage and the CIC by N/2) and report it as for 0x1534: pin to memory write in write clocks, plus the
  filter group delay. The FIR adds (T - 1)/4 = 3.5 output samples.

Testbench notes (things that cost time in 0x1534):
- `tb/run.sh` compares K = 0 against git `REF1533_REV`. For 0x1535 keep that, and add a K >= 1, FIR-off comparison
  against 0x1534 (rev 6d4c8ad) if it is cheap: the `decim_chan` rename in `mkref` is already there.
- `tb_ext_trigger.v` checks the version word (now 0x1534). `tb_decim.v` checks 0x06 and 0x29 values; update both.
- `cic_model.py` finds the latency by search and asserts 12. A fully saturated segment matches every latency, so keep
  shifts that do not saturate everything.
- After K >= 1, the reference and dut write-clock and encode toggle phases differ. `tb_decim.v` copies them before its
  last equivalence phase; keep that.
- Mutations: `DUT=<file> tb/run.sh 3` with `SIM_WORK` set to a separate directory. Some broken designs hang a capture
  until the 400 ms sim timeout, which takes minutes.

Tool gotchas:
- Git Bash mangles `/mnt/...` arguments and breaks heredocs that contain some quote patterns. Run WSL from PowerShell
  (`wsl -d Ubuntu -- bash /mnt/c/.../script.sh`) and write scripts with the Write tool.
- Line endings: `README.md` and `tb/tb_ext_trigger.v` are CRLF (`README.md` has 22 LF lines from b146da5);
  `zaklad.v`, `AGENTS.md` and the other tb files are LF. Python `open(...).read()` then `write(newline='')` silently
  converts CRLF files to LF.
- `PACKREG = ON` in `zaklad.adc` had no effect on the ADC pins, which feed registers on both clock edges.
- The timing report lists only 3 endpoints. For more, re-run `report_timing -ep_num N` on `~/al3-build/FPGA_pr.db`.

Also open: the structural `clkc[1]` violation (retime `sample_clock_enable`), symmetric IOB packing on the four
encode outputs, and the 32-bit sample-rate counter as critical path (down-counter with registered terminal count).

## 0x153A handoff (fpga-gen8, retired 2026-10-19 at about 810k tokens): ADC capture window, unfinished

Branch `worktree-agent-ac1eb52dff7a62fc7` (WIP commit 19d9a5c and this one). The trigger types / peak detect / counter
work is parked on branch `gen8-features-wip` (built and tested there as "0x153A" before the priority change; it has to
be rebased onto the capture fix and renumbered 0x153B; its commands 0x5E..0x62 and reads 0x34 / 0x35 do not collide
with the capture fix's 0x63, 0x64 and 0x36).

### The fault (bench: 0x1538, 0x1539)
- 0x1539 fixed the MCU readout but the outliers stayed and moved: on 0x1539 the A cores (CH1 A, CH2 A) are garbled
  while their samples lie in codes about 128..145 (0x80..0x91): rails (0 / 255), jumps over 40 codes; B cores clean
  (bench 2026-10-19 20:25 dcoffset sweep, and 20:49 RC low-pass: CH2 A 44 rails / 642 jumps / std 58.9 against 2.2 on
  CH2 B). On 0x1538 it was the B cores of CH2 at 126..133; 0x1535 was clean. `docs/hardware-status.md` has the data.
- Cause (TD report of the 0x1539 build plus the AD9288 datasheet, `IC's/AD9288.pdf` in the pecostm32 repo, table 1:
  tV min 2 ns, tPD max 6 ns, 4.5 typ, at 100 MSa/s per core):
  - 0x1532..0x1539 take all four ADC buses straight from the pins on the write clock edge (memory input, trigger_data,
    decimator), at about 1.2..2 ns before the write clock flop edge (pad + route 3.4..4.2 ns, write clock insertion
    2.49 ns). The B cores are encoded with the rising write clock, the A cores with the falling one; encode pads switch
    6.7..7.5 ns after the edge (fabric flops, 0.8..1.6 ns route + 3.4 ns pad, placement dependent).
  - Each core's data is valid from encode + 6 ns to the next encode + 2 ns: 6 ns of 10 at 200 MSa/s, and the A and B
    windows (5 ns apart) overlap for only about 0.4 ns. A single capture instant for both cores cannot work by
    construction; each build lands it somewhere else (B late on 0x1538, A early on 0x1539).
  - The code dependence (0x80..0x91: bit 7 set, bits 6..5 clear, many bits flip from the 0x7x neighbours) fits a
    capture inside the A window's transition: the bits that change are the ones caught half-way. Inference, not
    measured per bit.

### The design in 19d9a5c (approach (b) with defaults from (a))
- Every ADC pin goes into an IOB DDR input register (`AL_LOGIC_IDDR`, clk_200MHz; TD packs all 32: the area report
  shows IDDR on every `i_adc*` pin, so the pad-to-register delay is fixed: setup/hold aperture edge -0.11 .. +0.12 ns).
  q1 = pin at the rising edge, q2 = at the falling edge, both handed over on the rising edge (TD's sim model, copied
  into `tb/sim_models.v`). Two more rising-edge stages (`adc_d1/d2`, `adc_dd1/dd2`).
- Stage k (k = 0..5) = the pins 5 + 2.5 k ns before a write clock edge R. Per core a 3-bit stage in `cap_sel` (command
  0x63, two bytes high first, { 2B, 2A, 1B, 1A }), comb pick, registered on every write clock edge into `adc*_w`, which
  feeds the memory, trigger_data, the decimator and the mirror. That register costs one write clock of ADC latency (the
  6:1 pick plus the 32K BRAM's 1 ns setup do not fit the 4 ns from a stage change to the next write edge; the
  unregistered version failed `sample_write_clock` by 2.4 ns). EXT is not delayed, so EXT-to-data offsets grow by one
  word (firmware's EXT shift: +1 word legacy, +10 ns with K >= 1).
- The B sample of a word is kept 5 ns (half a word at slower rates) before the A sample: allowed stages A 1..2, B 3..4.
  Power-on (from `tb/capture_margins.py` on the last build): CH1 A 2, CH1 B 4, CH2 A 1, CH2 B 4 (0x0862). CH1 A and CH2 A
  differ because their encode pads differ by 0.67 ns of routing.
- **TD 4.6.8 ignores Verilog initial values**: builds with `cap_sel = 12'h861`, `12'h862`, `12'hFFF` and with
  `gen_out_enable = 0` instead of 1 gave the byte-identical bitstream `24b74262...`. Every flop configures to 0. So
  `cap_sel` / `cap_thr` are stored XOR their power-on value (`cap_sel_x`, `CAP_SEL_PO`) and 0 means the default. This
  also means every other `reg x = value` in zaklad.v powers up 0 on the hardware (`gen_out_enable`, `clk_out_divider`,
  `pretrigger_samples`, `total_samples`, `seq_count`, `seg_m`, `avg_n`, `adc*_encA`, `swc_adv_q`, `enc_ok`, ...): the
  firmware writes them, but the comments that call them power-on values are wrong and the iverilog runs (which honour
  them) differ from the hardware at power-up. Worth a sweep by the next agent.
- Outlier counters (read 0x36, 10 bytes: cap_sel 2 bytes, then 16-bit counts CH1 A, CH1 B, CH2 A, CH2 B): words whose
  sample differs from the core's previous word by more than the threshold (command 0x64, power-on 8), counted while the
  capture writes, cleared by 0x01 = 1. For training (below).
- Live reads 0x24..0x27 now return the IOB register (`adc_q1`); the pads have no other load.
- Ruled out:
  - capturing both cores on one edge with a better phase (0.4 ns common window, see above);
  - clk_200 rising-edge-only chains (5 ns grid; best B margin 1.3 ns);
  - registering the pick on clk_200 (shifts every stage by 5 ns; A loses its centred stage);
  - IOB output registers for the encodes: separate flops with `/* synthesis keep */` and a shadow copy for the 0x33 check
    built, but TD did not pack them (OREG NONE) and `clk_200MHz` failed by 0.2 ns; the encode pad delay stays
    placement dependent (0.8..1.6 ns route). An `AL_LOGIC_ODDR` (IOB) clocked by clk_ADC with d1 = held value,
    d2 = new value would make it fixed; not tried (needs the encode value one clk_ADC rising edge earlier);
  - flipping encA / encB (would keep latency but changes 0x33's convention bits; not needed).

### Build state (TD 4.6.8 SP1, `featbuild.sh` pattern = a copy of build.sh / build.tcl / src per variant)
- Last built: the design of 19d9a5c with plain initial values (`cap_sel = 12'h862`), hash `24b74262...f9ea`: 5011 LUT,
  4079 reg, all block RAM; `clk_200MHz` 0 errors (+0.111 ns), `sample_write_clock` 0 errors (+0.123 ns, IDDR q2 into
  `adc*_w`), `i_mcu_clk` 0 errors, `clkc[1]` 0 errors, `clkc[0]` 58 (SDC artefact). Thin slacks: expect the lottery.
- The XOR-coded registers of 19d9a5c are **not built yet**.
- Margins of that build (`tb/capture_report.sh ~/g8/w/cE`, tV 2 / tPD 6 / board 0.3 ns, worst bit, start/end of the
  valid window, 200 MSa/s):

| Core | encode pad after R | stage 1 | stage 2 | stage 3 | stage 4 |
|---|---|---|---|---|---|
| CH1 A (falling) | 6.79 + 5 | 4.30 / 1.47 | **1.80 / 3.97** | - | - |
| CH2 A (falling) | 7.46 + 5 | **3.63 / 2.14** | 1.13 / 4.64 | - | - |
| CH1 B | 6.74 | - | - | 4.35 / 1.43 | **1.85 / 3.93** |
| CH2 B | 6.75 | - | - | 4.34 / 1.43 | **1.84 / 3.93** |

  So about 1.8 ns worst-case datasheet margin on every core at the defaults (bold), against about 0 on 0x1539. Slower
  rates only widen the windows. TT 85 C numbers; a fast corner moves the encode pads earlier, which eats into the
  first number (the margin to the window start), so the defaults sit nearer the start on purpose.

### Testbench state (quick runs, seed 12345)
- pass: tb_decim (98, cic_model with `adc*_w` logged), tb_mcuread, encphase.
- fail: tb_ext_trigger 1 of 38, "EXT and CH1 level trigger on the same step give the same trigger_address offset":
  expected, the data is one word later against EXT now; change the check to the new offset (CH1 one word more).
- fail: tb_trigpos 42 of 882 ("legacy: every record sample taken before the edge reads LO"): its tag monitor records the
  sampling instant of the value on the pins at the memory write; it must follow the new capture (the value written on
  edge R is the pins at stage k before the previous edge R - T). Fix the monitor, not the design.
- contract: transcript must be regenerated as `contract_0x153A.txt` (version byte, stale 0x05 bits, READ_RAW data bytes
  change with the new sampling); run.sh still points at `contract_0x1539.txt`.
- not run: tb_fir, tb_long, tb_seg (adapted the same way as tb_decim: the reference's ADC pins are `dut.adc*_w` 1 ns
  after each write edge, the live reads are not compared).
- missing: `tb_adccap.v`, the coordinator's item (3): drive the ADC data pins as X during [encode pad + tV_min,
  encode pad + tPD_max] with the encode pad delays from TD (and +-1 ns), sweep the stages, and check that no X reaches
  `adc*_w` / the memory at the defaults at D = 0, 1, 9 (and that 0x1539's raw capture does catch X: the bench
  mechanism). Add it as run.sh step 11.

### Commands
- Build: `wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build.sh` (from the worktree path), then
  `wsl -d Ubuntu -- bash <repo>/fpga/al3/tb/capture_report.sh ~/al3-build` (exit 1 if a power-on stage has < 1 ns).
- Testbench: `wsl -d Ubuntu -- bash <repo>/fpga/al3/tb/run.sh 12345` and `... 3`, with
  `REF_SRC_DIR=<dir with a947e78.v 2839258.v 6d4c8ad.v e2aec2e.v ce943bf.v>` from a Windows worktree (export them with
  `git show <rev>:fpga/al3/src/zaklad.v`).
- One step: `ONLY=ext|decim|fir|long|seg|trigpos|contract|encphase|mcuread`.

### Files changed in 19d9a5c
`src/zaklad.v` (capture block before "Memory part", `adc*_w`, outlier counters after the trigger_data block, 0x63 /
0x64 writes, 0x36 read, version 0x3A), `tb/sim_models.v` (AL_LOGIC_IDDR), `tb/tb_ext_trigger.v`, `tb_decim.v`,
`tb_fir.v`, `tb_seg.v` (reference pins, live reads, logs, version), `tb/tb_mcuread.v` (accepts 0x3A), new
`tb/capture_io.sdc`, `tb/capture_margins.py`, `tb/capture_report.sh`. Docs (README "Changes" section 10, FPGA
commands.txt, protocol.md) are **not written yet** for the capture fix.

### Firmware side to expect
- Detect 0x153A. Nothing has to be written: the power-on stages are the computed ones.
- Self-check / training (recommended in the calibration wizard and at boot): park a quiet input at code 128 (offset
  PWM, probe open or the cal pin off), for each core and each allowed stage (A 1..2, B 3..4) write 0x63, capture 8k
  words at 200 MSa/s, read 0x36; keep the stage with zero outliers in the middle of the clean run (with two allowed
  stages: the one whose neighbour outside the allowed pair is also clean, else the default). Write the result to 0x63
  and store it with the calibration. Read 0x36 after normal captures to flag a bad core (non-zero count on a quiet
  input).
- EXT latency correction: +1 word (legacy) / +10 ns (K >= 1) against 0x1539; tb_trigpos prints the new offsets once
  its monitor is fixed.
- 0x53 bits 3/4 (falling-edge capture in the decimator) have no effect any more (the decimator gets registered data);
  keep them 0.

### Next steps, in order
1. Build the XOR version, check timing (the slacks are thin: rerun variants if a lottery path fails) and run
   `capture_report.sh`; if the per-core best stages differ from 0x0862, set CAP_SEL_PO and rebuild until they agree.
2. Fix tb_trigpos's monitor and tb_ext_trigger's offset check, regenerate the contract, write tb_adccap.v, run the full
   run.sh on seeds 12345 and 3.
3. Docs: README "Changes" section 10 (this analysis, the margins table, the bench checks: the dcoffset sweep and the RC
   test must be clean on every core at every range; 0x36 counts 0 on a quiet input at 128), FPGA commands.txt,
   protocol.md 5.1, the firmware spec above to the coordinator; pin the build in backups/.
4. Consider making the encode pads fixed (ODDR in the IOB) so the margins no longer depend on placement.
5. Rebase `gen8-features-wip` onto it as 0x153B (its tests: tb_trigtypes, tb_peak, tb_counter; its README section 10
   becomes 11).

## 0x153A review, fpga-gen9 (2026-10-19)

Review of the capture path as left in 19d9a5c (line numbers of `src/zaklad.v` at that commit), the clocking, the
constraints, the MCU side and the power-on values, before the design was changed. Sources: the AD9288 datasheet
(`D:\src\FNIRSI-1013D-1014D-Hack\IC's\AD9288.pdf`, table 1 and figures 2-4), pecostm32's 1013D schematic (the
AD9288 note "The data align mode is enabled (S1, S2 inputs high)"), TD re-timings of the placed 0x1538 and 0x1539
builds with zero I/O delays (`~/al3-gen7-1538`, `~/g8/final`; script pattern `tb/capture_report.sh`), gen8's 0x153A
build `~/g8/w/cE`, and the bench entries of 2026-10-19 16:25 to 20:51 in `docs/hardware-status.md`.

### What is right in 19d9a5c
- The diagnosis: 0x1532..0x1539 sample the ADC pins straight into the write-clock domain at an instant set by
  placement (pad + route 3.4..4.7 ns against a 2.49 ns write clock insertion). Re-timed 0x1539 numbers: the sample
  memory needs each pad stable over about [R - 2.7, R - 0.3] ns (CH1 A), [R - 3.1, R - 1.0] (CH1 B), [R - 2.7, R - 0.5]
  (CH2 A), [R - 2.9, R - 1.0] (CH2 B), R = the write clock edge; the encode pads switch 6.73 / 6.98 ns (B) and
  7.35 / 7.85 ns (A, after the falling edge) after R. With the datasheet window [encode + 6, next encode + 2] the A
  cores need stability from R - 1.35 / R - 0.85 on, which they do not get: A bad, B clean, both chips: exactly the bench.
  0x1538 re-timed the same way puts CH2 B 0.8 ns past the end of its window and CH1 A 0.4 ns before the start of its
  window: CH2 A + B bad, CH1 A occasionally, as benched. So the capture-window reading is confirmed by numbers, not only
  by the symptom.
- IOB DDR input registers on `clk_200MHz` (835-868): the pad-to-register path is fixed (1.22 ns, clock insertion
  0.94 ns, aperture about -0.11 .. +0.12 ns around the clock edge for every pin, cE report), so the sampling instants no
  longer move with placement. The 2.5 ns grid (q2, q1, d2, d1, dd2, dd1 = pins 5 .. 17.5 ns before an edge) is the
  right resolution for a 6 ns worst-case window: for any window position one grid point has >= 1.75 ns on both sides.
- A per-core stage register (0x63) with outlier counters (0x36) and threshold (0x64) is the right shape: the firmware
  can find the clean stage on the unit and a wrong default fails loudly at code 128. Storing the registers XOR their
  power-on value is the right answer to TD ignoring initial values.
- The MCU side of 0x1539 (read byte on the falling edge, address copy, `i_mcu_clk` as an asynchronous clock group)
  stands; nothing in the capture change touches it.
- `swc_rose` (748-751), the encode look-ahead (948-965) and `sce_q` (1020-1021) are sound: the encodes are a function
  of the write clock with 3.75 ns paths, B = write clock, A = complement.

### What is wrong or fragile
1. **The A stages are only right at some sample rates.** Stage k samples the pins 5 + 2.5 k ns before a rising write
   clock edge R. Core B is encoded on the rising edge, so its data window relative to R does not depend on the rate
   (the window of the sample encoded at R - T is [R - T + 13.3, R + 9.3] at every T = 10 (D + 1) ns; stages 3 and 4 sit
   inside it with 1.7 .. 1.8 ns at every T). Core A is encoded on the **falling** edge, T/2 before R, so its window
   relative to R moves with T: [R - T/2 + 13.1, R + T/2 + 9.1]. Stages 1 and 2 (R - 7.5, R - 10) are inside the right
   window only for T = 10, 20 and >= 50 ns. At T = 30 (D = 2, about 67 MSa/s, not in the firmware table) they take the
   A sample of the previous period (A then leads B: wrong order, clean data). At **T = 40 (D = 3, 50 MSa/s, in the
   firmware's rate table)** both stages sit in the A data transition [R - 10.9, R - 6.9]: garbled A cores at 50 MSa/s,
   by construction and at every corner. 0x1539 captured at about R - 1.5, which is inside the T = 40 window
   ([R - 6.9, R + 29.1]), so this fault is new in 19d9a5c. The fix is to reference the A capture to the falling write
   clock edge, which the A encode is made from (see the design below).
2. **The pick is a half-cycle path** (890-906): the six stage buses are launched on the rising `clk_200MHz` edge and the
   6:1 mux has to land in `adc*_w` on the write clock edge 2.5 ns later; slack +0.123 ns at TT 85 C in the cE build.
   TD reports one corner. A 2.5 ns path with 0.12 ns of margin is the same class of fault as 0x1537's encode flop
   (-0.21 ns) and the 0x1534..0x1538 "lottery": it may hold on this build and fail on the next, or on a cold unit.
   The pick must sit between two registers of the same clock edge with a whole period.
3. **The B window model is an assumption, and the schematic contradicts it.** 19d9a5c (and this review's numbers
   above) take the B data to follow the B encode (AD9288 figure 3, data align off). pecostm32's schematic says
   S1 = S2 = high, which per table 4 of the datasheet is "Data Align Enabled: Channel B data is delayed a 1/2 clock
   cycle, available on the rising edge of Clock A" (figure 4): then the B data would transition together with the A
   data, 6 .. 12 ns after the **A** encode. The re-timed 0x1538 / 0x1539 lane patterns fit only the align-off model
   (align-on would make CH1 B, whose pad paths are the longest, worse than CH1 A on 0x1539; the bench has it clean),
   so align-off is what this unit does, with tV about 2.5 .. 2.8 ns and tPD about 5.2 ns at the bench's operating
   point (inference from fitting the four lane results; the datasheet limits stay the design basis). The design must
   not bet on it: the B cores get a selectable reference edge (B encode or A encode), training settles it, and a wrong
   default fails loudly rather than shifting data.
4. **Margins are TT numbers.** The encode path (flop 1.07 ns insertion + 0.15 + 0.86 .. 0.90 ns route + 3.42 ns pad,
   6.7 .. 7.9 ns after R) is the only placement- and PVT-dependent term; the IOB sampling instant is tied to the clock
   edge within 0.3 ns. A +-1 ns PVT swing of the encode path (inference: about +-15 %) leaves 0.6 .. 0.9 ns at the
   fixed default stage against the datasheet window, and the best of two stages 2.5 ns apart keeps >= 1.75 ns for a
   window drift of -2.3 .. +2.7 ns. So the honest claim is design goal (b): selectable per core, self-checked, with
   >= 1.6 ns at the defaults at TT against the worst-case datasheet window; not (a), a guaranteed worst-corner margin.
5. **No I/O constraints on the ADC pins in the build SDC** (`zaklad.sdc`), as before; `tb/capture_io.sdc` is only used
   for the margin report. A `set_input_delay` cannot express "one of six stages must be inside a moving window", and
   with the IOB registers there is nothing for the placer to move, so the report (exit code on < 1 ns) stays the gate.
   The encode outputs stay fabric flops (TD does not pack them into IOBs with the 0x33 check on their nets, gen8), so
   the report must be re-run for every build and the power-on stages set from it.
6. **Power-on values.** TD 4.6.8 configures every flop to 0 (gen8's byte-identical builds). Registers whose Verilog
   initial value is not 0 and therefore differs on hardware until written: `gen_out_enable = 1` (565; the firmware
   writes 0x50..0x52 together in `fpga_on_off_generator`, so the generator is off until then, not 1 kHz),
   `clk_out_divider = 85899`, `clk_out_duty` (566-567), `pretrigger_samples = 750`, `total_samples = 1500` (611-612;
   written before every capture), `seq_count = 1`, `seg_m = 7`, `avg_n = 1` (632-634; `avg_n` = 0 means 65536 records,
   only reachable with 0x57 != 0, which the firmware writes together with 0x58..0x5B), `adc*_encA = 1` (662-665;
   reloaded every clock), `swc_adv_q = 1` (660; right after one write clock), `enc_nxt_n = 1`, `enc_ok = F`,
   `enc_pred_ok = 1` (949, 982-983; reloaded every clock), `seg_mask_s = 1FFF` (915; loaded at the first 0x01 = 1;
   before that every write goes to address 0, harmless), `ts_inc = 2` (1322; reloaded every clock), `q_first = 1`
   (1402; set at the first reset). Nothing the firmware relies on at boot depends on an initial value; the comments
   that call these power-on values are wrong and the iverilog runs differ from the hardware until the first writes.
   The 0x53 bits (`dec_*`, 619-622), `trigger_mode` and `cap_sel_x` / `cap_thr_x` are 0 in both worlds.
7. **0x53 bits 3 / 4** (`dec_neg_a`, `dec_neg_b`; `decim_chan` 181-192): with registered data they select the same
   value on either edge: dead logic that still costs 32 flops and reads back in 0x29. Keep the bits readable, remove
   the registers.
8. Smaller: the live reads 0x24..0x27 return `adc_q1` (the pin at the rising `clk_200MHz` edge), which is not a
   stage any core uses; fine for a liveness read, documented as such. The outlier counter's first two words of a
   capture are skipped (`oc_v`), right. The 0x36 read returns `cap_sel` as written, right.

### Verdict
Keep the IOB registers, the 2.5 ns grid, the per-core stage register, the outlier counters and the XOR coding.
Change the structure: capture the A cores against the falling write clock edge and the B cores against the rising
edge (or, selectably, the falling one), stage registers in the write-clock domain loaded by wire-only paths from the
`clk_200MHz` chain, the pick a full write clock period after them, `adc*_w` fed by wire-only paths. The word
association (B first by T/2) then holds at every rate by construction (shown in README section 10), and the only
half-period paths left are register-to-register wires.
