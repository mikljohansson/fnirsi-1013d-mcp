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

Python 3.12 or newer. With [uv](https://docs.astral.sh/uv/):

```
cd mcp
uv venv .venv
uv pip install --python .venv -e ".[dev]"
```

Plain venv works too: `python -m venv .venv` then `.venv/Scripts/pip install -e ".[dev]"` (Windows) or
`.venv/bin/pip install -e ".[dev]"` (Linux/WSL). In WSL keep the venv on the Linux side
(e.g. `~/.venvs/fnirsi1013d-mcp`), because `mcp/.venv` is the Windows one.

The MCP SDK is pinned to `mcp>=1.12,<2`: mcp 2.x renamed FastMCP to MCPServer and changed its API.

## Simulator

```
fnirsi1013d-sim --variant al3 --port 5555 --signal decay:tau=80us
```

| Option | Meaning |
|---|---|
| `--variant stock\|al3\|ef2\|ep4` | FPGA version word 0x1432 / 0x1532 / 0x1632 / 0x1732 with its ring size, memory limit, fixed or programmable geometry, generator |
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
| `FNIRSI_MCP_DATA_DIR` | `mcp/data` | `.npz` output, one folder per day |
| `FNIRSI_MCP_AUDIT_LOG` | `mcp/logs/audit.jsonl` | JSONL log of every tier 1+ call (empty string disables) |
| `FNIRSI_MCP_LEGACY_IDLE_RELEASE_S` | 60 | legacy firmware only: release REMOTE (keeping the host settings) after this many seconds without a tool call; 0 = hold until exit |
| `FNIRSI_MCP_CAL_FILE` | `<data dir>/cal/<serial>.json` | per-unit voltage calibration (`--cal-file`), loaded at connect if present, written by `scope_calibrate_volts` |
| `FNIRSI_MCP_MAX_RESPONSE` | device limit | largest response payload requested at once (`--max-response`); READ_RAW, READ_TRACE and READ_SCREEN are split, and device averaging falls back to the host when its result would not fit |

### Claude Code

The repository's [`.mcp.json`](../.mcp.json) registers `fnirsi1013d-sim`, the server with an in-process AL3
simulator producing an 80 us PI decay on CH1 and the TX gate on CH2 (Windows venv path). To add servers by hand:

```
claude mcp add fnirsi1013d-sim -- D:/src/fnirsi-1013d-ai/mcp/.venv/Scripts/fnirsi1013d-mcp.exe --sim "--variant al3 --signal decay:tau=80us"
claude mcp add fnirsi1013d -e FNIRSI_MCP_MAX_TIER=3 -- D:/src/fnirsi-1013d-ai/mcp/.venv/Scripts/fnirsi1013d-mcp.exe --port COM5
```

In WSL use the Linux venv, e.g. `~/.venvs/fnirsi1013d-mcp/bin/fnirsi1013d-mcp --port /dev/ttyACM0`.

### Tools

Names follow the mcp-builder convention (`scope_` prefix, snake_case); each maps 1:1 to the tool of the same
name in docs/mcp-tools.md section 4 (`set_channel` there is `scope_set_channel` here, and so on).

| Tool | Tier | Does |
|---|---|---|
| `scope_info` | 0 | firmware, protocol, FPGA variant, serial, capabilities, memory limits |
| `scope_profile` | 0 | static capability profile, live values, unverified facts, standing caveats |
| `scope_status` | 0 | mode, run_state (RUN/STOP/SINGLE), settings_seq, latest capture id and origin, host capture state, battery, link statistics |
| `scope_get_config` | 0 | channels, timebase, trigger and generator in physical units |
| `scope_read_calibration` | 0 | offset centres, gains, A/B compensation, volts per code per range |
| `scope_screenshot` | 0 | PNG of the 800x480 screen |
| `scope_fetch_waveform` | 0 | the latest capture (what the screen shows) with the settings it was taken under, without capturing |
| `scope_measure` | 0 / 1 with `capture=true` | vpp, vmin, vmax, mean, rms, ac_rms, frequency, period, duty, rise/fall time, CH1->CH2 delay |
| `scope_set_timebase` | 1 | sample rate, window or raw divider |
| `scope_set_trigger` | 1 | source, edge, level (volts or code), mode, pre-trigger, total samples, auto timeout |
| `scope_capture` | 1 | host single capture (the screen shows it, the scope stays in STOP); statistics, file, inline samples or preview |
| `scope_acquisition_control` | 1 | arm / wait / force / stop / run (RUN/STOP shared with the scope's button) |
| `scope_averaged_capture` | 1 | n captures averaged, hardware or edge alignment, on-device averaging when available; restarts when a setting is changed at the scope |
| `scope_capture_and_fft` | 1 | averaged amplitude spectrum, peaks in dBV and dBc, noise floor, THD |
| `scope_decay_fit` | 1 | edge-aligned average, then `exp`, `exp+offset`, `exp2` or `power` fit after the TX edge; tau, R^2, sample points |
| `scope_set_channel` | 2 | range, coupling, offset, probe, invert; relay changes limited to 5 per second |
| `scope_calibration_info` | 0 | scaling in effect per channel, calibration file and its fitted zero / slope / gain per range and core |
| `scope_calibrate_volts` | 2 | a known DC voltage on every range: fits 0 V code, volts per code and offset slope per range and ADC core, saves the calibration file |
| `scope_generator` | 3 | calibration-pin square wave; confirmation token to enable |
| `scope_self_test_loopback` | 3 | generator into CH1: frequency, duty, swing (the output is not ground-referenced), bench tests T4, T5, T7; token |
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
flags, `meta_json`); results up to 2000 points also carry the samples inline, longer ones a 500-point min/max
preview.

Physical units: volts are converted per ADC core. With a per-unit calibration covering the range,
`raw_code = zero + slope x (offset_pwm - pwm_ref) + volts / gain` per core (fitted by `scope_calibrate_volts`,
or imported from bench data with `python -m fnirsi1013d.calibration --import <t8 json...> --out <file>`); otherwise
the firmware's `uv_per_lsb_q8` and `zero_code_q8` (the same for both cores, after the stored compensation).
Results say which one was used (`scaling`). T8 on the owner's unit (fw 0.1.7): gains within ~1 %, but the
firmware zero was ~2 codes off (cores 5 codes apart, compensation corrects 4) and follows the trace offset with a
different slope. Time is
`(i - pretrigger_samples) x dt` relative to the hardware trigger, which is +-2 samples until T5 is run.

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

## Bench link check: `fnirsi1013d-bench`

`fnirsi1013d-bench [--port COM10] [--census 2000] [--n 200]` runs the firmware's
`firmware/scope/host_tests/bench_link.py` from the venv: `#RESET`, `#VER`, GET_INFO, PING latency, TEST_PATTERN
sizes up to 64 KiB, throughput, then the no-retry acceptance census (random 1..200 B PINGs, every E_BAD_CRC diffed
against the request with GET_BAD_FRAME, plus 100 x 64 KiB) and GET_LINK_STATS. LOCAL-safe; `--census 0` skips the
census. Exit status 0 only when every check passed. It needs the repository checkout (the script is not part of
the package).

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
