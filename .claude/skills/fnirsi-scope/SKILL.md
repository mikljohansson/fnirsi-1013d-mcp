---
name: fnirsi-scope
description: Run measurements on the FNIRSI 1013D oscilloscope through the fnirsi1013d MCP server (scope_* tools). Use when asked to measure on the scope, use the oscilloscope, capture CH1/CH2, look at a waveform, average captures, measure a metal detector / pulse-induction decay or time constant, take an FFT, or use the scope's generator / calibration pin. Covers session start, choosing range and timebase, averaging, the shared screen, generator safety, reading .npz results, reporting caveats and troubleshooting.
---

# Measuring with the FNIRSI 1013D

Full guide: `docs/agent-measurement-guide.md`. Tool reference: `docs/mcp-tools.md` (the live tool descriptions win).
The server is `fnirsi1013d` (real scope) or `fnirsi1013d-sim` (simulator) in `.mcp.json`.

## Start every session

1. `scope_profile`: capabilities of this unit and its standing caveats.
2. `scope_calibration_info`: volts conversion per channel (`unit-calibration` or `firmware`).
3. `scope_status` + `scope_get_config`: mode (collaborative = shared with the person at the scope), run state,
   live settings. Never assume your earlier settings still hold.

## Choose settings for the signal

- Range: fill 60-80 % of the screen (~8 div, 256 codes): `volts_per_div ~ Vpp / 5..6`. A "clipped" caveat means
  raise volts_per_div or move the offset. Set `probe` to the real probe factor; 10x for > ~40 V peak and PI flyback.
- Coupling: DC for levels and decays; AC only for small ripple on DC.
- Sample rate: >= 10 samples per period of the fastest feature, >= 10 per decay time constant, >= 10 MS/s when
  averaging (trigger jitter is 2 / fs). Window = total_samples / fs: 2-10 periods, or the whole decay.
- Trigger on the cleanest edge (TX gate on CH2 for detector work), level mid-edge in volts, mode `normal` for
  repetitive signals (`auto` just to find the signal), `pretrigger_fraction` 0.05-0.1 for decays after an edge.

## Take data

- `scope_capture` = one host capture; the screen shows it and the scope stays in STOP. When done:
  `scope_acquisition_control(action='run')` gives the person a live screen back.
- `scope_fetch_waveform` = what the scope shows now, with the settings of that capture; changes nothing.
- Noise below ~2 LSB or fits: `scope_averaged_capture` (n 16-64 quick, 256+ for fits; `align='edge'` on the TX gate).
- PI decays: `scope_decay_fit` (t_start past the flyback, check r2 and residuals). Spectra: `scope_capture_and_fft`.
- Quick numbers: `scope_measure`.

## The screen is shared

The person at the scope may change settings at any time (last change wins). Long runs restart on a mid-run change
and report `restarts`: pass that on; after repeated restarts ask them to leave the knobs alone. "Modal screen" /
E_BUSY = a menu is open on the scope: ask them to close it. Say what you changed; leave the scope running.

## Safety

- Generator and loopback test (tier 3) drive the calibration pin and need a confirmation token: state frequency,
  duty and where the pin is wired before confirming. **Always end with `scope_generator(enabled=false)`** after a
  loopback test or generator use, and say so.
- Relay changes (tier 2) are rate-limited; do not toggle ranges in loops.
- No raw FPGA / FEL for measurements; never write the SD card from an agent (bench CLI only).

## Results

`.npz` per waveform (`file` in the result): `numpy.load(path)`, metadata `json.loads(str(f["meta_json"]))`.
capture/fetch: `t` (s, 0 = trigger), `CH1_volts`, `CH1_codes`, `CH1_clipped`; averaged: `CH1_mean`, `CH1_std`;
decay_fit: `t` (after the edge), `data`, `fit`, `residual`; fft: `f`, `amplitude_vrms`.

## Report numbers with their caveats

Value + unit + method (n, sample rate, range, alignment), and: 8-bit ADC (~2 LSB noise, `volts_per_lsb` in every
result); max 200 MS/s per channel; ~30 MHz analog bandwidth (rise times < ~12 ns are the scope's); absolute volts
depend on `scaling`; the calibration pin is a frequency/duty/timing reference, not a voltage reference (~3.1 Vpp,
low level ~ -1.2 V on this board). Keep the tool's dynamic caveats (clipping, auto trigger, aliasing, restarts).

## Troubleshooting

| Symptom | Fix |
|---|---|
| E_BUSY / "modal screen" | a menu is open on the scope: ask the person to close it |
| "still starting" | just booted; the server waits up to 10 s, retry once |
| cannot connect / port busy | another session holds the serial port: ask who; never kill it |
| no device | check the COM port (`--port`), power, USB-storage mode, cable |
| no trigger | `scope_get_config`; level inside the signal, right source/edge, or mode `auto` |
| "capability ... not reported" | this firmware lacks the feature |
