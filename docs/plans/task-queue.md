# Task queue

Open items discovered on the bench or requested by the maintainer, not yet dispatched to an agent. The bench session dispatches
from here in batches (one agent generation gets a coherent set), and moves items to "Dispatched" with the agent name.
Dates are 2026 unless stated. Bench facts behind each item: `docs/hardware-status.md`.

## Firmware

- [ ] **ACQ Filter row: three modes** (owner decisions 2026-10-21 16:29-16:38): the UI's Filter choice becomes Off, Hi-Res
  (= boxcar) and Anti-alias (= CIC3 + FIR); "Average" stays the ACQ record mean only. Names mimic other scopes (owner 16:31):
  Tektronix Hi Res, Keysight High Resolution, Rigol High Res. Every mode stays available over MCP (SET_DECIM unchanged, no
  FPGA change: boxcar / CIC2 / CIC3 share one CIC chain, the FIR stays). When the host has set another mode (CIC2, CIC3
  without FIR, boxcar + FIR, ...) the box shows that mode's own name; a press steps through the three simplified modes only.
  Hi-Res stays in the Filter row, not as a third Sample choice: it excludes Anti-alias (same FPGA stage). No mockup round
  (owner 16:38: "it's a simple change, just have the agent do it when it comes time"): one firmware item (settings word
  migration for saved CIC2 / CIC3 values, the warnings that name the Filter, the user guide, the Peak greying unchanged),
  screenshots in the handoff. Not dispatched.
- **No 0x153E for 0.3.0** (owner decision 2026-10-21 16:15, after the walk-through of the 0x153E features): 0.3.0 ships on
  0x153D. Runt, window and the pulse-width range are dropped from the roadmap (owner: runt "rather special", window = check
  the dip or the spike separately, range "marginal"); both-core triggering stays the only later 0x153E candidate ("if we do
  something perhaps it'd just be this one"). Keep headroom in the FPGA ("not program the FPGA too full, who knows what we
  might want to do later"): about 19 % of the LEs stay free after 0x153D. E1-E3 and protocol 1.28 are not dispatched.
- **Pattern feature cut** (owner decision 2026-10-21 15:55, "Yes, let's do the cut. I guess we'll need to compromise here"):
  0x153D was projected at about 96 % of the LEs, register-bound, not closing timing even without the engine. The pattern
  generator (8 double-buffered steps, repeat / gap, the marker and its capture source 0x15 = 3, the pattern trigger engine
  0x76-0x7D / 0x42) is cut; the generator stays an accurate fractional two-step square (exact frequencies, the DDS stays
  removed), the host-stepped chirp (scope_sweep), the Bode analyser, the logger and roll mode stay. RTL: fpga-d3 strips D2
  to the square keeping the register subset the firmware's square path writes (0x6C entries 0..1, 0x6D, 0x50); firmware /
  MCP / docs: fw-nopat. Lost: multi-pulse transmit patterns, bursts, pattern-synchronised capture triggers.
- **Roll mode on the logger** (owner decision 2026-10-21 14:5x, from the three options: logger-based roll in the 0x153D
  firmware, restore 0x24 / 0x26 in the RTL, or drop roll mode): the long timebases (100 ms/div to 50 s/div) draw real
  decimated samples from the 0x153D stream, both channels, continuous, instead of averaging 10 or 100 live-pin peeks; no RTL
  change, no LUT cost; 0x153C + fw 0.2.104 stays a bisect-only pair with roll mode documented as invalid there (fw-roll).
- **ACQ Average as one behaviour** (owner decision 2026-10-20 21:59, "consistency matters"): the MCU path becomes a true
  mean of N records like the FPGA path (N by the 40 ms budget, about 2 ms per record read), Atlan's running (trace + previous)
  / 2 average goes; same picture whatever the filter and time base. After the 0.3.0 tag unless the owner says otherwise.
- **The card-only path is dropped** (owner 22:02: "if people want this firmware they also need to flash the FPGA"): the
  FPGA flash is a mandatory install step; docs-one carries it through the guides and the README.
- **UI selection for the epics** (owner between 22:07 and 22:12, "Sounds great!" to the coordinator's proposal): the device UI grows by two
  rows only: a Type row in the trigger menu (Edge, Pulse width, Nth edge, with their parameters) and a Sample row in ACQ
  (Normal, Peak); the hardware counter feeds the measurement cards with no new control. Runt, window and timeout triggers,
  the streaming logger, the pulse pattern generator and trigger output, DVM, serial decode, mask test and CSV are MCP-only.
  UI design for the two rows (mockups first, docs/ui-guidelines.md) starts after the FPGA contract (fpga-spec) is in.
- **One supported FPGA configuration** (owner decision 2026-10-20 21:52, reaffirmed 21:59: "every firmware version only
  supports the latest FPGA bitstream ... detect this and show a loud banner"; the banner is part of the work: "our firmware doesn't need to be compatible with
  anything besides our own FPGA bitstream"): the firmware targets the project's current bitstream (0x153B) only. Implied
  removals, to be scheduled (before or after the 0.3.0 tag: owner's call): the stock-0x1432 and Atlan-0x1532 paths, the
  0x1532..0x1537 encode-heal and core-order workarounds (enc_heal.c/h, 287 lines), the <= 0x153A legacy capture geometry of
  fw 0.2.97 (0x0E = W-2), the capability branches on the FPGA version word (62 sites in 11 files), the MCU average fallback
  once the accumulator fits every record, the EF2L45 / EP4CE6 variant code inherited from Atlan4 (14 files). User-facing
  consequence: the FPGA flash (soldering + programmer) is step 1 of the install, the "SD card alone" path and the
  per-bitstream feature table go. Diagnostics keeps one check: the version word must be the one this firmware was built
  for, with a plain message otherwise.
- [~] **The firmware image no longer fits the SD write window** (fw-sd dispatched 18:53) (bench 10-20 18:51: `--persist-only`
  of fw 0.2.100 (773,120 B = 1510 sectors at 750, end 2260) FAILED "does not fit below sector 2048"; the card's partition
  starts at 8192 but the firmware caps writes at PC_SD_LIMIT = 2048 (pc_cmds.c `pc_sd_write_end`, protocol row 40
  write_end = min(part_start, 2048), SD_INFO check code 3 "over 2048 sectors"); the 0.2.25 image was 397,312 B). Release
  blocker: a card made per the install guide (FAT32 from sector 2048) cannot hold the image at all. Fix: the window ends at the
  partition start (cap at a value with headroom, e.g. 8192 or 16384), the SD layout docs / make_sd_image.py /
  make_release_sd_package.py / docs/guide/install.md require the partition at >= 8192, the SD package covers the image,
  the bootloader's loadable size checked from the research notes, tests; then persist 0.2.10x and power-cycle.

- [x] **Frequency response: points below the floor say so and what to do** (fw 0.2.99, bench pending; fw-floor, firmware/AGENTS.md section 65)

- [x] **Frequency response track overview follows the Display variant** (fw 0.2.92, bench pending; fw-ov3, firmware/AGENTS.md section 49 addendum)

- [x] **AUTO SET chooses CH2 Hold when the span fits one range** (fw 0.2.93, bench pending; fw-eng5, firmware/AGENTS.md section 61)

- [x] **Auto-mode records on 0x1532..0x153A are spliced by the forced stop** (fw 0.2.97, branch fw-arm, bench pending: firmware/AGENTS.md section 64)
- [x] **Frequency response engine arms normal** (fw 0.2.97, fw-arm, section 64: not taken as written. The engine keeps FPGA auto with its CH1 trigger, 0x0E moved to the stop geometry's W, so a missing trigger gives a contiguous record instead of a forced, spliced one; a trigger more than total words before W completes; spliced / cut records are retaken.)

- [x] **0x153B+: the deadline force must write 0x0E = 0 too** (fw-small 53551d4: `fpga_force_stop` writes 0x0E = 0, then 0x1A = 0, for every `pc_hal_force` caller; test_fpga_bus test_force_deadline; firmware/AGENTS.md section 76, bench pending)

- [x] **Device sweep flags stricter than the host's** (fw 0.2.87, bench pending)

- [x] **RUN stays continuous; repeated sweeps reuse the previous range plan** (fw 0.2.87, bench pending)

- [x] **AUTO SET ends in STOP, never RUN** (fw 0.2.86, bench pending)

- [x] **AUTO SET must fail fast with a helpful message, never loop** (merged 10-20 11:40, bench pending)

- [x] **First record after a configuration change is garbage on 0x153A** (cause: a stale 0x54 shift from the UI table while the FPGA held the raw divider, fpga-gen10; fix fw 0.2.88 fw-decfix, bench pending)

- [x] **ACQ: sample rate vs time/div coupling** (fw 0.2.64 viability from the record length, snap notice, grey boxes unavailable; 0.2.69 ten samples per division; 0.2.76 zoom ladder; benched 10-19 22:45 on 0.2.76: owner 'zoom ladder seems to work great'; the 0.2.68 snap-path fix: bench to confirm)
- [x] **Release 0.3.0 readiness fixes** (rel-gen1 10-19 17:35; remaining: 0x1538 entry, screenshots, tested-on list, known issues in docs/release-notes/0.3.0.md)
- [x] **FIR step overshoot pulsing on the scope trace** (fw 0.2.64: Scope filter defaults to CIC2 without the FIR, Spectrum to CIC2+FIR, per-mode word 212; fpga-gen6 quantified it: overshoot 0.45-6.9 % vs edge phase, 4.03 Hz beat of the 1 kHz DDS at 1 MS/s, spectrum wag = image beating, see Dispatched; bench pending)
- **Hardware averaging with the default filter** (fw-seg, owner decision): with CIC2+FIR every slower time base falls back to the
  software average because the smallest memory depth (3000) does not fit the FPGA averaging memory; a 2000-sample depth while
  Average is on would make it work. Decide after the 0x1536 flash session.
  (check: CHANGELOG 0.2.58 / 0.2.64 and hardware-status 10-19 / 10-20: no decision on a 2000-sample depth while Average is on is recorded.)
- [x] **FPGA bus contract fixes on 0x1532-0x1536** (fw-bus, branch fw-bus, fw 0.2.72 provisional / protocol 1.22, firmware/AGENTS.md section 48; bench list there, not benched: dummy strobe in every readout, arm order on every bitstream, legacy EXT shift, EXT kept on channel toggles, core order from 0x33)
- [x] **CH2 at 0.1 V/div reads nothing when set at the start of a Bode sweep** (resolved 15:20: DC overload of the sensitive ranges by wiring B's -1.2 V reference offset, not firmware; Bode tool to flag it and recommend wiring A, mcp-gen5)
- [x] **UI polish batch 1** (fw 0.2.63 items a-n and q, o in 0.2.60, p redone in 0.2.68 as the two-state RUN/STOP button with the spanner Settings icon and the shorter sine; bench pending: hardware-status records the 0.2.59 walk-through only, images 0.2.63 / 0.2.68 loaded since)
- [x] **FLASH 0x1537** (done 10-19 16:31, verified febc9299; power cycle and bench checks next)
- [x] **Full-rate encode check on every arm** (0.2.62 benched on 0x1538 17:50: 0 garbled captures in 1353; refinement queued: a rate write sets the 0x33 sticky flag once and costs a re-arm, 112 of 5798; clear it on 0x0D or skip the first check after a rate write; and the type 22 write trace floods the 128-entry event ring, make it a debug level; done on branch fw-bus: the bitstream clears bit 0 only at 0x01 = 1, so a capture with a rate / filter write after its reset is judged by its data; type 22 off by default, GET_EVENT_LOG flags switch it, counters GET_LINK_STATS 64-67)
- [~] **Documentation site and write-ups** (READ_SCREEN landed in fw 0.2.70, T9 passed 10-19 21:11) (owner 10-19 17:30): docs/ stays the single source, published with MkDocs Material via a
  GitHub Pages workflow; three shelves: user guide (install, first run, calibration, each screen with a screenshot, generator),
  agent guide (MCP setup, measurement guide, worked examples with plots), engineering articles (calibration story, the ADC
  encode-phase fault, decimation filters and FIR, MMU and FFT, Bode design and bench, UX makeover). Tooling: tools/screenshots.py
  drives the scope through each state over USB (scope_screenshot) and saves named PNGs for reproducible screenshots; plots from
  MCP data saved with their generating command. Order: site skeleton + screenshot tool + user guide before the announcement
  (after the polish batch lands so the screens are final); articles at any pace. Dispatch a docs agent after fw-polish merges.
  Done (branch docs-site, docs agent): [x] site skeleton (mkdocs.yml, User guide / Agent guide / Engineering nav, docs/guide/*,
  docs/articles/index.md, docs/agent-setup.md), [x] .github/workflows/docs.yml (Pages deploy, no secret) and the `docs` CI job
  (`mkdocs build --strict`), [x] tools/screenshots.py (+ `--simulator`, mcp/tests/test_screenshots.py). Owner, once: Settings >
  Pages > Source: GitHub Actions (docs/releasing.md section 6). Remains: (1) screenshots on the unit once fw-screen (READ_SCREEN
  handler) lands: `python tools/screenshots.py --port COM10 --generator` for the automatic states, then the manual states one
  by one with the screen opened by hand (`--only spectrum`, diagnostics, settings, usb-drive, first-start, calibration-zero,
  generator, saved-pictures, saved-waveforms); check that READ_SCREEN works while a menu is open; (2) the user guide text per
  screen after fw 0.2.70 (every page carries a "to be written after fw 0.2.70" note); (3) the six articles in
  docs/articles/index.md; (4) agent guide worked examples with plots and the command that made each; (5) README screenshot block
  can point at docs/screenshots/*.png.
  Remains: screenshots taken on the unit, the user guide text per screen, the six articles, agent worked examples with plots, the README screenshot block.
- [x] **FPGA ADC data capture window (was: MCU-readout bit mixing)** (0x153A merged 10-20 00:30, flash pending)
- [x] **Frequency response mode UI** (0.2.69, bench pending; follow-ups above)
- [x] **Time/div viability: minimum samples per division** (0.2.69: 10 per division, 200 MS/s down to 50 ns/div)
- [x] **Bottom scroll bar in Spectrum and Frequency response** (Frequency response slider row fw 0.2.69, Spectrum slider fw 0.2.71; bench pending)
- [x] **Frequency response chart follow-ups** (fw 0.2.71: (a) Trace style Lines/Dots, (b) grid rule in both modes with ui-guidelines.md section 3, (c) header and (e) estimate from the settings in effect, (d) readout-fault stop reason 8; benched 10-19 21:28: header and estimate agree, reason 8 not triggered because the pre-scan stayed below 5 V/div; full check needs 0x153A)
- [x] **Frequency response small items** (merged 10-20 11:40, bench pending)
- [x] **Slider track geometry revision** (fw 0.2.74, loaded 10-19 22:19; the owner's verdict on the box geometry is not recorded)
- [x] **Scope tap-to-jump and zoom stepping** (fw 0.2.76, bench pending)
- **AUTO SET in Frequency response fails about 60 % on 0x1539** (owner 22:20): expected from the capture fault (pre-scan clips at
  5 V/div); re-test on 0x153A before any engine change.
  (check: hardware-status 10-20 holds no Frequency response AUTO SET retest on 0x153A.)
- [x] **Spectrum: remove the colour legends** (fw 0.2.75)
- [x] **ACQ menu: memory depths above 6000 do not come back after Filter -> Off** (fw 0.2.84 provisional, branch fw-acq2 e125cf2, bench pending: firmware/AGENTS.md section 40 end)
- [x] **Spectrum zoom ladder** (fw 0.2.77, bench pending)
- [x] **Host commands leave the modal menus** (fw 0.2.78 / protocol 1.23; benched 10-20 10:29 menu case and 10:31 nested Settings case: answered at once, ui_yields counted; the note itself goes in fw-yield2; other section 53 checks pending)
- [x] **Spectrum ladder follow-ups** (fw 0.2.84 provisional, branch fw-acq2 5bfe097 + c449a5c, bench pending: section 52 addendum fw-acq2; (a) a Capture card, (b) the limit uses memory depth / fs, (c) a depth change re-derives, N is not in the rate)
- [x] **Capture-stage training must probe the band** (fw 0.2.81; benched 10-20 10:48: wizard stores 0x08E3, the 200 MSa/s band sweep clean on every core)
- [x] **Spectrum ladder: pans never change the rate** (fw 0.2.82, owner walk-through pending)
- [x] **Remove the "Host command: menu closed" note** (fw 0.2.82)
- [x] **READ_SCREEN handler** (fw 0.2.70, benched: T9 passed 10-19 21:11, 800x480 in 0.22 s)
- [x] **FPGA review on a Fable agent** (fpga-gen9 on Fable: 0x153A merged 10-20 00:30, flashed 10-20 10:03; capture fix confirmed by the stage map 10:08-10:14, the CH1 A training pick is fw-cap2's)
- [x] **Preserve the mockup sources** (docs/mockups/ committed 19:50)
- [x] **Wizard zero point back at code 128** (fw 0.2.79, bench pending)
- [~] **Touch fixes batch 1** (batch 1 fw 0.2.54, benched 10-19 16:02 handles work; batch 2 fw 0.2.64 trace grab, dead band, draggable slider, tap bands, bench pending) (after UI step 1 lands; spec = docs/plans/touch-review.md section c1, merged c179c49: pick_left_handle()
  helper with a host test, shared scope_pointer_top(), pick at first touch, no tap on a handle, no trigger-point move during a
  channel drag, triggerchannel == 1 fix, clamp 6..396 in auto-setup and settings load; batch 2 = trace grab by nearest object,
  dead band, record-position slider under the grid draggable (owner 10-19 14:05: dragging the window right scrolls the trace left, the window follows the finger; today the slider has no touch handler, only the trace drag moves the trigger point), tap zones that keep the V/div and time-base tap bands but avoid the slider and cards (owner 10-19: keep the tap bands), after UI step 2; batch 3 = no blocking touch loop, needs an MCP soak). Owner decision
  2026-10-19 13:20 **(owner)**: the channel pointers at the left edge of the grid become drag handles, "handles shown on screen are
  draggable"; the on-trace grab stays as a secondary path with a nearest-enabled-object rule. Owner reproduced: with CH2 dragged to
  the bottom CH1 can be picked up again, but only when the drag starts exactly on the invisible zero line.
  Remains: batch 3 (no blocking touch loop, needs an MCP soak) (check: CHANGELOG 0.2.54-0.2.80 lists no such change).
- [x] **Spectrum Auto redesign** (owner, 10-18)
- [x] **Header: drop "Avg n"/"Max"** (owner, 10-18)
- [x] **Warnings state consequence + action, units MSa/s** (owner, 10-18)
- [x] **Spectrum view: no scope-view repaints over the strip** (owner, 10-18)
- [x] **FPGA 0x1535 firmware support** (UNBLOCKED 10-18 evening: bitstream 5040a2ae built, tb passes, latency 25 write clocks verified; output rate stays 200 MHz / N)
- [x] **Default filter = CIC2 now** (owner leaning yes after the 0x1534 bench; CIC + FIR once 0x1535 is benched)
- [x] **USB suspend/resume handling** (closed 10-19: fw-gen6's rework in 0.2.44 + 29/29 clean reopens after 10 min idle overnight, counters 36/37 = 1/0 all night: Windows never suspends the idle device; the old suspend->bus_reset was the reset lead-in)
- [x] **MMU + D-cache** fw 0.2.48 (fw-mmu; hook guard fixed 39b98c4 after the purge merge)
- [x] **Dead-code purge** (done by fw-gen7 in 0.2.47/0.2.48)
- [x] **Calibration v3: per-core, per-class gain** (fw-cal2, fw 0.2.45 merged c20957a; bench pending)
- [x] **Bench: -1 V point on CH1 range 2** (done 10-18 evening, docs/hardware-status.md)
- [x] **Generator menu: tapping the frequency row toggles the waveform** (fw-gen6 e0ee6f3 in 0.2.44: the removed Wave/Analog features read as present on 0x1533+; bench pending)
- [ ] **CH2 range-5 zero drift check** (fw-cal hypothesis): CH2 raw at ranges 4/5 vs the stored zeros now; re-calibrate after 10 minutes
  warm-up and re-measure range 6 immediately and 10 minutes later. If it drifts, document a warm-up recommendation (Atlan: 5 min).
- [ ] **Calibration v2 bench** (when fw-cal delivers): shorted inputs, per-core means equal within 0.3 codes after
  correction at full-rate and gated rates, T8-style PWM sweep, Nyquist spur gone in legacy mode, volts at 1.000 V /
  0.010 V match the host model within 1 %; owner reruns base calibration.
  (check: CHANGELOG 0.2.45 says calibration v2 is superseded by v3; hardware-status 10-19 10:26 shows v3 zeros exact at 0 V, but the PWM sweep and the 1.000 V / 0.010 V volts points are not recorded.)
- [x] **Reproducible firmware builds** (fw-gen7, fw 0.2.47: identity as data from the commit; .bin and .elf identical across directories)
- [ ] **CH2 reads +1.0..1.5 codes at range 6 with shorted input right after calibration** (10-18 late): channel offset with the cores
  agreeing; see docs/hardware-status.md late evening and the overnight log. Decide after the log: PWM/relay drift at hw range 5 vs probe.
- [~] **Morning bench with the owner** (done: SD backup and base calibration with the gain step 08:15; 0x1535 flashed 10:40 and benched 11:05; DEFAULT = CIC2+FIR verified 11:00; owner: MMU image screen, spectrum and phosphor 'seems to work') (10-19 morning: calibration v3 run 08:15, 0x1535 flashed 10:40 and benched; the +-1 V and phosphor checks still open; load the latest pinned image, not 0.2.46): (1) back up SD 700-710; base calibration
  with probes + USB unplugged, then the new gain step with both x1 probes on the post ("Gain calibration successful"); GET_CAL version 3,
  gains about full A 0.995 / B 1.028, gated 0.998 / 1.002; (2) +-1.0045 V on CH1 range 2: |A-B| <= 0.15 at 200 MSa/s, codes/V equal
  within 0.2 % across 200 MSa/s / 1 MSa/s / 10 kSa/s / CIC2, volts within 0.5 % of the DMM; fs/2 spur down >= 15 dB; (3) phosphor view
  visual steps (firmware/AGENTS.md section 24 / README "Phosphor view"), pass_ms in and out of the view; (4) 0.2.44 items: spectrum
  strip repaint loops, generator menu rows, Filter box steps + DEFAULT = CIC2; (5) flash FPGA 0x1535 (backup first) and bench CIC2+FIR
  (GET_DECIM bytes 26-27 = 0xE2 0x01 at 10 MSa/s); (6) persist a release candidate to the SD card after a soak.
  Remains: the +-1.0045 V check, phosphor visual steps with pass_ms in and out of the view, spectrum strip repaint and generator rows, persisting a release candidate to the SD card after a soak.
- [x] **16-bit trigger level ignores corr16 and the class gain** (fw-gen7, fw 0.2.47, 04619ca; bench pending)
- [ ] **Phosphor fast capture loop** (design study, fw-phosphor section 24): the view draws one record per pass (about 30 wfm/s);
  a capture loop that reads several records per pass would raise the waveform rate. Measure pass_ms in the view first.
  Remains: the capture loop itself; the design note docs/plans/phosphor-fast-capture.md landed with fw 0.2.58 and the scope-view pass is 15 ms median with the MMU (hardware-status 10-19 08:10).
- [ ] **Gain-per-range check** (fw-cal2 assumption 1, README 6.3 step 7): gains measured on one range are applied to all six; bench
  CH1 at ranges 1/2/3 with the generator static on the post; if spans disagree beyond 0.3 %, store gains per range (80 bytes free).
- [x] **Bode analyser, host steps H1-H3** (docs/plans/bode-design.md fe5780b; done by mcp-gen4: 1d46cf8, f0940ee, 363f676, d3a16bb, simulator-tested, bench pending in "MCP / host")
- [x] **Bode analyser, device steps D1-D5** (fw-bode 0.2.66 merged 18:20; owner 18:45: Bode plot is a main-menu mode row after Spectrum analyser at a 52 px pitch, eight rows, removed from the generator pop-over -> fw-polish2 item 6; the earlier "entry from the generator menu" note is superseded)
- [x] **docs/protocol.md tidy** (docs-gen3, merged 6f1ea23 on 10-19)
- [x] **FPGA 0x1536 firmware + MCP support** (fw 0.2.58 / protocol 1.17 and mcp-gen5 4699593; bench pending, see the '1.17 on the unit' item; 0x1536 itself was superseded by 0x1537 flashed 10-19 16:31)
- [ ] **Overnight zero step at 03:48** (docs/hardware-status.md 04:00): after 4.4 h flat, CH2 core A moved -1.7 codes (B unchanged) and CH1
  both cores +0.5 within one 10-minute interval, persisting. Morning: battery/charger state, recalibrate on 0.2.48, re-check the CH2 A/B
  split; if per-core zeros move with temperature or supply, consider a periodic zero re-check (needs an input-independent method) or a
  warm-up note in the calibration procedure.
- [x] **Calibration v4: background interleave-mismatch tracking** (fw-cal3, fw 0.2.50 / protocol 1.14: A - B tracker, full-class class probe, GET_CAL 288 bytes, diagnostics lines; built and host-tested, bench list firmware/README.md 6.5; the one-time gain step stays, design section 6)
- [x] **Calibration wizard (owner approved 10-19)** (fw-cal3, fw 0.2.50: built and host-tested, bench list firmware/README.md 6.4)
- [~] **USB: device lost after a long idle on 0.2.48** (0.2.49 merged; reopen soak 3 x 30 min idle 10-19 11:34-13:04 clean, counters 36/37 stayed 1/0) (fw-usb2 10-19: 0.2.49 merged with the suspend watch, inferred resume that acts,
  soft reconnect after a retry storm or 15 min suspended, counters 60-61, event type 20, protocol 1.14; bench plan firmware/AGENTS.md
  section 27; Windows selective suspend on this laptop: AC disabled, DC enabled) (10-19 10:20): port closed from about 08:40 to 10:20, device vanished from the bus
  (no 1209, no re-enumeration), GET_LINK_STATS after replug: suspends (36) = 17, resumes (37) = 0, no restart. The host suspended the idle
  device and it never resumed: RESUME interrupt never fires. Decode the event log (types 2/3 fields of row 50), check MUSB resume
  handling (INTRUSBE resume bit, POWER.Resume, PHY), and the F1C100s USB PHY suspend behaviour; bench with `fnirsi1013d-soak --mode reopen
  --idle 1800`. Overnight 10-minute idles never suspended (36 stayed 1); the long idle did.
  Remains: the long idle (about 2 h with the port closed) ending with the device still on the bus; the two-hour run was stopped after about 65 min at 14:20.
- [x] **UI makeover batch** (all four steps merged 10-19 by 14:42 as fw 0.2.51/0.2.55/0.2.56/0.2.59, bench pending)
- [x] **Measurement cards in the stock scheme** (fw 0.2.51 chip scheme, standard names, Freq fix; card layout again in 0.2.65; bench pending)
- [~] **Measurements v2** (fw 0.2.65: per-mode MEASURES lists, order, defaults Freq / Vpp / Vavg, spectrum THD, noise floor, RBW, Span, Center, Time, words 221-223; bench pending) (ux-review section 6, plan section 3a): per-mode MEASURES lists in spare settings words with default cards
  (a new unit shows none today: scope_reset_config_data sets no cards despite its comment); the cheap on-device set (rise/fall, overshoot,
  top/base, CH1-CH2 delay/phase/gain, cursor-gated statistics, PI decay tau; spectrum H2/H3, THD, SFDR, noise floor, band power, OBW);
  AC RMS beside Vrms; the standard names (Period, +Width, -Width, +Duty, -Duty) and the "Freg" fix. After the UI batch.
  Remains: spectrum H2/H3, SFDR, band power, OBW; AC RMS; CH1-CH2 delay / phase / gain; PI decay tau (check: CHANGELOG 0.2.65 names none of these).
- [x] **README timing fix** (docs-gen3, merged 6f1ea23 on 10-19)
- [x] **"Confirm to delete?" dialog** (fw 0.2.55, 24743a7: 160 x 44 px buttons 16 px apart; benched 10-19 16:06 select, save and delete work)
- [~] **Trigger position varies by run at decimated rates** (the three firmware items are in fw 0.2.72, fw-bus; 0x1537 was flashed 10-19 16:31, the unit now runs 0x153A; B1 checked 10-19 22:07) (10-18, +11 us / -1 us at 1 MS/s). FPGA side done (fpga-gen6,
  tb/tb_trigpos.v, fpga/al3/README.md "Changes" section 7). The trigger detector, decimator and FIR are deterministic: an
  ADC trigger lands at the same record index in every run, and EXT within one output sample. Two FPGA faults of
  0x1532..0x1536 are fixed in 0x1537 (backups/al3_seg_0x1537_426a3784*.bin, not flashed): (a) a 0x0B (or 0x57/0x59)
  written in the last write clock period before 0x01 = 0 was ignored at slow legacy rates, shifting the record by
  (old - new pre) words; (b) the A/B core order within a word flipped on random 0x0D / 0x53 writes. Firmware items
  (inference, see section 7):
  - legacy EXT captures land +10..+11 samples late (AD9288 4-cycle pipeline, no EXT shift for K = 0; pc_hal_scope.c:806-814);
  - fpga_swap_trigger_channel (fpga_control.c:454-484, statemachine.c:1846) overwrites the EXT source when a channel is
    toggled, a likely cause of the bench's EXT -> CH1 switch;
  - fpga_read_decim_data and the 1.00o5 readout have no dummy strobe and read one word late.
  Remaining: bench re-measure on 0x1537 with GET_CAPTURE_INFO and 0x15 writes logged. The three firmware items are built on
  branch fw-bus (fw 0.2.72; firmware/AGENTS.md section 48 bench items B2, B4, B5); re-measure on 0x1539 with that image.
  Remains: firmware/AGENTS.md section 48 bench items B3, B4, B5, B7 and the trigger-position re-measure with GET_CAPTURE_INFO on 0x153A.
- [x] **Digital phosphor** (fw-phosphor, fw 0.2.46 merged 2e73d28; visual bench pending)
- [x] **Bode / frequency response** (owner priority 3)

## MCP / host

- [x] **fnirsi1013d-bode crashes on a flat CH1 record** (merged 10-20 11:40, bench pending)

- [x] **Server releases the serial port when idle** (merged 10-20 11:40, bench pending)

- [x] **scope_status docstring** (docs-gen1)
- [x] **Bode bench session** (10-19 14:35-15:25: RC low-pass and high-pass consistent to 0.14 dB / 1 deg, floor -80 dB, thru +0.3 dB, divider flat: tool accepted; capacitor dispersion is the part; DC overload in wiring B found, wiring A recommended)
- [ ] **Bode follow-ups** (after the bench): CH1 re-ranging when CH1 drops below 20 codes (resonant input nodes), the harmonic fast
  mode (design 1.3), the impedance mode z_series_ohm (design 2.4, coil L / Q / SRF for the detector); the PNG plot is done (scope_report, mcp-gen6).
- [~] **1.19 / 1.20 on the unit, host side** (scope_health on 0x1539 10-19 21:12 and scope_cal_probe 20:25 done) (mcp-gen6 hand-off, mcp/AGENTS.md open item 0a): scope_health encode_check
  on 0x1538; scope_bode_sweep(where='device') on the divider and the RC next to where='host' (within 0.05 dB / 0.3 deg).
  Remains: device vs host Frequency response sweep on the RC and the divider, blocked by the A-core fault until 0x153A (now flashed 10-20 10:03).
- [x] **CAL_PROBE (1.21) host decoder** (mcp-gen6, b3b9c51)
- [ ] **1.17 on the unit, host side** (mcp-gen5, after the owner's 0x1536 flash and firmware/AGENTS.md section 35 checks 7-9): run
  `scope_capture_segments` (10 kHz generator: intervals 100 us +- 10 ns, every segment crossing at the pretrigger point) and
  `scope_averaged_capture(hardware=true)` against host averaging of the same signal (means within 0.1 code, N = 1 equal to READ_RAW);
  then make `hardware` default to auto when the record fits, add it to `scope_decay_fit`, and drop the "not yet verified" caveats.
- [ ] **Version bump to 0.3.0** in mcp/pyproject.toml and firmware PC_FW_VERSION, CHANGELOG section, release notes
  (mcp-gen3 item 4) when the owner gives the go for v0.3.0.
- [x] **Alias residual model check** (fpga-gen4)

## FPGA

- [ ] **Flash and bench 0x153C** (fpga-c4, built 10-21: `dist/al3-0x153C.bin` `2580b090...c7f2`, flash image `07393bf5...1f77`,
  unbenched; fw 0.2.104+ is built for it). Checklist: fpga/al3/AGENTS.md "0x153C built, fpga-c4"; bench checks: contract
  9.2 items 1-7 (README section 15). Then dist `recommended`, 0x153B `supported`.
- [x] **0x153C follow-ups outside the FPGA** (fw-cruft, branch `fw-cruft`, firmware/AGENTS.md section 77: the 0x0F writes
  dropped 43e2b0a, READ_LIVE / LIVE_READ retired b10bc96 + 1777a67, the 0x53 swap bit dropped 19618eb + eea44e1; found on
  the way: the UI roll mode still reads the removed 0x24 / 0x26, reported to the coordinator)
- [x] **Pin and flash 0x153B** (flashed 10-20 19:31, benched good 20:40-20:49, recommended)
- [x] **Long free-running testbench** for the decimation filter: fpga-gen4's 0x1535 work (tb/run.sh, seeds 12345/1/77, 13 FIR mutations caught; fpga/al3/README.md section 5, commit 292a596)
- [ ] **Open input / EXT bench tests 1-4** from fpga/al3/README.md "Changes" section 2 (needs an external 3.3 V source
  referenced to USB ground).

## Owner bench verdicts outstanding

- [ ] 0.2.40/0.2.41 layout: Roll/Average/Filter boxes and the pass-time label alignment; Max hold behaviour; overrange
  warning. (Auto mode verified by the coordinator on 0.2.41: 6/6 triggered records at 10 MS/s, untriggered flagged truthfully.)

## Roadmap to 0.3.0 (reconciled 2026-10-20)

Update 10:45: fw-cap2 merged as 0.2.81 (band training, picks 0x08E3 on the unit), fw-spzoom2 and fw-yield2 merged as 0.2.82
(pans keep the rate; note removed), pinned backups/fw-0.2.82_5ad6085e.bin and loaded. Open from their handoffs: Waterfall /
Density still clear on a pan (agent's choice, rows fit their window; owner may want history kept); the training's two captures
per point can still miss an intermittent stage (24 % at two bad codes), the repeat warning catches it afterwards.

State: fw 0.2.80 (protocol 1.24) on the owner's unit, FPGA 0x153A flashed 10-20 10:03. Epic view: `docs/plans/roadmap.md`;
tag-day list: `docs/plans/release-checklist.md`. Items below point to the entries above.

- **In flight (dispatched, not merged)**: fw-cap2 ("Capture-stage training must probe the band": several codes across 120..140,
  widest clean run, a persistently flagged core asks for a calibration; hardware-status 10-20 10:19); fw-spzoom2 ("Spectrum
  ladder: pans never change the rate"); fw-yield2 ("Remove the 'Host command: menu closed' note").
- **Bench with the owner**: re-run the wizard after fw-cap2 (CH1 A is trained to the bad stage 4 on 0.2.80); firmware/AGENTS.md
  section 53 rest (wizard mid-measurement detail 3, keypad half-typed, file-save detail 4), section 48 B3/B4/B5/B7 ("Trigger
  position varies by run"), section 49 overview walk-through, the UI walk-throughs of 0.2.63-0.2.77 (polish batch, zoom ladders,
  Measurements v2, Frequency response); "Morning bench with the owner" remains (+-1.0045 V, phosphor visual steps, persist a
  release candidate after a soak); the two-hour idle ("USB: device lost after a long idle"); "AUTO SET in Frequency response"
  retest on 0x153A; open-input / EXT tests 1-4; the outstanding 0.2.40 / 0.2.41 verdicts; CH2 zero checks ("CH2 range-5 zero
  drift", "CH2 reads +1.0..1.5 codes at range 6", "Overnight zero step", "Gain-per-range check", "Calibration v2 bench").
- **Firmware batches still open**: "Frequency
  response small items" (a)-(c) (single sweep stays on the fold-out, generator chip after a sweep, the CH1-level warning
  wording); "Hardware averaging with the default filter" (needs the owner's decision); touch batch 3 (no blocking touch loop);
  Measurements v2 remainder (H2/H3, SFDR, band power, OBW, AC RMS, delay / phase / gain, PI tau); "Phosphor fast capture loop".
- **MCP items**: "Server releases the serial port when idle"; "Bode follow-ups"; "1.17 on the unit" (segments, hardware
  averaging) then `hardware` default auto; "1.19 / 1.20 on the unit" device-vs-host sweep on 0x153A; simulator lacks Frequency
  response reason 8 and the header-before-first-sweep rule (see "Frequency response small items"); version bump to 0.3.0.
- **Docs items**: "Documentation site and write-ups" remains (screenshots on the unit, per-screen guide text, six articles,
  agent worked examples, README screenshot block); `docs/release-notes/0.3.0.md` placeholders (0x153A entry, screenshots,
  tested-on list, known issues); CHANGELOG hygiene before the tag; the checklist still names 0x1538 as the recommended
  bitstream, 0x153A (dist entry unbenched) has superseded it.
- **Release steps** (`docs/plans/release-checklist.md`): bitstream pins and `tools/bitstreams.py check --release`, version bump
  in `pc_cmds.h` and `mcp/pyproject.toml`, release commit with CI green, SD package, release candidate persisted plus the
  30-minute soak, `v0.3.0-rc1` dry run, tag `v0.3.0`, FEL hash check, announcement once.
## Dispatched

- fw-105 (Opus explicitly, dispatched 16:52, branch fw-105): number the 0x153D firmware 0.2.105 (PC_FW_FPGA_VERSION 0x153D),
  remove the now-dead per-bitstream checks (the logger's -2 path, roll mode's "needs 0x153D"), fold the provisional
  CHANGELOG blocks into a 0.2.105 section, protocol row 67 numbered, firmware/AGENTS.md section 80 with the bench checklist.
- fpga-d3 16:4x: 0x153D timing closed (commit ecdc6f0, roll h2x: the hold-off block and the logger block moved, no logic
  change): clk_200MHz 0 errors, 4.945 ns (+0.055 ns), every other clock 0 errors; 6430 LUT (74.4 %) / 5911 reg / 7451 LE
  (86.2 %, 1189 LEs free). 21 rolls; TD 4.6.8 has no placer seed / effort, optimize_place / optimize_route are no-ops,
  moving a clk_200MHz always block re-rolls. Full two-seed run.sh (18 steps) running, about 4-5 h; then dist and push.
- fw-nopat done 16:3x, merged 16:3x: SET_PATTERN 0x0217 / SET_PATTERN_TRIGGER 0x021A removed (unknown command), caps 24 / 27
  and features 12 / 13 reserved (features 0x0FFF), trigger source 3 refused, GEN 40 B with bursts / pattern source reserved,
  fpga_pat_* renamed fpga_gen_*, the model on fpga-d3's stripped square (commit taken at once), check_retired_regs lists
  0x6E / 0x6F / 0x76-0x7D / read 0x42, scope_pattern and the simulator's pattern models gone (39 tools, 41 with tier 4),
  protocol row 67 rewritten as built. Host tests green, check_vectors OK, ruff clean, 541 mcp tests, gcc10
  2fb27064f6013bc9620f2330ce0871c247d560e2f43600e688f40ddf64d6e23b. Open: fpga/al3/README.md pattern text (fpga-d3's file).
- fpga-d3 interim 16:3x: the stripped 0x153D builds at 6421 LUT (74.3 %) / 5911 reg / 7447 LE = 86.2 % (13.8 % free); the
  square with the DDS removed costs +373 LUT / +274 reg / +375 LE over C4 + D1. clk_200MHz not closed: b2 -1.103 ns (q_pass),
  rolls h1 -0.362, h2 -0.044 (q_untrig), a2 -1.240, h3 -0.682, all routing on old paths; six more positions building.
  tb_square 14102 checks both seeds (start 7-8 clocks after the 0x6D strobe), contract 42 checks, ext 39 checks.
- fpga-d3 interim 16:1x: origin/fpga-d2 merged and stripped to the fractional square (d60a0ad, not pushed): kept 0x6C entries
  0 / 1, 0x6D (modes 0 / 3, idle level, start, commit), 0x50, read 0x39 reduced to RUNNING / PENDING / step / mode active;
  0x6E, 0x6F, 0x76-0x7D, read 0x42 not decoded; 0x15 = 3 is EXT again. Changed semantics: a commit is taken at once when the
  step in progress has >= 3 clocks left (D2: from the third boundary), two commits in a row both apply. Estimate +180 LUT /
  +330 reg instead of D2's +1119 / +950; build running; tb_decim got tb_fir's mem_dirty mask. Forwarded to fw-nopat.
- Post-0.3.0 candidate (coordinator, 16:1x, owner asked what else could be compacted; planned as a future 0x153E with both-core
  triggering in docs/plans/roadmap.md section 6, owner 16:2x): a measured compaction audit on the
  0x153D source like contract section 13: the 0x17 / 0x55 level pair, seq_raw, cycle-identical narrowings (counter min / max
  about 64 reg / 40 LUT, the logger's u32 word counter with a firmware change, the square's u32 step count: 24 bits = 6 Hz
  floor). Before 0.3.0 only if fpga-d3 cannot close timing; feature blocks (filters, FIR, sequencer) are the owner's call.
- fpga-d2 done 15:5x, pushed f085b05 (not merged to main: the owner cut the pattern feature; fpga-d3 integrates the branch
  stripped to the square). tb_pattern 10462 checks both seeds; run.sh 18 of 19 PASS both seeds, tb_decim's 2 K = 0 failures
  reproduce on the base source (a testbench effect of the DDS removal: EXT captures run to their time-out; fix = tb_fir's
  mem_dirty mask, handed to fpga-d3). TD b6 with C2: 7351 LUT / 6416 reg / 8321 LE (96.3 %), 22 errors on old paths.
  Square-only estimate: 150-190 LUT / 180-270 reg + ~96 reg for the glitch-free shadows, about 81 % of the LEs.
- fw-nopat (Opus explicitly, dispatched 15:56, branch fw-nopat): the pattern cut on the firmware / MCP / docs side: SET_PATTERN,
  SET_PATTERN_TRIGGER, trigger source 3 PAT, the engine and marker registers and their caps / features retired, the square path
  unchanged (fpga_pat_* renamed fpga_gen_*), fpga_model to the stripped set, check_retired_regs extended, scope_pattern and
  the simulator's pattern models removed, protocol row 67 rewritten as built, CHANGELOG fw-d5 / mcp-d6 entries rewritten.
  fpga-d3 told to strip D2 to the square keeping 0x6C entries 0..1, 0x6D, 0x50 and 0x39's square bits; fpga-d2 told to push as is.
- fw-roll done 15:4x, merged 15:4x (head a83ea0d): roll.c (divider plan, column means, 3000-column record, trigger) + roll_view.c
  on its own 32 KiB stream ring (both channels, legacy 8-bit, 200 S/s at 2 s/div and slower up to 4 kS/s at 100 ms/div);
  averagecommand / fpga_average_trace_data(_long) / the old roll functions removed; check_retired_regs covers 0x24..0x27; ARM
  and LOG_START in a long time base now stop the UI roll instead of E_STATE detail 2. test_roll 161 checks; host tests green,
  check_vectors OK, ruff clean, 548 mcp tests, gcc10 8abd78c3fe60c2e6e97179e095359afc4fb502eab0af8ce99df00520a1d47e6f. At
  numbering for 0x153D: the "Roll mode needs FPGA bitstream 0x153D" path (pc_hal_log_start -2) becomes dead under the
  one-bitstream rule; remove it then. Open (section 78): saved roll waveforms unchecked, unmarked gaps after long menus.
- fpga-d3 interim 15:2x: C4 + D1 build b0 6048 LUT / 5637 reg / 7072 LE (82 %), clk_200MHz -0.226 ns on q_pass; four hold-off
  re-rolls all worse (-0.53..-1.16 ns on other old paths). Projection with D2: about 7170 LUT / 6590 reg / 8310 LE = 96 % of
  the LEs, register-bound; D2's own no-engine variant at 88 % did not close. Cut order it will put to the owner once the
  merged build confirms it: (1) the pattern trigger engine -451 LUT / -169 reg, (2) STEPS = 4 -172 / -208, (3) outside the
  contract: the logger's snapshot copies or the counter's min / max. Nothing cut yet; waiting for origin/fpga-d2.
- Coordinator 15:1x: the account's session limit cut off fw-roll, fpga-d2, fpga-d3 and fpga-c4 at about 15:10 (the WSL runs
  survived). The owner switched accounts and the coordinator to Opus: Fable only for the most difficult FPGA development
  (D3 integration, later E1 / E2). fpga-d2, fpga-d3 and fw-roll resumed; fpga-c4's remaining step (the run text after the
  rerun on bd24bdf) is done by the coordinator from ~/c4/logs/full2-*.log.
- fpga-d2 status 15:06 (on request): both full runs alive in step 16 of 18, push expected about 16:10; build b6 of its merged
  pre-C4 source 7351 LUT / 6416 reg / 8321 LE (96 % of the LEs), clk_200MHz 22 errors at 6.49 ns (divider compare, accumulator
  RAM; pattern paths clean); two tb_decim K = 0 equivalence mismatches on both seeds under investigation, documented rather
  than holding the push. Relayed to fpga-d3: the integrated 0x153D is LE-bound; report the cut order with numbers before cutting.
- fw-cruft done 14:4x, merged 15:0x: the 0x0F writes gone (check_retired_regs.py in make test), READ_LIVE 0x0206 + cap bit 9
  retired in row 67, 0x53 carries K alone (SET_DECIM drops flags bits 0..2, GET_DECIM reports 0; mcp DECIM_SWAP / NEG_* gone),
  fpga_model 0x29 reads bits 4..2 as 0 on 0x153C+. Host tests green, check_vectors OK, ruff clean, 548 mcp tests, gcc10 sha256
  4ec3bd0f267265270b775fa1183e65ef518de8ba8feaf6e3fa7abdece9c7933e. Found: the roll mode (100 ms/div to 50 s/div,
  test.c scope_get_long_timebase_data / scope_check_long_trigger via fpga_average_trace_data, averagecommand 0x24 / 0x26)
  reads the live ADC pins 0x153C removed; the roll trace is invalid on 0x153C (fw 0.2.104 + 0x153C is a bisect pair only).
- fw-roll (Opus explicitly, dispatched 15:06, branch fw-roll): roll mode on the streaming logger per the owner's decision
  below: the long timebases take their points from the 0x153D stream (fpga_log / pc_logring on 0x69 / 0x6A / 0x38) instead
  of the 0x24 / 0x26 live-pin averages; averagecommand and fpga_average_trace_data(_long) go.
- fpga-d3 (Fable, dispatched 14:03, branch fpga-d3): 0x153D integration per contract item D3: merge fpga-d1 (pushed) and
  fpga-d2 (when pushed) onto C4's cruft-removed source, version byte 0x3D, contract_0x153D.txt, run.sh (18 steps) ALL PASS
  both seeds, TD build with every clock closed (re-roll by block order first), dist/al3-0x153D.bin + manifest (unbenched,
  min firmware 0.2.105 provisional), README section, handoff with the owner's flash checklist (read --twice = the 0x153C
  padded image once that is flashed). Firmware handoffs (sections 74-75) win on register semantics; mismatches reported.
- fw-cruft (Opus explicitly, dispatched 14:03, branch fw-cruft): the firmware / host side of C4's cruft removal: the dead 0x0F
  writes, READ_LIVE 0x0206 + cap bit 9 retired (never built; folded into protocol row 67), 0x53 bits 4..2 mask, protocol.md
  lines 263 / 887 / 984 / 992, mcp ReadLiveReq / read_live / simulator, vectors regenerated, queue item line 226 closed.
- fpga-c4 pushed 7f6eb3a 13:5x (asked to push before its rerun on the final source finishes; the rerun text follows in one
  more commit), merged 14:0x: dist/al3-0x153C.bin 282,898 B sha256 2580b090db3dd16f5609ddcb0d27576f9dfe6a4651dbafa85000d807a6bec7f2,
  padded 1 MiB 07393bf5a728542b7de0738ca8aab7ccbf3b00e7267ca1efd8e66f8c2d361f77, status unbenched, min firmware 0.2.104,
  bitstreams.py check 10 consistent. TD h4: 6126 LUT (70.9 %), clk_200MHz 0 errors (+0.228 ns), cruft removal -206 LUT.
  First two-seed run ALL PASS except the tb_fir seed-12345 divergence (a simulator event-ordering race of the div20 alias
  wire, fixed in bd24bdf, same bytes); the 16-step rerun on bd24bdf is in progress. Flash checklist in fpga/al3/AGENTS.md
  (probe ef 40 14, read --twice = the 0x153B image 1391978c...3b88, write --confirm-id ef4014, read-back 07393bf5...1f77).
  The firmware's 0x0F writes do nothing on 0x153C and READ_LIVE was never built: no bench impact, cleanup queued (fw-cruft).
- fpga-d1 done 13:4x, pushed e1ee9dd (not merged yet: the 0x153C dist must come from fpga-c4's cruft-removed source, so D1
  and D2 land after C4 and D3 integrates them on that source). Logger RTL: 0x69 bit 0 stream (no trigger accepted, two
  synchronised copies), bit 1 clears the lap; 0x6A u13 R = the next word the MCU reads, 0x01 = 1 resets R / unread / counter;
  read 0x38 8 B snapshot {stream, lap, 0, W[12:8]}, W[7:0], u32 words written, u16 unread 0..8192. Deviations from the contract
  (adopted by fw-d4): the lap is a full detector (unread count = 8192 while streaming), byte 0 has one reserved bit (W is 13
  bits), 0x01 = 1 zeroes R too, W = address after the last word written. tb_stream 757 checks both seeds; full run.sh 17 steps
  ALL PASS both seeds on the pre-merge source, stream + ext steps on the merged (C2) source; rates T1 500 kS/s, T2 250 kS/s,
  T3 125 kS/s at the 300 ns strobe (FPGA not the limit). TD: 6208 LUT (71.9 %), +129 LUT over C1 + C3; 4 setup errors at
  -0.383 ns on C3's c_mx > k_mx compare (not a logger path; D3 re-rolls). Handoff in fpga/al3/AGENTS.md, README section 15.
- mcp-ci done 11:4x, merged 11:4x: the Bode tests compare the generator's configuration (the live 0x39 bits masked; the
  sweep's last SET_GEN leaves PENDING for up to two steps), and the simulator's log lap hook fires only once a word is unread
  (the IndexError was lap[0], the lap had skipped 0 words when the first read shared the start's clock tick; Python 3.12 on
  Windows has a 15.6 ms clock). No host behaviour change, no CHANGELOG entry. ruff clean, 53 tests of the three files pass
  locally; the CI run of this push is the proof.
- mcp-ci (Opus explicitly, dispatched 11:27, branch mcp-ci): the Windows CI mcp job has failed on both pushes since mcp-d6
  (runs 39569591451, 39576242367; Linux and the local Windows run green): test_bode_sim / test_bode_device compare a GET_GEN
  snapshot that includes the live 0x39 pattern status byte (128 before, 129 / 161 after), and test_d27's logger overflow test
  (32 KiB ring, log_stall_s 0.3) finds no lost chunk on the slow runner. Fix the tests (compare configuration, make the stall
  deterministic), keep the contract assertions. Owner reported the failure 11:2x.
- fw-small done 11:1x, merged 11:1x as the merge of 85b9315: fpga_force_stop (0x0E = 0 then 0x1A = 0, test_force_deadline fails
  33 checks without it), Vmax / Vmin / Vpp from the peak bytes with Sample = Peak (pc_glue_peak_extremes, pb_extremes test),
  the readout-fault text no longer points at the bitstream (calibration.md row back). Host tests green, check_vectors OK, gcc10
  sha256 42288cb180dfcd3b3aaa386429791e992e0e7fdf19f2059e80a78b83229ff440 (unnumbered, 0.2.104 + D4 + D5 + fw-small). Open from
  the handoff (section 76): FORCE during a sequencer run (debug builds) ends later segments after ~2 words; STOP ends a run.
  Bench: an explicit auto_timeout stops at the deadline; a spike with Peak reads full height in Vmax / Vpp.
- fw-small (Opus explicitly, dispatched 11:0x, branch fw-small): the three small firmware items left open by fw-one, fpga-gen13
  and ui-sample, bundled while the FPGA runs finish: (1) `pc_hal_force` writes 0x0E = 0 before 0x1A = 0 so an explicit
  auto_timeout stops at the deadline (queue item "the deadline force must write 0x0E = 0 too", test_fpga_bus model); (2) the
  Vmax / Vmin / Vpp cards read the peak record's min / max bytes with Sample = Peak (ui-sample section 73 "Open", ui-rows.md
  3.3), the ScopePeak board and the cards agree on a spike; (3) the "ADC readout fault near code 128" wizard message: keep it
  only if ACAL_E_READOUT is still reachable with the one supported bitstream and document it in calibration.md, else remove
  the code path. fw-d5 follow-up 2001e99 (host model start latencies) merged 10:5x as 3a81158, host tests green.
- Coordinator 10:2x: fpga-d2 reported its detached WSL runs dying with empty logs (a broad `pkill -f` from another agent on
  the shared WSL instance); rule sent to fpga-c4 and fpga-d1 and to be written into fpga/al3/AGENTS.md "Build" by C4: kill
  only by PID or by a pattern with your own work directory, never by vvp / run.sh / td / build.sh / tb_.
- fpga-c4 (Fable, dispatched 09:54, branch fpga-c4): 0x153C integration: the C0 cruft removal (section 13 list, 0x1B kept,
  div20), version byte 0x3C, the timing re-roll on the merged source (known misses on q_pass / pp_i), contract_0x153C.txt,
  run.sh 16 steps on two seeds, dist entry as a candidate (min_firmware 0.2.104), BITSTREAMS.md row, AGENTS.md status
  with the owner's flash checklist.
- fpga-c2 (Fable, dispatched 22:21, cut off and resumed, done 09:5x, merged 09:53 as 50cbc71): peak detect as the one-
  register switch (dec_on includes peak_en; decim_chan at order 0 already behaves as K = 1); the shared dec_trig_sample
  lines take the window max / min as the trigger byte; the reserved-bit writes in tb_decim / tb_fir / tb_seg keep bit 1
  clear; tb_peak 45; full run.sh 16 steps ALL PASS both seeds on the merged source; TD +136 LUT alone with a -0.504 ns
  miss on the sequencer's pp_i enable (lottery). WSL note: detached runs die when no wsl.exe stays attached.
- mcp-d6 (Opus, dispatched 09:05, done 10:2x, merged 10:23 as fc71df7): protocol 1.27 on the host, byte-matched to fw-d4 and
  fw-d5 (test_d27 holds their vector payloads; bode-plan-vectors.json v2 byte-identical): scope_log / scope_log_status /
  scope_log_stop (raw sample files + log.json, pumped in the background under the session lock), scope_pattern (tier 3,
  capture=true arms first), scope_sweep (host-stepped, ~2,000 steps/s), source "pattern", GEN exact frequency, report
  kinds log / pattern; 549 tests; README 40 tools. Note: the self-test's fast square is 200 MHz / 201 now (an even period
  puts every edge on the same core, so the T4 interleave check lost its information: bench item). Main with fw-d5 +
  mcp-d6: host tests green, check_vectors OK, gcc10 builds (817,152 B), ruff clean; pushed when pytest finishes.
- fw-d5 re-push 2bd7042 merged 10:01 as 238ee86 (local only): host tests green except test_bode.py (PointPlan.period comes
  with mcp-d6's bode.py), check_vectors OK; main is pushed together with mcp-d6 so CI stays green. fpga-d2's final rule: a
  commit during step k applies from step k+3, step 0 starts about 35-40 ns after the start write (the model follows).
- fw-d5 (Opus, dispatched 09:03, reported 09:4x, merge handed back 09:50): the generator as a fractional two-step pattern
  behind SET_GEN (P16 / H16 raw modes; GEN response 40 B with the 0x39 status), SET_PATTERN 0x0217 (12 + 8n B, start at
  offset 10, pin hi-Z flag), SET_PATTERN_TRIGGER 0x021A (28 B, response 32 B), trigger source 3 = the marker (button
  "PAT"), Bode on P = 4 M (vectors file version 2, even P above 4095 at N = 16), 18 strobes per frequency step; caps
  bits 1 GENERATOR / 24 / 27, features 12 / 13; gcc10 e900d830 (provisional). Its push predates fw-d4's merge: 14 files
  conflict, so fw-d5 merges origin/main (-X renormalize) in its worktree and pushes again; the two Bode tests wait for
  mcp-d6's bode.py. fpga-d2 rule: a commit during step k applies from step k+2.
- fw-d4 (Opus, dispatched 09:03, done 09:3x, merged 09:40 as a304273): the streaming logger: LOG_START 16 B / LOG 48 B /
  LOG_READ (16 B header + whole words, oldest first, never across a gap) / LOG_STOP, 8 MiB NOLOAD .logring in DDR, acq_state
  5, the contract tiers at 500 kB/s with 52-82 % CPU duty in the host model, gap and lap flags, the Scope view's "Logging
  to the host" notice with no UI pass while logging, auto-stop 10 s without a LOG_READ; register semantics agreed with
  fpga-d1 (0x6A = next word to read, full detector, 0x38 byte 0 = { stream, lap, 0, W[12:8] }), bytes agreed with mcp-d6;
  test_log 344, 151 vectors; gcc10 488d2827 (provisional). Deviations: LOG_READ returns at once, header 4..7 = the FPGA
  word counter, features bit 11, LOG_START needs the Scope view and a format matching the Filter.
- Coordinator 09:42: the fw-d4 merge brought 30 files in with CRLF on every line (and Atlan's vendor files had been CRLF
  since the start); the coordinator's CR check with the shell escape never worked. Fixed 0f177ca: every text file LF,
  .gitattributes text=auto eol=lf (al_ip sources excluded), merge.renormalize on; host tests, vectors and the gcc10
  build re-verified after the normalisation.
- mcp-c8b (Opus, dispatched 09:00, done 09:1x, merged 09:17 as 2c29614): counter level 0 = follow (simulator), the host bug where
  read_counter re-sent the level it read and pinned it on any gate change (fixed: the client remembers the level it set;
  scope_counter level='trigger'), peak detect aligned with all 41 decim_peak device vectors (legacy cases write nothing,
  FIR dropped / refused with peak, settle 11 N x 5 ns + 1 us), the simulator's GET_CAL gated flag follows the divider,
  README 35 tools / release notes 37 and 35; 524 tests. Remaining host-vs-firmware differences are timing-of-application
  only (the firmware applies a counter configuration on the next pass; the simulator at once).
- Coordinator 09:03: the fw-d5 and mcp-d6 briefs wrongly named a SET_SWEEP 0x0218; the contract has none (the chirp is the
  host stepping SET_GEN through the double buffer). fw-d5 caught it; both agents told to follow the contract.
- fw-d4 / fw-d5 / mcp-d6 (Opus, dispatched 09:03-09:05, branches of the same names): 0x153D firmware and host from the
  contract in parallel with the RTL: D4 LOG_START / LOG_READ / LOG_STOP with the DDR ring, acq_state 5, the contract
  rates; D5 the generator as a two-step fractional pattern behind SET_GEN (P16 / H16), SET_PATTERN / SET_PATTERN_TRIGGER /
  SET_SWEEP, Bode P = 4 M; D6 MCP 1.27 (scope_log, scope_pattern, scope_sweep, source "pattern", simulator). The firmware
  writes its bytes into protocol.md row 67; D6 matches at the merge (as C5 / C8). PC_FW_FPGA_VERSION stays 0x153C until
  the 0x153D integration (D3).
- fpga-d1 / fpga-d2 (Fable, dispatched 09:0x, branches of the same names): 0x153D items D1 (logger RTL: 0x69 / 0x6A / 0x38,
  stream gating, word counter, lap; tb_stream step 17) and D2 (pat_gen: 8 steps u32 + u16 fraction, double buffer, bursts /
  repeat / gap / continuous, marker as trigger source 0x15 = 3, the DDS removed, the pattern trigger engine 0x76-0x7D /
  0x42 as a third trig_qual instance with the routed fallback if timing fails; tb_pattern step 18). Both start from main
  with C1 + C3 (C2 and C4 land in parallel); C4 owns the version byte, the contract file, dist and the C0 cruft removal.
- mcp-c8b (Opus, dispatched 09:0x, branch mcp-c8b): host follow-ups to fw 0.2.104: simulator counter level 0 = follow, the
  full-class peak correction and the "not written" legacy case, the tool count in README / release notes, mcp/AGENTS.md.
- fw 0.2.104 numbered 08:5x (built for 0x153C: fw-c5, fw-c6, fw-c7, ui-type, ui-sample merged; ui-sample done 08:5x, merged
  08:57 as 2d4861f): host tests green on merged main, check_vectors OK (140); gcc10 sha256 7c640ab5fe1f9b1c75dc321c96038e7dc3fe8fb88fd9e105e81cf344351d2cbc pinned as
  backups/fw-0.2.104_7c640ab5.bin. Needs the 0x153C bitstream on the unit (C4 pending on fpga-c2); on 0x153B it shows the
  notice. ui-sample notes: Vmax/Vpp cards read the midpoint trace with Peak (open), at 200 MSa/s the record stays 8-bit
  and is drawn as bars from the samples, the roll-mode note is always shown in roll.
- ui-sample (Opus explicitly, dispatched 08:33, branch ui-sample): the ACQ Sample row per ui-rows.md (478 px menu, fourth box,
  Peak greys Filter and Average with the four-line note, no box in Spectrum, grey in roll), the UI's conversion through
  fw-c6's peak mode, the bars trace (integer step, overload from either byte, phosphor accumulates, X-Y midpoints), trace
  style "Peak" renamed "Bars", persistence, tests, docs.
- fw-c6 (dispatched 05:12, cut off and resumed, done 08:3x, merged 08:32 as ba6ce49): peak detect in the firmware byte-exact
  with the host: SET_DECIM filter 4 (the Filter's K / FIR kept behind it), GET_DECIM 0x81 / 0xC0 bytes, GET_CAPTURE_INFO 79
  bits 1/0, READ_RAW layout 3 minima then maxima, READ_TRACE 8-bit = the corrected midpoint and WIDE = the pairs, SET_SEQ
  mode 2 E_STATE 7, totals > 8190 refused while Peak; the full-class mean-core correction; 0x53 = 1 written at a peak arm;
  140 vectors; caps bit 21, features bit 9 (0x7FF); gcc10 41bd66ce (provisional). Note for C8: GET_CAL reports the gated
  class at D > 0, the peak correction uses the full class.
- fw-c7 (dispatched 05:13, cut off and resumed, done 08:1x, merged 08:19 as 4639122): the counter in the firmware:
  fpga_counter.c (boot 0x62 128/6 and 0x61 gate 100 ms source 0; one 0x35 read per pass, 17 strobes; 64-bit integer
  derivation), the rate-change rule = rewrite 0x61 (0 then value) and drop that pass's result, the level follows the
  trigger level unless a host pins it (level 0 = follow); GET_COUNTER byte-exact with the host; the Freq / Period / +-Width
  / +-Duty cards of the trigger source's channel take the counter while a current result holds (Vmin/Vmax/Vpp stay
  record-based: deviation from contract section 5); caps bit 22, features bit 10; 137 vectors; gcc10 3faa7a98 (provisional).
  MCP follow-up: the simulator refuses hysteresis > level for level 0; accept it as the firmware does (level 0 = follow).
- Coordinator 08:10: the account's Fable quota cut off fw-c6 and fw-c7 mid-edit; the owner reset it and both were resumed
  in place. Record correction: every agent dispatched since 22:00 without an explicit model (labelled "Opus" above) ran
  on the coordinator's model, Fable, because a general-purpose agent inherits it. From here firmware, MCP and docs
  agents are dispatched with model opus explicitly; FPGA RTL stays on Fable per the owner.
- ui-type (Opus, dispatched 05:13, done 05:4x, merged 05:48 as ae449b8): the trigger menu's Type row per ui-rows.md: trig_ui
  module (ladder, word period, ranges, snap, settings words 202/203 with 0xA000, glyphs), the Pulse polarity row, the
  keypad for N, a two-slot notice queue (time/div first), the shared No-trigger hint, the menu edits through fpga_trig_write
  (SET_TRIGGER2's path), a host SET_TRIGGER2 syncs the menu; the Frequency response sweep arms the edge type around
  pc_hal_arm; test_trig_ui 340 checks; gcc10 d14151b1 (provisional). Deviations: row labels stay white in the viewer, a
  host timeout type shows no chosen box, the Nth glyph is two short steps with stubs (the mockup's comb renders solid).
- mcp-c8 (Opus, dispatched 04:32, done 05:3x, merged 05:38 as bcb269b): protocol 1.26 on the host; Trigger2
  byte-identical to fw-c5; peak detect and GET_COUNTER wire formats from the contract (sent to fw-c6 / fw-c7 to match:
  SET_DECIM filter 4, shift-byte bits 7/6, mode 0x81, READ_RAW layout 3 = minima then maxima per channel, GET_CAPTURE_INFO
  79 bits 1/0, E_STATE 7; GET_COUNTER 0x0213 request 8 B with what = configure, response 24 B with level / hysteresis /
  freq_mHz); tools scope_set_trigger types, scope_counter (tier 0 read / 1 configure), scope_capture(peak=true),
  report kinds; simulator TrigQual port, hold-off, peak records, counter; 131 vectors; fw-one leftovers done; 520 tests.
- fw-c6 / fw-c7 / ui-type (Opus, dispatched 05:12-05:13, branches of the same names): C6 firmware peak detect (SET_DECIM
  mode 4, READ_RAW layout 3, the refusals, fpga_model, features bit 9, caps bit 21), C7 firmware counter (boot 0x61/0x62,
  the once-per-pass 0x35 reader, the cards' sources, GET_COUNTER / SET_COUNTER, features bit 10, caps bit 22), the trigger
  menu's Type row per docs/plans/ui-rows.md (rows, glyphs, ladder, snap notices queued, hints, persistence 0xA000). C6 and
  C7 share pc_cmds.c / fpga_model.c in delimited blocks; the ACQ Sample row follows C6.
- fw-c5 (Opus, dispatched 04:32, done 05:10, merged 05:11 as c432354): built for 0x153C, protocol 1.26 row 66: SET_TRIGGER2
  0x0219 24 B (type, polarity, flags LONGER, source, N u32, H u32, level, level2 = 0, hold-off u32 in 5 ns, reserved),
  GET_TRIGGER 52 B (old 28 + the body in effect), GET_STATUS 52 B (0x37, 0x34 byte 0, edges to count), GET_CAPTURE_INFO
  112 B; caps bits 2 and 20, features bit 8 (the contract's 5/6/7 were taken); the arm window writes 0x5E/0x5F/0x60 on
  every arm, 0x1B/0x66 only on a definition; host tests green, 115 vectors OK; gcc10 bde3ee77 (not pinned: the unit shows
  the mismatch notice until 0x153C is flashed). mcp-c8 told the exact bytes 05:12.
- fw-c5 (Opus, dispatched 04:32, branch fw-c5): 0x153C item C5, firmware trigger types + hold-off: PC_FW_FPGA_VERSION 0x153C,
  caps bits, SET_TRIGGER2 0x0219 / GET_TRIGGER per the contract (protocol 1.26, row 66), the arm sequence of 3.5, runt /
  window / range refused until 0x153E, fpga_model.c, host tests, vectors, docs. C6 (peak detect), C7 (counter) and the two
  UI rows start after it merges.
- mcp-c8 (Opus, dispatched 04:32, branch mcp-c8): MCP protocol 1.26 for 0x153C: SET_TRIGGER2 / GET_TRIGGER / GET_COUNTER /
  SET_DECIM mode 4 / READ_RAW layout 3, scope.py methods, tools (trigger types on scope_set_trigger, scope_counter, peak on
  scope_capture, report kinds), simulator models, vectors, docs; plus fw-one's leftovers (mismatch caveat in scope_info,
  ui_averaging_records, the --variant table, "on 0x1534 / 0x1536" wording). Vectors reconciled with fw-c5 at the merge.
- fpga-spec measured update (done 23:27, merged 23:29 as bb2880b; 827 lines): TD-measured deltas on the 0x153B source: DDS -61
  LUT, cruft (0x1B/0x3C/0x41/0x24-0x27 decoder entries, dec_neg, dec_swap, sample_read_enable, trigger_detect_enable, w_last
  under SIM) -78, divider compare 32->20 bits -53; the two-step fractional pattern core +48 LUT / +231 reg (one 16-bit
  accumulator per engine, cleared on burst start; the carry path is pipelined in the contract). Totals: 0x153C ~71 %, 0x153D
  ~79 %, 0x153E ~85 % with ~1,300 LUT margin. Generator = two-step pattern (0x6C u32 + u16 fraction, double buffer 0x6D bit
  5 / 0x39 bit 5), SET_GEN keeps its layout (P16 / H16 in 1/65536 clocks), Bode P = 4 M, 0x51/0x52 retired. Coordinator
  plan change: the C0 cruft removal (contract: "first, on main") is folded into C4 integration (Fable) after C1-C3 merge,
  so the three running RTL branches are not disturbed; fw-one told 23:29 to stop writing 0x53 bits 3/4 and to bound the
  divider at 199,999.
- ui-rows-review (Opus, dispatched 23:39, done before 23:48, read-only): adversarial review of the Type / Sample row mockups and note against
  docs/ui-guidelines.md and the owner's taste; answers to the note's six open questions so the coordinator can decide
  tonight and the owner veto in the morning.
- ui-rows round 2 (done 23:55, merged 23:56 as e433ed2): all review items in the boards and the note (ACQ 478 px with five
  chips and the real pass-time y, Pulse polarity row, pixel glyphs, viewer and Frequency response boards, "Bars", Idle
  "Off", keypad for N, snap notices queued time/div first, marker 0xA000). Leftovers accepted by the coordinator: the
  "not applied" note on four lines in the 66 px column; in Frequency response the parameter rows are not drawn (the sweep
  does not use them) so the menu fits. The mockups are the acceptance reference for the C5/C6 UI items; owner veto pending.
- Coordinator decisions on the ui-rows review (23:4x, as the owner's proxy, owner veto pending): ACQ menu widened to 478 px
  with Sample as the fourth box (the mockup hid a collision with the five memory chips and drew the pass time at the
  wrong y); no silent clamp: Width / Idle snap to the rate's range with the snap notice, +/- stop at the ends, Width
  Shorter >= 2 words; no feature gating (one-bitstream rule); hints say only what the firmware knows; under Pulse the edge
  row becomes Pulse polarity Positive / Negative; trace style "Peak" renamed "Bars"; Idle zero step "Off", values
  capitalised; no Sample box in Spectrum, grey in roll; viewer and Frequency response boards; N by +/- and the generator
  keypad on a tap, no held repeat; chip glyphs in firmware pixels; bars drawn with the integer step, overload from either
  byte, phosphor accumulates, X-Y midpoints. ui-rows round 2 dispatched just before 23:48.
- ui-rows (Opus, dispatched 23:15, done 23:37, merged 23:38 as 1742743): eight mockups under docs/mockups/ui-rows/ (a
  headless-Chrome PNG renderer added to gen_ux), docs/plans/ui-rows.md. Type as the trigger menu's sixth row with Width
  / shorter-longer or N / Idle parameter rows (+/- ladder), the chip shows the type as a glyph in the edge slot, Peak greys
  Filter and Average with a note, "Scope only" in Spectrum; six open questions (ladder-only values, glyph vs word, N
  held-repeat, Peak with phosphor/X-Y, Sample in Spectrum, Idle on EXT).
 mockups and design note docs/plans/ui-rows.md for the two decided UI rows (Type in the
  trigger menu: Edge / Pulse width / Nth edge with parameters; Sample in ACQ: Normal / Peak, and the Peak-vs-Filter
  presentation), the trigger chip's text per type, a peak-detect trace mockup, the firmware checklist; adversarial review
  follows, the owner sees the mockups with the morning batch.
- mcp-soft (Opus, dispatched 22:06, done 22:45, merged 22:46 as 8eaf912; ruff clean, 496 tests): scope_decode (UART auto-baud,
  SPI, I2C; limits: UART >= 4 samples per bit advised, refused under 2; SPI 3 per clock; I2C 8 per SCL period), scope_mask_test
  (masks stored under the data dir, exact geometry only), scope_export_csv, scope_dvm (resolution statement); simulator
  serial signals; report kinds. README and release notes say 34 tools; release notes need a line on the four tools
  (release follow-up).
- **Overnight autonomy** (owner 23:1x): continue the roadmap without the owner as far as 0x153E; the owner verifies
  everything as one batch (or bisects by flashing 0x153C, 0x153D in turn if the batch fails). No hardware tonight: images
  and bitstreams are built, simulated and pinned; every generation keeps its own dist entry and firmware image for the
  bisect.
- **0.3.0 scope** (owner 23:08): 0.3.0 ships when the chosen roadmap items are complete and benched, not the current
  feature set; realistically 2026-10-21 or 10-22. Contents: fw-one (one bitstream, banner, one averaging), mcp-soft tools,
  0x153C (triggers, hold-off, peak detect, counter) with its firmware and MCP 1.25 and the two UI rows, 0x153D (logger,
  pattern generator with its trigger engine and fractional periods, no DDS, cruft removal) with firmware and MCP 1.26;
  0x153E by the measured numbers. The release notes, announcement and checklist get one final refresh before the tag;
  the version bump happens then. Reason (owner): see what is possible on commodity hardware, and have the detector
  tooling ready in a couple of weeks.
- Owner decision 23:0x: the pattern generator's durations get a fractional part (an accumulator per step, carry = one
  extra 5 ns clock) so any average frequency is exact and the edges stay within one clock; a few dozen cells. In the
  contract with the DDS removal.
- Owner decisions 23:0x (LUT savings): (1) the pattern generator absorbs the square-wave generator (a two-step continuous
  pattern with double-buffered durations; the DDS and its sweep logic go; firmware generator menu, wizard 1 kHz and the device
  Bode sweep program the pattern); (2) chirps are MCU-stepped patterns (one period write per step); (3) an FPGA cruft audit
  under the one-bitstream rule: register paths our firmware never writes are removed. fpga-spec measures the deltas by
  synthesis and updates the contract and the D2 brief; 0x153E's question is answered by the measured numbers.
- fpga-spec addendum (done 22:54, merged 22:55 as 4d35cb3): section 7.5 pattern trigger engine (registers 0x76-0x7D mirroring
  the capture trigger's, status 0x42, burst start source in 0x6D bits 6..5, caps bit 27, SET_PATTERN_TRIGGER 0x021A), fallback
  = the capture qualifier routed; 0x153D now ~7,350 LUT (85 %), 0x153E ~91 % and gated on 0x153D's measured count; logger
  rates are the spec; trigger-out wire removed; DDS sweep kept, first to cut (owner 22:53).
- Owner decisions from the evening Q&A (22:38-22:49): (1) logger contract rates 500 / 250 / 125 kS/s documented now, higher
  after the bench; (2) no soldering to FPGA or other pins in this project, the prepared flash header is the only one
  (the trigger-out wire option is closed); (3) the pattern generator gets its own trigger engine with the capture
  trigger's vocabulary (two orthogonal engines, same register layout with a base offset; a burst starts from the MCU or
  from its engine); the marker stays a capture trigger source for MCU-started patterns; no "pattern starts on the capture
  trigger" link. fpga-spec is updating the contract (section 7.5, D2 brief, the 0x153D estimate).
- docs-one (Opus, dispatched 22:04, done 22:22, merged 22:23 as 003924d): the card-only path gone from 15 files; README 175
  lines, 34/34 links; placeholders {{BANNER_TEXT}} / {{BANNER_REACHABLE}} (first-run.md) and {{AVERAGE_TEXT}} (screens.md)
  for fw-one's wording; README's manual steps reordered by the coordinator to download first (as install.md and the
  runbook). Follow-ups: docs/mcp-tools.md capability rows and agent-measurement-guide.md line ~58 still say "on 0x1534 /
  0x1536" (fix with fw-one's merge, when the caps become fixed per firmware); calibration.md's "ADC readout fault near
  code 128" row removed although the firmware message may still exist (fw-one removes or keeps the message; align then).
- fw-one (Opus, dispatched 22:02, done 04:23, merged 04:24 as 1f13767, numbered fw 0.2.103 / protocol 1.25 at 868f8c9):
  PC_FW_FPGA_VERSION 0x153B with fixed caps, the version word read once at boot sets only the mismatch flag; the notice
  screen (Diagnostics / Settings / USB drive reachable, acquisition and the generator off, basic USB commands answered,
  the rest E_UNSUPPORTED detail 1); stock / Atlan / 0x1532..0x1537 / EF2 / EP4 code removed (enc_heal workarounds,
  legacy geometry, 0x0E = W-2, spliced bit, variant arms); Average = mean of N records on both paths (planner shared,
  GET_STATUS 46/47); 0x53 bits 3/4 written 0, divider bound 0xFFFFF (UI max 399,999); mcp protocol 1.25, simulator one
  bitstream with --fpga-version. Coordinator: gcc10 image 772,096 B sha256 7f87eb635eaacc0091bae7d604c84abf70c7a820
  18e0c5fcfbdc7baf0dc9faba pinned as backups/fw-0.2.103_7f87eb63.bin (not loaded: scope off); host tests green,
  check_vectors OK, mcp ruff clean and 501 tests; guide placeholders filled; contract protocol minors renumbered
  1.26..1.28. Bench tomorrow: the notice against a real mismatch (0x153A rollback image or the stock word), the Average
  passes, the soak repeat. Open for C8: scope_info / capture caveats from Profile.mismatch_caveat(), scope_status
  ui_averaging_records for the MCU state, mcp/README --variant table, "on 0x1534 / 0x1536" mentions in mcp-tools.md and
  the measurement guide. Open small: "ADC readout fault near code 128" message still in the firmware (calibration.md row
  removed); the 0x0E = 0 force (two lines in pc_hal_force).
- Coordinator 03:36-04:1x: the account's usage limit cut off fw-one and fpga-c2 (both terminated by the API, work saved in
  their worktrees); resumed after the 04:10 reset with the remaining steps (fw-one: screenshots.py kwarg, merge main, tests,
  gcc10 hash, push; fpga-c2: merge main with C1 and C3, the shared two-line change, re-simulate, push).
- fpga-c3 (done 03:3x, merged 03:34 as f629cec): C3 counter with min/max (0x61, 0x62, read 0x35 = 16 bytes, 17 strobes per
  read); tb_counter 115 checks both seeds; full run.sh 16 steps ALL PASS on the merged C1+C3 source both seeds; TD +347 LUT
  alone, merged C1+C3 6079 LUT (70 %) with 8 setup errors (-0.763 ns) on the sequencer's q_pass adder: placement spread, C4
  re-rolls or pipelines it. Firmware note: a 0x0D/0x53 write mid-gate adds or drops one edge; rewrite 0x61 after the settle
  wait or skip the first result after a rate change. Tool note: long WSL jobs need setsid nohup (a 22-min PowerShell
  background task was killed).
- fpga-c1 (done 2026-10-21 00:xx, merged 01:07 as da4dc6e): C1 trigger types + hold-off; tb_trigtypes 1293 checks on two
  seeds, full run.sh ALL PASS on 12345 and 4242; TD 5721 LUT (+731 over 0x153B) / 4.739 ns met after four timing re-rolls;
  deviations: 0x1B = 0 releases a running hold-off at once, the qualifier is held in reset until the first swc_adv edge after
  a capture reset (type registers written in the 0x01 window apply at every rate). Notes for C4: keep the 0x1B decoder entry
  when applying the C0 cruft list; expect to re-roll timing after merging C2/C3. Qualifier cost about 300 LUT per instance
  (the pattern trigger engine's estimate in the contract rises from 175 to about 300).
- fpga-c1 / fpga-c2 / fpga-c3 (Opus, dispatched 22:21-22:22, branches of the same names): 0x153C items C1 (trigger types
  pulse width / Nth edge / timeout, 0x1B hold-off, 0x37, the fire gate; owns the capture and EXT blocks and the shared
  decim_trig_sample lines), C2 (peak detect as a one-register switch, 0x56 bit 1, 0x29; owns decim_chan and the decimator
  control; sends its two-line shared change via the coordinator), C3 (counter + min/max, 0x61/0x62/0x35 16 bytes; new block
  after seq_lat). Each rebased from gen8-features-wip 9de82ca onto 0x153B, testbenches + run.sh steps 14-16, TD numbers; the
  version byte, contract file and dist entry are C4's (integration, after the three merge). Coordinator decision on contract
  question 1: 0x1B's meaning change accepted (one bitstream per firmware).
- fpga-spec (Opus, dispatched 22:01, done 22:20, merged 22:21 as 4b29d01): docs/plans/fpga-0x153c.md, 608 lines. Generations
  0x153C (triggers, hold-off, peak detect, counter; protocol 1.25), 0x153D (logger with the sample ring as the FIFO, pattern
  generator with marker as trigger source, DDS sweep; 1.26), 0x153E (both-core trigger, runt, window, width range; 1.27).
  Budget correction: 0x153B uses 4990 of 8640 LUTs and no block RAM is free (the roadmap's 7,000 / 200 kbit was stale);
  0x153C ~72 %, 0x153D ~81 %, 0x153E ~87 %. Logger ceiling is the MCU + USB: 1 ch 8-bit 0.58-1.0 MS/s, 1 ch 16-bit or 2 ch
  8-bit 290-510 kS/s, 2 ch 16-bit 145-260 kS/s; proposed contract 500 / 250 / 125 kS/s. Owner questions for the morning in
  section 12 (logger contract rates now or after the bench; pattern marker as internal trigger vs a trigger-out wire mod;
  DDS sweep in or out; build 0x153E at 87 % or stop at 0x153D; C9 timing).

- fpga-spec (Opus, dispatched 22:01): docs/plans/fpga-0x153c.md, the design and register contract for the roadmap epics
  the owner chose at 22:00 (advanced triggers, peak detect, streaming logger, pulse pattern generator with trigger output,
  hardware measurements, software on new data; no deeper memory; testers later): generations, register map with ranges per
  feature, caps bits, resource budget, the logger's sustainable rate from the bus and USB arithmetic, simulation and bench
  plan, parallel implementation briefs. 22:05 addendum: UI-agnostic; UI candidates as a list for the owner's decision.
- fw-one (Opus, dispatched 22:02): the two owner decisions in firmware: one built-for bitstream constant (0x153B), the
  mismatch banner with Diagnostics/Settings/USB drive reachable, compat code removed (stock, Atlan, enc_heal workarounds,
  legacy geometry, version-word branches, EF2/EP4 arms, simulator variants), one averaging behaviour (true N-record mean on
  the MCU path too). 22:03: first-run.md and screens.md left to docs-one.
- docs-one (Opus, dispatched 22:04): the owner's 22:02 decision "drop the SD card alone path": README, front page, install,
  prepare, runbook, first-run (banner placeholders), screens, calibration, release notes, announcement, BITSTREAMS.md rule,
  install skill, roadmap status paragraph with the decisions and chosen epics.
- mcp-soft (Opus, dispatched 22:06): host-side tools on captured data, no firmware: scope_decode (UART/SPI/I2C), scope_mask_test,
  scope_export_csv, scope_dvm; simulator scenarios; docs and skill line.

- readme-gen rounds 2 and 3 (21:41 / 21:52 briefs, done 21:48 / 21:55, merged 21:51 as ac4c6a5; README 181 lines, 21/22 links,
  docs/screenshots/scope-deep-memory.png pending from the bench): the visitor review and the dry run folded in, the owner's
  extra USPs (filters by name, Average mode, Bode plot, overview bar, calibration and capture checks, hands-free flashing),
  What's next from the roadmap, the fnirsi-install skill, runbook Windows fixes with the tool's real output lines; the
  owner's "MCP connection" wording re-applied to the pitch (bac990c).
- Coordinator 21:26-21:57: the 30-minute soak passed (0 errors in 899 status polls, 15 health reads, 15 captures; bench
  entry); the deep-memory screenshot taken (6e3513a); README and front page point the backup step at guide/prepare.md.
- hw-prep (Opus, dispatched 21:42, done 21:56, merged 21:57 as 42bf65b): docs/guide/prepare.md (photos, card reader attach for
  WSL, Windows hash commands, power during flashing = battery on and USB unplugged as benched), hardware-prep.md as the
  engineering reference (maintainer paths and lead gates gone, measured pinout the expected column, 5.3 replaced), README-
  spiflash and install.md consistent with the tool, release.yml footer names the install guide. Originally: a user-facing "Prepare and back up" page from hardware-prep.md section 3 without the
  maintainer's paths or "the lead's go" (dry-run findings: wrong expected pinout column, stale 5.3 procedure, scope power
  during flashing answered three ways, no usbipd recipe for a card reader, no Windows hash command, README-spiflash test
  status stale); hardware-prep.md demoted to engineering; install.md's partition sentence matches the tool; release.yml
  footer points at the install guide.
- docs-clean (Opus, dispatched 21:22, done 21:41, merged 21:42 as a075dbf): no plans deleted (every candidate is cited by
  section from code comments; docs/plans/README.md names the live papers and the design record); task-queue 954 -> 465
  lines; protocol.md at 1.24 with row 59 "not used"; hardware-prep status from the bench log; BITSTREAMS 0x1538/0x1539
  withdrawn; roadmap status paragraph; bench-log intro; programmer FAQ; photos placed; forum members' countries and the
  maintainer's paths removed; no secrets found.
- visitor-review and dryrun-agent (Opus, dispatched 21:30, done 21:32 / 21:38, read-only; their findings drive readme-gen
  round 2 and hw-prep): a Reddit visitor's review of README, install guide,
  runbook and screenshots; a context-free agent asked "guide me through hacking my scope" in the checkout, reporting the path
  it took, the questions it would ask, the steps and commands, the safety gates, the Windows gaps and contradictions.
- readme-gen (Opus, dispatched 21:21, done 21:28, merged 21:29 as 28fde67; README 133 lines, 17/17 links resolve): README rewritten for the hobbyist visitor (owner 21:0x: USPs first, install step 1 =
  "point your AI agent at this repository", manual path second, development content out, credits without the model name);
  docs/development.md takes the development material; docs/guide/agent-runbook.md for the visitor's agent (questions to ask,
  safety gates, exact commands, recovery); root AGENTS.md split into a visitor's-agent top section, development, and "The
  maintainer's bench"; docs/index.md aligned. Owns README.md, docs/index.md, AGENTS.md, mkdocs.yml, the two new pages.
- docs-clean (Opus, dispatched 21:22): finished design docs leave docs/plans (git rm after a citation check; docs/plans/README.md
  points at git history), task-queue Dispatched log collapsed before 2026-10-20, wording pass (owner -> maintainer, coordinator
  -> bench session, COM10 / D:\src placeholders outside the bench log), stale status headers (hardware-prep, mcp-tools,
  protocol rows, BITSTREAMS.md 0x1538/0x1539, roadmap status paragraph), bench-log intro, "Do I need an SPI programmer?" FAQ,
  research-notes personal-detail check, secrets and personal-path scan.
- Coordinator 21:11: 30-minute soak started (soak.py 1800, status every 2 s, capture every 10 s, spectrum every 30 s);
  CLAUDE.md removed 21:19 (owner 21:18: Claude Code reads AGENTS.md when no CLAUDE.md exists), references repointed.

- rel-gen2 (Opus, dispatched 20:59, done 21:15, merged 21:16 as 6f792a6): release notes 0.3.0 filled (screenshots as raw
  URLs pinned to 876700a, 0x153B recommended, the sector-16384 card rule, tested-on, known issues), changelog-0.3.0-draft.md
  (the condensed section for the release commit, 105 hashes checked), announcement.md final draft with the owner's open
  decisions on top, release-checklist ticks with evidence, install/calibration guides name 0x153B. README left untouched
  (the owner asked 21:0x for a hobbyist-first rewrite instead). Findings: CHANGELOG 0.2.46-0.2.50 out of order, 0.2.53/0.2.57
  missing, protocol numbers contradict neighbours in 0.2.64/0.2.68/0.2.73, provisional markers left on 0.2.64-0.2.69 and
  protocol.md rows 56, 61-63; BITSTREAMS.md 0x1538/0x1539 rows say unbenched while the manifest says withdrawn; roadmap stale
  at 0.2.80/0x153A; ui_diag.c:473 tells 0x1532-0x1536 users "fixed in 0x1538" (withdrawn). Its list of finished-history
  docs feeds the docs cleanup effort.

- fpga-gen13 (Fable, dispatched 20:00, done 20:30, merged 20:34: the records had no signal, the probes were off after the wizard; tb_timeout case B models the host capture; bode_cells prints NO TONE; 0x153B benched good 20:40 with the probes on and set recommended 20:42; checks 3 and 6 passed 20:49 and the 3-of-50 observation did not reproduce; open: the firmware corner with an explicit auto_timeout on 0x153B+ it queued). Originally: why every record on 0x153B is spliced on the unit (triggered and untriggered, the
  ring frozen after the capture, the seam fixed in the stored record) while tb_timeout and the 13-step run pass: the
  MCU-side read geometry (0x14, record start, the readout's latency words) vs fpga-gen11's capture-logic change; evidence
  in backups/bench-153b/. 0x153B's dist status to "withdrawn (bench 2026-10-20 19:57)" until fixed; the unit keeps 0x153B
  (owner 20:02: no rollback, roll forward when fixed; the scope is not needed for about two weeks).

- fw-diag (Opus, dispatched 18:15, done 18:37, merged as fw 0.2.100 and loaded 18:43, Status page check pending; section 66; open: counter 49 counts deliberate restarts too, a deliberate restart after a crash overwrites the stored record): the Restarts tile per the owner's verdict meanings
  (OK = no crashes, deliberate restarts not mentioned; Check = an earlier run crashed; Fault = a crash in this run; no
  exception jargon on the Status page), the noise rows with the open-input limits (3.0 full / 1.0 gated, labelled "inputs
  open", one constant with the wizard's pass criterion, the "provisional" note gone, a better advice line), the guide texts.

- fpga-gen12 (Fable, done 19:27, merged 19:28; the four residual windows were testbench artefacts: ext_pending gating, the 2.5 ns reset-release window, stale read-register bits in the phase-4 polls, and a pre-0x153B CIC integrator offset from the 0x53 bits 3/4 runs; full 13-step run ALL PASS on seeds 12345 and 4242, zaklad.v and the images unchanged, flash-session checklist in fpga/al3/AGENTS.md "0x153B flash-ready, fpga-gen12"). Originally: make 0x153B flash-ready: the two residual tb_fir / tb_seg mismatch windows (artefact
  or real), the full 13-step run on two seeds, a rebuild only if zaklad.v changes, the handoff "0x153B flash-ready" with the
  flash-session checklist. fpga-gen11 retired at 522k tokens (merged ac1f2ab).

- fpga-gen11 (Fable, reported 15:35, branch fpga-gen11 pushed, merge after its last testbench run): **cause of the spliced
  time-out records found.** 0x1532..0x153A latch 0x14 = pretrigger on an untriggered time-out stop (record = ring words
  0..total-1), contiguous only when the stop lands in the first ring pass (Atlan's 0x0E = total); our auto mode forces 0x1A = 0
  after many ring passes, so the record is spliced at the write pointer whenever it is inside 0..total-1 (28..42 % of the sweep's
  records, 18..37 % of untriggered Scope traces at fast time bases; all 15 bad records of cells_1235.npz fit a one-ring phase
  step). 0x153B built (dist/al3-0x153B.bin fdc3c21b.., padded 1391978c..; TD 0 errors, 4.583 ns): 0x14 latched to the newest
  total words on an untriggered stop, the time-out ends only untriggered captures, 0x18 is the record's own, 0x1A synchronised;
  tb_timeout 75 checks 0 errors (0x153A source fails 30). Not flashed: owner session needed (pin, probe, double read = the
  pinned 0x153A padded image 77672b5a.., write, retrain the stages, bench README section 11 checks 1-6).
- fw-floor (Opus, dispatched 16:25, done 16:50, merged as fw 0.2.99, benched 16:59: grey tail from 128 kHz with the note; section 65; open: Auto on a coarser range gives grey points without a note): ch2_small points grey on the device and in the mcp
  plot, the plain-words note with the action ("CH2 too small above 540 kHz to measure: set Stop below it or press AUTO SET.
  Less pickup: short ground lead on CH2."), the mcp caveat, the cards ignore grey points.
- fw-arm / fw-arm2 (done 16:10 / 16:24, merged as fw 0.2.97 / 0.2.98, section 64 + addendum; the agent retired at 399k
  tokens): host captures and the RC cells clean on 0x153A (0 seams in 100 + 60 untriggered captures, every auto record
  leak < -65 dB); 0.2.97's engine regression (half the records read as time-outs, retakes) fixed in 0.2.98: ten sweeps 25
  captures, 0 time-outs, 0 retakes. Originally: fw-arm (Opus, dispatched 15:36, fw 0.2.97 provisional, section 64): the firmware side of fpga-gen11's section 11: on
  <= 0x153A arm FPGA auto with 0x0E = 8192k + total - 2 and triggered := 0x18 && 0x14 != pretrigger instead of the forced
  stop; on 0x153B+ the force is safe; every arm path (UI auto, host ACQ, segments, bh_arm); test_fpga_bus; a "may be spliced"
  flag where the force remains.

- fw-yield3 (Opus, dispatched 15:28): regression from fw 0.2.94's "read-only requests inside the modal loops": with
  Diagnostics open on 0.2.95, the host's SET_CHANNEL / SET_TIMEBASE / SET_GEN / BODE_CTRL (tools/screenshots.py --generator,
  15:25) did not close Diagnostics; the Spectrum waterfall, the ACQ chip, the cards and later the Frequency response plot were
  drawn into the Diagnostics frame (docs/screenshots/scope.png, scope-mcp.png, frequency-response-sweep.png of commit 6f0a273
  show it; to be retaken). Wanted: state-changing requests unwind the modal as in 0.2.78 and the view redraws before READ_SCREEN
  answers; no view drawing while a modal owns the display; tests per modal loop. Provisional fw 0.2.96, section 63.

- fw-eng6 (the fw-eng5 agent continued, dispatched 14:17): CH2 range **Auto / Constant** (owner 14:16, bode-ui.md section 10):
  Constant = the CH2 popover's V/div, never changed by a sweep; a popover change by hand while the mode shows sets Constant
  (coordinator's addition, owner to confirm); AUTO SET writes its chosen range into the popover when it picks Constant; the
  warning text names the popover. Also READ_SCREEN must not unwind menus (docs-guide 14:15: since 0.2.78 every main-loop host
  command closes the open menu first, so screenshots of Settings, the generator menu, the file viewers, First start and the
  wizard come out as the screen after the menu closed): a read-only command (READ_SCREEN, GET_*) is answered from inside the
  modal loop without leaving it. Separate agent fw-shot if fw-eng6 declines it.
  **Result (branch fw-eng6, fw 0.2.94, merged 14:37, pinned 10d3fb2a and loaded 14:38, host sweeps on the RC 0 unplanned / 2 planned, owner walk-through 14:57 all five steps confirmed on 0.2.95 (the CH2 menu tap -> Constant stays); the fw-eng5/6 agent retired at 408k tokens, section 62 is its handoff; firmware/AGENTS.md section 62):** both items built.
  Constant: the CH2 menu's display V/div is the one source; the engine writes no other CH2 range (pre-scan included);
  AUTO SET writes its Constant range into CH2; leaving the mode keeps it; the warnings name the CH2 menu; the flat note
  drops ": span kept". The popover hook is commit 93a0c0f alone (revertable). The flat path does run the rule (host
  test: a thru gives Constant on 0.5 V/div); the 14:19 Auto is not explained from the code, so BODE_CTRL bytes 50..57
  now carry AUTO SET's CH2 verdict, reason, points and spread for the bench. READ_SCREEN and the read-only GET_* are
  answered at the modal loops' yield check with the screen open (ce78bc9, protocol row 63 note).

- docs-guide / docs-articles (Opus, dispatched 14:04, done 14:15 / 14:16, merged 14:18, `mkdocs build --strict` green; stale lines they found in ui-guidelines.md, bode-ui.md section 5, firmware/README.md fixed at the merge; the fpga/al3/AGENTS.md header still says 0x153A "Not flashed": fpga-gen11 to fix with its handoff; screenshots of modal screens need the READ_SCREEN change given to fw-eng6): the user guide text per screen on fw 0.2.93 (docs/guide/*.md, the
  "to be written" notes gone, screenshot placeholders kept, new states named for tools/screenshots.py) plus the release-notes
  placeholders (0x153A entry, tested-on list, known issues); and the six engineering articles of docs/articles/index.md from
  research/notes, the FPGA README / handoffs, the bench log and the plans. Screenshots on the unit: coordinator with
  tools/screenshots.py once the text names the states.

- fw-ov3 / fw-eng5 (done 13:41 / 13:50, merged 13:46 / 13:52 as fw 0.2.92 / 0.2.93, handoffs firmware/AGENTS.md section 49
  addendum / section 61): the Frequency response track overlays magnitude (cyan, drawn last) and phase (pink) in Overlaid
  (`slider_ov_draw.c`); AUTO SET chooses CH2 Hold when the levels inside its span spread under 20 dB, else Auto; planned CH2
  switches in BODE_CTRL bytes 48..49 (50 B), the header counts the unplanned ones. Benched 13:57 on the RC: five sweeps 100 Hz..
  1 MHz / 25 points each 0 unplanned, 2 planned, 25 captures, 0 time-outs, 0 retakes, 0 flagged; AUTO SET (owner) left CH2 Auto;
  the track shows both curves, cyan over pink, dim outside the window (screenshot 13:58). fw-eng5 committed ten files with CRLF
  endings (whole-file conflicts; normalised at the merge). Open: the Hold case needs both tips on the generator (owner), section
  61's owner question (apply the Hold rule when RUN starts on a dragged span).

- fw-eng3 / fw-eng4 (done 13:12 / 13:25, merged as fw 0.2.90 / 0.2.91, handoffs firmware/AGENTS.md sections 59 / 60): the post-pre-scan
  check ranges both ways and drops a coarse-CH1 plan; a step up needs two clipping records; the trigger level at CH1's mid-level;
  BODE_CTRL answer 48 bytes with captures / time-outs / leak retakes / other retakes. Benched 13:28: ten sweeps on the RC with 0
  time-outs, 0 retakes, 0 flagged points, switches 2 then 0, 10 kHz within 0.01 dB of the host. Open: the time-out record splice
  itself (fpga-gen11), the Scope's auto-mode traces.

- fw-eng2 (done 12:38, about 390k tokens in total with fw-eng, merged 12:42 as fw 0.2.89, handoff firmware/AGENTS.md section 58):
  the engine arms on CH1 rising at 128 (restored on leaving), spliced records (CH1 leak > -40 dB) retaken, planned clips retaken
  before stepping up, below-floor points step down in one sweep; firmware/tools/bode_cells.py. Benched 12:42: plan settles by
  sweep 3, zero switches after, four clean sweeps of seven; retries 5..15 per sweep remain (the spliced time-out record).
- fpga-gen11 (Fable, dispatched 12:42): why a time-out-started record on 0x153A is spliced from two captures (phase jump at a
  random sample; cells_1235.npz), with the readout geometry (record start from the trigger address, 0x153A's extra word of ADC
  latency, the auto time-out path 0x0E / 0x18) in FPGA and firmware; also the Scope's auto-mode traces.

- fw-decfix (done 12:22, about 210k tokens, merged 12:25 as fw 0.2.88, handoff firmware/AGENTS.md section 57): the last 0x0D /
  0x53 / 0x54 / 0x56 values are tracked in fpga_bus.c, `pc_hal_divider_now()` feeds pc_decim_next / pc_arm_geometry / GET_DECIM /
  GET_CAPTURE_INFO / READ_TRACE, 0x54 written at every arm with the filter on, 0x54 / 0x55 traced (event 22) and counted (link
  stats 65), `bh_arm` rewrites 0x0D when the FPGA holds another divider; tb_raw only affects GET_TIMEBASE; dead code removed.
  Bench: section 57 (the deterministic reproduction clean, GET_DECIM = the FPGA's divider, 0x54 in the trace at every arm,
  twenty device sweeps with the pre-scan ranging CH1 right).

- fw-eng (done 12:28, about 300k tokens, merged 12:21 as fw 0.2.87, handoff firmware/AGENTS.md section 56): per-point range plan
  (ranges, offsets, correction kept with the table; a sweep at the same span / points / Settle / CH2 mode skips the pre-scan and
  starts each point there, switching only on a clip or a drop below 30 % of the next range's full scale; plans from a clipped,
  signal-less or coarse-CH1 sweep are thrown away); flags named after the host's constants (none differed) plus the host's flat
  / no-signal rules; a suspect record retaken once; the waits after a range change now add up as the host's (100 ms). Not done:
  the view shows nothing about the plan; the estimate still counts the pre-scan on reused sweeps.

- fpga-gen10 (Fable, done 12:05, about 305k tokens, merged 12:07; handoff fpga/al3/AGENTS.md "First-record saturation"): the
  0x153A decimator cannot produce a 0xFFFF record with the registers read back; the firmware's host ARM computes N and the shift
  from the UI time base after `acq_view_fit` snapped it without writing 0x0D, so the FPGA keeps the raw divider 4095 while 0x54
  gets the shift for N 400 / 200 (S 9 / 7) and a full-scale input saturates every word; heals on a Filter-off UI conversion or
  a new raw time base. Deterministic host repro in the handoff. Not yet explained by that path: the Bode pre-scan case (bh_arm
  writes its own divider) and the 11:15 round-0 capture: the write trace must log 0x54 / 0x55 to settle them. tb_firstrec.v is
  run.sh step 12 (38 checks). Prescription for the firmware: pc_decim_next / pc_arm_geometry / GET_DECIM take the divider the
  FPGA holds (pc_hal_divider_now), pc_dec_apply always writes 0x54, the trace counts 0x54 / 0x55, test_fpga_bus cases.

- fw-fr4 (done 11:55, about 120k tokens, merged 11:56 as fw 0.2.86): AUTO SET = overview, passes, one single sweep at the found
  settings, STOP (view bit 1 clear throughout; button red STOP / "Auto..." while it runs, chip RUN, then green RUN / chip STOP);
  "Stop sweep" and the Sweep card count the sweep in progress from the engine (the table's completion flags were reused on a
  same-span repeat, hence "50 / 50"). Bench: section 55 addendum.

- fw-acq2 (done 11:45, about 290k tokens, merged 11:42 as fw 0.2.85, pinned backups/fw-0.2.85_7b224ade.bin, not yet loaded): memory
  chips redrawn on every Filter change (acq_view_mem_chips), Spectrum "Capture" card (memory depth / fs, on by default after RBW
  and Time; settings word 217), the zoom-in limit from the real capture time (narrowest span from 0 Hz: 1 kHz at 3000, 2 kHz at
  6000..9000, 5 kHz at 12000+), the rate re-derived at once when the memory depth changes (the FFT length no longer matters to the
  rate); bench steps at the end of firmware/AGENTS.md sections 40 and 52.

- mcp-gen7 (done 11:38, about 240k tokens, merged 11:40): idle port close after 30 s (FNIRSI_MCP_IDLE_CLOSE_S), Bode flat-record
  retakes and the dead-CH1 guard, reason 8 text, simulator reason 8 / flat-record hook / header rule, docs/mcp-tools.md. The
  session's own MCP server process still runs the old code until the session restarts: the idle close is checked next session.
- fw-fr3 (done 11:40, about 245k tokens, merged 11:40 as fw 0.2.84, handoff firmware/AGENTS.md section 55): Single sweep button
  states on the fold-out (owner 10-19 21:32), the generator setting restored on the pin after a sweep (bode-ui.md 7), CH1-low
  warnings by cause (range, probe roll-off, DUT), AUTO SET fails fast (owner 10-20 11:23): absurd points dropped, reference =
  median of the three lowest valid points, peaks need two neighbours, inconsistent passes end in STOP with one of four texts;
  side effects: a sweep keeps running while a fold-out is open; a Q > 30 tank whose overview point lands on the resonance is
  reported "peak not confirmed: sweep by hand".

Entries dated before 2026-10-20 are one line each (collapsed 2026-10-20; the full reports are in this file's git history and in the handoff sections of firmware/AGENTS.md, mcp/AGENTS.md and fpga/al3/AGENTS.md).

- Batch of 10-19 20:58 (the maintainer: continue off the roadmap): fw-freq2, ux-overview, docs-site, fw-screen, fw-bus and fpga-gen9 dispatched; results below.
- docs-site (Sonnet, 10-19, done 21:05): mkdocs site skeleton with Material, the Pages workflow and the CI docs job, tools/mkdocs_hooks.py, tools/screenshots.py.
- fw-freq2 (Opus, 10-19, done 21:25): fw 0.2.71, Frequency response follow-ups, the Spectrum slider, the grid rule, Trace style Lines/Dots; handoff firmware/AGENTS.md section 47.
- fpga-gen9 (Fable, 10-19, done 10-20 00:25): bitstream 0x153A (ADC capture through IOB registers with selectable stages, training registers) merged; handoff fpga/al3/AGENTS.md "0x153A review".
- fw-cap (done 01:05, about 400k tokens plus a helper agent for the docs / mcp commits, merged 01:09 as fw 0.2.79 / protocol 1.24
  row 64, handoff firmware/AGENTS.md section 54, pinned backups/fw-0.2.79_7188c5cf.bin, not loaded: scope off; the morning's FEL
  image): caps bit 19 CAPTURE_STAGES / features bit 7 on 0x153A..0x153F; training in the wizard's zero step (stages 2..5 per core
  at code 128, 8000 words each, clean = 0x36 count 0, pick 3 or 4 with a clean neighbour, B retried with the reference bit, no
  clean stage = "ADC readout fault near code 128: check the FPGA bitstream"), stored in sector 708 bytes 488..503 with its own
  version (zero / gain block unchanged), written at boot when trained on the running bitstream; relative self-check after each
  full-rate capture (a core more than 8 outliers above its chip partner is flagged; both cores of a chip garbling at once is
  caught only by the wizard); GET_HEALTH 124 bytes layout 2, GET_CAL 364 bytes, event type 23; EXT offsets 12 / 14 legacy
  samples on 0x153A; wizard zero at 128 (0.2.67 workaround removed). Not testable from firmware: the reference-bit test (needs
  raw 0x63 writes from a host script). Was: (Opus, branch fw-cap, dispatched 00:35): firmware for 0x153A (detection, EXT offsets, training
  with 0x63 / 0x64 / 0x36 stored with the calibration, 0x36 in GET_HEALTH and Diagnostics, 0x53 bits 3/4) and the wizard zero
  point back at code 128.
- fw-yield (Opus, 10-19, done 23:50): fw 0.2.78 / protocol 1.23 row 63, host requests close the modal screens instead of E_BUSY; handoff section 53.
- fw-spzoom (Opus, 10-19, done 23:30): fw 0.2.77, the Spectrum zoom ladder (span drives the sample rate); handoff section 52.
- fw-zoom (Opus, 10-19, done 22:35): fw 0.2.76, Scope tap-to-jump at touch-down and the zoom ladder; handoff section 50.
- fw-spec (Opus, 10-19, done 22:31): fw 0.2.75, the Waterfall and Density legends removed; section 51.
- fw-over2 (Opus, 10-19, done 22:17): fw 0.2.74, the slider window box geometry (decision 7) in all three modes; section 49 addendum.
- fw-over (Opus, 10-19, done 21:50): fw 0.2.73, the slider-track overview and tap-to-jump in Scope, Spectrum and Frequency response; handoff section 49.
- fw-bus (Opus, 10-19, done 21:40): fw 0.2.72 / protocol 1.22 row 62, the FPGA bus contract fixes (dummy strobe, EXT shift, arm order, encode check by data); handoff section 48.
- fw-screen (Opus, 10-19, done 21:10): fw 0.2.70, the READ_SCREEN handler; bench T9 passed 21:11.
- ux-overview (Opus, 10-19, done 21:12): docs/plans/slider-overview.md and the regenerated boards.
- fpga-gen8 (Fable, 10-19, done 20:57, retired at 834k tokens): 0x1539 delivered and flashed; the 0x153A capture-window design started; found that TD 4.6.8 ignores Verilog initial values; features on branch gen8-features-wip.
- Swarm of 10-19 13:40 (the maintainer: ship the whole roadmap before the announcement): fw-readers, fw-ui2, fw-touch1, fw-ui3, fw-seg, fpga-gen6 and docs-gen3 dispatched; results below.
- mcp-gen5 (Opus, 10-19, done 15:40, retired): protocol 1.17 host side, Bode follow-ups (wiring A default, ch2_overload, AC coupling option); handoff mcp/AGENTS.md.
- rel-gen1 (Opus, 10-19, done 17:35, retired): bitstreams in fpga/al3/dist with manifest.json, BITSTREAMS.md and tools/bitstreams.py; release.yml, release notes layout, README rewrite, release-checklist.md.
- fw-freq (Opus, 10-19, done 19:45, retired): fw 0.2.69, the Frequency response mode UI (chart, cursors, AUTO SET, Single sweep, CSV, MEASURES); handoff section 45.
- ux-bode (Opus, 10-19, done 19:55, retired): docs/plans/bode-ui.md, the Frequency response mode design.
- mcp-gen6 (Opus, 10-19, done 19:05, retired): protocols 1.19-1.21 host side, scope_bode_sweep where=device, fnirsi1013d.report and scope_report; handoff mcp/AGENTS.md.
- fw-cal5 (Opus, 10-19, done 18:45, retired): fw 0.2.67, off-centre zero and CAL_PROBE (protocol 1.21 row 61). fw-polish2 (done 18:50, retired): fw 0.2.68, UI polish batch 2; handoff section 44.
- fw-bode (Opus, 10-19, done 18:20, retired): fw 0.2.66 / protocol 1.20 row 60, the Bode plot on the device; handoff section 42.
- Third wave 10-19 18:25: mcp-gen6, fpga-gen8, fw-polish2 and fw-cal5 dispatched; results above.
- Second swarm landed by 10-19 18:00: fw-enc fw 0.2.62 (encode check on every arm), fw-polish fw 0.2.63 (UI polish batch 1), fw-acq fw 0.2.64 (ACQ coupling, touch batch 2), fw-meas fw 0.2.65 (Measurements v2); handoffs sections 38-41.
- Second swarm 10-19 17:15: fw-enc, fw-polish, fw-acq, fw-meas, fw-bode and rel-gen1 dispatched; results above.
- fpga-gen7 (Fable, 10-19, done 17:30, retired): 0x1538 (stateless encode, 0x33 self-check); root causes of the 0x1537 and 0x1535 encode faults.
- fw-cal4 (Opus, 10-19, done 16:45, retired): fw 0.2.60 (wizard failure line) and 0.2.61 / protocol 1.18 (encode check-and-retry, FPGA write trace); handoff section 37.
- fpga-gen6 (Fable, 10-19, done 15:20, retired): the determinism faults of 0x1532-0x1536 found and fixed in 0x1537; tb_trigpos, tb_seqcontract, the FIR edge study.
- fw-ui4 (Opus, 10-19, done 14:42, retired): fw 0.2.59, Diagnostics pages, the four-screen wizard, the first-run screen; handoff section 34.
- fw-seg (Opus, 10-19, done 14:25, retired): fw 0.2.58 / protocol 1.17 row 56, segmented capture and hardware averaging on 0x1536; handoff section 35.
- mcp-gen4 (Opus, 10-19, done 14:25, retired): the Bode host tool (H1-H3), protocols 1.15 and 1.16 host side; handoff mcp/AGENTS.md.
- fw-ui2 (Opus, 10-19, done 14:19, retired): fw 0.2.56, UX makeover step 2 (CTRL pages with fold-outs, cards, chips); handoff section 33.
- fw-ui3 (Opus, 10-19, done 14:07, retired): fw 0.2.55, UX makeover step 3 (Settings fold-out trim, Settings screen, delete dialog); handoff section 31.
- fw-touch1 (Opus, 10-19, done 13:54, retired): fw 0.2.54, touch fix batch 1; handoff section 32.
- docs-gen3 (Sonnet, 10-19, done 13:54, retired): protocol tidy, README timings, roadmap.md, the announcement draft, release-readiness.md.
- fw-readers (Opus, 10-19, done 13:48, retired): fw 0.2.52 / protocol 1.16 row 55, the diagnostics readers (GET_HEALTH, SD_INFO identity, SAVE_REPORT); handoff section 30.
- fw-ui1 (Opus, 10-19, done 13:35, retired): fw 0.2.51, UX makeover step 1 (menu rows, USB drive screen, cards); handoff section 29.
- fw-touch-review (Opus, 10-19, done 13:28): docs/plans/touch-review.md, 20 touch defects with fix batches.
- fw-cal3 (Opus, 10-19, done 12:15, retired): fw 0.2.50 / protocol 1.15 row 54, the calibration wizard, the A-B tracker and the class probe; handoff section 28.
- fw-usb2 (Opus, 10-19, done 11:20, retired): fw 0.2.49 / protocol 1.14, USB suspend watch, inferred resume, soft reconnect; handoff section 27.
- fpga-gen5 (Fable, 10-19, done 03:50, retired): 0x1536 segmented capture and hardware averaging built, simulated and documented; not flashed.
- docs-gen1 (Sonnet, 10-19, done 01:50): README, the agent guide and skill, protocol, releasing and hardware-prep updates, AGENTS.md.
- docs-gen2 (Sonnet, 10-19, done 02:00): CHANGELOG backfill 0.2.8-0.2.44.
- fw-gen7 (Opus, 10-19, done 01:40, retired): the dead-code purge and reproducible images, fw 0.2.47 / 0.2.48; handoff section 25.
- fw-mmu (Opus, 10-19, done 01:10, retired): fw 0.2.48, MMU and D-cache; handoff section 26.
- bode-design (Opus, 10-19, done 01:05): docs/plans/bode-design.md.
- fw-cal2 (Opus, 10-18, retired): calibration v3 (per-core, per-class gain), fw 0.2.45 / protocol 1.12 row 51; handoff section 23.
- fw-phosphor (Opus, 10-18, retired): the digital phosphor view, fw 0.2.46 / protocol 1.13 row 52; handoff section 24.
- mcp-gen3 (Opus, 10-18/19, retired 01:20): GET_CAL v2 and protocols 1.11-1.13 host side; handoff mcp/AGENTS.md.
- fw-gen6 (Opus, 10-18, retired): fw 0.2.44, 0x1535 FIR support, spectrum strip repaint, CIC2 default, USB suspend rework (protocol 1.11); handoff section 22.
- fw-cal (Opus, 10-18, retired): fw 0.2.43 / protocol 1.10, calibration v2; handoff section 21.
- fw-gen5 (Opus, 10-18, retired): fw 0.2.26-0.2.42, the spectrum analyser and the auto trigger mode; handoff section 20.
- fpga-gen4 (Fable, 10-18, done 23:40, retired): 0x1535 (compensation FIR) built, pinned and documented; the 100 kHz residual explained as the DDS generator's own spur.
