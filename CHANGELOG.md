# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The firmware (`PC_FW_VERSION` in `firmware/scope/pc_cmds.h`) and the mcp package (`mcp/pyproject.toml`)
share one version from the first tag on. A release tag `vX.Y.Z` requires both to read `X.Y.Z` and a
`## [X.Y.Z] - YYYY-MM-DD` section in this file, whose text becomes the release notes. See
`docs/releasing.md`. Tags below 1.0.0 are published as pre-releases.

## [Unreleased]

Nothing has been tagged yet. Every firmware version below is a development build. It was FEL-loaded
on the owner's unit (AL3A10 FPGA, bitstream 0x1532) and benched as recorded in
`docs/hardware-status.md`. Image hashes in that log come from local builds. A release image is built
by CI from the tag commit and has its own hash.

### Plan for the first tag (v0.3.0)

- Land the rest of the current firmware queue (`docs/plans/roadmap.md` item 0). As of 0.2.7, the fix for
  request corruption under RX back-pressure is still open. That version added counters for it.
- Bench every item, persist the firmware to the card, and run a 30-minute soak with the MCP server polling.
- Set `PC_FW_VERSION` to `0.3.0`, set the mcp package version from `0.1.0` to `0.3.0`, and move the
  entries below into a `## [0.3.0]` section.
- Tag `v0.3.0`. Load the attached scope image over FEL and check its sha256 against `SHA256SUMS`
  (`docs/releasing.md`, section 4).

### Firmware (scope application, `firmware/scope`, base Atlan4 v1.00o5)

With `PC_PROTOCOL=0` the tree builds byte-identical to Atlan4's v1.00o5 image (`make check-reference`).
Every protocol change sits behind `#if PC_PROTOCOL`.

#### 0.2.45 - 2026-10-19

- Added: calibration data v3 with a gain per ADC core and clock class (c8f17bf). The base calibration
  now has a second step: with both probes on the calibration post, the generator holds the post low
  and then high, and the spans give each core's gain at full rate and gated. The gated mean stays the
  reference of the voltage calibration. 200 MSa/s, filtered (CIC / FIR) and legacy captures then read
  the same volts, and the A/B gain difference behind the full-rate fs/2 spur is corrected. The zero
  step also measures the offset slopes at full rate (c8f17bf).
- Changed: the readout multiplies each core by its gain (code = (raw x gain + offset + dither) >> 16),
  and the 16-bit path takes the full-class gain into its scale (c8f17bf). Same memory accesses per
  sample as 0.2.43.
- Changed: sector 708 block v3 (304 bytes), protocol 1.12, GET_CAL 256 bytes (docs/protocol.md
  section 14 row 51) (3aa3431, c8f17bf). v2 blocks of 0.2.43-0.2.44 are not read: run the base
  calibration again.
- Added: the gain step prompt after a passed zero step, gain failure reasons in the box, and gains,
  their range and lever in the diagnostics view (f5fdf72, c8f17bf).

#### 0.2.7 - 2026-10-18

- Changed: SD_STAGE is served from the 1 ms timer poll instead of one request per UI pass (d88e9ea).
  On 0.2.3 a 64 KiB stage took about 5.8 s.
- Added: six RX back-pressure counters in GET_LINK_STATS, 47 counters in all (d88e9ea).
- Added: our version is shown on the main screen, the restore screen and the diagnostics screen
  (d88e9ea). The diagnostics screen also shows an identity line with fw, protocol, base, git hash and
  build time.
- Fixed: the MCP chip is part of the top-bar redraw, so it no longer blinks off (d88e9ea).
- Changed: the diagnostics temperature is shown only when a DS3231 answered at boot (d88e9ea).
- Docs: the 1.5 s startup gap is the boot splash (0aea00f).

#### 0.2.6 - 2026-10-18

- Changed: the UI hides what the hardware cannot do (68f7868). This covers the generator Wave, Ampl,
  Offset and Phase rows (there is no DAC), and memory depths above the bitstream's maximum. Long memory,
  the EXT trigger and hold-off are hidden too. Time & Date is hidden when no DS3231 answers.
- Changed: saved settings are fixed up at boot to match (68f7868).
- Bench: census clean on 0.2.6. Visual check of the hidden items is pending (4db7576).

#### 0.2.5 - 2026-10-18

- Fixed: EP2 OUT is unmasked again after a USB suspend, reset or disconnect (5086f7d). RX used to fall
  back to the 2 ms poll sweep after a resume.
- Added: six USB device event counters in GET_LINK_STATS (5086f7d). They count bus resets, suspends,
  resumes, disconnects, EP0 setups and malformed EP0 setups.
- Bench: the idle USB fault seen on 0.2.2 was not reproduced (0ddd08c).

#### 0.2.4 - 2026-10-18

- Added: first-pass stage marks in GET_LINK_STATS, counters 31-34 (b05cdbd). They showed that the 1.5 s
  startup delay is in the init path, not in the first UI pass (5ee4533).

#### 0.2.3 - 2026-10-18

- Changed: the MCP chip sits above RUN/STOP and stays steady (baee5b5).
- Fixed: SET_TIMEBASE applies in one step, without flicker through intermediate values (baee5b5).

#### 0.2.2 - 2026-10-18

- Added: SD sector commands SD_INFO, SD_READ_SECTORS, SD_STAGE and SD_WRITE_SECTORS (0d4d9bb). Writes
  are limited to a window below the first partition. Sectors 16-79 and 708-710 need explicit flags.
- Added: first-pass startup counters (0d4d9bb).
- Bench: the first persist of 0.2.3 to sector 750 failed mid-write and the second succeeded (a7fcf11).

#### 0.2.1 - 2026-10-18

- Fixed: READ_TRACE clips a count past the end of the trace (d9e1ac6).

#### 0.2.0 - 2026-10-18

- Added: protocol 1.3, shared control with the UI (d77298f). There is no LOCAL/REMOTE and no lock.
  It brings settings_seq, RUN/STOP/SINGLE, host single captures shown on screen and the MCP chip.
- Bench: collaborative mode was accepted interactively on 0.2.1 (ecfd226).

#### 0.1.7 - 2026-10-18

- Changed: wrap-safe RX ring helpers, with an all-phase ring test (5de512e).
- Bench: first image persisted to the SD card and booted after a power cycle (9164b18, 30e8958).

#### 0.1.6 - 2026-10-18

- Fixed: each OUT packet is read in one FIFO access run (697e1f6). This fixed request corruption at the
  2 KiB RX ring wrap. A census of 2000 PINGs and 100 x 64 KiB had no errors (3c7b623).

#### 0.1.5 - 2026-10-18

- Added: request-corruption diagnostics GET_BAD_FRAME, the `PC_FIFO_MAP` alternative layout (`-m1`
  builds) and a census mode in `bench_link.py` (c31bd01).

#### 0.1.4 - 2026-10-18

- Added: protocol 1.2 GET_LINK_STATS counters and an RX sweep for lost USB interrupts (aabe64a).

#### 0.1.3 - 2026-10-18

- Fixed: the CDC FIFOs no longer overlap the fixed EP0 FIFO at bytes 0-63 (d0ae338).

#### 0.1.2 - 2026-10-18

- Changed: safe requests are answered from the 1 ms timer IRQ, so PING takes about 1 ms instead of
  about 45 ms (afaee38, e3c5bf8).
- Fixed: EP2 is flushed on a bus reset (afaee38).

#### 0.1.1 - 2026-10-18

- Fixed: CDC FIFO layout inside the 2 KiB FIFO RAM (5a8432b). Large responses had stalled and 64 KiB
  failed on Windows.
- Added: TX recovery, ENTER_FEL and protocol 1.1 main-loop pass times in GET_STATUS (5a8432b).

#### 0.1.0 - 2026-10-18

- Added: the USB control protocol, with frame layer, command layer behind a hardware abstraction layer,
  CDC transport and shared test vectors (9ffd7ba, c5f41c8, 4aa239c). It is enabled by default (193e447).
- Added: Atlan4 v1.00o5 scope source with a plain Makefile and a byte-identical reference build
  (2633b3f).
- Bench: enumeration, identify, REMOTE, readout, front end, timebase, trigger and generator checks
  passed on hardware (0b9c783, a7579d5, a4577f3, f7bd2bf, f9f9349).

### Host library and MCP server (`mcp/`, package version 0.1.0)

- Added: protocol v1 frame codec, serial/TCP transport and device simulator (0d8b40f, cc1c908, d888fb3).
- Added: high-level client, analysis, FastMCP server and tests (8c80c1e). Also an in-process
  simulator (`--sim`) and the README (a60d35a).
- Fixed: Windows stdio deadlock on the first scipy import (6f43acd).
- Fixed: Windows serial stalls and corruption on large responses (c265992). The port is now never
  reconfigured after open, and reads are driven by `in_waiting`.
- Added: protocol 1.1 support and link robustness work (55f373e).
- Added: protocol 1.2 support (20b4d8b).
- Added: `fnirsi1013d-flash` FEL dev loop with per-command timeout classes (136eb1b), and `--sha256`
  (20b4d8b). ENTER_FEL is retried (8c28543, 0ade410).
- Added: `fnirsi1013d-bench` (8c28543).
- Added: collaborative mode, protocol 1.3, on the host side, in the simulator and in the vectors (f88c9e3).
  Follow-ups: 319904a, 4568913, 51ddb13.
- Removed: the provisional 8 KiB Windows response cap (19ac919).
- Added: per-unit, per-core voltage calibration with `scope_calibrate_volts` and `scope_calibration_info`
  (bf4a4bb, a66353e, a9232a5).
- Added: SD sector commands with `fnirsi1013d-flash --persist` and `--restore` (9e79583). The SD API in
  `Scope` came next, with the `fnirsi1013d-sd` CLI, `scope_sd_info` and a version check before a persist
  (c4b9735). A persist resumes after a USB drop (83b20c9).
- Changed: GET_LINK_STATS names for the fw 0.2.4 and 0.2.5 counters (83b20c9, cf77969).
- Added: agent measurement guide, the fnirsi-scope skill, and Codex and user-scope MCP setup (dc7f4b1).

### Tooling, FPGA and documentation

- Added: `firmware/tools/make_sd_image.py` and `mksunxi.py` (2633b3f). They write only the parts you pass
  and never zero sectors 708-710.
- Added: SPI flash tools for the FT2232D, `spiflash_d2xx.py` and `spiflash_ftdi.py` (71774ff, 108df11, fee75d9).
- Added: T8 calibration bench helpers (b712f6c, 133767f, 27e368e).
- Added: AL3 FPGA project sources and a headless Anlogic TD flow. The flow rebuilds Atlan4's 0x1532
  bitstream byte-identical (e790a7d, 42c79dc).
- Added: protocol specification, MCP tool surface, hardware preparation guide and roadmap (9d32933,
  1bd4179, a947e78).
- Added: CI and release workflows, `tools/make_release_sd_package.py` and `docs/releasing.md`.
