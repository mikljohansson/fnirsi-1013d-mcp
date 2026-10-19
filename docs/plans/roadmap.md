# Roadmap (owner-approved 2026-10-18)

Order of work after the current firmware/MCP queues. Each item names the directory, the acceptance test and
the bench steps the owner must be present for. Items are small enough for one agent task each.

## 0. In flight (gen-2 agents)

- firmware: back-pressure request corruption (hdr_crc under rx_stalls), SD_STAGE from the ISR, hide unsupported
  UI (generator SINE/TRGL, RTC/temperature when no DS3231, 1014D remnants; switches in Other settings), our
  version string on the diagnostics screen, MCP chip drawn by the top-bar routine, startup init trim (1.5 s).
- mcp: persist resume after a link drop, GET_LINK_STATS decoder alignment, Claude Code skill + Codex guide.
- ci-release: GitHub Actions CI, release workflow, CHANGELOG, releasing guide.

## 1. First public release (v0.3.0)

- All item-0 fixes benched; firmware persisted to the card; a 30-minute soak with the MCP server polling.
- README demo material: a photo/clip of the MCP chip and a shared-control session; FPGA flashing caution.
- Tag v0.3.0; verify the attached hashes against a FEL load.
- Announce: EEVblog thread first, then r/oscilloscopes, r/embedded or r/electronics, r/ClaudeAI.

## 2. External trigger (FPGA + firmware + MCP)

Status 2026-10-18 evening: DONE on the bench. FPGA 0x1533 flashed (fpga/al3, backups/al3_ext-trigger_0x1533_a9656703.bin), firmware 0.2.9+
(trigger menu EXT, SET_TRIGGER source 2, generator interlock, GET_STATUS EXT flags), MCP scope_set_trigger(source="EXT"). Loopback via the
generator: 10/10 triggered, -50 ns at 20 MS/s. Open: a test with an external 3.3 V source referenced to the USB ground (the probe ground
sits on VRF1, +1.24 V; see fpga/al3/README.md "Changes" 2), and the host-capture trigger-position variance at decimated rates.


- FPGA (fpga/al3, Anlogic TD headless): make the calibration pin an input when `gen_out_enable=0` (it is
  already hi-Z then), add a trigger source "EXT" (command 0x15 value 2), edge select, 2-stage synchroniser;
  keep version word 0x15xx with a new minor nibble so firmware can detect it; keep 0x1532 behaviour otherwise.
- Firmware: trigger source EXT in the UI trigger menu (hidden unless the FPGA advertises it) and in SET_TRIGGER.
- MCP: `scope_trigger(source="ext")`; tool descriptions.
- Bench: generator from a second source (the owner's detector MCU or the bench supply's TTL out) into the calib
  pin, captures aligned to it; flash via spiflash_d2xx.py with the stock backup at hand.

## 3. Deeper memory (FPGA + firmware + MCP)

- FPGA: use the AL3's 2 spare 32k blocks + remaining 9k blocks (target >= 12,288 x 32 words = 24,576 samples
  per channel); widen pretrigger/total/trigger_addr/read_offset registers to 16 bits (new high-byte commands or
  wider fields; document in FPGA commands.txt); timing closure stays >= 200 MHz with margin (pipeline the
  sample-rate divider while at it).
- Firmware: memory-depth table per FPGA version; buffers already 16k-word aligned, extend; UI memory menu.
- MCP: max_total_samples from GET_INFO (already dynamic).
- Bench: full-depth captures byte-identical across re-reads; T3/T5 geometry checks at the new depth.

## 4. On-scope averaging and FFT (firmware)

Defaults decision (owner, 2026-10-18 evening): the default acquisition mode becomes CIC + compensation FIR (FPGA 0x1535, after 0x1534
is benched); until then Off. Other defaults: memory depth 3000, spectrum 2048 points / Hann / 10 dB/div with Auto on entry, phosphor off,
generator off at boot, trigger CH1 rising auto, collaborative USB on, a discreet hint to run base calibration when v2 data is absent.
Owner's order (2026-10-18 evening): spectrum analyser first, then digital phosphor, then Bode; the 3D waterfall is dropped; on-device
features have priority, host (MCP) analysis tools on exported captures are welcome alongside. FPGA decimation filter (boxcar/CIC, 16-bit
samples, version 0x1534) in work by fpga-gen3 as the foundation for clean spectra and higher resolution at slow time bases.
Detailed design and step plan for the FFT/spectrum analyser, Bode plots and digital phosphor: `docs/plans/dpos350p-features.md`
(2026-10-18). Note from that study: kiss_fft is not vendored in firmware/scope (only in Atlan's extracted tree, float-based); a small
purpose-written Q15 FFT is proposed instead.

- ACQUIRE_AVG (cap exists, not built): N captures accumulated on the scope with edge alignment, returned as
  16-bit means; useful for the detector loop at 9 captures/s.
- FFT: fixed-point radix-2 (kiss_fft is already vendored in Atlan's tree: `kiss_fft*.c` from v0.025y1) on
  up to 4096 points, a spectrum view in the UI and READ_FFT over the protocol.
- Bench: compare against the host FFT; CPU time per frame (pass_ms).

## 5. Calibration in firmware (in work: fw-cal, worktree branch fw-cal, started 2026-10-18 evening)

Visible symptom (2026-10-18, spectrum view): a spur at the Nyquist bin (fs/2), ~18 dB above the noise floor on CH1, because the two
interleaved ADC cores differ in zero (A 130.9 vs B 125.6 codes on the owner's unit) and the sample-skipping decimation alternates cores.
Per-core zero correction in firmware removes it everywhere; the boxcar/CIC modes also cancel it (both cores summed).

- Base calibration stores measured per-core zero and the PWM slope; on-screen volts use them; export via
  GET_CAL so the host model and the screen agree. Owner re-runs base calibration once.
- Built (fw 0.2.43, protocol 1.10, branch fw-cal): zeros per core in two clock classes (the legacy divider gates the
  ADC clock and moves the zero; filtered captures run at full rate), dithered sub-code correction, GET_CAL export.
  Bench list: firmware/README.md section 6.2.

## 5b. Memory depth and readout (firmware + FPGA), owner request 2026-10-18

- Memory depth default stays 3000 (owner, after seeing the UI slow down at long memory); show the resulting pass time next to the
  selector so the update-rate trade-off is visible (pass 35-75 ms at 3000 samples today, ~9 passes/s at full depth).
- Make depth cheap: read only the on-screen window for the display pass and the full record for the host/FFT; faster FPGA readout if
  the bus allows (0.3 us per sample byte today). Shares work with the digital-phosphor fast loop.

## 6. Smaller community wishes (firmware)

- Screenshot over USB (READ_SCREEN; RGB565 framebuffer, ~768 kB, 0.4 s at 2 MB/s).
- CSV export of the on-screen trace to the SD card.
- Dot display mode; triggered sweeps slower than 10 ms/div; hold-off and roll mode in the 1.00 line.

## 7. Later / research

- Trigger on both ADC cores or sub-sample interpolation (FPGA).
- 200 MHz DDS clock for a finer generator (FPGA).
- Frequency-response analysis tool on the host using the square-wave generator's harmonics (limited).
- EF2L45 / EP4CE6 owners: untested paths; invite testers after the release.

## 8. FPGA-headroom features, prioritised (owner approved all, 2026-10-18 evening)

Order after the spectrum analyser (done), digital phosphor (next) and Bode (then). Rationale in brackets: D = metal-detector value,
C = community value, $ = cost. Resources free after 0x1535: ~7,000 LUTs, ~200 kbit block RAM, one PLL, one M18x18.

1. **Segmented capture + hardware averaging** (FPGA + firmware + MCP) [D high: one segment per TX pulse with the dead time skipped,
   coherent averaging of the decay at full rate; C high; $ medium]. Segments of 128-2048 samples, up to the RAM; trigger per segment;
   16-bit accumulators for N averaged records; readout of one segment set or the averaged record; MCP `scope_capture_segments`,
   `scope_capture_averaged`.
2. **Advanced triggers** (FPGA + firmware + MCP) [D: Nth edge / pulse-width on the TX train; C high; $ low]: pulse width, runt, window,
   timeout, Nth edge, time-based hold-off; trigger menu grows a "Type" selector; SET_TRIGGER gains type + parameters.
3. **Peak-detect acquisition** (FPGA + firmware) [C high: glitch capture at slow time bases; D medium; $ low]: min/max per decimation
   window in the two bytes of the 16-bit word; a Filter choice "Peak"; trace drawn as min/max band.
4. **Streaming data logger** (FPGA FIFO + firmware + MCP) [D high: whole sweeps; C unique; $ medium-high]: continuous 16-bit filtered
   samples at <= ~1 MS/s into the 32 MB DDR (30 s at 500 kS/s), roll display with history and zoom, host download/streaming.
5. **Pulse pattern generator + trigger output** (FPGA + firmware + MCP) [D high: drive the TX stage, sync external gear; $ low-medium]:
   per-pulse widths/gaps, bursts, frequency sweeps for Bode; trigger-out pulse mode on the shared pin.
6. **Hardware measurements** (FPGA + firmware) [C medium; $ low]: 200 MS/s frequency/period counter, pulse width, duty, min/max as
   always-on readouts.
   They feed the existing bottom-row measurement cards (Measures menu, twelve per channel); the cards also get a spectrum-mode
   set (peak frequency and level per channel, THD) in the UX makeover (docs/plans/ux-makeover.md section 3).
7. **Software features on the new data** [C medium; $ low each]: 4-digit DVM readout at CIC rates (after calibration v2), serial decode
   (UART/I2C/SPI) from analog captures, mask/pass-fail testing on the phosphor map.
8. **PRBS / noise output** for impulse and noise-based frequency response [later].

Not pursued: equivalent-time sampling beyond 200 MS/s (30 MHz front end), logic-analyser channels (no spare pins reach a connector),
hardware FFT (the MCU does 4096 points in 11 ms).
