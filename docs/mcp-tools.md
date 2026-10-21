# MCP tool surface for the FNIRSI 1013D

Status: the design of 2026-10-18 as implemented, updated with each mcp generation (server 0.3.0 in preparation, protocol
1.24). Where this text and the server differ, the tool docstrings (`fnirsi1013d-mcp --list-tools`) are current. Wire protocol: `docs/protocol.md` (cited as P-section). Firmware side:
`docs/plans/firmware-usb-protocol.md`. Markers [V]/[M]/[U]/[D] as in the protocol document.

## 1. Design principles

Taken from the MCP prior art surveyed in `research/notes/web-sources.md` section 6:

- **Capability profile plus caveats on every output** (grayzhuang/oscilloscope-mcp). The server builds a
  profile from GET_INFO and a static table, validates every request against it before touching the wire, and
  attaches a `caveats[]` list to each result: static ones (8-bit, ~30 MHz) and dynamic ones (clipping, auto
  trigger fired, rate too low for the requested band).
- **Safety tiers with confirmation for anything that leaves the box** (zinntikumugai/rigol-oscilloscope-mcp):
  tiers below, two-step confirmation token for tier 3+, JSONL audit log, read-back of applied values,
  fail closed on `E_UNSUPPORTED`.
- **Arrays go to files, stats come inline** (ikatkov/rigol-mcp). Any waveform longer than 2,000 points is
  written to an `.npz` file (time vector, volts, raw codes, metadata) under `<data dir>/<date>/`
  (default `~/.fnirsi1013d/data`). The tool
  returns statistics, the file path and an optional decimated min/max preview of at most 500 points.
- **Physical units in, physical units out.** Tools take volts, seconds and hertz, and convert with the
  scope's own calibration (P-8.5). Raw escape fields (`range_index`, `divider`, `level_code`) exist for
  experiments.
- **Actionable errors.** Firmware error text (P-4.2) is passed through, prefixed by what the tool was doing
  and followed by a fix hint, e.g. "set_trigger: pretrigger_samples must be <= total_samples - 2 (got 16382,
  total 16382). Lower pretrigger_fraction."

Implementation outline: Python 3.11+, the `mcp` SDK (FastMCP), `pyserial`, `numpy`, `scipy` (fits only).
Package layout in `mcp/`: `fnirsi/protocol.py` (frame codec, CRCs, structs), `fnirsi/client.py`
(transport, retries, session), `fnirsi/profile.py`, `fnirsi/analysis.py` (averaging, FFT, fits),
`server.py` (tools). One serial connection per server process; a lock serialises all tool calls on it
(siglent-sds-mcp pattern).

## 2. Safety tiers [D]

| Tier | Name | What it changes | Examples | Default |
|---|---|---|---|---|
| 0 | READ_ONLY | nothing | `scope_info`, `scope_profile`, `scope_status`, `get_config`, `fetch_waveform` (LOCAL), `read_calibration`, `screenshot`, `report`, `export_csv`, `decode` and `measure` on existing data | allowed |
| 1 | ACQUIRE | FPGA acquisition registers only (sample rate, trigger, memory geometry, arming); no effect outside the scope; takes the UI away from the person at the scope | `remote_session`, `set_timebase`, `set_trigger`, `capture`, `acquisition_control`, `averaged_capture`, `capture_segments`, `capture_and_fft`, `decay_fit`, `measure` / `decode` with `capture=true`, `mask_test`, `dvm` | allowed |
| 2 | FRONT_END | relays (range, AC/DC) and the offset PWM; audible clicks, relay wear, a small transient at the input when AC coupling switches | `set_channel` | allowed, rate-limited to 5 relay changes per second by the server |
| 3 | OUTPUT | drives the calibration pin (3.3 V logic square wave). If that pin is wired into the detector (e.g. as a TX gate drive), this switches real coil current | `generator`, `self_test_loopback` | needs a confirmation token; off at server start |
| 4 | RAW | arbitrary FPGA bus writes, rebooting into FEL | `raw_fpga`, `enter_fel` | disabled unless `FNIRSI_MCP_ENABLE_RAW=1`; needs a confirmation token |

Configuration (environment variables of the server process):

| Variable | Default | Meaning |
|---|---|---|
| `FNIRSI_MCP_PORT` | auto | serial port; auto-discovery checks USB product string `FNIRSI 1013D` and the `#VER` reply |
| `FNIRSI_MCP_MAX_TIER` | 3 | highest tier the server exposes at all |
| `FNIRSI_MCP_ENABLE_RAW` | 0 | allow tier 4 |
| `FNIRSI_MCP_GEN_MAX_HZ` | 1000000 | generator frequency ceiling |
| `FNIRSI_MCP_GEN_MAX_DUTY` | 50 | generator duty ceiling in percent (protects a TX stage driven from the pin) |
| `FNIRSI_MCP_HOME` | `~/.fnirsi1013d` | per-user directory for `data/` and `logs/` |
| `FNIRSI_MCP_DATA_DIR` | `<home>/data` | where `.npz` files, per-unit calibrations and Bode thru files go |
| `FNIRSI_MCP_AUDIT_LOG` | `<home>/logs/audit.jsonl` | every tier 1+ call: time, tool, args, applied values, result status |
| `FNIRSI_MCP_IDLE_CLOSE_S` | 30 | close the serial port after this many seconds without a tool running (a long run counts until it returns) and reopen it on the next call, so other programs can use the port meanwhile; 0 = hold it until exit |
| `FNIRSI_MCP_LEGACY_IDLE_RELEASE_S` | 60 | legacy firmware: release REMOTE (keeping the host settings) after this many idle seconds; 0 = hold |

Confirmation tokens: a tier 3/4 call without `confirm_token` does nothing and returns
`{"confirmation_required": true, "token": "<8 hex>", "summary": "<what will happen>"}`. The token is bound to
a hash of the arguments and expires after 120 s. The agent repeats the call with the token. The summary
spells out the physical effect, e.g. "calibration pin will output 3.3 V square wave, 10.000 kHz, 30 % duty".

## 3. Static profile and standing caveats

`scope_profile` returns this, merged with GET_INFO. Every waveform tool includes the "standing" caveats that
apply to its settings.

| Item | Value | Source |
|---|---|---|
| Max sample rate | 200 MS/s per channel, both channels at once, 2 x 100 MS/s interleaved cores | [V] zaklad.v, SYNTHESIS.md section 2 |
| Rate formula | fs = 200 MHz / (N + 1), N = 0..2^32-1 | [V] |
| Resolution | 8 bit, about 2 LSB noise; averaging n captures gives at most log2(sqrt(n)) extra bits and only while noise dithers (>= ~0.5 LSB rms) | [M] thread p91-120 |
| Analog bandwidth | about 20-40 MHz (-3 dB) depending on unit; treat 30 MHz as the limit. Not 100 MHz. | [M] thread p1-90 |
| Interleave artefacts | A/B core offset (1.4-5.5 codes before compensation) gives a spur at fs/2; gain/timing mismatch gives an image at fs/2 - f | [M] 1014D acqprobe, inference |
| Memory per channel | stock FPGA 0x1432: 3000 samples (fixed 50 % trigger); Atlan4 AL3 0x1532: up to 16382; EF2 0x1632 / EP4 0x1732: up to 12286 | P-5.1 |
| Trigger | edge on core A of CH1 or CH2, 8-bit level, quantised to 2 / fs; no hold-off; no external input on current bitstreams | P-7.5 |
| Ranges at 1x probe | 5 V, 2.5 V, 1 V, 500 mV, 200 mV, 100 mV per div (relays), 50 mV per div (software zoom of 100 mV, about 7 effective bits) | P-8.3 |
| Full scale | about 256 codes = ~8.8 divisions; e.g. ~0.88 V at 100 mV/div | P-8.5 [U] |
| Coupling | DC or AC (relay) | |
| Input impedance / max input | 1 MOhm; maximum safe voltage per FNIRSI is not reliable, assume 40 V peak at 1x and use a 10x probe for anything higher [U] | needs datasheet check |
| Generator | square wave only, 3.3 V logic on the calibration pin, 11.6 mHz steps, 20 ns edge jitter, <= 25 MHz; only on Atlan4 bitstreams. No DAC, no arbitrary waveform | P-8.6 |
| Throughput | ~50-65 full dual-channel AL3 captures per second via raw reads; hundreds per second for short windows | P-11 [U USB rate] |

## 4. Tool reference

Conventions: `channel` is `"CH1"` or `"CH2"`. Times are seconds (floats); the server also accepts strings with
units (`"100us"`). Every result object has `ok`, `caveats` (list of strings) and, for tier 1+, `applied` (the
read-back configuration). Errors raise MCP tool errors with the actionable text.

### 4.1 Session and status (tier 0/1)

**`scope_info`** (tier 0). No parameters. Returns firmware version and build, protocol version, the FPGA version
word the scope reports and `fw_fpga_version` (the one the firmware is built for; `fpga_mismatch` when they differ,
protocol 1.25), serial, capability flags as names, ring and memory limits. On a mismatch the caveats carry
`Profile.mismatch_caveat()` (the scope shows a notice and refuses captures and settings until that bitstream is
flashed); every tool that checks the scope first carries the same caveat.

**`scope_profile`** (tier 0). No parameters. Returns section 3 merged with live values (current memory
limit, which bitstream, which caps are unverified: `read_lag`, interleave order, FORCE).

**`scope_status`** (tier 0). No parameters. Returns `control` (LOCAL/REMOTE), `acq_state`, `capture_id`,
`trigger_addr`, time since arm, battery level and charging, `exit_reason` if the scope went back to local
on its own (user tap, watchdog, port closed).

**`remote_session`**: retired with collaborative mode (docs/plans/collaborative-mode.md, protocol 1.3). The scope
is shared: no ownership to take or release, the last change wins, host captures appear on screen, and long runs
restart (and say so) when a setting is changed at the scope. On legacy firmware without the SHARED_MODE capability
the server takes REMOTE implicitly and releases it (keeping the host settings) after 60 s idle and at exit.
The port itself is closed after 30 s without a tool running (`FNIRSI_MCP_IDLE_CLOSE_S`; on legacy firmware only once
REMOTE is released) and reopened on the next call, which reloads the per-unit calibration file as a first connect does.
`scope_status` reports `mode`, `run_state` and `settings_seq`; `acquisition_control` gained `run`.
Protocol 1.7 adds `display_mode` (`scope`, or `spectrum` while the screen shows the spectrum view); while it is
`spectrum`, every tool that checks the scope first (captures, setters) adds the caveat that the person sees the
spectrum of each capture, not the trace. The data are unaffected. Later modes: `waterfall` / `density` (1.13 spectrum
sub-views), `phosphor` (1.13), `bode` (1.20, the scope's Bode plot: its own acquisition is off). `features` gains
`enc_check` (1.19, 0x1538) and `bode` (1.20, what the device sweep needs: DECIM and the generator).

**`health`** (`scope_health`, tier 0, firmware 0.2.52+ / protocol 1.16, also while a menu is open). GET_HEALTH in
words and values: USB state now (off, reconnecting, not configured, configured, suspended; high speed, port open,
POWER / FADDR, time in the current suspend, since SET_CONFIGURATION), the CP15 control word (MMU, D-cache, I-cache),
unit life (power-on number, operating hours, uptime, whether the settings sector was valid and the boot write
succeeded, how the previous power-on ended), the settings save state, and the stored restart record (exception type,
power-on, restarts, the saved registers; it survives power cycles until cleared at the scope). Read-only: the clear
and restart-test flags are not exposed. From 1.19 (fw 0.2.62, GET_HEALTH 104 bytes) `encode_check`: how 200 MSa/s
captures are checked for a garbled ADC core (none; record data on AL3 0x1532..0x1537; the FPGA's self-check read 0x33
on 0x1538+), the last 0x33 byte with its failing pins, the boot convention check, captures checked / found garbled /
re-taken clean, and one line in `in_words`. Every tool that checks the scope first adds a caveat while the last
full-rate capture was accepted garbled after 16 re-arms (or the boot convention check failed). Event types 21
(`adc_fullrate_garbled`: reason, re-arms, caller, residual or 0x33 byte) and 22 (`fpga_write`) are decoded in words by
`scope_event_log`.

**`event_log`** (`scope_event_log`, tier 0, protocol 1.5, fw 0.2.12+). GET_EVENT_LOG read plainly: `last` (1..128,
default 64) newest events with device ms, age and the detail in words; `reset_snapshot` (fw 0.2.15+, the events
before the first bus reset since it was armed) and `bus_events` (fw 0.2.24+, the 16-entry bus-level ring). `fpga_trace`
(protocol 1.22, fw 0.2.72): true / false switches the FPGA write trace (event type 22: every rate / filter write and
sampling reset with its caller) before reading, omitted leaves it; it is off after boot because it floods the ring
(the writes are always counted in GET_LINK_STATS 64-66). The result carries `fpga_write_trace` from 1.22.

**`save_report`** (`scope_save_report`, tier 1, 1.16, cap SD_SECTORS). SAVE_REPORT: the scope writes
`reports/report-PPPPP-NN.txt` on its card's FAT partition (identity, life, USB, battery, counters, event log, both
channels' calibration in words) and the tool returns the name and size; the file stays on the card. FatFs errors
(no card, not ready, denied, 100 reports this power-on) come back in words.

**`sd_info`** (`scope_sd_info`, tier 0, cap SD_SECTORS): card size, partition, write window, eGON headers at 16, 80
and 750 (checksums with `verify`); from 1.16 `identify` (default true) adds `card_image`: the state of the
identification (identity record, build tail, version string only, unknown, no image), version, commit hash and time,
protocol, and whether it is the running image, with a sentence ("the card boots fw X; running now: fw Y").

**`get_config`** (tier 0). Returns both channels (enabled, V/div, coupling, offset in divisions and volts,
probe factor, invert), timebase (rate, dt, time/div equivalent, divider), trigger (source, edge, level in
volts and code, mode, pre-trigger samples and fraction, total samples, window duration), generator.

### 4.2 Configuration (tier 1/2)

**`set_channel`** (tier 2).

| Param | Type | Notes |
|---|---|---|
| `channel` | `"CH1"`/`"CH2"` | |
| `enabled` | bool | readout only; both channels are always sampled |
| `volts_per_div` | float | snapped to the nearest range at the given probe factor; or `range_index` 0..6 |
| `coupling` | `"DC"`/`"AC"` | relay |
| `offset_div` | float | vertical offset in divisions (UI semantics), or `offset_raw` 0..2000 PWM counts |
| `probe` | 0.5, 1, 10, 20, 50, 100, 1000 | scaling only; never changes hardware |
| `invert` | bool | |

Returns `applied` with the chosen range, `volts_per_lsb`, full-scale span in volts, zero code.
Description caveats: "Changing volts_per_div or coupling clicks relays and needs 50 ms settling (handled).
50 mV/div is a software zoom of 100 mV/div: same resolution, about 3.4 mV per code. Offset is a PWM-derived
DC injection; after changing it, wait for one capture before trusting DC levels."

**`set_timebase`** (tier 1).

| Param | Type | Notes |
|---|---|---|
| `sample_rate` | float Hz | snapped to 200 MHz / (N + 1), nearest at or above the request |
| `window` | float s | alternative: choose the highest rate that fits this duration into the current `total_samples` |
| `divider` | int | raw N |

Returns actual rate, dt, window = total_samples x dt. Caveats: "The trigger is checked only every 2 samples
(core A), so trigger jitter is up to 2 / fs. For averaging of PI decays, prefer >= 10 MS/s so the 2-sample
jitter (200 ns) stays small against the decay." "Roll / long timebases (>= 20 ms/div in the UI) are not
available remotely in protocol v1."

**`set_trigger`** (tier 1).

| Param | Type | Notes |
|---|---|---|
| `source` | `"CH1"`/`"CH2"` | `"EXT"` reserved; fails closed until an FPGA provides it |
| `edge` | `"rising"`/`"falling"` | |
| `level` | float V | converted to a raw core-A code with the channel's calibration minus `adc_a_comp`; or `level_code` 0..255 |
| `mode` | `"auto"`/`"normal"`/`"single"` | remote captures never re-arm on their own, so single equals normal |
| `pretrigger_fraction` | 0..1 | or `pretrigger_samples` (even) |
| `total_samples` | int | even, 80 .. max for the bitstream |
| `auto_timeout` | float s | auto mode only |
| `type` | `"edge"`, `"pulse_width"`, `"nth_edge"`, `"timeout"` | protocol 1.26 (caps TRIG_TYPES, SET_TRIGGER2): `runt` / `window` are refused with the hint "FPGA 0x153E" |
| `polarity` | `"positive"` / `"negative"` (pulse), `"rising"` / `"falling"` (edge, nth_edge), `"high"` / `"low"` (timeout) | per type; the edge type's polarity is `edge` |
| `width` | float s | pulse_width: the width compared with (the trigger is the trailing edge); timeout: the held time. Converted to samples of the trigger stream in effect (one memory word on CH1 / CH2: 2 / fs legacy, 1 / fs filtered or peak; 5 ns on EXT), rounded to the nearest, clamped to 1 .. 16777215 with a caveat; the firmware converts nothing |
| `longer` | bool | pulse_width: pulses longer than `width` (default shorter) |
| `n` | int 1..65535 | nth_edge: the edge to trigger on |
| `idle` | float s | nth_edge: the count restarts after this long without any edge (0 = count from the arm); same conversion as `width`, 0 .. 16777215 samples |
| `hold_off` | float s | caps HOLDOFF: from a record's trigger no trigger is accepted until it has elapsed, across re-arms and segments; 5 ns steps up to 21.47 s, rounded; 0 = off. Every SET_TRIGGER2 the tool sends also clears a hold-off still running at the next capture (the firmware writes 0x66) |

Returns applied values, including the level actually reachable (codes are about 3.4 mV apart at 100 mV/div),
the pre/post window durations, `holdoff` (5 ns units) / `holdoff_s`, and from 1.26 `type` (name, polarity, the
width / idle / held time in samples and seconds, `stream_sample_s`). Caveats: "Level is an 8-bit code compared to
core A only; edges faster than 2 / fs may be missed."; for a type the resolution and position statement; a clamp or
rounding of `width` / `idle` / `hold_off`. The type parameters need protocol 1.26 with caps TRIG_TYPES (refused with
that hint below it). The client method is `Scope.set_trigger2(type=, polarity=, longer=, n=, h=, level=, holdoff=
(5 ns units) | holdoff_s=)`, which round-trips GET_TRIGGER's 24-byte block and zeroes the fields a new type does
not use (the firmware refuses N / H on the wrong type).

### 4.3 Acquisition (tier 1)

**`capture`** (tier 1). Arms, waits, reads.

| Param | Type | Default | Notes |
|---|---|---|---|
| `channels` | list | enabled ones | |
| `timeout_s` | float | 2.0 | normal mode waits this long for a trigger |
| `window` | `[t0, t1]` s relative to the trigger | whole capture | reads only this part (faster) |
| `raw` | bool | false | also store raw codes per core |
| `processed` | bool | false | use the firmware's processed trace instead of raw reads plus host compensation (slower, matches the screen) |
| `peak` | bool | false | protocol 1.26 (caps PEAK_DETECT): peak detect for this capture: SET_DECIM 4 before the arm, the previous Filter restored after (a caveat says so); with the Filter already `peak` nothing is toggled. Refused at 200 MSa/s (peak needs a rate below it) and over 8190 samples |

Returns per channel: min, max, mean, rms, Vpp, frequency estimate, clipped-sample count; `trigger`
(`fired` / `auto_timeout` / `none`), `capture_id`, dt, sample count, file path, preview. Dynamic caveats:
"N samples clipped at 0/255: increase volts_per_div", "auto trigger fired: data not synchronised",
"signal frequency estimate X is above fs/2.5: aliasing likely", "frequency > 30 MHz: above analog bandwidth".
A peak-detect record (`peak` true in the result, kind `peak` in the `.npz`) adds per channel `envelope` (min, max,
mean and max width), the samples / preview as `<ch>_min` and `<ch>_max`, and the file `<ch>_min_volts`,
`<ch>_max_volts`, `<ch>_min_codes`, `<ch>_max_codes`; `volts` / `codes` are the midpoint; the caveat says the
envelope includes the noise extremes and that averaging is refused while peak detect is on (`scope_averaged_capture`
refuses with a hint; SET_SEQ mode 2 answers E_STATE detail 7). Volts of a peak record (READ_RAW layout 3, READ_TRACE
WIDE) use the **full clock class** of the calibration (the stored v3 zeros and gains of the full class, the v4 tracker
included), as the firmware's midpoint does (fw-c6 `fpga_channel_correction_full`): under peak detect the ADCs run at
200 MSa/s whatever the divider, while GET_CAL's `gain_a` .. `off_b` (bytes 92..107) and CAL_GATED describe the
divider's gated class at a divider > 0, so those bytes are not what a peak record needs. At divider 0 and over 8190
samples the record stays legacy (GET_DECIM why 2 / 3) and the firmware does not write peak detect at all (shift byte
0x00).

**`fetch_waveform`** (tier 0 in LOCAL, 1 in REMOTE). Reads the current capture without re-arming (the UI's
last trace in LOCAL). Same returns as `capture`.

**`acquisition_control`** (tier 1). Low-level control for agents that want to arm, do something else (e.g.
fire the detector), then wait.

| Param | Type | Notes |
|---|---|---|
| `action` | `"arm"`, `"wait"`, `"force"`, `"stop"` | |
| `timeout_s` | float | for wait, <= 10 |

Caveat on `force`: "Only available once verified on this FPGA variant; otherwise use mode=auto."

**`averaged_capture`** (tier 1). The main tool for detector work.

| Param | Type | Default | Notes |
|---|---|---|---|
| `n` | int | 64 | number of captures, 1..4096 |
| `channels` | list | `["CH1"]` | |
| `window` | `[t0, t1]` | whole capture | only this part is read and averaged |
| `align` | `"hardware"`, `"edge"` | `"hardware"` | `edge`: per capture, find the crossing of `align_channel` at `align_level` with linear interpolation and shift by the sub-sample offset before averaging |
| `align_channel` | channel | trigger source | typically CH2 carrying the detector's TX gate |
| `align_level` | float V | trigger level | |
| `timeout_s` | float | 1.0 | per capture; timeouts are dropped and counted |
| `reject_clipped` | bool | true | drop captures with clipped samples in the window |
| `on_device` | bool | auto | use ACQUIRE_AVG when the firmware has it and `align="hardware"` |
| `hardware` | bool | false | protocol 1.17 (caps SEQ): one FPGA averaging run sums the `n` records (SET_SEQ mode 2, READ_SEQ sums), no per-record transfer; `align="hardware"` only, records up to 2046 words, `n` <= 257 with 8-bit samples (65536 with the decimation filter); no per-sample std; refused with the GET_SEQ fit reason when the geometry does not fit. Opt-in until bench check 9 (firmware/AGENTS.md section 35) has compared it with READ_RAW on the unit |

Returns mean waveform (file), per-sample standard deviation (file), `n_used`, `n_dropped` with reasons,
estimated noise per sample before/after, effective resolution estimate, timing (captures per second).
Caveats: "Averaging reduces random noise by sqrt(n) but not the A/B interleave mismatch, the 8-bit
quantisation of a noise-free signal, or trigger jitter (up to 2 / fs with hardware alignment).",
"edge alignment needs a clean edge on align_channel with at least 3 samples on the slope".

**`capture_segments`** (tier 1, protocol 1.17, caps SEQ). Segmented capture: the FPGA re-arms
itself after every trigger and stores `count` records of the live trigger geometry (`set_trigger` total and
pretrigger) in slots of 2^m words (the smallest slot that holds the record plus 2 words), each with a trigger
timestamp in 5 ns units; the host reads the table and every segment afterwards (READ_SEQ, READ_RAW addr_mode 3).

| Param | Type | Default | Notes |
|---|---|---|---|
| `count` | int | 16 | 1..256, at most 8192 / slot words |
| `channels` | list | `["CH1"]` | |
| `holdoff` | float s | 0 | wait after each segment before re-arming (5 ns steps) |
| `circular` | bool | false | refill every slot until `timeout_s`, then STOP: the table holds the latest segments |
| `timeout_s` | float | 2.0 | the run is ended with STOP after this long and keeps what it has |

Returns `segments`, `completed`, `untriggered`, `stopped`, `wrapped`, `slot_words`, interval statistics, a table
(slot, time from the first trigger, triggered, per channel min / max / mean / Vpp; the first 64 rows) and a `.npz`
with `t`, `<CH>_volts` (segments x samples), `<CH>_clipped`, `timestamps_s`, `triggered`, `slots`. Untriggered
segments (auto mode) are flagged. The sequencer setting is cleared after every run, so later captures are plain.

**`capture_and_fft`** (tier 1).

| Param | Type | Default | Notes |
|---|---|---|---|
| `channel` | channel | | |
| `n_avg` | int | 1 | power-spectrum averaging over captures |
| `window_fn` | `"hann"`, `"flattop"`, `"blackman-harris"`, `"rect"` | `"hann"` | flattop for amplitude accuracy |
| `fmin`, `fmax` | float Hz | 0, fs/2 | reported band |
| `peaks` | int | 10 | number of peaks to list |
| `detrend` | bool | true | remove mean |

Returns frequency resolution, peaks (frequency, dBV, dBc), noise floor, THD of the strongest peak, file with
the spectrum. Caveats: "8-bit ADC: SFDR around 45-50 dBc at best; spurs below that are not trustworthy.",
"expect a spur at fs/2 and images at fs/2 - f from core A/B mismatch", "content above ~30 MHz is attenuated
by the front end", "spectral leakage: window=X has resolution bandwidth Y".

**`decay_fit`** (tier 1). Fits the receive decay after a TX turn-off edge (pulse induction).

| Param | Type | Default | Notes |
|---|---|---|---|
| `channel` | channel | `"CH1"` | the decay signal |
| `edge_channel` | channel or null | trigger source | where the TX turn-off edge is found; null = use the hardware trigger point |
| `edge` | `"falling"`/`"rising"` | `"falling"` | |
| `t_start` | float s | 10e-6 | start of the fit after the edge (skip flyback and recovery) |
| `t_end` | float s | 200e-6 | end of the fit |
| `model` | `"exp"`, `"exp2"`, `"exp+offset"`, `"power"` | `"exp+offset"` | `exp2` = sum of two exponentials (target + ground/coil), `power` = A t^-k for ferrous/viscous ground checks |
| `n_avg` | int | 64 | captures averaged first (via `averaged_capture` with edge alignment) |
| `baseline` | `[t0, t1]` or null | null | window to estimate the offset instead of fitting it |
| `blank_after_clip` | float s | 2e-6 | exclude samples this long after any clipped sample |

Returns tau (and tau2), amplitude(s), offset, fit window actually used, R^2, residual rms, number of samples,
sample-integration values at user-chosen delays (`sample_points` param, list of seconds, the way PI
detectors sample the decay), file with data, fit and residuals. Caveats: "The flyback spike of a PI coil
can be hundreds of volts: use a 10x probe or a clamp and check the input rating before connecting.",
"after clipping, the front end needs recovery time; samples within blank_after_clip are excluded",
"with fs = F, the decay is sampled every 1/F; tau below ~5 samples is not resolvable", "AC coupling
distorts decays longer than the coupling time constant [U value]; use DC coupling for decays".

**`scope_set_decimation`** (tier 1, protocol 1.9, capability DECIM). Param `filter`: `off` (legacy 8-bit sample
skipping), `boxcar` (mean of N raw 200 MSa/s samples), `cic2`, `cic3` (2- / 3-fold boxcar), with the FIR (protocol
1.11, capability FIR) `boxcar+fir`, `cic2+fir`, `cic3+fir`, and `peak` (protocol 1.26, capability PEAK_DETECT, the
ACQ menu's Sample = Peak: SET_DECIM filter 4, the firmware writes 0x53 K = 1 and 0x56 bit 1; every stored word is the
window's raw min and max, the FIR is off, the shift unused; GET_DECIM's shift bytes carry bit 7 peak active / bit 6
peak written; captures read READ_RAW layout 3; the Filter word is kept for the way back; the caveat names the
refused averaging). The `+fir` choices: the CIC decimates by N / 2 and a 15-tap compensation FIR decimates by 2 (flat passband to fs / 4, about
20 dB more alias rejection; gain (N/2)^K, S one higher, 250 ns latency). The FIR engages only at N >= 16 with
N % 4 == 0 (10 MSa/s and slower); `effective` then reads e.g. `cic2+fir`, otherwise the result carries `fir_note`. It is the scope's acquisition Filter setting (shared, saved, part of settings_seq); the core-order and
capture-edge flags are kept as they are. Returns `applied`: `setting`, `effective` and `reason` (the firmware
lowers the order where the 48-bit accumulators would not be exact: CIC3 only up to N = 10,334, CIC2 up to
1,050,630; 200 MSa/s and totals over 8190 samples stay legacy), `k`, `n` (= 200 MSa/s / output rate), `shift` S,
`gain` N^K, `codes_per_count` (2^S / N^K), `noise_equivalent_n`, `storage_bits`, `effective_bits_estimate`
(8 + log2 sqrt(noise-equivalent N), capped by the 16-bit storage; assumes the raw noise dithers, >= 0.5 LSB),
`settle_us`, `max_total_samples` (8190), and `ext_latency_shift_samples` with the EXT source; plus the timebase.
Fails with a hint when the trigger total is over 8190 or the bitstream has no filter. Captures, fetches and
`processed` reads then use the 16-bit samples (READ_RAW layout 2, or READ_TRACE WIDE for `processed`; the same u16
values, never the 8-bit trace, so `scope_capture_and_fft` and the averaging tools get the full resolution): `codes` = stored x 2^S / N^K
plus the mean core compensation (fractional codes), volts with the cores' mean zero and gain, results carry
`decimation` and a caveat (window length, resolution, raw clipping inside a window is invisible), the `.npz`
`CH<n>_samples16`. `scope_get_config` shows `decimation` on DECIM bitstreams. Caveats: shared Filter setting, the
legacy cases, the exactness limits, clipping.

**`scope_spectrum`** (tier 0, protocol 1.7, firmware >= 0.2.28). Reads the spectrum the scope's own spectrum
view shows (READ_SPECTRUM, docs/protocol.md section 14 row 45): the on-scope FFT of the latest capture, window and
FFT length as chosen at the scope (N = the largest power of two the record holds, up to the view's length). Takes
no capture. Params: `channel`, `fmin`, `fmax`, `peaks`, `cross_check`. Returns `unit` (dBV, dBA in A mode),
`frame` (spectra computed since boot), `sample_rate`, `divider`, `n_fft`, `bin_width`, `window`, `enbw_bins`,
`rbw_hz`, `gain_db` (20 log10 of the volts per code the scope applied), `peaks` (frequency, level, dBc), the median
noise floor per bin, the bins inline when they fit `inline_max_points`, and a `.npz` (`f`, `level_db`, empty bins
NaN). Fails with a hint when the screen is in the scope view or shows no spectrum of that channel.
`cross_check=true` reads the same capture's trace (checked by the spectrum frame before and after) and recomputes
it twice on the host: `fft` is the on-scope FFT model (codes minus the zero code, same window and N; expect
< 0.1 dB within 60 dB of the peak), `volts` is `scope_capture_and_fft`'s computation on the calibrated volts
(its `median_diff_db` is the scaling difference: the scope uses the firmware's volts per code, the host the
per-unit calibration when there is one). Caveats: 8-bit SFDR, firmware scaling, and "a host capture with a raw
divider is labelled with the UI time base's rate" (known firmware limit).

**`measure`** (tier 0 on an existing capture, 1 if it captures). Items: `vpp`, `vmin`, `vmax`, `mean`,
`rms`, `ac_rms`, `frequency`, `period`, `duty`, `rise_time`, `fall_time`, `delay(CH1->CH2)`. Computed on the
host from raw data with calibration. Caveat on rise time: "rise times below ~12 ns (0.35 / 30 MHz) measure
the scope, not the signal".

**`scope_counter`** (tier 0 reading; tier 1 when `gate` / `source` / `level` / `hysteresis` configure it, since the
scope's measurement cards share the counter; protocol 1.26, caps COUNTER, GET_COUNTER 0x0213). The FPGA's reciprocal
counter: over a gate of 10 ms / 100 ms (boot) / 1 s it counts E periods between the first and the last rising edge of
the source (CH1 / CH2 through a Schmitt comparator at `level` with `hysteresis` codes, or EXT), T and W 5 ns clocks
(period and high time), and the raw min / max over the gate.

| Param | Type | Default | Notes |
|---|---|---|---|
| `gate` | `"10ms"`, `"100ms"`, `"1s"` | as set | configures the gate |
| `source` | `"trigger"`, `"CH1"`, `"CH2"`, `"EXT"` | as set (boot: trigger = follows the trigger source) | |
| `level` | float V | as set (boot: code 128) | comparator level at the probe tip, converted to a raw code (CH1 / CH2) |
| `hysteresis` | int codes | as set (boot: 6) | codes below the level that read low |
| `readings` | int 1..50 | 1 | snapshots, one per new gate (the tool waits for each gate to end) |

Returns `source` (the effective channel), `gate_s`, `frequency_hz` = E / (T x 5 ns), `period_s`, `width_s` (positive),
`duty` = W / T, `edges`, `vmin` / `vmax` / `vpp` (CH1 / CH2: the raw extremes through the 8-bit model's mean of both
cores), `level_code` / `level_volts`, `hysteresis_codes`, every reading, `frequency_mean_hz` / `frequency_spread_hz`
with several, a `.npz` (kind `counter`, `scope_report` plots it) and the `resolution` statement: one 5 ns clock over
the gate (5e-7 relative at 10 ms, 5e-9 at 1 s), the 50 MHz crystal's accuracy (about 1e-5, uncalibrated) on top.
Caveats: no valid count (fewer than two edges: raise the gate, set the level mid-swing), vmin / vmax include the
noise extremes, the thresholds are raw codes. Wire: request u8 what (0 read / 1 configure), gate, source, level,
hysteresis, 3 x 0; response 24 bytes: flags (new, valid, EXT, CH2), seq, gate, source, u32 E, T, W, u8 min, max,
level, hysteresis, u32 freq_mHz. The firmware's implementation (item C7) must match these bytes.

### 4.3b Data tools: decode, mask test, CSV export, DVM (mcp-soft, 2026-10-20)

Host-side work on captured data (roadmap: advanced capabilities are MCP features; the agent presents the data). No
firmware or protocol change; every tool works on the simulator (`--signal uart:...`, `spi_clk:...`, `i2c_scl:...`,
`dc:v=...` with `--noise`). Library: `fnirsi1013d.decode` (decoders), `fnirsi1013d.export` (CSV),
`analysis.mask_envelope` / `mask_check` / `dvm_summary` / `dvm_resolution`.

**`decode`** (`scope_decode`, tier 0 on existing data, tier 1 with `capture=true`). Serial decode of a capture.

| Param | Type | Default | Notes |
|---|---|---|---|
| `protocol` | `uart`, `spi`, `i2c` | | |
| `source` | path or `latest` | none | a saved capture (`.npz`); without it this server's last capture, else the scope's latest capture |
| `capture`, `channels`, `timeout_s` | | | as `scope_capture`; `channels` are the lines: UART `[data]`, SPI `[clock, data]`, I2C `[SCL, SDA]` (defaults CH1 / CH1, CH2) |
| `threshold` | float V | mid-level | logic threshold; 5 % of the swing is hysteresis on each side |
| `baud`, `data_bits`, `parity`, `stop_bits`, `idle` | | auto, 8, none, 1, high | UART; `baud` auto = 1 / the shortest pulse, snapped to a standard rate; `idle='low'` for an inverted line |
| `word_bits`, `cpol`, `cpha`, `msb_first`, `gap` | | 8, 0, 0, true, 2.5 periods | SPI; no chip select on two channels: words are counted from the first clock after a pause longer than `gap` |
| `max_frames` | int | 200 | frames inline (all are in the file) |

Returns `frames` with the time of each relative to the trigger (UART: value, hex, char, `errors` parity /
framing; SPI: words per `transfer`, `incomplete`; I2C: START, `address`, `rw`, `ack`, `data` bytes with their ACK,
`stop` / `repeated_start`, errors such as NACK on the address or stray bits), `n_frames`, `n_errors`, `preview`
lines, `applied` (baud or clock, samples per bit, threshold, levels), `lines` (which channel is which) and a `.npz`
(`t`, `<CH>_volts`, the frames in `meta_json`) that `scope_report` plots with the frames labelled. Caveats: the
samples per bit (at least 4 per bit, 8 per I2C SCL period, for reliable timing; under 2 it refuses with the rate
needed; measured on the simulator: UART decodes down to 2.5 samples per bit, SPI 3 per clock period, I2C 8 per SCL
period), the 8-bit noise band, a record ending inside a frame, auto-trigger data. Maximum rate per sample rate:
UART fs / 4 baud (200 MSa/s: 50 Mbaud, far above the 30 MHz front end; 2 MSa/s: 500 kbaud), SPI clock fs / 4, I2C
SCL fs / 8.

**`mask_test`** (`scope_mask_test`, tier 1: captures). Pass / fail against a mask.

| Param | Type | Default | Notes |
|---|---|---|---|
| `channel` | channel | CH1 | |
| `reference` | `capture`, a capture `.npz`, a mask `.npz` or a stored mask name | `capture` | the mask is built around the reference waveform, or reused as stored |
| `tolerance_volts` / `tolerance_div` | V / divisions | 0.2 div of the live range | amplitude tolerance (ignored with a mask file) |
| `tolerance_time` | s | 2 samples | time tolerance: running min / max over +-k samples |
| `n`, `timeout_s`, `stop_on_fail` | | 10, 2.0, false | captures tested with the live settings |
| `mask_name` | str | none | also store the mask as `<data dir>/masks/<name>.npz` |

Returns per capture `passed`, `fail_samples`, `first_fail` (t, volts, the bound crossed and its limit), the worst
excursion; `passed` / `failed` / `dropped`, `mask.file` (reusable; the dated `.npz` next to the captures, or the
named one), and a result `.npz` (`t`, `upper`, `lower`, `reference`, the first failing capture or the last one as
`<CH>_volts`, `fail_mask`) that `scope_report` plots as the band with the outside samples marked. The mask is bound to
the sample rate and record length: a mismatch is refused with the settings to restore, and a settings change at the
scope ends the run. Caveats: a tolerance under about 2 codes fails on noise alone, under 2 samples on trigger jitter
alone; auto-trigger captures are not synchronised.

**`export_csv`** (`scope_export_csv`, tier 0, touches no scope). `source` (`.npz` path or `latest`), `out` (default
beside the file), `raw` (also the compensated codes and 16-bit filter outputs; the half-length per-core arrays stay in
the `.npz`), `delimiter`, `caveats`. Columns: the time or frequency axis, then every array of that length (volts,
clipped flags, mean / std, fit / residual, spectrum levels, complex values as `_re` / `_im`); segments in long format
with a `segment` column and the per-segment timestamps repeated. The `#` header carries the tool and arguments, the
sample rate, capture id, per-channel V/div, coupling, probe and offset, the trigger, decimation, the scaling note, the
recorded caveats and the column units. The capture tools have no `format` option (there is no such convention in the
server): capture first, export the file.

**`dvm`** (`scope_dvm`, tier 1). Voltmeter readout per channel over `n` captures (1..256, default 8) or `duration_s`
(<= 600 s), with the live settings. Per channel: `dc_mean` (and the spread between captures), `ac_rms`, `rms`, `min`,
`max`, `vpp`, `frequency` (the dominant spectral component, 6x above the median bin, else null), the `range` in
effect (V/div, volts per code, full scale, coupling, probe, `scaling`), `resolution` (`uncertainty_volts` = the larger
of the sample noise over all samples, the spread of the per-capture means and the quantisation floor; `digits` =
log10(full scale / uncertainty); `dither` false when the sample noise is under 0.5 code, then the mean cannot resolve
below one code and the uncertainty is half a code) and a `statement` sentence that puts those together, e.g. "DC
0.2500 V +- 27 uV (1 sigma) on the 100 mV/div range (8-bit, 3.62 mV per code, full scale 928 mV), 24000 samples in 6
captures, sample noise 1.05 codes: 4.5 digits of full scale; absolute accuracy is that of the calibration
(firmware), about one code". So "4-digit DVM" is a resolution claim the tool justifies from the measured noise and
the sample count; the absolute accuracy stays the calibration's (`scope_calibration_info`). Wraps
`scope_measure`'s statistics (`analysis.stats`); a settings change at the scope restarts the run. Saves the
per-capture readings (`capture_t_s`, `<CH>_mean` / `_ac_rms` / `_rms` / `_min` / `_max` / `_frequency`).

### 4.4 Output (tier 3)

**`generator`** (tier 3, needs `confirm_token`, only with cap GENERATOR_DDS).

| Param | Type | Notes |
|---|---|---|
| `enabled` | bool | false tri-states the pin |
| `frequency` | float Hz | <= `FNIRSI_MCP_GEN_MAX_HZ` and <= 25 MHz |
| `duty_percent` | float | <= `FNIRSI_MCP_GEN_MAX_DUTY` |

Returns the actual frequency (11.64 mHz steps), duty and enable state. Caveats: "Square wave only, 3.3 V
logic, about 20 ns edge jitter. Not a sine source and not a power driver: buffer it before driving a MOSFET
gate.", "On the stock FPGA the pin always outputs a fixed 1 kHz and this tool is unavailable."
Disabling (`enabled=false`) does not need a token.

**`bode_sweep`** (`scope_bode_sweep`, tier 3, token, caps DECIM and the generator): frequency response CH2 / CH1 of a
passive DUT on the calibration post; parameters and returns in mcp/README.md "Bode sweep" and the guide's section 3b
(design: docs/plans/bode-design.md). `where='device'` (protocol 1.20, fw 0.2.66): the scope's own engine runs the sweep
(BODE_CTRL action 1 with the settings, or 5 for `thru_cal='measure'`), the tool polls READ_BODE until the state leaves
`running`, reads the 20-byte rows, recomputes f from the DDS period M, maps the flag bits to the host tool's flag names
and returns the same structure as the host sweep (plus `device`: sweep id, state, reason, thru state, view), so the
two compare point by point. Presets only, `ch2_range` auto / fixed, DC; `thru_cal` 'device' applies the scope's stored
thru, a stored name is applied on the host; `close_view` closes the scope's Bode view afterwards (it restores the
scope's settings). While the view shows (`display_mode` `bode`) the capture tools add a caveat. A device sweep ended
with READ_BODE reason 8 (ADC readout fault: the capture self-check flagged a core on 0x153A+, fw 0.2.83; the pre-scan
clipped at 5 V/div on older bitstreams) returns no table and a caveat pointing at `scope_health` and the calibration
wizard. Host sweeps retake a flat or toneless record once (the 0x153A first record after a configuration change);
a point still flat is flagged `ch1_low` + `incoherent` with no H, and a caveat names its frequencies. fw 0.2.99:
`ch2_small` is one of the flags that leave a point out of the markers (BAD_FLAGS, grey on the scope and in
`scope_report`'s plot); when such points sit on CH2's finest range (100 mV/div) the first caveat is the scope's note,
"CH2 too small above 541 kHz to measure: set Stop below it or press AUTO SET. Less pickup: short ground lead on
CH2." with the frequency where that tail begins (`bode.ch2_floor_hz`).

### 4.4a Presenting results (tier 0)

**`report`** (`scope_report`, tier 0, touches no scope): `source` = a result file a tool returned (`.npz`, or a Bode
sweep's `.json`) or `'latest'`; `title`, `command` (shown on the page; default: the tool and the arguments recorded in
the file), `caveats` (added to the recorded ones), `plot`. Writes `<home>/reports/<date>/<stem>.png` (matplotlib,
optional extra `plot`) and `<stem>.html` (plot embedded as base64, table, summary, caveats, command; no scripts, no
external resources, light and dark). Returns both paths, the kind, the summary lines, up to 50 table rows, the caveats
and `how_to_show` (Claude Code: Read the PNG, publish the HTML with the Artifact tool). Kinds: capture, averaged
capture, decay fit, segments, host FFT, the scope's spectrum, Bode (host or device). Library: `fnirsi1013d.report`
(`build_report`, `load_result`), CLI `python -m fnirsi1013d.report <file|latest>`.

**`self_test_loopback`** (tier 3, token). Asks the user (in the description) to connect the calibration
pin to CH1 with the probe. Runs: generator at 10 kHz / 50 % and 1 MHz / 25 %, captures at several rates,
checks frequency, duty and levels against expectations, and runs the protocol bench tests that need a
known signal (protocol section 12, T4/T5/T7). Returns pass/fail per check. Used after every firmware or
bitstream change.

### 4.5 Calibration, screen (tier 0)

**`cal_probe`** (`scope_cal_probe`, tier 2, protocol 1.21 / fw 0.2.67, provisional numbering; diagnostic): CAL_PROBE
runs the calibration wizard's zero measurement of one channel and hardware range (or, `whole_step`, the wizard's whole
zero step on the screen) and returns the verdict (OK, CLIP, NOISE, ZERO, SPREAD, SLOPE, DRIFT, CLASS, TIMING,
READOUT) in words, the failing point / class / core / value, the centre and zero PWM, the zero split, the encode-check
counts during the probe and per point (coarse, gated slope and zero pair, full-rate zero pair and slope) per core the
mean, RMS about the mean (`noisy` above the wizard's limit of the point's class: 3 codes full rate, 1 code gated
since fw 0.2.100, 3 codes before), RMS within a conversion, extremes and clipped samples. Nothing is stored; the tool
stops the UI acquisition for it and sets RUN again. A/B switches for the bus readout: `settle_reads`, `prestrobe`.

**`calibration_info`** (`scope_calibration_info`, tier 0): the conversion in use per channel, the scope's calibration
(v3 block, current correction), from 1.15 `tracking` per channel (the v4 tracker: tracking / frozen / idle, d, e, q,
cm, ages, `in_words`) and from 1.16 `calibration_provenance` (which firmware build and FPGA calibrated, which parts it
wrote, the date with a clock module, power-ons and operating hours since), plus the per-unit file.

**`read_calibration`** (tier 0). Per channel: offset centres, gain factors per range, core A/B
compensation, derived volts per code per range. Caveat: "Calibration is done on the scope's UI; these values
are read-only from here. If volts look wrong, run the scope's base and signal calibration first."

**`screenshot`** (tier 0, cap SCREENSHOT). Returns a PNG image (MCP image content) of the 800x480 screen.
About 0.1-0.3 s [U]. Useful for showing the human what the scope displays in LOCAL mode.

### 4.6 Raw access (tier 4, disabled by default)

**`raw_fpga`** (tier 4). Takes a list of operations (`{"cmd": 0x14}`, `{"write": [..]}`, `{"read": n}`) and
returns the read bytes. For bring-up and FPGA experiments only. Caveat: "Bypasses all validation. Wrong
writes to 0x33/0x36 (relays) or 0x50-0x52 (generator) change hardware; 0x01 resets acquisition."

**`enter_fel`** (tier 4). Reboots the scope into FEL so a new image can be loaded with `xfel`. The serial
port disappears.

## 5. Typical agent workflows

PI decay characterisation with the TX gate on CH2:

1. (Nothing to acquire: the scope is shared with the person at it.)
2. `set_channel(CH2, volts_per_div=1, probe=10, coupling=DC)` (TX gate drive), `set_channel(CH1,
   volts_per_div=0.1, probe=10, coupling=DC)` (coil or preamp output).
3. `set_timebase(sample_rate=20e6)`; `set_trigger(source=CH2, edge=falling, level=1.5, mode=normal,
   pretrigger_fraction=0.05, total_samples=16000)` gives 40 us before and 760 us after the edge.
4. `decay_fit(channel=CH1, edge_channel=CH2, t_start=8e-6, t_end=300e-6, model=exp2, n_avg=256)`.
5. Change the target, repeat 4, compare tau.
6. `acquisition_control(action='run')` if the scope should run on its own again.

VLF RX phase measurement: `set_timebase(window=2e-3)`, trigger on the TX reference on CH2, `averaged_capture
(n=32, channels=[CH1, CH2])`, then `measure(delay(CH1->CH2))` and `capture_and_fft` on both for amplitude and
phase at the operating frequency.

## 6. Host-side processing notes

- Raw to volts: `v = (code_compensated - zero_code) x volts_per_lsb x probe`, where `code_compensated` adds
  `adc_a_comp` / `adc_b_comp` per core and clamps like the firmware (P-7.6). The server implements this in
  numpy and checks it against READ_TRACE in a unit test.
- Time axis: `t[i] = (i - pretrigger_samples) x dt`. Uncertainty of the trigger point: about +-2 samples
  until protocol test T5 has been run, then +-1.
- Edge-aligned averaging resamples each capture by its fractional offset with linear interpolation;
  higher-order interpolation is pointless below ~5 samples per rise time.
- Long jobs (`averaged_capture` with large `n`) report progress through MCP progress notifications and honour
  cancellation by sending STOP.
