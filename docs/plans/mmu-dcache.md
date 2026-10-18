# MMU + D-cache (fw-mmu, fw 0.2.48, protocol stays 1.13)

Task queue item "MMU + D-cache". Status: implemented behind `PC_MMU` (default 1), host-tested, **not benched**.

## Why

Facts: the bench read CP15 control 0x0005107C (I-cache on, C bit set, MMU off; firmware/AGENTS.md section 20). On the
ARM926EJ-S the D-cache and the write buffer only act on accesses the MMU marks cacheable / bufferable; with the MMU
off every data access is non-cacheable, non-bufferable (research/notes/repo-pecostm32.md "Caches", repo-donwulff.md
"Infrastructure facts"). Measured cost: about 90 CPU cycles per DDR access, the stack included (section 20: FFT 4096
points 10.9 ms in DDR, 2048 points 4.9 ms DDR vs 2.3 ms SRAM). Every store stalls for the full DDR write as well.
The FFT, spectrum, phosphor (582 KB of maps), trace processing and the 1.00o5 per-pixel display code are
memory-bound, so the expected win is several times on those paths (inference; the bench measures it).

## Mapping (flat 1:1, 4096 first-level section descriptors, domain 0 client, AP = 11 everywhere)

| region | what | attribute (ARMv5 C B) |
|---|---|---|
| 0x00000000, 1 MB | SRAM A: low vectors 0x00-0x3F, phosphor tables 0x0400-0x0FFF, FFT work 0x1000-0x7008, FEL stack 0x7C00 | write-back (1 1); `PC_MMU_SRAM=0` maps it strongly ordered for the A/B |
| 0x80000000-0x81FFFFFF | DDR: image, .data, .bss, free DDR, bootloader's sector-710 copy 0x81BFFC00, stacks 0x81FB5000-0x82000000 | write-back (1 1) |
| pages of `maindisplaybuffer` (4 KB granules, in two coarse tables) | the framebuffer the display engine (DEBE layer 0) scans | write-through (1 0) |
| 0x81A00000, 1 MB | restart record (section 10 of firmware/AGENTS.md); nothing else lives there | strongly ordered (0 0) |
| everything else | peripherals 0x01C00000-0x01C3FFFF, display engine 0x01E00000-, boot ROM 0xFFFF0000, the eGON header page below 0x80000000, DDR aliases above 32 MB | strongly ordered (0 0): exactly what the MMU-off state gives today |

No region faults: no translation, domain or permission fault can come from this table (host test checks every
descriptor). The A bit (alignment check) stays clear, as today: on ARMv5 unaligned LDR rotates and that behaviour
does not depend on the MMU, so no alignment audit of the 1.00o5 code is needed.

**Who else reads or writes DDR.** Only the display engine. The SD driver is PIO through the FIFO register (the
"original code ... uses DMA" path is not built; sd_card_interface.c `sd_send_data`, status-polled per word), the USB
driver is FIFO register access only (no DMA, firmware/AGENTS.md section 4), the FPGA bus is GPIO. So the framebuffer
is the only coherence problem; the restart record and the FEL hand-off are the two lifetime problems.

**Framebuffer: write-through, decided by the cache's allocation policy.** The ARM926 D-cache allocates on read misses
only; a store that misses goes to the write buffer whether the page is write-back or write-through. 1.00o5 drawing is
almost all stores to pixels it has not read (fills, text, lines, traces), so write-back would buy nothing for those
and would need cleaning: the display engine scans DDR continuously, there is no page flip, and the modal menus draw
from their own loops, so a dirty line could stay on screen stale for as long as nothing evicts it (inference: a menu
idling on the touch poll touches few lines). Write-through is coherent by construction (the write buffer drains by
itself within microseconds), keeps the store stall gone (today every pixel store waits the full ~90 cycles), and
still caches the reads (display_copy_rect save/restore under menus, READ_SCREEN, blending). Cost estimate per 16-bit
pixel store: today ~90 cycles; with the write buffer it is bounded by the drain rate, about 20-40 cycles per single
write (inference; bench: `pass_ms`). The alternative (write-back plus a whole-D-cache clean every 1 ms tick, at most
512 lines of 32 bytes for the 16 KB D-cache the F1C100s datasheet gives; unverified on the unit) is noted as the follow-up if pass_ms shows the drain rate is the limit.
Granularity: the pages containing any byte of `maindisplaybuffer` (0x804DD8E0, 768,000 B in the 0.2.46 layout; the
code takes the address and size at run time) are write-through; the few KB of neighbouring .bss sharing those pages
are write-through too, which is harmless.

**SRAM A: write-back by default, measured by the A/B.** With the D-cache on DDR, SRAM's advantage (2x on the FFT)
mostly turns into "both hit the cache" for the 16 KB working sets (FFT N <= 2048: 2N words of plan + 2N words of
data). FFT_BENCH runs both memories and reports them separately, so the bench decides whether the spectrum view
should keep the SRAM plan; nothing changes in spectrum.c in this step.

## Page table

16 KB first level aligned to 16 KB plus two 1 KB coarse tables aligned to 1 KB, in `.bss` (`pc_mmu.c`). Built after
main's BSS clear (the hook runs right after 1.00o5's `arm32_dcache_enable()`, before interrupts are enabled), while the
MMU is still off, so the table is in DDR when the walk reads it (ARM926 walks bypass the D-cache; the table is never
written again). The builder `pc_mmu_build` is pure C and host-tested (`host_tests/test_mmu.c`): every section and page
attribute, the framebuffer coverage at several addresses (inside one section, across a section boundary, page
aligned and not), descriptor type bits, and a table walk that answers "which attribute does address X get".

## Enable sequence (`pc_mmu_enable`, IRQs still masked)

1. build the tables; drain the write buffer (c7, c10, 4)
2. invalidate I- and D-cache (c7, c7, 0): the cache may hold stale lines from before the FEL load or the restart
3. invalidate the TLBs (c8, c7, 0)
4. TTBR0 (c2) = table, DACR (c3) = 0x00000001 (domain 0 client, others no access; only domain 0 is used)
5. control (c1): set M (bit 0), C (2), I (12); clear A (1), S/R (8, 9), V (13, high vectors) and RR (14); write,
   read back (the readback is the barrier ARM926 needs; the flat map keeps the next fetch at the same address)

I-cache today: on (bit 12) with the MMU off, all fetches cacheable. With the MMU on, I-cacheability follows the C bit
of the descriptor: DDR and SRAM keep it, strongly ordered regions (boot ROM) do not fetch through it.

## Leaving: restart, exception, ENTER_FEL, power cycle

- **`_reset` entry (start.s)**, the target of a NULL call, of every exception (via `pc_exception_common`) and of the
  eGON branch: after the record stores (strongly ordered, already in DDR) IRQ/FIQ are masked; if the MMU is on, the
  whole D-cache is cleaned and invalidated (test-clean-invalidate loop c7, c14, 3), the write buffer drained, then M, C
  and I are cleared and I/D/TLB invalidated. A restart therefore sees memory exactly as an MMU-off image would have
  left it (.data is not reloaded on a restart, so its last values must reach DDR). With the MMU off at entry (cold
  boot, FEL `exe`) the D-cache is only invalidated: nothing ran with the MMU on, so there are no dirty lines, and
  cleaning lines of unknown content could write garbage over the freshly loaded image.
- **ENTER_FEL (`pc_fel_jump`)**: IRQs are already masked; the same clean + disable runs before the existing I/D off,
  high vectors, invalidate and jump to 0xFFFF0020. The boot ROM and sunxi-fel then see the MMU off and DDR coherent.
- **Watchdog / power cycle**: hardware reset clears CP15; dirty lines are lost, which only matters for data that
  had to survive (the restart record, which is strongly ordered for that reason).

## Fault diagnosis

The MMU cannot raise aborts with this table, but an exception now also records DFSR (c5, 0), IFSR (c5, 1) and FAR
(c6) at record words [10] [11] [12]; GET_LINK_STATS appends counter 58 = fault status (DFSR for a data abort, IFSR
for a prefetch abort, else 0) and 59 = fault address (FAR, data abort only), n = 60 (decode by n, protocol row 38;
no minor bump). The existing 49-56 keep their meaning; fault PC = counter 51 - 8 for a data abort. `map_addr.py
--restart` takes the first eight as before.

## Audit of uncached assumptions (step 3 of the brief)

- Display: one scanned buffer (`maindisplaybuffer`, DEBE_LAY0_FB_ADDR set once in `sys_init_display`); the other
  display buffers (displaybuffer1/2/tmp) are CPU-only copies. Write-through covers it.
- USB: FIFO and CSR registers only, strongly ordered (as now); the bounce buffer and rings are CPU-only.
- SD: PIO via SD0_FIFO, per-word status polling; settings / calibration / sector-710 buffers are CPU-only.
- FPGA: GPIO registers, strongly ordered; the bus macros do register accesses only, so pulse widths keep their
  device-access timing. The gap between two pulses shrinks where a DDR store sat in between (trace buffer writes).
  Risk (inference): none known for the FPGA's synchronous bus; the bench's readback compare covers it.
- Delay loops: tp_delay, fpga_delay, sd_card_delay are register-only asm loops (I-cache timing, unchanged);
  fpga_check_ready's nop loop is register-only; menu.c `delay(volatile)` is only in a debug watchdog path.
- `volatile` on DDR variables (timer ticks, USB ring indexes, MSC state): ISR / main sharing on one core, which the
  cache keeps coherent.
- Code written as data: only the vector copy at start (before the MMU is on; the enable invalidates the I-cache).

## Switches and identity

`PC_MMU=1` (default) / `PC_MMU=0` (`make gcc10-nommu`, build-gcc10-nommu/, version suffix `-nommu`, today's
behaviour plus the FSR/FAR record words). `PC_MMU_SRAM=0` maps SRAM A strongly ordered. `#VER` and GET_INFO carry
the version string; FFT_BENCH reports the CP15 control word (bit 0 = MMU on), so the host can confirm at run time.

## Bench plan

See firmware/AGENTS.md section 26 (sequence, expected numbers, failure symptoms).
