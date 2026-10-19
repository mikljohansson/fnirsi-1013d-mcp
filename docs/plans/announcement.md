# Announcement draft (release 0.3.0)

Draft for the owner to edit. Written 2026-10-19 from the READMEs, `CHANGELOG.md`, `docs/hardware-status.md` and
`docs/plans/roadmap.md`. Every number below names its bench source; check each against `docs/hardware-status.md` once more
after the final bench round, and drop any line whose source has changed.

Placeholders to fill before posting:

- `{{VERSION}}`: the final version, expected 0.3.0 (firmware and MCP package share it).
- `{{RELEASE_URL}}`: https://github.com/mikljohansson/fnirsi-1013d-mcp/releases/tag/v{{VERSION}}
- `{{SCREENSHOT_SPECTRUM}}`, `{{SCREENSHOT_PHOSPHOR}}`, `{{SCREENSHOT_MCP_SESSION}}`, `{{CLIP}}`: images and a short clip (take
  screenshots with the `scope_screenshot` tool or a photo of the screen; the MCP chip in the top bar should be visible in one).
- `{{BITSTREAM_NOTE}}`: how readers get the FPGA bitstreams 0x1534 / 0x1535 / 0x1536. Today the `.bin` files are not in git and not
  attached to releases (see `docs/plans/release-readiness.md`); settle that first, then write one sentence here.
- `{{TESTED_ON}}`: the final list of what was benched (firmware version, FPGA version, host OS).

Where it goes, in this order, all on the same day (owner decision 2026-10-19: one announcement, no follow-up posts):

1. EEVblog forum, the FNIRSI 1013D thread (testgear subforum): the long version below.
2. The repository: pin the same text as a GitHub Discussion, and put the three-sentence summary at the top of `README.md`.
3. Reddit: r/oscilloscopes and r/electronics (hardware), r/ClaudeAI (the MCP angle), the short version below, one link each.

---

## Long version (EEVblog thread, GitHub Discussion)

**Title:** Open firmware, FPGA changes and an MCP server for the FNIRSI 1013D (AL3A10 variant), release {{VERSION}}

### What this is

A set of changes to the FNIRSI 1013D tablet oscilloscope that I have been working on, released as {{VERSION}}:

- a new scope firmware that runs from the SD card (the stock firmware in the MCU flash stays untouched, and removing the card puts
  the scope back to stock),
- changed FPGA bitstreams for the Anlogic AL3A10 variant,
- a USB protocol and a Python MCP server, so that an AI agent (Claude Code, Codex, anything that speaks MCP) can configure the scope,
  capture and analyse signals while you keep using the touch screen.

It stands on the work of others, above all Atlan4's firmware and FPGA design and pecostm32's reverse engineering (credits at the
end). The repository is https://github.com/mikljohansson/fnirsi-1013d-mcp, GPL-3.0.

I wrote this for one job: iterating on transmit and receive waveforms for a VLF / pulse-induction metal detector. Those are low
frequency signals, so the scope's real limits (200 MS/s, 8 bit, about 30 MHz of analog bandwidth) were never the problem. The
firmware additions are useful on their own without any agent in the loop.

### What it adds over Atlan4's firmware

Everything here is from the changelog and the bench log; the numbers are from my unit (AL3A10 variant), not claims about every unit.

- **USB control protocol and MCP server.** Binary, CRC-checked, over USB CDC; no driver on Windows or Linux. Measured: PING median
  0.89 ms with 500 PINGs and no failures (firmware 0.2.48 census), 64 KiB responses at 2.35 MB/s and a full 16,382-sample
  dual-channel capture including readout in 110 ms (firmware 0.1.2 bench), a 30-minute capture soak with 17,908 content-checked
  requests, no failures and no restarts (firmware 0.2.49). The server has 27 tools with safety tiers (anything that drives the
  calibration pin or reboots the scope needs a confirmation token) and a device simulator, so you can try the tools without
  hardware.
- **Shared control.** There is no remote lock-out. You and the agent change the same live settings; the screen follows the agent and
  the agent sees your touch changes. A small chip in the top bar shows that a host is connected.
- **Spectrum analyser on the scope.** FFT length up to 4096, Hann / flat-top / rectangular windows, averaging, max hold, waterfall
  and density views, tap to zoom, drag to pan, an Auto setting. With the MMU and data cache enabled (firmware 0.2.48) a 4096-point
  FFT takes 2.72 ms instead of 10.96 ms, and a pass of the scope view takes a median of 15 ms instead of 30 (FFT_BENCH and
  GET_STATUS on my unit, 2026-10-19). The agent can read the same spectrum over USB.
- **Decimation filters in the FPGA.** Slow time bases no longer alias by skipping samples: the FPGA filters the 200 MS/s stream
  (boxcar, CIC2, CIC3, optionally with a compensation FIR) before decimating, and the scope gets 16-bit samples. A 892.857 kHz square
  wave sampled at 1 MS/s folds to 107 kHz; the alias measured -17.0 dBV with a boxcar and -76.1 dBV with CIC2 plus FIR, against a
  +1.8 dBV fundamental (FPGA 0x1535 bench, 2026-10-19). The Nyquist spur from the two interleaved ADC cores, -27 dBV with plain
  skipping, is gone (-88 dBV) in the filtered modes (0x1534 bench).
- **Per-core calibration.** Each channel is two ADC cores interleaved. Their zero and gain differ, and the difference changes with the
  clock mode. Calibration v3 measures it per core and per clock class from the calibration post, a wizard walks you through it, and a
  background tracker follows the slow drift of the full-rate class. On my unit the two cores' gains differed by 3.3 % at full rate
  one night (bench 2026-10-18) and by 0 % the next morning, which is why a one-time calibration is not enough. After the v3
  calibration the classes agree on a post level (CH1 at -1.19 V: full-rate mean 93.57 codes, gated 93.48, CIC2 93.58; 2026-10-19
  08:30), but the full-rate A - B split was still 1.4 to 1.6 codes off within 15 minutes of the calibration. That is the case the
  v4 tracker is for; its bench is still open (see the caveats).
- **Digital phosphor view** with persistence of 0.2 s, 1 s, 5 s or infinite, mono or heat palette.
- **External trigger on the calibration pin** (FPGA 0x1533 and later). Loopback test through the scope's own generator: 10 of 10
  captures triggered, trigger index 50 ns early at 20 MS/s.
- **Generator** (the 32-bit DDS square wave on the calibration pin) and a **Bode sweep** tool on the host side that uses it.
- **Hands-free development loop.** `fnirsi1013d-flash` reboots the scope into FEL, loads a build into RAM and can write it to the SD
  card over the protocol, with a backup first.
- **Reproducible builds.** The same commit gives the same sha256 in any checkout and in CI.
- Menus no longer carry options the hardware cannot do (there is no DAC, so no generator waveform shapes).

{{SCREENSHOT_SPECTRUM}} {{SCREENSHOT_PHOSPHOR}} {{SCREENSHOT_MCP_SESSION}}

### What an agent can do with it

Prompts I have used: "capture CH1 and measure the frequency", "average 32 captures at 1 MS/s and fit the decay after the trigger",
"set the generator to 5 kHz at 25 % duty and show me the duty on CH2", "sweep this filter and give me the corner frequency". The
server saves large results as `.npz` files and returns the path, logs every hardware-changing call to an audit file, and ships a
Claude Code skill (`.claude/skills/fnirsi-scope`) with the measurement rules I want an agent to follow (ranges, averaging,
generator safety). It is not a toy for chatting about waveforms: the point is repeatable measurements while you hold the probes.

{{CLIP}}

### Caveats, please read before you flash anything

- **Verified on one variant only:** the Anlogic AL3A10 FPGA (unmarked 144-pin LQFP next to a Winbond 25Q80 config flash with a 2x3
  header). The inherited code also covers the EF2L45 and EP4CE6 variants, but I have not tested them. `docs/hardware-prep.md` helps
  you identify your board.
- **The FPGA bitstream needs an SPI programmer.** The config flash is wired only to the FPGA, so there is no way to flash it from the
  scope itself. You have to solder pins to the 2x3 header and use a 3.3 V programmer (I use an FT2232D board with a script in the
  repository). Back up the existing flash first, twice, and compare. The firmware works without a new bitstream (it falls back to
  the legacy path), but the filters, FIR, external trigger and the other FPGA features need bitstreams 0x1533 to 0x1535 (0x1536
  for segmented capture, if that ships in {{VERSION}}). {{BITSTREAM_NOTE}}
- **The stock firmware stays.** Nothing here writes the MCU flash. The open firmware boots from the SD card through Atlan4's
  bootloader, which has a menu for the open firmware, the stock firmware and FEL mode.
- **Atlan4's bootloader and updater are not redistributed.** They exist only as binaries, so the release includes a script that
  downloads them from Atlan4's repository on your machine and checks their sha256.
- Real hardware limits are unchanged: 200 MS/s per channel, 8 bit, about 30 MHz bandwidth, no DAC (the generator is a logic-level
  square wave).
- Tested on: {{TESTED_ON}}. Windows and Linux hosts (Linux through WSL for the FEL loader).
- **Use at your own risk.** Flashing the FPGA flash or the SD sectors can leave the scope unbootable until you restore a backup. I
  have done this on my unit with backups at hand; keep yours.
- Open items I am still measuring: how the full-rate calibration drifts over hours, a small channel-2 offset after calibration, and
  whether gains should be stored per range. They are listed in `docs/plans/task-queue.md`.
- Most of the research, firmware, MCP server and bench scripts were written by Claude agents coordinated by me, who owns the scope and
  did the soldering, the bench work and the decisions. The bench log shows what was measured on the hardware and what was only
  built or simulated.

### How to install

Short version; the full steps are in the README and `docs/releasing.md`.

1. Download from the release page ({{RELEASE_URL}}): `fnirsi_1013d-fw-{{VERSION}}.bin`, `fnirsi_1013d-sdtools-{{VERSION}}.zip`, the
   `fnirsi1013d` wheel and `SHA256SUMS`. Check the hashes.
2. Back up your SD card and, if you will touch the FPGA, the config flash (`docs/hardware-prep.md`).
3. Build the SD parts with the tools zip (`make_release_sd_package.py build ... --parts parts`) and write only those parts to a
   spare micro SD card with `make_sd_image.py write`. Boot the scope from it.
4. Optional, for the FPGA features: flash the bitstream with your SPI programmer.
5. Install the MCP server (`pip install` the wheel), point your agent at it (`fnirsi1013d-mcp --port auto`), and ask it to look at
   channel 1. Setup for Claude Code and Codex is in `mcp/README.md`. You can try it first against the simulator:
   `fnirsi1013d-mcp --sim`.

### How to help

- **Test on another variant** (EF2L45, EP4CE6) or another AL3 unit, and tell me what differs: the FPGA version word shown on the
  diagnostics screen, the boot result, whether the calibration wizard succeeds.
- **Bench reports.** Calibration results (GET_CAL dumps), the alias and spur numbers on your unit, long-idle USB behaviour on your
  OS. Post them in the thread or open an issue; the format of the existing entries in `docs/hardware-status.md` is a good model.
- **Bug reports** with the firmware version (`#VER` or the diagnostics screen) and the steps. Pull requests are welcome, from people
  and from agents; protocol changes need host tests and an update of `docs/protocol.md`.
- Ideas for the metal-detector use case, which is what decides the order of the remaining work.

### Credits

- **pecostm32**: the reverse engineering (schematics, FPGA bus traces, emulator) and the original open firmware.
- **Atlan4**: years of firmware development, the custom FPGA bitstreams for the three variants, the bootloader and updater, and
  the community testing around them.
- **Donwulff**: audited fixes, a test harness and the headless Anlogic TD build flow.
- The people in this forum thread, whose notes, dumps and photos this started from.

---

## Short version (Reddit, r/oscilloscopes and r/electronics)

**Title:** Open firmware + FPGA changes + MCP server for the FNIRSI 1013D tablet scope (spectrum view, decimation filters, per-core calibration)

I have released {{VERSION}} of an open firmware for the FNIRSI 1013D (Anlogic AL3A10 variant). It boots from the SD card, so the
stock firmware stays untouched. Over Atlan4's firmware it adds a spectrum analyser (4096-point FFT in 2.7 ms on the scope),
FPGA decimation filters that remove aliasing at slow time bases (alias -17 dBV with a boxcar, -76 dBV with CIC2 + FIR in my test),
per-core ADC calibration, a digital phosphor view, an external trigger on the calibration pin and a USB protocol. A Python MCP
server on top lets an AI agent (Claude Code, Codex) drive the scope and analyse captures while you keep using the touch screen.

Verified on one hardware variant only; the FPGA features need an SPI programmer. GPL-3.0. Release notes, install steps and the
bench log: {{RELEASE_URL}}. Credit to Atlan4, pecostm32 and Donwulff, whose work this builds on.

For r/ClaudeAI, replace the second paragraph with: the MCP server has 27 tools with safety tiers and confirmation tokens, a
simulator you can try without the scope (`fnirsi1013d-mcp --sim`), a Claude Code skill with measurement rules, and shared control
so you can touch the scope while the agent works. The measurement use case is pulse-induction metal detector design.

## Banner for the top of README.md (three sentences)

> **Release {{VERSION}}.** Open firmware, FPGA changes and an MCP server for the FNIRSI 1013D (Anlogic AL3A10 variant): a spectrum
> analyser, FPGA decimation filters, per-core calibration and a USB protocol, so that an AI agent can use the scope next to you. It runs
> from the SD card and leaves the stock firmware untouched; the FPGA features need an SPI programmer. See the release notes and "Getting
> started" below.
