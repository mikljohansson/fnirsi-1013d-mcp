# MCP tool surface for the FNIRSI 1013D

Status: design draft, 2026-10-18. Wire protocol: `docs/protocol.md` (cited as P-section). Firmware side:
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
  written to an `.npz` file (time vector, volts, raw codes, metadata) under `mcp/data/<date>/`. The tool
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
| 0 | READ_ONLY | nothing | `scope_info`, `scope_profile`, `scope_status`, `get_config`, `fetch_waveform` (LOCAL), `read_calibration`, `screenshot` | allowed |
| 1 | ACQUIRE | FPGA acquisition registers only (sample rate, trigger, memory geometry, arming); no effect outside the scope; takes the UI away from the person at the scope | `remote_session`, `set_timebase`, `set_trigger`, `capture`, `acquisition_control`, `averaged_capture`, `capture_and_fft`, `decay_fit`, `measure` | allowed |
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
| `FNIRSI_MCP_DATA_DIR` | `mcp/data` | where `.npz` files go |
| `FNIRSI_MCP_AUDIT_LOG` | `mcp/logs/audit.jsonl` | every tier 1+ call: time, tool, args, applied values, result status |

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

**`scope_info`** (tier 0). No parameters. Returns firmware version and build, protocol version, FPGA version
word and variant name, serial, capability flags as names, ring and memory limits.
Description caveat: "If fpga_variant is 'stock', pre-trigger and total samples are fixed (1500 / 3000) and
there is no generator."

**`scope_profile`** (tier 0). No parameters. Returns section 3 merged with live values (current memory
limit, which bitstream, which caps are unverified: `read_lag`, interleave order, FORCE).

**`scope_status`** (tier 0). No parameters. Returns `control` (LOCAL/REMOTE), `acq_state`, `capture_id`,
`trigger_addr`, time since arm, battery level and charging, `exit_reason` if the scope went back to local
on its own (user tap, watchdog, port closed).

**`remote_session`** (tier 1). Takes the scope from the UI or gives it back.

| Param | Type | Default | Notes |
|---|---|---|---|
| `action` | `"acquire"` / `"release"` | | |
| `keep_settings` | bool | false | on release, keep the host's settings in the UI instead of restoring the user's |
| `show_traces` | bool | false | draw each capture on screen (slower) |
| `watchdog_s` | int | 15 | scope returns to local if the server goes silent this long; the server sends keep-alives |

Description caveat: "While acquired, the scope's touch UI is frozen; a tap on the banner hands control back
to the person at the scope, and the next tool call will then fail with 'scope released by user'." Every tier
1+ tool calls `acquire` implicitly when needed and reports that it did.

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

Returns applied values, including the level actually reachable (codes are about 3.4 mV apart at 100 mV/div)
and the pre/post window durations. Caveats: "Level is an 8-bit code compared to core A only; edges faster
than 2 / fs may be missed." "Hold-off is not implemented in any current FPGA bitstream." "On the stock FPGA
pre-trigger is fixed at 50 % and total at 3000."

### 4.3 Acquisition (tier 1)

**`capture`** (tier 1). Arms, waits, reads.

| Param | Type | Default | Notes |
|---|---|---|---|
| `channels` | list | enabled ones | |
| `timeout_s` | float | 2.0 | normal mode waits this long for a trigger |
| `window` | `[t0, t1]` s relative to the trigger | whole capture | reads only this part (faster) |
| `raw` | bool | false | also store raw codes per core |
| `processed` | bool | false | use the firmware's processed trace instead of raw reads plus host compensation (slower, matches the screen) |

Returns per channel: min, max, mean, rms, Vpp, frequency estimate, clipped-sample count; `trigger`
(`fired` / `auto_timeout` / `none`), `capture_id`, dt, sample count, file path, preview. Dynamic caveats:
"N samples clipped at 0/255: increase volts_per_div", "auto trigger fired: data not synchronised",
"signal frequency estimate X is above fs/2.5: aliasing likely", "frequency > 30 MHz: above analog bandwidth".

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

Returns mean waveform (file), per-sample standard deviation (file), `n_used`, `n_dropped` with reasons,
estimated noise per sample before/after, effective resolution estimate, timing (captures per second).
Caveats: "Averaging reduces random noise by sqrt(n) but not the A/B interleave mismatch, the 8-bit
quantisation of a noise-free signal, or trigger jitter (up to 2 / fs with hardware alignment).",
"edge alignment needs a clean edge on align_channel with at least 3 samples on the slope".

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

**`measure`** (tier 0 on an existing capture, 1 if it captures). Items: `vpp`, `vmin`, `vmax`, `mean`,
`rms`, `ac_rms`, `frequency`, `period`, `duty`, `rise_time`, `fall_time`, `delay(CH1->CH2)`. Computed on the
host from raw data with calibration. Caveat on rise time: "rise times below ~12 ns (0.35 / 30 MHz) measure
the scope, not the signal".

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

**`self_test_loopback`** (tier 3, token). Asks the user (in the description) to connect the calibration
pin to CH1 with the probe. Runs: generator at 10 kHz / 50 % and 1 MHz / 25 %, captures at several rates,
checks frequency, duty and levels against expectations, and runs the protocol bench tests that need a
known signal (protocol section 12, T4/T5/T7). Returns pass/fail per check. Used after every firmware or
bitstream change.

### 4.5 Calibration, screen (tier 0)

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

1. `remote_session(acquire)`.
2. `set_channel(CH2, volts_per_div=1, probe=10, coupling=DC)` (TX gate drive), `set_channel(CH1,
   volts_per_div=0.1, probe=10, coupling=DC)` (coil or preamp output).
3. `set_timebase(sample_rate=20e6)`; `set_trigger(source=CH2, edge=falling, level=1.5, mode=normal,
   pretrigger_fraction=0.05, total_samples=16000)` gives 40 us before and 760 us after the edge.
4. `decay_fit(channel=CH1, edge_channel=CH2, t_start=8e-6, t_end=300e-6, model=exp2, n_avg=256)`.
5. Change the target, repeat 4, compare tau.
6. `remote_session(release)`.

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
