# Measuring with the FNIRSI 1013D from an agent

How to run a good measurement session through the `fnirsi1013d` MCP server (mcp/). Written for Claude Code (the
`fnirsi-scope` skill condenses it), Codex and any other MCP client. Tool reference: `docs/mcp-tools.md`; live tool
descriptions are authoritative. Server setup: `mcp/README.md`.

## 1. Start of a session

1. `scope_profile`: what this unit can do (FPGA variant, memory depth, generator, which facts are unverified).
   Read its standing caveats once.
2. `scope_calibration_info`: which volts conversion is in effect per channel. The source string is `scope
   calibration v3 (full|gated clock)` (the scope's own per-core, per-class zeros and gains, after its base
   calibration on firmware >= 0.2.45), `unit-calibration` (measured per-unit zero/gain, T8; with v3 in use it only
   contributes the gated per-core gains) or `firmware` (the firmware formula; absolute DC levels can be off by a few
   codes). Only calibration v3 is decoded: if the scope still holds a v2 block (firmware 0.2.43-0.2.44) the volts
   model refuses it and the error says to update the firmware and run the base calibration again. That is done at
   the scope by the person: System settings, Base calibration, with the probes and USB unplugged for the zero step,
   then both x1 probes on the calibration post for the gain step. An agent cannot do it; ask. With no calibration
   block (version 1) the readings use the 1.00o5 compensations until it is run.
3. `scope_status`: `mode` collaborative (shared with the person at the scope) or exclusive (legacy firmware),
   `display_mode` (`scope`, `spectrum`, `waterfall`, `density` or `phosphor`, protocol modes 0-4), `run_state`,
   `settings_seq`.
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
- `scope_spectrum`: what the scope's own spectrum view shows (firmware >= 0.2.28), when the person has it open
  (`scope_status` `display_mode` = `spectrum`, `waterfall` or `density`; it fails with a hint otherwise, also in the
  `phosphor` view, which has no spectrum). Cheap, takes no capture, single shot, firmware scaling. Use it to
  talk about the same picture the person sees; use `scope_capture_and_fft` for averaging, other windows or the
  per-unit calibration. `cross_check=true` confirms both agree on the same capture.
- `scope_measure`: vpp, rms, frequency, duty, rise time, CH1->CH2 delay on the last capture (or a fresh one).

## 3a. Choosing a decimation mode for decay curves

On firmware >= 0.2.30 with the 0x1534 bitstream or later (`scope_info` caps list `DECIM`), `scope_set_decimation`
(`filter` = `off`, `boxcar`, `cic2`, `cic3`, and on 0x1535 with caps `FIR` also `boxcar+fir`, `cic2+fir`,
`cic3+fir`) makes the FPGA filter every raw 200 MSa/s sample down to the chosen rate instead of skipping samples,
and store 16 bits.
Below 200 MSa/s that is free resolution and anti-aliasing; the cost is a total of at most 8190 samples (set
`scope_set_trigger total_samples` first) and that it is the scope's own Filter setting (tell the person, turn it
`off` when done).

- **The decay constant is not biased.** Boxcar and CIC are linear filters and an exponential stays an exponential
  with the same tau after any of them; only the amplitude scales and the first K output samples after a sharp
  edge (TX turn-off, flyback) are smeared. Start the fit at least K samples (`t_start >= K x dt`) past the flyback.
  The sample time is self-consistent with the ADC trigger; with the EXT trigger the firmware corrects the pipeline
  latency to the nearest output sample (`ext_latency_shift_samples`).
- **FIR (FPGA 0x1535, `+fir`)**: needs the `FIR` capability (on 0x1534 the call is refused). `cic2+fir` flattens the passband to fs / 4 and rejects aliases about 20 dB more,
  at a little less noise reduction and a longer edge transient (the 15-tap FIR spans 7 output samples on each
  side of a sharp edge: start fits at least 8 samples after it). Use it for spectra and waveform shape; for tau
  fits plain `cic2` keeps the edge transient short. It engages at 10 MSa/s and slower only.
- **Which filter**: `cic2` is the default for decays (good alias rejection, 1.5 N noise-equivalent averaging, a
  2-sample edge transient). `boxcar` when the samples right after the edge matter most (1-sample transient, N
  noise-equivalent). `cic3` for spectra and narrow-band work (best alias rejection, about 1.8 N, 3-sample
  transient; exact only up to N = 10,334, i.e. down to about 19.4 kSa/s, below that the firmware uses CIC2 and
  says so in `reason`).
- **Rate**: as before, at least 10 samples per time constant; then the filter averages all N = 200 MSa/s / rate raw
  samples per output. Averaging captures (`scope_averaged_capture`) on top still lowers the noise by sqrt(n); with
  16-bit samples the 8-bit quantisation no longer limits it.
- Check the range with the filter `off` first: a clipped raw sample inside a filter window does not show in the
  output.
- The scope's own calibration and auto setup always run with the filter off. At the scope, memory depths of 9000
  and more are hidden and reduced to 6000 while the filter is on; the host limit is 8190 samples. CIC3 is
  downgraded to CIC2 below about 19.4 kSa/s (N > 10,334).
- Every capture tool reads the 16-bit samples themselves (READ_RAW layout 2, the same u16 values READ_TRACE WIDE
  returns), so host spectra are not limited by the 8-bit trace the screen uses.

Bench, owner's unit, FPGA 0x1534 with fw 0.2.34, 1 MSa/s (N = 200; S = 0 / 8 / 15 as row 47 says):

| | legacy | boxcar | CIC2 | CIC3 |
|---|---|---|---|---|
| DC noise, std in 8-bit codes | 0.86 | 0.19 | 0.11 | 0.12 |
| alias of a 900 kHz square, dBV (8-bit trace path) | +2.4 | -17.5 | -30.4 | -35.7 |

The 1 kHz fundamental was unchanged within 0.1 dB, and the core A/B spur at Nyquist fell from -27 to -88 dBV. The
alias figures came through the 8-bit trace path, which limits the CIC2 / CIC3 values by quantisation. To repeat them
at full resolution, set up the same signal at the same rate, then call `scope_set_decimation` and
`scope_capture_and_fft` (window `flattop`) once per filter, and read the alias line in `peaks` against the
fundamental.

Effective bits (re the 8-bit range) vs N, estimate `8 + log2 sqrt(noise-equivalent N)` with dithering raw noise
(the unit has about 1-2 LSB), capped by the 16-bit storage (2^S / N^K codes per count):

| N (output rate) | boxcar | CIC2 | CIC3 |
|---|---|---|---|
| 2 (100 MSa/s) | 8.5 | 8.7 | 8.8 |
| 10 (20 MSa/s) | 9.7 | 9.9 | 10.1 |
| 20 (10 MSa/s) | 10.2 | 10.5 | 10.6 |
| 100 (2 MSa/s) | 11.3 | 11.6 | 11.8 |
| 200 (1 MSa/s) | 11.8 | 12.1 | 12.3 |
| 1000 (200 kSa/s) | 13.0 | 13.3 | 13.4 |
| 10000 (20 kSa/s) | 14.6 | 14.9 | 15.1 |
| 20000 (10 kSa/s) | 15.1 | 15.4 | CIC2: 15.4 |
| 100000 (2 kSa/s) | 15.6 (storage) | 15.2 (storage) | CIC2: 15.2 |

## 4. The screen is shared

On collaborative firmware the person at the scope can change any setting at any time; the last change wins. Every
capture carries the `settings_seq` it was taken under. Long runs (averaging, decay fit, FFT) restart when a
setting changes mid-run and say so in `restarts` / caveats: report it, and if it keeps happening, ask the person
to leave the knobs alone (the tool gives up after 5 restarts). A menu open on the scope makes setting changes fail
with "modal screen" (E_BUSY): ask them to close it. Leave the scope as you found it where reasonable (RUN, the
user's range) and say what you changed.

## 5. Grounds: the probe ground is not the USB ground

The BNC shells (probe ground clips) sit on VRF1, a TL432 reference about +1.24 V above the scope's digital ground
(USB connector shell, SD card), through R71 (100 Ohm) (fpga/al3/README.md "Changes" 2). Consequences:

- Never assume probe ground = USB ground. The calibration output reading about -1.24 / +1.9 V against the probe
  ground is this offset by design, not an error.
- Ground loops shift every trace: if the device under test is earthed and the PC is earthed (USB), a current flows
  through R71 (about 12 mA, calculated) and VRF1 sags (to about 1.1 V, calculated), moving all channels. Ask the
  user to float one side (battery-powered device under test, battery laptop, or an isolated USB link), and check
  by comparing a shorted-input zero (`scope_fetch_waveform` mean) with and without the device connected.
- EXT trigger (bitstreams 0x1533+): reference the source to the USB ground (connector shell); 3.3 V logic only.
  A source that shares the probe ground idles at ~1.24 V at the pin and is not recognised: insert a red LED or
  3 x 1N4148 in series (anode at the source) and trigger on the rising edge.
- Never connect the calibration pin to a detector coil, drain or damping resistor: the flyback is hundreds of
  volts.

## 5a. Safety

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
| capture / fetch | `t` (s, 0 = trigger), `CH1_volts`, `CH1_codes`, `CH1_clipped` (and `_raw_a` / `_raw_b` with raw=true; decimated captures: `CH1_samples16`, codes are fractional) |
| averaged_capture | `t`, `CH1_mean`, `CH1_std`, `CH1_clipped_any`, `edge_shifts_samples` |
| decay_fit | `t` (s after the edge), `data`, `fit`, `residual`, `fit_mask`, `excluded`, `std` |
| capture_and_fft | `f` (Hz), `amplitude_vrms` |

Results with up to 2000 points also carry the samples inline; longer ones a 500-point min/max preview.

## 7. Reporting numbers

Give value, unit and how it was measured (n averaged, sample rate, range, alignment), plus the caveats that apply:

- 8-bit ADC, ~2 LSB noise per sample; volts per code is in every result (`volts_per_lsb`).
- 200 MS/s per channel at most; trigger jitter up to 2 / fs without edge alignment.
- Analog bandwidth ~30 MHz: rise times below ~12 ns and content above 30 MHz measure the scope.
- Absolute volts: say which `scaling` applied (`scope calibration v3 ...`, `unit-calibration` or `firmware`).
- While the scope shows its spectrum, waterfall or phosphor view the capture tools add a caveat. The data are
  unaffected, but the person sees a different picture than your trace: pass the caveat on.
- The calibration pin is a frequency / duty / timing reference, not a voltage reference: it is a ~3.1 Vpp square
  wave referenced to the USB ground, so against the probe ground (VRF1, +1.24 V) it reads about -1.24 / +1.9 V.
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
