# mcp/ handoff for agents (mcp-gen6, 2026-10-19: protocol 1.19-1.20, reports; mcp-gen5 retired after 1.17)

Python host library, device simulator, MCP server and bench CLIs for the FNIRSI 1013D open firmware's USB protocol.
Read first: `../AGENTS.md` (repo rules, bench coordination), `../docs/protocol.md` (wire spec; **section 14 is how
the firmware actually behaves**, rows 30-52 are protocol 1.3-1.13), `../docs/mcp-tools.md` (tool design),
`README.md` (user-facing install, tools, CLIs, output formats), `../docs/agent-measurement-guide.md` (how agents
should measure; skill `../.claude/skills/fnirsi-scope/`). Do not duplicate those here; fix them instead.

## Working rules (learned the hard way)

- **COM10 is exclusive and belongs to the lead.** Never open it, not even for LOCAL-safe reads, without an explicit
  "port free" from the lead in a message after you asked. (mcp-gen2 once opened it "while free" during a bench
  calibration; it happened not to collide.) Develop and test against the simulator.
- **Stage files by name** (`git add mcp/src/... mcp/tests/...`), never `git add mcp`: other agents edit files in the
  tree concurrently, and a directory add sweeps their uncommitted work into your commit (happened once, 4f17414).
- Pushing to main is allowed for mcp work and CI fixes (no tags). This checkout's main is shared with the
  coordinator, who merges branches locally: **never `git pull --rebase`** (it flattens their merge commits and
  conflicts); update with `git fetch && git merge --ff-only origin/main` (`git pull --ff-only` still rebases here because
  `pull.rebase` is set) and tell the lead if it refuses. When local main holds other agents' unpushed
  commits under yours, ask the lead before pushing. Leave others' uncommitted files alone. Before a push run ruff,
  the mcp suite and `make -C firmware/scope/host_tests test` (WSL), then confirm CI with `gh run watch <id>`.
- **Report and wait.** Messages from other agents (fw-*, fpga-*) describe work; only the lead queues it. Queue
  items you discover in `../docs/plans/task-queue.md` ("MCP / host"). After each item send the lead the commit hash,
  the test count and the CI run id, and add a CHANGELOG line (Unreleased, host section) citing the commit.
- **CI** (`.github/workflows/`): ruff + pytest on ubuntu-24.04 and windows-latest, py3.12 and 3.13, plus firmware
  and vector jobs. Keep it green; tests must not depend on timing margins (see the deferred-cancel fix 617ab34).
- Write patch scripts with the Write tool and run them; bash heredocs in this harness mangle backslashes
  (`"\n"` becomes a newline), and a heredoc whose text holds triple quotes or apostrophes can fail as a whole ("unexpected
  EOF"). Files are LF except `../docs/plans/task-queue.md` (CRLF: keep it, or the commit rewrites every line); on Windows write with `write_bytes(... .encode())`. Windows Python does not resolve
  `/c/Users/...` paths: pass `C:/Users/...`. The session scratchpad is shared with other agents: prefix your files.
- **Worktrees have no venv**: an agent worktree (`.claude/worktrees/...`) needs its own `uv venv mcp/.venv` and
  `uv pip install --python mcp/.venv -e "mcp[dev]"` (gitignored); the tracked `.mcp.json` is gone (owner's local file,
  `.mcp.json.example` is the template), so worktrees have no MCP servers either.
- Writing patch scripts: never pass the environment on a command line or print it (it holds API keys); the wheel
  smoke script hands the env to its client through the process environment for that reason.
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
| `server.py` | FastMCP server `fnirsi1013d-mcp`, 30 tools up to tier 3, 32 with tier 4 (`scope_report`, `scope_cal_probe` added) (list with tiers in README.md); `--list-tools` prints them with the data dir and audit log without opening a port. |
| `paths.py` | Per-user home (`FNIRSI_MCP_HOME`, default `~/.fnirsi1013d`: `data/`, `logs/audit.jsonl`) and the checkout detection (`checkout()`, `checkout_path()` only when the file exists, `checkout_dir()`): the CLIs default to repository files only when run from a checkout, else the argument is required. |
| `flash.py` | `fnirsi1013d-flash`: ENTER_FEL (3 attempts, FEL-device check between), usbipd, sunxi-fel RAM load, `#VER`; `--persist` / `--persist-only` / `--restore` (SD sector 750 via the firmware). |
| `sdcard.py` | SD access (`SdCard`: checked reads, verified backups, staged 32-sector writes, compare), `plan_write`, `check_image`, `image_version`. |
| `sdcli.py` | `fnirsi1013d-sd` info / read / write (`--dry-run`, `--yes` + typed confirmation, backup first). |
| `soak.py` | `fnirsi1013d-soak`: long timestamped poll, drops, reopen, counters, event log, restart detection. |
| `bench.py` | `fnirsi1013d-bench`: wraps `../firmware/scope/host_tests/bench_link.py` (fw-gen2's), plus uptime before/after; `--fft` runs FFT_BENCH (`Scope.fft_bench`) itself. |
| `vectors.py` | Host vectors in `../docs/protocol-vectors.json` (`--check`, `--merge`). |
| `units.py` | `parse_quantity`, `fmt_si`. |
| `report.py` | Result presentation: `load_result` (any `.npz` the tools save, or a Bode `.json`; kind from `meta_json["kind"]` / the arrays), `build_report` -> PNG (matplotlib, optional extra `plot`, in `dev`) + one self-contained HTML page (base64 PNG, table, summary, caveats, command; no scripts / external URLs, tested), `latest_result`, CLI `python -m fnirsi1013d.report`. Tool `scope_report` (tier 0) writes under `<home>/reports/<date>/`. |
| `bode.py` | Host-driven Bode sweep (docs/plans/bode-design.md H1-H3): `plan_point` / `plan_sweep` (M, N, p / q, record, settle; `plan_vectors` = `../docs/bode-plan-vectors.json`), `tone` (rect / Hann), `residual_noise`, `measure_pair` (H, SNR, leakage, half-record check), `markers`, `PointResult` / `SweepResult` (table, CSV, JSON, arrays), `ThruCal`, `BodeRunner` (drives a `Scope`: pre-scan, centring, background, CH2 auto-range with the splice, E_BUSY, restore), `result_caveats`, CLI `fnirsi1013d-bode`. Device sweep (1.20): `device_sweep` (BODE_CTRL, READ_BODE poll, rows -> `SweepResult`), `result_from_device`, `point_from_row`, `device_settle_code` / `device_ch2_code`. |

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
- **`scope_bode_sweep` / `fnirsi1013d-bode`** (mcp-gen4): the host Bode sweep, README "Bode sweep" and guide 3b. Tier 3
  with a token, refuses without DECIM / generator, above FNIRSI_MCP_GEN_MAX_HZ and above `max_duration_s`; thru
  calibrations in `<data_dir>/bode/thru/<name>.json`; the first sweep of a session returns `connection` (wiring).
  Simulator only so far (accuracy there: 0.02 dB / 0.2 deg to 1 MHz with auto-range splices); the bench procedure is
  below. Design deviations, on purpose: a Hann window where the 32-bit increment rounding breaks the coherence (below
  ~50 Hz, design 1.2 assumed rect was enough); the -3 dB marker is relative to the higher end of the sweep (or the
  peak), so an RC swept from 1 kHz reads 1.0075 fc (10.02 kHz for the bench RC, 9.92 kHz with the probe's ~100 pF);
  CH1 is ranged once by the pre-scan (no re-ranging when CH1 falls, flag `ch1_low` instead); background captures are
  per (N, CH2 range), lazily, not once per N; the 50 ms relay settle is a host sleep after every range or offset move.
  `mode` (harmonic fast mode) and `z_series_ohm` are not implemented (task queue "Bode follow-ups"); the PNG is
  `scope_report`. **`where='device'`** (mcp-gen6, protocol 1.20): `bode.device_sweep`; same result structure plus
  `device`; presets only, ch2 auto / fixed, DC; thru 'measure' / 'device' / a stored name (host-applied);
  `close_view`. Rows carry no SNR / leak / codes (NaN, `snr_db` / `ch2_codes` null in the table). Simulator only.
- **`scope_report`** (mcp-gen6): see the layout table; README "Reports", guide 7a, skill "Presenting results". The
  Bode `.npz` meta records `kind`, `args` and the `.json` path (the two files have different timestamps).
- **Packaging** (mcp-gen5): the wheel works outside a checkout; `.github/scripts/wheel_smoke.py` (CI job `wheel`, Linux and
  Windows) builds it, installs it into a fresh venv in a temp dir, runs `--list-tools` and an MCP stdio session on
  `--sim` with a capture, and checks that files land under `FNIRSI_MCP_HOME` and nothing inside the venv. The server's
  data dir / audit log default to `~/.fnirsi1013d`; the owner's local `.mcp.json` sets `FNIRSI_MCP_DATA_DIR=mcp/data`
  (calibration file there). `vectors --file`, `fnirsi1013d-flash --image / --backup-dir`, `fnirsi1013d-sd write
  --backup-dir`, `fnirsi1013d-bench --bench-link` are required outside a checkout and refuse with a message.
- **Sequencer** (protocol 1.17, mcp-gen5): `scope_capture_segments` and `scope_averaged_capture(hardware=true)` (opt-in until
  the unit agrees with READ_RAW; task queue "1.17 on the unit, host side"). Both check the fit with GET_SEQ's geometry
  first, end a run with STOP at the timeout (the run keeps what it has), and always clear the setting afterwards so later
  plain ARMs are captures. Simulator only so far.
- **`fnirsi1013d-sim`**: `--variant stock|al3|ef2|ep4|al3x|al3d|al3f|al3s`, `--legacy`, `--ext`, `--ui-period`,
  `--caps`, ... al3x = 0x1533 (EXT trigger); al3d = 0x1534 (EXT + decimation: sub-sampled boxcar^K of the noise-free
  core-mean code plus output noise with the exact kernel variance, seeded per word block, 16-bit ring layout,
  16-bit trigger level, 120 ns latency, EXT latency shift; no device averaging); al3f = 0x1535 (al3d plus the FIR:
  CIC on N / 2 convolved with the 15 RTL taps from `fpga/al3/src/zaklad.v`, 250 ns latency). Signals `dut_in` /
  `dut_out` (mcp-gen4): input and output node of an RC or series RLC on the generator through rs = 1 kOhm, against
  the probe ground, with a 30 MHz front end; `gain` / `delay` model a probe mismatch (`--ch1 dut_in:r=1.6k,c=10n
  --ch2 dut_out:r=1.6k,c=10n`). Captures below 24 kHz (N >= 128, 64 sub-samples per CIC period) cost about 0.5 s
  each in the simulator; tests sweep above 24 kHz where they can.
- **`fnirsi1013d-soak`**: `--mode status|ping|capture|reopen` (reopen: `--rounds` of soak `--duration`, close,
  `--idle` s closed, reopen; after each reopen the events (types 1-4 with the 0.2.44 suspend fields), the counter
  deltas, counters 36 / 37 and the selective-suspend state: the fw 0.2.44 suspend bench, firmware/AGENTS.md 22), `--duration` (30 min), `--interval` (0.1 s),
  `--reopen-timeout` (60 s), `--log`. Logs host facts (USB selective suspend via powercfg, hub path), uptime at
  open/reopen/end (and every status poll), drops with Windows error codes, port gone/back, reopen; after a reopen
  the first requests are GET_EVENT_LOG (plain read; a frozen pre-reset snapshot is printed and saved, then re-armed
  with flags = 1) and GET_LINK_STATS; on a restart it reads the restart record (counters 49-56, raw hex) and the
  event log. Summary: requests, failures, drops, device restarts, longest gap, latency, counter deltas
  (incl. usb_irq_polled). Exit 0 clean / 1 failure, drop or restart / 2 cannot start. Tested on the simulator
  only; the lead runs it on the bench.

## Protocol support (host = simulator = vectors): 1.21, fw 0.2.67+

`P.PROTO_MINOR` follows the highest section 14 row implemented. The simulator always speaks the newest protocol
(shared mode); `--legacy` gives 1.2.

| Protocol | Firmware | What the host uses |
|---|---|---|
| 1.0-1.2 | 0.1.x | exclusive LOCAL/REMOTE model (fallback when GET_INFO lacks SHARED_MODE), GET_LINK_STATS 27 |
| 1.3 | 0.2.0+ | SHARED_MODE (caps bit 13), GET_STATUS 44 bytes (settings_seq, capture_settings_seq, run_state, capture_origin), SET_ACQ_STATE 0x0208 (control request), GET_CAPTURE_INFO 0x0209, SEQ_INVALID, READ_RAW hold, READ_TRACE clip (0.2.1+), SD commands 0x0500-0x0503 (0.2.2+, cap bit 14) |
| 1.4 | 0.2.9+ | EXT trigger (source 2 with caps bit 3 on FPGA 0x1533+), GET_STATUS byte 42 ext_flags, GET_CAPTURE_INFO flag bit 5; selecting EXT switches the generator off, SET_GEN enable under EXT = E_STATE |
| 1.5 | 0.2.12+ | GET_EVENT_LOG 0x0009 (fast path; 64 then 128 entries, reset-snapshot tail from 0.2.15, request flags byte bit 0 = re-arm from 0.2.18, bus-level ring tail from 0.2.24); event types 1-19 decoded, types 1-4 with the 0.2.44 suspend fields |
| 1.6 | 0.2.27+ | FFT_BENCH 0x0F02 (diagnostic; `Scope.fft_bench`, 5 s timeout, `fnirsi1013d-bench --fft`; 6 entries since 0.2.31) |
| 1.7 | 0.2.28+ | spectrum view: GET_STATUS byte 43 display_mode, READ_SPECTRUM 0x020A (bulk timeout class; 32-byte header, i16 centi-dBV); `scope_spectrum` with `cross_check` |
| 1.9 | 0.2.30+ | decimation filter (caps bit 15 DECIM, FPGA 0x1534): GET_DECIM 0x020C / SET_DECIM 0x020D, GET_TRIGGER words = samples and GET_TIMEBASE period N x 5 ns while on, READ_TRACE flag bit 2 WIDE (header bytes 30/31 = mode, S), READ_RAW layout 2, GET_CAPTURE_INFO 76 bytes; `decim_config` mirrors `pc_decim.c` (all `decim` vectors). 1.8 was skipped |
| 1.10-1.11 | 0.2.43-0.2.44 | calibration v2: no longer decoded (the volts model raises on `cal_version` 2) |
| 1.11 | 0.2.44+ | 0x1535 FIR: caps bit 16 FIR, SET_DECIM / GET_DECIM flags bit 3, 0x29 bits 6 / 5 (`DECIM_FIR_EN` / `DECIM_FIR_ACT`), gain (N/2)^K and S + 1 while active (`decim_config(..., has=DECIM_HAS_FIR)`, `DecimConfig.from_status`), filter names `cic2+fir` (`parse_decim_filter`) |
| 1.12 | 0.2.45+ | calibration v3, GET_CAL 256 bytes: `CalResp.v3`, `zero16_at` / `zero_at` (firmware rounding), per-class `slope_codes`, `gain_corr` / `gain_q16`, `gain16_full`, the next readout's `gain_a/b`, `off_a/b`, `gain16`; `parse_adc_cal_block` decodes the v3 block (`adc_cal_v3` vectors). The `adc_cal` (v2) vectors section is unused by mcp; fw-gen7 removes it |
| 1.13 | 0.2.46+ | GET_STATUS display_mode 2 waterfall / 3 density (spectrum sub-views; READ_SPECTRUM works) / 4 phosphor; capture tools add the spectrum or phosphor caveat (`Session.ensure_control`). The host decodes no settings words (210 included) |
| 1.14 | 0.2.49+ | GET_LINK_STATS n = 62 (60 `usb_inferred_resumes`, 61 `usb_soft_reconnects`), event type 20 `usb_soft_disconnect` (reason, back-off level, suspends; fw-usb2, f3763cc) |
| 1.15 | 0.2.50+ | calibration v4 (row 54): GET_CAL 288 bytes, `CalResp.track_*` / `cm_*`, `has_tracker` (by length; 256 / 248 / 72 still parse), `TRACK_*` flags; the volts model takes the tracked d, e (8-bit full class) and cm (full class, 8-bit and 16-bit) out; conversions re-read GET_CAL when the cached answer is over `CAL_TRACK_AGE_S` (1 s) old; `scope_calibration_info` `tracking` (state, values, `in_words`) |
| 1.16 | 0.2.52+ | row 55: GET_HEALTH 0x000A (`HealthResp`, 96 bytes, fast timeout class, allowed while modal; `describe()`; flags CLEAR_RESTART / RESTART_TEST), SD_INFO flag IDENT (`SdInfo.ident` = `SdIdent`, 112 bytes; the scope caches it until a write reaches sector 750), GET_CAL 356 bytes (`prov_state`, `prov`, `provenance()` -> `Provenance`, power-ons / minutes since), SAVE_REPORT 0x0504 (`SaveReportResp`, 15 s timeout); stored layouts `parse/pack_life_record`, `parse/pack_provenance`, `parse/pack_ident_record` = the `diag_layouts` vectors; tools `scope_health`, `scope_save_report`, `scope_sd_info` `card_image`, `scope_calibration_info` `calibration_provenance`. Simulator: `Device.power_on`, `minutes_stored`, `restart_record`, `cal_prov` / `cal_prov_bad`, `ident_cache`, `reports`; `make_egon_image(ident=)`; `SIM_BUILD` / `SIM_BUILD_TIME` = the running build |
| 1.18 | 0.2.61+ | row 57: link counters 62-63 (`adc_fullrate_bad` / `_recovered`), event types 21 / 22 named (decoded in words since 1.19 work) |
| 1.19 | 0.2.62+ | row 58 (FPGA 0x1538): `Caps.ENC_CHECK` (18), `Features.ENC_CHECK` (5), `HealthResp` 104 bytes (`enc_*`, `has_enc_check` by length, `describe_enc`, `ENC33_PINS`), event 21 (reason / re-arms / caller / value) and 22 in words; `Session.enc_notes` caveat on every tool that calls `ensure_control`. Simulator `al3e`; `_enc_check` per full-rate capture, hooks `enc_bad_next` / `enc_unrecovered_next` / `enc_rearms` |
| 1.20 | 0.2.66+ | row 60: `Cmd.BODE_CTRL` / `READ_BODE` (cap DECIM), `BodeCtrlReq.make`, `BodeHeader`, `BodeCtrlResp`, `ReadBodeReq`, `BodeRow` (`f_hz` from M, `flag_names` = `BODE_ROW_FLAGS` = the host names in bit order), `parse_read_bode`, `DisplayMode.BODE`, `Features.BODE` (6). `Scope.bode_ctrl` / `read_bode`. Simulator `SimBode`: the host `BodeRunner` in a thread over `_LoopLink` (`Device.internal_request`: no duplicate cache, WAIT blocks in place; `_internal` marks the engine's ARMs so a host ARM ends the sweep, reason 5); the view snapshots `settings` + run state at open and restores at close; thru kept as a `ThruCal`, stale by (start, stop, points); estimate = host estimate / 2.5 |
| 1.21 | 0.2.67+ | row 61 (provisional numbering): CAL_PROBE 0x0402 (no cap bit): `CalProbeReq` (= vector `req_cal_probe`), `CalProbeResp` 252 bytes (`describe`, `ACAL_RESULTS`, `CAL_PROBE_POINTS`, flags `CALP_*`), `Scope.cal_probe`; tool `scope_cal_probe` (tier 2: stops the UI, RUN again after). Simulator `_cal_probe_range`: the wizard's eight points on the front-end model, CLIP / NOISE checks, `cal_probe_fail` = (point, rc) hook, E_STATE 0 / 1 and E_BAD_ARG 0-4 |
| 1.17 | 0.2.58+ | row 56 (FPGA 0x1536, caps SEQ): GET_STATUS 48 bytes (`features` = `Features` flags, `ui_average` 0 off / 1 software / 2 hardware, `ui_average_log2n`), GET_DECIM 36 (same 4 bytes), GET_CAPTURE_INFO 96 (`CaptureInfo.run` = `SeqRunInfo`, `is_run`), SET_SEQ / GET_SEQ (`SetSeqReq.make`, `SeqState` incl. fit, next geometry, FPGA status, held run), READ_SEQ (`ReadSeqReq`, `ReadSeqHeader`; table of u64 `SeqEntry`, averaged record as u16 lanes / u16 or u32 sums), READ_RAW `ADDR_SEGMENT` (3); maths `seq_fit` / `seq_max_count` / `seq_holdoff_units` / `seq_cmd57` / `seq_slots` / `seq_seg_word` / `seq_dead_ns` / `seq_ui_plan` / `seq_avg_code` = all 73 `seq` vector cases. Client: `get_seq`, `set_seq`, `read_seq_table`, `read_seq_average`, `read_segment`, `capture_segments` -> `SegmentRun`, `capture_hw_average` -> `AveragedRun` (`SeqError` with the fit code). Simulator `al3s`: runs modelled in a background thread on one signal timeline (`simulate_run`, `_simulate_segments` / `_simulate_average`, `finish(elapsed, stopped)`), STOP ends a run, circular runs wrap once, averaging beyond 128 records (16 wide) scales the exact sums; `Device.ui_average` hook. Tools `scope_capture_segments`, `scope_averaged_capture(hardware=)`, `scope_status` features / ui_averaging |

GET_LINK_STATS counts (named by the count field): 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3),
35 (0.2.4), 41 (0.2.5-0.2.6), 47 (0.2.7-0.2.11), 48 (0.2.12-0.2.16, usb_irq_polled), 49 (0.2.17-0.2.18,
boot_splash_ms), 57 (0.2.19, restart record 49-56). Names match `bench_link.py`. The payload is u16 count, u16 0,
then the counters (word 0 is not a counter). Since 0.2.21 n = 58 with 47 and 57 retired (always 0; the soak
tool reports them only when they move, i.e. on older firmware). Since 0.2.44 counter 36 counts SUSPEND and 37 RESUME
interrupts only, and 10 `link_resets` no longer counts suspends.

## Volts model (`calibration.channel_scale`, `scope.Scope.scale`)

Per channel and ADC core (A = 0x20/0x22, low byte in layout 2; B = 0x21/0x23), in this order:

1. **Scope calibration v3** (GET_CAL version 3, after a base calibration on fw >= 0.2.45): for the capture's clock
   class (`P.cal_clock_class(divider, decimated)`: full at divider 0 and for every filtered capture, gated
   otherwise) the raw 0 V code `z_k = zero16_at(...) / 16` (zero + per-class slope x dcoffset, dcoffset =
   offset_pwm - dc_calibration_offset[range]) and volts per raw code `g_k = uv_per_lsb x G_k[c]` (G = gain onto the
   gated mean gain). READ_RAW: `(raw - z_k) g_k`. READ_TRACE 8-bit codes are corrected on the scope to
   `T + G_k (raw - z_k)` (T = `cal_target`), inverted by `volts_compensated`. 16-bit samples: mean raw code
   `m = out x 2^S / gain`, volts `(m - mean z) x harmonic mean of g_A, g_B` (`volts_mean_raw`), firmware 8-bit codes
   `T + gain16 (m - mean z)` (`codes_mean_raw`, inverse `volts_wide_codes`). Source string
   `scope calibration v3 (full|gated clock)`. **v4** (protocol 1.15, row 54, full class only, from GET_CAL 256..287):
   the 8-bit v3 code c of core A becomes `c - (d + e (c - 128)) / 2 - cm`, of core B `c + (d + e (c - 128)) / 2 - cm`
   (`ChannelScale._v4_code`, in `codes_from_raw` and `volts_raw`); 16-bit codes `- cm` (`codes_mean_raw`,
   `volts_mean_raw`). READ_TRACE codes carry v4 already (`volts_compensated` uses them as they are). q is shown, not
   applied (as the firmware). Source string `, v4 tracking (A-B .., cm ..)`. Simulator: `SimConfig.v4` = per channel
   (d, e, q, cm) of true full-class drift with a converged tracker (GET_CAL reports and the readouts take out d, e, cm).
2. **Per-unit JSON** (`<data_dir>/cal/<serial>.json`, below): with v3 in use it contributes only its gated per-core
   gains, carried to the class by `G_k[c] / G_k[gated]` (`, unit-calibration gains` in the source); its zeros are
   ignored. Without v3 it supplies zeros, slopes and gains for the ranges it covers.
3. **Firmware formula** (uv_per_lsb_q8, zero_code_q8 and the 1.00o5 whole-code compensations).

`scope_calibration_info` shows all three: the live conversion per channel, `scope_calibration` (version, flags
incl. gains measured / kept, the next readout's gain / offset / gain16 / corr16, the stored zeros, slopes and noise
per class and core, the gain corrections per class and core, gain16, the range and lever the gains were measured
with) and the JSON's fits. Simulator truth for tests: `SimConfig.zero_error`, `core_b_offset`, `gated_zero_shift`,
`core_gain` (relative gain per channel, class, core), `offset_slope`, and `cal_v3=True` for a bound v3 block
measured from that truth.

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

Per unit at `<data_dir>/cal/<serial>.json` (default data dir `~/.fnirsi1013d/data`; the owner's `.mcp.json` points it at
`mcp/data`, gitignored), loaded at connect,
`--cal-file` / `FNIRSI_MCP_CAL_FILE` override, written by `scope_calibrate_volts`, shown by
`scope_calibration_info`. The owner's `mcp/data/cal/1013D-0001.json` (69 points) is rebuilt with
`python -m fnirsi1013d.calibration --unit 1013D-0001 --out data/cal/1013D-0001.json --import
../backups/t8_CH1_0.010V.json ../backups/t8_CH1_0.308V.json ../backups/t8_ch1_1.000V.json
../backups/t8off_CH1_0.000V.json ../backups/t8off_CH2_0.000V.json` (delete it first; `--import` appends). Facts
(T8, fw 0.1.7): gains within 1.5 % of uv_per_lsb on every range; slope -0.264 (A) / -0.260 (B) codes per PWM count,
range-independent; 0 V at dcoffset 0: CH1 A 132.47 / B 127.14, CH2 A 128.80 / B 129.19 (CH2 has 0 V points only, so
firmware gains). The firmware formula assumes 128 on the compensated mean. Grounds: the probe ground is VRF1,
+1.24 V over the USB ground (fpga/al3/README.md); the calibration output reads -1.24 / +1.9 V against it by design.

## Tests (`tests/`, 418, about 120 s)

`test_protocol` (codec, structs, vectors, link stats/event log), `test_transport` (incl. the scripted-link deferred
cancel test), `test_robustness`, `test_serial_link`, `test_simulator_scope`, `test_shared_mode` (1.3),
`test_firmware_match` (section 14 items), `test_server` (every tool in-process, restarts, legacy fallback),
`test_stdio` (server subprocess), `test_analysis`, `test_calibration`, `test_flash` (FEL flow, bench wrapper),
`test_sdcard` (SD commands, persist, resume after drops, restore, CLI), `test_ext_trigger`, `test_soak`,
`test_fir` (1.11 FIR maths / sim / tool, suspend event fields, soak reopen mode), `test_cal_v3` (1.12 GET_CAL 256, `adc_cal_v3` block vectors, volts on both cores and classes, GET_CAL's next-readout
gain / offset vs the host model, unit file gated gains only, scope_calibration_info),
`test_paths` (outside-a-checkout defaults and refusals, `--list-tools`), `test_seq` (1.17: maths vs the `seq` vectors, wire layouts vs the firmware frames, segment / circular / STOP / auto runs, averaging legacy and 16-bit with positions, fit errors, the tools), `test_diag` (1.16 stored layouts vs `diag_layouts`, GET_HEALTH / SD identity / provenance / SAVE_REPORT layouts and simulator, the tools), `test_cal_v4` (1.15 GET_CAL 288, v4 volts on raw / WIDE / trace codes vs the simulator's drift, GET_CAL re-read, tracker words), `test_spectrum` (1.7 spectrum view, event log 0.2.22-0.2.25 additions, FFT_BENCH), `test_decim` (1.9: maths vs
the firmware vectors, wire layouts, simulator noise / volts / trigger / EXT latency, `scope_set_decimation`),
`test_enc_check` (1.19: vector `resp_get_health_1_19`, 96-byte compatibility, words, simulator counters, tool and caveat; CAL_PROBE: vector, round trip, model, tool), `test_bode_device` (1.20: vectors, codecs, device vs host within 0.02 dB / 0.2 deg with the same flags and ranges, thru, STOP / abort / reason 5, the tool), `test_report` (every kind through the tools, offline HTML check, no-matplotlib fallback, CLI),
`test_bode` (planner rules and vectors, DFT, a synthetic RC through dithered 8-bit at 200 MSa/s, gates, markers,
thru cal), `test_bode_sim` (the DUT model, the runner end to end: RC, thru, fixed range, floor, legacy and CIC2
points, a resonance that steps CH2 up and down, E_BUSY, EXT restore, the CLI), `test_bode_tool` (the MCP tool).
Fixtures: `sim`/`transport`/`scope` are collaborative with UI captures on demand (`ui_capture_period_s=0`);
`legacy_sim` / `sim_factory(legacy=True)` give the 1.2 model. Simulator hooks: `SimServer.corrupt_next`,
`garbage_next`, `drop_next`, `bad_crc_next`, `lose_next`/`lose_cmd`, `drop_link_on`; `Device.ui_set_*`,
`ui_press_run/stop`, `ui_run_capture`, `ui_set_spectrum` (display mode, window, FFT length, channel), `ui_set_display` (any display mode), `on_arm`,
`user_tap` (legacy), `log_event` (also feeds the bus-level ring), `self_restart`, `ui_set_decim`, `sd.*` faults.
SimConfig `cal_v3` (a bound v3 block measured from the model: zeros, slopes, gains G = g_ref / g), `core_gain`
(relative gain per channel, class and core) and `gated_zero_shift` (per-core zero moves while the encode clock is
gated). Known flake: `test_sdcard::test_slow_write_answers_are_retried_without_reopening` (0.2 s write
timeout) can fail once under a loaded full run; it passes alone.
Run: `.venv/Scripts/python -m pytest -q`; WSL: `wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/mcp &&
~/.venvs/fnirsi1013d-mcp/bin/python -m pytest -q -p no:cacheprovider"`; lint: `.venv/Scripts/ruff check --select
F,E9,B023 --line-length 120 src tests`; vectors: `python -m fnirsi1013d.vectors --check` (the C checker:
`make -C ../firmware/scope/host_tests test` in WSL).

## Bode bench (for the coordinator; design 6.3)

**Done 2026-10-19 (docs/hardware-status.md):** wiring A and B agree within 0.4 dB, floor -80 dB, thru +0.3 dB, divider
flat; wiring A (clips on the DUT ground) is the default now. Wiring B's 1.24 V DC saturated CH2 on 0.1 / 0.2 V/div with a
flat record the offset loop had centred: `ch2_overload` (flat record, or a tone collapse against the range-1 prescan
/ the previous range at the same frequency), auto steps up, fixed stops, `ch2_coupling='ac'`, offset exhaustion is a
caveat (simulator: `SimConfig.frontend_window_v`, pinned samples lose their noise). Open: the 15 nF RC reads an
apparent C falling 14.7 -> 9.8 nF and tan delta rising to 0.29 by 200 kHz while the divider is flat; the coordinator's
high-pass with the same parts decides dielectric vs measurement. The procedure below is the original plan.

DUT: RC low-pass, R = 1.60 kOhm 1 %, C = 10.0 nF film or C0G (fc 9.95 kHz). CH1 x1 on the RC input (the post), CH2 x1
on the output. Generator off before wiring. `--port COM10` (or the MCP tool with the same arguments; it asks for a
token). Every run prints the table, the markers and the caveats; keep the `--json` files in `backups/bode/`.
Expected magnitude / phase of the RC (no probe load): 1 kHz -0.04 dB / -5.7 deg, 3 kHz -0.38 / -16.8, 10 kHz
-3.03 / -45.2, 30 kHz -10.0 / -71.7, 100 kHz -20.1 / -84.3, 300 kHz -29.6 / -88.1; CH2's ~100 pF adds about 1 % to C.

```bash
cd mcp && mkdir -p ../backups/bode
B=".venv/Scripts/python -m fnirsi1013d.bode --port COM10 --yes"   # or fnirsi1013d-bode after a venv reinstall
# 0. plan and time (no scope)
$B --plan-only --start 1kHz --stop 300kHz --points 30
# 1. ground wiring. A: DUT ground on the CH1 / CH2 ground clips. B: DUT ground on the USB shell, clips open.
$B --start 1kHz --stop 300kHz --points 30 --json ../backups/bode/rc_clips.json      # wiring A
$B --start 1kHz --stop 300kHz --points 30 --json ../backups/bode/rc_usb.json        # wiring B
#    then in wiring A, CH2 tip on the DUT ground node (reads the return error e / V1):
$B --start 1kHz --stop 300kHz --points 30 --json ../backups/bode/rc_clips_e.json
# 2. thru: both probes on the post (no DUT), raw then calibrated, over the RC's plan and a wide one
$B --start 1kHz --stop 300kHz --points 30 --thru measure --thru-out ../backups/bode/thru_rc.json
$B --start 1kHz --stop 300kHz --points 30 --thru ../backups/bode/thru_rc.json --json ../backups/bode/thru_check.json
$B --start 100Hz --stop 2MHz --points 50 --thru measure --thru-out ../backups/bode/thru_wide.json
# 3. floor: CH1 on the post loaded with 1 kOhm to the DUT ground, CH2 tip on that ground
$B --start 100Hz --stop 2MHz --points 50 --json ../backups/bode/floor.json
# 4. coherence: single points at M = 50 and M = 5000; see leak_db in the table
$B --start 1MHz --stop 1MHz --points 1
$B --start 10kHz --stop 10kHz --points 1
# 5. the RC (wiring B): a thru with this plan (both probes on the post), then five repeats on the RC
$B --start 1kHz --stop 1MHz --points 50 --thru measure --thru-out ../backups/bode/thru_rc50.json
for i in 1 2 3 4 5; do $B --start 1kHz --stop 1MHz --points 50 --thru ../backups/bode/thru_rc50.json --json ../backups/bode/rc_$i.json; done
```

Accept (design 6.3): step 1 wiring B within 0.2 dB / 1 deg of the expected values to 100 kHz; wiring A visibly off from
about 30 kHz up if design 2.5 holds (then the connection text stays; otherwise relax it). Step 2: raw `mismatch_db`
within +-0.2 dB and phase within +-1 deg to 1 MHz (record them); calibrated (thru_check) +-0.02 dB / +-0.1 deg. Step 3:
every point flagged `near_floor` or `low_snr`, |H| below about -55 dB; record the worst |H| per decade (that is the
isolation floor). Step 4: no `incoherent` flag, `leak_db` below -60 at both points (above -40 means the DDS and the ADC
clock are not locked: design risk "clock coherence", fall back to the Hann window). For the pattern-tone half of
bench item 4 use the generator and `scope_capture_and_fft` on CH1 at 12.5 MSa/s CIC3+FIR: 900 kHz (non-integer M)
shows a 100 kHz tone near -37.5 dBV, 1 MHz does not. Step 5: within 0.2 dB / 1 deg of the RC (fit C_total from the -45 deg crossing), `minus_3db_hz` 9.9-10.1 kHz
(relative to the 1 kHz point), five repeats within 0.05 dB. Then the tank (design 6.3 item 6): 1 mH parallel 10 nF
fed through 10 kOhm from the post, CH2 x10 on the tank, `--start 30kHz --stop 80kHz --points 100 --settle resonant`:
peak at 50.3 kHz within 0.5 %, Q against an LCR meter; the same with `--settle normal` should flag `unsettled` points.
Record the verdicts in `docs/hardware-status.md`.

## Protocol vectors contract

`../docs/protocol-vectors.json` is shared; fw-gen2's `gen_vectors.py` keeps entries whose `source` is not
`firmware`. Host entries are `host_*` from `vectors.build_host_vectors()` (19, incl. five 1.3 ones); add new ones
there, `--merge --check`, never change an existing vector's bytes.

## Open items (priority order)

Pending work for every agent lives in `../docs/plans/task-queue.md` (section "MCP / host"); the coordinator dispatches
from there and moves items to "Dispatched". Add host items you discover there, not only here.

0. **1.17 on the unit** (task queue "1.17 on the unit, host side"): after the owner flashes 0x1536 and the firmware checks
   7-9 of `../firmware/AGENTS.md` section 35 pass, run `scope_capture_segments` and `scope_averaged_capture(hardware=true)`
   on the bench (coordinator holds COM10), then default `hardware` to auto where the record fits, add it to
   `scope_decay_fit`, and drop the "not yet verified" caveats. The release-readiness items of the wheel are done
   (8ea8886 paths, 0e1d36b wheel smoke + CI job, ef8aa30 docs + `.mcp.json.example`; the coordinator untracked the owner's
   `.mcp.json` in 508f0c5).
0a. **1.19 / 1.20 on the unit** (coordinator holds COM10): `scope_health` on 0x1538 (`encode_check` mode 2, last 0x33
   0xF6, counts), then `scope_bode_sweep(where='device')` on the divider and the RC of firmware/AGENTS.md 42 steps 3-4
   next to the same `where='host'` sweep: expect within 0.05 dB / 0.3 deg where both are valid; check the poll
   interval (0.2 s) and the device's `elapsed_ms` against its estimate; `scope_cal_probe` on both channels at range 0
   with nothing connected (the bench's 0.2.67 numbers are in docs/hardware-status.md 18:20).
1. **Bode: bench results in, then the follow-ups.** Host steps H1-H3 are done (1d46cf8 simulator DUT, f0940ee H1,
   363f676 runner and CLI, d3a16bb tool, docs commit after). Read the coordinator's bench numbers (procedure below)
   against the design's expectations; if the wiring verdict (section 2.5) or the floor changes the advice, fix
   `bode.CONNECTION_TEXT`, README "Bode sweep" and guide 3b. Then the task queue's "Bode follow-ups": CH1
   re-ranging, the harmonic fast mode (design 1.3, same planner and DFT, harmonics as extra bins), the impedance mode
   (design 2.4: Z = R_ext V2 / (V1 - V2), L / C / ESR markers; the coil measurement for the detector), PNG plot
   (matplotlib optional). The device steps D1-D5 belong to a firmware agent; `docs/bode-plan-vectors.json` is the
   planner they must reproduce (regenerate only on a deliberate plan change: `python -m fnirsi1013d.bode
   --write-vectors ../docs/bode-plan-vectors.json`).
2. **Release 0.3.0** (on hold until the lead says go): bump `version` in `pyproject.toml` (still 0.1.0) with the
   lead's release (tag = `PC_FW_VERSION` = mcp version, CHANGELOG section); the 30-minute soak
   (`fnirsi1013d-soak --duration 1800`) is the gate; fold the baseline soak results into README "Link soak".
3. **USB drops / suspend**: read the lead's `--mode reopen` logs with row 50 (type 1 b of a few ms after minutes
   of silence = a port reset without a host suspend; b of minutes and no type 3 = a missed resume).
4. Collaborative mode bench acceptance (`../docs/plans/collaborative-mode.md`), incl. per-request cost in shared
   mode for averaging.
5. Metal-detector tools (lock-in amplitude/phase, PI target comparison, ground balance, monitor); the Bode work
   covers the sweep.
6. Adopt ACQUIRE_AVG / READ_SCREEN / READ_LIVE when the firmware ships them (simulator has them). READ_SCREEN built
   in firmware (fw 0.2.70-screen, branch fw-screen, firmware/AGENTS.md section 46), bench pending (T9);
   `scope_screenshot` needs no change, tests/test_read_screen.py checks the wire format against the vectors.
7. GET_BAD_FRAME (0x0008) decode and dump on E_BAD_CRC (the cmd id is in `Cmd`, no client method yet).
