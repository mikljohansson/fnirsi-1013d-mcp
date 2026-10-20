# The MMU and the FFT

The scope's processor is an Allwinner F1C100s: one ARM926EJ-S core at 600 MHz, 32 MB of DDR, and no floating-point
unit. On it the project runs a spectrum analyser with FFTs of up to 16,384 points. This article covers the
fixed-point FFT core, how it was made fast and tested, and the change that mattered most: turning on the MMU, which
switched on the data cache that 1.00o5 had left without effect.

Sources: [firmware/README.md](../../firmware/README.md) "FFT core", the [MMU and D-cache plan](../plans/mmu-dcache.md),
[firmware/AGENTS.md](../../firmware/AGENTS.md) sections 20 and 26, the [bench log](../hardware-status.md) and
[CHANGELOG.md](../../CHANGELOG.md).

## The FFT core

`fx_fft.c` is a real-input FFT in integers. The firmware has no libc and no floating point in its acquisition paths,
and the CPU has no FPU, so the design uses int32 data, Q30 twiddle factors and windows, and int64 products. It computes
an N/2-point complex radix-2 FFT of the even and odd samples packed as real and imaginary parts, then a split step that
yields the N-point real spectrum. N runs from 256 to 16,384.

The input is scaled so that N x max|x| <= 2^29. The README derives that this keeps every intermediate value below
2^31. So no stage needs its own scaling and no input can overflow, 8-bit or 16-bit.

Windows (rectangular, Hann, flat-top) and the twiddle table are computed when a plan is made, from a cosine table
generated at build time. Levels come out in centi-dB. A full-scale 8-bit sine reads 39.07 dB on every window; the
spectrum view adds 20 log10 of the volts per code, so its levels are dBV. The dB conversion uses the count of leading
zeros and a 257-entry log2 table with interpolation.

## Tested on the host

The C code builds on the PC as well. `host_tests/test_fft.py` compares it with numpy in float64 for every N and window:
noise, tones on and off a bin with a second tone 60 dB down, full-scale DC, an impulse, ±1 LSB noise, 16-bit tones.
A second target, `make test-arm`, builds the same file with GCC 10 and the firmware's flags, runs it under
`qemu-arm -cpu arm926`, and byte-compares the output with the C build. The measured bounds:

| N | Worst bin error | Worst level error above -60 dBFS |
|---|---|---|
| 256 | -124 dBFS | 0.006 dB |
| 4096 | -113 dBFS | 0.008 dB |
| 16384 | -104.8 dBFS | 0.019 dB |

The plan's limits were a bin error under -100 dBFS and a level error under 0.05 dB.

## First timings: slower than expected

fw 0.2.27 added FFT_BENCH, a protocol command that times the FFT on the scope's own CH1 buffer. On 2026-10-18 it
reported, with a Hann window:

| Points | DDR | On-chip SRAM |
|---|---|---|
| 1024 | 5.3 ms | 4.0 ms |
| 2048 | 11.4 ms | 8.5 ms |
| 4096 | 24.3 ms | 18.1 ms |
| 16384 | 108.7 ms | |

That was about four times slower than the estimate. The same command reported the CP15 control register: 0x0005107C.
The I-cache was on and the C bit (data cache) was set, but the MMU was off. On the ARM926 the data cache and the
write buffer act only on accesses the MMU marks cacheable, so with the MMU off every data access went to DDR. The
measured cost was about 90 CPU cycles per access, the stack included.

## Working around it: assembly and SRAM

The first answer was to keep the data in registers. In fw 0.2.31 the hot loops became ARM assembly inside `fx_fft.c`:
the 8-bit load with window and bit reversal fused with the first stage, the stages themselves (two LDRD and two STRD
per butterfly), and the split step. In 0.2.37 the dB conversion followed: three memory accesses per bin instead of
about nine. Each assembly loop has a C twin that computes the same integers, which is what the host tests run and
what the qemu test compares against.

FFT_BENCH on 0.2.31: 4096 points in 10.9 ms (from 24.3), 2048 points 4.9 ms in DDR and 2.3 ms in SRAM. On-chip SRAM
was about twice as fast as DDR, so fw 0.2.33 ran the spectrum view's FFTs of up to 2048 points from SRAM A at
address 0x1000.

The spectrum view worked. On 2026-10-18 the maintainer said "wow, the spectrum analyser works". With the 1 kHz square from
the post at 1 MSa/s the fundamental read +2.71 dBV against +2.9 dBV in theory for a 3.1 Vpp square, and the levels
and harmonic ratios came within 0.4 dB of theory.

The real fix was already listed in fw-gen5's handoff as a later step: enable the MMU with a flat map, DDR cacheable,
the framebuffer uncached and the peripherals strongly ordered. 1.00o5's per-pixel display code paid the same 90 cycles on every store.

## The MMU design (fw-mmu, fw 0.2.48)

The page table is a flat 1:1 map: 4096 first-level section descriptors, every address mapped to itself, domain 0,
full access. No entry can fault. The attributes:

| Region | Attribute |
|---|---|
| SRAM A (first 1 MB) | write-back |
| DDR 0x80000000-0x81FFFFFF | write-back |
| the pages of the scanned framebuffer | write-through |
| the 1 MB holding the restart record | strongly ordered |
| everything else (peripherals, display engine, boot ROM) | strongly ordered, as with the MMU off |

The plan audited who else reads or writes DDR. The SD driver is PIO through a FIFO register, the USB driver uses FIFO
registers only, and the FPGA bus is GPIO. Only the display engine reads DDR on its own, and only one buffer: the one
it scans. So the framebuffer was the only coherence question.

**Why write-through for the framebuffer.** The ARM926 data cache allocates on read misses only; a store that misses
goes to the write buffer whether the page is write-back or write-through. 1.00o5 draws almost entirely with stores to
pixels it has not read. So write-back would gain nothing for drawing, and it would need cleaning: there is no page flip,
and a modal menu idling on its touch poll could leave dirty lines stale on screen. Write-through is coherent by
construction, still removes the per-store stall through the write buffer, and still caches the reads. The plan keeps
write-back plus a cache clean every 1 ms as a follow-up if the drain rate turned out to limit the pass time.

**Leaving cleanly.** The restart record must reach DDR before a reset, so its section is strongly ordered. Every path
that leaves the image, a crash restart or ENTER_FEL, cleans and invalidates the whole data cache and turns the MMU off
first, so the next image sees DDR as an MMU-off image would have left it. A cold start with the MMU already off only
invalidates, so it never writes stale lines over a freshly loaded image. Exceptions also record the fault status and
address registers.

The table builder is pure C and host-tested: every attribute, the framebuffer coverage across section boundaries, and
a table walk. `make gcc10-nommu` builds the same code with the MMU off for an A/B comparison.

## The bench (2026-10-19 morning)

The first image from merged main, loaded at 08:05, booted fine, but FFT_BENCH read 0x0005107C and the old timings. The
branch's own image read 0x0005107D. The cause was a build switch, not the cache: the enable hook was guarded by
`#if PC_PROTOCOL && PC_MMU`, and fw-gen7's purge, merged at the same time, had removed `-DPC_PROTOCOL`. The merged
image compiled the hook away. Commit 39b98c4 fixed the guard.

The rebuilt image loaded at 08:10 read 0x0005107D: MMU, data cache and instruction cache on. FFT_BENCH, buffers in DDR:

| Points | MMU off | MMU on |
|---|---|---|
| 1024 | 2.20 ms | 0.21 ms |
| 2048 | 4.95 ms | 0.68 ms |
| 4096 | 10.96 ms | 2.72 ms |
| 16384 | 51.9 ms | 33.7 ms |

At 16,384 points the working set exceeds the cache; the README gives 16 KB from the F1C100s datasheet, and the plan
marks that size as unverified on the unit. In SRAM, 1024 points went from 1.03 to 0.22 ms and 2048 from 2.30 to
0.66 ms. DDR and SRAM are now equally fast, so the SRAM placement no longer buys speed. The assembly loops did not
change; the gain is the cache.

The main-loop pass in the scope view fell to 14-18 ms, median 15 ms, from 30 ms. The FPGA readout at about 0.3 us per
byte did not change: it is bound by the GPIO bus.

The maintainer then checked the screen, every menu, the spectrum and phosphor views and touch on the MMU image: "seems to
work afaikt". No leftover pixels or stale text were reported. A 30-minute capture soak on the 0.2.49 MMU image
(11:04-11:34) passed 17,908 content-checked requests with no failures, drops or restarts.

## What it left

- The cache size is still taken from the datasheet.
- The spectrum view's SRAM plan could go, since DDR is now as fast; the README says the bench decides.
- The plan's write-back framebuffer stays a follow-up; the bench gave no reason to need it.
- The [Frequency response design](../plans/bode-design.md) (section 3.7) had estimated that the cache would barely
  matter for its sweeps, because the FPGA bus readout dominates there.
