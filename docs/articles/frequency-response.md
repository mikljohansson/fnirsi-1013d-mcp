# Frequency response: design and bench

The 1013D has no signal generator in the usual sense. Its only output is a logic-level square wave from the FPGA's DDS
on the calibration post, through a 1 kOhm resistor. This article explains how the project measures the magnitude and
phase of a filter with that square wave and two 8-bit channels: the plan, the host tool, the on-device engine, and how
both were benched on an RC low-pass on the owner's unit.

Sources: the [Bode design study](../plans/bode-design.md), the [Bode UI plan](../plans/bode-ui.md),
[firmware/README.md](../../firmware/README.md) "Frequency response", [firmware/AGENTS.md](../../firmware/AGENTS.md)
sections 42 and 55 to 61, the host tool [bode.py](../../mcp/src/fnirsi1013d/bode.py) and the
[bench log](../hardware-status.md). "Bode" is the internal name; the mode on the screen is "Frequency response".

## The principle

CH1 sits on the input of the device under test (DUT), CH2 on its output, and the post drives the input. For each
frequency the tool computes the complex amplitude of the fundamental on both channels and takes H = X2 / X1.

Three facts make a square wave usable (design section 1):

- **Coherent records.** A DFT at f over an exact whole number of periods has zeros at every other harmonic of f. So the
  square's 3rd, 5th and higher harmonics drop out, and so does DC. A rectangular window is enough.
- **Clean edges.** The DDS runs on a 50 MHz clock. For f = 50 MHz / M with an integer M every period has the same edge
  pattern. Other frequencies put a real tone into the stimulus; the [decimation article](decimation.md) tells how a
  900 kHz square was found to carry 100 kHz at about -37.5 dBV. The plan therefore always uses an integer M.
- **The source drops out.** For any linear two-port, V2 / V1 does not depend on what drives port 1. With CH1 on the
  DUT's input, the 1 kOhm and the generator level do not matter.

Both channels are converted on the same encode edges, so the phase needs no trigger: the record's start phase cancels
in the ratio. The design also notes, as an inference, that the DDS and the ADC clocks both come from the 50 MHz crystal,
so stimulus and sampling are locked.

## The frequency plan

Each point gets a deterministic plan, shared by the host and the device (design 3.2):

- Above 1.5 MHz: legacy 200 MSa/s.
- 24 kHz to 1.5 MHz: CIC3 + FIR at N = 16, that is 12.5 MSa/s.
- Below 24 kHz: N = 16 x 2^k, the smallest that gives at most 512 samples per period. M is then rounded to a multiple
  of N / 4, so a period is a whole number of samples (257 to 512). The frequency moves by at most 0.2 %.
- The record holds at least 8 periods and 2048 samples, at most 8190 with the filter.

The filter's droop and delay are identical on both channels and cancel in the ratio. Every point stays in the
calibration's full-rate class, so the 1.1 % gap between the clock classes never shows as a step.

**Per point:** set the DDS, wait a settle time, capture both channels, take the single-bin DFT at the exact DDS
frequency f_a = round(2^32 / M) x 50 MHz / 2^32, and compute H. The settle time is a number of periods: Fast 3,
Normal 10, Resonant 50, High Q 200, three times that at the first point. A resonance of quality Q needs about 1.47 Q
periods to settle to -40 dB.

**Checks and flags.** The record is split into halves; if their H differs by more than 0.1 dB or 1 degree the point
is taken again with twice the settle time, then flagged unsettled. CH1's level one bin beside the fundamental, above
-40 dB, means the record was not coherent. Once per rate and CH2 range a capture with the generator off measures the
background; a point less than 20 dB above it is near_floor. Other flags: ch1_low (CH1 under 20 codes), ch2_small
(CH2 under 3 codes, where the ADC's linearity limits the result to about 1 dB), clipped, spliced.

**Ranging.** A pre-scan at the first frequency picks both ranges at 85 % of full scale. With CH2 on Auto the sweep
steps CH2 to a more sensitive range when the next point would fit, and measures that point on both ranges, so the
splice makes the two agree.

## The ground-return prediction

The design study's biggest new risk was the ground. The BNC shells are not at the scope's digital ground. They sit on
VRF1, a 1.24 V reference tied to ground through R71, 100 Ohm. The study reasoned that a DUT grounded on a probe clip
would return the stimulus current through R71 and add a common error to both channels, possibly a floor near -24 dB.
It recommended grounding the DUT on the USB shell with the clips open ("wiring B"). This was marked as an inference to
check on the bench first.

## The host tool on the bench (2026-10-19)

The host tool came first (`scope_bode_sweep`, `fnirsi1013d-bode`): no firmware change, the existing commands. The owner
wired an RC low-pass, 1 kOhm and 15 nF (fc 10.6 kHz), and the coordinator swept 1 kHz to 300 kHz in 30 points.

- **14:20.** V2 was 25 mV flat: CH2 was not on the R-C junction. Rewired.
- **14:35.** 30 of 30 valid, +0.08 dB at 1 kHz, -3 dB at 11.3 kHz. Above 30 kHz the slope was shallower than one pole:
  -24.6 dB and -70 degrees at 300 kHz against -29.0 dB and -88 expected.
- **14:40 to 14:50.** The DUT ground on a BNC shell, then on a USB extension's shell with the probe grounds floating:
  the same within 0.4 dB. The R71 return path did not explain the deviation.
- **14:55, floor.** CH2's tip on the ground rail: -78 to -90 dB at every point. Crosstalk was not the cause, and the
  floor was 25 dB better than the design assumed.
- **14:58, thru.** Both tips on the input: CH2 / CH1 within +0.15 to +0.30 dB and 0 to 0.5 degrees. The channels
  matched.
- **15:05, divider.** 991 Ohm over 102 Ohm: -20.62 to -20.43 dB against -20.60 expected, phase -0.2 to +1.4 degrees.
  The tool was flat.
- **15:15 to 15:25.** A second capacitor of the same type showed the same bend. A high-pass built from the same parts
  matched 1 - H of the low-pass within 0.14 dB and 1.0 degree at every point.

The conclusion: the deviation was the capacitor. Its effective value fell from 14.7 nF at 1 kHz to 9.8 nF at 200 kHz,
with a loss tangent rising from 0.03 to 0.29: a lossy, dispersive ceramic (owner: AliExpress parts, likely Y5V class).
The tool was accepted.

One more finding came out of it. In wiring B, CH2 read almost nothing on 0.1 V/div. A test at 15:20 showed why: with
the DUT on USB ground it sits about 1.2 V below the probe reference, and that DC saturates the 0.1 and 0.2 V/div front
end before the offset can correct it. Since wiring A measured the same H, the clips became the recommended wiring, and
the tool got a ch2_overload flag.

## The engine on the device

fw-bode built the same measurement into the firmware (fw 0.2.66): the planner and DFT in fixed point (`bode_dsp.c`),
the runner as a main-loop state machine (`bode_sweep.c`), the view, and BODE_CTRL / READ_BODE for the host. Its host
test runs 3000 random points and 300 random sweeps against the Python planner and gets identical plans, with H within
0.002 dB and 0.002 degrees of the host's. One deviation is documented: the noise estimate is a median of period
differences in the time domain instead of the host's median over FFT bins, since the device has no
non-power-of-two FFT.

fw 0.2.69 gave it the redesigned screen of the UI plan, as the main menu's third row.

## The device sweep on the bench

The first device sweeps, on 2026-10-19 at 20:43, were wrong: every point flagged, both channels at 5 V/div. The cause
was the A-core capture fault of bitstream 0x1539 (see the [capture-fault article](adc-capture-fault.md)). One rail
sample counted as a clip, the pre-scan backed off to 5 V/div, and there the whole signal sat in the bad codes.

With 0x153A trained on 2026-10-20, a run of faults showed one after another, each fixed in turn:

| Time | Firmware | Found | Fixed by |
|---|---|---|---|
| 10:52 | 0.2.82 | sweeps end with "ADC readout fault" | the engine judged clipping from a sticky overload flag; 0.2.83 judges each record alone |
| 11:10 | 0.2.83 | the pre-scan sometimes left CH1 at 2.5 V/div | the first-record saturation; 0.2.88 |
| 12:32 | 0.2.88 | records started by the FPGA's auto time-out were unstable near the knee | 0.2.89 arms every capture on a CH1 trigger |
| 13:06 | 0.2.89 | ranges churned between sweeps | 0.2.90 and 0.2.91: a step up needs two clipping records |
| 13:28 | 0.2.91 | | clean |

The 12:32 test was decisive. A raw-cell script took 8 captures at each of 4 frequencies in 3 trigger variants. The
host-style and triggered variants were stable at every cell. The auto time-out variant, the engine's, gave bad records
at 6 of 8 and 5 of 8 captures at two cells, without rail samples. Later analysis found 15 of 32 time-out records
spliced from two captures, with a phase jump at a random sample, and 64 of 64 triggered records clean.

fw 0.2.91 set the engine's trigger level at CH1's expected mean, so the time-out never fires. At 13:28, ten sweeps of
100 Hz to 1 MHz in 25 points:

- range switches 2 on the first sweep, then 0;
- 0 time-outs and 0 retakes, 0 flagged points on sweeps 2 to 10;
- 10.0 kHz at -2.85 or -2.86 dB every sweep (host: -2.854 dB);
- 2.14 s per sweep.

Earlier, at 12:05, a clean device sweep and a host sweep of the same RC taken right after agreed within 0.05 dB and
0.5 degrees at every point.

## AUTO SET and repeated sweeps

AUTO SET runs a 20-point overview from 100 Hz to 10 MHz, then picks the span from it: fc / 10 to 10 fc for a low- or
high-pass, around f0 by Q for a resonance, with finer passes when the overview cannot resolve a sharp tank. Two owner
decisions of 2026-10-20 (11:47-11:52) shaped it. AUTO SET ends with one sweep at the found settings and STOP, never in
RUN; the owner had reported that AUTO SET "still loops", and the bench read that as RUN repeating with the relays
clicking. And repeated sweeps at the same settings reuse
the previous point-by-point range plan, so the relays stay quiet. fw 0.2.93 adds a choice between CH2 Hold and Auto: Hold when the CH2 levels inside the span spread less than
20 dB.

At 13:57 on fw 0.2.93, five sweeps at AUTO SET's span (100 Hz to 1 MHz, 25 points) each read 0 unplanned and 2 planned
range switches, 0 time-outs, 0 retakes, 0 flagged points, in 1.5 s.

## Limits

- Passive DUTs or buffered inputs only. The 1 kOhm resistor is the post's only protection, so never a coil, a MOSFET
  drain or anything that can drive the post.
- About 2 MHz is useful with x1 probes on CH1; the post's resistor and the probe capacitance make a pole near 1.6 MHz
  (design estimate).
- About -55 dB is the floor of the hardware for a 3.1 Vpp stimulus: one code peak on the most sensitive range.
- The device keeps CH2 DC coupled. The fast harmonic mode and the impedance mode of the design are not built.
- The design's tank test (bench item 6) is not in the bench log yet.
