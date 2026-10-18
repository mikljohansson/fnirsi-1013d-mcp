# mcp/ handoff for agents (mcp-gen3, 2026-10-18)

Python host library, device simulator, MCP server and bench CLIs for the FNIRSI 1013D open firmware's USB protocol.
Read first: `../AGENTS.md` (repo rules, bench coordination), `../docs/protocol.md` (wire spec; **section 14 is how
the firmware actually behaves**, rows 30-48 are protocol 1.3-1.10), `../docs/mcp-tools.md` (tool design),
`README.md` (user-facing install, tools, CLIs, output formats), `../docs/agent-measurement-guide.md` (how agents
should measure; skill `../.claude/skills/fnirsi-scope/`). Do not duplicate those here; fix them instead.

## Working rules (learned the hard way)

- **COM10 is exclusive and belongs to the lead.** Never open it, not even for LOCAL-safe reads, without an explicit
  "port free" from the lead in a message after you asked. (mcp-gen2 once opened it "while free" during a bench
  calibration; it happened not to collide.) Develop and test against the simulator.
- **Stage files by name** (`git add mcp/src/... mcp/tests/...`), never `git add mcp`: other agents edit files in the
  tree concurrently, and a directory add sweeps their uncommitted work into your commit (happened once, 4f17414).
- Pushing to main is allowed for mcp work and CI fixes (no tags). Use `git pull --rebase --autostash` before
  pushing (others' uncommitted firmware edits sit in the tree), then confirm CI with `gh run watch <id>`.
- **CI** (`.github/workflows/`): ruff + pytest on ubuntu-24.04 and windows-latest, py3.12 and 3.13, plus firmware
  and vector jobs. Keep it green; tests must not depend on timing margins (see the deferred-cancel fix 617ab34).
- Write patch scripts with the Write tool and run them; bash heredocs in this harness mangle backslashes
  (`"\n"` becomes a newline). Files are LF; on Windows write with `write_bytes(... .encode())`.
- **Venv reinstall gotcha**: `uv pip install --python .venv -e ".[dev]"` fails ("Access denied") while any
  `fnirsi1013d-*.exe` is running (the owner's MCP session, the lead's bench), and uv may already have removed
  other .exe files. Rerun when nothing runs; meanwhile `python -m fnirsi1013d.<module>` works for every CLI.
  As of this handoff `fnirsi1013d-soak.exe` was not yet created (use `python -m fnirsi1013d.soak`).

## Layout (`src/fnirsi1013d/`)

| Module | Role |
|---|---|
| `protocol.py` | Pure wire format (no numpy): frames, `FrameParser`, `Cmd`/`Status`/`Caps`/`Control`/`RunState`/`CaptureOrigin`/`ExtFlags`/`UiBusy`, payload dataclasses (tolerant unpack), CaptureInfo, SD structs, eGON checksum, GET_LINK_STATS names by count (`link_stats_names`), GET_EVENT_LOG (`parse_event_log`, `DeviceEvent.describe`, reset snapshot), restart record names, scaling helpers, DEBUG_FPGA codec, READ_SPECTRUM / FFT_BENCH codecs, decimation maths (`decim_config` etc.,
a mirror of `firmware/scope/pc_decim.c`, checked against the `decim` vectors), `DecimState`, CaptureInfo 76. |
| `transport.py` | `TcpLink`, `SerialLink`, discovery, `Transport` (seq, same-seq retries, deferred WAIT/ACQUIRE_AVG with control requests, resync, recovery, timeout classes incl. 2.5 s bulk on 1.3, first-answer-wins inbox, no resend after a cancelled deferred). |
| `scope.py` | `Scope` client: one method per command; `capture` (host single), `read_capture` (latest capture of any origin with its GET_CAPTURE_INFO settings), `read_trace_capture`, `capture_context`, `set_acq_state`, `ensure_control`/`end_control` + `legacy_*` (1.2 model), `scale()` (per-core volts via calibration), `sd_*`, `event_log(rearm=)`, `link_stats()`, `read_spectrum()` (`Spectrum`), `fft_bench()`, `get_decim()` /
`set_decim()` (keeps the flags by default); decimated captures (`Capture.decim`, `ChannelData.samples16`) through
`_read_capture16` (READ_RAW layout 2) and `read_trace(wide=True)`; `CaptureContext.decim` keeps the fast host-capture
path. |
| `calibration.py` | Per-unit, per-core volts model (T8): `UnitCalibration` (points, least-squares zero / 1/gain / PWM slope per channel-range-core, channel model `zero = Z + slope x dcoffset`), `channel_scale`, T8 importer CLI. |
| `analysis.py` | Stats, frequency, edges, Welford averager, FFT, decay fits, sample points, `scope_fft_db` (float model of the on-scope FFT), `compare_spectra`, `decim_noise_n` / `decim_bits`. |
| `simulator.py` | `Device` (protocol state machine, FPGA ring, shared mode, legacy mode, SD card, EXT trigger, event log) + `SimServer` (TCP) with test hooks. CLI `fnirsi1013d-sim`. |
| `server.py` | FastMCP server `fnirsi1013d-mcp`, 26 tools (list with tiers in README.md). |
| `flash.py` | `fnirsi1013d-flash`: ENTER_FEL (3 attempts, FEL-device check between), usbipd, sunxi-fel RAM load, `#VER`; `--persist` / `--persist-only` / `--restore` (SD sector 750 via the firmware). |
| `sdcard.py` | SD access (`SdCard`: checked reads, verified backups, staged 32-sector writes, compare), `plan_write`, `check_image`, `image_version`. |
| `sdcli.py` | `fnirsi1013d-sd` info / read / write (`--dry-run`, `--yes` + typed confirmation, backup first). |
| `soak.py` | `fnirsi1013d-soak`: long timestamped poll, drops, reopen, counters, event log, restart detection. |
| `bench.py` | `fnirsi1013d-bench`: wraps `../firmware/scope/host_tests/bench_link.py` (fw-gen2's), plus uptime before/after; `--fft` runs FFT_BENCH (`Scope.fft_bench`) itself. |
| `vectors.py` | Host vectors in `../docs/protocol-vectors.json` (`--check`, `--merge`). |
| `units.py` | `parse_quantity`, `fmt_si`. |

## State of each tool / CLI

- **`fnirsi1013d-mcp`** (server): collaborative mode (shared settings, last change wins, host captures are singles
  that leave the scope in STOP, `scope_acquisition_control(action='run')` resumes; long runs pin a settings_seq and
  restart on a change, `max_restarts` 5); legacy exclusive model on firmware without SHARED_MODE (implicit REMOTE,
  idle release after 60 s keeping settings). Tiers 0-4, tokens for 3/4, relay rate limit, audit log, `.npz` per
  waveform. Read-only extras: `scope_calibration_info`, `scope_sd_info`, `scope_event_log`, `scope_spectrum`
  (with `cross_check`; the capture tools' caveat while the screen shows the spectrum view comes from
  `Session.ensure_control`). No SD write tool by
  design. Verified on the unit by the owner on fw 0.1.7 (capture/measure); collaborative mode, EXT and SD on
  hardware are the lead's bench items.
- **`fnirsi1013d-flash`**: FEL dev loop verified on the unit. `--persist-only` verified on fw 0.2.7 (5.3 s total,
  write 111 KiB/s with 32-sector SD_WRITEs, no resends). Resume after a USB drop, version check vs `#VER`
  (`--force`), backups in `../backups/sdcard/`, failures exit 7 with a partial-image warning.
- **`fnirsi1013d-bench`**: bench_link census through the venv; prints device uptime before/after and flags
  `DEVICE RESTARTED`. `--fft [--reps n]`: FFT_BENCH table (same columns as host_tests/fft_bench.py), simulator
  only so far.
- **`fnirsi1013d-sd`**: bench SD access; writes need `--yes` + typed `WRITE <n>@<sector>`, back up first.
- **`fnirsi1013d-sim`**: `--variant stock|al3|ef2|ep4|al3x|al3d` (al3d = 0x1534 with the decimation model:
  sub-sampled boxcar^K of the noise-free core-mean code plus output noise with the exact boxcar variance, seeded
  per word block; 120 ns pipeline latency; EXT latency shift), `--legacy`, `--ext`, `--ui-period`, `--caps`, ...
- **`fnirsi1013d-soak`**: `--mode status|ping|capture`, `--duration` (30 min), `--interval` (0.1 s),
  `--reopen-timeout` (60 s), `--log`. Logs host facts (USB selective suspend via powercfg, hub path), uptime at
  open/reopen/end (and every status poll), drops with Windows error codes, port gone/back, reopen; after a reopen
  the first requests are GET_EVENT_LOG (plain read; a frozen pre-reset snapshot is printed and saved, then re-armed
  with flags = 1) and GET_LINK_STATS; on a restart it reads the restart record (counters 49-56, raw hex) and the
  event log. Summary: requests, failures, drops, device restarts, longest gap, latency, counter deltas
  (incl. usb_irq_polled). Exit 0 clean / 1 failure, drop or restart / 2 cannot start. Tested on the simulator
  only; the lead runs it on the bench.

## Protocol support (host = simulator = vectors)

| Protocol | Firmware | What the host uses |
|---|---|---|
| 1.0-1.2 | 0.1.x | exclusive LOCAL/REMOTE model (fallback when GET_INFO lacks SHARED_MODE), GET_LINK_STATS 27 |
| 1.3 | 0.2.0+ | SHARED_MODE (caps bit 13), GET_STATUS 44 bytes (settings_seq, capture_settings_seq, run_state, capture_origin), SET_ACQ_STATE 0x0208 (control request), GET_CAPTURE_INFO 0x0209, SEQ_INVALID, READ_RAW hold, READ_TRACE clip (0.2.1+), SD commands 0x0500-0x0503 (0.2.2+, cap bit 14) |
| 1.4 | 0.2.9+ | EXT trigger (source 2 with caps bit 3 on FPGA 0x1533+), GET_STATUS byte 42 ext_flags, GET_CAPTURE_INFO flag bit 5; selecting EXT switches the generator off, SET_GEN enable under EXT = E_STATE |
| 1.5 | 0.2.12+ | GET_EVENT_LOG 0x0009 (fast path; 64 then 128 entries, reset-snapshot tail from 0.2.15, request flags byte bit 0 = re-arm from 0.2.18, bus-level ring tail from 0.2.24) |
| 1.6 | 0.2.27+ | FFT_BENCH 0x0F02 (diagnostic; `Scope.fft_bench`, 5 s timeout, `fnirsi1013d-bench --fft`) |
| 1.7 | 0.2.28+ | spectrum view: GET_STATUS byte 43 display_mode, READ_SPECTRUM 0x020A (bulk timeout class; 32-byte header, i16 centi-dBV) |
| 1.10 | 0.2.43+ | calibration v2: GET_CAL 248 bytes (`CalResp.v2`, `zero_at`, `parse_adc_cal_block` vs the `adc_cal` vectors); `channel_scale` puts the v2 zeros first (class from `P.cal_clock_class`, unit file gains only), READ_RAW / WIDE corrected on the host, READ_TRACE already corrected; `decim_to_code` takes corr16 (1/16 code) |
| 1.9 | 0.2.30+ | decimation filter (caps bit 15 DECIM on FPGA 0x1534): GET_DECIM 0x020C / SET_DECIM 0x020D, GET_TRIGGER words = samples and GET_TIMEBASE period N x 5 ns while on, READ_TRACE flag bit 2 WIDE (header bytes 30/31 = mode, S), READ_RAW layout 2, GET_CAPTURE_INFO 76 bytes. 1.8 was skipped |

GET_LINK_STATS counts (named by the count field): 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3),
35 (0.2.4), 41 (0.2.5-0.2.6), 47 (0.2.7-0.2.11), 48 (0.2.12-0.2.16, usb_irq_polled), 49 (0.2.17-0.2.18,
boot_splash_ms), 57 (0.2.19, restart record 49-56). Names match `bench_link.py`. The payload is u16 count, u16 0,
then the counters (word 0 is not a counter). Since 0.2.21 n = 58 with 47 and 57 retired (always 0; the soak
tool reports them only when they move, i.e. on older firmware). Event types 1-19 decoded (15 = RESTART, 0.2.19;
16 / 19 USB state at the start / end of init, 17 / 18 EP2 state at a TX abort; row 43).

## Transport rules (from hardware; keep them)

1. Never reconfigure the serial port after open (Windows usbser sends CDC line-coding requests on every
   SetCommState); fixed 20 ms timeout; `tests/test_serial_link.py` guards it.
2. Reads are `in_waiting`-driven.
3. DTR opened low, raised after open (the 0->1 edge flushes a stale TX frame); DTR changes via EscapeCommFunction.
   Each open/close costs about three EP0 class requests (SET_LINE_CODING, SET_CONTROL_LINE_STATE x2, +1 at close).
4. Resync on connect: 512 x 0x00 + `#RESET` + PING; recovery = pause, flush, preamble, PING.
5. Retries resend the same seq (timeouts, E_BAD_CRC/E_BAD_LEN); the device's duplicate cache answers completed
   OK requests (<= 256 B). Never resend a cancelled deferred request (errors are not cached). First answer per seq
   wins.
6. Timeout classes: fast 100 ms (PING, GET_INFO, GET_STATUS, GET_BATTERY, GET_LINK_STATS, GET_EVENT_LOG), main loop
   300 ms REMOTE / 1 s otherwise, bulk 0.5 / 1.5 s and 2.5 s on 1.3; SD_WRITE 5 s with 4 same-seq resends.
7. Host partial-frame timeout 2 s. One request in flight; control requests only from the deferred callback.
8. No response cap by default (the 8 KiB Windows cap was dropped after the 0.1.6/0.1.7 census).
9. Server imports scipy/pyserial before `mcp.run` (Windows stdio deadlock otherwise).

## Calibration file

Per unit at `<data_dir>/cal/<serial>.json` (default data dir `mcp/data`, gitignored), loaded at connect,
`--cal-file` / `FNIRSI_MCP_CAL_FILE` override, written by `scope_calibrate_volts`, shown by
`scope_calibration_info`. The owner's `mcp/data/cal/1013D-0001.json` (69 points) is rebuilt with
`python -m fnirsi1013d.calibration --unit 1013D-0001 --out data/cal/1013D-0001.json --import
../backups/t8_CH1_0.010V.json ../backups/t8_CH1_0.308V.json ../backups/t8_ch1_1.000V.json
../backups/t8off_CH1_0.000V.json ../backups/t8off_CH2_0.000V.json` (delete it first; `--import` appends). Facts
(T8, fw 0.1.7): gains within 1.5 % of uv_per_lsb on every range; slope -0.264 (A) / -0.260 (B) codes per PWM count,
range-independent; 0 V at dcoffset 0: CH1 A 132.47 / B 127.14, CH2 A 128.80 / B 129.19 (CH2 has 0 V points only, so
firmware gains). The firmware formula assumes 128 on the compensated mean. Grounds: the probe ground is VRF1,
+1.24 V over the USB ground (fpga/al3/README.md); the calibration output reads -1.24 / +1.9 V against it by design.

## Tests (`tests/`, 296, about 50 s)

`test_protocol` (codec, structs, vectors, link stats/event log), `test_transport` (incl. the scripted-link deferred
cancel test), `test_robustness`, `test_serial_link`, `test_simulator_scope`, `test_shared_mode` (1.3),
`test_firmware_match` (section 14 items), `test_server` (every tool in-process, restarts, legacy fallback),
`test_stdio` (server subprocess), `test_analysis`, `test_calibration`, `test_flash` (FEL flow, bench wrapper),
`test_sdcard` (SD commands, persist, resume after drops, restore, CLI), `test_ext_trigger`, `test_soak`,
`test_cal_v2` (1.10 GET_CAL, block vectors, 0 V on both cores per clock class, no unit zeros on top),
`test_spectrum` (1.7 spectrum view, event log 0.2.22-0.2.25 additions, FFT_BENCH), `test_decim` (1.9: maths vs
the firmware vectors, wire layouts, simulator noise / volts / trigger / EXT latency, `scope_set_decimation`).
Fixtures: `sim`/`transport`/`scope` are collaborative with UI captures on demand (`ui_capture_period_s=0`);
`legacy_sim` / `sim_factory(legacy=True)` give the 1.2 model. Simulator hooks: `SimServer.corrupt_next`,
`garbage_next`, `drop_next`, `bad_crc_next`, `lose_next`/`lose_cmd`, `drop_link_on`; `Device.ui_set_*`,
`ui_press_run/stop`, `ui_run_capture`, `ui_set_spectrum` (display mode, window, FFT length, channel), `on_arm`,
`user_tap` (legacy), `log_event` (also feeds the bus-level ring), `self_restart`, `ui_set_decim`, `sd.*` faults.
SimConfig `cal_v2` (a bound v2 block measured from the model) and `gated_zero_shift` (per-core zero moves while the
encode clock is gated). Known flake: `test_sdcard::test_slow_write_answers_are_retried_without_reopening` (0.2 s write
timeout) can fail once under a loaded full run; it passes alone.
Run: `.venv/Scripts/python -m pytest -q`; WSL: `wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/mcp &&
~/.venvs/fnirsi1013d-mcp/bin/python -m pytest -q -p no:cacheprovider"`; lint: `.venv/Scripts/ruff check --select
F,E9,B023 --line-length 120 src tests`; vectors: `python -m fnirsi1013d.vectors --check` (the C checker:
`make -C ../firmware/scope/host_tests test` in WSL).

## Protocol vectors contract

`../docs/protocol-vectors.json` is shared; fw-gen2's `gen_vectors.py` keeps entries whose `source` is not
`firmware`. Host entries are `host_*` from `vectors.build_host_vectors()` (19, incl. five 1.3 ones); add new ones
there, `--merge --check`, never change an existing vector's bytes.

## Open items (priority order)

Pending work for every agent lives in `../docs/plans/task-queue.md` (section "MCP / host"); the coordinator dispatches
from there and moves items to "Dispatched". Add host items you discover there, not only here. Agent-to-agent
messages describe work; only the coordinator puts it in your queue.

0. **Protocol 1.11 (fw-gen6, 0.2.44, row 49)**: 0x1535 FIR enable (SET_DECIM flags bit 3), caps bit 16, FIR gain
   (N/2)^K in the 16-bit conversion; after the lead reports the merge.
1. **Release 0.3.0** (on hold): bump `version` in `pyproject.toml` (still 0.1.0) with the lead's release; the 30-minute soak
   (`fnirsi1013d-soak --duration 1800`) is the gate; fold the baseline soak results (requests, drops, restarts,
   latency, counter deltas, the selective-suspend setting) into README "Link soak" once the lead has them.
2. **EXT trigger on hardware**: pending the 0x1533 bitstream on the unit; check the CH1-vs-EXT offset (the
   simulator models only the 22 ns FPGA path; the AD9288 pipeline adds more) and the ext_flags fields.
3. **USB drops / self-restarts**: help the lead read soak logs (pre-reset snapshots: ep0_setup 0x20/0x22 before a
   suspend+bus_reset points at a host control request; a bare suspend at selective suspend; restart records map
   with the ELF).
4. Collaborative mode bench acceptance (`../docs/plans/collaborative-mode.md`), incl. per-request cost in shared
   mode for averaging.
5. CH2 known-voltage calibration points (gains); then T8 is complete.
6. Metal-detector tools (sweep, lock-in amplitude/phase, PI target comparison, ground balance, monitor).
7. Adopt ACQUIRE_AVG / READ_SCREEN / READ_LIVE when the firmware ships them (simulator has them).
8. GET_BAD_FRAME (0x0008) decode and dump on E_BAD_CRC (the cmd id is in `Cmd`, no client method yet).
