# DPOS350P features on the 1013D: spectrum analyser, Bode plots, digital phosphor

Feasibility and design study, 2026-10-18 (agent dpos350p-features, read-only study; no code changed).
Scope: whether three FNIRSI DPOS350P features can be adapted (not copied) to the 1013D's UI and hardware, and
how to split them between the scope and the MCP host. Roadmap context: items 4 (on-scope FFT), 6 (READ_SCREEN)
and 7 (FRA on the host) in `docs/plans/roadmap.md`.

Legend: **[F]** fact with a citation. **[I]** inference. **[E]** estimate, assumptions stated, to be measured.
Sources: the five Amazon product photos (downloaded to the session scratchpad and viewed; marketing images,
1500x1500, partly rendered), `research/notes/dpos350p-firmware.md` (firmware strings), `research/notes/web-sources.md`
section 5, and the 1013D tree. Nothing from `research/dpos350p/` or `research/atlan4-extracted/` was executed.

## 0. Summary

| Feature | DPOS350P | 1013D on the scope | 1013D on the host (MCP) | Detector value | Effort |
|---|---|---|---|---|---|
| Spectrum (line/filled) | 200 kHz-350 MHz, FFT 4k-32k | Yes: fixed-point 1k-4k live, 16k single [E] | Exists (`scope_capture_and_fft`) | Medium-high (EMI, harmonics in the kHz band) | M |
| 2D waterfall | Yes | Yes, 5-10 fps at 4k points [E] | Easy (PNG) | Medium | S after spectrum |
| "Energy" / density view | Yes (exact meaning unclear) | Yes, as spectrum persistence on the phosphor engine | Easy | Low-medium | S after phosphor |
| 3D waterfall | Yes | Possible, low value | Easy (matplotlib) | Low | Host only |
| Bode / FRA | 10 Hz-50 MHz, sine source, amp/offset | Yes with the square-wave DDS, ~1 Hz to ~1-2 MHz useful [I] | Easiest first | **High** (coil/tank Q and resonance, RX filters) | M |
| Digital phosphor | 50k wfm/s claim | Yes, ~10 wfm/s today, a few hundred with a fast loop [E] | Easy (histogram PNG) | Medium (TX jitter, decay variation) | M |

**Recommended order**: (1) hide the dead channel-menu FFT switch; (2) host Bode tool; (3) fixed-point FFT core
plus on-target benchmark; (4) on-scope spectrum view; (5) phosphor engine and Phosphor trace mode; (6) on-scope Bode
screen; (7) waterfall and density views; (8) FPGA helpers (boxcar decimation, segmented capture); 3D waterfall stays
on the host. Reasoning: Bode gives the most to the metal-detector work and needs no firmware risk on the host; the
FFT core and phosphor engine are shared by several later views, so they come before their consumers.

Two corrections to the brief: kiss_fft is **not** vendored in `firmware/scope/` [F: no `kiss*` file there]; it exists
only in Atlan's extracted 0.026k2 tree (`research/atlan4-extracted/0.026k2 .../fnirsi_1013d_scope/kiss_fft.c`), where
nothing calls it, it defaults to `float`, and `kiss_fft_alloc` generates twiddles with `cos()`/`floor()` in double
[F: `_kiss_fft_guts.h:127-137`], which the `-nostdlib` build cannot link. A small purpose-written fixed-point FFT is
the better fit (section 1.2). And the D-cache is off: the MMU is never enabled, so every data access is uncached
[F: `research/notes/repo-donwulff.md:356`]; all CPU estimates below are given for that case.

## 1. Shared foundations

### 1.1 Platform facts that bound the design

- CPU ARM926EJ-S 600 MHz, ARMv5TE DSP multiplies (SMULWB/SMLAWB, 32x16), no FPU [F: AGENTS.md]. I-cache works,
  D-cache off [F: repo-donwulff.md:356]. Enabling the MMU and D-cache is Donwulff's "largest single performance
  win" (ROADMAP 31) [F: repo-donwulff.md:376]; it is not a prerequisite here, but every estimate improves with it
  (3-10x for memory-bound loops [I]).
- Display: two 800x480 RGB565 buffers; the trace view is drawn into `displaybuffertmp` and blitted to the visible
  buffer, trace area 728x432 at (2,48) [F: `scope_functions.c:2682` fill, `:2864` copy]. The main loop pass is
  35-75 ms today [F: `docs/hardware-status.md:121`, `pass_ms`]. 1.00o5 computes trace geometry in `double`
  [F: `scope_functions.c:2600-2601`]; new hot paths must be integer.
- Acquisition: up to 16,382 samples/ch on AL3 [F: `variables.h:398`], readout ~0.3 us per byte, so 4096 samples of
  one channel cost ~1.2 ms and a full dual-channel record ~9.8 ms [E from AGENTS.md]. Sample rates below 200 MS/s are
  made by gating the write clock, i.e. **sample skipping with no anti-alias filter** [F: `fpga/al3/src/zaklad.v:433-453`].
- Generator: 32-bit DDS on `clk_50MHz`, square only, `0x51` phase increment (85899 = 1 kHz, so f = inc x 50 MHz / 2^32,
  0.0116 Hz steps), `0x52` duty [F: `zaklad.v:71-82, 189-190, 278-283`; `fpga/al3/README.md:179`], 20 ns edge jitter
  [F: README.md:304]. Output through R21 1 kOhm, DC-coupled, swing 3.10 V from -1.09 V to +2.01 V relative to the
  probe ground, which sits on VRF1 ~+1.24 V above digital ground [F: `fpga/al3/README.md:354-372`]. Shared with the
  EXT trigger input (interlock exists) [F: roadmap item 2].
- Memory: ~26 MiB free DDR above `.bss` [F: `firmware/README.md:445`]. Every buffer below is < 1.2 MB.
- UI: menus are modal loops; while one is open the protocol answers E_BUSY [F: AGENTS.md "Bench coordination"].
  New analyser screens must be **main-loop display modes**, not modal menus, so MCP polling keeps working.
- Rules carried over: `PC_PROTOCOL=0` must stay byte-identical to Atlan's image [F: firmware/AGENTS.md section 16],
  so feature code lives in new files and behind the existing hook/`#if` pattern; maintainer policy "hide unsupported
  options; compact menus" [F: firmware/AGENTS.md section 14].

### 1.2 Fixed-point FFT core and its cost

Design [I]: real-input FFT of N samples as an N/2-point complex FFT plus a split step; int32 data, Q15 twiddles
(SMULWB keeps the product in one instruction), radix-4 stages with a final radix-2, per-stage conditional scaling
(block floating point) so 8-bit input never overflows and roundoff stays far below the ADC noise. Twiddle, Hann and
flat-top tables are generated at build time by a Python script (no libm on target). Magnitude as |X|^2 in 64 bits,
dB via CLZ + 256-entry log2 table (10*log10 = 3.0103*log2), 0.01 dB steps. Host-buildable C, tested against numpy.

Cost model [E]: B = (N/4) log2(N/2) radix-2 butterfly equivalents, plus ~N butterfly equivalents for window, split
step, magnitude and dB. Cached: ~25 cycles each. Uncached (today): ~240 cycles each, assuming ~9 data accesses per
butterfly at ~25 cycles per uncached DDR access (not measured; step C2 measures it). 600 MHz.

| N (real samples) | butterflies B | total eq. | cached ms | uncached ms (today) | readout 1 ch for comparison |
|---|---|---|---|---|---|
| 1024 | 2,304 | 3.3 k | 0.14 | 1.3 | 0.3 ms |
| 2048 | 5,120 | 7.2 k | 0.30 | 2.9 | 0.6 ms |
| 4096 | 11,264 | 15.4 k | 0.64 | 6.1 | 1.2 ms |
| 8192 | 24,576 | 32.8 k | 1.4 | 13 | 2.5 ms |
| 16384 | 53,248 | 69.6 k | 2.9 | 28 | 4.9 ms |

Conclusion [E]: even uncached, a 4096-point spectrum costs less than a tenth of today's 35-75 ms pass; 16k is fine
for single shots and averaging at a few frames per second. If the on-chip SRAM at address 0 (vectors are copied
there, `start.s:66-75`) has room for a 16-32 KB work buffer, the 4096-point case approaches the cached column [I:
SRAM size and speed unverified; check the F1C100s manual]. FFT memory: 4096 points need < 60 KB in total.

### 1.3 Shared screen pattern for analyser modes

The trace area (728x432) becomes the plot. Its bottom ~36 px hold a parameter strip of 5-6 tap fields, the way the
DPOS350P does it (photos: Reference / Attenuation / Start / Stop / Center / Freq width / Movement). A tap opens the
existing numeric keyboard (`scope_open_keyboard_menu`, menu.c:5096) or slider (`scope_open_slider`, menu.c:8739);
those are modal only while editing. Top bar stays (channel range = input attenuation, sample rate), the right
column keeps RUN/STOP and gains "Exit". Entry: a new **Analyzer** item in the main menu (menu.c:2784; today System
settings, Picture view, Waveform view, Diagnostic view, USB connection; the 147 px wide panel has room for a sixth
59 px row) with two choices, Spectrum and Bode. Phosphor is not a separate screen (section 4).

## 2. Spectrum analyser (FFT, waterfall, density, 3D)

### 2a. What the DPOS350P does (photos 71R8B+fnsGL, 71vt6vSO9IL; firmware strings)

- Full-screen spectrum, filled blue trace, y axis labelled in dBmV with "Ref" at the top. Header readout
  `Max = 10.0 MHz +64.0 dBmV 1.59 V` (peak marker). Two cursors with a readout block C1 / C2 / delta: frequency, dBmV,
  uV. Top buttons: Return, Function, Screenshot, Cursor, auto (A) and pause. Bottom strip: Reference 71.7 dBmV,
  Attenuation 17.0 dBmV, Start frequency 0 Hz, Stop frequency 150 MHz, Center freq 75 MHz, Freq width 150 MHz,
  Movement Slow move [F: photo].
- "Attenuation" is the vertical scale, not an input attenuator: with Attenuation 17.0 the y labels step by 17 dB
  (71.7, 54.7, 37.7, 20.7, 3.73, -13.2, -30.2); in the second photo 15.0 gives 15 dB steps [F: photo; the label is a
  translation of dB/div, I]. "Movement" toggles coarse/fine adjustment (strings "Fast move / Slow move",
  dpos350p-firmware.md 4.1).
- Visualisation modes shown as stacked layers: filled spectrum, a 2D waterfall (time downwards, rainbow palette
  with a colour bar, swept signal visible), and a "3D waterfall" rendered as a shaded perspective spectrogram
  [F: photo]. Strings: "FFT display", "FFT length", "Waterfall display", "3D display" (digest 4.1); FFT 4K-32K and
  RBW are claimed in the review (web-sources.md:145). The "energy" mode the maintainer mentions is not labelled in the
  photos or the strings I have; it is probably the filled/gradient spectrum or a density view [I].

### 2b. Adapted design for the 1013D

- Screen: one channel at a time (CH1/CH2 field), or both overlaid in channel colours. Plot 728 columns; each column
  shows the max of the bins it covers (peak-preserving, so a 2048-bin spectrum keeps narrow lines). Y in **dBV**
  (high-impedance input, so dBm into 50 Ohm would be wrong; dBmV = dBV + 60 is a one-word option if the maintainer wants
  DPOS parity). Peak marker readout at the top (frequency, dBV, Vrms), two frequency cursors via the existing cursor
  buttons.
- Strip fields (hidden when not applicable): **Start / Stop** (tap the label to flip to Center / Span), **RBW**,
  **Ref** (dBV at top), **Scale** (5/10/20 dB/div), **Avg** (Off, 4, 16, Peak hold), **View** (Spectrum, Waterfall,
  Density). Not exposed: FFT length (derived from RBW and shown as a readout), "Attenuation" as such (the channel
  V/div relay is the real input attenuator and stays in the channel menu), window (Hann default; flat-top only when
  Avg = peak hold or behind a long press [I: maintainer to decide]).
- Mapping: Stop sets fs = 200 MHz/(D+1) with fs >= 2.5 x Stop; RBW picks N in {1024, 2048, 4096} (live) or 16384
  (single/averaged) using RBW = 1.5 fs / N for Hann. Start > 0 with a narrow span uses the same full FFT and shows a
  slice; a true zoom (digital down-conversion with an NCO table, then FFT) is a later step S6.
- Waterfall: a ring of 8-bit palette indices, 728 x 300 rows (218 KB); each new spectrum writes one row; render =
  palette lookup into the tmp buffer, no memmove. Colour bar on the right edge of the plot. Density ("energy" [I]):
  the phosphor engine of section 4 fed with spectrum points, so frequent levels glow; this is the closest cheap
  analogue of a real-time spectrum display.
- 3D waterfall: host only (MCP returns a PNG). On the scope it would need hidden-line removal over ~32 stacked traces;
  feasible (~10-20 ms [E]) but it adds a mode without adding information.
- The channel menu already has an "FFT" switch that stores `fftenable` but draws nothing [F: menu.c:3107, 3176;
  statemachine.c:1883-1893; the placeholder `scope_functions.c:2798`]. Hide it now (maintainer policy); once the spectrum
  exists it can open the analyser on that channel instead of adding a split view.

### 2c. Feasibility and numbers

- Frame budget at 4096 points [E]: readout 1.2 ms + FFT 6 ms (uncached) + plot 5-10 ms + today's fill/blit/grid
  overhead ≈ the current 35-75 ms pass, i.e. **~10-20 spectra/s** when the record fills fast. At low fs the capture
  time dominates: 4096 samples at 10 kS/s take 0.41 s (RBW 3.7 Hz), so ~2 spectra/s; inherent, there is no streaming.
- Waterfall render [E]: 218 k pixels x ~35 cycles = 13 ms per frame uncached; 5-10 fps with the spectrum.
- Dynamic range: 8-bit ADC, SFDR ~45-50 dBc, A/B interleave spur at fs/2 - f [F: `docs/mcp-tools.md:222`]. Noise
  floor per bin at 4096 points ~-80 dBFS [E: ~-50 dBFS ADC noise minus 33 dB processing gain]; spurs, not noise,
  set the useful range. Front end -3 dB at 20-40 MHz [F: SYNTHESIS 2].
- Aliasing: because decimation skips samples, out-of-band signals fold into the span as real-looking peaks. The UI
  must show fs and say "no anti-alias filter" once. FPGA help: a **boxcar decimation mode** (sum D ADC samples per
  stored sample instead of skipping; first null at fs, ~-20 dB at a few fs [E]) and, as a side effect, a 16-bit
  high-res record (2 x 16-bit per 32-bit word, 8192 samples/ch). LUT use is 7% and 3 DSP blocks are unused
  [F: repo-donwulff.md:251], so this fits. An FFT in the FPGA does not pay off (48 BRAM9K, readout dominates).
- Host: `scope_capture_and_fft` already does windows, averaging, peaks, THD [F: docs/mcp-tools.md:210-223]. Host adds
  `scope_spectrogram` (repeated captures, PNG waterfall/3D) cheaply.

### 2d. Split

On the scope: live spectrum, peak marker/cursors, averaging, 2D waterfall, density view. Host: deep FFTs (16k with
averaging over many captures), 3D waterfall, spectrogram files, THD/peak tables, anything needing float. Optional
READ_FFT (roadmap item 4) returns the last on-scope spectrum in dB, mainly for bench acceptance against the host.

### 2e. Risks and unknowns

Uncached cost per access is assumed (C2 measures it); SRAM availability unknown; aliasing confusion without the FPGA
boxcar mode; the analyser changes fs and record length, which host captures in between will see (settings
fingerprint already flags it, firmware/AGENTS.md section 6); a 16k FFT adds ~30 ms to a pass and so to MCP latency.

### 2f. Implementation steps (each one agent task)

| Step | Work | Bench acceptance |
|---|---|---|
| C0 | Hide the channel-menu FFT switch (menu draw + touch condition, `#if PC_PROTOCOL`) | Maintainer: switch gone, menu compact; `make check-reference` IDENTICAL |
| C1 | `fx_fft.c/.h` + table generator + host tests vs numpy | Host: bin error < 0.05 dB above -60 dBFS, roundoff noise < -100 dBFS for N = 1k-16k |
| C2 | On-target benchmark (timer0 ms ticks or a cycle loop) in DDR and, if available, SRAM; result in a debug counter | Table of measured ms for 1k-16k replaces the [E] column above, logged in hardware-status.md |
| S1 | Analyzer > Spectrum screen: one channel, Hann, 0..fs/2 at the current fs, Ref, Scale, peak marker | Generator 1 kHz 50% on CH1: peaks at 1/3/5 kHz, 3rd at -9.5 dB and 5th at -14.0 dB vs the fundamental within 0.5 dB; same capture via `scope_capture_and_fft` within 1 dB for peaks > -40 dBc; `pass_ms` recorded |
| S2 | Start/Stop/Center/Span with automatic fs, RBW to N, Avg (4/16/peak hold), CH1/CH2/both | Stop 100 kHz shows fs and RBW correctly; noise-floor bin std drops >= 3x with Avg 16; peak hold keeps a generator sweep 1-10 kHz made from MCP |
| S3 | READ_FFT over the protocol (optional) + MCP decoder | Device vs host spectrum of the same READ_RAW capture within 0.2 dB above -50 dBc |
| S4 | Waterfall view | MCP steps the generator 1/2/3 kHz each second: a clean staircase; >= 5 fps at 4096 points |
| S5 | Density view on the phosphor engine (after P1) | Generator toggled between two frequencies: both lines visible with intensity by time share |
| S6 | Zoom FFT (NCO mix + decimate + FFT) for narrow spans | 10 kHz +/- 50 Hz span resolves a 10.000/10.010 kHz pair (generator + second source) |
| F1 | FPGA boxcar decimation mode bit + firmware/MCP flag (Tier 2, fpga/al3) | Generator 3.9 MHz, fs 1 MS/s: alias at 100 kHz drops >= 15 dB vs skipping mode |

## 3. Frequency response analyser (Bode)

### 3a. What the DPOS350P does (photo 71Tnb+iimgL, digest 4.1/7)

Plot of gain (dB, left axis, orange) and phase (deg, right axis, blue) against a **log frequency axis** (1.00 MHz,
3.69 MHz, 13.6 MHz, 50.0 MHz, i.e. three geometric divisions). Cursor block C1/C2/delta with frequency, dB, deg.
Top: Return, Calibration, Screenshot, Cursor, Loop, pause. Bottom: Amplitude 1.00 V, Offset 0 uV, Start 1.00 MHz,
Stop 50.0 MHz, Frequency count 50, Movement [F: photo]. Range 10 Hz-50 MHz with a sine generator; progress percent
during sampling (update log); forum reports erratic phase at first (web-sources.md:148).

### 3b. Adapted design

- Stimulus: the DDS square wave on the calibration post, 50% duty (no even harmonics). **CH1 probes the DUT input,
  CH2 the output; H(f) = X2(f)/X1(f)**. Measuring the actual stimulus on CH1 cancels the 1 kOhm source resistance,
  the unknown post wiring, the generator level and its 20 ns jitter (both channels see the same edges, sampled
  simultaneously).
- Per point: set f; wait a settle time (>= 3 periods, user-set in periods); capture both channels with fs chosen so
  the record holds 16-64 periods; compute the complex amplitude at f on both channels with a windowed single-bin DFT
  (Goertzel or sine/cosine table); gain = 20 log |H|, phase = arg H. Generator and ADC share the 50 MHz crystal, so f
  can be set to an exact bin (k fs / N within 0.006 Hz) and leakage is negligible [I].
- **Harmonics option**: the square wave also carries 3f, 5f, 7f ... at 1/3, 1/5, 1/7 of the fundamental. Evaluating
  H at those bins from the same capture gives 3-4 extra points per capture, at -9.5/-14/-17 dB less SNR. Default off;
  "Harmonics: 1 / 1-3-5 / 1-...-9". Only valid for linear DUTs (the stimulus is 3.1 Vpp).
- **Thru calibration** (DPOS "Calibration"): both probes on the same node, one sweep, store per-point H_cal; later
  sweeps show H / H_cal. Removes inter-channel gain/phase mismatch and probe differences.
- **Source = Internal / External**. External: the scope follows whatever CH1 sees. It estimates f from CH1 (zero
  crossings, refined by the DFT peak), measures H at that f and adds a point when f has moved by more than one
  plot pixel. The maintainer sweeps a sine generator by hand or with its sweep function. From the host, the MCP can also
  step an external SCPI generator.
- Screen: log-f axis over 728 px, dB on the left in CH2 colour, degrees on the right in a second colour, grid by
  decades; points drawn as they arrive with "37/50" progress; Single / Loop in the right column (RUN/STOP = start/
  abort). Strip: **Start**, **Stop**, **Points** (10-200), **Settle** (periods), **Harmonics**, **Source**. A long
  press on the plot or a strip button runs Calibrate. Hidden (unsupported): Amplitude, Offset, waveform.
- Placement: Analyzer > Bode. It takes the generator: when entered it remembers the generator state, turns EXT
  trigger off (interlock), and restores both on exit.

### 3c. Feasibility and numbers

- Usable range [I]: low end ~1 Hz (generator held 0.1 Hz cleanly, fpga/al3/README.md:363; AC coupling and patience
  set the limit). High end ~1-2 MHz for the fundamental: R21 1 kOhm into ~100 pF of x1 probe/cable gives a ~1.6 MHz
  pole on the stimulus itself (CH1 sees it, so H stays correct, but the level drops), and the 50 MHz DDS clock makes
  the edge timing coarse above that. With harmonics, points to ~5-10 MHz; front end -3 dB at 20-40 MHz.
- Time per point [E]: settle + capture (~(3 + 16) periods) + readout 2 x 16k ~10 ms + DFT. DFT on 2 x 16k samples
  uncached ~2-3 ms per frequency (one MAC + table load per sample). 1 kHz: ~30 ms; 10 Hz: ~2 s. A 50-point log sweep
  10 Hz-1 MHz takes ~15-25 s, dominated by the lowest decade; 100 Hz-1 MHz takes ~3-5 s.
- Accuracy [E]: 8-bit, but the DFT over 16k samples gives ~40 dB processing gain; gain repeatability ~0.1-0.2 dB and
  phase ~1 degree for |H| > -40 dB, worsening to ~1 dB/10 degrees near -60 dB. Pick the CH2 range per point
  (auto-range by peak code) to keep the output at > 1/4 of full scale.
- Memory: 200 points x (f, re/im per channel, cal) < 10 KB.
- FPGA help: none needed. A lock-in (DDS-referenced I/Q accumulators) would remove the readout, but readout is
  ~10 ms against settle times of periods; not worth Tier 2 effort [I].
- Detector use [I]: TX coil + capacitor driven through R21 shows the resonance frequency and Q directly; RX
  preamp/filter responses with an external attenuator for small signals.

### 3d. Split

Host first (`scope_bode`): sweep control, float DFT/sine fit, thru-cal files, CSV + PNG, harmonics, external SCPI
generator. It reuses `scope_generator` and dual-channel captures; roadmap item 7 already plans it. On the scope:
the stand-alone sweep and plot, the same fixed-point DFT, thru-cal in RAM (optionally saved to the SD card).
Optional READ_BODE returns the on-scope table for acceptance.

### 3e. Risks and unknowns

- Stimulus is 3.1 Vpp logic with ~+0.46 V mean against the probe ground (fpga/al3/README.md:359-364): DC bias on the
  DUT (AC-couple it or accept it), no amplitude control (use an external divider; the CH1 reference makes that safe),
  large-signal drive can make amplifiers slew or clip.
- The output pad has only R21 as protection [F: fpga/al3/README.md:430]; a DUT that back-drives the post can hurt the
  FPGA. Say so in the UI help and the MCP tool description.
- Sub-sample skew between channels at high f (front-end differences) needs the thru calibration; A/B interleave
  mismatch spur at 200 MS/s.
- Sweep inside the main loop must be a state machine (one step per pass) so MCP polling and RUN/STOP stay live.

### 3f. Implementation steps

| Step | Work | Bench acceptance |
|---|---|---|
| B1 | MCP `scope_bode` (mcp/): log sweep, settle, dual capture, windowed DFT at f (+ odd harmonics), thru-cal, CSV/PNG | RC low-pass 10 kOhm / 10 nF after the post (fc 1.59 kHz): -3 dB at fc within 5%, phase -45 +/- 3 deg at fc, -20 dB/dec +/- 1 dB at 10 fc; repeat run within 0.2 dB / 1 deg |
| B2 | Fixed-point single-bin DFT in `fx_fft.c` (host-tested) | Host: amplitude error < 0.05 dB, phase < 0.2 deg for 8-bit synthetic tones vs numpy |
| B3 | Analyzer > Bode screen, internal source, Single/Loop, log axis, progress, cursor | Same RC as B1: on-scope values within 0.5 dB / 3 deg of B1; RUN/STOP aborts within one pass; MCP PING/GET_STATUS answered during the sweep |
| B4 | Thru calibration, harmonics option, auto-range of CH2 | Thru on one node after cal: 0 +/- 0.2 dB, 0 +/- 2 deg across the sweep; harmonics points lie on the B1 curve within 1 dB |
| B5 | External source "follow" mode | Maintainer sweeps an external sine by hand over the RC: points on the B1 curve within 1 dB |
| B6 | Detector check: TX tank through R21 | Resonance within 1% of 1/(2 pi sqrt(LC)) from LCR values; Q readout recorded |

## 4. Digital phosphor (intensity-graded persistence)

### 4a. What the DPOS350P does (photos 71JqOCHJEXL, 81j7o6GZWJL)

Normal scope screen (same chrome, measurement row and status bar) with the trace drawn as a colour-graded density:
rare paths blue, frequent paths green to yellow/red, on a fast square wave with ringing and timing jitter; the
amplitude-modulated example shows the dense envelope as solid colour [F: photos]. Marketed as "Digital fluorescent"
with 50,000 wfm/s (photo 71R8B+fnsGL); strings show "Waveform light" (trace brightness) in System (digest 4.1). No
control layout for persistence is visible in the material I have.

### 4b. Adapted design

- Place: ACQ menu, "Trace" selector, today Linear / Dots / Peak / Pro [F: `variables.c:554`, `menu.c:5697`]. Add a
  fifth choice **Phos**. When selected, one more row appears in the ACQ menu: **Persistence** 0.2 s / 1 s / 5 s /
  Infinite, and **Colour** Mono / Heat (Mono = channel colour by intensity, Heat = blue-to-red like the DPOS). Nothing
  else changes on the screen.
- Engine: per enabled channel a 728 x 400 array of 8-bit hit counts (291 KB each). Each capture rasterises its
  display points: for every screen column the cells between consecutive y values are incremented (saturating), so
  steep edges are drawn like lines and dense records like peak-detect. Decay once per displayed frame, scaled to
  the persistence time; Infinite = no decay. Render: count to colour through a 256-entry RGB565 table per palette,
  skipping zero words; CH2 drawn over CH1 by max.
- Clear on any capture-relevant change, using the existing settings fingerprint (`pc_hal_settings_fingerprint`,
  firmware/AGENTS.md section 6), and on the 50% TRIG / RUN.
- Faster acquisition while Phos is on: read only the samples that map to the screen (0x1F read offset) and
  accumulate several captures per displayed frame; display at ~10-15 fps while captures run as fast as the trigger
  allows. Show the achieved wfm/s in the ACQ chip or the measurements row.

### 4c. Feasibility and numbers

- Per capture [E]: window readout 2 ch x ~1500 samples = 0.9 ms; rasterise ~728 columns x ~5 cells RMW ≈ 0.3 ms
  uncached; arm/trigger overhead ~0.5 ms plus the record time. So **~300-600 wfm/s** for a repetitive signal at fast
  timebases, versus ~9 full-depth captures/s on the current path. The 0.3 us/byte bus is the hard bound (~1000 wfm/s
  for 1500-sample windows on both channels).
- Per display frame [E]: decay via 4-cells-per-word arithmetic, 73 k words x ~80 cycles ≈ 10 ms; colourise worst case
  291 k cells x ~30 cycles ≈ 15 ms (typical far less, most words are zero). Fits in the current pass budget; with
  the D-cache on it would be ~3x cheaper [I].
- Memory: 2 x 291 KB counts + 2 x 512 B palettes. 16-bit counts (1.2 MB) only if Infinite needs more than 255 levels;
  8-bit with log-scaled increments is enough [I].
- FPGA help: **segmented capture** (K short triggered records back-to-back in the 16k memory, one readout for K
  waveforms) raises the dead-time-limited rate for rare events; the bus bound stays. A hit-count histogram in the
  FPGA does not fit (728 x 256 x 8 bit = 186 KB vs 54 KB of BRAM9K). Donwulff lists segmented bursts as ROADMAP 29
  [F: repo-donwulff.md:375].
- Host: N captures to a 2D histogram PNG is trivial (numpy), at USB rates of ~50 captures/s [F: docs/protocol.md:868].

### 4d. Split

On the scope: the Phos trace mode and its fast loop, because persistence is only useful live while probing. Host:
long-exposure histograms, eye/jitter statistics, files. The MCP returns the on-scope view via screenshots.

### 4e. Risks and unknowns

1.00o5's acquisition and trace pipeline is entangled (trigger point search, long mode, averaging all in
`scope_acquire_trace_data` / `scope_display_trace_data`); the fast loop must not break the shared-control capture
hooks or READ_RAW holds (firmware/AGENTS.md section 6). Roll/long mode: Phos disabled there (hide). The uncached
RMW cost is assumed. Touch scan and battery reads in the loop cap the rate [I].

### 4f. Implementation steps

| Step | Work | Bench acceptance |
|---|---|---|
| P1 | `phosphor.c/.h`: rasteriser, decay, palettes; host tests with synthetic traces | Host: counts match a reference rasteriser; decay halves counts at the set time constant +/- 10% |
| P2 | ACQ Trace "Phos" + Persistence/Colour rows, clear on fingerprint change, normal capture path | Generator 1 kHz, MCP toggles duty 30/70% every 0.25 s: both falling edges shown, intensities about 1:1; `pass_ms` <= 100 ms; screenshot via READ_SCREEN (roadmap 6) |
| P3 | Fast loop: screen-window readout, several captures per frame, wfm/s readout | 1 MHz generator at 1 us/div: >= 200 wfm/s measured (record actual); MCP PING/GET_STATUS still answered |
| P4 | MCP `scope_persistence` (host histogram PNG) | Same duty test: host image matches the on-scope view |
| P5 | FPGA segmented capture (Tier 2, optional) | K = 8 segments: 8 distinct trigger instances per readout, wfm/s up >= 3x vs P3 |

## 5. Cross-cutting notes

- Prerequisites: READ_SCREEN (roadmap 6) makes every UI acceptance test scriptable; C2 turns the [E] numbers into
  measurements before S1/P2 commit to frame-rate targets.
- MCP visibility: GET_STATUS should report the active display mode (scope / spectrum / bode / phos) so agents know
  why fs or the generator changed; a host SET_* that conflicts (e.g. generator while Bode runs) gets E_BUSY with a
  detail code. Needs a protocol row and the mcp agent's agreement.
- Optional MMU/D-cache enable (Donwulff ROADMAP 31) speeds all three, but touches the framebuffer (display DMA reads
  DDR, so the framebuffer must be uncached or cleaned) and the USB FIFO paths; treat as its own risky project.
- Nothing here is portable code from the DPOS350P: its firmware is V3s/Cortex-A7 with hard float
  (dpos350p-firmware.md section 0). Only the UX ideas carry over.
