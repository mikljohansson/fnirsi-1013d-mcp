# fnirsi1013d: MCP server, host library and device simulator

Python tooling for the FNIRSI 1013D open firmware's USB control protocol
([`docs/protocol.md`](../docs/protocol.md)) and the MCP tool surface in
[`docs/mcp-tools.md`](../docs/mcp-tools.md).

| Module | What it is |
|---|---|
| `fnirsi1013d.protocol` | Frame codec (16-byte LE header, CRC-16/CCITT-FALSE header, CRC-32 payload), incremental parser with resync, payload structs for every command, capability bits, error codes, scaling helpers |
| `fnirsi1013d.transport` | pyserial / TCP links, request engine (one request in flight, control requests during WAIT, retries through the duplicate cache, recovery), `#VER` probe, port auto-detect |
| `fnirsi1013d.simulator` | Device simulator speaking the exact protocol over TCP (`fnirsi1013d-sim`) |
| `fnirsi1013d.scope` | High-level client: numpy arrays, volts and seconds |
| `fnirsi1013d.analysis` | Statistics, edges, edge-aligned averaging, spectra, decay fits |
| `fnirsi1013d.server` | FastMCP server (`fnirsi1013d-mcp`) |
| `fnirsi1013d.vectors` | Checks and extends the shared `docs/protocol-vectors.json` |

## Install

Python 3.12 or newer on Windows or Linux (macOS should work, the scope is a plain USB CDC serial device, but nobody
has run it against the hardware there). No driver is needed.

### From a release (users)

Each GitHub release attaches the wheel `fnirsi1013d-X.Y.Z-py3-none-any.whl`. Install it as a tool, which puts
`fnirsi1013d-mcp` and the other commands on your `PATH` in an environment of their own:

```
uv tool install https://github.com/mikljohansson/fnirsi-1013d-mcp/releases/download/vX.Y.Z/fnirsi1013d-X.Y.Z-py3-none-any.whl
# or: pipx install <the same URL>
# or, into a venv you manage: python -m pip install <the same URL>
```

Replace `X.Y.Z` with the release (the release page lists the asset). Between releases,
`uv tool install "git+https://github.com/mikljohansson/fnirsi-1013d-mcp#subdirectory=mcp"` installs main.
Check it, without a scope:

```
fnirsi1013d-mcp --list-tools          # the tools, the data dir and the audit log; opens no port
fnirsi1013d-mcp --sim "--variant al3 --signal decay:tau=80us"   # a server on the built-in simulator (stdio)
```

Files the server writes go to a per-user directory, `~/.fnirsi1013d` (`%USERPROFILE%\.fnirsi1013d` on
Windows): `data/<date>/*.npz` (waveforms), `data/cal/<serial>.json` (per-unit voltage calibration),
`data/bode/thru/` (Bode thru calibrations), `reports/<date>/` (`scope_report`'s PNG and HTML pages) and
`logs/audit.jsonl`. `FNIRSI_MCP_HOME` or `--home` moves the whole
directory; `FNIRSI_MCP_DATA_DIR` / `--data-dir` and `FNIRSI_MCP_AUDIT_LOG` / `--audit-log` move one part (table
below). Nothing is written inside the installed package.

The bench CLIs need their inputs named when they do not run from a checkout: `fnirsi1013d-flash --image <bin>` and
`--backup-dir <dir>` for `--persist`, `fnirsi1013d-sd write ... --backup-dir <dir>`, `fnirsi1013d-bench
--bench-link <checkout>/firmware/scope/host_tests/bench_link.py`, `python -m fnirsi1013d.vectors --file
<protocol-vectors.json>`. Each refuses with a message saying which argument is missing.

### From a checkout (development)

With [uv](https://docs.astral.sh/uv/):

```
cd mcp
uv venv .venv
uv pip install --python .venv -e ".[dev]"
```

Plain venv works too: `python -m venv .venv`, then `.venv\Scripts\python -m pip install -e ".[dev]"` (Windows) or
`.venv/bin/python -m pip install -e ".[dev]"` (Linux). In WSL keep the venv on the Linux side (e.g.
`~/.venvs/fnirsi1013d-mcp`), because `mcp/.venv` is the Windows one. Run from a checkout, the CLIs default to the
repository's files where those exist: the `make gcc10` image for `fnirsi1013d-flash`, `backups/sdcard/` for SD
backups, `firmware/scope/host_tests/bench_link.py`, `docs/protocol-vectors.json`. The server's data dir and audit log
default to the per-user directory as for a wheel; point `FNIRSI_MCP_DATA_DIR` at `mcp/data` to keep using a
calibration file there.

`python .github/scripts/wheel_smoke.py` (from the repository root, needs `build`, in the dev extras) builds the
wheel, installs it into a fresh venv outside the checkout and runs the server from there: `--list-tools`, an MCP
stdio session on the simulator with a capture, the file locations. CI runs it on Linux and Windows.

The MCP SDK is pinned to `mcp>=1.12,<2`: mcp 2.x renamed FastMCP to MCPServer and changed its API.

## Simulator

```
fnirsi1013d-sim --variant al3 --port 5555 --signal decay:tau=80us
```

| Option | Meaning |
|---|---|
| `--variant stock\|al3\|ef2\|ep4` | FPGA version word 0x1432 / 0x1532 / 0x1632 / 0x1732 with its ring size, memory limit, fixed or programmable geometry, generator |
| `--variant al3x`, `--ext SIGNAL` | AL3 with the external trigger (0x1533): trigger source EXT on the calibration pin, fed by `--ext` (default: CH2's signal), 3.3 V logic, 22 ns latency |
| `--variant al3f` | AL3 0x1535: as al3d plus the CIC compensation FIR (protocol 1.11, caps FIR): CIC on N / 2, the 15 RTL taps, 250 ns latency |
| `--variant al3d` | AL3 0x1534: EXT trigger plus the decimation filter (protocol 1.9, caps DECIM): boxcar / CIC2 / CIC3 model with 16-bit samples, the 120 ns pipeline latency and the EXT latency correction (no device averaging) |
| `--variant al3e` | AL3 0x1538: al3s plus the per-capture encode self-check (protocol 1.19, caps ENC_CHECK, features bit 5): GET_HEALTH 88..103 count every full-rate capture; test hooks `Device.enc_bad_next` (garbled, re-taken clean, event 21) and `enc_unrecovered_next` (accepted garbled: GET_HEALTH 90 bit 2, last 0x33 = 0x77) |
| `--variant al3s` | AL3 0x1536: al3f plus the sequencer (protocol 1.17, caps SEQ): segment runs (slots, table entries with 5 ns timestamps, circular runs, STOP, READ_RAW addr_mode 3) and averaging runs (positions, exact lane sums, 16-bit u32 sums) on one continuous signal timeline. A run is modelled record by record in a background thread (about 8 ms per legacy record of a 1 kHz signal at 200 MSa/s); averaging runs of more than 128 records (16 with 16-bit records) scale the sums of the first ones, so the simulated noise stops falling there |
| `--signal SPEC` | CH1 signal; CH2 defaults to the matching TX gate (decay) or a sync square (sine/square) |
| `--ch1 SPEC`, `--ch2 SPEC` | per-channel signals |
| `--noise L` | Gaussian noise per ADC core, LSB rms (default 1.0, about 2 LSB as on the real unit) |
| `--hw-read-lag`, `--fw-read-lag` | true FPGA read lag and the value the firmware applies; a mismatch shifts READ_RAW by one word (test T3) |
| `--true-order b_first\|a_first` | physical A/B core order; `a_first` makes the 1.00o5 interleave convention wrong (test T4) |
| `--trigger-align-offset N` | offset of the latched `trigger_addr` against the crossing word (test T5) |
| `--time-scale X` | scales modelled acquisition time; 0 completes captures instantly |
| `--seed N` | reproducible noise and phases |
| `--caps MASK` | capability override; `0x1803` is firmware 0.1.0 on AL3 |
| `--fail-responses-over BYTES` | never send larger responses, like firmware 0.1.0 on the unit (over 5 KiB failed, T2) |

Signal specs are `kind:key=value,...` with SI units, at the BNC in volts:

| Kind | Keys (defaults) |
|---|---|
| `sine` | `freq` (1 kHz), `amp` peak (0.3), `offset`, `phase` |
| `square` | `freq`, `amp` (+-amp around offset), `duty` (0.5), `offset` |
| `decay` | PI-style: `period` (1 ms), `width` TX on-time (200 us), `tau` (80 us), `amp` (0.5), `tau2`, `amp2`, `flyback` spike volts with `flyback_tau` (0.5 us), `offset`, `tx` level during TX |
| `gate` | TX gate: `period`, `width`, `high` (3.3), `low` (0); high while TX is on, falling edge at turn-off |
| `dc` | `v` |
| `gen` | the calibration-pin square wave from the generator state (loopback for `scope_self_test_loopback`) |
| `zero` | noise only |

What the simulator models, all observable through the protocol: four 8-bit cores per capture with a core B
offset (+3 / -2 codes) that the calibration compensates; trigger on core A only with 2-dt quantisation;
`pretrigger_words` before the trigger; modular `capture_start_word`; auto timeout (`trigger_addr` forced to
`pretrigger_words`); stock FPGA with only part of the 4096-word ring fresh; collaborative mode (protocol 1.3,
the default) with one live settings set, `settings_seq`, RUN/STOP/SINGLE, UI captures in RUN every `--ui-period`,
GET_CAPTURE_INFO and test hooks for the person at the scope (`Device.ui_set_channel`, `ui_set_timebase`,
`ui_set_trigger`, `ui_press_run`, `ui_press_stop`, `ui_run_capture`, `on_arm`); with `--legacy` (or any `--caps`
without SHARED_MODE) the old protocol 1.2 LOCAL/REMOTE model with snapshot and
restore, watchdog, user tap (`Device.user_tap()`), DTR drop (TCP close); deferred WAIT and ACQUIRE_AVG with
control requests in between; the duplicate-request cache; E_BAD_CRC, E_BAD_LEN, E_VERSION, E_BUSY and the rest;
the `#VER` / `#LOCAL` probe; TEST_PATTERN; READ_SCREEN; a DEBUG_FPGA register model with the stale first byte.
The on-device Bode plot (protocol 1.20, every variant with the filter and the generator): BODE_CTRL / READ_BODE and
display mode 5 by `SimBode`, which runs the host tool's `BodeRunner` against the device through an in-process link
(the firmware's engine is a port of it), so a device sweep of `dut_in` / `dut_out` agrees with a host sweep; a host
ARM during it ends it (reason 5).

## MCP server

```
fnirsi1013d-mcp --port COM5                       # real scope
fnirsi1013d-mcp --port auto                       # discovery (below)
fnirsi1013d-mcp --port tcp://localhost:5555       # simulator in another terminal
fnirsi1013d-mcp --sim "--variant al3 --signal decay:tau=80us"   # in-process simulator, no second terminal
```

Port discovery (`auto`): serial ports whose USB product string is `FNIRSI 1013D`, or VID 0x1209 with a
pid.codes test PID 0x0001..0x0010, are tried first; the ST VCP IDs 0x0483:0x5740 (1.00o5 descriptors) only
count when the `#VER` probe answers `#OK,FNIRSI-1013D-AI,...`.

| Variable | Default | Meaning |
|---|---|---|
| `FNIRSI_MCP_PORT` | `auto` | port when `--port` is not given |
| `FNIRSI_MCP_MAX_TIER` | 3 | highest safety tier registered |
| `FNIRSI_MCP_ENABLE_RAW` | 0 | allow tier 4 |
| `FNIRSI_MCP_GEN_MAX_HZ` | 1000000 | generator frequency ceiling |
| `FNIRSI_MCP_GEN_MAX_DUTY` | 50 | generator duty ceiling, percent |
| `FNIRSI_MCP_HOME` | `~/.fnirsi1013d` | per-user directory holding `data/` and `logs/` (`--home`) |
| `FNIRSI_MCP_DATA_DIR` | `<home>/data` | `.npz` output (one folder per day), `cal/`, `bode/thru/` (`--data-dir`) |
| `FNIRSI_MCP_AUDIT_LOG` | `<home>/logs/audit.jsonl` | JSONL log of every tier 1+ call (`--audit-log`; empty string disables) |
| `FNIRSI_MCP_LEGACY_IDLE_RELEASE_S` | 60 | legacy firmware only: release REMOTE (keeping the host settings) after this many seconds without a tool call; 0 = hold until exit |
| `FNIRSI_MCP_CAL_FILE` | `<data dir>/cal/<serial>.json` | per-unit voltage calibration (`--cal-file`), loaded at connect if present, written by `scope_calibrate_volts` |
| `FNIRSI_MCP_MAX_RESPONSE` | device limit | largest response payload requested at once (`--max-response`); READ_RAW, READ_TRACE and READ_SCREEN are split, and device averaging falls back to the host when its result would not fit |

### Claude Code and Codex setup

The skill `.claude/skills/fnirsi-scope/` (body: [`docs/agent-measurement-guide.md`](../docs/agent-measurement-guide.md))
teaches an agent a measurement session; with a wheel install, give the agent that guide (copy the skill directory
into `~/.claude/skills/`, or point Codex at the guide).

**Claude Code, wheel install.** One command registers the server for your user in every project
(`fnirsi1013d-mcp` must be on `PATH`, as after `uv tool install`; otherwise give its full path, e.g.
`<venv>/bin/fnirsi1013d-mcp` or `<venv>\Scripts\fnirsi1013d-mcp.exe`):

```
claude mcp add --scope user fnirsi1013d -e FNIRSI_MCP_MAX_TIER=3 -- fnirsi1013d-mcp --port auto
claude mcp add --scope user fnirsi1013d-sim -- fnirsi1013d-mcp --sim "--variant al3 --signal decay:tau=80us"
claude mcp list
```

Or per project: copy [`.mcp.json.example`](../.mcp.json.example) to `.mcp.json` in the project and edit it:

```json
{
  "mcpServers": {
    "fnirsi1013d": {
      "type": "stdio",
      "command": "<fnirsi1013d-mcp, or its full path>",
      "args": ["--port", "<auto, COM5 or /dev/ttyACM0>"],
      "env": { "FNIRSI_MCP_MAX_TIER": "3" }
    }
  }
}
```

`--port auto` finds the scope (below); name the port when more than one serial device answers. Add
`"FNIRSI_MCP_HOME": "<dir>"` to `env` to keep the data and audit log somewhere other than `~/.fnirsi1013d`.

**Claude Code, checkout (the owner's workflow).** `.mcp.json` is not tracked: copy `.mcp.json.example` and point
`command` at the checkout's venv (`mcp/.venv/Scripts/fnirsi1013d-mcp.exe` on Windows, relative paths are resolved
from the repository root), the port at the scope's (COM10 on the owner's bench), and keep
`"FNIRSI_MCP_DATA_DIR": "mcp/data"`, `"FNIRSI_MCP_AUDIT_LOG": "mcp/logs/audit.jsonl"` in `env` if the per-unit
calibration lives in `mcp/data/cal/`. In WSL use the Linux venv, e.g. `~/.venvs/fnirsi1013d-mcp/bin/fnirsi1013d-mcp
--port /dev/ttyACM0`.

**Codex** reads MCP servers from `~/.codex/config.toml` (Codex has no skills directory: point it at
`docs/agent-measurement-guide.md`):

```toml
[mcp_servers.fnirsi1013d]
command = "fnirsi1013d-mcp"            # or the full path in a venv / checkout
args = ["--port", "auto"]
env = { FNIRSI_MCP_MAX_TIER = "3" }

[mcp_servers.fnirsi1013d-sim]
command = "fnirsi1013d-mcp"
args = ["--sim", "--variant al3 --signal decay:tau=80us"]
```

Only one session may hold the scope's serial port: do not run the real-scope server in two clients at once.

### Tools

Names follow the mcp-builder convention (`scope_` prefix, snake_case); each maps 1:1 to the tool of the same
name in docs/mcp-tools.md section 4 (`set_channel` there is `scope_set_channel` here, and so on).

| Tool | Tier | Does |
|---|---|---|
| `scope_info` | 0 | firmware, protocol, FPGA variant, serial, capabilities, memory limits |
| `scope_profile` | 0 | static capability profile, live values, unverified facts, standing caveats |
| `scope_status` | 0 | mode, display_mode (scope / spectrum / waterfall / density / phosphor / bode), run_state (RUN/STOP/SINGLE), settings_seq, latest capture id and origin, host capture state, battery, link statistics; from protocol 1.17 the bitstream's features and the acquisition menu's Average (`ui_averaging` off / software / hardware, records per hardware run); features `enc_check` (1.19) and `bode` (1.20) |
| `scope_get_config` | 0 | channels, timebase, trigger and generator in physical units |
| `scope_read_calibration` | 0 | offset centres, gains, A/B compensation, volts per code per range |
| `scope_screenshot` | 0 | PNG of the 800x480 screen |
| `scope_fetch_waveform` | 0 | the latest capture (what the screen shows) with the settings it was taken under, without capturing |
| `scope_measure` | 0 / 1 with `capture=true` | vpp, vmin, vmax, mean, rms, ac_rms, frequency, period, duty, rise/fall time, CH1->CH2 delay |
| `scope_set_timebase` | 1 | sample rate, window or raw divider |
| `scope_set_decimation` | 1 | decimation filter off / boxcar / CIC2 / CIC3 (fw >= 0.2.30, FPGA 0x1534), with `+fir` the compensation FIR (fw >= 0.2.44, FPGA 0x1535): effective filter, N, shift, codes per count, effective bits; captures then read 16-bit samples |
| `scope_set_trigger` | 1 | source, edge, level (volts or code), mode, pre-trigger, total samples, auto timeout |
| `scope_capture` | 1 | host single capture (the screen shows it, the scope stays in STOP); statistics, file, inline samples or preview |
| `scope_acquisition_control` | 1 | arm / wait / force / stop / run (RUN/STOP shared with the scope's button) |
| `scope_averaged_capture` | 1 | n captures averaged, hardware or edge alignment, on-device averaging when available; `hardware=true` sums the n records in the FPGA in one run (bitstream 0x1536, fw >= 0.2.58; records up to 2046 words, n <= 257 with 8-bit samples); restarts when a setting is changed at the scope |
| `scope_capture_segments` | 1 | segmented capture (bitstream 0x1536, fw >= 0.2.58): the FPGA re-arms after every trigger and keeps `count` records with 5 ns trigger timestamps (or refills them until `timeout_s` with `circular`); per segment time, interval, triggered, min / max / mean / Vpp; `.npz` with every record |
| `scope_capture_and_fft` | 1 | averaged amplitude spectrum, peaks in dBV and dBc, noise floor, THD |
| `scope_spectrum` | 0 | the spectrum the scope's own spectrum view shows (READ_SPECTRUM, fw >= 0.2.28): bins, rate, bin width, window, ENBW/RBW, gain, peaks; `cross_check` recomputes it on the host from the same capture |
| `scope_decay_fit` | 1 | edge-aligned average, then `exp`, `exp+offset`, `exp2` or `power` fit after the TX edge; tau, R^2, sample points |
| `scope_set_channel` | 2 | range, coupling, offset, probe, invert; relay changes limited to 5 per second |
| `scope_event_log` | 0 | the scope's last 128 USB / link events with device ms (protocol >= 1.5), the pre-reset snapshot and (fw >= 0.2.24) the bus-level ring; `fpga_trace` (fw >= 0.2.72) switches the FPGA write trace (event 22) on or off |
| `scope_sd_info` | 0 | SD card size, partition, write window, eGON headers at 16/80/750 (optionally checksum-verified); fw >= 0.2.2; from fw 0.2.52 the identity of the image the card boots and whether it is the running one |
| `scope_health` | 0 | fw >= 0.2.52: USB state now, MMU / caches, power-on number, operating hours, settings save state, the stored restart record, in words; fw >= 0.2.62 (1.19) `encode_check`: the full-rate capture check (mode, last 0x33 byte and failing pins, captures checked / garbled / re-taken) |
| `scope_save_report` | 1 | fw >= 0.2.52: the scope writes its diagnostics report to `reports/` on its SD card; returns the file name |
| `scope_calibration_info` | 0 | scaling in effect per channel, the scope's calibration (v1 / v2, flags, current correction, stored per-core zeros per clock class), calibration file and its fitted zero / slope / gain per range and core |
| `scope_cal_probe` | 2 | fw >= 0.2.67 (protocol 1.21, diagnostic): the calibration wizard's zero measurement of one channel and range on the scope (verdict, failing point, per point and core mean / RMS / extremes / clipped); nothing stored |
| `scope_calibrate_volts` | 2 | a known DC voltage on every range: fits 0 V code, volts per code and offset slope per range and ADC core, saves the calibration file |
| `scope_generator` | 3 | calibration-pin square wave; confirmation token to enable |
| `scope_self_test_loopback` | 3 | generator into CH1: frequency, duty, swing (the output is not ground-referenced), bench tests T4, T5, T7; token |
| `scope_bode_sweep` | 3 | frequency response CH2 / CH1 of a passive DUT on the calibration post: stepped clean-edge tones, coherent 16-bit records, single-bin DFT; magnitude, phase, markers, thru calibration, CH2 auto-ranging; `where='device'` (fw >= 0.2.66) lets the scope run it and show the plot; token (below) |
| `scope_report` | 0 | a saved result (`.npz` / Bode `.json`, or `latest`) as a PNG plot (matplotlib, extra `plot`) and a self-contained HTML page (plot, table, caveats, command) under `<home>/reports/<date>/`; touches no scope (below) |
| `scope_raw_fpga` | 4 | raw FPGA bus operations (DEBUG_RAW builds); needs `FNIRSI_MCP_ENABLE_RAW=1` and a token |
| `scope_enter_fel` | 4 | reboot into FEL; needs `FNIRSI_MCP_ENABLE_RAW=1` and a token |

**Collaborative mode** (firmware with the SHARED_MODE capability, protocol 1.3; docs/plans/collaborative-mode.md).
There is no ownership: the person at the scope and the agent share one live set of settings, the last change
wins, and the screen shows host captures (a green "MCP" chip marks a connected host). `scope_capture` is a host
single: the scope stays in STOP afterwards until either side presses RUN (`scope_acquisition_control(action=
'run')`). `scope_fetch_waveform` reads whatever the scope captured last, converted with the settings of that
capture (`capture_settings` in the result). Every capture carries the `settings_seq` it was taken under. Long runs
(`scope_averaged_capture`, `scope_decay_fit`, `scope_capture_and_fft`) pin their settings to one `settings_seq`;
when someone changes a setting at the scope during the run, the run starts again with the new settings and
`restarts` (plus a caveat) says after how many captures and what changed. After 5 restarts the tool gives up.
A SET_* while a scope menu is open fails with "modal screen" (E_BUSY), as before.

**Legacy firmware** (no SHARED_MODE, protocol <= 1.2) keeps the exclusive model: every tier 1+ tool takes REMOTE
when needed and says so in `caveats`, the touch UI is frozen while the host holds it, and a tap on the banner makes
the next call fail with "scope released by user". There is no release tool any more: the server releases REMOTE,
keeping the host's settings, after `FNIRSI_MCP_LEGACY_IDLE_RELEASE_S` (60 s) without tool calls and at exit.

Confirmation tokens are bound to the exact
arguments, single-use, and expire after 120 s. Waveforms of any length go to `.npz` (`t`, volts, codes, clip
flags, `meta_json`; decimated captures add `CH<n>_samples16`, the stored u16 filter outputs, and
`meta_json["decimation"]`); results up to 2000 points also carry the samples inline, longer ones a 500-point min/max
preview.

Physical units: volts are converted per ADC core. With the scope's own calibration v3 in use (firmware >= 0.2.45,
protocol 1.12, GET_CAL version 3 after a base calibration at the scope), each core's 0 V code and gain come from the
scope's stored block for the capture's clock class (full rate: divider 0 and filtered captures; gated: legacy divider
>= 1): zeros plus the per-class offset slope, and per-core gain corrections onto the gated mean gain (so 200 MSa/s,
filtered and gated captures agree, and the cores agree at full rate). READ_TRACE codes are already corrected on the
scope; a per-unit file then only contributes its gated per-core gains (`scaling` = `scope calibration v3 (...)`).
Calibration v2 (firmware 0.2.43-0.2.44) is refused: update the firmware and re-run the base calibration. Otherwise, with a per-unit calibration covering the range,
`raw_code = zero + slope x (offset_pwm - pwm_ref) + volts / gain` per core (fitted by `scope_calibrate_volts`,
or imported from bench data with `python -m fnirsi1013d.calibration --import <t8 json...> --out <file>`); otherwise
the firmware's `uv_per_lsb_q8` and `zero_code_q8` (the same for both cores, after the stored compensation).
Results say which one was used (`scaling`). T8 on the owner's unit (fw 0.1.7): gains within ~1 %, but the
firmware zero was ~2 codes off (cores 5 codes apart, compensation corrects 4) and follows the trace offset with a
different slope. Time is
`(i - pretrigger_samples) x dt` relative to the hardware trigger, which is +-2 samples until T5 is run.

## Bode sweep: `scope_bode_sweep` and `fnirsi1013d-bode`

Host-driven frequency-response analyser (docs/plans/bode-design.md, steps H1-H3; `bode.py`). Needs an AL3 bitstream
with the decimation filter (0x1534+, best 0x1535 with the FIR) and the generator.

**Wiring.** Calibration post to the DUT input, CH1 tip on the DUT input, CH2 tip on the DUT output. **Wiring A
(default): the DUT ground on the probe ground clips.** Wiring B, for a DUT that must not see the probe reference: the
DUT ground on the scope's USB ground (USB shell), both clips open; the DUT then sits 1.24 V below the probe reference,
so pass `ch2_coupling='ac'` (`--ch2-coupling ac`) or keep CH2 at 0.5 V/div and above. Never both. Bench 2026-10-19 (fw 0.2.50, `backups/bode/`): on a 1 kOhm / 15 nF RC, wiring A and wiring B gave the same H within 0.4 dB from 1 kHz to 300 kHz (rc1_15n_clips_2.json vs rc1_15n_usbgnd.json); in wiring B the floor was -80 dB, the thru +0.3 dB, and a 991 / 102 Ohm divider read flat -20.5 dB / 0 +-1.4 deg. The R71 return error did not show. Wiring B costs the 1.24 V between the grounds: with DC coupling it saturated CH2 on 0.1 and 0.2 V/div (a flat record the offset cannot reach).
A saturated CH2 is flagged `ch2_overload` (left out of the markers); auto ranging steps CH2 back up, a fixed range
stops with the remedy, and an offset that runs out of PWM range is a caveat. Passive DUTs or buffered inputs only; the post has nothing but R21 (1 kOhm)
between the DUT and the FPGA pad. H = CH2 / CH1 at the DUT input node, so it does not depend on the 1 kOhm source; a
filter specified for a 50 / 600 Ohm source shows its V2 / V1, not its insertion response.

**Per point.** f = 50 MHz / M for an integer M (clean edges, no DDS pattern tones; `f_actual` in the table), raw
DDS words; rate 200 MSa/s / N with N = 16 x 2^k (257-512 samples per period) below 24 kHz, N = 16 up to 1.5 MHz,
legacy 200 MSa/s (8-bit) above; CIC3 + FIR (the firmware drops to CIC2 below about 24 Hz, the table says which
filter ran); a record of a whole number of periods (>= 8, 2048-8190 samples), so the square's harmonics land on other
bins; settle time `settle` periods (3x at the first point and after the generator was off); the DFT at the exact
DDS frequency (a Hann window below about 50 Hz where the 32-bit increment rounding breaks the coherence). The plan
is deterministic: the same start, stop and points give the same frequencies (`docs/bode-plan-vectors.json`).

**Ranging.** A pre-scan at the first frequency picks CH1's range (and CH2's in `ch2_range='auto'`) and centres both
on their DC level with the offset. `auto` steps CH2 to a more sensitive range when the next range would still be
under 60 % of full scale, measures that point on both ranges and scales the rest of the sweep onto the first range
(`spliced`, factors in `ranges.splices`); a clip steps back up and repeats the point. At most 6 switches per sweep.
`fixed` keeps CH2's range (or give a V/div).

**Quality flags** per point: `clipped` / `ch1_clipped`, `ch1_low` (CH1 under 20 codes: the DUT input loads the
source), `ch2_small` (under 3 codes: +-1 dB), `near_floor` (CH2 under 20 dB above a generator-off capture in the
same bin), `low_snr`, `unsettled` (the record halves disagree by > 0.1 dB / 1 deg after a retry with twice the
settle), `incoherent` (energy beside the fundamental on CH1), `fir_off`, `spliced`, `no_cal` / `cal_range` (thru
calibration). Points with a flag from the first group are left out of the markers (-3 dB crossings relative to the
higher end, or peak, -3 dB points and Q for an interior peak; phase crossings).

**Thru calibration.** Both probes on the DUT input, `thru_cal='measure'` (stored under `thru_name` in
`<data_dir>/bode/thru/`), then `thru_cal='<name>'` with the same start, stop and points divides it out (CH1 / CH2
gain, skew, probe mismatch). Points measured on other ranges than the calibration are flagged `cal_range`.

**Time.** About 40 ms per point plus the settle and record time: 50 points 1 kHz-1 MHz about 3 s, 100 Hz-1 MHz about
5 s, 10 Hz-100 kHz about 27 s (the low decade dominates). The tool refuses sweeps estimated above `max_duration_s`
(120 s). E_BUSY (a menu open on the scope) is retried once, then the sweep stops with the partial table. Every
setting (generator words, filter, channels, time base, trigger incl. EXT, RUN) is given back.

**Files.** `.csv` (the table), `.json` (table, summary, splices, caveats, metadata) and `.npz` (`f_requested`,
`f_actual`, `m`, `n`, complex `h`, `h_raw`, `x1`, `x2`, `mag_db`, `phase_deg`, ranges, `snr_db`, `valid`).

```bash
fnirsi1013d-bode --plan-only --start 1kHz --stop 1MHz --points 50     # the frequency plan and the time estimate
fnirsi1013d-bode --port COM10 --start 1kHz --stop 300kHz --points 30 --yes --thru measure --thru-out thru.json
fnirsi1013d-bode --port COM10 --start 1kHz --stop 300kHz --points 30 --yes --thru thru.json --csv rc.csv
fnirsi1013d-sim --variant al3f --ch1 dut_in:r=1.6k,c=10n --ch2 dut_out:r=1.6k,c=10n   # an RC on the simulator
```

The simulator's `dut_in` / `dut_out` signals are the input and output node of an RC (`r`, `c`) or series RLC (`l`)
network on the generator through `rs` (1 kOhm), against the probe ground, with a 30 MHz front end; `gain` and
`delay` on one channel model a probe mismatch for thru-calibration tests.

**On the scope: `where='device'`** (firmware 0.2.66+, protocol 1.20, BODE_CTRL 0x0211 / READ_BODE 0x0212). The
scope's own engine (fw-bode, bode_sweep.c: the same planner, ranging, splice and flags) runs the sweep and draws the
Bode plot; the tool sends BODE_CTRL action 1 with the settings (or 5 for `thru_cal='measure'`), polls READ_BODE until
the state leaves `running` (progress per point), reads the 20-byte rows, recomputes `f_actual` from the DDS period M,
maps the row flags to the names above (bit 14 = a retry) and returns the same structure as the host sweep plus
`device` (sweep id, state, reason, thru state, view). The device has no SNR / leakage / code levels per row (flags
only; `snr_db` is null). Limits: the four settle presets, `ch2_range` 'auto' or 'fixed' (hold: CH2's range at the
start), DC coupling. Thru: `'measure'` (kept on the scope until power-off and stored here as for the host), `'device'`
(the scope's copy), a stored name (applied on the host to the raw rows). The Bode view stays on the screen (the
scope's own acquisition is off; capture tools then add a caveat) unless `close_view=true`, which also gives the
scope's settings back. A host capture during a device sweep ends it (READ_BODE reason 5). Library:
`bode.device_sweep(scope, start, stop, points, ...)`, `Scope.bode_ctrl` / `read_bode`.

**Plots.** `scope_report` (below) on `files.npz` draws magnitude and phase over log f with the -3 dB marker; flagged
points are hollow.

Not yet: the harmonic fast mode, the impedance mode (`z_series_ohm`), CH1 re-ranging during a sweep.

## Reports: `scope_report` and `python -m fnirsi1013d.report`

The tools measure; `scope_report` presents a result for the person. Input: a result file a tool returned (`file`,
`files.npz`, a Bode `.json`) or `latest` (the newest `.npz` in the data directory). Output, under
`<home>/reports/<date>/`: `<stem>.png` (matplotlib; `uv pip install "fnirsi1013d[plot]"`, included in `dev`) and
`<stem>.html`, one file with the PNG embedded as base64, the result table, the summary (Bode markers, fit quality,
captures averaged), every caveat the tool recorded plus `caveats`, and the generating command (`command`, or the tool
and the arguments recorded in the file). No scripts, fonts or CDNs: it opens offline, follows the light / dark
preference and can be published as is (in Claude Code with the Artifact tool). Without matplotlib the page says why
there is no plot and carries the numbers. Kinds: capture / fetch, averaged capture, decay fit (with the residual),
segments (overlaid), host FFT, the scope's spectrum, Bode (host or device).

```bash
python -m fnirsi1013d.report latest                      # newest result under ~/.fnirsi1013d/data
python -m fnirsi1013d.report path/to/123456_789_decay.npz --title "Coil A, 8 mm steel" --caveat "probe x10"
```

## Firmware dev loop: `fnirsi1013d-flash`

`fnirsi1013d-flash [--image PATH] [--port COM10] [--no-fel] [--distro Ubuntu]` reboots the running scope into
FEL with ENTER_FEL, hands the boot ROM device (1f3a:efe8) to WSL with usbipd, loads the image into RAM with
`sunxi-fel -p write 0x7FFFFFE0 <image> exe 0x80000000`, and waits for the serial port and `#VER`. Details and exit
codes: firmware/README.md section 4. It changes what the scope runs: announce it at the bench first.

ENTER_FEL is sent up to 3 times, 1 s apart, each as a fresh request: the firmware answers OK before it reboots, and
a request lost on the way in (fw <= 0.1.5 RX bug) is harmless to resend. Between attempts it looks for the FEL
device: if it is there, the OK was lost after the scope executed the command, and the tool continues. If none is answered and the scope still
answers `#VER`, the tool stops ("still runs firmware ..., nothing was loaded"). Every failure prints one `FAILED:`
line and exits non-zero (2 image, 3 scope link / ENTER_FEL, 4 usbipd / FEL device, 5 sunxi-fel, 6 port did not
come back, 1 anything else).

### Making an image permanent: `--persist`

With firmware 0.2.2 or later (capability SD_SECTORS) the flash tool can write the image to the SD card through
the USB protocol, without removing the card:

```
fnirsi1013d-flash --image <bin> --sha256 <hash> --persist   # RAM load as above, then write sector 750
fnirsi1013d-flash --image <bin> --persist-only              # write the card through the running firmware
fnirsi1013d-flash --restore backups/sdcard/sd_750-<end>_<time>.bin  # put a backup back
```

`--persist` refuses images whose eGON header length or checksum is wrong (run `firmware/tools/mksunxi.py`), cards
without a partition table, images that do not fit below `write_end` (min(partition start, 2048)), and images
whose version (the `#VER` literal in the binary) differs from what the running firmware answers to `#VER`
(persist what is running and tested; `--force` overrides). It first backs up sectors 750..750+n (n = the larger
of the new and the old image) to `backups/sdcard/sd_750-<end>_<time>.bin` (read twice and compared, saved,
re-read) and prints its sha256. It then writes 32-sector pieces (`--write-sectors`; an SD_WRITE blocks the
scope's main loop for the write and its read-back, up to ~4 s for 128 sectors on fw 0.2.3; 5 s timeout, resent
with the same seq so the duplicate cache answers a completed write): the 484-byte staging chunks, the stage
CRC the firmware checks before writing, and the CRC the firmware reads back from the card are all checked. Then
it reads the image back with SD_READ_SECTORS and compares, and SD_INFO VERIFY recomputes the eGON checksum on the
card. Each phase prints its duration.

If the link drops during the write (bench 2026-10-18: the scope fell off USB at 256 KiB and re-enumerated by
itself), the tool waits up to `--port-return` (15 s) for the port, reconnects and resends the unconfirmed
piece (while the scope still answers PING the piece is resent on the same connection instead); pieces before it are already confirmed by the card's read-back CRC. A piece is tried at most 4 times. The
summary lists the pieces that needed resends. `--stage-delay-ms` pauses between staging requests (each already
waits for its answer). When it cannot finish, the FAILED line says that the card may hold a partial image and
must be repaired (rerun or `--restore`) before the scope is power-cycled. Only sector 750 onwards is touched: the bootloader (16-79), the
calibration, settings and boot-mode sectors (708-710) need explicit firmware flags the tool never sets. Failures
exit with code 7 and name the backup to restore.

### SD card from the bench: `fnirsi1013d-sd`

```
fnirsi1013d-sd info [--verify]                        # card, partition, write window, eGON headers 16/80/750
fnirsi1013d-sd read 750 128 -o sectors.bin            # CRC-checked reads
fnirsi1013d-sd write file.bin --sector 1000 --dry-run # the firmware's window and flag rules, nothing written
fnirsi1013d-sd write file.bin --sector 1000 --yes     # type "WRITE <n>@<sector>" to confirm; backup first
```

`--boot` allows sectors 16-79, `--config` 708-710. Writes back up the target range to `backups/sdcard/`, write
128-sector pieces with CRC checks and read everything back. The MCP server only has the read-only
`scope_sd_info`; writing the card is never an agent tool.

## Link soak: `fnirsi1013d-soak`

```
fnirsi1013d-soak --port COM10                                # 30 min GET_STATUS at 10 Hz (the v0.3.0 soak)
fnirsi1013d-soak --mode capture --duration 600 --log soak.log   # 4 KiB TEST_PATTERN, content-checked
fnirsi1013d-soak --mode ping --interval 0.02
fnirsi1013d-soak --mode reopen --duration 10 --idle 180 --rounds 10   # suspend / resume bench (fw 0.2.44)
```

Holds the port open and polls (`--mode status|ping|capture`, `--interval`, `--duration`), LOCAL-safe.
`--mode reopen` runs `--rounds` of (GET_STATUS soak for `--duration`, close the port, leave it closed `--idle`
seconds, reopen) and prints after every reopen the host's USB selective-suspend setting, the new event-log entries
(fw 0.2.44: bus reset with the ms since a suspend, suspend with POWER and the frame number, resume with the ms
suspended and whether it was inferred) and counters 36 / 37 (SUSPEND / RESUME interrupts). It never
gives up on a drop: it polls the COM port list every 0.5 s while the port is gone and reopens when it is back
(`--reopen-timeout`, default 60 s). Output, one line per event, wall-clock time in ms:

```
2026-10-18T17:01:02.300 host: USB selective suspend AC disabled, DC enabled; port COM10 1209:0001 ..., location 1-9.3, hwid USB VID:PID=1209:0001 SER=1013D-0001 LOCATION=1-9.3
2026-10-18T17:01:02.310 soak mode status, interval 0.1 s, duration 1800 s
2026-10-18T17:01:02.345 open COM10 (fw 0.2.11, proto 1.4)
2026-10-18T17:01:02.350 uptime open 431977 ms
2026-10-18T17:01:02.360 stats start (41 counters) rx_packets=10 rx_bytes=1145 ...
2026-10-18T17:12:40.118 fail #1 GET_STATUS: write to COM10 failed: WriteFile failed (...) [winerror 22]
2026-10-18T17:12:40.120 port COM10 gone
2026-10-18T17:12:42.640 port COM10 back after 2.52 s
2026-10-18T17:12:42.650 events reopen: 3 new of 41 since boot (device now 691234 ms)
2026-10-18T17:12:42.651   device 689001 ms (~host 2026-10-18T17:12:40.417) suspend
2026-10-18T17:12:42.651   device 689003 ms (~host 2026-10-18T17:12:40.419) bus_reset
2026-10-18T17:12:42.651   device 690120 ms (~host 2026-10-18T17:12:41.536) dtr 1
2026-10-18T17:12:42.660 stats reopen delta usb_bus_resets +1 usb_suspends +1 dtr_up +1 (since start: ...)
2026-10-18T17:12:42.700 open COM10 (fw 0.2.12, proto 1.5)
2026-10-18T17:12:43.020 reopen COM10 ok (down 2.90 s)
2026-10-18T17:20:11.002 DEVICE RESTARTED (uptime went backwards: 1580221 -> 175012 ms)
2026-10-18T17:31:02.400 stats end delta none (since start: usb_bus_resets +1 usb_suspends +1 dtr_up +1)
2026-10-18T17:31:02.401 summary requests 17985, failures 1, drops 1 (down 2.90 s), longest gap 2.90 s, latency median 1.1 ms, p99 2.3 ms, max 12.0 ms; counters delta usb_bus_resets +1 ...
```

On protocol >= 1.5 (fw 0.2.12) the first requests after a reopen are the fast-path GET_EVENT_LOG and
GET_LINK_STATS, so the device's event ring still holds what happened before the reset; the new events are
printed with device ms and the matching host time (a host port reset shows as suspend, then bus reset). The
counter deltas cover link_resets, dtr_up/down, the usb_* events, ep0 setups, usb_irq_polled (lost USB interrupts
served by the timer poll; also in the summary), rx_stalls and the CRC/timeout counters. The pre-reset snapshot
(fw 0.2.15+, the events right before a bus reset) is read with a plain request, saved, and only then re-armed
(fw 0.2.18 flag). GET_STATUS uptime_ms is logged at open, every reopen and the end, and checked on every status
poll: a decrease means the firmware restarted itself (which looks like a USB drop to the host); it prints
DEVICE RESTARTED and is counted as a device restart, separately from drops. `fnirsi1013d-bench` compares the
uptime before and after its run the same way. A request that only times out (no port error) is logged and
followed by the protocol's recovery; a write/read error or a vanished port is a drop. Exit 0 when nothing failed,
1 on any failure, drop or restart, 2 when the port cannot be opened at start.

## Bench link check: `fnirsi1013d-bench`

`fnirsi1013d-bench [--port COM10] [--census 2000] [--n 200]` runs the firmware's
`firmware/scope/host_tests/bench_link.py` from the venv: `#RESET`, `#VER`, GET_INFO, PING latency, TEST_PATTERN
sizes up to 64 KiB, throughput, then the no-retry acceptance census (random 1..200 B PINGs, every E_BAD_CRC diffed
against the request with GET_BAD_FRAME, plus 100 x 64 KiB) and GET_LINK_STATS. LOCAL-safe; `--census 0` skips the
census. Exit status 0 only when every check passed. It needs the repository checkout (the script is not part of
the package).

`fnirsi1013d-bench --fft [--reps 3]` instead runs FFT_BENCH (firmware >= 0.2.27, protocol 1.6) through the
package's own transport and prints the table `firmware/scope/host_tests/fft_bench.py` prints: per N and memory
(DDR, on-chip SRAM) the plan init, run min/max and dB conversion times in ms, the peak bin and its level in dB re
1 code RMS, plus the CP15 cache bits and the device uptime before/after. The scope's UI pass waits for the run
(about 0.15 s at 3 reps); a scope menu open makes it fail with E_BUSY. Needs no repository checkout.

## Tests

```
cd mcp
.venv/Scripts/python -m pytest -q          # Windows
~/.venvs/fnirsi1013d-mcp/bin/python -m pytest -q -p no:cacheprovider   # WSL
```

The suite covers the protocol vectors, transport framing, retries and resync against the simulator, simulator
semantics, every MCP tool end-to-end with the server in-process, the server as a stdio subprocess, decay fits
recovering a known tau, and averaging reducing noise by about sqrt(n).

Shared vectors: `python -m fnirsi1013d.vectors --check` checks the Python parsers against
`docs/protocol-vectors.json`. `--merge` adds the host vectors (`host_*`, `"source": "host"`) without touching the
firmware's. The firmware's C checker (`firmware/scope/host_tests/check_vectors.py`) runs on the union.

## Spec points: firmware and simulator agree

docs/protocol.md section 14 records how firmware v0.1.0 fills the gaps of sections 1-13. The simulator follows it,
and `tests/test_firmware_match.py` pins the host-visible items:

- short request payloads are zero-extended; E_BAD_LEN only for frames over 512 bytes (then `len + 4` bytes are
  skipped) and malformed DEBUG_FPGA op lists; longer payloads are accepted, the tail ignored;
- error `detail`: the mask bit number for SET_CHANNEL, SET_TRIGGER and SET_GEN (0xFF = channel field), the byte
  offset of the field for every other command;
- only OK responses with a payload of at most 256 bytes are replayed from the duplicate cache;
- WAIT in IDLE, ABORTED or DONE answers at once; a WAIT retransmitted with the pending seq is ignored and its
  answer follows once; any other non-control request during a deferred one gets E_BUSY; STOP answers before the
  aborted WAIT;
- ARM while ARMED or TRIGGERED gives E_STATE (STOP first);
- GET_STATUS FPGA flag bits are 0 in LOCAL;
- KEEP_SETTINGS: a host release uses the release request's flag, every other exit the acquire-time flag;
- TEST_PATTERN 3: state = length, one xorshift32 step (13/17/5) before every 4-byte group, emitted little-endian;
- stock bitstream: GET_GEN reports enable 1, 1 kHz, 50 %; `min_total_samples = max_total_samples = 3000`;
- SET_CHANNEL: the resulting offset PWM must stay within 0..2000 or E_BAD_ARG (detail 3); the server checks this
  first and says how many divisions are reachable;
- READ_TRACE in REMOTE needs a DONE capture; `sw_trigger_index` is -1 unless 20 <= pretrigger and
  pretrigger + 64 <= total;
- ASCII lines count as traffic for the watchdog; at most 63 characters after the `#`, longer gives one overflow
  and `#ERR,UNKNOWN`;
- USB identity: 0x1209:0x0001, product `FNIRSI 1013D`, serial `1013D-0001` (GET_INFO `serial` is the same).
- protocol 1.1 (firmware 0.1.1): GET_INFO `proto_minor` 1; GET_STATUS is 32 bytes with u16 `pass_ms` and
  `pass_max_ms` (main-loop period; a 1.0 device's 28-byte body parses with both 0); ENTER_FEL on every known
  bitstream (AL3 caps 0x1C03); `#RESET` drops a frame in flight, a pending WAIT (no answer), the duplicate cache
  and partial input, keeps REMOTE, answers `#OK`; DTR 0 -> 1 flushes an old frame; a response with no progress for
  2 s is dropped.

Choices made only on the host or in the simulator, not covered by section 14:

1. **FORCE** (debug builds) is modelled as an auto-timeout completion: `trigger_addr = pretrigger_words`, flag
   `auto_timeout`, `fpga_triggered` 0. Test T7 decides what the hardware does.
2. **Auto-timeout data** sits contiguous in the ring with `capture_start_word = 0`.
3. **trigger_addr** is the word whose core-A sample is the first one past the level; the host puts t = 0 at
   interleaved index `2 x pretrigger_words`. Test T5 measures the real offset (`--trigger-align-offset`).
4. **SET_TRIGGER** with only one of pretrigger/total requested: the host sends both, filling the other from
   GET_TRIGGER, so the firmware always validates a consistent pair.
5. **Overload flags** in GET_STATUS come from the last READ_RAW: a code of 0 or 255 on either core of a channel.
6. **ACQUIRE_AVG** (not in v0.1.0): `count = 0` means to the end; values are mean compensated code x 256 clamped to
   16 bits; `n_dropped` counts timeouts and, with REQUIRE_TRIGGER, auto-timeout captures.
7. **READ_SCREEN** (not in v0.1.0): at most 41 rows fit one response; the host asks for 40 or fewer.
8. **Host side**: response frames with `proto_major != 1` are ignored; responses larger than `--max-response` are
   never requested.
9. **Stale first byte (T3)**: the simulator's DEBUG_FPGA model returns, on the first strobe after a 0x1F write,
   the byte at `ptr - hw_read_lag` off by +1, then post-increments. One hypothesis only; the host relies on
   nothing but READ_RAW returning exactly `start_word .. start_word + count - 1`.

To make the simulator report what the real firmware reports, use `--caps 0x1C03` for 0.1.1 or `--caps 0x1803`
for 0.1.0 (AL3, no ACQUIRE_AVG, READ_SCREEN, READ_LIVE, FORCE or DEBUG_FPGA).

## Link robustness

- **On connect** the transport sends 512 zero bytes, a newline and `#RESET` (firmware 0.1.1; 0.1.0 answers
  `#ERR,UNKNOWN`), then PINGs until the matching answer arrives. This completes and invalidates any partial frame or
  line an aborted session left in the device's RX ring. Serial ports are opened with DTR low and then raised, so
  0.1.1 also flushes a frame left over from the previous connection.
- **Corrupted requests**: an `E_BAD_CRC` or `E_BAD_LEN` answer means the device saw a damaged request and executed
  nothing; the same frame (same seq) is resent, for normal and deferred requests (WAIT, ACQUIRE_AVG).
- **Lost answers**: resent with the same seq, so the device's duplicate cache answers without executing twice;
  after repeated losses the recovery procedure (pause, flush, preamble, PING) runs.
- **Timeouts per command class** (time before the same frame is resent): 100 ms for PING, GET_INFO,
  GET_STATUS, GET_BATTERY and the text lines (answered from the firmware's 1 ms timer interrupt since 0.1.2);
  main-loop commands 300 ms in REMOTE and 1 s in LOCAL (a UI pass); bulk reads (READ_RAW, READ_TRACE,
  READ_SCREEN, TEST_PATTERN) 0.5 s / 1.5 s; WAIT and ACQUIRE_AVG use their own deadline. The transport tracks
  REMOTE from status-shaped answers. `Transport(response_timeout=...)` overrides all classes.

## Windows notes

- The serial link sets every port parameter once at open and never again. On Windows each pyserial setting change
  (even `timeout`) calls SetCommState, and usbser.sys then sends CDC line-coding control requests. On firmware 0.1.0
  their 8 setup bytes (`a1 21 00 00 00 00 07 00`) appeared inside in-flight bulk responses; the firmware agent
  traced this to the inherited USB FIFO layout (EP2 TX FIFO outside the 2 KiB FIFO RAM, aliased onto RX), fixed in
  0.1.1. Reads ask only for `in_waiting` bytes. `tests/test_serial_link.py` guards both rules.
- The server imports scipy and pyserial before the stdio transport starts (`preload_native_modules`): loading a DLL
  later from the event loop deadlocks on Windows against the stdio reader thread's pending read on stdin.
  `tests/test_stdio.py` guards this.

## Library notes

`Scope.info` is a property (the cached GET_INFO; `Scope.identify()` re-reads it). `Capture.t` is seconds relative
to the trigger and `ChannelData.volts` is volts at the probe tip.
