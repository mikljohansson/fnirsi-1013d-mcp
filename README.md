# fnirsi-1013d-mcp

[![CI](https://github.com/mikljohansson/fnirsi-1013d-mcp/actions/workflows/ci.yml/badge.svg)](https://github.com/mikljohansson/fnirsi-1013d-mcp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mikljohansson/fnirsi-1013d-mcp?include_prereleases)](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases)
[![Licence: GPL-3.0](https://img.shields.io/badge/licence-GPL--3.0-blue)](LICENSE)

Open firmware, FPGA changes and an MCP server for the FNIRSI 1013D tablet oscilloscope.

Documentation site: <https://mikljohansson.github.io/fnirsi-1013d-mcp/> (user guide, agent guide, engineering; built from `docs/`).

It replaces the scope application of the FNIRSI 1013D (Anlogic AL3A10 variant) with an open firmware that runs from
the SD card, and adds a USB protocol and an MCP server so that an AI agent (Claude Code, Codex, anything that speaks
MCP) can configure, capture and analyse while you keep using the touch screen. The stock firmware in the MCU flash is
never touched; the FPGA features need a new bitstream, which you write with an SPI programmer after backing up.

<!--
Screenshot block. Take these with the `scope_screenshot` MCP tool (PNG of the 800x480 screen), save them under
docs/img/ and replace this comment with the table below. Checklist: docs/plans/release-checklist.md.

1. docs/img/scope.png: scope view, CH1 and CH2 on, two or three measurement cards, the MCP chip in the top bar
   (a host connected), the decimation filter on (CIC2+FIR).
2. docs/img/spectrum.png: Spectrum analyser view of the calibration-pin square wave, the spectrum cards visible.
3. docs/img/diagnostics.png: Menu > Diagnostics, Status page on a calibrated unit (identity line, FPGA line with the
   bitstream's features, the four tiles, calibration OK).
4. docs/img/bode.png: a Bode plot made from `fnirsi1013d-bode` output (magnitude and phase of the bench RC low-pass,
   R 1.6 kOhm / C 10 nF); the host tool writes .csv / .json / .npz and no PNG, so plot the CSV.

| | | | |
|---|---|---|---|
| ![Scope view with measurement cards and the MCP chip](docs/img/scope.png) | ![Spectrum analyser view](docs/img/spectrum.png) | ![Diagnostics, Status page](docs/img/diagnostics.png) | ![Bode plot from fnirsi1013d-bode](docs/img/bode.png) |
| Scope view | Spectrum analyser | Diagnostics | Bode plot (host) |
-->

## Features

**For agents, over USB (MCP first).** Advanced capabilities live in the MCP server: the agent drives them and
presents the data; the scope's screen gets a control only where one row in an existing menu makes a feature usable
at the bench ([`docs/plans/roadmap.md`](docs/plans/roadmap.md) section 6).

- **MCP server** with safety tiers: read-only and acquisition tools run freely, relay changes are rate-limited,
  anything that drives the calibration pin needs a confirmation token, raw FPGA access and FEL are off unless
  enabled. Every hardware-changing call goes to an audit log ([`docs/mcp-tools.md`](docs/mcp-tools.md)).
- **USB protocol**: binary, CRC-checked, over USB CDC, no driver on Windows or Linux
  ([`docs/protocol.md`](docs/protocol.md)). Raw ADC buffers, processed traces, every acquisition parameter, the
  trigger, the generator, calibration and link diagnostics are reachable.
- **Shared control**: no remote lock-out. You and the agent change the same live settings; the screen follows the
  agent and the agent sees your touch changes. A chip in the top bar shows that a host is connected.
- **Measurement tools** for the motivating use case, pulse-induction metal detector work: averaged capture with
  edge alignment, FFT, exponential decay fit, segmented capture and hardware averaging (bitstream 0x1536 and later).
- **Bode plot**: `scope_bode_sweep` and `fnirsi1013d-bode` sweep the generator and measure magnitude and phase
  with both channels ([`mcp/README.md`](mcp/README.md) "Bode sweep").
- **Simulator**: try every tool without a scope (`fnirsi1013d-mcp --sim`).
- **Hands-free firmware loop**: `fnirsi1013d-flash` reboots the scope into FEL, loads a build into RAM and can
  persist it to the card after a backup.

**On the scope.**

- **Spectrum analyser**: FFT up to 4096 points (2.7 ms on the scope with the MMU and data cache on), windows,
  averaging and max hold, waterfall and density views, tap to zoom, drag to pan, Auto.
- **FPGA decimation filters**: boxcar, CIC2 or CIC3 before the decimator, plus a compensation FIR, so slow time
  bases no longer alias by skipping samples (an alias of -17 dBV with a boxcar, -76 dBV with CIC2 + FIR in the
  bench test of 2026-10-19 in [`docs/hardware-status.md`](docs/hardware-status.md)).
- **Per-core calibration with a wizard**: zero and gain per ADC core and clock class, with background tracking of
  the full-rate drift; a first-run screen leads an uncalibrated unit to it.
- **Diagnostics**: identity, FPGA bitstream and features, calibration state, USB link, restarts, battery, an event
  log, and a report saved to the card.
- **Digital phosphor view**, **external trigger** on the calibration pin, the generator (a 32-bit DDS square wave on
  the calibration pin).
- **Reproducible images**: the same commit gives the same sha256 in any checkout and in CI.

### What needs which FPGA bitstream

The firmware runs on every bitstream; a feature appears when the bitstream has it. Details, hashes and how to flash:
[`fpga/al3/dist/BITSTREAMS.md`](fpga/al3/dist/BITSTREAMS.md).

| Feature | Stock 0x1432 | Atlan4 0x1532 | Ours |
|---|---|---|---|
| MCP server, protocol, shared control, spectrum, phosphor, Diagnostics | yes | yes | yes |
| Calibration wizard | without the gain steps | yes | yes |
| 16,384 samples per channel, generator | no | yes | yes |
| External trigger on the calibration pin | no | no | 0x1533 and later |
| Decimation filters (boxcar, CIC2, CIC3), Bode sweep | no | no | 0x1534 and later |
| Compensation FIR (the Bode sweep's best setting) | no | no | 0x1535 and later |
| Segmented capture, hardware averaging (MCP) | no | no | 0x1536 and later |

## Hardware and caveats

- **Supported: the FNIRSI 1013D with the Anlogic AL3A10 FPGA** (unmarked 144-pin LQFP next to a Winbond 25Q80 config
  flash with an unpopulated 2x3 header). That is the variant verified here. The EF2L45 and Altera EP4CE6 variants are
  inherited from Atlan4's firmware and untested; the bitstreams are AL3A10 only.
  [`docs/hardware-prep.md`](docs/hardware-prep.md) helps you identify your board.
- **The FPGA flash needs a programmer.** The config flash is wired only to the FPGA, so the scope cannot write it.
  You solder pins to the 2x3 header, find its pinout by continuity, and use a 3.3 V SPI programmer (an FT2232D board
  with [`firmware/tools/spiflash_d2xx.py`](firmware/tools/README-spiflash.md) is what the owner's unit was flashed
  with). A wrong pin or 5 V logic can damage the board. Back up the flash twice and compare first.
- **The stock firmware stays.** Nothing here writes the MCU flash. The open firmware boots from the SD card through
  Atlan4's bootloader, whose menu also starts the stock firmware and FEL mode. Remove the card and the scope is stock
  again, except that a custom bitstream must be restored to the stock one first.
- **The SD card holds the open firmware**, its calibration, settings and boot-mode byte in raw sectors before the
  FAT32 partition, which must start at sector 16384 (8 MiB) or later on an MBR card (GPT cards are refused).
- **Real limits** that no firmware changes: 200 MS/s per channel, 8 bit, about 30 MHz analog bandwidth, two
  interleaved ADC cores per channel, no DAC (the generator is a logic-level square wave on the calibration pin).
- **Use at your own risk.** Flashing the FPGA flash or the SD sectors can leave the scope unbootable until you restore
  a backup. Tier 3 MCP tools drive the calibration pin, which can switch real current if that pin is wired into your
  circuit.

## Install

From a release (the [releases page](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases); v0 releases are
marked pre-release). Full commands and the reasoning: [`docs/releasing.md`](docs/releasing.md) section 3.

1. **Back up** the SD card you will use, and the FPGA config flash if you will flash a bitstream
   ([`docs/hardware-prep.md`](docs/hardware-prep.md) section 3).
2. **Download** `fnirsi_1013d-fw-X.Y.Z.bin`, `fnirsi_1013d-sdtools-X.Y.Z.zip`, `SHA256SUMS` (and the bitstream and
   wheel you want), then `sha256sum -c SHA256SUMS --ignore-missing`.
3. **Build the SD parts** from the tools zip. This downloads Atlan4's bootloader v0.8 and updater from his
   repository and checks their hashes; they are binary-only, so this project does not redistribute them:

   ```
   python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin --parts parts
   ```

4. **Write them to a spare micro SD card** (Linux, or WSL with the card reader attached through `usbipd`):

   ```
   python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin \
           --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
   ```

   The tool backs up the card, writes only these parts and reads them back. **Do not `dd` the whole package**: that
   zeroes sectors 708-710 (calibration, settings, display and boot mode).
5. **Optional, the FPGA bitstream**: [`fpga/al3/dist/BITSTREAMS.md`](fpga/al3/dist/BITSTREAMS.md) (attached to each
   release): which image, the programmer wiring, backup, write, verify, rollback.
6. **First boot.** The boot menu shows PECO (the open firmware, preselected with boot mode 4), FEL, FNIRSI (stock)
   and Update. An uncalibrated unit starts with a first-run screen: choose Calibrate now and follow the wizard
   (or later from Menu > Diagnostics > Calibrate): nothing connected and USB unplugged for the zero level, then each probe
   at x1 on the calibration post ([`firmware/README.md`](firmware/README.md) section 6.4). Calibrate again after changing the
   bitstream.
7. **Back to stock**: pick FNIRSI in the boot menu or remove the card. With a custom bitstream, write your FPGA
   backup first ([`BITSTREAMS.md`](fpga/al3/dist/BITSTREAMS.md) "Rollback").

## MCP quick start

```
uv tool install <URL of the release's fnirsi1013d-X.Y.Z-py3-none-any.whl>   # or pipx install / pip install
fnirsi1013d-mcp --list-tools                         # the tools and where files go (~/.fnirsi1013d)
claude mcp add --scope user fnirsi1013d -- fnirsi1013d-mcp --port auto
```

The scope enumerates as a USB serial device ("FNIRSI 1013D"); `--port auto` finds it. Without a scope:
`fnirsi1013d-mcp --sim "--variant al3"`. **Codex**, in `~/.codex/config.toml`:

```toml
[mcp_servers.fnirsi1013d]
command = "fnirsi1013d-mcp"
args = ["--port", "auto"]
```

Per project in Claude Code: copy [`.mcp.json.example`](.mcp.json.example) to `.mcp.json`. Install from a checkout,
environment variables and the full setup: [`mcp/README.md`](mcp/README.md). Only one client may hold the scope's
serial port at a time.

Then ask your agent, for example: "capture CH1 and measure the frequency", "average 32 captures at 1 MS/s and fit
the decay after the trigger", "set the generator to 5 kHz 25 % and show me the duty on CH2". Large results are saved
as `.npz` files and the path is returned.

## Agents and the measurement guide

[`docs/agent-measurement-guide.md`](docs/agent-measurement-guide.md) is what an agent should know before it measures:
session start, ranges and time bases, averaging, the shared screen, generator safety, reading results, caveats.
Claude Code loads it as the skill [`.claude/skills/fnirsi-scope`](.claude/skills/fnirsi-scope) when the repository is
open; with a wheel install copy that directory into `~/.claude/skills/`, or point Codex at the guide.

## Documentation

| Topic | Where |
|---|---|
| Wire protocol | [`docs/protocol.md`](docs/protocol.md) |
| MCP tools and safety tiers | [`docs/mcp-tools.md`](docs/mcp-tools.md) |
| Measurement guide for agents | [`docs/agent-measurement-guide.md`](docs/agent-measurement-guide.md) |
| Board identification, backups, programmers, FEL | [`docs/hardware-prep.md`](docs/hardware-prep.md) |
| FPGA bitstreams: which, how to flash, rollback | [`fpga/al3/dist/BITSTREAMS.md`](fpga/al3/dist/BITSTREAMS.md) |
| Firmware: build, SD layout, FEL, screens, calibration | [`firmware/README.md`](firmware/README.md) |
| FPGA design changes and the headless TD flow | [`fpga/al3/README.md`](fpga/al3/README.md) |
| Terms of the signal chain | [`docs/glossary.md`](docs/glossary.md) |
| Bench log (what was measured on the hardware) | [`docs/hardware-status.md`](docs/hardware-status.md) |
| Releases and their checks | [`docs/releasing.md`](docs/releasing.md) |
| Roadmap and developer history | [`docs/plans/roadmap.md`](docs/plans/roadmap.md), [`CHANGELOG.md`](CHANGELOG.md) |

## Development

Start your coding agent in the repository; [`AGENTS.md`](AGENTS.md) and the per-directory `AGENTS.md` files hold the
context an agent (or a human) needs: layout, build commands, the USB and FPGA facts learned on the bench, conventions.

- Firmware: `firmware/` (C, bare-metal F1C100s, GCC in WSL or Linux; `make gcc10`, host tests).
- MCP server and simulator: `mcp/` (Python, FastMCP, pytest).
- FPGA: `fpga/al3/` (Verilog, Anlogic TD headless flow). TD 4.6.8 and a free licence come from anlogic.com after
  registering (details in [`fpga/al3/TD_SETUP.md`](fpga/al3/TD_SETUP.md)).

The dev loop with hardware: build with `make gcc10` in `firmware/scope`, then `fnirsi1013d-flash` loads
`build-gcc10-pc/fnirsi_1013d.bin` into RAM hands-free (the protocol's ENTER_FEL command, `usbipd` on Windows and
`sunxi-fel`), test, persist when good ([`firmware/README.md`](firmware/README.md) section 4).

## Contributing and bench reports

Issues and pull requests are welcome, from humans and from agents. Add host tests for protocol changes, update the
relevant `AGENTS.md`, and never flash a device without a verified backup.

Reports from other units are the most useful contribution: the FPGA variant and the version word (Diagnostics, Status),
the firmware version (`#VER` or Diagnostics), what you did and what you saw, whether the calibration wizard passes
(a GET_CAL dump or the Diagnostics report saved to the card). The entries in
[`docs/hardware-status.md`](docs/hardware-status.md) are a good model.

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

GPL-3.0, inherited from the pecostm32 and Atlan4 code this derives from. See [`LICENSE`](LICENSE).
