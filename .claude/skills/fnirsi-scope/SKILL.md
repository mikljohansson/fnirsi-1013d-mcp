---
name: fnirsi-scope
description: Run measurements on the FNIRSI 1013D oscilloscope through the fnirsi1013d MCP server (scope_* tools). Use when asked to measure on the scope, use the oscilloscope, capture CH1/CH2, look at a waveform, average captures, measure a metal detector / pulse-induction decay or time constant, take an FFT, measure a frequency response / Bode plot of a filter or tank, decode a serial bus (UART, SPI, I2C) from a capture, test waveforms against a mask (pass / fail), read voltages like a DVM (DC, AC RMS, true RMS), export a capture as CSV, trigger on a pulse width / the Nth edge / a timeout or set a trigger hold-off, measure a frequency or duty cycle with the hardware counter, catch glitches with peak detect, log a signal continuously for minutes or hours (streaming logger), chirp the generator, or use the scope's generator / calibration pin, or plot / present a scope result. Covers session start, choosing range and timebase, averaging, the shared screen, generator safety, reading .npz results, reporting caveats and troubleshooting.
---

# Measuring with the FNIRSI 1013D

Full guide: `docs/agent-measurement-guide.md`. Tool reference: `docs/mcp-tools.md` (the live tool descriptions win).
The server is `fnirsi1013d` (real scope) or `fnirsi1013d-sim` (simulator) (`.mcp.json`, see `.mcp.json.example`).

## Start every session

1. `scope_profile`: capabilities of this unit and its standing caveats.
2. `scope_calibration_info`: volts conversion per channel (`scope calibration v3 ...`, `unit-calibration` or
   `firmware`). Only the scope's calibration v3 is decoded: a v2 block (fw 0.2.43-0.2.44) is refused with a message
   to update the firmware and re-run the base calibration, which the person does at the scope (probes and USB
   unplugged for the zero step, both x1 probes on the calibration post for the gain step). Ask; do not skip it.
3. `scope_status` + `scope_get_config`: mode (collaborative = shared with the person at the scope), display mode
   (scope, spectrum, waterfall, density, phosphor, bode), run state, live settings. Never assume your earlier settings
   still hold.

## Choose settings for the signal

- Range: fill 60-80 % of the screen (~8 div, 256 codes): `volts_per_div ~ Vpp / 5..6`. A "clipped" caveat means
  raise volts_per_div or move the offset. Set `probe` to the real probe factor; 10x for > ~40 V peak and PI flyback.
- Coupling: DC for levels and decays; AC only for small ripple on DC.
- Sample rate: >= 10 samples per period of the fastest feature, >= 10 per decay time constant, >= 10 MS/s when
  averaging (trigger jitter is 2 / fs). Window = total_samples / fs: 2-10 periods, or the whole decay.
- Trigger on the cleanest edge (TX gate on CH2 for detector work), level mid-edge in volts, mode `normal` for
  repetitive signals (`auto` just to find the signal), `pretrigger_fraction` 0.05-0.1 for decays after an edge.
- Trigger types (caps `TRIG_TYPES`, guide 3c): `scope_set_trigger(type='pulse_width', polarity, width, longer)`,
  `type='nth_edge'` (`n`, `idle`), `type='timeout'` (`polarity`, `width`); widths in seconds, rounded to one
  trigger-stream sample (2 / fs legacy, 1 / fs filtered, 5 ns EXT: the result says). `hold_off` (seconds) skips
  the pulses of a train. Runt / window need the 0x153E bitstream.

## Take data

- `scope_capture` = one host capture; the screen shows it and the scope stays in STOP. When done:
  `scope_acquisition_control(action='run')` gives the person a live screen back.
- `scope_fetch_waveform` = what the scope shows now, with the settings of that capture; changes nothing.
- Noise below ~2 LSB or fits: `scope_averaged_capture` (n 16-64 quick, 256+ for fits; `align='edge'` on the TX gate;
  `hardware=true` on a 0x1536 bitstream averages in the FPGA, fast, hardware alignment only, not yet benched).
- Many triggers with timestamps (pulse trains, per-pulse PI decays): `scope_capture_segments` (0x1536; set the record
  length with `scope_set_trigger` first).
- PI decays: `scope_decay_fit` (t_start past the flyback, check r2 and residuals). Spectra: `scope_capture_and_fft`; what the scope's own spectrum view shows: `scope_spectrum`.
  Below 200 MSa/s on a DECIM bitstream, `scope_set_decimation` (filter off, boxcar, cic2, cic3, plus the +fir
  variants on a 0x1535 bitstream; cic2 for decays) gives 16-bit, anti-aliased samples (guide section 3a); turn it
  off when done. `scope_spectrum` works while the screen shows the spectrum, waterfall or density view.
  Captures add a caveat while a spectrum or phosphor view is on: pass it on.
- Frequency, period, width, duty at 5 ns resolution without a capture: `scope_counter` (caps `COUNTER`; quote its
  `resolution`; set `level` mid-swing; vmin / vmax include noise peaks). Glitches shorter than the sample period
  at slow rates: `scope_capture(peak=true)` (caps `PEAK_DETECT`; min / max envelopes, no averaging with it).
- Quick numbers: `scope_measure`. Voltmeter-style readings over several captures: `scope_dvm` (quote its
  `statement`: range, volts per code, noise, uncertainty and digits; absolute accuracy is the calibration's).
- Serial bus: `scope_decode(protocol='uart'|'spi'|'i2c')` on the last / a saved / a fresh capture (guide section 3:
  lines on CH1 (+CH2), >= 4 samples per bit, 8 per I2C SCL period; report frames, errors and the measured baud).
- Pass / fail: `scope_mask_test` (band around a reference capture, `tolerance_div` 0.2 and 2 samples to start,
  `mask_name` to reuse; keep the settings as when the mask was built). CSV of any result: `scope_export_csv`.
- Frequency response of a passive DUT on the calibration post: `scope_bode_sweep` (guide section 3b). Wiring
  before anything: CH1 tip on the DUT input, CH2 on the output, DUT ground on the probe ground clips (wiring A;
  USB-shell ground only with `ch2_coupling='ac'`). Thru calibration first (`thru_cal='measure'`, both probes on the DUT input), then the DUT with that name.
  Report f_actual, the markers, the calibration used, and leave flagged points (near_floor, clipped, unsettled, ch2_small) out.
  `where='device'` (fw 0.2.66+) lets the scope run the sweep and show the plot itself (same result structure; presets
  only, ch2_range auto/fixed, DC; the view stays open unless `close_view=true`).

## The screen is shared

The person at the scope may change settings at any time (last change wins). Long runs restart on a mid-run change
and report `restarts`: pass that on; after repeated restarts ask them to leave the knobs alone. A menu open on
the scope is closed by the next request (fw 0.2.78+; the caveat says so: tell the person); on older firmware
"modal screen" / E_BUSY = ask them to close it. Say what you changed; leave the scope running.

## Grounds

The probe ground (BNC shells) is VRF1, about +1.24 V above the scope's USB/digital ground (fpga/al3/README.md).
- The calibration output reading about -1.24 / +1.9 V against the probe ground is by design.
- Earthed device under test plus earthed PC = ground loop through R71: VRF1 sags (about 1.1 V, calculated) and every
  trace shifts. Ask to float one side (battery DUT or laptop, isolated USB); check with a shorted-input zero with
  and without the device connected.
- EXT trigger sources: 3.3 V logic referenced to the USB ground; if they share the probe ground, a red LED (or
  3 x 1N4148) in series and the rising edge. Never connect the calibration pin to a coil, drain or damping resistor.

## Safety

- Generator and loopback test (tier 3) drive the calibration pin and need a confirmation token: state frequency,
  duty and where the pin is wired before confirming. **Always end with `scope_generator(enabled=false)`** after a
  loopback test or generator use, and say so.
- Relay changes (tier 2) are rate-limited; do not toggle ranges in loops.
- No raw FPGA / FEL for measurements; never write the SD card from an agent (bench CLI only).

## Results

`.npz` per waveform (`file` in the result): `numpy.load(path)`, metadata `json.loads(str(f["meta_json"]))`.
capture/fetch: `t` (s, 0 = trigger), `CH1_volts`, `CH1_codes`, `CH1_clipped`; averaged: `CH1_mean`, `CH1_std`;
segments: `CH1_volts` (segments x samples), `timestamps_s`, `triggered`; decode: the lines plus the frames in `meta_json`;
mask_test: `upper`, `lower`, `reference`, `fail_mask`; dvm: per-capture `CH1_mean` etc. over `capture_t_s`;
decay_fit: `t` (after the edge), `data`, `fit`, `residual`; fft: `f`, `amplitude_vrms`.

## Presenting results

For anything the person should look at: `scope_report(source=<the file the tool returned> or 'latest', title=...)`
writes a PNG plot and a self-contained HTML page (plot, table, caveats, command; offline, no CDNs) under
`<home>/reports/<date>/`. In Claude Code, Read the PNG (it renders) and describe what it shows; publish the HTML with
the Artifact tool (private by default; offer the link) or give the path. No matplotlib: `png` is null, the page has
the numbers. Always put the numbers and caveats in the reply as well (guide section 7a).

## Report numbers with their caveats

Value + unit + method (n, sample rate, range, alignment), and: 8-bit ADC (~2 LSB noise, `volts_per_lsb` in every
result); max 200 MS/s per channel; ~30 MHz analog bandwidth (rise times < ~12 ns are the scope's); absolute volts
depend on `scaling`; the calibration pin is a frequency/duty/timing reference, not a voltage reference (~3.1 Vpp,
referenced to USB ground, so -1.24 / +1.9 V against the probe ground). Keep the tool's dynamic caveats (clipping, auto trigger, aliasing, restarts).

## Troubleshooting

| Symptom | Fix |
|---|---|
| E_BUSY / "modal screen" | older firmware (< 0.2.78): a menu is open, ask the person to close it. 0.2.78+: the text says why (calibration measuring, file save, USB drive mode): wait and retry |
| scope restarted, link dropped, settings lost, which firmware? | fw 0.2.52+: `scope_health` (in_words: USB state, power-on, previous end, restart record) and `scope_sd_info` `card_image` (what the card boots vs what runs); `scope_save_report` writes a report file on the card for the person |
| volts slightly off at 200 MSa/s / filtered, hours after calibrating | `scope_calibration_info`: `tracking` (fw 0.2.50+ v4 tracker state) and `calibration_provenance` (when, by which build) |
| "still starting" | just booted; the server waits up to 10 s, retry once |
| the scope's calibration wizard fails its zero step | fw 0.2.67+: `scope_cal_probe(channel, range_index)` shows what the wizard measured (inputs at 0 V) |
| 200 MSa/s capture with one core garbled, or "accepted garbled" caveat | fw 0.2.62+: `scope_health` `encode_check` and `scope_event_log`; repeat the capture; 0x1538 bitstream fixes 0x1532..0x1537 |
| screen shows a Bode plot, the scope does not acquire | its Bode view is open: Menu > Scope at the scope, or `scope_bode_sweep(where='device', close_view=true)` |
| cannot connect / port busy | another session holds the serial port: ask who; never kill it |
| no device | check the COM port (`--port`), power, USB-storage mode, cable |
| no trigger | `scope_get_config`; level inside the signal, right source/edge, or mode `auto` |
| "capability ... not reported" | this firmware lacks the feature |
| first command after a long idle fails (port error, Windows error 22, device re-enumerates) | fixed in fw 0.2.44 (USB suspend is a state, not a reset); on older firmware reopen the port once. If it persists on 0.2.44+, check Windows USB selective suspend (`fnirsi1013d-soak --mode reopen` prints its state; disable it with `powercfg /SETACVALUEINDEX SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0` then `powercfg /SETACTIVE SCHEME_CURRENT`, or Device Manager > USB Root Hub > Power Management) and tell the person |
| the scope's serial port is gone after a long idle (no COM port, no new device; the scope screen is alive) | the host suspended the device and then dropped it (seen 2026-10-19 on fw 0.2.48 after 1 h 40 min with the port closed; the device never sees a RESUME interrupt). fw 0.2.49 reconnects by itself: within about 10 s after a host's failed retries, at the latest 15 min into a suspend (GET_LINK_STATS `usb_soft_reconnects`, event `usb_soft_disconnect`); wait for the port to come back. On older firmware, or if it does not come back, ask the person to unplug and replug the USB cable; afterwards read GET_EVENT_LOG and GET_LINK_STATS and report the suspend / resume / soft-disconnect events |
