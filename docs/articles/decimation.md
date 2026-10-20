# Decimation filters and the FIR

At slow time bases the stock design does not filter. It runs the ADC encodes slower and stores every sample, so a
signal above half the sample rate folds into the band and looks real. This article follows the two bitstreams that
changed that on the maintainer's unit: 0x1534 with boxcar and CIC filters, and 0x1535 with a compensation FIR after the CIC.
It ends with what the 16-bit samples required of the firmware and the protocol.

Sources: [fpga/al3/README.md](../../fpga/al3/README.md) "Changes" sections 3 to 5, [firmware/README.md](../../firmware/README.md)
"Decimation filter", [fpga/al3/AGENTS.md](../../fpga/al3/AGENTS.md) ("First-record saturation"), the
[bench log](../hardware-status.md) and the [glossary](../glossary.md).

## The starting point

In Atlan4's AL3 bitstream (0x1532) and in 0x1533 the 0x0D divider D gates the encode clocks. Each core then converts
at 100 MHz / (D + 1) and every sample is stored. That is plain sample skipping: no anti-alias filter, no gain in
resolution, and 0 dB of alias rejection. The 8-bit ADC also has about 2 LSB of noise, which stays in every sample.

## 0x1534: filter before decimating (fpga-gen3, 2026-10-18)

With the filter on, the ADCs always convert at the full rate and the FPGA stores one filtered 16-bit sample per N input
samples. The decimation follows the divider so that the firmware's rate table keeps working:

    N = 2 x (floor(D / 2) + 1),   output rate = 200 MSa/s / N

For the table's odd dividers that is the old rate 200 / (D + 1). Command 0x53 sets the order K: 0 legacy, 1 boxcar,
2 CIC2, 3 CIC3.

**The filter.** A CIC of order K decimating by N has gain N^K. The input arrives as A/B pairs at 100 MHz, so the design
splits the filter. A polyphase prefilter (1 + z^-1)^K runs once per pair over the four samples B(k-1), A(k-1), B(k),
A(k), and a CIC of order K decimates the pair stream by P = N / 2 (the noble identity). K = 1 is the plain sum of N
samples. The prefilter has a zero at 100 MHz. That is exactly where the A/B core mismatch sits, so the interleave spur
of the [calibration article](calibration.md) disappears for every K.

**Scaling.** The integrators and combs are 48 bits wide and modular. They are exact while 255 x N^K < 2^48. For K = 3
that is N <= 10,334 (20 kSa/s and faster); the firmware falls back to CIC2 below that. The stored sample is
min(65535, y >> S), with the shift (command 0x54)

    S = max(0, bitlen(255 x N^K) - 16)

so full scale uses the top bit of 16 and never saturates. In ADC codes the mean is out x 2^S / N^K.

**Latency.** Unused stages for K < 3 are plain registers, so the latency is 12 write clocks (120 ns) from pin to memory
for every K and N. The centre of a stored sample lies K (N - 1) x 2.5 ns earlier still. ADC triggers run on the stored
stream and need no correction. An EXT trigger does: the firmware shifts its trigger point by that lag.

**What the model predicts** (`tb/cic_model.py`, N = 100, 2 MSa/s; droop and alias at fo / 4):

| K | Noise bits gained | Droop | Alias rejection |
|---|---|---|---|
| 1 boxcar | 3.32 | -0.91 dB | -10.5 dB |
| 2 CIC2 | 3.61 | -1.82 dB | -20.9 dB |
| 3 CIC3 | 3.75 | -2.74 dB | -31.4 dB |

The noise-bits column is the gain for white noise. The README notes that the ADC's linearity and the front end will
limit it at some point, and that this limit has not been measured.

The build grew from 635 to 2304 LUTs. The testbench proved K = 0 cycle-identical to 0x1533 and recomputed every stored
sample of 18 decimated captures with a direct-form model. The maintainer's unit got 0x1534 at 21:46 on 2026-10-18.

## The first bench

fw 0.2.34 drove it. At 1 MSa/s, with the post's generator off:

- The standard deviation of a DC level fell from 0.86 codes (legacy) to 0.19 (boxcar), 0.11 (CIC2) and 0.12 (CIC3).
- With the 1 kHz square the fs/2 spur from the A/B mismatch fell from -27 dBV to -88 dBV.
- A 900 kHz square, whose fundamental folds to 100 kHz at 1 MSa/s, gave an alias of +2.4 dBV legacy, -17.5 boxcar,
  -30.4 CIC2 and -35.7 CIC3.

The CIC2 and CIC3 residuals were higher than the model. The same numbers came back through the 16-bit host path, so
they were not 8-bit quantisation. fpga-gen4 found the cause in the source. The DDS runs on a 50 MHz clock, so its edges
sit on a 20 ns grid. For 900 kHz the edge pattern repeats every 9 periods, which puts a real 100 kHz tone of about
-37.5 dBV into the stimulus. The model with that tone gave -31.1 (CIC2) and -39.0 dBV (CIC3). The filters performed as
designed. Since then every alias test, and the frequency response plan, uses 50 MHz / M for an integer M.

The bench also explained a step of 1.8 codes between legacy and filtered levels. It came from the encode gating, not
the filter, and led to the two clock classes of the calibration.

## Where a FIR can help

The maintainer wanted CIC plus a compensation FIR as the default. fpga-gen3's study (`tb/fir_design.py`) compared two
places for it:

- **A FIR at the output rate**, after the CIC. It flattens the droop below 0.02 dB with 15 taps. It cannot remove
  aliases, because they have already folded in the CIC decimation. It even raises the alias band by 1 to 7 dB.
- **The CIC decimating by N / 2, then a symmetric 15-tap FIR decimating by 2.** This flattens the passband to fo / 4
  within 0.04 to 0.08 dB and adds alias rejection: 17, 34 and 51 dB for K = 1, 2, 3 against 10.5, 20.9 and 31.4 dB for
  the CIC alone. One tap set per K covers every N from 16 to 4096 within 0.1 dB of ripple and 1 dB of rejection.

The second was built as 0x1535 (fpga-gen4). Each channel uses one of the FPGA's three 18x18 multipliers, time-shared
over the 8 multiply-accumulates of the symmetric pre-add. That needs N / 2 >= 8 write clocks per output, and the
pair-stream CIC needs N / 2 even. So the FIR engages for N >= 16 with N a multiple of 4: at 10 MSa/s and slower in the
firmware's table.

The taps are 18-bit and sum to 2^17, so the DC gain is exactly 1. The largest gain for any input, the sum of the
positive taps, is 1.17 to 1.21, and the step response overshoots by 6.6 to 6.9 %. The firmware therefore adds 1 to
the shift with the FIR, which keeps the CIC samples below 32,768 so the stored value cannot saturate. The latency grows
to 25 write clocks (250 ns), plus the FIR's group delay of 3.5 output samples. All 13 mutations of the FIR in the
testbench were caught.

## The FIR on the bench

0x1535 was flashed at 10:33-10:40 on 2026-10-19. At 11:05 the bench session ran the alias bench. CH1 sat on the post,
range 3, with the generator at 892.857 kHz (50 MHz / 56, a clean-edge square, fundamental +1.8 dBV). At 1 MSa/s (N 200)
the alias lands at 107.1 kHz. Hann FFTs of 3000 host samples, three captures:

| Filter | Alias, measured | Model (3.1 Vpp square) |
|---|---|---|
| boxcar | -17.0 dBV | -16.1 dBV |
| boxcar + FIR | -81.2 dBV | -71.5 dBV |
| CIC2 | -35.6 dBV | -34.7 dBV |
| CIC2 + FIR | -76.1 dBV | -83.5 dBV |
| CIC3 | -54.0 dBV | -53.3 dBV |
| CIC3 + FIR | -80.8 dBV | -82.8 dBV |

The noise floor was about -92 dBV. The CIC rows match the model within 1 dB; the FIR rows within 2 to 7 dB of a
measurement limited by the floor. With the FIR the alias sits 78 to 83 dB below the fundamental in every order, against
19, 37 and 56 dB without it. The noise on a shorted input at 1 MSa/s was 0.054 codes with CIC2 and 0.075 with
CIC2 + FIR.

At 11:00 the maintainer pressed DEFAULT and the Filter box read CIC2+FIR.

## Ringing on a square wave

The maintainer then saw "horns" on the 1 kHz calibration square with any +FIR setting, growing and snapping back at 3 to
4 Hz. fpga-gen6's edge study traced it. The FIR is a sharp linear-phase filter, so a step overshoots by up to about 7 %
before and after the edge: Gibbs ringing. What a capture shows is that response sampled where the edge falls on the
output grid. The 1 kHz DDS square is really 999.995973 Hz; each period its edge moves 4.027 ns along the 1 us grid of
1 MSa/s, so it sweeps the grid at 4.03 Hz. The CIC alone has a monotonic step response and shows no horn.

The study also showed the reverse in the spectrum: above fo / 4 the CIC alone lets more of the harmonics' images
through, and their beat makes the upper spectrum wag more than with the FIR. So since fw 0.2.64 each mode keeps its own
filter: the Scope defaults to CIC2 without the FIR, the Spectrum view to CIC2 + FIR.

## What 16-bit samples asked of the firmware

A filtered word holds one 16-bit sample per channel, {CH2, CH1}, instead of one A/B pair per channel. That changed
several things (firmware README "Decimation filter", protocol 1.9 row 47):

- **Depth.** The memory holds 8192 filtered samples per channel instead of 16,384 8-bit samples. The firmware allows at
  most 8190 with the filter on; the ACQ menu offers 3000 and 6000.
- **Geometry.** The pretrigger, total, time-out and trigger address count output samples.
- **Readout.** A channel is read in two passes from the same start, low bytes then high bytes: the same bus cost as the
  A and B passes of a legacy record.
- **Display.** The screen path never sees 16-bit data. The firmware converts each sample into the 8-bit code domain
  (out x 2^S / N^K plus the calibration's correction), so the trace, the measurements and the software trigger run
  unchanged. The 16-bit buffer stays for the spectrum's FFT and the host.
- **Trigger.** The FPGA compares the 16-bit sample with a 16-bit level (command 0x55). The firmware converts the UI's
  8-bit level as round(v x N^K / 2^S).
- **Protocol.** GET_DECIM and SET_DECIM, a READ_TRACE flag for 16-bit samples, READ_RAW layout 2 with 16-bit words,
  and GET_CAPTURE_INFO grew to 76 bytes to carry the mode, S and N of each capture. 0x1535 added the FIR bits.

## The first-record saturation

One more fault showed on 2026-10-20 on bitstream 0x153A. A filtered capture that followed a change of mode or rate
sometimes came back as a whole record of 0xFFFF on both channels; the next capture was clean.

fpga-gen10 showed from the Verilog that the decimator cannot produce it in steady state: with the registers read back,
every stored word is the exact windowed sum, at most 32,640. A record of 0xFFFF needs a different configuration in
effect while it was written. The mismatch was in the firmware. The FPGA held a raw divider of 4095 (N 4096), but once
the UI's time/div snapped without a rate write, the host arm computed N from the UI's rate table and wrote that
shift. CIC2 + FIR at N 4096 with the shift for N 400 gives 255 x 2^22 >> 9 = 2^21 for any input above code 32: every
word saturates. A new testbench step reproduced it exactly.

fw 0.2.88 takes the divider the FPGA holds and writes the shift (0x54) at every arm. At 12:26 the deterministic
reproduction gave 0 saturated records in 20. The repository notes that this path does not explain the Bode engine's
pre-scan failures by itself; fw 0.2.88 also made the engine rewrite the divider when it differs, and after it ten
device sweeps all ranged correctly.
