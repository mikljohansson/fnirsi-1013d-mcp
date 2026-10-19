# Engineering articles

Longer write-ups of the hard parts of this project, for people who want to know why the scope behaves as it does, or
who want to build on it. None is written yet. Each entry below is the planned abstract and names the sources the
article will be drawn from. The facts are already in the repository; the articles will tell them in order.

!!! note "Status"
    Planned. Articles can be written at any pace; they do not wait for the firmware polish. Sources are in
    `docs/plans/`, `firmware/AGENTS.md` and `fpga/al3/AGENTS.md` (the GitHub links below).

## The calibration story

Two ADC cores per channel, each with its own zero and gain, interleaved at 200 MS/s: a mismatch shows as a spur at half
the sample rate and as a trigger-dependent offset. This article follows the calibration from Atlan4's 1.00o5 (what it
measured, stored and applied) through per-core zeros, per-class gains and the four-screen wizard to the background
tracker that follows the full-rate drift, and what the bench showed at each step.
Sources: [interleave tracking plan](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/interleave-tracking.md),
[firmware README](../../firmware/README.md) section 6, [firmware/AGENTS.md](../../firmware/AGENTS.md), the
[bench log](../hardware-status.md).

## The ADC encode-phase and capture-window faults

On the owner's unit the first calibration wizard failed on clean host data. The cause was in the FPGA: the encode
clock phase of the AD9288 and the window in which the capture reads the ADC data were wrong on the A cores. This
article tells the hunt: the symptom, the bench measurements that isolated it, bitstreams 0x1538 (the encode check),
0x1539 (readout fix) and 0x153A (programmable capture phase and training), and how the firmware now checks every
capture. Sources: [bench log](../hardware-status.md), [fpga/al3/AGENTS.md](../../fpga/al3/AGENTS.md),
[task queue](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/task-queue.md).

## Decimation filters and the FIR

Taking every Nth sample at a slow time base aliases. The FPGA now filters before the decimator (boxcar, CIC2 or CIC3)
and a compensation FIR flattens the CIC droop. The article derives the filters, shows the bench alias figures (an
alias of -17 dBV with a boxcar against -76 dBV with CIC2 + FIR) and explains the 16-bit sample layout it forced on the
protocol. Sources: [firmware README](../../firmware/README.md) "Decimation filter", [fpga/al3/AGENTS.md](../../fpga/al3/AGENTS.md),
[glossary](../glossary.md).

## The MMU and the FFT

A 600 MHz ARM926 with no FPU runs the spectrum analyser. Turning on the MMU and the data cache took the 4096-point FFT
from 10.96 ms to 2.72 ms and halved the scope view pass. The article covers the page table, the cache coherence
problems with the display buffers, the fixed-point FFT core and how it is tested on the host. Sources:
[MMU and D-cache plan](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/mmu-dcache.md),
[firmware README](../../firmware/README.md) "FFT core", [firmware/AGENTS.md](../../firmware/AGENTS.md).

## Frequency response: design and bench

Measuring magnitude and phase of a filter with a square-wave generator and two 8-bit channels. The article explains
the frequency plan, the per-point steps (settle, range, capture, single-bin analysis), the flags that mark unreliable
points, the host tool and the on-device state machine, and how both were benched against an RC low-pass.
Sources: [Bode design](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/bode-design.md),
[Bode UI](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/bode-ui.md),
[firmware README](../../firmware/README.md) "Frequency response", [bench log](../hardware-status.md).

## The UX makeover

The 1.00o5 interface rebuilt as one consistent system: the main menu, the CTRL page, fold-outs, the Settings and
Diagnostics screens, dialogs and touch. The article explains the process (mockups first, adversarial review,
consistency over muscle memory) and the rules that came out of it. Sources:
[UI guidelines](../ui-guidelines.md),
[UX makeover plan](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/ux-makeover.md),
[UX review](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/ux-review.md),
[Diagnostics review](https://github.com/mikljohansson/fnirsi-1013d-mcp/blob/main/docs/plans/diagnostics-review.md).
