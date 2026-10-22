# Roadmap to 0.3.0

Epic-level view of what is left before the first public release, v0.3.0. The task queue (`docs/plans/task-queue.md`)
holds the individual items with their bench facts; this file groups them into epics, orders them and says what has to
be true before the tag. Dates are 2026. State as of 2026-10-20 (sections 1-3 reconciled against the changelog and the bench log): firmware 0.2.80,
protocol 1.24, FPGA bitstreams up to 0x153A built, the maintainer's unit runs 0x153A (flashed 2026-10-20 10:03).

**Maintainer decision 2026-10-19: everything on this list ships before the announcement. One big splash, no follow-up
posts.** The announcement draft is `docs/plans/announcement.md`; the tag-day checks are
`docs/plans/release-readiness.md`. Nothing below is a "maybe later": an item either lands and is benched, or it is
removed from the list by the maintainer.

**Status 2026-10-20 evening** (added at the documentation review before the tag; the sections below stand as written that
morning): firmware 0.2.102, protocol 1.24; FPGA 0x153B built, flashed 2026-10-20 19:31, benched good at 20:40 and now the
recommended bitstream (`fpga/al3/dist/BITSTREAMS.md`); the maintainer's unit runs it. Release 0.3.0 is in preparation
(`docs/release-notes/0.3.0.md`, `release-checklist.md`, `announcement.md`). Done since the morning: the three section 1
epics (fw-cap2 as 0.2.81, fw-spzoom2 and fw-yield2 as 0.2.82); from section 3, the Frequency response engine batches
(0.2.84-0.2.99: range plan, fail-fast AUTO SET that ends in STOP, the below-floor note), the ACQ memory chips and the
Spectrum ladder follow-ups (0.2.85), the idle serial-port release and simulator reason 8 (mcp-gen7), the SD write window
(0.2.101), the documentation site's guide text, screenshots and six articles, and the 0.3.0 release notes. Note that
0x153B is not the features bitstream of section 3 item 1: it fixes the spliced time-out records of 0x1532..0x153A
(`fpga/al3/README.md` section 11); trigger types, peak detect and the counter stay on the features branch (section 6).
Still open: the hardware averaging decision, touch batch 3, the Measurements v2 remainder, the phosphor fast capture
loop, the Bode follow-ups (harmonic and impedance modes), "1.17 on the unit", and the release steps of section 5.

**Owner decisions 2026-10-20, 21:52 to 22:02** (after the status above): every firmware version supports one FPGA
bitstream, the project's latest (0x153B for 0.3.0), and shows a loud banner on any other; the capability branches, the
stock and Atlan paths and the per-bitstream feature tables go. ACQ Average becomes one behaviour (a true mean of N
records on the MCU path as on the FPGA path). The "SD card alone" install path is dropped: the FPGA flash is a required
install step, so the supported configuration is one firmware with one bitstream. For the work after the tag the owner
chose the epics advanced triggers, peak detect, the streaming logger, the pulse pattern generator, hardware
measurements and the software features on the new data (section 8, items 2 to 7); no deeper memory; testers with other
variants later. The design of the next bitstream is in `docs/plans/fpga-0x153c.md` (being written).

Status words: **done** (merged and benched), **built** (merged, host-tested, bench pending), **in flight** (an agent
is working on it), **next** (designed or specified, not started), **maintainer** (needs the maintainer at the bench).

## 1. In flight

Reconciled 2026-10-20 against `CHANGELOG.md` 0.2.51-0.2.80, `docs/hardware-status.md` and `docs/plans/task-queue.md`
("Roadmap to 0.3.0" there points to every item). Three agents are working; nothing else is unmerged.

| Epic | State | What is left | Where |
|---|---|---|---|
| Capture-stage training that probes the band (fw-cap2) | in flight; bench 2026-10-20 10:19 showed the wizard keeps stage 4 for CH1 A on 0x153A although the stage map shows it bad (a single code, a single capture can read 0) | probe several codes across 120..140 with two captures per point, sum the counts, pick the stage with the widest clean run, let a persistently flagged core ask for a calibration; then the wizard rerun and the 0x153A items of `fpga/al3/README.md` section 10 | `docs/hardware-status.md` 10-20 10:19, `firmware/AGENTS.md` section 54, task-queue "Capture-stage training must probe the band" |
| Spectrum ladder, pans keep the rate (fw-spzoom2) | in flight (maintainer 10-20 10:27) | the rate is derived from span and centre at zoom steps only; pans clamp to the current axis 0..fs/2; the ladder follow-ups (capture time shown, real capture time for the 2 s limit, re-derive on FFT length or memory change) may ride along | task-queue "Spectrum ladder: pans never change the rate", "Spectrum ladder follow-ups"; `firmware/AGENTS.md` section 52 |
| Remove the "Host command: menu closed" note (fw-yield2) | in flight (maintainer 10-20 10:31) | the modal screens still unwind silently; note text, timer and `pc_glue_yield_note` go, the yield counters stay | task-queue "Remove the 'Host command: menu closed' note"; section 53 |

## 2. Built, awaiting bench

Everything here is merged on main. Benched parts are named in the right column; the rest needs the maintainer (probes, DMM,
calibration post, programmer) or a free serial port. The unit runs fw 0.2.80 on FPGA 0x153A.

| Epic | Firmware / protocol | Bench still to do |
|---|---|---|
| FPGA 0x153A capture path, training and self-check | fw 0.2.79 / 0.2.80, protocol 1.24 (row 64); wizard zero back at code 128 (fail-fast check passed on 0x1539 10-20 10:02); 0x153A flashed 2026-10-20 10:03, verified | capture fix confirmed by the stage map (10:08-10:14: only the power-on stage 4 is wrong for CH1 A); the trained stages still leave CH1 A garbled at two codes, so the wizard rerun waits for fw-cap2; `fpga/al3/README.md` section 10 items, EXT offsets, reference-bit test (raw 0x63 writes from a host script); the dist entry for 0x153A is still unbenched |
| UX makeover steps 1-4 and the polish batches | fw 0.2.51 (menu, USB drive), 0.2.54-0.2.56 (touch handles, Settings, CTRL pages), 0.2.59 (Diagnostics, four-screen wizard, first-run), 0.2.63 and 0.2.68 (polish batches 1 and 2) | maintainer walk-through of 0.2.59 done 10-19 16:02 (menu, chips, handles, CTRL, spectrum, Settings, Diagnostics work; Saved pictures delete dialog 16:06; wizard passed 20:30 "the new wizard is great"); walk-through of the polish batches (`firmware/AGENTS.md` sections 29, 31-34, 39, 44) and the Undo-last-calibration / Check-on-post items that fw-ui4 did not do |
| Scope time/div and sample rate coupling, zoom ladders, slider track | fw 0.2.64 (viability from the record, snap, per-mode filter default, touch batch 2), 0.2.69 (ten samples per division), 0.2.73-0.2.74 (slider overview, tap to jump, box geometry), 0.2.76 (Scope ladder), 0.2.77 (Spectrum ladder, not loaded) | Scope tap and ladder benched 10-19 22:45 ("seems to work great"); overview walk-through (`firmware/AGENTS.md` section 49), `pass_ms` with the overview (18 ms measured 22:07), Spectrum ladder on the unit (section 52), the 0.2.68 snap-path fix |
| Frequency response on the device | fw 0.2.66 (first version), 0.2.69 (redesigned UI), 0.2.71 (Trace style, grid rule, slider, reason 8, header); protocol 1.20-1.21 | benched on 0x1539 10-19 20:43-21:28: mechanics right, every point flagged by the A-core capture fault; retest on 0x153A: AUTO SET, device vs host sweep on the RC and the divider (0.05 dB / 0.3 deg), reason 8; the small items (single sweep stays on the fold-out, generator chip, warning wording) are still open |
| Host commands leave the modal screens | fw 0.2.78, protocol 1.23 (row 63) | menu case and nested Settings case benched 10-20 10:29 and 10:31; left: wizard mid-measurement refusal (detail 3), keypad half-typed, file-save refusal (detail 4) |
| FPGA bus contract fixes | fw 0.2.72, protocol 1.22 (row 62): dummy strobe in every readout, EXT shift, arm order, encode check by data | B1 checked 10-19 22:07; B3, B4, B5, B7 and the trigger-position re-measure (`firmware/AGENTS.md` section 48) |
| Segmented capture and hardware averaging | fw 0.2.58, protocol 1.17; mcp `scope_capture_segments`, `scope_averaged_capture(hardware=true)`; 0x1536 itself is superseded (0x1537 flashed 10-19 16:31, now 0x153A) | `firmware/AGENTS.md` section 35 checks and the host "1.17 on the unit" run (10 kHz generator, N = 1 equals READ_RAW); the hardware-averaging default with the CIC2+FIR filter needs the maintainer's decision |
| Measurements v2 and the card scheme | fw 0.2.51 (chip scheme, standard names), 0.2.65 (per-mode MEASURES, THD, noise floor, RBW / Span / Center / Time) | walk-through (`firmware/AGENTS.md` section 41); remainder still to build: spectrum H2/H3, SFDR, band power, OBW, AC RMS, CH1-CH2 delay / phase / gain, PI decay tau |
| Calibration v3 and v4 plus the wizard | v3 (0.2.45), v4 tracker, class probe, wizard (0.2.50), four-screen wizard (0.2.59), CAL_PROBE (0.2.67), protocol 1.15-1.21 | v3 benched 10-19 08:15 (gain step) and 10:26 (zeros exact at 0 V); left: `firmware/README.md` 6.5 tracker convergence, the +-1.0045 V check against the DMM, gain per range (README 6.3 step 7), CH2 range-5 zero drift, CH2 +1.0..1.5 codes at range 6, the overnight zero step |
| Digital phosphor | 0.2.46, protocol 1.13 | maintainer 10-19 08:10 "seems to work"; visual steps (`firmware/AGENTS.md` section 24) and `pass_ms` in and out of the view |
| USB idle robustness | fw 0.2.49 (suspend watch, inferred resume, soft reconnect), protocol 1.14 | reopen soak 3 x 30 min 10-19 11:34-13:04 clean; the two-hour idle (stopped at about 65 min 14:20) |
| Generator menu | hit-area fix (0.2.44) | tap every row of the generator, trigger and acquisition menus; no removed action reachable |

Done and benched: MMU and D-cache (0.2.48, 10-19 08:10: FFT 4096 points 10.96 to 2.72 ms), FPGA 0x1535 flashed 10-19 10:33 and
its FIR alias bench (78-83 dB, DEFAULT = CIC2+FIR verified 11:00), the Bode host tool (accepted 10-19 15:25, RC and divider),
READ_SCREEN (0.2.70, T9 10-19 21:11), reproducible builds and the dead-code purge (0.2.47), MMU capture soak 17,908 requests
clean (10-19 11:34).

## 3. Next epics

Measurements v2, the 0x1536 firmware and the Bode device steps D1-D5 of the earlier list are merged (section 2). What is
next; independent blocks can run in parallel in separate worktrees.

1. **FPGA 0x153B** (`fpga/al3/AGENTS.md` "0x153A status"): trigger types (pulse width, Nth edge), peak detect and the hardware
   frequency / period counter from the features branch gen8-features-wip, on top of the 0x153A design; wanted from the
   review: pipelined divider compare, encodes into IOB ODDRs, simulation-only initial values zeroed (TD ignores them). Device
   UI only for trigger types, peak detect and the counter (section 6). After the fw-cap2 bench closes 0x153A.
2. **Remaining firmware batches** (task-queue "Roadmap to 0.3.0", "Firmware batches still open"): ACQ memory chips after
   Filter -> Off, Spectrum ladder follow-ups, Frequency response small items, hardware averaging decision, touch batch 3,
   Measurements v2 remainder.
3. **Phosphor fast capture** (`docs/plans/phosphor-fast-capture.md`, `firmware/AGENTS.md` section 24). Design note exists; the
   scope-view pass is 15 ms median with the MMU, 18 ms with the overview. Acceptance: waveforms per second with the view on.
4. **MCP and host**: release the serial port when idle, Bode follow-ups (CH1 re-ranging, harmonic fast mode, impedance mode),
   1.17 / 1.19 / 1.20 on the unit, simulator reason 8 and header rule.
5. **Docs and release**: the documentation site remainder, `docs/release-notes/0.3.0.md` placeholders, CHANGELOG hygiene, then
   `docs/plans/release-checklist.md` (the checklist still names 0x1538 as the recommended bitstream; 0x153A has superseded it).

## 4. Small items that ship with the above

Listed so none is forgotten; each is one commit to one afternoon.

- [x] "Confirm to delete?" dialog: buttons 44 px high, 16 px apart, primary on the right (fw 0.2.55; benched 2026-10-19 16:06).
- Trigger position varies by run at decimated rates (+11 us / -1 us at 1 MSa/s): deterministic test with
  GET_CAPTURE_INFO logging. FPGA side fixed in 0x1537, firmware items in fw 0.2.72; remains: the bench items B3, B4, B5, B7
  (`firmware/AGENTS.md` section 48) on 0x153A.
- Open-input and EXT bench tests 1-4 (`fpga/al3/README.md` "Changes" section 2): needs an external 3.3 V source
  referenced to the USB ground.
- Maintainer verdicts outstanding from 0.2.40 / 0.2.41: Roll / Average / Filter box layout and the pass-time label, Max
  hold, the overrange warning.
- CHANGELOG hygiene before the tag: the 0.2.x entries are out of order around 0.2.46-0.2.50, and the "Plan for
  the first tag" block must go (`docs/plans/release-readiness.md`).
- Morning bench items from the queue that are not covered by the epics above: CH2 range-5 zero drift, the overnight
  zero step, the persisted release candidate after a soak.

## 5. Release 0.3.0

The announcement point. Gate, in order (details and the tick list in `docs/plans/release-readiness.md`):

1. Every epic in sections 1-3 is merged and its bench list is ticked in `docs/hardware-status.md`, or the maintainer has
   explicitly removed the item.
2. The FPGA bitstreams the release depends on are flashed on the maintainer's unit and benched: 0x1534 and 0x1535 are
   (0x1535 since 2026-10-19), 0x1536 is not yet. Decide how users get them (open point in the readiness list: bitstream files are not in git and not
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

**Where each feature lives (maintainer decision, 2026-10-19):** advanced capabilities are MCP features first: the agent drives them
and presents the data (text, plots, HTML on demand); the screen gets a control only where one row in an existing menu makes
the feature usable at the bench without a laptop. Device UI: trigger types (pulse width, Nth edge: a Type row in the Trigger
dropdown), peak-detect acquisition (a box in ACQ), the hardware frequency / period counter (feeds the measurement cards), and
the phosphor speed-up from segments (no new control). MCP only: segmented capture, streaming logger, pulse pattern generator
and sweeps, PRBS / noise output, DVM, serial decode, mask testing, CSV export. Note: 0x1536+ uses all block RAM for segments
and averaging, so "deeper plain memory" competes with them for the same RAM and is a choice, not an addition.

Ideas that stay in the repository for later and are not part of the one-splash plan. Detail of each lives in the older roadmap text in git history (commit 9cc599c and earlier) and in
`docs/plans/dpos350p-features.md`.

- Deeper memory: use the AL3's spare block RAM for about 24,576 samples per channel, wider pretrigger and total
  registers, memory-depth table per FPGA version.
- PRBS or noise output.
- CSV export of the on-screen trace, a dot display mode, triggered sweeps slower than 10 ms/div.
- Invite testers with EF2L45 and EP4CE6 units; those paths are inherited from Atlan4 and untested here.

### A future FPGA 0x153E (owner, 2026-10-21)

0.3.0 ships on 0x153D; 0x153E is not scheduled. The owner wants FPGA headroom kept ("not program the FPGA too full, who knows
what we might want to do later"), so a 0x153E would combine one feature with the compaction below, measured block by block
like `docs/plans/fpga-0x153c.md` section 13 on the 0x153D source.

1. **Trigger on both ADC cores** (the only feature candidate; contract section 3.4): the edge found to 5 ns instead of 10 ns at
   200 MS/s, so repeated captures line up twice as tightly (sharper averaged and phosphor traces at the fastest time bases).
   About 200 LUT, on the half-cycle compare path, the design's main timing risk.
2. **Compaction, small leftovers** (not measured yet, probably tens of LUT each): one trigger level register instead of the
   8-bit 0x17 / 16-bit 0x55 pair, the sequencer's raw readout mode `seq_raw` (READ_SEQ S = 0xFF) if the host no longer needs
   it, and the registers section 13 kept only because the firmware polls them.
3. **Compaction, cycle-identical narrowings** (the design fills by registers, so these count most; each has a trade-off):
   the counter's min / max (about 64 reg / 40 LUT; the measurement cards use them), the logger's u32 word counter (needs a
   firmware change), the square generator's u32 step count (24 bits saves about 100 reg but raises the lowest square from
   23 mHz to about 6 Hz).

4. **Finer ADC encode / capture phase** (fpga-capwin, 2026-10-22, README section 18): the free PLL outputs (125 ps steps)
   to centre each core group's capture and to move the encodes away from the partner core's output switching, to get the
   remaining ~1 % of CH2 code errors at 200 MSa/s below 0x153C's level. Two global clocks, a phase-dependent hand-over.

Dropped by the owner on 2026-10-21 and not candidates: the pulse pattern generator with its trigger engine and marker (cut from
0x153D at 15:55, it did not fit; chirps stay host-stepped on the square), runt, window and the pulse-width range triggers.

## 7. Not pursued

Equivalent-time sampling beyond 200 MS/s (the front end is 30 MHz), logic-analyser channels (no spare pins reach a
connector), hardware FFT (the MCU does 4096 points in 2.7 ms).

## 8. FPGA-headroom features (numbering kept for the references in `bode-design.md` and `ux-makeover.md`)

Resources free after 0x1535: about 7,000 LUTs, 200 kbit block RAM, one PLL, one M18x18. All of this is after the
announcement, except item 1, which is the 0x1536 epic of section 3. D = metal-detector value, C = community value.

1. Segmented capture and hardware averaging [D high, C high]: built as 0x1536 (now carried by 0x1537..0x153A), firmware support
   merged in fw 0.2.58 (bench pending, section 2).
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
