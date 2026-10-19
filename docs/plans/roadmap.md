# Roadmap to 0.3.0

Epic-level view of what is left before the first public release, v0.3.0. The task queue (`docs/plans/task-queue.md`)
holds the individual items with their bench facts; this file groups them into epics, orders them and says what has to
be true before the tag. Dates are 2026. State as of 2026-10-19 evening: firmware 0.2.51 (main 9cc599c), protocol 1.15,
FPGA bitstreams up to 0x1536 built, the owner's unit runs 0x1534.

**Owner decision 2026-10-19: everything on this list ships before the announcement. One big splash, no follow-up
posts.** The announcement draft is `docs/plans/announcement.md`; the tag-day checks are
`docs/plans/release-readiness.md`. Nothing below is a "maybe later": an item either lands and is benched, or it is
removed from the list by the owner.

Status words: **done** (merged and benched), **built** (merged, host-tested, bench pending), **in flight** (an agent
is working on it), **next** (designed or specified, not started), **owner** (needs the owner at the bench).

## 1. In flight

| Epic | State | What is left | Where |
|---|---|---|---|
| UX makeover, steps 1-4 | step 1 done in code (fw 0.2.51: main menu rows, USB drive screen, Display row, measurement cards in the stock scheme, strips without Scope/Auto, AUTO SET = spectrum Auto); steps 2-4 next, one agent generation each | step 2 CTRL pages with fold-outs, strips and spectrum header gone, top bar chips rebuilt, warnings top-middle; step 3 Settings fold-out trim and full Settings screen; step 4 Diagnostics as a display mode (Status / Calibration / Log / Developer), first-run screen, ENTER_FEL from the menu. Open design questions: look of the new full-screen pages, short CTRL labels, main-menu row height once Bode ships | `docs/plans/ux-makeover.md`, `ux-review.md`, `docs/ui-guidelines.md`; mockups are the acceptance reference |
| Diagnostics data readers | in flight (fw-readers, fw 0.2.52, protocol 1.16) | USB link state, card image identity, calibration provenance, power-on count and minutes, persisted restart record, save state, CP15 word, SAVE_REPORT; they feed the step 4 Diagnostics pages | `docs/plans/diagnostics-review.md` section 4 |
| Touch fixes, batches 1-3 | batch 1 specified, starts after UI step 1 (now) | batch 1: channel pointers become drag handles ("handles shown on screen are draggable"), pick at first touch, no tap on a handle, clamp 6..396; batch 2 after UI step 2: trace grab by nearest object, dead band, tap zones that keep the V/div and time-base bands; batch 3: no blocking touch loop, needs an MCP soak | `docs/plans/touch-review.md` section c1 |
| USB idle robustness | built (fw 0.2.49: suspend watch, inferred resume that acts, soft reconnect after a retry storm or 15 minutes suspended; protocol 1.14); bench pending | the long idle (port closed 30 min or more, see `fnirsi1013d-soak --mode reopen --idle 1800`) must end with the device still on the bus; plan in `firmware/AGENTS.md` section 27. The earlier failure: closed 08:40-10:20, device gone from the bus | `docs/hardware-status.md` 2026-10-19 10:20 |

## 2. Built, awaiting bench

All of these are merged. The bench lists name the exact steps; most need the owner (probes, DMM, calibration post).

| Epic | Firmware / protocol | Bench still to do |
|---|---|---|
| Calibration v3 and v4 plus the wizard | v3 per-core, per-clock-class gain (0.2.45, GET_CAL 256 bytes); v4 background tracking of the full-rate A - B mismatch, class probe, the one-screen calibration wizard (0.2.50, protocol 1.15, GET_CAL 288 bytes) | `firmware/README.md` 6.3 (gain step: gains per class, the +-1.0045 V check against the DMM, fs/2 spur at least 15 dB down) and 6.4 / 6.5 (wizard flow, tracker convergence, diagnostics lines). Open questions behind it: gain per range (README 6.3 step 7, 80 bytes free to store per range), CH2 range-5 zero drift, CH2 +1.0..1.5 codes at range 6 after calibration, the overnight zero step at 03:48 |
| Digital phosphor | 0.2.46, protocol 1.13 | visual steps (`firmware/AGENTS.md` section 24, README "Phosphor view"); pass time in and out of the view; with the MMU the scope view pass is 15 ms median |
| Spectrum analyser with the compensation FIR | spectrum view 0.2.28 onward; decimation filter 0.2.30; FIR firmware support 0.2.44 (protocol 1.11); default Filter CIC2+FIR since 0.2.48 | flash FPGA 0x1535 (backup first, programmer needed), then GET_DECIM bytes 26-27 = 0xE2 0x01 at 10 MSa/s and the CIC2+FIR spectra against CIC2; the Auto redesign and the warnings need the owner's verdict |
| Generator menu | hit-area fix (0.2.44) | tap every row of the generator, trigger and acquisition menus; no removed action reachable |
| MMU and D-cache | 0.2.48; benched 2026-10-19 08:10 (CP15 0x0005107D; FFT 4096 points 10.96 to 2.72 ms; scope view pass 30 to 15 ms) | owner's screen coherence verdict is in ("seems to work"); touch, soak, SD save and restore, ENTER_FEL round trips (section 26 steps 4-9) |
| Bode analyser, host tool | steps H1-H3: `scope_bode_sweep`, `fnirsi1013d-bode` (mcp-gen4), simulator-tested | bench on the RC low-pass R 1.6 kOhm / C 10 nF (fc 9.95 kHz) with the DUT ground on the USB shell: floor, skew, coherence, wiring A/B; record in `docs/hardware-status.md` (procedure `mcp/AGENTS.md` "Bode bench") |
| Reproducible builds, dead-code purge | done (fw 0.2.47: identity as data from git, hidden options removed, `PC_PROTOCOL` always on) | none; the release check in `docs/releasing.md` section 1 |

## 3. Next epics

Each is a coherent block for one agent generation (firmware plus MCP where noted). Order below is the suggested
order; independent blocks can run in parallel in separate worktrees.

1. **Measurements v2** (after the UI batch lands, `docs/plans/ux-makeover.md` section 3a, `ux-review.md` section 6).
   Per-mode MEASURES lists with default cards (a new unit shows none today), the cheap on-device set (rise and
   fall, overshoot, top and base, CH1-CH2 delay, phase and gain, cursor-gated statistics, PI decay tau; spectrum H2,
   H3, THD, SFDR, noise floor, band power, OBW), AC RMS beside Vrms, the standard names (Period, +Width, -Width,
   +Duty, -Duty) and the "Freg" fix. Acceptance: each measurement agrees with the host analysis of the same
   capture within the stated tolerance.
2. **FPGA 0x1536 firmware support and the flash session** (`fpga/al3/README.md` section 6,
   `docs/plans/segmented-capture.md`). Commands 0x57-0x5D, reads 0x2B-0x32 through the 0x1F pointer, the segment
   table (valid, triggered, trigger offset, 40-bit 5 ns timestamp), averaging with positions P and shift S;
   capture modes "Segments" and "Average N", capture-info fields, READ_RAW layouts for a segment set and the
   averaged record; MCP `scope_capture_segments` and `scope_capture_averaged`. The bitstream is pinned
   (`backups/al3_seg_0x1536_4eaf6811.bin`, padded 66866c84) but not flashed; firmware support comes first because
   the bench check 5 (an N = 1 average equals the memory's record) needs both. **Owner**: one flash session with
   the programmer for 0x1535 and 0x1536 together (backup of the 25Q80 first, read-back verify), then the check
   list of the two README sections.
3. **Phosphor fast capture** (design study in `firmware/AGENTS.md` section 24). The view draws one record per pass
   (about 30 waveforms per second at the old pass time); a capture loop that reads several records per pass
   raises the rate. Measure `pass_ms` in the view first; with the MMU the pass is half as long, so the study
   starts from the new numbers. Acceptance: waveforms per second with the view on, on the bench.
4. **Bode, device steps D1-D5** (`docs/plans/bode-design.md`), only after the host version proves the numbers.
   Display mode with magnitude and phase panes, BODE_CTRL / READ_BODE; entry from the generator menu (the main
   menu has no eighth row). Host follow-ups from the bench: CH1 re-ranging when CH1 drops below 20 codes, the
   harmonic fast mode, the impedance mode (coil L, Q and SRF for the detector), an optional PNG plot.

## 4. Small items that ship with the above

Listed so none is forgotten; each is one commit to one afternoon.

- "Confirm to delete?" dialog: buttons 44 px high, 16 px apart, primary on the right (UI step 1 or 2).
- Trigger position varies by run at decimated rates (+11 us / -1 us at 1 MSa/s): deterministic test with
  GET_CAPTURE_INFO logging.
- Open-input and EXT bench tests 1-4 (`fpga/al3/README.md` "Changes" section 2): needs an external 3.3 V source
  referenced to the USB ground.
- Owner verdicts outstanding from 0.2.40 / 0.2.41: Roll / Average / Filter box layout and the pass-time label, Max
  hold, the overrange warning.
- CHANGELOG hygiene before the tag: the 0.2.x entries are out of order around 0.2.46-0.2.50, and the "Plan for
  the first tag" block must go (`docs/plans/release-readiness.md`).
- Morning bench items from the queue that are not covered by the epics above: CH2 range-5 zero drift, the overnight
  zero step, the persisted release candidate after a soak.

## 5. Release 0.3.0

The announcement point. Gate, in order (details and the tick list in `docs/plans/release-readiness.md`):

1. Every epic in sections 1-3 is merged and its bench list is ticked in `docs/hardware-status.md`, or the owner has
   explicitly removed the item.
2. The FPGA bitstreams the release depends on (0x1534, 0x1535, 0x1536) are flashed on the owner's unit and
   benched. Decide how users get them (open point in the readiness list: bitstream files are not in git and not
   attached to releases today).
3. Version bump to 0.3.0 in `firmware/scope/pc_cmds.h` and `mcp/pyproject.toml`, a curated `## [0.3.0]` section in
   `CHANGELOG.md` (the release notes), `release: v0.3.0` commit, CI green.
4. A release candidate persisted to the SD card and a 30-minute soak with the MCP server polling
   (`fnirsi1013d-soak`), plus the long idle check.
5. Demo material for the announcement: screenshots of the main views (`scope_screenshot`), a shared-control session
   with the MCP chip visible, a short clip.
6. Tag `v0.3.0`, wait for `release.yml`, then check the attached hashes against a FEL load (`docs/releasing.md`
   section 4) and note the result in `docs/hardware-status.md`.
7. Post the announcement once (EEVblog thread, the subreddits named in the draft, the repository README banner).

## 6. After the announcement (not promised)

Ideas that stay in the repository for later and are not part of the one-splash plan. The first three need FPGA
work. Detail of each lives in the older roadmap text in git history (commit 9cc599c and earlier) and in
`docs/plans/dpos350p-features.md`.

- Deeper memory: use the AL3's spare block RAM for about 24,576 samples per channel, wider pretrigger and total
  registers, memory-depth table per FPGA version.
- Advanced triggers (pulse width, runt, window, timeout, Nth edge), peak-detect acquisition, streaming data logger,
  pulse pattern generator with trigger output, hardware measurements, serial decode, mask testing, PRBS output.
- Trigger on both ADC cores, a 200 MHz DDS clock for a finer generator.
- CSV export of the on-screen trace, a dot display mode, triggered sweeps slower than 10 ms/div.
- Invite testers with EF2L45 and EP4CE6 units; those paths are inherited from Atlan4 and untested here.

## 7. Not pursued

Equivalent-time sampling beyond 200 MS/s (the front end is 30 MHz), logic-analyser channels (no spare pins reach a
connector), hardware FFT (the MCU does 4096 points in 2.7 ms).

## 8. FPGA-headroom features (numbering kept for the references in `bode-design.md` and `ux-makeover.md`)

Resources free after 0x1535: about 7,000 LUTs, 200 kbit block RAM, one PLL, one M18x18. All of this is after the
announcement, except item 1, which is the 0x1536 epic of section 3. D = metal-detector value, C = community value.

1. Segmented capture and hardware averaging [D high, C high]: built as 0x1536, firmware support is section 3 item 2.
2. Advanced triggers [D: Nth edge, pulse width on the TX train; C high]: a "Type" selector in the trigger menu.
3. Peak-detect acquisition [C high: glitch capture at slow time bases]: min and max per decimation window.
4. Streaming data logger [D high, C unique]: continuous 16-bit filtered samples up to about 1 MS/s into the 32 MB DDR.
5. Pulse pattern generator and trigger output [D high: drive the TX stage]: per-pulse widths and gaps, bursts,
   frequency sweeps for Bode.
6. Hardware measurements [C medium]: 200 MS/s frequency and period counter, pulse width, duty, min and max as
   always-on readouts feeding the measurement cards.
7. Software features on the new data [C medium]: 4-digit DVM readout at CIC rates, serial decode, mask testing on
   the phosphor map.
8. PRBS or noise output for impulse and noise-based frequency response.
