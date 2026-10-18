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
`pretrigger_words`); stock FPGA with only part of the 4096-word ring fresh; LOCAL/REMOTE with snapshot and
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
| `scope_status` | 0 | LOCAL/REMOTE, acquisition state, capture id, battery, exit reason, link statistics |
| `scope_get_config` | 0 | channels, timebase, trigger and generator in physical units |
| `scope_read_calibration` | 0 | offset centres, gains, A/B compensation, volts per code per range |
| `scope_screenshot` | 0 | PNG of the 800x480 screen |
| `scope_fetch_waveform` | 0 (LOCAL) / 1 | current capture or the UI's last trace, without re-arming |
| `scope_measure` | 0 / 1 with `capture=true` | vpp, vmin, vmax, mean, rms, ac_rms, frequency, period, duty, rise/fall time, CH1->CH2 delay |
| `scope_remote_session` | 1 | acquire or release REMOTE ownership |
| `scope_set_timebase` | 1 | sample rate, window or raw divider |
| `scope_set_trigger` | 1 | source, edge, level (volts or code), mode, pre-trigger, total samples, auto timeout |
| `scope_capture` | 1 | arm, wait, read; statistics, file, inline samples or preview |
| `scope_acquisition_control` | 1 | arm / wait / force / stop |
| `scope_averaged_capture` | 1 | n captures averaged, hardware or edge alignment, on-device averaging when available |
| `scope_capture_and_fft` | 1 | averaged amplitude spectrum, peaks in dBV and dBc, noise floor, THD |
| `scope_decay_fit` | 1 | edge-aligned average, then `exp`, `exp+offset`, `exp2` or `power` fit after the TX edge; tau, R^2, sample points |
| `scope_set_channel` | 2 | range, coupling, offset, probe, invert; relay changes limited to 5 per second |
| `scope_generator` | 3 | calibration-pin square wave; confirmation token to enable |
| `scope_self_test_loopback` | 3 | generator into CH1: frequency, duty, levels, bench tests T4, T5, T7; token |
| `scope_raw_fpga` | 4 | raw FPGA bus operations (DEBUG_RAW builds); needs `FNIRSI_MCP_ENABLE_RAW=1` and a token |
| `scope_enter_fel` | 4 | reboot into FEL; needs `FNIRSI_MCP_ENABLE_RAW=1` and a token |

Every tier 1+ tool takes REMOTE ownership when needed and says so in `caveats`. If the person at the scope taps
the banner, the next call fails with "scope released by user". Confirmation tokens are bound to the exact
arguments, single-use, and expire after 120 s. Waveforms of any length go to `.npz` (`t`, volts, codes, clip
flags, `meta_json`); results up to 2000 points also carry the samples inline, longer ones a 500-point min/max
preview.

Physical units: volts are `(code_compensated - zero_code) x volts_per_lsb x probe` with the firmware's
`uv_per_lsb_q8` and `zero_code_q8`. That formula is bench test T8 and is flagged in every result. Time is
`(i - pretrigger_samples) x dt` relative to the hardware trigger, which is +-2 samples until T5 is run.

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

## Spec points resolved here

These are the choices the host and the simulator make where docs/protocol.md leaves room. The firmware should
match them or the spec should be amended:

1. **Oversize request** (valid header, `len` > 492): `E_BAD_LEN` right after the header, then `len + 4` bytes
   are skipped. The firmware parser already does this.
2. **Request length**: shorter than the v1.0 struct gives `E_BAD_LEN`; longer is accepted and the tail ignored.
   PING and DEBUG_FPGA are variable length.
3. **Deferred requests**: a WAIT retransmitted with the same seq while still pending is ignored and its response
   comes once. Any other non-control request while a deferred one is pending gets `E_BUSY`.
4. **WAIT outside ARMED/TRIGGERED** (IDLE, ABORTED, DONE) returns the status body at once with status OK.
5. **STOP during a deferred request**: the STOP response comes first, then the deferred request's
   `E_ABORTED`; `acq_state` becomes ABORTED.
6. **Duplicate cache**: every completed response whose encoded frame is at most 256 bytes, completed
   deferred responses included.
7. **FORCE**: completes like an auto timeout: `trigger_addr = pretrigger_words`, flag `auto_timeout`,
   `fpga_triggered` 0.
8. **Auto-timeout data**: contiguous in the ring with `capture_start_word = 0`, unsynchronised.
9. **trigger_addr**: the word whose core-A sample is the first one past the level. The host puts t = 0 at
   interleaved index `2 x pretrigger_words`, the core-B sample of that word.
10. **READ_TRACE in REMOTE before any DONE capture**: `E_STATE`. `count` is clipped to the end;
    `start >= sample_count` gives `E_BAD_ARG`.
11. **TEST_PATTERN 3**: low byte of the xorshift32 state after each step (`x ^= x << 13; x ^= x >> 17;
    x ^= x << 5`), seed = `length`, seed 0 replaced by 1.
12. **Stock GET_INFO**: `min_total_samples = max_total_samples = 3000`, since the geometry is fixed.
13. **SET_TRIGGER with only one of pretrigger/total in the mask**: the host always sends both, filling the
    other from GET_TRIGGER, so the firmware validates a consistent pair.
14. **SET_CHANNEL offset**: `offset_mode 1` maps to `dcoffset = pwm - center_cal[range]`. `dcoffset` is clamped
    so the PWM stays within 0..2000.
15. **Overload flags** in GET_STATUS come from the last READ_RAW: a code of 0 or 255 on either core of the
    channel.
16. **ACQUIRE_AVG**: `count = 0` means to the end of the capture. Values are the mean compensated code x 256,
    clamped to 0..65535. `n_dropped` counts timeouts and, with REQUIRE_TRIGGER, auto-timeout captures.
17. **ASCII lines**: the 63-character limit excludes the `#`. Event text has no `#`. A longer line gives one
    overflow and `#ERR,UNKNOWN`. This matches the firmware's `PC_ASCII_MAX_LINE`.
18. **READ_SCREEN**: at most 41 rows per request fit 66,560 bytes. The host reads 40.
19. **Host side**: response frames with `proto_major != 1` are ignored.
20. **Stale first byte (T3)**: the simulator's DEBUG_FPGA model returns, on the first strobe after a 0x1F
    write, the byte at `ptr - hw_read_lag` off by +1, then post-increments. That is one hypothesis. The
    firmware's pointer arithmetic for the dummy read must follow whatever T3 shows. The host contract is only
    that READ_RAW returns exactly `start_word .. start_word + count - 1`.

## Windows note

The server imports scipy and pyserial before the stdio transport starts (`preload_native_modules`). Loading a
DLL later from the event loop deadlocks on Windows against the stdio reader thread's pending read on stdin.
`tests/test_stdio.py` guards this.
