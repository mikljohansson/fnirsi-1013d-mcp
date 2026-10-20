# The ADC encode-phase and capture-window faults

On 2026-10-19 the new calibration wizard failed on the maintainer's unit, while the same measurement taken from the PC looked
clean. The cause was in the FPGA: three faults, each hidden behind the one before. This article tells the hunt in
order, over about 19 hours and four bitstreams, on the maintainer's unit, the only one tested.

Sources: the [bench log](../hardware-status.md) (2026-10-19 15:35 to 2026-10-20 10:49), [fpga/al3/README.md](../../fpga/al3/README.md)
"Changes" sections 7 to 10, [fpga/al3/AGENTS.md](../../fpga/al3/AGENTS.md) (the 0x1538, 0x1539 and 0x153A sections and
the 0x153A review) and [firmware/AGENTS.md](../../firmware/AGENTS.md) sections 36 to 38 and 54.

## How the capture works

Each channel has one AD9288 with two cores. At full rate the FPGA's write clock runs at 100 MHz; core B is encoded on
its rising edge and core A on the falling edge, 5 ns apart. The FPGA stores one 32-bit word per write clock (CH1 A
and B, CH2 A and B), and the MCU reads the words over an 8-bit bus. Three things must be right: the encode edges, the
instant the FPGA latches each core's data, and the moment the MCU reads the bus. Each of the three failed on this unit.

## The symptom

fw 0.2.59, with the four-screen wizard, was loaded at 15:35 on bitstream 0x1535. At 16:02 the maintainer reported that the
Zero level step failed with "too much noise" on both channels in most runs, with probes and USB unplugged. A host
READ_RAW at 200 MSa/s at the same time showed 0.5 to 1.1 codes RMS per core. The limit is 3 codes.

A first reading blamed a wire on the calibration post; it was withdrawn at 16:18. fw 0.2.60 printed the numbers. At 16:25 the lines read "CH1: range 4 at 200MSa/s: core B, 21.1
codes, 21.0 per capture" and "CH2: range 1 at 200MSa/s: core A, 24.4 codes, 24.3 per capture". One core per channel
was 21 to 24 codes RMS inside a single capture.

## Fault 1: the encode phase (0x1535, 0x1537)

The wizard switches the sample rate twice per range. fpga-gen6 had already found, in simulation, an A/B encode-phase
race in bitstreams 0x1532 to 0x1536 (fpga/al3/README.md section 7, finding 3): a rate write that lands in a 1.25 ns
window leaves one chip's encodes half a word out of phase until the next such accident, and at 200 MSa/s that core is
latched while its outputs change. The test at 16:30 confirmed it on 0x1535: after rate toggles, CH1 core B read 15 to
29 codes RMS in 15 of 30 and 12 of 30 trials, and 1.1 otherwise.

0x1537 was meant to fix this. It was flashed at 16:27-16:31. At 16:33 CH1 was clean in all 120 trials, but CH2 core A
was bad in 35, 36 and 11 of 40 trials at 32 to 34 codes RMS. At 16:45 the wizard failed CH2 with "the input is not at
0 V": chip 2's cores now arrived in the other order, so the stored per-core zeros landed on the wrong cores.

At 16:48 a run with one rate write and then 1221 captures in 60 s, with no other write, showed CH2 core A bad in about
40 % of the captures, at random. fpga-gen7 read the cause from TD's timing report: 0x1537's A encode flop had a slack
of -0.209 ns at TT 85 C. It loaded the right value, the value one clock late, or went metastable, anew at every arm.
Which chip shows it is an inference; the report does not name the chip.

## The fix and the check (0x1538)

0x1538 drives both encodes from the write clock with no state of their own, on paths with 3.75 ns: B equals the write
clock, A its complement, at every rate. It also adds a self-check, FPGA read 0x33. On every falling edge the FPGA
compares each encode pin with the value it predicted. Bit 0 is a sticky failure flag, bits 3:2 give the encode
convention, and a good capture reads 0xF6.

The firmware (fw 0.2.62) checks every 200 MSa/s capture before anything reads it, and discards and re-arms a bad one,
at most 16 times (details at the end).

0x1538 was flashed at 17:43-17:46. At 17:50 rate toggles gave 0 garbled captures in 120 trials, and a 60 s run gave 0
garbled in 1233. The FPGA flagged 112 of 5798 captures; fw 0.2.72 traced those to rate writes landing after a
capture's reset, and judges such captures by their data instead.

The encode phase was fixed. But at 17:55 the wizard still failed: "CH1: too much noise, range 1 at 200MSa/s: core B 7.2
codes", and "CH2: the input is not at 0 V".

## Fault 2: the MCU readout (0x1539)

fw-cal5 built CAL_PROBE (fw 0.2.67), which runs the wizard's own measurement of one range from the PC. At 18:35, with
probes and post disconnected, CH2 failed at the full-rate zero point of range 0: core A spread over 124..195 at 11
codes RMS, core B up to 255. The slope points 40 codes away were clean. A sweep of the offset at 200 MSa/s showed
outliers only while a core's mean was between 126 and 133. The wizard parks the zero at code 128, the worst code.

The reading at the time: bytes near 0x7F / 0x80, where many bits flip at once, came back as a mix of two successive
bytes. A zero measured 12 codes either side of 128 still failed at 116 and 140, the 0x77 / 0x78 and 0x8F / 0x90
boundaries.

fpga-gen8 found a matching fault in TD's report once the MCU clock was declared as a clock. The FPGA loaded the read
byte on the rising edge of the MCU strobe, and the MCU reads the port right after that edge, while the byte takes 9.4
to 10.9 ns from the clock pad to the data pads, different per bit. 0x1539 loads the byte on the falling edge, at the
start of the strobe. A new testbench step over all 256 codes with per-bit pad delays found it clean everywhere. It was
flashed at 20:03-20:06.

## Fault 3: the capture window (0x153A)

At 20:25 CAL_PROBE passed on 0x1539, but the offset sweep showed the fault had moved, not gone. Both A cores now
returned rail values and jumps over 40 codes whenever their samples lay in codes 128..145 (0x80..0x91); the B cores
were clean. The wizard passed at 20:30 because its two-point zero avoided that band. At 20:51 the same fault broke the
on-device frequency response: on an RC, CH2 core A read 44 rail samples and 642 jumps, 58.9 codes RMS against 2.2 on
core B.

fpga-gen8 and then fpga-gen9 traced it with the AD9288 datasheet. Each core's data is valid from 6 ns after its encode
to 2 ns after the next one: 6 ns of every 10 ns at 200 MSa/s, and the A and B windows overlap for less than 1 ns. Up to
0x1539 the FPGA latched all four buses straight from the pins, at an instant set by placement. Re-timing the placed
0x1538 and 0x1539 designs reproduced every garbled lane of the bench: 0x1539 sampled the A pins 0.8 to 1.4 ns inside
the A transition, and 0x1538 ran CH2 B past the end of its window. So the 0x1538 outliers that led to 0x1539 were the
capture window too; the readout fix closed a real timing hole, but not the one the bench had shown. That the code
pattern follows from the bits caught half-way is marked in the repository as an inference.

The design of 0x153A:

- Every ADC pin goes into an IOB DDR register, so the sampling instant is tied to the clock edge within about 0.1 ns,
  not to placement.
- A chain of registers gives the pins as they were 5 to 17.5 ns before an edge, in 2.5 ns stages.
- Core A is captured against the falling write clock edge it is encoded on, core B against the rising one. (gen8's
  first version used the rising edge only, which fpga-gen9's review showed would garble the A cores at 50 MSa/s.)
- Command 0x63 selects the stage per core. Read 0x36 returns four outlier counters: words whose sample differs from the
  core's previous word by more than a threshold (command 0x64). A wrong stage garbles loudly and is counted.
- TD 4.6.8 ignores Verilog initial values; every flop powers up 0. So the stage register is stored XOR its power-on
  value, 0x08E4 (CH1 A4 B4, CH2 A3 B4).

At the power-on stages every core had at least 1.33 ns on both sides of the datasheet worst-case window in three
builds, at TT 85 C. The review is explicit that a worst-corner margin without training is not guaranteed. A new
testbench step with an AD9288 transition model ran 2274 checks. 0x153A was merged at 00:30 on 2026-10-20.

## Training on the unit

On the morning of 2026-10-20 fw 0.2.79 first checked the fail-fast rule on 0x1539: the wizard's zero step at code 128
failed on both channels, loudly, as intended. 0x153A was flashed at 10:02-10:03.

At 100, 50 and 20 MSa/s every core was clean. At 200 MSa/s CH1 core A still garbled at means 127..135. The power-on
stage 4 was wrong for that core on this unit, which the design allows, and the counters saw it.

A debug build mapped the counters per core and stage at 10:08-10:14:

| Core | Bad stages | Clean stages |
|---|---|---|
| CH1 A | 4 | 2, 3, 5 |
| CH1 B | 2 | 3, 4, 5 |
| CH2 A | none | 2, 3, 4, 5 |
| CH2 B | 2 | 3, 4, 5 |

The B reference bit (for the AD9288's data-align mode, which pecostm32's schematic notes) changed nothing; the log
reads that as data align off on this unit, as fpga-gen9 had inferred. The map also showed that the threshold of 8 codes was below
CH2's analog noise of about 3 codes RMS near code 128 at range 0; CH2's counters saturated on noise alone. The firmware
now uses 32.

fw 0.2.80's training still kept stage 4 for CH1 A. It probed one code with one capture per stage, and the fault was
intermittent and lived at a few codes. fw 0.2.81 trains over a band: the input parked at six codes from 120 to 140, two
8000-word captures per stage and point, the counts summed, and the stage picked from the widest clean run.

At 10:42-10:48, with the probes unplugged, the maintainer ran the wizard. It picked "CH1 A3 B4" and "CH2 A3 B4", the
expected 0x08E3, and saved them with the calibration. The band sweep at 200 MSa/s was then clean on every core over
means 109..135, with CH1 A at a maximum standard deviation of 0.98 code, no rail samples and no jumps. 100, 50 and
20 MSa/s were clean too. 0x153A became the recommended bitstream.

## How every capture is checked now

- Every 200 MSa/s capture is checked before the screen, the measurements or the host can read it. On 0x1538 and later
  the firmware reads 0x33, one bus byte. On older bitstreams it takes the RMS of core B against the mean of its two
  core A neighbours over 250 words per channel; above 8 codes the capture is re-armed, and the 17th bad one is
  accepted and flagged.
- On 0x153A the firmware reads 0x36 after every accepted full-rate capture. A core whose count exceeds its chip
  partner's by more than 8 is flagged. A core flagged in 3 captures within a minute turns the Diagnostics verdict amber
  with "ADC capture flagged 3 times within a minute: run Calibrate."
- The wizard's zero step measures at code 128 and fails with "ADC readout fault near code 128: check the FPGA
  bitstream" when a core has no clean stage.

One limit is written down as an inference: the self-check compares the two cores of a chip, so a fault on both cores
of one chip at once would not be flagged after normal captures. The training sees it, since it counts each core alone
at a quiet input.
