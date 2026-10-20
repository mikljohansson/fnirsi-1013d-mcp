# Bode / frequency response analyser on the 1013D: design study

Design study, 2026-10-19 (agent bode-design; docs only, no code, nothing benched). Builds on the Bode notes in
`docs/plans/dpos350p-features.md` section 3 (the feasibility pass) and refines them with what landed since: the
0x1534 CIC and 0x1535 FIR decimation, calibration v3, the spectrum view's conventions and the DDS pattern-tone
finding. Roadmap: `docs/plans/roadmap.md` (Bode follows phosphor; section 8 item 5 adds FPGA frequency sweeps later).

Legend: **[F]** fact with a citation. **[I]** inference, to be confirmed. **[E]** estimate with the assumptions stated,
to be measured. "Codes" are 8-bit ADC LSB.

## 0. Summary

| Question | Answer |
|---|---|
| Default mode | **Stepped single tone**: one DDS frequency per point, CH1 = DUT input, CH2 = DUT output, H = X2 / X1 from a single-bin DFT at the exact DDS frequency over a coherent record of 16-bit CIC3+FIR samples. |
| Fast mode | **Harmonic multi-tone**: two fundamentals per decade, H also read at the 3rd..9th harmonics of each; about 4x fewer captures, quality-gated per point. |
| Not now | Chirp / swept DDS (needs FPGA sweep support, roadmap 8 item 5), step-response deconvolution beyond the multi-tone (8-bit dynamic range). |
| Frequency set | Always 50 MHz / M for integer M (no DDS pattern tones). Costs nothing below 500 kHz (step < 1 %); 2 % steps at 1 MHz, 10 % at 5 MHz. |
| Range | 1 Hz to about 2 MHz useful with x1 probes, about 5 MHz with x10 on CH1; the spectrum's 25 MHz cap is not reached. |
| Accuracy (estimate) | +-0.1 dB / +-0.5 deg to -30 dB with thru calibration; +-1 dB at about -55 dB (CH2 at 100 mV/div sees one code); floor set by crosstalk and the ground return, to be measured. |
| Biggest risk (new) | **Ground return**: if the DUT returns the stimulus current through a probe ground clip, it flows through R71 (100 Ohm) into VRF1 and adds a common error to both channels. Wire the DUT ground to the scope's USB ground and leave the clips open (section 2.5). Bench item 1. **Bench 2026-10-19: not observed (both wirings within 0.4 dB); wiring A (clips) is the default now, section 2.5.** |
| Sweep time, 50 points 100 Hz-1 MHz | about 3 s host-driven, about 2 s on the device today (section 3.7). The lowest decade dominates: 10 Hz-100 kHz takes about 12 s either way. |
| Order | Host tool `scope_bode_sweep` first (no firmware change), bench it on an RC low-pass at 10 kHz, then the on-device view. |

## 1. Measurement principle

### 1.1 The stimulus as it is

| Property | Value | Source |
|---|---|---|
| Waveform | Square wave, `phase_acc < threshold` on the 50 MHz DDS clock, 32-bit phase increment (0x51), duty threshold (0x52), enable (0x50) | [F] `docs/protocol.md` 8.6; `fpga/al3/README.md` "Atlan4 zaklad.v vs" table |
| Frequency | f = inc x 50 MHz / 2^32, 11.64 mHz steps, at most 25 MHz | [F] protocol 8.6 |
| Edges | Registered at 50 MHz, so quantised to 20 ns | [F] protocol 8.6 |
| Level | 0 / 3.1 V against the FPGA (USB) ground; about -1.2 / +1.9 V against the probe ground | [F] `fpga/al3/README.md` "Changes" 2; `docs/hardware-status.md` (swing 3.1 Vpp) |
| Source impedance | R21 = 1 kOhm, DC coupled, no other protection | [F] `fpga/al3/README.md` "Changes" 2 |
| Probe ground | BNC shells = VRF1 (+1.24 V, TL432) through R71 = 100 Ohm | [F] same section |
| Pin sharing | EXT trigger uses the same pin (0x1533+); selecting EXT turns the generator off | [F] protocol section 14 row 41 |
| Fundamental of a 50 % square | (2 / pi) x 3.1 V = **1.97 V peak, 1.40 V rms, +2.9 dBV** | arithmetic |
| Harmonic n (odd) | 1.97 V / n peak: n = 3 -9.5 dB, 5 -14 dB, 9 -19 dB, 31 -30 dB, 99 -40 dB | arithmetic |

Coherence [I]: the ADC encode clocks (100 MHz pair, `clk_200MHz` from the PLL) and the DDS clock are both derived
from the 50 MHz crystal input `i_xtal` (`fpga/al3/README.md` SDC row). Then the stimulus and the sample clock are
locked: a stimulus of exactly M DDS clocks per period is exactly 4M raw samples per period at 200 MSa/s. Bench item 4
checks it (a coherent capture shows no leakage of the fundamental).

Clean edges [F]: for f = 50 MHz / M with integer M, every period has the same edge pattern. For other frequencies the
edges dither between two clock positions in a pattern that repeats at a low rate and puts a real tone into the
stimulus; a 900 kHz square carries a 100 kHz tone at about -37.5 dBV (`docs/hardware-status.md` 2026-10-18 late;
`fpga/al3/README.md` section 3, bench check 4).

### 1.2 Option (a): stepped single tone (recommended default)

Per point: set the DDS to f, wait the settle time, capture CH1 and CH2 together, compute the complex amplitude of the
fundamental on each channel, H(f) = X2 / X1, |H| in dB and arg H in degrees.

**Why it works with a square wave.** The DUT is assumed linear. The stimulus harmonics excite the DUT at 3f, 5f ...,
and the response contains them, but a DFT at f over an integer number of periods has exact zeros at every other
harmonic of f. Coherent sampling turns "reject the harmonics" into "pick the record length".

**Coherent record.** Let the stimulus be f = 50 MHz / M and the capture rate fo = 200 MHz / N (N = 1 for legacy
200 MSa/s, else the decimation factor). Samples per period = 4M / N = p / q in lowest terms. A record of L = j x p
samples holds exactly C = j x q periods. Harmonic n then lands in bin n x C (mod L, with folding), and it lands on the
fundamental's bin only if n = +-1 (mod p) [arithmetic]. So:

| Contamination path | Rejection | Basis |
|---|---|---|
| Harmonics 3f, 5f ... inside the band | exact (other bins, rectangular window zeros) | [F] DFT property, coherent record |
| DC (stimulus mean, ADC zero) | exact (bin 0); the mean is removed before the DFT anyway | [F] |
| Harmonics that fold onto f (order n = p +- 1) | amplitude 1/n times the CIC's attenuation next to its null at multiples of fo: below -80 dB for p >= 8 with CIC3 | [E] sinc^3 at 1/p of fo from the null; `fpga/al3/README.md` section 3 alias table |
| Legacy 200 MSa/s (no filter) | n = 4M +- 1, i.e. above 190 MHz at 5 MHz: the ~30 MHz front end removes it | [E] |
| Very low f (M > 10^4, inc rounding up to 6e-4 relative) | the record is not exactly coherent; leakage of the 3rd harmonic below -70 dB after mean removal; analyse at the exact DDS frequency f_a = inc x 50 MHz / 2^32 | [E] |
| DDS pattern tones | none for integer M | [F] above |
| Offset-PWM ripple (25 kHz and harmonics, per channel) | a separate bin unless f is within one bin of j x 25 kHz; caught by the background capture (3.6) | [F] 25 kHz PWM, `AGENTS.md`; ripple seen in `firmware/AGENTS.md` 21 |

**Rate per point.** Use the FPGA filter at the rate the record needs, not the UI time base:

| f band | Capture | N (fo) | Samples per period | Periods C | Record L |
|---|---|---|---|---|---|
| 1-24 Hz | CIC2+FIR (CIC3 is exact only for N <= 20668) | 16 x 2^k, the smallest k with 4M / N <= 512 | 257 .. 512 | 8 | 2056 .. 4096 |
| 24 Hz-24 kHz | CIC3+FIR | same ladder (N = 16384 .. 32) | 257 .. 512 | 8 | 2056 .. 4096 |
| 24 kHz-1.5 MHz | CIC3+FIR | 16 (12.5 MSa/s) | 512 .. 8 | 8 .. 500 | <= 4096, a multiple of p |
| 1.5-10 MHz | legacy 200 MSa/s (FULL class, 8-bit) | 1 | 133 .. 20 | many | 4096 .. 16382 |

Rules behind the table: N a multiple of 4 and >= 16 so the FIR engages (protocol row 49); samples per period >= 8
keeps f inside the FIR's flat band (fo / 4) [F `fpga/al3/README.md` section 5]; the depth limit with the filter is
8190 (protocol row 47). The decimation filter's droop and group delay are identical on both channels and cancel in
X2 / X1 [I: same K, N, S and FIR enable on both channels by construction, `zaklad.v` instantiates `decim_chan` per
channel with shared control].

Every point stays in the calibration's FULL class (divider 0 or filtered): the FULL / GATED class gap of 1.1 %
(`firmware/AGENTS.md` 21) would otherwise show as a step where the sweep changes class.

**DFT.** Remove the mean, multiply by a rotating phasor at f_a, accumulate I and Q over L samples (rectangular
window: the record is coherent). Cost per sample: one load and a few multiplies. A Hann window is the fallback for the
External source mode (3.1), where coherence is lost.

### 1.3 Option (b): multi-tone from one square (fast mode)

The square at f0 carries odd harmonics at 1/n. One capture gives H at f0, 3f0, 5f0 ... from the same two records,
each harmonic on its own coherent bin.

| Harmonic n | Stimulus level re fundamental | CH2 codes peak with H = 0 dB on the best range | Notes |
|---|---|---|---|
| 1 | 0 dB, 1.97 V | 115 at 500 mV/div | |
| 3 | -9.5 dB | 38 at 500 mV/div | |
| 5 | -14 dB | 23 | |
| 9 | -19 dB | 13 | |
| 31 | -30 dB | 3.7 | ADC spurs comparable |
| 99 | -40 dB | 1.2 | |

Limits:
- **Nonlinearity, not noise.** Noise after the DFT is about 0.003 code (3.4). The AD9288's own distortion (SFDR about
  45-50 dBc, `fpga/al3/README.md` section 3) and any DUT nonlinearity create harmonics of the response at exactly
  n x f0, the bins being measured. CH1 is safe (a two-level square stays a two-level square under a static
  nonlinearity) [I]; CH2 carries arbitrary shapes. A harmonic point is valid only where its CH2 level is within 25 dB
  of the record's largest CH2 component and at least 3 codes peak [E]. Points that fail are dropped, not drawn grey.
- **Log coverage.** Harmonics 1, 3, 5, 7, 9 sit at 0, 0.48, 0.70, 0.85, 0.95 decades above f0; a second fundamental
  at f0 x 10^0.5 interleaves 0.5, 0.98, 1.2 ... So two fundamentals per decade give about 10 usable points per decade
  where the DUT is not steep. Steep regions (stopbands past -25 dB re the passband) lose points: the fast mode is an
  overview, not the reference sweep.
- **Even harmonics** appear with odd M (duty 0.5 +- 1/(2M)); they are tiny and ignored.

### 1.4 Option (c): step response, chirp

| Variant | Feasible now | Why |
|---|---|---|
| Step response (one edge, differentiate, FFT) | Only as the multi-tone in disguise | A square wave is a periodic step; its coherent FFT at the harmonic bins is the same measurement with the same 1/n and SFDR limits. The broadband extreme (f0 = 1 kHz, 4096-point FFT with `fx_fft.c`, harmonics up to fo / 4) is a useful one-shot preview, accurate only to about n = 30 [E]. |
| Chirp / exponential sweep | No | Needs a DDS that ramps its increment (FPGA, roadmap 8 item 5) and a record long enough at one rate for 3+ decades (8190 samples at most; the streaming logger, item 4, would lift that). A square-wave exponential sweep could separate its harmonic responses in time (Farina's method) [I]: a later study. |
| PRBS / noise | No | Roadmap 8 item 8. |

### 1.5 Recommendation

Default: option (a), one capture per point, coherent, 16-bit CIC3+FIR, rectangular single-bin DFT, thru calibration
available. Fast: option (b) with harmonics up to 9 and two fundamentals per decade. Both share the frequency planner,
the DFT and the quality flags, so the fast mode is a small addition once (a) works.

## 2. Accuracy budget

### 2.1 Levels and codes per range

From the protocol's example (range 5, 100 mV/div, 3.43 mV per code, about 29 codes per division; `docs/protocol.md`
8.5), scaled by V/div [E: assumes the per-range calibration keeps the nominal ratios]. x1 probe; x10 is ten times
coarser at the tip. V/div per range from `variables.c` (x1: 5 V, 2.5 V, 1 V, 500 mV, 200 mV, 100 mV; 50 mV is a zoom).

| Hardware range (x1 V/div) | mV per code | Full scale (+-128 codes) | Stimulus fundamental (1.97 V pk) | H giving 1 code peak |
|---|---|---|---|---|
| 0 (5 V) | 171 | +-22 V | 11.5 codes | (gain DUTs only) |
| 2 (1 V) | 34.3 | +-4.4 V | 58 codes | -35 dB |
| 3 (500 mV) | 17.2 | +-2.2 V | 115 codes | -41 dB |
| 4 (200 mV) | 6.9 | +-0.88 V | clips | -49 dB |
| 5 (100 mV) | 3.43 | +-0.44 V | clips | **-55 dB** |

50 mV/div and below are software zoom of range 5 (`AGENTS.md`), so -55 dB at one code peak is the hardware limit
for a 3.1 Vpp stimulus. CH1 sits on range 3 for the bare stimulus (180 codes peak to peak, centred with the offset).

### 2.2 Error sources

| Source | Size | Effect on H without thru cal | With thru cal | Basis |
|---|---|---|---|---|
| CH1 / CH2 gain (input_calibration per range) | 1-2 % | +-0.1-0.2 dB | < 0.02 dB (same ranges) | [E] |
| CH2 range change during a sweep | 1-2 % per range, possibly frequency dependent | step of up to 0.2 dB | spliced at the switch point (3.5) | [E] |
| Core A / B gain mismatch | 3.3 % before v3, < 0.1 % after | the CIC combines both cores and leaves their mean: part of the channel gain | covered | [F] `firmware/AGENTS.md` 21, 23 |
| FULL vs GATED class gap | 1.1 % | avoided: the sweep never uses gated rates | - | [F] `firmware/AGENTS.md` 21 |
| Decimation droop, delay | up to 2.7 dB / a few samples | identical on both channels: cancels | - | [I] |
| Noise | about 0.5-1 code rms raw; after a DFT over L x N raw samples about 0.003 code | negligible above 0.3 code peak | - | [E] 3.4 |
| ADC INL / DNL on small CH2 signals | 30 codes pk: +-0.05 dB, +-0.3 deg; 5 codes: +-0.2 dB, +-1.5 deg; 1 code: +-1 dB, +-6 deg | same | same | [E] the analog noise is the only dither |
| CH1 -> CH2 crosstalk and stimulus pickup | unknown, guess -60 to -70 dB | floor | measured as a floor sweep (bench 3) | [E] |
| Ground return through R71 | about 20 log(Z_ret / Z_in), up to about -24 dB at low f | **dominant if the clips carry the return** | not removed | [I] 2.5 |
| Offset-PWM ripple | about 0.5 code at j x 25 kHz | only within one bin of j x 25 kHz | - | [I] from `firmware/AGENTS.md` 21 (0.42 code mean shift at 200 MSa/s) |
| Inter-channel skew (front end, routing) | <= 2 ns guess | 0.07 deg at 100 kHz, 0.7 deg at 1 MHz | < 0.1 deg | [E] |
| Front-end bandwidth mismatch (~30 MHz, 10 %) | | 0.2 deg, 0.01 dB at 1 MHz | removed | [E] |
| x10 probe compensation | a few % shelf above ~1 kHz | +-0.3 dB | removed if probes stay on their channels | [E] |
| AC coupling | corners differ per channel | large phase error below ~100 Hz | partly | [I]: always sweep in DC coupling |
| Frequency | crystal tolerance, tens of ppm | exact relative to the sample clock | - | [I] |

Thru calibration (from the DPOS design): both probes on the DUT input node, one sweep with the same plan and ranges,
store H_cal per point; later sweeps show H / H_cal. Valid while the frequency plan, both ranges, probes and coupling
are unchanged; the UI says when it no longer applies (section 4).

### 2.3 Dynamic range, fixed versus auto range

| CH2 strategy | +-0.2 dB / +-1.5 deg down to | +-1 dB down to | Cost |
|---|---|---|---|
| Fixed at range 3 (fits a 0 dB passive DUT) | -27 dB | -41 dB | none |
| Down-ranging (3.5) to range 5 | -41 dB | -55 dB | 2-3 relay switches per sweep, 50 ms each |

The 16-bit path's extra bits help only as far as the analog noise dithers the 8-bit converter, so at 1-5 codes peak
the converter's linearity, not the resolution, sets the error [E]. That is why the design does not rely on a fixed
range alone: for stopband and notch measurements down-ranging buys 14 dB for well under a second per sweep. Fixed
range stays available, and it is what the first host version does.

### 2.4 The 1 kOhm source

For any linear two-port, V2 / V1 depends on the network and its load, not on what drives port 1 [F: two-port
algebra]. With CH1 on the DUT input node, H = V2 / V1 is therefore independent of R21, the post wiring and the
generator level. What R21 does change:
- **The level at the DUT input.** V1 = Vs x Zin / (Zin + 1 kOhm): a 100 Ohm input gives 9 % (0.18 V pk, still 10
  codes at range 3, 52 at range 5); 10 Ohm gives 1 %. The planner ranges CH1 too, and the UI warns below 20 codes.
- **Not the "insertion" response.** A filter specified for a 50 Ohm or 600 Ohm source shows its V2 / V1, not
  V2 / Vsource. Users who want the latter add their own source resistor after the post and probe CH1 before it (the
  impedance mode below then gives Zin as well).

Presentation: the connection text says "Source: calibration post, 3.1 Vpp square through 1 kOhm. H = CH2 / CH1:
CH1 must touch the DUT input." The header mentions the CH1 level only when it is low (warning, section 4).

**Impedance mode (Z).** A series resistor R_ext from the post to the DUT, the DUT to ground, CH1 on the post side,
CH2 across the DUT: Z = R_ext x V2 / (V1 - V2). Gives |Z| and phase, and at a marker L = X / omega,
C = -1 / (omega X) and ESR. For the detector work this is the coil measurement: inductance, Q, self-resonance and the
critical damping resistor of a PI coil [I]. Choose R_ext near the expected |Z| (100 Ohm - 10 kOhm). Host first,
device later.

### 2.5 The ground return (new; bench item 1)

The stimulus current must return to the FPGA's ground. Where it flows decides whether the measurement works.

- **DUT ground on a probe ground clip** (the obvious wiring): the current flows BNC shell, R71 (100 Ohm), VRF1, TL432,
  GND [F: the path from `fpga/al3/README.md` "Changes" 2]. Both front ends measure against VRF1, so both channels
  read the node voltage plus e = I_in x Z_ret, where Z_ret is 100 Ohm in parallel with the shell capacitors (100 nF,
  1 nF; whether they go to GND or to VRF1 is not traced) [I]. H_meas = (V2 + e) / (V1 + e). The error floor is about
  Z_ret / Z_in: an RC low-pass with R = 1.6 kOhm reads about 10 % (0.8 dB, 6 deg) wrong from 100 kHz up if the caps
  shunt R71 above about 16 kHz, and far worse (a floor near -24 dB) if they do not [E]. Ordinary probe compensation
  never shows this because a 1 MOhm input draws no current.
- **Recommended: DUT ground on the scope's digital ground** (USB connector shell, or GND of a USB breakout), **probe
  ground clips left open** [I]. The return then stays in the digital ground. The probes read against VRF1, which
  the TL432 ties to GND for AC, so the AC readings are correct; the DC offset of -1.24 V is rejected by the DFT. The
  DUT sees 0 / 3.1 V with a +1.55 V mean.
- **Not recommended: both** (a clip on a node tied to USB ground): R71 then loads VRF1 with about 12 mA, VRF1 sags to
  about 1.1 V and follows the 3V3 rail (`fpga/al3/README.md` "Changes" 2 (a)); every trace shifts.
- On battery without USB (on-device use) the user needs a ground wire to the USB shell. If the front panel has a
  ground post beside the calibration post, a DMM tells which ground it is (0 V to the USB shell = digital).

Bench item 1 measures the difference directly: the same RC low-pass in both wirings, and CH2 placed on the DUT
ground node (it then reads e itself).

**Bench 2026-10-19 (the reasoning above is kept; the measurement overrules it).** Bench 2026-10-19 (fw 0.2.50, `backups/bode/`): on a 1 kOhm / 15 nF RC, wiring A and wiring B gave the same H within 0.4 dB from 1 kHz to 300 kHz (rc1_15n_clips_2.json vs rc1_15n_usbgnd.json); in wiring B the floor was -80 dB, the thru +0.3 dB, and a 991 / 102 Ohm divider read flat -20.5 dB / 0 +-1.4 deg. The R71 return error did not show. Wiring B costs the 1.24 V between the grounds: with DC coupling it saturated CH2 on 0.1 and 0.2 V/div (a flat record the offset cannot reach). The predicted
return error needs Z_ret comparable to the DUT; with the shell capacitors and the TL432 holding VRF1 it stayed below
the 0.4 dB the two wirings differ by. Wiring A (DUT ground on the probe clips) is now the documented default, wiring
B the alternative with `ch2_coupling='ac'`; the tool flags a saturated CH2 (`ch2_overload`: a flat record, or a tone
that vanishes on a more sensitive range at the same frequency), steps an auto range back up and stops a fixed range
with that remedy (mcp-gen5).

### 2.6 Phase

Both channels are converted on the same encode edges (two AD9288s on the same 100 MHz pair,
`research/notes/SYNTHESIS.md` hardware summary), so H's phase needs no trigger and has no trigger jitter: the record's
start phase cancels in the ratio. The DDS 20 ns edge grid does not matter for integer M (the waveform is periodic).
What remains is inter-channel skew and front-end mismatch (2.2), both removed by the thru calibration. Phase is shown
wrapped to (-180, +180]; its noise is sigma_A / A rad, negligible above a few codes.

### 2.7 Frequency and the clean-edge set

f = 50 MHz / M. With inc = round(2^32 / M), f_a differs from 50 MHz / M by at most M x 1.2e-10 relative; the DFT uses
f_a.

| f | M | Step to M +- 1 | Densest log sweep without merged points |
|---|---|---|---|
| 10 kHz | 5000 | 0.02 % | any |
| 100 kHz | 500 | 0.2 % | any |
| 500 kHz | 100 | 1 % | 100 per decade (2.3 %) |
| 1 MHz | 50 | 2 % | 100 per decade |
| 2 MHz | 25 | 4 % | 50 per decade (4.7 %) |
| 5 MHz | 10 | 10 % | 20 per decade (12 %) |
| 10 MHz | 5 | 20 % | 10 per decade (26 %) |

Decision: **always restrict to integer M.** Below 500 kHz it is invisible; above, the sweep merges points that round
to the same M and reports the actual count and frequencies. Any integer M is fine for the single tone (odd M only adds
tiny even harmonics); duty threshold 2^31. Frequency changes are phase-continuous (a 0x51 write does not reset the
accumulator) [I], which shortens the DUT's transient.

Upper limit: R21 into the post, cable and x1 probe (~100 pF) makes a pole near 1.6 MHz on the stimulus itself
(`docs/plans/dpos350p-features.md` 3c). CH1 sees it, so H stays right, but V1 falls 20 dB per decade above it: about
2 MHz useful with x1 on CH1, 5-10 MHz with x10 (about 15 pF) [E]. Lower limit: about 1 Hz for patience (18 periods
per point); the generator holds 0.1 Hz cleanly (`fpga/al3/README.md` "Changes" 2).

### 2.8 Probe loading

CH1's probe loads the DUT input node: it is part of the source and drops out of the ratio. CH2's probe is part of the
DUT's load: 1 MOhm || ~100 pF (x1) or 10 MOhm || ~15 pF (x10) [E]. For a 10 nF tank, x1 shifts f0 by 0.5 % and x10
by 0.07 %; for coil self-resonance x10 is mandatory. x10 costs 20 dB of dynamic range (2.1), so the advice is x1 on
low-impedance outputs below 1 MHz and x10 on tanks and high-impedance nodes.

## 3. Sweep design

### 3.1 Frequency points

Start and stop (1 Hz .. 10 MHz), points 10 / 20 / 50 / 100 over the sweep, log spaced, snapped to integer M, merged
when equal. Order low to high (a DUT with DC-blocking capacitors settles its bias during the slow first points).
Source Internal only in v1; External (an external sine generator, f estimated from CH1, Hann window, no coherence) is
a later host option, as in the DPOS study.

### 3.2 Per-point plan (deterministic, shared by host and device)

```
M0     = round(50e6 / f_req)
if 50e6 / M0 > 1.5 MHz:  N = 1 (legacy 200 MSa/s, K = 0), M = M0
else:
  N    = 16 x 2^k, the smallest k >= 0 with 4 M0 / N <= 512        # FIR-legal (N % 4 = 0, N >= 16), D = N - 1
  if N == 16:  M = M0                                             # 24 kHz .. 1.5 MHz
  else:        S = round(4 M0 / N) (257 .. 512), M = S x N / 4    # below 24 kHz: M is a multiple of N / 4
skip the point if M equals the previous point's M
f_a    = round(2^32 / M) x 50e6 / 2^32                             # the frequency the DFT uses
p / q  = 4M / N in lowest terms (samples per period; q = 1 below 24 kHz)
L      = the smallest multiple of p holding >= 8 periods, grown to >= 2048 samples,
         at most 8190 (filtered) or 16382 (legacy)
filter = CIC3 + FIR (the firmware drops to CIC2 for N / 2 > 10334; GET_DECIM byte 30 says why, protocol row 47)
settle = max(settle_periods / f_a, 1 ms) + the firmware's filter settle ((K + 10) N 5 ns + 1 us, protocol row 49)
```

Why M is tied to N below 24 kHz: with a power-of-two N, p keeps the odd part of M (10 Hz as M = 5,000,000 would give
p = 78125, far past the depth limit), so no record would be coherent. Choosing M = S x N / 4 makes the samples per
period the integer S, at a frequency offset of at most 1 / (2S) = 0.2 % from the request, invisible on a log sweep.
Between 24 kHz and 1.5 MHz, N = 16 and p = M / gcd(M, 4) <= 2048 always fits.

N changes only when f crosses an octave of the ladder, so the time base and trigger total are rewritten about once
per octave below 24 kHz and never between 24 kHz and 1.5 MHz. Example: a 1 kHz request gives N = 512, S = 391,
M = 50048 (999.0 Hz), and L = 3128 samples hold exactly 8 periods.

### 3.3 Settling

The DUT's transient after a frequency step decays with its own time constants. For a resonance of quality Q the
envelope time constant is Q / (pi f0), i.e. Q / pi periods; -40 dB needs 1.47 Q periods, -60 dB 2.2 Q [arithmetic].

| Settle preset | Periods before the record | DUTs it settles to -40 dB | Use |
|---|---|---|---|
| Fast | 3 | RC / RL, Q <= 2 | overview |
| Normal (default) | 10 | Q <= 7 | most filters |
| Resonant | 50 | Q <= 34 | coils with capacitors, LC filters |
| High Q | 200 | Q <= 136 | tanks |

Plus a fixed 1 ms minimum, and three times the preset at the first point (bias settling of coupling capacitors).

**Free settle check** [I]: split the coherent record into halves (the plan picks an even number of p-blocks where it
can) and compute H on each. If they differ by more than 0.1 dB or 1 deg, the point was still settling: recapture once
with the settle time doubled; if it still fails, flag the point "unsettled". The header then suggests the next preset.

The FPGA filter's own settling after a divider or mode change is a few ms at most (protocol row 49 formula; 1.2 ms at
N = 20000), and the firmware already waits it out before the sampling reset.

### 3.4 Capture length and resolution

The estimate's noise is sigma_A = sigma x sqrt(2 / (L x N)) for white noise, since every raw sample contributes
through the filter [E]. With sigma = 1 code raw:

| f | N | L | Raw samples used | sigma_A (codes) | SNR at 1 code peak |
|---|---|---|---|---|---|
| 10 Hz | 65,536 (CIC2) | 2440 | 1.6e8 | 0.0001 | 79 dB |
| 1 kHz | 512 | 3128 | 1.6e6 | 0.0011 | 59 dB |
| 100 kHz | 16 | 4000 | 6.4e4 | 0.006 | 45 dB |
| 2 MHz (legacy) | 1 | 8200 | 8e3 | 0.016 | 36 dB |

Noise is never the limit for points above 1 code peak. The record length is set by settling and the half-record
check, not by SNR; longer records (L up to 8190) matter only near the floor.

### 3.5 Ranging

- **CH1**: chosen once from a pre-scan capture at the first frequency (the most sensitive range that keeps the peak
  inside 85 % of full scale), re-checked when CH1 clips or drops below 20 codes.
- **CH2, Fixed**: the user's V/div, or a pre-scan of the first, middle and last point plus 6 dB margin.
- **CH2, Auto (down-ranging)**: before each point, predict the CH2 peak from the previous point. If it is below 25 %
  of full scale and a more sensitive range exists, switch (the firmware holds ARM 50 ms for relay settling, protocol
  8.3 SET_CHANNEL) and measure this point on both ranges once: the splice scales the new range so the two agree at
  this frequency. On a clip, step up one range and repeat the point. A monotonic filter response needs at most 3
  switches (range 3 to 5); a resonance peak up to 2 more.
- Offset: each range change re-centres the offset PWM through the firmware's normal channel path.

### 3.6 Background capture

Once per distinct N (a handful per sweep): one capture with the generator off, giving each channel's level in every
bin the sweep will use (offset-PWM ripple, mains hum, ambient pickup). A point whose CH2 level is less than 20 dB
above the background in its bin is flagged "near floor". Switching the generator off for one capture briefly changes
the DUT's DC state; the next point's first-point settle covers it.

### 3.7 Sweep time

Per point: overhead + (settle + record) periods / f. Normal settle, C = 8: 18 periods. Overheads [E]:
- **Host**, about 40 ms: SET_GEN round trip, ARM, WAIT, READ_RAW of 2 x 4096 16-bit samples (4.9 ms on the FPGA bus
  at 0.3 us per byte plus about 7 ms of USB at the measured 2.35 MB/s, `docs/hardware-status.md` REMOTE throughput),
  plus Python.
- **Device today**, about 15 ms: readout 4.9 ms, DFT about 3 ms uncached at ~90 cycles per data access
  (`firmware/AGENTS.md` 20), an incremental plot update.
- **Device with the D-cache**, about 10 ms: the bus readout is GPIO-bound and does not speed up [I].
- Relay switches and background captures add about 0.3 s per sweep.

| Sweep | Points | Periods / f summed | Host | Device today | Device cached |
|---|---|---|---|---|---|
| 1 kHz - 100 kHz | 20 | 0.08 s | 0.9 s | 0.4 s | 0.3 s |
| 1 kHz - 1 MHz | 50 | 0.14 s | 2.1 s | 0.9 s | 0.6 s |
| 100 Hz - 1 MHz | 50 | 1.05 s | 3.1 s | 1.8 s | 1.6 s |
| 10 Hz - 100 kHz | 50 | 10.5 s | 12.5 s | 11.3 s | 11.0 s |
| 10 Hz - 1 MHz | 100 | 16.4 s | 20.4 s | 17.9 s | 17.4 s |
| 40-60 kHz tank, High Q | 100 | 0.42 s | 4.4 s | 1.9 s | 1.4 s |
| Fast mode 10 Hz - 100 kHz (8 fundamentals) | ~40 | 2.6 s | 3.0 s | 2.8 s | 2.7 s |

Conclusions: above about 1 kHz the per-point overhead dominates and the device is 2-3x faster than the host; below
100 Hz the DUT's periods dominate and only the fast mode helps. The D-cache barely matters for Bode.

## 4. UI (on-device view)

A display mode of the main loop like the spectrum and phosphor views: no modal loop, the MCP keeps working, GET_STATUS
byte 43 display mode 5. It owns the generator while it is on: on entry it saves the generator state and the trigger
source, sets the trigger to CH1 if it was EXT (protocol row 41 interlock), switches both channels on in DC coupling,
and restores everything on exit.

**Entry.** The main menu has no room for an eighth row (rows 6 and 7 fill y 343..459, `pc_hal_scope.c`). Recommended:
a "Bode sweep" row in the generator menu, since Bode is a use of the generator. Alternative for the maintainer to decide:
main-menu row 6 becomes "Analyzer" with a Spectrum / Phosphor / Bode choice.

**Screen** (trace area 728 x 432 at (2, 48), as the spectrum view, `firmware/README.md` "Spectrum view"):
- **Header, one line**, left to right: the marker readout ("M 15.0 kHz -4.2 dB -51 deg") or, without a marker, the
  automatic markers ("-3 dB 10.2 kHz" for a single crossing; "Peak 50.3 kHz +5.1 dB  Q 23" for an interior maximum
  with two crossings); a "Thru cal" tag while a valid calibration is applied; at the right "23/50  4 s" while
  sweeping, else the point count.
- **Two stacked panes sharing the log frequency axis** (no twin y axes): magnitude, 6 divisions, 5 / 10 / 20 dB per
  division with the reference at the top line; phase, 4 divisions, -180..+180 deg at 90 deg per division. Decade grid
  lines with 2 and 5 sub-lines; labels under the plot in the spectrum's number format ("100", "1k", "10k").
- **Colours**: magnitude in the CH2 colour, phase in a second fixed colour, points as small dots joined by lines.
  Colour shows state: flagged points (near floor, unsettled, CH1 low) grey with no joining line; the thru-cal
  reference dim; markers as white triangles like the spectrum's peak marker; the sweep's current frequency as a thin
  grey vertical line.
- **Before the first sweep** the plot shows the connection text instead of an empty grid:
  ```
  Cal post --+-- DUT input  (CH1 tip)        DUT output  (CH2 tip)
             1 kOhm source, 3.1 Vpp square, H = CH2 / CH1
  DUT ground -> USB ground (USB shell). Probe ground clips: leave open.
  ```
- **Strip** (bottom row; the box shows the value, a tap steps it, as in the spectrum strip):

| Field | Values | Tap |
|---|---|---|
| Start | 1 Hz .. 5 MHz in 1-2-5 steps | left half down, right half up |
| Stop | 10 Hz .. 10 MHz in 1-2-5 steps | same |
| Points | 10, 20, 50, 100 | next |
| Settle | Fast, Normal, Resonant, High Q | next |
| Mode | Sweep, Fast (harmonics) | next |
| CH2 range | Auto, Hold (the channel's V/div) | next |
| Cal | Off, Thru (green: valid; orange: settings changed since) | runs the guided thru calibration |
| Single | | one sweep, then STOP |
| Scope | | leave the view |

- **Controls**: the right column's RUN / STOP keeps its meaning. RUN repeats sweeps, each overwriting point by point;
  STOP finishes the current point and holds the data. A tap on the plot places a marker on the nearest point; a drag
  moves it along the points. This deliberately differs from the spectrum view's tap-to-zoom: the Bode axis is the
  sweep range (Start / Stop), and zooming would mean re-sweeping. Identical settings give identical frequency plans.
- **Warnings** (orange, centred over the plot, consequence then action, only while they apply):

| Warning | When |
|---|---|
| CH2 overrange: set CH2 range Auto or raise V/div | a point clipped with CH2 range Hold |
| CH1 level low: DUT input loads the 1 kOhm source | the CH1 fundamental below 20 codes on the most sensitive range |
| N points near the floor: shown grey | section 3.6 flags |
| N points not settled: choose Settle Resonant | the section 3.3 check failed after the retry |
| Thru cal does not match: run Cal again | ranges, probes, coupling or the frequency plan changed since the calibration |
| Generator was the EXT trigger: trigger set to CH1 | on entry, once |

- **Persistence**: Start, Stop, Points, Settle, Mode and CH2 range in a new settings word (211 if still free); the
  thru calibration in RAM, optionally on the card later.

## 5. Protocol and MCP

### 5.1 Host-driven first (no firmware change)

All commands exist. Per sweep: SET_REMOTE; SET_CHANNEL both DC with their ranges; SET_TRIGGER source CH1, mode auto,
pretrigger 0, total = L; SET_DECIM CIC3+FIR. Per point:

```
SET_GEN      freq_mode 1 (raw phase_inc = round(2^32 / M)), duty_mode 1 (2^31), enable
SET_TIMEBASE mode 1, divider N - 1 (only when N changes; for even N the filter's 2 (floor(D/2) + 1) = D + 1 = N)
GET_DECIM    effective K, S, gain, why (protocol row 47 bytes 2-15 and 30)
(sleep the settle time on the host)
ARM; WAIT (record time + 100 ms); FORCE if not DONE
READ_RAW     layout 2 (16-bit, both channels, count L), or layout 1 for legacy 200 MSa/s
SET_CHANNEL  only for an auto-range step
```

Codes from the 16-bit words: code = out x 2^S / gain + corr16 / 16 (protocol row 49), volts with the calibration of
protocol 8.5 and GET_CAL. The ratio needs only the relative scale, so the volts model matters only at range changes.
The MCP tool must use the generator's confirmation token (it drives the pin: tier 3, as `scope_generator`) and must
give the generator back in the state it found.

**MCP tool `scope_bode_sweep`**:

| Parameter | Default | Meaning |
|---|---|---|
| start, stop | 100 Hz, 1 MHz | sweep range |
| points | 50 | log spaced, snapped and merged |
| settle | "normal" | fast / normal / resonant / high_q, or a number of periods |
| mode | "tone" | tone / harmonics |
| ch2_range | "fixed" in v1, "auto" from step H3 | |
| thru_cal | none | "measure" (both probes on the DUT input) or a saved calibration to apply |
| z_series_ohm | none | impedance mode with that series resistor |
| confirm_token | | as for scope_generator |

Returns: the frequency plan actually used; per point f_hz, mag_db, phase_deg, v1_vrms, v2_vrms, ch2_range and flags
(near_floor, unsettled, ch1_low, clipped, spliced), inline up to 200 rows; markers (-3 dB crossings, peak, Q, phase
crossings); timing; files: .csv (the table), .npz (also the complex X1 / X2 per point and the capture metadata) and a
.png Bode plot (matplotlib as an optional dependency; without it, no PNG); caveats in the existing style (wiring rule,
flagged points, the 1 kOhm note). Thru calibrations are stored and listed under `mcp/data/`. The measurement guide
(`docs/agent-measurement-guide.md`) gets a Bode section with the wiring rule.

### 5.2 On-device later

Two commands, taking the next free numbers (0x020B is unused in `pc_protocol.h`; check why before using it):
- **BODE_CTRL**: start / single / stop with the strip's parameters (start and stop in mHz, points, settle, mode, range
  mode, flags); answers the plan size and an estimated duration.
- **READ_BODE**: a 32-byte header (sweep id, state, points done and total, flags, calibration state), then 16 bytes
  per point: u32 f in mHz, i16 magnitude in centi-dB, i16 phase in centi-degrees, i16 V1 and i16 V2 in centi-dBV,
  u8 CH2 range, u8 flags.

The MCP tool then gets `where="device"`, the same table from READ_BODE, about 2-3x faster above 1 kHz. This needs a
protocol minor version, a section 14 row and vectors for the planner, so Python and C produce the same plan.

## 6. Implementation plan

### 6.1 Steps

| Step | What | Who / effort | Acceptance |
|---|---|---|---|
| H0 | Bench items 1-4 below with existing tools (a scratch script over READ_RAW and SET_GEN) | bench session + maintainer, 1 h | wiring rule confirmed or replaced; floor and skew recorded in `docs/hardware-status.md` |
| H1 | `mcp/src/fnirsi1013d/bode.py`: planner (M, N, p / q, L, settle), DFT, H, markers, flags; pure numpy; unit tests with synthetic squares through RC / RLC models, folded harmonics and non-coherent low f included | Opus agent, 1 day | plan vectors; < 0.01 dB / 0.05 deg on a synthetic RC with 8-bit quantisation and noise |
| H2 | Simulator: a DUT model (RC, RLC, tank) between the generator and CH2 in `simulator.py` | same agent, 0.5 day | the tool runs in CI against the simulator |
| H3 | `scope_bode_sweep` with fixed range, thru cal, CSV / NPZ / PNG, caveats and the guide section; then auto range with the splice | same agent, 1 + 0.5 day | bench item 5 |
| D1 | `bode_dsp.c/.h`: fixed-point planner and phasor DFT, host-buildable, a C twin for any assembly, vectors shared with H1 | Opus agent, 1 day | host tests equal to the vectors; `make -C host_tests test-arm` |
| D2 | Sweep engine in the main loop: a state machine that drives the generator, raw divider and filter through the existing pc_hal paths, sets `pc_glue_ui_decim_arm` for its captures (`firmware/AGENTS.md` 19) and reads `pc_hal_trace16` | Opus agent, 2 days | the bench-5 RC sweep within 0.05 dB of H3 |
| D3 | Bode view: panes, header, strip, markers, warnings, connection text, entry row | Opus agent, 2-3 days | maintainer UI verdict |
| D4 | BODE_CTRL / READ_BODE, protocol row, mcp `where="device"` | 1 day | host and device tables agree |
| D5 | Fast (harmonic) mode on both sides, impedance mode on the device, thru cal on the card | 1-2 days | fast-mode points within 0.3 dB of the tone sweep where valid |

**Status 2026-10-19 (fw-bode, fw 0.2.66, protocol 1.20 provisional; not benched):** D1-D4 are built: `bode_dsp.c`
(planner and DFT, host-tested against H1's plan vectors and `measure_pair`), `bode_sweep.c` (the runner as a main-loop state
machine), `bode_view.c` (the view; entry from the generator menu, maintainer), `pc_bode.c`, BODE_CTRL / READ_BODE (protocol row
60; rows of 20 bytes with the DDS period M and f in cHz, since 10 MHz does not fit a u32 in mHz). Deviations from section 4:
the strip became the CTRL page's Sweep and Thru fold-outs and the header the cards (UX makeover); AUTO SET runs one sweep;
no Mode field (the fast mode is D5). The mcp side (`where="device"`) is open. Handoff and bench procedure:
`firmware/AGENTS.md` section 42.

### 6.2 Risks

| Risk | Consequence | Mitigation |
|---|---|---|
| Ground return through R71 (2.5) | wrong results with the natural wiring | bench item 1 first; connection text and tool caveats; a digital-ground lead for on-device use |
| Crosstalk / pickup floor above -55 dB | less dynamic range | floor sweep, grey points; an external attenuator plus x1 on CH2 for deep stopbands |
| Clock coherence assumption wrong | leakage, harmonic contamination | bench item 4; fall back to a Hann window and C >= 16 |
| DUT back-drives the post (active circuit, PI detector) | FPGA pad damage: R21 is the only protection (`fpga/al3/README.md` "Changes" 2 (b)) | UI and tool text: passive DUTs or buffered inputs only; never the coil or the MOSFET drain |
| Stimulus too large for active DUTs (3.1 Vpp, +1.55 V mean) | clipping, slewing: H invalid | external divider and coupling capacitor before the CH1 point; the half-record check |
| Host E_BUSY when the maintainer opens a menu | the sweep aborts | the tool retries a point once, then stops with the partial table |
| Long low-frequency sweeps | tool timeouts | the tool estimates the duration first and refuses above 120 s unless asked |
| Range-dependent front-end response | steps at range switches above ~1 MHz | splice at the switch point; thru cal per range pair if the bench shows it matters |

### 6.3 Bench first (maintainer with the bench session, after H1-H3 or with a scratch script)

DUT: RC low-pass, R = 1.60 kOhm 1 %, C = 10.0 nF film or C0G: fc = 9.95 kHz. CH1 x1 on the RC input (the post), CH2 x1
on the output (its ~100 pF adds about 1 %: fit C_total from the -45 deg point). DC coupling, generator on.

| f | Expected abs H | Expected phase |
|---|---|---|
| 1 kHz | -0.04 dB | -5.7 deg |
| 3 kHz | -0.38 dB | -16.8 deg |
| 10 kHz | -3.03 dB | -45.2 deg |
| 30 kHz | -10.0 dB | -71.7 deg |
| 100 kHz | -20.1 dB | -84.3 deg |
| 300 kHz | -29.6 dB | -88.1 deg |

1. **Ground wiring**: 30 points 1 kHz - 300 kHz with the DUT ground on the CH1 / CH2 clips, then on the USB shell with
   the clips open. Then CH2's tip on the DUT ground node in the first wiring (it reads e). Expect a visible
   difference from about 30 kHz up if section 2.5 holds; adopt the better wiring.
2. **Thru**: both probes on the post, 100 Hz - 2 MHz: |H| and phase raw (the uncalibrated mismatch: record it), then
   with the thru calibration applied (expect +-0.02 dB, +-0.1 deg).
3. **Floor**: CH1 on the post loaded with 1 kOhm to the DUT ground, CH2's tip on that ground: |H| over the sweep is the
   isolation floor.
4. **Coherence**: at M = 50 (1 MHz) and M = 5000 (10 kHz), the spectrum of a coherent CH1 record: the bins next to
   the harmonics must sit at the noise floor. At 900 kHz (non-integer M) the 100 kHz pattern tone must appear, at
   1 MHz not.
5. **RC sweep**: 50 points 1 kHz - 1 MHz, Normal settle. Accept: within 0.2 dB and 1 deg of the model from 1 kHz to
   100 kHz, fc within 2 %, the -3 dB marker at 9.9-10.0 kHz; five repeats spread under 0.05 dB.
6. **Tank**: 1 mH parallel 10 nF (f0 = 50.3 kHz) fed through 10 kOhm from the post, CH2 x10 on the tank, Resonant
   settle: the peak marker at f0 within 0.5 %, Q against an LCR meter if one is available; then Normal settle, which
   the settle check should flag.
