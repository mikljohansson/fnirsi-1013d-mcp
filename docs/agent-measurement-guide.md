# Measuring with the FNIRSI 1013D from an agent

How to run a good measurement session through the `fnirsi1013d` MCP server (mcp/). Written for Claude Code (the
`fnirsi-scope` skill condenses it), Codex and any other MCP client. Tool reference: `docs/mcp-tools.md`; live tool
descriptions are authoritative. Server setup: `mcp/README.md`.

## 1. Start of a session

1. `scope_profile`: what this unit can do (FPGA variant, memory depth, generator, which facts are unverified).
   Read its standing caveats once.
2. `scope_calibration_info`: which volts conversion is in effect per channel. `unit-calibration` = measured per-unit
   zero/gain (T8); `firmware` = the firmware formula, absolute DC levels can be off by a few codes.
3. `scope_status`: `mode` collaborative (shared with the person at the scope) or exclusive (legacy firmware),
   `run_state`, `settings_seq`.
4. `scope_get_config`: the live settings. Do not assume your last settings still hold: the person may have changed
   them.

## 2. Setting up for a signal

- **Vertical range** (`scope_set_channel volts_per_div`): the screen is about 8 divisions, 256 codes. Aim for the
  signal to fill 60-80 % of the screen: `volts_per_div ~ Vpp / 5..6`. Smaller wastes resolution (8 bits only);
  larger clips. Any "clipped" caveat means: raise volts_per_div or move the offset. 50 mV/div is a software zoom of
  100 mV/div (no extra resolution).
- **Probe**: set `probe` to the real probe factor (10x for anything over ~40 V peak, PI flyback, mains-derived).
  It only scales numbers; the attenuation is in the probe.
- **Coupling**: DC for anything where levels or slow decays matter (PI decays, logic levels); AC only to look at
  small ripple on a DC level.
- **Sample rate** (`scope_set_timebase sample_rate` or `window`): at least 10 samples per period of the highest
  frequency you care about (fs = 200 MHz / (N + 1), max 200 MS/s per channel); for decays at least 10 samples per
  time constant, and >= 10 MS/s when averaging so the 2-sample trigger jitter stays small. The capture window is
  total_samples / fs (up to 16382 samples on AL3): make it cover 2-10 periods, or the whole decay of interest.
- **Trigger** (`scope_set_trigger`): source on the channel with the clean edge (the TX gate on CH2 for detector
  work), level in volts mid-way on that edge, mode `normal` for repetitive signals, `auto` to see anything at all.
  Put the trigger where you need data: `pretrigger_fraction` 0.05-0.1 for decays after an edge, 0.5 to see both
  sides.

## 3. Taking data

- `scope_capture`: one host capture. The scope screen shows it and the scope stays in STOP afterwards; resume with
  `scope_acquisition_control(action='run')` when you are done, so the person gets a live screen back.
- `scope_fetch_waveform`: what the scope captured last (also its own RUN captures), converted with the settings of
  that capture (`capture_settings`). Changes nothing.
- `scope_averaged_capture`: when the noise matters (signal features below ~2 LSB, decay tails): averaging n captures
  lowers random noise by about sqrt(n). `align='edge'` with `align_channel` on the TX gate removes trigger jitter.
  16-64 for a quick look, 256+ for fits. It reports drops (no trigger, clipped) and `restarts`.
- `scope_decay_fit`: PI decays: averaged, edge-aligned, fit window after the edge (`t_start` past the flyback,
  `t_end` before the noise floor), `exp`, `exp+offset`, `exp2` or `power`. Check `r2`, residuals and caveats.
- `scope_capture_and_fft`: spectra; `flattop` window for amplitudes, `hann` for resolution.
- `scope_measure`: vpp, rms, frequency, duty, rise time, CH1->CH2 delay on the last capture (or a fresh one).

## 4. The screen is shared

On collaborative firmware the person at the scope can change any setting at any time; the last change wins. Every
capture carries the `settings_seq` it was taken under. Long runs (averaging, decay fit, FFT) restart when a
setting changes mid-run and say so in `restarts` / caveats: report it, and if it keeps happening, ask the person
to leave the knobs alone (the tool gives up after 5 restarts). A menu open on the scope makes setting changes fail
with "modal screen" (E_BUSY): ask them to close it. Leave the scope as you found it where reasonable (RUN, the
user's range) and say what you changed.

## 5. Safety

- Tier 2 (`scope_set_channel`) clicks relays; changes are rate-limited. Tier 3 (`scope_generator`,
  `scope_self_test_loopback`) drives the calibration pin and needs a confirmation token: say what will happen
  (frequency, duty, where the pin is wired) before confirming.
- **Never leave the generator on**: after a loopback test or a generator experiment call
  `scope_generator(enabled=false)` (no token needed) and confirm it in your report. If the pin is wired into a
  detector it switches real coil current.
- Tier 4 (raw FPGA, FEL) is off unless the operator enabled it; do not use it for measurements.
- Writing the SD card (firmware persist) is a bench CLI action (`fnirsi1013d-flash --persist`), never an agent tool.

## 6. Reading the files

Every waveform is saved as `.npz` (path in `file`); load with `numpy.load(path)`; metadata in
`json.loads(str(f["meta_json"]))`.

| Tool | Arrays |
|---|---|
| capture / fetch | `t` (s, 0 = trigger), `CH1_volts`, `CH1_codes`, `CH1_clipped` (and `_raw_a` / `_raw_b` with raw=true) |
| averaged_capture | `t`, `CH1_mean`, `CH1_std`, `CH1_clipped_any`, `edge_shifts_samples` |
| decay_fit | `t` (s after the edge), `data`, `fit`, `residual`, `fit_mask`, `excluded`, `std` |
| capture_and_fft | `f` (Hz), `amplitude_vrms` |

Results with up to 2000 points also carry the samples inline; longer ones a 500-point min/max preview.

## 7. Reporting numbers

Give value, unit and how it was measured (n averaged, sample rate, range, alignment), plus the caveats that apply:

- 8-bit ADC, ~2 LSB noise per sample; volts per code is in every result (`volts_per_lsb`).
- 200 MS/s per channel at most; trigger jitter up to 2 / fs without edge alignment.
- Analog bandwidth ~30 MHz: rise times below ~12 ns and content above 30 MHz measure the scope.
- Absolute volts: say whether `scaling` was `unit-calibration` or `firmware`.
- The calibration pin is a frequency / duty / timing reference, not a voltage reference: on this board it is a
  ~3.1 Vpp square wave whose low level sits about -1.2 V at the probe (capacitively coupled).
- Pass on dynamic caveats (clipping, auto trigger fired, aliasing, restarts) instead of dropping them.

## 8. Troubleshooting

| Symptom | Meaning / fix |
|---|---|
| "modal screen", E_BUSY | a menu is open on the scope: ask the person to close it, retry |
| "still starting" | the scope just booted (about 5 s after a FEL load); the server waits up to 10 s |
| cannot connect, port busy / PermissionError | another session holds the serial port (only one may): ask who, never kill it |
| no device found | wrong COM port (`--port`), scope off or in USB-storage mode, cable; `fnirsi1013d-bench --census 0` checks the link |
| "no trigger within ..." | level outside the signal, wrong source or edge: `scope_get_config`, try mode `auto` |
| all captures dropped as clipped | raise volts_per_div or set `reject_clipped=false` deliberately |
| "capability ... not reported" | this firmware build lacks the feature (screenshot, device averaging, FORCE, SD) |
| "released by user" | legacy firmware only: the person tapped the REMOTE banner; ask before continuing |
