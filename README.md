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

- **Agent control over USB.** A binary, CRC-protected protocol over USB CDC: 1 ms command latency, 2 MB/s
  readout, a full 16k-sample dual-channel capture every 110 ms. Every acquisition parameter, trigger, raw ADC
  buffers, processed traces, generator, calibration and link diagnostics are reachable.
- **Collaborative mode.** No "remote" lock-out. The agent and you change settings on the same live scope;
  the screen follows the agent, the agent sees your touch changes. A small green MCP chip in the top bar shows
  a host is connected.
- **MCP server with 20+ tools**, safety tiers, a device simulator for development without hardware, and
  derived tools for measurement work: averaged capture with edge alignment, FFT, exponential decay fit
  (pulse-induction metal detector work was the motivating use case).
- **Per-unit voltage calibration** on the host, fitted from bench-supply measurements, correcting the ADC
  core zero errors the firmware's model misses.
- **Hands-free firmware updates.** `fnirsi1013d-flash` reboots the scope into FEL, loads a build into RAM
  in seconds, and can persist it to the SD card over the protocol. No card handling, no buttons.
- **Link robustness fixes** found on the bench: USB FIFO layout (the inherited layout aliased the control
  endpoint), receive-ring wrap corruption, host serial driver quirks on Windows.
- **Reproducible builds**: with Atlan4's exact compiler the firmware builds byte-identical to his release
  when the protocol is compiled out, so every change is attributable.
- **FPGA toolchain**: a headless Anlogic TD flow that rebuilds the AL3 bitstream byte-identical, ready for
  the next FPGA work (external trigger, deeper memory).

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

### 2. Install the MCP server

```
cd mcp
python -m venv .venv && .venv/Scripts/activate      # or source .venv/bin/activate
pip install -e .
fnirsi1013d-mcp --port COM10                         # or /dev/ttyACM0; "auto" finds the scope
```

The scope enumerates as a USB serial device (VID 0x1209, PID 0x0001, "FNIRSI 1013D"), no driver needed on
Windows, Linux or macOS.

**Claude Code**: the repo's `.mcp.json` registers two servers, `fnirsi1013d` (the real scope) and
`fnirsi1013d-sim` (the simulator). Start Claude Code in the repo and ask it to look at channel 1. Outside the
repo: `claude mcp add fnirsi1013d -- <repo>/mcp/.venv/Scripts/fnirsi1013d-mcp --port COM10`.

**Codex**: add to `~/.codex/config.toml`:

```toml
[mcp_servers.fnirsi1013d]
command = "<repo>/mcp/.venv/Scripts/fnirsi1013d-mcp"
args = ["--port", "COM10"]
```

Both read `AGENTS.md` for project context; `docs/mcp-tools.md` lists every tool with its safety tier.

### 3. Use it

Talk to the scope through your agent: "capture CH1 and measure the frequency", "average 32 captures at
1 MS/s and fit the decay after the trigger", "set the generator to 5 kHz 25 % and show me the duty on CH2".
Meanwhile you keep using the touch screen; settings are shared both ways. Large results are saved as `.npz`
files and the path is returned; hardware-changing tools are logged to an audit file.

Firmware updates: `fnirsi1013d-flash --image <bin> --sha256 <hash> --persist` loads a build into RAM over
FEL and writes it to the card. `fnirsi1013d-bench` checks the link.

## Development

Start your coding agent in the repo and talk to it; `AGENTS.md` and the per-directory `AGENTS.md` files hold
the context an agent (or a human) needs: layout, build commands, the USB and FPGA facts learned on the bench,
conventions and prioritised TODO lists.

- Firmware: `firmware/` (C, bare-metal F1C100s, GCC in WSL/Linux; `make`, host tests, bench script).
- MCP server and simulator: `mcp/` (Python, FastMCP, pytest).
- FPGA: `fpga/al3/` (Verilog, Anlogic TD headless flow).
- Docs: `docs/protocol.md` (wire protocol), `docs/mcp-tools.md`, `docs/plans/`, `docs/hardware-status.md`
  (bench log), `research/notes/` (the reverse-engineering digests this started from).

The dev loop with hardware: build, `fnirsi1013d-flash` into RAM (hands-free via the protocol's FEL command),
test, persist when good. Everything runs on Windows with WSL or on Linux.

## Roadmap and community wishes

Open items in this repo are tracked in the `AGENTS.md` TODO lists. Feature requests seen in the EEVblog
thread and elsewhere that fit this hardware, in rough priority:

- External trigger input on the calibration pin (FPGA), to sync captures to an external TX pulse.
- Deeper memory using the AL3's spare block RAM, and trigger on both ADC cores.
- FFT and averaging / high-resolution modes on the scope itself (the 1.00 line lacks FFT, roll mode and
  hold-off; the DPOS350P's FFT and frequency-response analyser are the reference ideas).
- Screenshot over USB, CSV export, a dot display mode, triggered sweeps slower than 10 ms/div.
- Base calibration that stores the measured per-core zero, so the on-screen volts match the host's.
- Hiding every UI option the running hardware does not support (in progress).

## Contributing

Issues and pull requests are welcome, from humans and from agents. Keep `PC_PROTOCOL=0` byte-identical to
Atlan4's image, add host tests for protocol changes, update the relevant `AGENTS.md`, and never flash a
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
