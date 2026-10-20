# Task queue

Open items discovered on the bench or requested by the maintainer, not yet dispatched to an agent. The bench session dispatches
from here in batches (one agent generation gets a coherent set), and moves items to "Dispatched" with the agent name.
Dates are 2026 unless stated. Bench facts behind each item: `docs/hardware-status.md`.

## Firmware

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

- [ ] **0x153B+: the deadline force must write 0x0E = 0 too** (fpga-gen13, 10-20 20:27, from the code, not benched): on
  `PC_STOP_NEWEST` the host poll, `pc_ui_auto_service` and FORCE call `pc_hal_force` (0x1A = 0 alone); the FPGA stops only
  when `~trigger_mode_s && time_base_timeout` (`time_base_cnt > 0x0E`, zaklad.v). With the default 0x0E = total samples and a
  deadline of at least two records that is true. With an explicit SET_TRIGGER `auto_timeout` the firmware writes 0x0E = that
  *sample* count as a *word* count (`pc_auto_timeout` -> `fpga_arm_reset`) and forces at about the time of that many samples
  (`pc_host_auto_ms` = deadline(cfg) / 2): in the legacy layout (two samples per word) the force comes at half the FPGA's
  count, nothing stops, the record ends on the FPGA's own time-out up to twice later (0x14 = W - total + pre, contiguous),
  and a trigger in between is reported untriggered (`host_forced`). Fix: `pc_hal_force_stop` (0x0E = 0, then 0x1A = 0) on
  0x153B+ too; change 4 of README section 11 makes the 0x0E write before the 0x14 read harmless there. test_fpga_bus: the
  0x153B model with 0x0E = 2 x the deadline in words, force at the deadline, done within 2 write clocks. The same race
  existed on every bitstream before fw 0.2.97 (fpga/al3/AGENTS.md "0x153B 'spliced' on the unit", hypothesis (c)).

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

- fpga-c1 / fpga-c2 / fpga-c3 (Opus, dispatched 22:24-22:26, branches of the same names): 0x153C items C1 (trigger types
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
