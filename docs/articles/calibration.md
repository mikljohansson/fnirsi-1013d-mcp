# The calibration story

Each channel of the FNIRSI 1013D has one AD9288 with two 8-bit converters, core A and core B. The FPGA clocks them half
a period apart, so together they give 200 MSa/s. Each core has its own zero and its own gain. When they disagree, a
200 MSa/s trace alternates sample by sample between two levels, and the spectrum shows a tone at half the sample rate.
This article follows how that mismatch was measured and corrected, from Atlan4's 1.00o5 firmware to a background
tracker. All numbers come from the maintainer's unit, the only one tested.

Sources: [firmware README section 6](../../firmware/README.md), the
[interleave tracking plan](../plans/interleave-tracking.md), [firmware/AGENTS.md](../../firmware/AGENTS.md) sections 21,
23 and 28, and the [bench log](../hardware-status.md).

## What 1.00o5 did

The project's firmware is based on Atlan4's v1.00o5, which builds on pecostm32's open firmware (see the
[README credits](../../README.md)). Its base calibration finds, per range, the offset PWM that puts core A at code 128.
It then averages the integer difference B - A over the six ranges with truncation and splits it into two whole-code
compensations, one per core.

A separate volts calibration uses an external DC source at 300 mV to 15 V and fits one scale factor per range. It
assumes the zero sits at exactly code 128, so any zero error goes into the gain.

The maintainer ran both on 2026-10-18 on the stock bitstream 0x1432. The first volts attempt failed because the probes were
on 10x; the routine needs 1x.

## The first measurements

The T8 bench the same day (fw 0.1.7, CH1, bench supply and DMM) found CH1's raw core zeros at 132.5 (A) and 127.0 (B):
a spread of 5.5 codes. The stored compensations were -2 and +2: the integer truncations had lost the rest. CH2 was
close to the model (A 128.4, B 128.9).

The spectrum view showed the result. At 1 MSa/s the bin at half the sample rate read -32 dBV against a -50 dBV floor.
A live capture had core A at 130.93 and core B at 125.58. The sample-skipping decimation alternates cores, so the A/B
offset becomes a square wave at fs/2.

## Two clock classes

The first decimation bitstream, 0x1534, gave an unexpected step. A steady level read 84.40 codes in legacy mode and
86.2 codes with any filter. Both paths weigh the cores equally, so the firmware arithmetic was not the cause.

A READ_RAW sweep across sample rates found it. At 200 MSa/s CH1 read A 87.80 and B 85.09. At 20 MSa/s and every slower
rate it read A about 87.0 and B about 81.5. Core B dropped 3.6 codes as soon as the clock was gated, core A 0.8. In
legacy mode Atlan4's `zaklad.v` gates the ADC encode clocks with the sample-rate divider, so below 200 MSa/s each core
runs slower. With the filter on, the ADCs always run at 100 MHz per core. The repository does not know the mechanism
inside the AD9288. It notes, as an inference, that the datasheet minimum of 1 MSPS is not the explanation, since the
full step already shows at 20 MSa/s.

From then on the calibration has two clock classes. FULL covers 200 MSa/s and every filtered capture. GATED covers
every slower legacy rate.

**2026-10-21 to 22: one class tried, and reverted.** The 0x153C hardware counter miscounted with Filter Off at a
divided rate, because the gated encodes fed it the divided stream. The owner decided that FPGA 0x153D should run the
ADC encodes at 200 MSa/s at every rate and keep one sample pair in every D + 1, and fw 0.2.105 merged the two classes
into one for it (fw-fullrate). The bench the next morning (11:36, the same probes and script on both bitstreams, share
of samples at least 4 codes off a 3-sample median, Filter Off) turned that around. 0x153D read core A 3.04 % and core
B 1.29 % at 200 MSa/s and 1.32 % / 0.92 % at 2 MSa/s; 0x153C with fw 0.2.104 read 1.01 % / 0.86 % at 200 MSa/s and
**0 / 0 at 2 MSa/s**. The full-rate encodes had brought the partner cores' coupling to every time base, and 0x153D's
encode timing was worse at 200 MSa/s too. The owner reverted both (11:45): 0x153D takes 0x153C's encode generation
back, the calibration its GATED class (fw-gated), and the counter feeds the cards only while the encodes run at full
rate (200 MSa/s, a filter, Peak). A calibration saved by the single-class builds still loads, with its 200 MSa/s values
standing in for the gated ones until the next Calibrate.

## Calibration v2: per-core zeros (fw 0.2.43)

fw-cal built calibration v2 on 2026-10-18. The model per channel, range, core and class is a zero plus an offset slope:

    raw_k(0 V) = zero[class][core][range] / 16 + slope[core][range] / 65536 x dcoffset

The measurement takes each core's slope either side of the PWM centre, refines the centre so the mean of both gated
cores reads 128, and measures the zeros there, in about 5 s. A range fails on clipping, noise above 3 codes RMS, drift
above 0.5 code or implausible values, and nothing changes unless both channels pass. The correction works in
sixteenths of a code: a whole-code offset plus one more code with a probability set by the fraction (a xorshift
dither), so the mean is right to 1/16 code without tones. The block sits in sector 708 outside Atlan4's checksummed
area, so an older firmware ignores it.

The maintainer ran it the same evening. With both probes shorted, the firmware's own traces read:

| Rate | CH1 corrected A - B | Mean |
|---|---|---|
| 200 MSa/s | +0.42 code | 127.9 |
| 1 MSa/s | +0.07 code | 127.9 |
| 10 kSa/s | +0.02 code | 127.9 |
| CIC2 | | 127.94 |

The gated and filtered paths now agreed. The 200 MSa/s residual came from the 25 kHz offset-PWM ripple (one record
spans 5 us of its 40 us period); 64 full-rate conversions instead of 8 fixed it (commit 3bfc892). The pass time also
dropped from 44-58 ms to 30 ms, because the per-sample division of 1.00o5 was gone. The fs/2 spur dropped to -34.7 dBV against a -61 dBV floor, from -27 against -50. Better, but still
26 dB above the floor.

## A gain mismatch

A zero-only model cannot remove an error that grows with the signal. The maintainer set 1.0045 V and then -1.0045 V
(measured with a DMM) on CH1, range 2. The endpoint gains over 2.009 V were:

| Class | Core A | Core B | A/B |
|---|---|---|---|
| Full rate | 28.62 codes/V | 27.71 codes/V | 1.033 |
| Gated | 28.54 codes/V | 28.42 codes/V | 1.004 |

The full-rate class also read 1.1 % below the gated class. Every filtered capture read with the full-rate gain, so the
host's volts read 2.7-3.5 % low at 200 MSa/s and in CIC2. In the gated class, core A's midpoint sat 0.53 code below its
zero: a bow, which the repository reads as INL. A linear gain cannot remove it, so it stays.

## Calibration v3: per-core, per-class gains (fw 0.2.45)

fw-cal2 measured the gains from the calibration post. The FPGA's DDS can hold the post low (about -1.24 V against the
probe ground) or high (about +1.87 V). Both cores and both classes see the same node, so no voltage has to be known:
only relative gains are measured. The reference stays the mean gated gain of both cores, which is what the volts
calibration was fitted on.

The two levels sat at -35 and +53 codes on range 2, where core A's bow would give a 0.5 % gain error. So the step moves
the offset PWM to centre the levels about the zero code (0.07 % in the host test), then measures low, high and low
again so linear drift cancels.

The maintainer ran the new zero and gain steps at 08:15 on 2026-10-19 on fw 0.2.48. Both channels reported version 3, flags
0x0F and a lever of 88.0 and 88.6 codes. The CH1 full-rate gains came out at A 0.9993 and B 1.0126, a ratio of 1.013.
The DMM bench the evening before had given 1.033.

## The full-rate class does not hold still

Fifteen minutes later, at 08:30, a post-level check showed the full-rate A - B at +1.4 to +1.6 codes at the low level.
The raw split was 5.0 codes at both levels, so the gains now matched, while the stored zeros predicted 3.9. Within a
night the full-rate gain ratio had read 1.033, 1.013 and 1.000.

The overnight log on fw 0.2.44 had shown the gated class flat over 4.4 hours (CH1 cores within 127.84..128.24). At 03:48 CH2 core A stepped down 1.7 codes in both classes and stayed there; CH1
rose 0.47 code. The cause is not known.

A shorted-input log at 10:26 showed the v3 zeros exact at 0 V; the deviation at the post levels was even in the
signal, a bow of about one code. At 10:45 the FIR bench found a third effect: both full-rate cores had risen about
0.8 code since 08:15 as a common mode, while the gated class read 128.0. The conclusion in the plan: the gated class
keeps a one-time calibration; the full-rate class does not.

## Calibration v4: the background tracker (fw 0.2.50)

fw-cal3 designed two trackers ([plan](../plans/interleave-tracking.md), README 6.5).

The A - B tracker uses every 8-bit full-rate record of a calibrated channel, from the UI or a host, at most one per
50 ms. For each interior sample it compares the sample with the mean of its two neighbours from the other core. It uses
only flat parts: all three samples inside 4..251 and the neighbours within 6 codes of each other. Edges and anything
near fs/2 drop out, so a 40 MHz sine is never accepted. A record counts with at least 256 such pairs. The fit is

    A - B = d + e x + q x^2,   x = code - 128

with moments that forget 1/256 per record. That gives a time constant of 256 records, 12.8 s at the 50 ms gate. The
offset d is always updated. The slope e needs at least 8 codes RMS of signal spread, and the bow q needs three
separated levels; otherwise each holds its last value. Half of d and e is applied to each core with opposite signs,
folded into the per-core gain and offset of the readout, so there is no cost per sample. The mean of the cores, and
with it the filtered path and the volts, is unchanged. The bow is estimated and shown but not applied.

The class probe handles the common mode, which the A - B tracker cannot see. The only stable reference on the board is
the gated class. So at most every 30 s, with a quiet input and nothing else going on, the firmware takes 8 gated, 64
full-rate and 8 gated conversions (about 100 ms, one missed screen update) and compares the means. The difference is
low-passed over four probes and subtracted from the full-rate class, including the 16-bit path.

In the host test, a model of a drifting two-core ADC, a 1.7-code step of core A falls under 0.1 code after 736
records (36.8 s). The one-time gain step stays: the class gain and the gated gains come only from it.

The README lists a bench procedure for the tracker. The bench log records fw 0.2.50 loaded at 13:05 on 2026-10-19 with
the tracker flags still 0x00, and no tracker run after that. Its convergence on hardware is not yet measured.

## The four-screen wizard

The UX makeover turned the calibration into a modal wizard of four screens in fw 0.2.59: Zero level, CH1 gain, CH2
gain and Done. Each screen has a title, one instruction, a progress or result line and two buttons. Nothing is written
before Save calibration, and Cancel writes nothing, also in the middle of a measurement. There is no Skip. Clear
calibration keeps the volts factors, because restoring them needs an adjustable DC supply up to 15 V (UX review,
finding I1).

On the bench the Zero level step failed every time. The cause was in the FPGA, and it took four bitstreams to find and
fix: that hunt is the [capture-fault article](adc-capture-fault.md). The wizard first passed at 20:30 on 2026-10-19,
on bitstream 0x1539 and fw 0.2.69. The maintainer's words: "the new wizard is great". It passed with a workaround that
measured the zero 12 codes either side of 128, outside the band where the capture was still wrong. The maintainer removed the
workaround so that a bad bitstream fails loudly (fail fast). fw 0.2.79 measures at code 128 again, and the next
morning it failed on 0x1539 on both channels, as intended.

On bitstream 0x153A the zero step starts by training each core's capture stage over codes 120..140. The maintainer ran the
wizard there on 2026-10-20 between 10:42 and 10:48. It picked "CH1 A3 B4" and "CH2 A3 B4" and saved them with the
calibration. The self-check then read 18,424 captures with none flagged.

## What is open

- The tracker's bench run (README 6.5 list).
- Whether gains measured on range 2 hold on all six ranges: an inference from the mechanism, with a bench step listed.
- Applying the bow, if the tracked q shows a persistent bow of a code or more.
- A gated A - B tracker, not built: the 03:48 step was one event in nine hours of logging.
