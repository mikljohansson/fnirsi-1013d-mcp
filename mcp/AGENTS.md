# mcp/ handoff for agents

State on 2026-10-18: Python host library, device simulator and MCP server for the FNIRSI 1013D open firmware's
USB protocol. Firmware on the owner's unit is 0.1.4 (protocol 1.2). This file is for a fresh agent with no
context. Read first: `../CLAUDE.md` (repo rules, bench coordination), `../docs/protocol.md` (wire spec;
**section 14 is how the firmware actually behaves**), `../docs/mcp-tools.md` (tool surface, tiers),
`README.md` (user-facing install and usage). Do not duplicate those here; fix them instead.

## Layout

| Module (`src/fnirsi1013d/`) | Role |
|---|---|
| `protocol.py` | Pure wire format: frame encode/decode, `FrameParser` (resync, oversize skip, ASCII lines and overflow), `Cmd`/`Status`/`Caps` enums, payload dataclasses (tolerant unpack: longer payloads ignore the tail, shorter ones zero-fill), TEST_PATTERN generator, GET_LINK_STATS names, scaling (`uv_per_lsb_q8`, `zero_code_q8`), DEBUG_FPGA op codec. Shared by host, simulator and tests. |
| `transport.py` | Links (`TcpLink`, `SerialLink`), discovery (`list_candidates`, `#VER` probe), request engine `Transport` (seq, retries, deferred WAIT/ACQUIRE_AVG, control requests, resync, recovery, timeout classes, `LinkStats`). |
| `scope.py` | `Scope` client: one method per command plus `capture`, `read_capture` (READ_RAW + host compensation + volts), `read_trace_capture`, `acquire_avg_device`; chunked reads under `max_response`; `Capture`/`ChannelData` with numpy arrays, `t` in s relative to trigger, `volts` at the probe tip. |
| `analysis.py` | Stats, crossings, frequency/duty, rise/fall, delay, preview, Welford `Averager`, fractional shift, edge finding, FFT (windows, peaks, THD), decay fits (`exp`, `exp+offset`, `exp2`, `power`), clip blanking, sample points. |
| `simulator.py` | `Device` (protocol state machine + FPGA/ring model) and `SimServer` (TCP, one host at a time, test hooks). CLI `fnirsi1013d-sim`. |
| `server.py` | FastMCP server (`fnirsi1013d-mcp`): `Session` (connection, lock, tokens, relay limit, audit, .npz), 21 tools, `--sim` in-process simulator, `preload_native_modules`. |
| `flash.py` | `fnirsi1013d-flash`: ENTER_FEL (3 fresh attempts, 1 s apart; `#VER` check if all unanswered) -> usbipd -> sunxi-fel RAM load -> wait for `#VER`. Any failure: one `FAILED:` line, non-zero exit. |
| `bench.py` | `fnirsi1013d-bench`: runs `../firmware/scope/host_tests/bench_link.py` (link checks + census; `--fel` not exposed). |
| `vectors.py` | Checks/extends `../docs/protocol-vectors.json` (`--check`, `--merge`). |
| `units.py` | `parse_quantity("100us")`, `fmt_si`. |

Tests in `tests/` (162): `test_protocol` (codec, structs, shared vectors), `test_transport`, `test_robustness`
(resend, resync, #RESET, 1.1/1.2 fields, timeouts, paused responses), `test_serial_link` (no-reconfigure rule,
fake pyserial), `test_simulator_scope`, `test_firmware_match` (protocol.md section 14 items), `test_server` (every
tool in-process), `test_stdio` (server as a real subprocess), `test_analysis`, `test_flash`.

## Setup and running

```
cd mcp
uv venv .venv && uv pip install --python .venv -e ".[dev]"        # Windows venv lives in mcp/.venv
.venv/Scripts/python -m pytest -q                                   # ~18 s
wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/mcp && ~/.venvs/fnirsi1013d-mcp/bin/python -m pytest -q -p no:cacheprovider"
.venv/Scripts/fnirsi1013d-sim --variant al3 --port 5555 --signal decay:tau=80us
.venv/Scripts/fnirsi1013d-mcp --port tcp://localhost:5555           # or --sim "<sim args>", or --port COM10
.venv/Scripts/fnirsi1013d-flash --image <bin> --sha256 <hash>       # reload: only with the lead's ack
.venv/Scripts/fnirsi1013d-bench --port COM10 --census 2000          # LOCAL-safe link census
.venv/Scripts/ruff check --select F,E9,B023 --line-length 120 src tests
```

- WSL needs its own venv (`~/.venvs/fnirsi1013d-mcp`, created with `uv venv` + `uv pip install -e .[dev]` from
  `/mnt/d/...`); never let a Linux tool touch `mcp/.venv`.
- Repo-root `../.mcp.json` registers `fnirsi1013d-sim` (server with in-process AL3 simulator, PI decay on CH1,
  TX gate on CH2). For the real scope: `claude mcp add fnirsi1013d -- <venv>/Scripts/fnirsi1013d-mcp.exe --port COM10`.
- MCP SDK pinned `mcp>=1.12,<2` (2.x renamed FastMCP). Files are LF; Python on Windows writes CRLF in text mode,
  so write with `write_bytes(... .encode())` or `newline="\n"`. Bash heredocs in this harness sometimes mangle
  backslashes and quotes: write patch scripts with the Write tool and run them.

## Transport rules (learned on hardware; keep them)

1. **Never reconfigure the serial port after open.** On Windows every pyserial setting change (even `timeout`)
   calls SetCommState and usbser.sys sends CDC line-coding control requests. On firmware 0.1.0 their setup bytes
   leaked into in-flight bulk responses (FIFO aliasing, fixed in firmware). `SerialLink` uses a fixed 20 ms
   timeout; `tests/test_serial_link.py` guards it.
2. **Reads are `in_waiting`-driven** (or `read(1)` with the fixed timeout). One large pending `read(65536)`
   sometimes never completed on Windows.
3. **DTR**: opened low, raised after open; firmware >= 0.1.1 flushes a stale TX frame on DTR 0->1. DTR drop
   releases REMOTE (exit_reason 4) unless IGNORE_DTR. DTR changes use EscapeCommFunction, not SetCommState.
4. **Resync on connect** (`Transport.resync`): 512 x 0x00 + `\n#RESET\n` + PING until the matching answer;
   stale answers are dropped by seq. Recovery (`recover`) = pause 150 ms, flush, same preamble, PING.
5. **Retries resend the same frame (same seq)**: on timeout, and on `E_BAD_CRC`/`E_BAD_LEN` answers (device saw a
   corrupted request; nothing executed), for normal and deferred requests. Idempotency comes from the device's
   duplicate cache (only OK responses <= 256 B; a retried READ_RAW simply re-executes, it is read-only).
6. **Timeout classes** (`TimeoutPolicy`): 100 ms for PING/GET_INFO/GET_STATUS/GET_BATTERY/GET_LINK_STATS and
   text lines (timer-IRQ answered); main-loop commands 300 ms REMOTE / 1 s LOCAL; bulk reads 0.5 / 1.5 s; WAIT
   has its own deadline. REMOTE is tracked from status-shaped answers. `response_timeout=` overrides.
7. **Host partial-frame timeout is 2 s**, not the device's 100 ms (a paused large response must survive).
8. One request in flight; only control requests (GET_STATUS, STOP, SET_REMOTE, PING) while a deferred one is
   pending, and only from the `deferred(cancel=...)` callback (single reader).
9. **Windows response cap**: `Scope.connect` defaults `max_response` to 8192 on Windows serial ports
   (`WINDOWS_SERIAL_MAX_RESPONSE`); 0 = device limit. Provisional, see TODO 1.
10. Server: import DLL-loading modules (scipy, pyserial) before `mcp.run` (Windows stdio deadlock otherwise).

## Protocol vectors contract

`../docs/protocol-vectors.json` is shared with the firmware. Schema is the firmware's: per vector `hex` (byte
stream, then silence), `frames` (direction-agnostic valid proto-1 frames), `device_events` (frame / bad_crc /
bad_len / bad_version / ascii / ascii_overflow). The firmware's `firmware/scope/host_tests/gen_vectors.py`
regenerates its own entries and keeps every entry whose `source` is not `firmware`. Host entries are `host_*`
with `"source": "host"`, built by `vectors.build_host_vectors()`; add new ones there, run
`python -m fnirsi1013d.vectors --merge --check`, and never change an existing vector's bytes (pin fields, e.g.
`proto_minor=0`). The C checker: `wsl ... cd firmware/scope/host_tests && make && python3 check_vectors.py
--checker build/vector_check`. Both checkers pass on the current 65 vectors.

## Simulator fidelity

Mirrors: variants stock/al3/ef2/ep4 (version word, ring, max samples, fixed vs programmable geometry, generator,
stock fresh-window), the ring (4 cores, core B offset +3/-2 codes compensated by GET_CAL), trigger on core A
with 2-dt quantisation, pretrigger arming, modular capture_start, auto timeout, FORCE (as auto timeout),
read_lag (`--hw-read-lag`/`--fw-read-lag`), A/B order (`--true-order`), trigger alignment offset, LOCAL/REMOTE
with snapshot/restore and every exit reason, deferred WAIT/ACQUIRE_AVG, duplicate cache, all of protocol.md
section 14 (zero-extended requests, detail numbering, ARM-while-ARMED, LOCAL flags 0, KEEP_SETTINGS rules,
TEST_PATTERN 3, stock GET_GEN, offset PWM range, #RESET even inside a partial frame), protocol 1.1 timing fields
(constant 5 ms) and 1.2 GET_LINK_STATS (counts what a TCP model can see). `--caps` mimics a build (0x1C03 =
fw 0.1.1+ on AL3, 0x1803 = 0.1.0), `--fail-responses-over` drops large responses, `--time-scale` (0 = instant).
Test hooks on `SimServer`: `corrupt_next`, `garbage_next`, `drop_next` (responses), `bad_crc_next`, `lose_next`
(+ `lose_cmd` filter: requests lost before execution); `Device.user_tap()`.

Limits: no USB timing or packet loss model (TCP), no relay transients, AC coupling = subtract the signal's
declared DC, front-end bandwidth not modelled, `checkfirstadc` rails logic approximated, READ_SCREEN draws a
simple synthetic screen, DEBUG_FPGA stale-byte model is a hypothesis (T3), noise is white Gaussian.
Signals: `sine`, `square`, `decay` (PI: period, width, tau, amp, tau2/amp2, flyback), `gate`, `dc`, `gen`
(calibration-pin loopback), `zero`; CH2 defaults to the matching gate / sync square.

## MCP tools and safety

21 tools, `scope_` prefix, list with tiers in `README.md`. Tiers: 0 read-only, 1 acquisition, 2 relays
(`scope_set_channel`, 5 changes/s), 3 calibration-pin output (`scope_generator`, `scope_self_test_loopback`),
4 raw (`scope_raw_fpga`, `scope_enter_fel`, only with `FNIRSI_MCP_ENABLE_RAW=1`). `FNIRSI_MCP_MAX_TIER` limits
registration. Tier 3/4: a call without `confirm_token` returns `{confirmation_required, token, summary}`; the
token is bound to a hash of the exact arguments, single-use, 120 s. Disabling the generator needs no token.
Tier 1+ calls auto-acquire REMOTE and say so in `caveats`; a user tap makes the next call fail with "scope
released by user". Every tier 1+ call goes to the JSONL audit log; waveforms go to `.npz` under the data dir.

## Analysis assumptions

- Volts: `(code + comp - zero_code) x volts_per_lsb x probe` with firmware `uv_per_lsb_q8` / `zero_code_q8`
  (bench test T8 pending). Time: `(i - pretrigger_samples) x dt`, t = 0 at the core-B sample of the trigger
  word (T5 pending, +-2 samples).
- `scope_averaged_capture`: host averaging (Welford mean/std, clipped-any mask) or device ACQUIRE_AVG (only
  hardware alignment, no clip rejection, must fit one response). Edge alignment: per capture the crossing of
  `align_channel` at `align_level` nearest the trigger (+-12 samples), linear interpolation, then a fractional
  shift; removes the 2-sample trigger quantisation.
- `scope_capture_and_fft`: coherent-gain-corrected RMS amplitude spectrum, power averaging, ENBW reported,
  peaks with parabolic interpolation, THD over harmonics 2..5.
- `scope_decay_fit`: edge-aligned average, fit window `t_start..t_end` after the edge, samples within
  `blank_after_clip` of any clipped sample excluded, optional baseline window fixes the offset; scipy
  `curve_fit` with log-linear initial guesses; `exp2` orders tau < tau2; `ok=false` if tau is implausible.
  Recovers 80 us within 3 % on the simulator and over stdio.

## Bench coordination (see `../CLAUDE.md`)

COM10 is exclusive: if open fails with PermissionError someone else (the lead, the owner) is using it; wait
(`waitport`-style retry loop), never kill their process. Allowed whenever the port is free: LOCAL-safe reads
(PING, TEST_PATTERN, GET_*, `#VER`). SET_REMOTE and anything hardware-changing only when the lead asks. ENTER_FEL /
reload: announce "about to reload with <image> for <reason>", wait for the lead's ack, report after with
`#VER`; pin the image with `--sha256` (the build file can be rebuilt in place by the firmware agent).

## Known issues and current numbers (COM10, Windows, this transport)

- fw 0.1.4 LOCAL, retries off, 304 requests: 0 bad CRC, 0 garbage; 4 requests (1.3 %) unanswered, one per
  ~2048 bytes sent host->device (looks like the device RX ring wrap; reported to the firmware agent). With
  retries on: 0 failures.
- `fnirsi1013d-flash` verified on the unit 2026-10-18 (pinned fw 0.1.4 image from `backups/`): ENTER_FEL
  0.08 s, FEL device 9-3 at 1.3 s, attach 2.7 s, load 3.1-5.0 s, COM10 back with `#VER` at 6.6 s.
- PING ~1 ms (timer IRQ, fw >= 0.1.2); TEST_PATTERN 64 KiB ~65-72 ms LOCAL; lead measured 2.35 MB/s REMOTE,
  full-depth capture arm+wait+read 110 ms median.
- Lead's census on 0.1.4 (random 1..200 B payloads): ~4 % E_BAD_CRC + ~0.5 % no response, consistent with
  the request straddling the 2 KiB RX ring wrap being corrupted.
- Lead's census on 0.1.3: ~4 % PING E_BAD_CRC (request corrupted on arrival, firmware RX path) and
  garbage_bytes from the old 100 ms host partial drop (fixed in 20b4d8b).
- Firmware 0.1.x has no ACQUIRE_AVG, READ_SCREEN, READ_LIVE; FORCE/DEBUG_FPGA only in PC_DEBUG builds. The
  server fails closed on missing caps. Firmware also has GET_BAD_FRAME diagnostics (c31bd01), not used here.

## TODO (priority order)

1. Drop the Windows 8 KiB cap (`WINDOWS_SERIAL_MAX_RESPONSE`, README "Windows notes") once the firmware RX fix
   lands and the lead confirms a clean census at 20460 / 32748 / 65536 x20.
2. fw 0.1.6 (697e1f6) fixes the 2 KiB RX ring-wrap loss (packets straddling the ring end were read from the
   FIFO in two access runs). Re-run the 304-request LOCAL repro (PING x100 + TEST_PATTERN 495/4096/20460/32748/
   65536 x40, retries=0): expect zero lost requests. Add GET_BAD_FRAME (0x0008, protocol 1.2, item 29: u32
   bad_count, u32 rx_bytes, u16 len, u16 0, last bad-CRC request as received) to protocol.py / Scope /
   simulator / FAST_CMDS and dump it on any E_BAD_CRC. Then: re-run the retries=0 census after each firmware RX fix; include GET_LINK_STATS before/after
   (`Scope.link_stats()`) and GET_STATUS `pass_ms` in failure reports to the firmware agent.
3. `--persist` / settings commands (SD sector writes) when the firmware adds them: tier 2+, confirmation token,
   never touch calibration (protocol.md: no SET_CAL in v1).
4. Metal-detector tools: a sweep tool (generator frequency or duty x averaged capture, one file), VLF amplitude/
   phase at the TX frequency via lock-in on the averaged capture (CH1 vs CH2 reference), PI target comparison
   (decay_fit with/without target, delta tau and sample-point deltas), ground-balance helper (exp2/power on a
   reference capture), a continuous-monitor tool with drift statistics.
5. Bench tests T3/T4/T5/T7/T8 helpers using DEBUG_FPGA builds; feed the results back into `read_lag`,
   `interleave_order`, the trigger alignment and the scaling caveats.
6. Adopt ACQUIRE_AVG / READ_SCREEN / READ_LIVE on hardware when the firmware ships them (simulator already
   implements them; check payload layouts against the firmware first).
7. MCP progress for long averaged runs works in-process; verify with Claude Code over stdio.
