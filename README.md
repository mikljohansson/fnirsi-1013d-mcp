# fnirsi-1013d-mcp

Open firmware, FPGA design and an MCP server for the FNIRSI 1013D tablet oscilloscope, so that AI coding
agents (Claude Code, Codex, anything that speaks MCP) can use the scope as a measurement instrument, side by
side with the person holding it.

Built on the reverse engineering of [pecostm32](https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack), the open
firmware of [pecostm32](https://github.com/pecostm32/FNIRSI_1013D_Firmware) and
[Atlan4](https://github.com/Atlan4/Fnirsi1013D), and the work of the
[EEVblog community](https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/).

## Why

The stock 1013D is a closed box with a 3000-sample memory and no PC interface. Atlan4's firmware opened it up
and, with a custom FPGA bitstream, raised memory to 16k samples per channel and added a signal generator. This
project adds the missing piece: a fast, robust USB protocol and an MCP server, and a mode of operation where
the agent and the human share the instrument.

What you get over stock and over Atlan4's firmware:

- **Agent control over USB.** A binary, CRC-protected protocol over USB CDC (protocol 1.13): 1 ms command
  latency, 2 MB/s readout, a full 16k-sample dual-channel capture every 110 ms. Every acquisition parameter,
  trigger, raw ADC buffers, processed traces, generator, calibration and link diagnostics are reachable.
- **Collaborative mode.** No "remote" lock-out. The agent and you change settings on the same live scope;
  the screen follows the agent, the agent sees your touch changes. A small green MCP chip in the top bar shows
  a host is connected.
- **MCP server with 20+ tools**, safety tiers, a device simulator for development without hardware, and
  derived tools for measurement work: averaged capture with edge alignment, FFT, exponential decay fit
  (pulse-induction metal detector work was the motivating use case).
- **Spectrum analyser view on the scope** (main menu, "Spectrum analyzer"): FFT length, window, dB/div and
  reference level, tap-to-zoom and drag-to-pan, Auto, averaging (Off / 4 / 16 / 64 / Max hold), and waterfall and
  density views. The agent can read it with `scope_spectrum`. Details: `firmware/README.md`, "Spectrum view".
- **Decimation filters** (acquisition menu "Filter", AL3 bitstream 0x1534 or later): the FPGA filters the 200 MS/s
  stream before decimating, with boxcar, CIC2 or CIC3, so slow time bases no longer alias by skipping samples.
  Bitstream 0x1535 adds a compensation FIR after the CIC (boxcar+FIR, CIC2+FIR, CIC3+FIR at 10 MS/s and slower).
  Captures then carry 16-bit samples. CIC2 is the default for new settings. Details: `firmware/README.md`,
  "Decimation filter", and `fpga/al3/README.md`.
- **Digital phosphor view** (main menu, "Phosphor display"): the traces show how often each pixel was hit by
  recent records, with persistence 0.2 s / 1 s / 5 s / infinite and a mono or heat palette. It is a display mode
  of the normal scope view. `firmware/README.md`, "Phosphor view".
- **Calibration v3** (System settings, Base calibration): per-core, per-clock-class zeros and gains, stored on
  the card and applied in the firmware's readout, so 200 MS/s, filtered and legacy captures read the same volts.
  It is a two-step procedure, see "Calibrate" below. The host keeps a per-unit calibration file as well, fitted
  from bench-supply measurements (`scope_calibrate_volts`).
- **MMU and D-cache** (fw 0.2.48): the F1C100s code runs with the MMU and data cache on. The same code builds
  without it (`make gcc10-nommu`, version suffix `-nommu`) for A/B comparison. `docs/plans/mmu-dcache.md`.
- **Hands-free firmware updates.** `fnirsi1013d-flash` reboots the scope into FEL, loads a build into RAM
  in seconds, and can persist it to the SD card over the protocol. No card handling, no buttons.
- **Removed options**: the hardware cannot do them, so the menus no longer carry them. They are the channel menu's
  FFT switch, the generator's Wave / Ampl / Offset / Phase rows (there is no DAC), Long memory and trigger
  hold-off (`firmware/README.md`, "Removed and hidden menu options"). The `PC_PROTOCOL=0` build and the
  byte-identical check against Atlan4's binary are gone as well.
- **Link robustness fixes** found on the bench: USB FIFO layout (the inherited layout aliased the control
  endpoint), receive-ring wrap corruption, host serial driver quirks on Windows.
- **Verified lineage**: the port of Atlan4's v1.00o5 source was proven by rebuilding his released binary byte for
  byte (git history up to 2026-10-18); from there on the firmware is simplified freely, with the credits intact.
- **Reproducible images**: the firmware image depends only on the source, the toolchain and the commit. Two
  clones of the same commit, in any directory, give the same sha256 (`firmware/README.md`, "Reproducible images").
- **FPGA toolchain**: a headless Anlogic TD flow that rebuilds Atlan4's AL3 bitstream byte-identical. It built
  our bitstreams 0x1533 (external trigger on the calibration pin), 0x1534 (boxcar / CIC) and 0x1535 (CIC + FIR).
  The owner's unit runs 0x1535 since 2026-10-19 (`docs/hardware-status.md`); 0x1536 (segmented capture, hardware averaging) is built and awaits firmware support.

## Status

Firmware 0.2.48 with protocol 1.13, development builds only: nothing is tagged yet (`CHANGELOG.md`). The owner's
unit boots 0.2.25 from the SD card; every later build has been loaded into RAM over FEL and benched as recorded in
`docs/hardware-status.md`, and a power cycle returns to the card's 0.2.25. Persisting a newer build to the card
is a deliberate step (see "Firmware updates" below). The 0.2.48 MMU image had not been run on the unit as of the
last log entry.

## Hardware this supports

FNIRSI 1013D with the **Anlogic AL3A10** FPGA variant (unmarked 144-pin FPGA next to a Winbond 25Q80 config
flash with an unpopulated 2x3 header). Atlan4's firmware also supports the EF2L45 and Altera EP4CE6 variants
and this firmware inherits that support, but only the AL3 unit has been tested here. See
`docs/hardware-prep.md` for identifying your board.

Real hardware limits, which no firmware changes: 200 MS/s per channel, 8-bit, about 30 MHz analog
bandwidth, two interleaved ADC cores per channel, no DAC (the generator is a square wave on the calibration
pin).

## Getting started

### 1. Prepare the scope (once)

1. Back up everything and, optionally, flash Atlan4's AL3 bitstream for 16k samples: `docs/hardware-prep.md`.
   The FPGA step needs a 3.3 V SPI programmer on the config-flash header (an FT2232D board works via
   `firmware/tools/spiflash_d2xx.py`).
2. Write the firmware package to a spare micro SD card: `firmware/README.md` section 3
   (`firmware/tools/make_sd_image.py`). The stock firmware in the MCU flash is never touched; remove the card
   and the scope is stock again.
3. Boot. Atlan4's bootloader shows a menu with the scope, the stock firmware and FEL mode.

Releases, once tagged, attach the scope image, the SD tools, the mcp package and `SHA256SUMS`. They do not
attach Atlan4's binary-only bootloader and updater: `tools/make_release_sd_package.py` fetches those on your
machine and checks their hashes. See `docs/releasing.md`.

### 2. Install the MCP server

```
uv tool install <URL of the release's fnirsi1013d-X.Y.Z-py3-none-any.whl>   # or pipx install / pip install
fnirsi1013d-mcp --list-tools                         # check: the tools and where files go (~/.fnirsi1013d)
claude mcp add --scope user fnirsi1013d -- fnirsi1013d-mcp --port auto
```

The scope enumerates as a USB serial device (VID 0x1209, PID 0x0001, "FNIRSI 1013D"), no driver needed on
Windows or Linux (macOS untested). `--port auto` finds it; `fnirsi1013d-mcp --sim "--variant al3"` runs on the
built-in simulator without a scope.

**Claude Code** per project: copy `.mcp.json.example` to `.mcp.json` (servers `fnirsi1013d` and
`fnirsi1013d-sim`) and ask the agent to look at channel 1. **Codex**: `~/.codex/config.toml`:

```toml
[mcp_servers.fnirsi1013d]
command = "fnirsi1013d-mcp"
args = ["--port", "auto"]
```

From a checkout (development) and the details: [`mcp/README.md`](mcp/README.md) "Install" and "Claude Code and
Codex setup".

Both read `AGENTS.md` for project context; `docs/mcp-tools.md` lists every tool with its safety tier.

### 3. Use it

Talk to the scope through your agent: "capture CH1 and measure the frequency", "average 32 captures at
1 MS/s and fit the decay after the trigger", "set the generator to 5 kHz 25 % and show me the duty on CH2".
Meanwhile you keep using the touch screen; settings are shared both ways. Large results are saved as `.npz`
files and the path is returned; hardware-changing tools are logged to an audit file.

Firmware updates: `fnirsi1013d-flash --image <bin> --sha256 <hash> --persist` loads a build into RAM over
FEL and writes it to the card. Without `--persist` only RAM is written and a power cycle returns to the card's
firmware. `fnirsi1013d-bench` checks the link.

### 4. Calibrate (once per unit, after warming up)

The firmware's base calibration (System settings, Base calibration) has two steps. Calibration data v3 is
required: blocks written by 0.2.43 and 0.2.44 (v2) are not read, and until the calibration is run again the scope
uses the original 1.00o5 compensations.

1. Zero step: unplug both probes and USB, then OK. Each channel takes a few seconds.
2. Gain step, after the zero step passed: put both probes (set to x1) on the calibration post and press OK.
   A touch outside the box skips the step and keeps the previous gains.

Details and failure messages: `firmware/README.md` sections 6.2 and 6.3. The host side of the volts model is in
`mcp/AGENTS.md`.

## Development

Start your coding agent in the repo and talk to it; `AGENTS.md` and the per-directory `AGENTS.md` files hold
the context an agent (or a human) needs: layout, build commands, the USB and FPGA facts learned on the bench,
conventions and prioritised TODO lists.

- Firmware: `firmware/` (C, bare-metal F1C100s, GCC in WSL/Linux; `make`, host tests, bench script).
- MCP server and simulator: `mcp/` (Python, FastMCP, pytest).
- FPGA: `fpga/al3/` (Verilog, Anlogic TD headless flow). TD 4.6.8 and a free licence come from anlogic.com after
  registering (Service Support -> Tools & Materials Download -> Software Tools; details in `fpga/al3/README.md`).
- Docs: `docs/protocol.md` (wire protocol), `docs/mcp-tools.md`, `docs/plans/`, `docs/hardware-status.md`
  (bench log), `research/notes/` (the reverse-engineering digests this started from).

The dev loop with hardware: build with `make gcc10` in `firmware/scope` (WSL or Linux), then `fnirsi1013d-flash`
loads `build-gcc10-pc/fnirsi_1013d.bin` into RAM hands-free (the protocol's ENTER_FEL command, `usbipd` and
`sunxi-fel`), test, persist when good. Everything runs on Windows with WSL or on Linux.

## Roadmap and community wishes

Open items in this repo are tracked in the `AGENTS.md` TODO lists. Feature requests seen in the EEVblog
thread and elsewhere that fit this hardware, in rough priority:

- Deeper memory using the AL3's spare block RAM, and trigger on both ADC cores.
- A frequency-response (Bode) analyser: host tool first, then on the scope (`docs/plans/bode-design.md`).
- CSV export, a dot display mode, triggered sweeps slower than 10 ms/div.
- The remaining queue is in `docs/plans/task-queue.md`.

Done since the first version of this list: the external trigger on the calibration pin (bitstream 0x1533),
FFT and averaging on the scope (spectrum view), the decimation filters, calibration v3 storing the per-core
zeros and gains, screenshot over USB (`scope_screenshot`), and removing the options the hardware cannot do.

## Contributing

Issues and pull requests are welcome, from humans and from agents. Add host tests for protocol changes, update the relevant `AGENTS.md`, and never flash a
device without a verified backup. Bench results belong in `docs/hardware-status.md`.

## Credits

- [pecostm32](https://github.com/pecostm32): the reverse engineering (schematics, FPGA bus traces, emulator)
  and the original open firmware. Repos: [FNIRSI-1013D-1014D-Hack](https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack),
  [FNIRSI_1013D_Firmware](https://github.com/pecostm32/FNIRSI_1013D_Firmware),
  [Anlogic_AL3-10_Analyzing](https://github.com/pecostm32/Anlogic_AL3-10_Analyzing).
- [Atlan4](https://github.com/Atlan4/Fnirsi1013D): years of firmware development, calibration, the custom FPGA
  bitstreams for all three variants, the bootloader/updater.
- [Donwulff](https://github.com/Donwulff/FNIRSI_1014D_Firmware): audited fixes, test harness, the headless
  Anlogic TD flow.
- The EEVblog "FNIRSI-1013D 100MHz tablet oscilloscope" thread community.
- Research, firmware, MCP server and bench work in this repo were done by Claude (Fable 5.1, via Claude Code
  agents) coordinated by and with [@mikljohansson](https://github.com/mikljohansson), who owns the scope, the
  soldering iron and the decisions.

## Licence

GPL-3.0, inherited from the pecostm32 and Atlan4 code this derives from. See `LICENSE`.
