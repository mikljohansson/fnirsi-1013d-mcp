# Announcement draft (release 0.3.0)

Final draft for the maintainer, rewritten 2026-10-20 (rel-gen2) from the README, `docs/release-notes/0.3.0.md`,
`CHANGELOG.md`, `docs/hardware-status.md` and `fpga/al3/dist/BITSTREAMS.md`. Every number names its bench source. Two
placeholders stay until the tag exists: `{{VERSION}}` (0.3.0; firmware and MCP package share it) and `{{RELEASE_URL}}`
(https://github.com/mikljohansson/fnirsi-1013d-mcp/releases/tag/v{{VERSION}}, the tag URL, not `/latest`: v0 releases are
pre-releases).

## Maintainer decisions before posting

- [ ] Credits and AI attribution wording: the credits below and README "Credits" (F12 in
      `docs/plans/release-readiness.md`). The AI sentence is README's, word for word, including the model name
      "Fable 5.1". Confirm or change both places together.
- [ ] Repository description and topics on GitHub (F11). Suggested description: "Open firmware, FPGA bitstreams and an
      MCP server for the FNIRSI 1013D oscilloscope (AL3A10)". Suggested topics: `oscilloscope`, `fnirsi`, `fnirsi-1013d`,
      `firmware`, `fpga`, `anlogic`, `mcp`, `model-context-protocol`, `claude-code`, `metal-detector`.
- [ ] GitHub Discussions on or off. The plan below pins the long version as a Discussion; with Discussions off, drop that
      step and point readers to issues.
- [ ] Which forums: the EEVblog FNIRSI 1013D thread; Reddit r/oscilloscopes, r/electronics, r/ClaudeAI. Add or remove.
- [ ] The clip: none is recorded. Post without one, or record a short one (the agent changing a setting while the screen
      follows, the MCP chip visible) and add its link under "What an agent can do with it".
- [ ] The README banner (the three sentences at the end of this file): add it at release time or leave the README as it is.

## Where it goes

All on the same day (maintainer decision 2026-10-19: one announcement, no follow-up posts):

1. EEVblog forum, the FNIRSI 1013D thread (testgear subforum): the long version below.
2. The repository: the same text as a pinned GitHub Discussion (if Discussions are on).
3. Reddit: r/oscilloscopes and r/electronics with the short version, r/ClaudeAI with its variant; one link each.

---

## Long version (EEVblog thread, GitHub Discussion)

**Title:** Open firmware, FPGA bitstreams and an MCP server for the FNIRSI 1013D (AL3A10 variant), release {{VERSION}}

### What this is

I have released {{VERSION}} of a set of changes to the FNIRSI 1013D tablet oscilloscope:

- a scope firmware that runs from the SD card. The stock firmware in the MCU flash stays untouched. Remove the card and
  the scope is stock again.
- FPGA bitstreams for the Anlogic AL3A10 variant.
- a USB protocol and a Python MCP server. An AI agent (Claude Code, Codex, any MCP client) can configure the scope,
  capture and analyse while you keep using the touch screen.

It builds on Atlan4's firmware and FPGA design and on pecostm32's reverse engineering (credits at the end). Repository:
https://github.com/mikljohansson/fnirsi-1013d-mcp, GPL-3.0. Documentation: https://mikljohansson.github.io/fnirsi-1013d-mcp/.

I wrote it for one job: transmit and receive waveforms of a VLF / pulse-induction metal detector. Those signals are low
frequency, so the scope's real limits (200 MS/s, 8 bit, about 30 MHz) were never the problem. The firmware additions
work without any agent.

![Scope view with the MCP chip](https://raw.githubusercontent.com/mikljohansson/fnirsi-1013d-mcp/876700a97e5f211a61281d65d77d551f90d7d604/docs/screenshots/scope.png)
![Frequency response on the scope](https://raw.githubusercontent.com/mikljohansson/fnirsi-1013d-mcp/876700a97e5f211a61281d65d77d551f90d7d604/docs/screenshots/frequency-response-sweep.png)

### What it adds over Atlan4's firmware

The numbers are from my unit (AL3A10), not claims about every unit. Sources are in `docs/hardware-status.md`.

- **Frequency response on the scope.** The calibration post drives a passive filter or tank. CH1 reads its input, CH2
  its output. The screen plots magnitude and phase, with -3 dB, peak, Q and phase markers, cursors and cards. AUTO SET
  finds the span and stops. RUN repeats sweeps for tuning and reuses the previous ranges, so the relays stay quiet. On
  bitstream 0x153B, ten sweeps of an RC network (100 Hz to 1 MHz, 25 points) needed 25 captures each, no retakes, and
  flagged no point (bench 2026-10-20 20:40).
- **Calibration per ADC core, with a wizard.** Each channel is two interleaved ADC cores with their own zero and gain,
  and the difference changes with the clock mode. The wizard measures both per core and per clock class. On bitstream
  0x153A and later it also trains the instant at which the FPGA captures each core's data. A self-check after every
  200 MSa/s capture counts outliers. On my unit it flagged 0 of 18,865 captures on 0x153A (bench 2026-10-20). A
  background tracker follows the full-rate drift.
- **USB protocol and MCP server.** Binary, CRC-checked, over USB CDC; no driver on Windows or Linux. PING median 0.89 ms
  over 500 PINGs with no failures (firmware 0.2.48). A full 16,382-sample dual-channel capture including readout in
  110 ms (firmware 0.1.2). A 30-minute capture soak with 17,908 content-checked requests, no failures and no restarts
  (firmware 0.2.49). The server has 32 tools in safety tiers; 30 are on by default, raw FPGA access and the reboot into
  FEL only when enabled. Anything that drives the calibration pin needs a confirmation. A device simulator lets you try
  the tools without hardware.
- **Shared control.** No remote lock-out. You and the agent change the same live settings. The screen follows the
  agent and the agent sees your touch changes. A chip in the top bar shows that a host is connected.
- **Spectrum analyser on the scope.** FFT up to 4096 points, three windows, averaging, max hold, waterfall and density
  views, tap to zoom, drag to pan, Auto. With the MMU and data cache on, a 4096-point FFT takes 2.72 ms instead of
  10.96 ms (firmware 0.2.48, 2026-10-19).
- **Decimation filters in the FPGA.** Slow time bases no longer alias by skipping samples: the FPGA filters the
  200 MS/s stream (boxcar, CIC2, CIC3, optionally a compensation FIR) before decimating, and captures carry 16-bit
  samples. A 892.857 kHz square sampled at 1 MS/s folds to 107 kHz. That alias measured -17.0 dBV with a boxcar and
  -76.1 dBV with CIC2 plus FIR, against a +1.8 dBV fundamental (bitstream 0x1535, 2026-10-19).
- **Diagnostics screen**: identity, FPGA bitstream and features, calibration state, USB link, restarts, battery, an
  event log, and a report saved to the card.
- **Digital phosphor view**, persistence 0.2 s to infinite, mono or heat palette.
- **External trigger on the calibration pin** (bitstream 0x1533 and later). In a loopback through the scope's own
  generator 10 of 10 captures triggered.
- **Generator** (a 32-bit DDS square wave on the calibration pin) and a **Bode sweep** tool on the host that uses it.
- **Hands-free development loop.** `fnirsi1013d-flash` reboots the scope into FEL, loads a build into RAM and can write
  it to the SD card over USB, with a backup first. The same commit gives the same image sha256 in any checkout and in CI.
- Menus no longer offer what the hardware cannot do (there is no DAC, so no generator waveform shapes).

![Spectrum analyser](https://raw.githubusercontent.com/mikljohansson/fnirsi-1013d-mcp/876700a97e5f211a61281d65d77d551f90d7d604/docs/screenshots/spectrum.png)
![Diagnostics, Status page](https://raw.githubusercontent.com/mikljohansson/fnirsi-1013d-mcp/876700a97e5f211a61281d65d77d551f90d7d604/docs/screenshots/diagnostics.png)

### What an agent can do with it

Prompts I have used: "capture CH1 and measure the frequency", "average 32 captures at 1 MS/s and fit the decay after
the trigger", "set the generator to 5 kHz at 25 % duty and show me the duty on CH2", "sweep this filter and give me the
corner frequency". The server saves large results as `.npz` files and returns the path, makes a plot and an HTML page
of a result on request, and logs every hardware-changing call. A Claude Code skill (`.claude/skills/fnirsi-scope`)
carries the measurement rules I want an agent to follow: ranges, averaging, generator safety. The point is repeatable
measurements while you hold the probes.

### Caveats, please read before you flash anything

- **Verified on one unit, the AL3A10 variant** (an unmarked 144-pin LQFP next to a Winbond 25Q80 config flash with a
  2x3 header). The inherited code covers the EF2L45 and EP4CE6 variants, but I have not tested them.
  `docs/hardware-prep.md` helps you identify your board.
- **The FPGA bitstream needs an SPI programmer.** The config flash is wired only to the FPGA, so the scope cannot flash
  it. You solder pins to the 2x3 header and use a 3.3 V programmer (I use an FT2232D board with a script in the
  repository). Back up the flash twice and compare first. The firmware runs without a new bitstream; the FPGA features
  need one. The bitstreams are attached to the release, with `BITSTREAMS.md` (hashes, wiring, rollback). Use 0x153B.
- **The SD card's FAT32 partition must start at 8 MiB (sector 16384) or later.** The firmware sits in the raw sectors
  before it. Most cards start lower, so you recreate the partition. The card tool refuses a card that does not fit and
  says what it needs.
- **The stock firmware stays.** Nothing here writes the MCU flash. The open firmware boots through Atlan4's bootloader,
  whose menu also starts the stock firmware and FEL mode.
- **Atlan4's bootloader and updater are not redistributed.** They exist only as binaries. A script in the release
  downloads them from Atlan4's repository on your machine and checks their sha256.
- Real limits are unchanged: 200 MS/s per channel, 8 bit, about 30 MHz bandwidth, no DAC.
- Tested on: one unit, my FNIRSI 1013D with the AL3A10 FPGA (JTAG IDCODE 0x18006C31), bootloader v0.8, bitstream 0x153B,
  development firmware up to 0.2.102 written to the card and cold-booted from it. Host: Windows 11, with WSL Ubuntu for
  the FEL loader and the card tools; the host tests also run on Linux and Windows in CI.
- **Use at your own risk.** Flashing the FPGA flash or the SD sectors can leave the scope unbootable until you restore
  a backup. Keep yours.
- Open items: whether the gains should be stored per range, a small CH2 offset after calibration, how the full-rate
  calibration drifts over hours. The release notes list the known issues.
- The bench log (`docs/hardware-status.md`) shows what was measured on the hardware and what was only built or
  simulated.

### How to install

Short version; the full steps are on the documentation site (Install).

1. Download from the release page ({{RELEASE_URL}}): `fnirsi_1013d-fw-{{VERSION}}.bin`,
   `fnirsi_1013d-sdtools-{{VERSION}}.zip`, the `fnirsi1013d` wheel and `SHA256SUMS`. Check the hashes.
2. Back up your SD card and, if you will touch the FPGA, the config flash.
3. Prepare a spare micro SD card with its FAT32 partition at 8 MiB. Build the SD parts with the tools zip
   (`make_release_sd_package.py build ... --parts parts`) and write only those parts with `make_sd_image.py write`.
4. Optional: flash bitstream 0x153B with your SPI programmer.
5. Boot, and run the calibration wizard when the first-run screen asks.
6. Install the MCP server from the wheel, point your agent at it (`fnirsi1013d-mcp --port auto`) and ask it to look at
   channel 1. Without a scope: `fnirsi1013d-mcp --sim`.

### How to help

- **Test on another variant** (EF2L45, EP4CE6) or another AL3 unit, and tell me what differs: the FPGA version word on
  the Diagnostics screen, the boot result, whether the calibration wizard passes.
- **Bench reports**: the Diagnostics report saved to the card, the alias and spur numbers on your unit, long-idle USB
  behaviour on your OS. Post them in the thread or open an issue; the entries in `docs/hardware-status.md` are a model.
- **Bug reports** with the firmware version (`#VER` or Diagnostics) and the steps. Pull requests are welcome, from
  people and from agents; protocol changes need host tests and an update of `docs/protocol.md`.
- Ideas for the metal-detector use case, which decides the order of the remaining work.

### Credits

- **pecostm32**: the reverse engineering (schematics, FPGA bus traces, emulator) and the original open firmware.
- **Atlan4**: years of firmware development, the custom FPGA bitstreams for the three variants, the bootloader and
  updater, and the community testing around them.
- **Donwulff**: audited fixes, a test harness and the headless Anlogic TD build flow.
- The people in this forum thread, whose notes, dumps and photos this started from.
- Research, firmware, MCP server and bench work in this repo were done by Claude (Fable 5.1, via Claude Code agents)
  coordinated by and with @mikljohansson, who owns the scope, the soldering iron and the decisions.

---

## Short version (Reddit, r/oscilloscopes and r/electronics)

**Title:** Open firmware, FPGA bitstreams and an MCP server for the FNIRSI 1013D tablet scope

I have released {{VERSION}} of an open firmware for the FNIRSI 1013D (Anlogic AL3A10 variant). It boots from the SD
card, so the stock firmware stays untouched. Over Atlan4's firmware it adds a frequency response mode that measures a
filter with the scope alone, a spectrum analyser (4096-point FFT in 2.7 ms), FPGA decimation filters against aliasing
at slow time bases, per-core ADC calibration with a wizard, a phosphor view, an external trigger and a USB protocol. A
Python MCP server lets an AI agent (Claude Code, Codex) drive the scope while you keep using the touch screen.

Verified on one unit only; the FPGA features need an SPI programmer. GPL-3.0. Release notes, install steps and the
bench log: {{RELEASE_URL}}. Credit to Atlan4, pecostm32 and Donwulff, whose work this builds on.

For r/ClaudeAI, replace the second sentence of the first paragraph onwards with: the MCP server has 32 tools in safety
tiers with confirmation tokens, a simulator you can try without the scope (`fnirsi1013d-mcp --sim`), a Claude Code skill
with measurement rules, and shared control, so you can touch the scope while the agent works. The use case is
pulse-induction metal detector design. Most of the code was written by Claude agents that I coordinated.

## Banner for the top of README.md (three sentences)

> **Release {{VERSION}}.** Open firmware, FPGA bitstreams and an MCP server for the FNIRSI 1013D (Anlogic AL3A10
> variant): frequency response and spectrum views on the scope, FPGA decimation filters, per-core calibration and a USB
> protocol, so that an AI agent can use the scope next to you. It runs from the SD card and leaves the stock firmware
> untouched; the FPGA features need an SPI programmer. See the release notes and "Install" below.
