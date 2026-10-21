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

The port is free while no tool runs: the server closes the scope's serial port about 30 s after the last tool call
(`FNIRSI_MCP_IDLE_CLOSE_S`, 0 = hold) and reopens it on the next one, so the person's own scripts and the flash tool
can use it in between. A tool that fails with a port error (access denied, port busy) right after a pause usually
means another program has the port: ask the person, do not retry in a loop.

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
  16-64 for a quick look, 256+ for fits. It reports drops (no trigger, clipped) and `restarts`. `hardware=true`
  (`scope_status` `features` lists `hw_average`) sums the records in the FPGA in one run:
  much faster for many records, but only with `align='hardware'`, records up to 2046 words (4092 samples 8-bit) and
  n <= 257 for 8-bit samples, and without the per-sample std or clipped-record rejection. It is not yet verified
  on the unit: say so when you report numbers from it.
- `scope_capture_segments` (`features` lists `segments`): many triggers in one go, each record with a 5 ns
  timestamp, the FPGA re-arming itself in well under a microsecond plus `holdoff`. Use it for pulse trains, bursts and
  per-pulse detector captures (timing jitter between pulses, the decay of every pulse). Set the record length with
  `scope_set_trigger` first (short records give more segments: 8192 / slot words); `circular=true` keeps the latest
  ones until `timeout_s`. Untriggered segments (auto mode) are flagged. Not yet verified on the unit.
- `scope_decay_fit`: PI decays: averaged, edge-aligned, fit window after the edge (`t_start` past the flyback,
  `t_end` before the noise floor), `exp`, `exp+offset`, `exp2` or `power`. Check `r2`, residuals and caveats.
- `scope_capture_and_fft`: spectra; `flattop` window for amplitudes, `hann` for resolution.
- `scope_spectrum`: what the scope's own spectrum view shows (firmware >= 0.2.28), when the person has it open
  (`scope_status` `display_mode` = `spectrum`, `waterfall` or `density`; it fails with a hint otherwise, also in the
  `phosphor` view, which has no spectrum). Cheap, takes no capture, single shot, firmware scaling. Use it to
  talk about the same picture the person sees; use `scope_capture_and_fft` for averaging, other windows or the
  per-unit calibration. `cross_check=true` confirms both agree on the same capture.
- `scope_measure`: vpp, rms, frequency, duty, rise time, CH1->CH2 delay on the last capture (or a fresh one).
- **Decoding a serial bus**: `scope_decode(protocol='uart'|'spi'|'i2c')` on a saved capture (`source`), the last
  capture, or a fresh one (`capture=true`). Put the lines on the channels it expects (UART: data on CH1; SPI: clock on
  CH1, MOSI or MISO on CH2; I2C: SCL on CH1, SDA on CH2; or name them in `channels`), a range where the logic swing
  fills a few divisions without clipping (1 V/div for 3.3 V logic), DC coupling, and a sample rate of at least 4
  samples per bit (UART: 4 x baud; SPI: 4 x the clock; I2C: 8 x SCL) with a record long enough for the whole message.
  Trigger on the first edge (UART: falling on the data line, idle high; I2C: falling on SDA, the START). Report the
  frames with their times, the errors (parity, framing, NACK, incomplete words) and `applied` (baud or clock as
  measured, samples per bit); the frames are in the `.npz` and `scope_report` plots them labelled. Without a chip
  select SPI words are counted from the first clock after a gap: say so when the transfer starts before the record.
- **Mask testing**: `scope_mask_test` builds a tolerance band around a reference capture (taken now, or a saved
  `.npz`; `tolerance_div` or `tolerance_volts`, `tolerance_time`) and tests `n` captures against it, pass / fail per
  capture with the first failing sample and the worst excursion; `mask_name` stores the mask for later runs
  (`reference='<name>'`). Keep the settings (rate, record length, range) the same as when the mask was built: a
  mismatch is refused, a change at the scope ends the run. Tolerances under 2 codes or 2 samples fail on noise and
  trigger jitter alone; start at 0.2 div and 2 samples and report the tolerance with the verdict. `scope_report` on
  the result shows the band and the outside samples.
- **Voltmeter readings**: `scope_dvm` gives DC, AC RMS, true RMS, min / max / Vpp and the dominant frequency per
  channel over several captures, with a `statement` of what the DC value resolves (range, volts per code, measured
  noise, 1-sigma uncertainty, digits of full scale). Quote the statement, not just the number: the digits come from
  the noise and the sample count, the absolute accuracy from the calibration (`scaling`), about one code.
- **CSV**: `scope_export_csv(source=<file>)` writes any saved result as CSV with a `#` header (settings, scaling,
  caveats) for spreadsheets and other tools.

## 3a. Choosing a decimation mode for decay curves

`scope_set_decimation` (`scope_info` caps `DECIM` and `FIR`; `filter` = `off`, `boxcar`, `cic2`, `cic3`, `boxcar+fir`,
`cic2+fir`, `cic3+fir`; `peak` is section 3c) makes the FPGA filter every raw 200 MSa/s sample down to the chosen rate
instead of skipping samples,
and store 16 bits.
Below 200 MSa/s that is free resolution and anti-aliasing; the cost is a total of at most 8190 samples (set
`scope_set_trigger total_samples` first) and that it is the scope's own Filter setting (tell the person, turn it
`off` when done).

- **The decay constant is not biased.** Boxcar and CIC are linear filters and an exponential stays an exponential
  with the same tau after any of them; only the amplitude scales and the first K output samples after a sharp
  edge (TX turn-off, flyback) are smeared. Start the fit at least K samples (`t_start >= K x dt`) past the flyback.
  The sample time is self-consistent with the ADC trigger; with the EXT trigger the firmware corrects the pipeline
  latency to the nearest output sample (`ext_latency_shift_samples`).
- **FIR (`+fir`)**: `cic2+fir` flattens the passband to fs / 4 and rejects aliases about 20 dB more,
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

Bench, maintainer's unit, 2026-10-18 (FPGA 0x1534 with fw 0.2.34), 1 MSa/s (N = 200; S = 0 / 8 / 15 as row 47 says):

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

## 3c. Trigger types, the hardware counter, peak detect (protocol 1.26, FPGA 0x153C and later; 0x153D is the bitstream of release 0.3.0)

`scope_info` caps `TRIG_TYPES`, `HOLDOFF`, `COUNTER`, `PEAK_DETECT` (`scope_status` features `trig_types`, `counter`,
`peak`). Not yet benched on the unit when this was written: say so with numbers from them.

- **Trigger on a pulse width or the Nth edge** (`scope_set_trigger`): `type='pulse_width'` with `polarity`
  (`positive` / `negative` pulse), `width` in seconds and `longer=true` for pulses longer than it (default shorter);
  the trigger sits on the pulse's trailing edge. `type='nth_edge'` with `polarity` (`rising` / `falling`), `n` and
  `idle` (seconds; after that long without an edge the count restarts, so `n` counts within a burst; 0 = from the
  arm). `type='timeout'` with `polarity` (`high` / `low`) and `width`: the level held that long with no edge.
  `type='edge'` is the plain trigger. Widths and idle times are rounded to one trigger-stream sample: one memory word
  on CH1 / CH2 (2 / fs legacy, 1 / fs with a filter or peak detect), 5 ns on EXT; the result's `applied.type` shows the
  samples and the time they stand for, the caveats name a clamp. Use the sample rate the width needs (a 15 us pulse
  at 10 MSa/s is 75 words: fine; at 1 kSa/s it is below one word). `hold_off` (seconds, 5 ns steps) blocks every
  trigger for that long after a record's trigger, across re-arms and segments: with `scope_capture_segments` on a
  pulse train, hold-off 2.5 ms on 1 kHz pulses keeps every third one. Every change through `scope_set_trigger`
  clears a hold-off still running at the next capture.
- **The counter** (`scope_counter`): frequency, period, positive width, duty and the raw min / max of CH1, CH2 or EXT,
  counted by the FPGA at 200 MHz over a gate of 10 ms, 100 ms (boot) or 1 s; no capture needed and it does not
  disturb one. Resolution is one 5 ns clock over the gate (5e-7 relative at 10 ms, 5e-9 at 1 s); the 50 MHz crystal
  (about 1e-5, uncalibrated) is the accuracy: quote the `resolution` statement with the number. Reading is tier 0;
  `gate`, `source`, `level` (volts) and `hysteresis` configure it (tier 1) and the scope's measurement cards follow
  (at boot the level follows the trigger level, 6 codes hysteresis, the trigger channel; a `level` in volts pins it,
  `level='trigger'` follows again). A signal that does not cross the level by more than the hysteresis gives no
  count: put the trigger level (or `level`) mid-swing. `vmin` / `vmax` are raw extremes over the whole gate, noise
  peaks included; a capture's min / max are a few codes narrower.
- **Peak detect at slow time bases** (`scope_capture(peak=true)` for one capture, `scope_set_decimation(filter='peak')`
  to leave it on): below 200 MSa/s every stored sample is the min and max of its window of raw 200 MSa/s samples, so
  a glitch shorter than the sample period is kept where plain sampling (and the filters) miss it. The result carries
  `envelope` per channel, `<ch>_min` / `<ch>_max` previews and `<ch>_min_volts` / `<ch>_max_volts` in the `.npz`;
  `volts` is the midpoint. The envelope is a few codes wider than the signal (noise extremes over N samples): do not
  read noise from it. Averaging is refused while peak detect is on (the words hold min / max pairs); records are at
  most 8190 samples as with the filters. `scope_report` draws the envelope as a band.

## 3d. The logger and chirps (protocol 1.27, FPGA 0x153D)

`scope_info` caps `LOGGER` (`scope_status` features `logger`). Simulator only when this was written: say so with numbers
from them. There is no pulse-pattern tool: a pattern generator was cut (it did not fit the FPGA); the
calibration pin gives the square of `scope_generator` only.

- **Log for a long time** (`scope_log`, `scope_log_status`, `scope_log_stop`): when a record of 16,382 samples is not
  enough (RX drift over minutes, a slow decay train, an intermittent event), stream CH1 / CH2 to a file. Pick the
  rate within the tiers: 500 kS/s for one 8-bit channel, 250 kS/s for one 16-bit or two 8-bit, 125 kS/s for two
  16-bit (the tool refuses more and says why). The default `filter='auto'` logs the CIC3 + FIR output (no aliasing);
  `bits=8` keeps the filtered word's high byte (0.4 % of full scale per code), `bits=16` the full word,
  `filter='off'` raw samples of both ADC cores (aliases above fs / 2). Set the channels, ranges and the generator
  first: while the log runs the scope's screen is frozen and every acquisition tool answers "logging:
  LOG_STOP first". Give `duration` or end it with `scope_log_stop`; watch `scope_log_status` (samples, USB rate,
  gaps). The result is a directory of raw sample files plus `log.json`, read back with
  `fnirsi1013d.logfile.load_log(dir)`, and a summary `.npz` for `scope_report` (min .. max per bin, gaps marked). Report
  every gap (`gaps`, `lost_samples`, the cause: a lap = the scope fell behind, an overflow = the host did): the
  samples around a gap are exact, the time axis jumps.
- **Chirp a filter on the spectrum view** (`scope_sweep`): put the post on the filter's input and CH1 on its output,
  switch the scope to its Spectrum view (or use `scope_capture_and_fft`), then `scope_sweep(start='1kHz',
  stop='100kHz', steps=200, time_s=1)`: the square steps through the frequencies, each step glitch-free and
  phase-continuous, and the spectrum's peak hold traces the response. The host sends one command per step, so about
  2,000 steps per second is the limit (the result's `stats` say what was achieved); for a measured magnitude and
  phase use `scope_bode_sweep` instead. The generator is restored afterwards.
- **The generator** (`scope_generator`) is the same pattern underneath: its frequency is exact to 2^-16 of the period
  (`frequency_exact_hz`), edges on the 5 ns grid; 200 MHz / an integer (1 kHz, 1 MHz) has no dither at all.

## 3b. Frequency response (Bode)

`scope_bode_sweep` measures H(f) = CH2 / CH1 of a passive network driven by the calibration post (filters, RC / RL
networks, coil plus capacitor tanks fed through a resistor). It uses the decimation filter and drives the pin, so it
is tier 3 with a confirmation token.

1. **Wiring first, ask the person to do it and confirm.** Post to the DUT input; CH1 tip on the DUT input (not the
   post side of an added resistor unless that is what you want to measure); CH2 tip on the DUT output; **the DUT
   ground on the probe ground clips** (wiring A, the default since the bench of 2026-10-19 showed the same H as the
   USB-ground wiring within 0.4 dB). Only for a DUT that must not see the probe reference: DUT ground on the USB
   shell with both clips open (wiring B), and then `ch2_coupling='ac'`, because the DUT sits 1.24 V below the probe
   reference and saturates CH2 on 0.1 / 0.2 V/div (flag `ch2_overload`). The first sweep of a session returns this
   as `connection`: show it to the person.
2. **Never** on a coil being driven, a MOSFET drain, a powered circuit or anything that can drive the post. A PI
   detector's coil can be measured only unpowered and disconnected from its driver, through a series resistor.
3. **Thru calibration** once per session and probe setup: both probes on the DUT input, `thru_cal='measure'`,
   `thru_name='...'`; check that `mismatch_db` is within a few tenths of a dB of 0 (else the wiring is wrong). Then
   sweep the DUT with `thru_cal='<name>'` and the same start, stop and points.
4. **Choose the sweep.** Default 100 Hz-1 MHz, 50 points, `settle='normal'` (Q up to about 7). Use `resonant` (Q up
   to 34) or `high_q` for tanks; if points come back `unsettled`, rerun with the next preset. Each decade below 1 kHz
   costs real time (10 Hz-100 kHz about 27 s); above about 2 MHz the stimulus itself rolls off with a x1 probe on
   the post. `ch2_range='auto'` (default) follows a falling response down to the 100 mV/div range (about -55 dB).
5. **Report** f_actual (not the requested values), magnitude and phase with the markers (`minus_3db_hz`, or the
   peak, Q and bandwidth), the thru calibration used, and the flags: drop or grey out `near_floor`, `low_snr`,
   `clipped`, `unsettled` and `ch2_small` points rather than quoting them (`ch2_small`: CH2 under 3 codes, under the
   floor; at 100 mV/div often generator pickup, not the DUT: the caveat "CH2 too small above ... kHz" names where it
   starts, so rerun with `stop` below it); say how many range splices there were. Pass the caveats on.

6. **On the scope instead (firmware 0.2.66+, protocol 1.20):** `where='device'` lets the scope run the same plan
   and rules itself (about 2-3 x faster above 1 kHz, no USB round trip per point) and shows the Bode plot on its
   screen, so the person watches the sweep; the tool polls it and returns the same table, markers and files (the
   rows carry flags but no SNR / leakage / code levels; `device` holds the sweep id and state). Limits there: the
   settle presets only, `ch2_range` 'auto' or 'fixed', DC coupling; `thru_cal='measure'` stores the thru on the
   scope (until power-off) and here, `thru_cal='device'` applies the scope's copy. The view stays on the screen
   (its own acquisition is off, capture tools then add a caveat) unless `close_view=true`; a host capture during a
   device sweep ends it. Use it when the person wants to see the plot or the host sweep is too slow; use the host
   sweep for AC coupling, a V/div of your choice or a custom settle.

Accuracy (design estimate, to be confirmed on the bench): +-0.1 dB / +-0.5 deg down to about -30 dB with the thru
calibration, about +-1 dB near -55 dB. The ground-return error is not removed by the calibration.

## 4. The screen is shared

On collaborative firmware the person at the scope can change any setting at any time; the last change wins. Every
capture carries the `settings_seq` it was taken under. Long runs (averaging, decay fit, FFT) restart when a
setting changes mid-run and say so in `restarts` / caveats: report it, and if it keeps happening, ask the person
to leave the knobs alone (the tool gives up after 5 restarts). A streaming log (`scope_log`) does not restart: the
scope's screen is frozen while it runs, and a log ended at the scope keeps what it has with a caveat. A menu open on the scope is closed by your next request on
firmware 0.2.78+ (protocol 1.23; the caveat says so, the scope shows no note): tell the person
if they were using it. Older firmware fails with "modal screen" (E_BUSY): ask them to close it. Leave the scope as you found it where reasonable (RUN, the
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
- EXT trigger (caps `EXT_TRIGGER`): reference the source to the USB ground (connector shell); 3.3 V logic only.
  A source that shares the probe ground idles at ~1.24 V at the pin and is not recognised: insert a red LED or
  3 x 1N4148 in series (anode at the source) and trigger on the rising edge.
- Never connect the calibration pin to a detector coil, drain or damping resistor: the flyback is hundreds of
  volts.

## 5a. Safety

- Tier 2 (`scope_set_channel`) clicks relays; changes are rate-limited. Tier 3 (`scope_generator`,
  `scope_self_test_loopback`, `scope_bode_sweep`) drives the calibration pin and needs a confirmation token: say
  what will happen (frequency, duty or the sweep span, where the pin is wired) before confirming. The Bode sweep
  turns the generator back to its previous state itself.
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
| averaged_capture | `t`, `CH1_mean`, `CH1_std`, `CH1_clipped_any`, `edge_shifts_samples` (`hardware=true`: `t`, `CH1_mean`) |
| capture_segments | `t` (s, 0 = each segment's trigger), `CH1_volts` (segments x samples), `CH1_clipped`, `timestamps_s` (from the first trigger), `triggered`, `slots` |
| decay_fit | `t` (s after the edge), `data`, `fit`, `residual`, `fit_mask`, `excluded`, `std` |
| capture_and_fft | `f` (Hz), `amplitude_vrms` |
| bode_sweep | `f_actual` (Hz), complex `h` (after splice and thru cal), `h_raw`, `x1` / `x2` (volts peak), `mag_db`, `phase_deg`, `ch1_range` / `ch2_range`, `snr_db` (NaN for `where='device'`), `valid`; also `.csv` and `.json` (`files`) |
| decode | `t`, `<CH>_volts` of the lines; the frames, `applied` and the preview in `meta_json` |
| mask_test | `t`, `upper`, `lower`, `reference`, `<CH>_volts` (the first failing capture, else the last), `fail_mask`; per-capture results in `meta_json`; the mask file (`mask.file`) holds `t`, `upper`, `lower`, `reference` |
| dvm | `capture_t_s`, `<CH>_mean`, `_ac_rms`, `_rms`, `_min`, `_max`, `_frequency` per capture; the combined readings in `meta_json` |

Results with up to 2000 points also carry the samples inline; longer ones a 500-point min/max preview.
`scope_export_csv` turns any of these files into CSV (header with the settings and caveats, one row per sample).

## 7. Reporting numbers

Give value, unit and how it was measured (n averaged, sample rate, range, alignment), plus the caveats that apply:

- 8-bit ADC, ~2 LSB noise per sample; volts per code is in every result (`volts_per_lsb`).
- 200 MS/s per channel at most; trigger jitter up to 2 / fs without edge alignment.
- Analog bandwidth ~30 MHz: rise times below ~12 ns and content above 30 MHz measure the scope.
- Absolute volts: say which `scaling` applied (`scope calibration v3 ...`, `unit-calibration` or `firmware`).
- While the scope shows its spectrum, waterfall, phosphor or Bode view the capture tools add a caveat. The data are
  unaffected, but the person sees a different picture than your trace: pass the caveat on.
- Full-rate (200 MSa/s, filter off) captures: from fw 0.2.62 the scope checks every one for a garbled ADC core and
  re-takes it (`scope_health` `encode_check`). A caveat "accepted garbled after 16 re-arms" means the last such
  capture may still be bad: repeat it and report the counts.
- The calibration pin is a frequency / duty / timing reference, not a voltage reference: it is a ~3.1 Vpp square
  wave referenced to the USB ground, so against the probe ground (VRF1, +1.24 V) it reads about -1.24 / +1.9 V.
- Pass on dynamic caveats (clipping, auto trigger fired, aliasing, restarts) instead of dropping them.

## 7a. Presenting results

The tools measure and analyse; you present (roadmap: advanced features are MCP features, the agent presents the
data). For anything the person will look at, call **`scope_report`** on the result: `source` = the `file` (or
`files.npz`) the measuring tool returned, or `'latest'`; give a `title` and, if the default is unclear, the
`command` in words. It writes under `<home>/reports/<date>/`:

- a **PNG** (time traces with the std band or clipped samples, decay with fit and residual, spectrum, Bode magnitude
  and phase with the -3 dB marker and flagged points hollow); needs matplotlib (`pip install "fnirsi1013d[plot]"`),
  otherwise `png` is null and `plot_note` says why;
- a **self-contained HTML page**: the plot embedded, the result table, the summary (markers, fit quality), every
  recorded caveat plus yours, and the generating command. No scripts or CDNs: it opens offline and in dark mode.

How to show them:

- **Claude Code**: Read the PNG path (the Read tool renders images) so you see what the person will see, and say what
  it shows; then publish the HTML page with the **Artifact** tool (it starts private; it is the person's data, so offer
  the link rather than sharing it anywhere) or give its path to open in a browser.
- Other clients (Codex, Desktop): give both paths; paste the summary and the caveats into the reply either way.
- Always state the numbers and caveats in the reply too: the page supports the answer, it does not replace it.
- For a comparison (before / after, host vs device Bode) make one report per result and put the numbers side by side
  in the reply.

## 8. Troubleshooting

| Symptom | Meaning / fix |
|---|---|
| "modal screen", E_BUSY detail 0 | firmware before 0.2.78 with a menu open on the scope: ask the person to close it, retry |
| E_BUSY "calibration measurement running" / "file save in progress" / "USB drive mode" | the scope is in a step that must finish (0.2.78+): wait and retry, or ask the person (USB drive: Exit) |
| the scope restarted, the link dropped, settings look lost, or "is the right firmware on it?" | fw 0.2.52+: `scope_health` (USB state, power-on number, how the previous power-on ended, unsaved settings, a stored restart record with the exception type) and `scope_sd_info` (`card_image`: what the card boots and whether that is the running build, e.g. after a FEL load). For a fault report ask for `scope_save_report` and tell the person the file name on the card; quote `in_words` rather than raw registers |
| volts drift at 200 MSa/s or filtered captures read a little off hours after calibrating | fw 0.2.50+ tracks it: `scope_calibration_info` `tracking` says whether the tracker and the class probe have run (the probe needs a quiet input with the scope running and no menu); `calibration_provenance` says when and by which build the scope was calibrated |
| "still starting" | the scope just booted (about 5 s after a FEL load); the server waits up to 10 s |
| the scope's calibration wizard fails its zero step ("too much noise", "the input is not at 0 V") | fw 0.2.67+: `scope_cal_probe(channel, range_index)` runs the wizard's own measurement and returns per point and core what it saw (mean, RMS, extremes, clipped); compare with a host capture at the same range (`scope_capture`, 200 MSa/s). Inputs at 0 V (probes off) as for the wizard |
| a 200 MSa/s capture shows one core (every other sample) off by tens of codes, or the caveat "accepted garbled" | fw 0.2.62+: `scope_health` `encode_check` (the check mode, the last 0x33 byte and its failing pins, captures checked / garbled / re-taken) and `scope_event_log` (event `adc_fullrate_garbled`). Repeat the capture; on AL3 0x1532..0x1537 the fix is the recommended bitstream (0x153B) |
| the screen shows a Bode plot, the scope does not acquire | its Bode view is open (`scope_status` display_mode `bode`): the person leaves it with Menu > Scope, or `scope_bode_sweep(where='device', close_view=true)` |
| cannot connect, port busy / PermissionError | another session holds the serial port (only one may): ask who, never kill it |
| no device found | wrong COM port (`--port`), scope off or in USB-storage mode, cable; `fnirsi1013d-bench --census 0` checks the link |
| "no trigger within ..." | level outside the signal, wrong source or edge: `scope_get_config`, try mode `auto` |
| all captures dropped as clipped | raise volts_per_div or set `reject_clipped=false` deliberately |
| "capability ... not reported" | this firmware build lacks the feature (screenshot, device averaging, FORCE, SD) |
| "released by user" | legacy firmware only: the person tapped the REMOTE banner; ask before continuing |
| first command after a long idle fails (port error, Windows error 22, device re-enumerates) | before fw 0.2.44 a host suspend was handled as a link reset; 0.2.44 keeps the session (53/53 reopens after 10 min idle on the bench, 2026-10-19). On older firmware reopen the port once. If it persists on 0.2.44+, check Windows USB selective suspend: `fnirsi1013d-soak --mode reopen --idle 180 --rounds 3` prints the state and the device's suspend/resume events; disable it with `powercfg /SETACVALUEINDEX SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0` and `powercfg /SETACTIVE SCHEME_CURRENT` (or Device Manager > USB Root Hub > Power Management), and tell the person what you changed |
| the scope's serial port is gone after a long idle (no COM port, no new device; the scope screen is alive) | the host suspended the device and then dropped it (seen 2026-10-19 on fw 0.2.48 after 1 h 40 min with the port closed; the device never sees a RESUME interrupt). fw 0.2.49 reconnects by itself: within about 10 s after a host's failed retries, at the latest 15 min into a suspend (GET_LINK_STATS `usb_soft_reconnects`, event `usb_soft_disconnect`); wait for the port to come back. On older firmware, or if it does not come back, ask the person to unplug and replug the USB cable; afterwards read GET_EVENT_LOG and GET_LINK_STATS and report the suspend / resume / soft-disconnect events |
