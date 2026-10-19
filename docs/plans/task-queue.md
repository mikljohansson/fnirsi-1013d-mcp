# Task queue

Open items discovered on the bench or requested by the owner, not yet dispatched to an agent. The coordinator dispatches
from here in batches (one agent generation gets a coherent set), and moves items to "Dispatched" with the agent name.
Dates are 2026 unless stated. Bench facts behind each item: `docs/hardware-status.md`.

## Firmware

- **ACQ: sample rate vs time/div coupling** (owner 10-19 13:44, 1 kHz cal signal: after changing the sample rate in the ACQ menu
  the old time/div stays selected although drawn grey; the trace is cut off, zeros show past the record end, the slider scrolls
  before the trace, the shape changes). Cause: Atlan design, statemachine.c handle_acquisition_menu_touch sets the FPGA rate without
  touching timeperdiv, the time/div tap in the menu sets only the display scale, viable_time_per_div[29][35] (variables.c:430) only
  colours the boxes; only plot tap-zoom couples the two via time_per_div_sample_rate. Fix after UI step 2 (fw-ui2 edits the ACQ
  menu now): snap timeperdiv to the nearest viable value when the rate changes and show the snap, make non-viable boxes
  unavailable, clamp the record slider to the record, derive viability from the real record length (memory depth) instead of the
  table, apply the same rule to SET_TIMEBASE / the protocol path, and check the resampling and zero-fill code for the exact
  cut-off and scroll behaviour.
- [x] **Release 0.3.0 readiness fixes** (rel-gen1 10-19 17:35; remaining: 0x1538 entry, screenshots, tested-on list, known issues in docs/release-notes/0.3.0.md) (was: docs/plans/release-readiness.md, 12 findings by docs-gen3): attach our FPGA bitstreams
  (0x1533-0x1536, our own build of GPL sources) to the GitHub release with sha256 and the flash procedure, make the mcp wheel work
  outside a checkout (data/log dirs, .mcp.json example without the Windows venv path and COM10), write release notes separate from
  the CHANGELOG dev diary, fix docs/releasing.md step 1, dry-run release.yml on a scratch tag or fork, refresh README.md (Status, tool
  count, FPGA state, Getting started gaps, the proposed outline). Dispatch when an mcp/docs agent is free; before tagging.
- **FIR step overshoot pulsing on the scope trace** (owner 10-19 14:05, 1 kHz cal square, any +FIR filter: overshoot "horns" at
  the edges grow and snap back at 3-4 Hz; CIC alone shows a clean square). Known property: fpga/al3/README.md section 4 gives
  6-9 % step overshoot for the CIC + half-band FIR chain (flat to fo/4, sharp cut-off = Gibbs ringing on a non-band-limited edge);
  the pulsing is the edge sliding across the decimated sample grid (DDS 1 kHz vs the decimated rate), inference until simulated.
  fpga-gen6 asked to simulate a sliding-edge square through the 0x1535 chain and quantify. Decided (owner 10-19 14:10): keep CIC2+FIR as
  the default in Spectrum (flat passband, alias rejection) and make CIC2 without FIR the default for the Scope trace, with the Filter
  box per mode; goes into the ACQ coupling batch. Owner also sees the upper spectrum "wag" pass to pass on the same square: fpga-gen6 asked to quantify bin variation vs edge phase (image terms in the transition band, hypothesis).
- **Hardware averaging with the default filter** (fw-seg, owner decision): with CIC2+FIR every slower time base falls back to the
  software average because the smallest memory depth (3000) does not fit the FPGA averaging memory; a 2000-sample depth while
  Average is on would make it work. Decide after the 0x1536 flash session.
- **FPGA bus contract fixes on 0x1532-0x1536** (fpga-gen6 findings via fw-seg, section 35 open items): the A/B core order within a
  word can flip after a sample-rate or filter change (interleave right by chance until bench test T4), the screen readout returns
  data one word late (averaged trace sits 2 samples earlier), writes inside the sampling-reset window can be lost at slow rates on
  the older arm order. Firmware fix batch after fpga-gen6 reports; bench with the owner.
- [x] **CH2 at 0.1 V/div reads nothing when set at the start of a Bode sweep** (resolved 15:20: DC overload of the sensitive ranges by wiring B's -1.2 V reference offset, not firmware; Bode tool to flag it and recommend wiring A, mcp-gen5) (bench 10-19 15:10, docs/hardware-status.md): the
  initial / fixed range path gives 48 uV on a 68 mV signal while 0.2 and 0.5 V/div are right and 0.1 reached by the mid-sweep
  switch is right too. Suspects: the initial SET_CHANNEL sequence for range 0.1 (relay / offset / trace position not applied, or the
  software-zoom boundary), or the host picking the wrong range index. mcp-gen5 to find the sequence difference; coordinator
  reproduces on the device with READ_RAW at 0.1 V/div. Also check the scope screen at 100 mV/div.
- **UI polish batch 1** (owner walk-through of fw 0.2.59, 10-19 15:30-, items as reported; dispatch one Opus agent when the
  walk-through ends): (a) main menu Settings icon = a cogwheel; (b) CTRL Grid: the whole button is the toggle (grey off, green on),
  no slide switch inside; (c) X-Y icon = a tilted figure 8, not an oval; (d) Trace style disabled (grey) in X-Y where it does not
  apply; (e) explain or rename "Pro" trace style (looks like Linear); (f) ACQ menu Memory column: "memory pass .." text collides
  with "Memory" and the first depth chip; (g) Settings fold-out brightness sliders sometimes close the menu mid-drag (touch
  leaving the slider box counts as outside?); (h) Settings screen: drop the "Channel, trigger, time base ..." header hedge and the
  "Asks first. Keeps the calibration ..." hint; Restore dialog body uses the dialog width (no needless line breaks); (i) review
  whether "File message timeout" deserves a fold-out slot (owner question; it is 1.00o5's Notification confirmation = the file-saved
  message waits for a tap or times out); (j) Diagnostics uses the Settings screen's header/tab design; battery indicator drawn
  over the Diagnostics page (not over Settings); (k) Calibration tab: "DC shift trim off" becomes the button's state, drop the
  "Needs a DC supply up to 15V" hedge; (l) wizard: "Step N of 6" moves to the top right of the title line; (m) Waterfall / Density
  show one channel (by design since 0.2.32; say so in the Display fold-out or allow CH1+CH2?). (n) Developer page "Debug overlay" is 1.00o5's per-pass debug box (right middle, flickers, raw numbers): either make it a static, labelled overlay drawn once per pass without clearing, or drop it (owner: "not sure what it is supposed to do"). (o) Zero level instruction names the calibration post too (fw-cal4, 0.2.60). (q) main menu Scope icon: a clean sine instead of the square wave (owner 17:20; sent to fw-polish). (p) RUN/STOP button: when the host (MCP) stops the acquisition the button stays green as in RUN; redraw it on every host-driven run-state change. Owner proposal: make it a two-state button that shows the current state and the action, green "RUN" while running (tap stops), red "STOP" while stopped (tap starts); check the dialog-colour rule (red = destructive) against this, since here red would mean "stopped", and settle the wording in the mockup before coding.
- [x] **FLASH 0x1537** (done 10-19 16:31, verified febc9299; power cycle and bench checks next). Was: REQUIRED before 0.3.0. The encode-phase race
  of 0x1532-0x1536 makes about half of the 200 MS/s captures after a rate change carry one metastable ADC core (bench 10-19 16:30);
  0x1537 fixes it in hardware. Plan: firmware/AGENTS.md section 35 (adapted to 0x1537: backups/al3_seg_0x1537_426a3784_padded.bin
  sha febc9299...), checks fpga/al3/README.md section 7, rollback al3_fir_0x1535 padded 5ade9e75. Community note for the
  announcement: units on the stock or 0x1532-0x1536 bitstreams get the firmware heal (0.2.61) but should flash 0x1537.
- [x] **Full-rate encode check on every arm** (0.2.62 benched on 0x1538 17:50: 0 garbled captures in 1353; refinement queued: a rate write sets the 0x33 sticky flag once and costs a re-arm, 112 of 5798; clear it on 0x0D or skip the first check after a rate write; and the type 22 write trace floods the 128-entry event ring, make it a debug level) (was: (fw-enc dispatched 10-19 16:58 as fw 0.2.62 with fpga-gen7's 0x1538 interface: read 0x33 after every 200 MS/s capture, bit 0 = discard and re-arm, bits 3:2 = 01 order guaranteed B-first, 0xF6 = good; data check on every full-rate arm for 0x1532-0x1537): 0.2.61 checks only after a 0x0D write; the bench shows the bad
  phase is re-rolled at every sampling reset on 0x1537 (40 % of arms). On 0x1538 read the self-check status flag after each arm at
  200 MS/s and re-arm when it is set (no data check needed); on 0x1532-0x1537 run the data check on every full-rate arm instead of
  only after 0x0D. Fix the stored core-order convention (interleave tracker A-B model, 0x53 swap bit) to what 0x1538 guarantees.
  fw-cal4 retired at 400k (handoff section 37); new agent.
- **Documentation site and write-ups** (owner 10-19 17:30): docs/ stays the single source, published with MkDocs Material via a
  GitHub Pages workflow; three shelves: user guide (install, first run, calibration, each screen with a screenshot, generator),
  agent guide (MCP setup, measurement guide, worked examples with plots), engineering articles (calibration story, the ADC
  encode-phase fault, decimation filters and FIR, MMU and FFT, Bode design and bench, UX makeover). Tooling: tools/screenshots.py
  drives the scope through each state over USB (scope_screenshot) and saves named PNGs for reproducible screenshots; plots from
  MCP data saved with their generating command. Order: site skeleton + screenshot tool + user guide before the announcement
  (after the polish batch lands so the screens are final); articles at any pace. Dispatch a docs agent after fw-polish merges.
- **FPGA MCU-readout bit mixing near code 128** (root cause of the wizard failure, bench 18:35; 0x1539 fix by fpga-gen8 first, features after; fw-cal5 workaround: zero measured off-centre; a flash session follows). Was: **Wizard measurement setup fault** (fw-cal5 running, 10-19 18:00): zero step fails on 0x1538 with clean host data; see
  hardware-status 17:55. Deliverable includes a host-triggerable dry run so the coordinator can verify without the owner.
- **Bode mode UI redesign** (owner 10-19 18:50: fw-bode remapped RUN/STOP and AUTO SET to sweep control, which breaks the fixed
  right-bar meanings; a designer (ux-bode, Opus) is producing the Bode mode mockup: action page with the standard slot meanings,
  CTRL page with Display / Sweep / Thru / Grid, cards via MEASURES, docs/plans/bode-ui.md with the control map and a self-review).
  Then a firmware batch implements it; until then the Bode view keeps its interim controls (menu row moves in polish batch 2).
- **Touch fixes batch 1** (after UI step 1 lands; spec = docs/plans/touch-review.md section c1, merged c179c49: pick_left_handle()
  helper with a host test, shared scope_pointer_top(), pick at first touch, no tap on a handle, no trigger-point move during a
  channel drag, triggerchannel == 1 fix, clamp 6..396 in auto-setup and settings load; batch 2 = trace grab by nearest object,
  dead band, record-position slider under the grid draggable (owner 10-19 14:05: dragging the window right scrolls the trace left, the window follows the finger; today the slider has no touch handler, only the trace drag moves the trigger point), tap zones that keep the V/div and time-base tap bands but avoid the slider and cards (owner 10-19: keep the tap bands), after UI step 2; batch 3 = no blocking touch loop, needs an MCP soak). Owner decision
  2026-10-19 13:20 **(owner)**: the channel pointers at the left edge of the grid become drag handles, "handles shown on screen are
  draggable"; the on-trace grab stays as a secondary path with a nearest-enabled-object rule. Owner reproduced: with CH2 dragged to
  the bottom CH1 can be picked up again, but only when the drag starts exactly on the invisible zero line.
- [x] **Spectrum Auto redesign** (owner, 10-18): deterministic probe procedure independent of the current state: probe
  at 50 MSa/s in normal trigger mode (2-3 captures, skip untriggered), measure f0 (time-domain period) and the band top
  f_hi (bins above max(peak - 40 dB, median + 10 dB)); span = max(1.25 f_hi, 20 f0) clamped to 25 MHz; rate = slowest
  with fs/2 >= 1.25 span; FFT length longest the record holds (<= 4096) with RBW <= f0/4; Hann; ref = first scale
  multiple >= peak + 3 dB after a confirmation capture; scale from dynamic range; Average 4; one refinement pass; 1-2 s
  allowed with an "Auto..." state; identical presses give identical settings. README documents the algorithm.
- [x] **Header: drop "Avg n"/"Max"** (owner, 10-18): the strip's Average field shows it. Header = peak marker per channel,
  RBW, filter tag only.
- [x] **Warnings state consequence + action, units MSa/s** (owner, 10-18): "Aliasing possible: set Acquisition > Filter"
  (only while Filter is Off and fs < 200 MSa/s), "CHn overrange: increase V/div", "Stopped: press RUN to fill the view",
  "Waiting for a capture at the new rate"; README table.
- [x] **Spectrum view: no scope-view repaints over the strip** (owner, 10-18): host SET_TIMEBASE / SET_TRIGGER / SET_GEN (and
  menu changes) made the bottom strip "glitch and show something else for a flicker": 1.00o5's setting-change redraw routines paint
  the scope view's bottom row regardless of the display mode. Route every setting redraw through the display-mode-aware draw (skip or
  redirect when display_mode != scope), and audit the top bar/right column the same way.
- [x] **FPGA 0x1535 firmware support** (UNBLOCKED 10-18 evening: bitstream 5040a2ae built, tb passes, latency 25 write clocks verified; output rate stays 200 MHz / N): 0x56 FIR
  enable, 0x29 bits 5/6, S = ... + 1, (N/2)^K maths, latency/settle formulas, Filter box FIR option, SET_DECIM flags
  bit 3, GET_DECIM, caps bit 16, protocol 1.11. Confirm with fpga-gen4 that the output rate stays 200 MHz / N.
- [x] **Default filter = CIC2 now** (owner leaning yes after the 0x1534 bench; CIC + FIR once 0x1535 is benched).
- [x] **USB suspend/resume handling** (closed 10-19: fw-gen6's rework in 0.2.44 + 29/29 clean reopens after 10 min idle overnight, counters 36/37 = 1/0 all night: Windows never suspends the idle device; the old suspend->bus_reset was the reset lead-in) (10-18 22:48, fw 0.2.43 FEL-loaded, PHY fix present): after an idle period with the port closed the
  device logged suspend -> bus_reset -> set_config and the host's first write hit Windows error 22 (device failed before the open); the
  reconnect a second later worked. usb_resumes has been 0 on every build while usb_suspends count: the device never sees or handles the
  resume, so the host resets the port instead. Investigate MUSB suspend/resume (INTRUSB resume enable, POWER.SuspendM/Resume, PHY state
  during suspend, clocks), make resume work, and bench with the soak tool in idle-closed-port mode (open, close, wait 2-5 min, open).
- [x] **MMU + D-cache** fw 0.2.48 (fw-mmu; hook guard fixed 39b98c4 after the purge merge). Benched 10-19 08:10: CP15 0x0005107D, FFT 4096
  10.96 -> 2.72 ms, 1024 2.20 -> 0.21 ms, scope-view pass_ms 30 -> 15, census clean. Still to bench: screen coherence (owner), touch, soak,
  SD save/restore, ENTER_FEL round trips (section 26 steps 4-9).
- [x] **Dead-code purge** (done by fw-gen7 in 0.2.47/0.2.48) (owner dropped the byte-identical rule 10-18): PC_PROTOCOL always on, hidden-option masks
  replaced by removal (FFT switch, generator Wave/Ampl/Offset/Phase, Long memory, hold-off, 1014D remnants), unused files,
  check-reference target and reference downloads removed from the Makefile/CI/docs. Do after the spectrum/calibration
  work lands to avoid conflicts.
- [x] **Calibration v3: per-core, per-class gain** (fw-cal2, fw 0.2.45 merged c20957a; bench pending): design in firmware/AGENTS.md section 21;
  3bfc892 (64 full-rate conversions) is merged into main (5effb7d) but unbuilt, goes out with 0.2.44; block v3 376 bytes; owner re-runs
  base calibration; bench with 0 V, 1.0045 V and the -1 V point per class.
- [x] **Bench: -1 V point on CH1 range 2** (done 10-18 evening, docs/hardware-status.md): one gain per core and class suffices; full-rate
  class A/B 1.033 and 1.1 % below the gated class; gated A/B 1.004 with a 0.5-code INL bow in core A that no gain term removes.
- [x] **Generator menu: tapping the frequency row toggles the waveform** (fw-gen6 e0ee6f3 in 0.2.44: the removed Wave/Analog features read as present on 0x1533+; bench pending) (owner 10-18 evening). The Wave row was removed in the menu
  purge; the touch hit-areas (or the row-to-action table) still map the old row positions. Fix the hit-area table, verify every row of
  the generator, trigger and acquisition menus by tapping each; also check that no removed action is still reachable.
- [ ] **CH2 range-5 zero drift check** (fw-cal hypothesis): CH2 raw at ranges 4/5 vs the stored zeros now; re-calibrate after 10 minutes
  warm-up and re-measure range 6 immediately and 10 minutes later. If it drifts, document a warm-up recommendation (Atlan: 5 min).
- [ ] **Calibration v2 bench** (when fw-cal delivers): shorted inputs, per-core means equal within 0.3 codes after
  correction at full-rate and gated rates, T8-style PWM sweep, Nyquist spur gone in legacy mode, volts at 1.000 V /
  0.010 V match the host model within 1 %; owner reruns base calibration.
- [x] **Reproducible firmware builds** (fw-gen7, fw 0.2.47: identity as data from the commit; .bin and .elf identical
  across directories): the same source with the same GCC 10.3.1 gives different images from main and from a worktree
  (0.2.44: d4c0b716 vs 08f9a5ed, 172,941 differing bytes). Likely __FILE__/path strings; add `-ffile-prefix-map=$(abspath .)=.` (and
  `-fdebug-prefix-map`), check for __DATE__/__TIME__, and make CI's image hash match a local main build. Fold into the dead-code purge.
- [ ] **CH2 reads +1.0..1.5 codes at range 6 with shorted input right after calibration** (10-18 late): channel offset with the cores
  agreeing; see docs/hardware-status.md late evening and the overnight log. Decide after the log: PWM/relay drift at hw range 5 vs probe.
- [~] **Morning bench with the owner** (10-19 morning: calibration v3 run 08:15, 0x1535 flashed 10:40 and benched; the +-1 V and phosphor checks still open; load the latest pinned image, not 0.2.46): (1) back up SD 700-710; base calibration
  with probes + USB unplugged, then the new gain step with both x1 probes on the post ("Gain calibration successful"); GET_CAL version 3,
  gains about full A 0.995 / B 1.028, gated 0.998 / 1.002; (2) +-1.0045 V on CH1 range 2: |A-B| <= 0.15 at 200 MSa/s, codes/V equal
  within 0.2 % across 200 MSa/s / 1 MSa/s / 10 kSa/s / CIC2, volts within 0.5 % of the DMM; fs/2 spur down >= 15 dB; (3) phosphor view
  visual steps (firmware/AGENTS.md section 24 / README "Phosphor view"), pass_ms in and out of the view; (4) 0.2.44 items: spectrum
  strip repaint loops, generator menu rows, Filter box steps + DEFAULT = CIC2; (5) flash FPGA 0x1535 (backup first) and bench CIC2+FIR
  (GET_DECIM bytes 26-27 = 0xE2 0x01 at 10 MSa/s); (6) persist a release candidate to the SD card after a soak.
- [x] **16-bit trigger level ignores corr16 and the class gain** (fw-gen7, fw 0.2.47, 04619ca; bench pending) (fw-cal2 handoff, section 23): about 1 % level error at full
  rate in filtered captures. Small firmware fix; add a host test.
- [ ] **Phosphor fast capture loop** (design study, fw-phosphor section 24): the view draws one record per pass (about 30 wfm/s);
  a capture loop that reads several records per pass would raise the waveform rate. Measure pass_ms in the view first.
- [ ] **Gain-per-range check** (fw-cal2 assumption 1, README 6.3 step 7): gains measured on one range are applied to all six; bench
  CH1 at ranges 1/2/3 with the generator static on the post; if spans disagree beyond 0.3 %, store gains per range (80 bytes free).
- [x] **Bode analyser, host steps H1-H3** (docs/plans/bode-design.md fe5780b; done by mcp-gen4: 1d46cf8, f0940ee, 363f676,
  d3a16bb, simulator-tested, bench pending in "MCP / host"): MCP tool `scope_bode_sweep` host-driven with the existing
  commands (SET_GEN raw 50 MHz / M tones, SET_DECIM CIC3+FIR, coherent READ_RAW layout 2, single-bin DFT), thru-calibration, fixed range
  first, then down-ranging; bench first on an RC low-pass R 1.6 kOhm / C 10 nF (fc 9.95 kHz) with DUT ground on the USB shell.
- [x] **Bode analyser, device steps D1-D5** (fw-bode 0.2.66 merged 18:20; owner 18:45: Bode plot is a main-menu mode row after Spectrum analyser at a 52 px pitch, eight rows, removed from the generator pop-over -> fw-polish2 item 6; the earlier "entry from the generator menu" note is superseded). Was: after the host version proves the numbers: display mode with magnitude/phase panes,
  BODE_CTRL / READ_BODE. Owner decision: entry from the generator menu (the main menu has no eighth row).
- [ ] **docs/protocol.md tidy** (docs-gen1 findings): row 32 says GET_STATUS bytes 42-43 are zero while rows 41/45 define ext_flags and
  display_mode there (add a "superseded" note); row 49 calls CIC+FIR the eventual default while CIC2 is the default since 0.2.44; row 51
  says x1 or x10 probes for the gain step while README 6.3 says x1 (fw-cal2: range 2 for x1, range 5 for x10, so both are supported:
  align the README). Next firmware agent.
- [ ] **FPGA 0x1536 firmware + MCP support** (after fpga-gen5; fpga/al3/README.md section 6, docs/plans/segmented-capture.md, fpga/al3/AGENTS.md):
  commands 0x57-0x5D, reads 0x2B-0x32 via the 0x1F pointer, segment table (64-bit entries: valid, triggered, trigger offset, 40-bit
  5 ns timestamp), averaging with positions P and shift S at readout; firmware capture modes "Segments" and "Average N" with capture-info
  fields, READ_RAW layouts for a segment set and the averaged record; MCP `scope_capture_segments` / `scope_capture_averaged`; bench check 5
  (an N = 1 average equals the memory's record) first after flashing. Next firmware generation + mcp-gen4/5. Firmware: fw-seg, 0.2.58;
  MCP host side: mcp-gen5 4699593 (`scope_capture_segments`, `scope_averaged_capture(hardware=true)`, simulator al3s).
- [ ] **Overnight zero step at 03:48** (docs/hardware-status.md 04:00): after 4.4 h flat, CH2 core A moved -1.7 codes (B unchanged) and CH1
  both cores +0.5 within one 10-minute interval, persisting. Morning: battery/charger state, recalibrate on 0.2.48, re-check the CH2 A/B
  split; if per-core zeros move with temperature or supply, consider a periodic zero re-check (needs an input-independent method) or a
  warm-up note in the calibration procedure.
- [x] **Calibration v4: background interleave-mismatch tracking** (fw-cal3, fw 0.2.50 / protocol 1.14: A - B tracker, full-class class probe, GET_CAL 288 bytes, diagnostics lines; built and host-tested, bench list firmware/README.md 6.5; the one-time gain step stays, design section 6) (10-19 morning, docs/hardware-status.md 08:30): the full-rate core
  offset and gain relationship changes over hours (3.3 % gain mismatch last night, 0 % now; zero split 3.9 vs 5.0 codes within 15 min of
  calibration; the 03:48 overnight step), so v3's one-time full-class gains/zeros go stale while the gated class stays within 0.3 code.
  Standard interleaved-ADC fix: estimate offset mismatch (mean A - mean B) and gain mismatch (std A / std B, or the fs/2 spur power) from
  live records when the signal is band-limited (no energy near fs/2 and enough samples), low-pass the estimates over many records, and apply
  them on top of the stored zeros/gains in the full-rate class only. Design first (fw-cal3): detection of valid records, convergence time,
  interaction with the 16-bit path and the spectrum view, a diagnostics readout of the tracked mismatch, host test with a drifting model.
  Owner decision needed on whether the one-time v3 gain step stays (it still fixes the class gain ratio and the gated class).
- [x] **Calibration wizard (owner approved 10-19)** (fw-cal3, fw 0.2.50: built and host-tested, bench list firmware/README.md 6.4): replace the fold-out calibration submenu and the two separate flows with one modal
  wizard that owns the screen (only Cancel or completion leave it; stray touches do nothing); "Step n of 3" header. Step 1 Zero: "Unplug
  both probes and the USB cable", Start, per-channel progress, result lines with a check mark or a one-line reason. Step 2 Gain, one channel
  at a time: "Plug in the CH1 probe at x1, tip on the calibration post", Measure; the firmware checks the level first ("no signal on CH1:
  tip on the post?") and offers Retry; then CH2. Retry per step without redoing earlier steps; Skip keeps earlier values (flag bit 4).
  Step 3 Summary in plain words per channel (cores matched to x code, gains), date, Done; one success message at the end only. Consider
  detecting USB connected and warning instead of demanding the unplug. Next firmware generation (fw-cal3 together with v4 tracking).
- [~] **USB: device lost after a long idle on 0.2.48** (fw-usb2 10-19: 0.2.49 merged with the suspend watch, inferred resume that acts,
  soft reconnect after a retry storm or 15 min suspended, counters 60-61, event type 20, protocol 1.14; bench plan firmware/AGENTS.md
  section 27; Windows selective suspend on this laptop: AC disabled, DC enabled) (10-19 10:20): port closed from about 08:40 to 10:20, device vanished from the bus
  (no 1209, no re-enumeration), GET_LINK_STATS after replug: suspends (36) = 17, resumes (37) = 0, no restart. The host suspended the idle
  device and it never resumed: RESUME interrupt never fires. Decode the event log (types 2/3 fields of row 50), check MUSB resume
  handling (INTRUSBE resume bit, POWER.Resume, PHY), and the F1C100s USB PHY suspend behaviour; bench with `fnirsi1013d-soak --mode reopen
  --idle 1800`. Overnight 10-minute idles never suspended (36 stayed 1); the long idle did.
- [x] **UI makeover batch** (all four steps merged 10-19 by 14:42 as fw 0.2.51/0.2.55/0.2.56/0.2.59, bench pending) (owner 10-19; plan docs/plans/ux-makeover.md, mockups https://claude.ai/artifact/M3udheReCiHLQmASaY5qZr revision 4,
  reviews docs/plans/diagnostics-review.md and ux-review.md): step 1 main menu rows + USB drive screen; step 2 CTRL pages with fold-outs
  (Display Normal/Phosphor/X-Y with the phosphor parameters, Trace style, Grid, V/div per channel; Spectrum View/Analysis/Axis), strips
  removed, Trace style out of ACQ, AUTO SET as the spectrum Auto, version string and Move fast out of the top bar, warnings top-middle,
  measurement cards in the stock chip scheme (+ "Freg" fix); step 3 Settings fold-out trim + full Settings screen (drag speed, file
  message timeout, RTC on/off, At power-on values, restore defaults without the boot byte); step 4 Diagnostics as a display mode with
  Status / Calibration / Log / Developer pages and the readers of diagnostics-review.md section 4, the four-screen wizard (section 6a),
  Clear keeps the volts factors, Volts calibration kept as a service action, Enter FEL now via ENTER_FEL, first-run screen. Settled 10-19 afternoon
  (plan sections 3, 3a, 6a, 6b): CTRL pages identical in both modes (Display with Normal / X-Y and a Phosphor switch, Trace style or
  Analyse with the channel selector, Scale and Ref level, Grid, V+/CH/V- steppers, 50 % TRIG), strips and spectrum header gone with
  readouts as cards in Atlan's card layout, top bar chips rebuilt, Spectrum row always shown and switching Roll off. Open: look of the
  new full-screen pages (stock palette vs the navy style), short CTRL labels vs wider buttons, main-menu row height when Bode ships.
  Next firmware generations (one step each).
- [ ] **Measurement cards in the stock scheme** (owner 10-19): chip with the channel digit in the channel colour + "Metric: value" in
  white, dark grey rounded card; fix the "Freg" label; scope_functions.c scope_display_channel_measurements. Part of the UI batch
  (docs/plans/ux-makeover.md section 3).
- [ ] **Measurements v2** (ux-review section 6, plan section 3a): per-mode MEASURES lists in spare settings words with default cards
  (a new unit shows none today: scope_reset_config_data sets no cards despite its comment); the cheap on-device set (rise/fall, overshoot,
  top/base, CH1-CH2 delay/phase/gain, cursor-gated statistics, PI decay tau; spectrum H2/H3, THD, SFDR, noise floor, band power, OBW);
  AC RMS beside Vrms; the standard names (Period, +Width, -Width, +Duty, -Duty) and the "Freg" fix. After the UI batch.
- [ ] **README timing fix**: firmware/README.md still quotes the pre-MMU FFT times (4096 points 10.9 ms); the MMU bench measured 2.72 ms
  on 0.2.48 (docs/hardware-status.md 08:10). Update the spectrum section's numbers. Next firmware agent.
- [ ] **"Confirm to delete?" dialog**: its two buttons sit 11 px apart (ux-review); apply the dialog rule (44 px, 16 px apart, primary right).
  UI batch step 1.
- [ ] **Trigger position varies by run at decimated rates** (10-18, +11 us / -1 us at 1 MS/s). FPGA side done (fpga-gen6,
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
  Remaining: bench re-measure on 0x1537 with GET_CAPTURE_INFO and 0x15 writes logged.
- [x] **Digital phosphor** (fw-phosphor, fw 0.2.46 merged 2e73d28; visual bench pending): design in docs/plans/dpos350p-features.md
  section 4; XY mode could get intensity grading from the same map.
- [x] **Bode / frequency response** (owner priority 3): design docs/plans/bode-design.md; host tool done (H1-H3 above); device steps D1-D5 queued.

## MCP / host

- [x] **scope_status docstring** (docs-gen1): names all five display modes (mcp-gen4, with the Bode docs commit).
- [x] **Bode bench session** (10-19 14:35-15:25: RC low-pass and high-pass consistent to 0.14 dB / 1 deg, floor -80 dB, thru +0.3 dB, divider flat: tool accepted; capacitor dispersion is the part; DC overload in wiring B found, wiring A recommended) (was: RC 1k/15n, floor -80 dB, thru +0.3 dB, divider flat -20.5 dB: tool proven; RC capacitor deviation open; 0.1 V/div bug found) (mcp-gen4 hand-off; procedure in mcp/AGENTS.md "Bode bench"): wiring A/B, thru, floor, coherence, the
  1.6 kOhm / 10 nF RC; record the floor, skew and the wiring verdict in docs/hardware-status.md.
- [ ] **Bode follow-ups** (after the bench): CH1 re-ranging when CH1 drops below 20 codes (resonant input nodes), the harmonic fast
  mode (design 1.3), the impedance mode z_series_ohm (design 2.4, coil L / Q / SRF for the detector), an optional PNG plot.
- [ ] **1.17 on the unit, host side** (mcp-gen5, after the owner's 0x1536 flash and firmware/AGENTS.md section 35 checks 7-9): run
  `scope_capture_segments` (10 kHz generator: intervals 100 us +- 10 ns, every segment crossing at the pretrigger point) and
  `scope_averaged_capture(hardware=true)` against host averaging of the same signal (means within 0.1 code, N = 1 equal to READ_RAW);
  then make `hardware` default to auto when the record fits, add it to `scope_decay_fit`, and drop the "not yet verified" caveats.
- [ ] **Version bump to 0.3.0** in mcp/pyproject.toml and firmware PC_FW_VERSION, CHANGELOG section, release notes
  (mcp-gen3 item 4) when the owner gives the go for v0.3.0.
- [x] **Alias residual model check** (fpga-gen4): the 100 kHz residual is the DDS generator's own spur. The DDS clock is 50 MHz, so a
  900 kHz square's edges fall on a 20 ns grid with a 55.56-tick period that repeats every 9 periods (10 us): a real 100 kHz component at
  about -37.5 dBV for 3.1 Vpp that no filter can remove. Model vs bench at N = 200: CIC2 -31.1 vs -30, CIC3 -39.0 vs -36 dBV. For alias
  tests use f = 50 MHz / M with M even (e.g. inc = round(2^32 / 56) = 892.857 kHz, aliasing to 107.1 kHz); fpga-gen4 fixed README check 4.

## FPGA

- [x] **Long free-running testbench** for the decimation filter: fpga-gen4's 0x1535 work (tb/run.sh, seeds 12345/1/77, 13 FIR
  mutations caught; fpga/al3/README.md section 5, commit 292a596).
- [ ] **Open input / EXT bench tests 1-4** from fpga/al3/README.md "Changes" section 2 (needs an external 3.3 V source
  referenced to USB ground).

## Owner bench verdicts outstanding

- [ ] 0.2.40/0.2.41 layout: Roll/Average/Filter boxes and the pass-time label alignment; Max hold behaviour; overrange
  warning. (Auto mode verified by the coordinator on 0.2.41: 6/6 triggered records at 10 MS/s, untriggered flagged truthfully.)

## Dispatched

- Swarm started 10-19 13:40 after step 1 merged (owner: ship the whole roadmap before the announcement, one big splash; front-load
  everything that needs no hardware). Provisional firmware numbers, renumbered at merge: fw-readers 0.2.52 / protocol 1.16,
  fw-ui2 0.2.56 (step 2: CTRL fold-outs, steppers, strips to cards, chips, ACQ Display row removed), fw-touch1 0.2.54 (touch-review
  c1), fw-ui3 0.2.55 (step 3: settings fold-out trim, Settings screen, delete dialog), fw-seg 0.2.56 / protocol 1.17 rows 70+
  (0x1536 firmware support, phosphor fast-capture design note), fpga-gen6 (trigger determinism testbench, 0x1536 contract tb,
  0x1537 if the FPGA is at fault), docs-gen3 (Sonnet: protocol tidy, README timings, roadmap.md, announcement draft,
  release-readiness.md). Queued behind them: fw-ui4 (step 4, needs fw-readers), Measurements v2 (needs step 2), touch batches 2-3,
  mcp 1.16/1.17, Bode device steps (after the owner's Bode bench), 0.3.0 release.
- mcp-gen5 (done 15:40, about 565k tokens, retired; handoff mcp/AGENTS.md): protocol 1.17 host side pushed 2097181 (CI 39258110694), Bode follow-ups pushed 514182f (CI 39260053987, 398 tests):
  wiring A default with the bench numbers, ch2_overload flag + auto range step-up + fixed-range failure text, --ch2-coupling ac,
  offset-range-exhausted caveat, simulator models the pinned front end. Next for mcp: 1.17 on the unit after the 0x1537 flash.
- rel-gen1 (done 17:35, about 265k tokens, retired): merged (bitstreams 0x1533-0x1537 in fpga/al3/dist with manifest.json,
  BITSTREAMS.md and tools/bitstreams.py check in CI; release.yml attaches them, accepts vX.Y.Z-rcN pre-release tags, takes release
  notes from docs/release-notes/X.Y.Z.md (0.3.0 draft with placeholders); releasing.md fixed; README rewritten; release-checklist.md).
  Open: add 0x1538 as recommended when pinned and benched; owner decisions: attach older bitstreams, AI credits wording (F12).
- fw-cal5 (done 18:45, about 490k tokens, retired): 0.2.67 merged (off-centre zero, CAL_PROBE, readout fault named); the
  workaround is insufficient on 0x1538 (hardware-status 18:50): the wizard waits for 0x1539. fw-polish2 (done 18:50, about 400k,
  retired): 0.2.68 merged; walk-through firmware/AGENTS.md section 44. Owner 18:55: the mode row is "Frequency response".
- fw-bode (done 18:20, about 760k tokens, retired): Bode on the device merged 0fbcf02 as fw 0.2.66 / protocol 1.20 row 60
  (bode_dsp.c, bode_sweep.c main-loop engine, bode_view.c panes/markers/cards, generator menu row "Bode plot", CTRL Sweep/Thru/Grid,
  BODE_CTRL 0x0211 / READ_BODE 0x0212 20-byte rows, display mode 5, features bit 6, words 219-220); pinned backups/fw-0.2.66_e2483907.bin;
  walk-through firmware/AGENTS.md section 42 (RC and divider on the bench). Not loaded (the scope holds the CAL_PROBE image).
- Third wave 18:25: mcp-gen6 (protocols 1.19-1.21 host side, scope_bode_sweep where=device, fnirsi1013d.report PNG/HTML +
  scope_report, guide/skill "Presenting results"), fpga-gen8 (0x1539: pulse-width / Nth-edge / timeout triggers, peak detect,
  200 MHz counter), fw-polish2 (batch 2), fw-cal5 (wizard screen-context fault).
- Landed from the second swarm by 18:00: fw-enc 0.2.62 (benched on 0x1538: clean), fw-polish 0.2.63 (items a-q; Pro style and
  the Debug overlay switch removed, RUN/STOP = green RUN / amber Stopped, Waterfall channel row), fw-acq 0.2.64 (viability from
  the record length, time/div snap notice, slider drag, dead band, per-mode filter words 207/212, Peak/Pro buffer overrun fixed),
  fw-meas 0.2.65 (per-mode MEASURES, order, +N more, THD/Noise, counter hook, words 221-223). Pinned backups/fw-0.2.6{2,3,4,5}_*.bin;
  0.2.65 loaded on the scope 17:54. Walk-throughs: firmware/AGENTS.md sections 38-41. Still running: fw-bode, fw-cal5.
- Second swarm started 10-19 17:15 (owner: no deadline, do everything that needs no hardware; advanced features are MCP-first,
  device UI only for trigger types, peak detect, hardware counter). Provisional numbers: fw-enc 0.2.62 / protocol 1.19 (encode
  self-check on 0x1538, data check on every full-rate arm), fw-polish 0.2.63 (UI polish batch 1 a-p), fw-acq 0.2.64 (ACQ coupling,
  per-mode filter default, touch batch 2, spectrum touch), fw-meas 0.2.65 (Measurements v2), fw-bode 0.2.66 / protocol 1.20 rows
  60+ (Bode on the device D1-D5), rel-gen1 (bitstream distribution, releasing.md, README rewrite, release checklist). Waiting for
  0x1538 to merge: fpga-gen8 (trigger types, peak detect, hardware counter: 0x1539), then the pulse pattern generator and the
  streaming logger design; mcp-gen6 (1.19 / 1.20 host side, result presentation helpers) after fw-enc / fw-bode land.
- fpga-gen7 (done 17:30, about 355k tokens, retired): 0x1538 merged (stateless encode, B-first guaranteed, read 0x33 self-check,
  tb_encphase; root cause of 0x1537 = encA setup slack -0.209 ns, of 0x1535 = async enable race); pinned backups/
  al3_seg_0x1538_0c24ecf6.bin + padded 2fc7ea5b..., in fpga/al3/dist as unbenched; bench checks fpga/al3/README.md section 8. Was: 0x1537 leaves ADC chip 2 in a bad encode phase after a write in the scope's arm sequence (bench
  16:33-16:40); reproduce in simulation, fix as 0x1538 with a phase self-check, pin, handoff.
- fw-cal4 (done 16:45, about 400k tokens, retired; handoff section 37): 0.2.60 (failure line, instruction) and 0.2.61 (encode check-and-retry after 0x0D, write trace events, protocol 1.18) merged. Was: running 10-19 15:50-: 0.2.60 merged (failure line with range/rate/core and RMS numbers, instruction names the post;
  pinned backups/fw-0.2.60_27634587.bin, loaded 16:18). Now on the real fault: the wizard's 200 MS/s class reads one garbage core
  per channel (CH1 B, CH2 A, 21-24 codes RMS) while host READ_RAW is clean; FPGA mode/shift state hypothesis; fix as 0.2.61.
- fpga-gen6 (done 15:20, about 555k tokens, retired): FPGA not deterministic on 0x1532-0x1536 (pretrigger/slot-mask write race
  at slow rates, A/B core-order flip on divider writes); both fixed in 0x1537, built deterministically, pinned
  backups/al3_seg_0x1537_426a3784.bin (sha 426a3784...) + padded febc9299..., NOT flashed; tb_trigpos (882 checks, 36 fail on
  0x1536), tb_seqcontract (1.17 byte sequences, transcript tb/contract_0x1537.txt); FIR edge study: overshoot 0.45-6.9 % vs edge
  phase, the 1 kHz DDS drifts 4.03 ns/period = 4.03 Hz beat at 1 MS/s (the owner's 3-4 Hz), spectrum wag is image beating in
  fo/4..fo/2, CIC alone worse than +FIR there. Firmware follow-ups: EXT shift in legacy mode (+10 samples late), keep EXT in
  fpga_swap_trigger_channel, dummy strobe in the two screen readouts (one word late). Flash 0x1537 (supersedes 0x1536) with the
  owner; bench checks fpga/al3/README.md section 7.
- fw-ui4 (done 14:42, about 620k tokens, retired): merged and renumbered to fw 0.2.59 (Diagnostics as a main-loop page: Status,
  Calibration, Log, Developer; four-screen wizard; first-run screen; old diagnostics and start-up screens removed; fw-ui3 interim
  rows removed; ui_diag.c, ui_dlg.c, ui_diag_view.c, test_ui_diag); images pinned backups/fw-0.2.59_4ba89469.bin and
  fw-0.2.59-nommu_24151796.bin; walk-through firmware/AGENTS.md section 34. Not done: Check on post, Undo last calibration (sector
  711). UI makeover steps 1-4 are all on main now; the whole bench walk-through (sections 29, 31-35) is one owner session.
- fw-seg (done 14:25, about 575k tokens, retired): merged 4905934 as fw 0.2.58 / protocol 1.17 (row 56, renumbered from the
  provisional 70; handoff section 35): caps bit 17 SEQ, SET_SEQ / GET_SEQ / READ_SEQ, READ_RAW segment mode, GET_STATUS 48 B with
  a features word, GET_DECIM 36, GET_CAPTURE_INFO 96, UI Average as a hardware run where the record fits; images pinned
  backups/fw-0.2.58_2f9c36f9.bin and fw-0.2.58-nommu_887f1a04.bin; flash session plan in section 35; design note
  docs/plans/phosphor-fast-capture.md. Not loaded.
- mcp-gen4 (done 14:25, about 570k tokens, retired): Bode host tool, protocol 1.15 and 1.16 host side; handoff mcp/AGENTS.md 1cb17e0
  (open item 0 = the mcp release-readiness items). mcp-gen5 dispatched 14:27 on those items; 1.17 after fw-seg merges.
- fw-ui2 (done 14:19, about 510k tokens, retired): merged 68fc8a0 and renumbered to fw 0.2.56 (CTRL page with Display / Trace style or
  Analyse / Grid / steppers / 50 % TRIG, fold-outs, strips and header gone, spectrum cards, chips, warnings in every variant,
  ACQ acquisition only, settings word 210 bit 3 + word 211); images pinned backups/fw-0.2.56_35b2c2f6.bin and
  fw-0.2.56-nommu_a406ed19.bin; walk-through firmware/AGENTS.md section 33; not loaded. fw-seg told to use 0.2.58.
- fw-ui3 (done 14:07, about 370k tokens, retired): merged as fw 0.2.55 (settings fold-out trimmed, Settings screen with At power-on
  (three values, written on Back), Restore defaults keeps calibration/boot/CDC, shared delete dialog, old Other settings menu removed,
  interim Baseline calibration row + "Calibration (moves to Diagnostics)" group for fw-ui4 to delete); images pinned
  backups/fw-0.2.55_89c1e947.bin and fw-0.2.55-nommu_1dc9b165.bin; walk-through firmware/AGENTS.md section 31; not loaded.
- mcp-gen4: protocol 1.15 host side pushed 556ed72 (CI 39248624412 green, 367 tests: GET_CAL 288 tracker fields, volts model with
  d/e/cm, scope_calibration_info tracking section, simulator v4 drift); 1.16 host side pushed 0967b67 + 33cd1b5 (CI 39250138551 green, 376 tests: scope_health, scope_sd_info identify, provenance in calibration info, scope_save_report; health stays read-only, clearing the restart record is the Diagnostics page's job). Next: mcp items of the release readiness batch, then 1.17 when fw-seg merges.
- fw-touch1 (done 13:54, about 200k tokens, retired): merged as fw 0.2.54 (touch_pick.c handles column, first-touch pick, nearest
  switched-on channel, shared clamped channel+trigger move, EXT fix, clamps 6..396; test_touch_pick 538 checks); images pinned
  backups/fw-0.2.54_05a126f7.bin and fw-0.2.54-nommu_572784ed.bin; walk-through firmware/AGENTS.md section 32; not loaded.
  Numbers: a branch that lands below main's current number is renumbered at merge (fw-ui2 0.2.53 -> 0.2.56).
- docs-gen3 (done 13:54, about 230k tokens, retired): merged 6f1ea23 (protocol tidy, README MMU timings, roadmap.md rewrite,
  announcement.md draft, release-readiness.md with 12 findings -> queued item).
- fw-readers (done 13:48, about 465k tokens, retired): merged 72eacb3 as fw 0.2.52 / protocol 1.16 (row 55: GET_HEALTH 0x000A,
  SD_INFO image identity, GET_CAL 356 B with provenance, SAVE_REPORT 0x0504, power-on count in settings words 224..255, sector 709
  written once per boot); images pinned backups/fw-0.2.52_cd8bf6fc.bin and fw-0.2.52-nommu_372267eb.bin; host tests green;
  bench checks in firmware/AGENTS.md section 30; not loaded. fw-ui4 dispatched on it; mcp-gen4 told to add 1.16 after 1.15.
- fw-ui1 (done 13:35, about 365k tokens, retired): step 1 merged 9cc599c as fw 0.2.51, images pinned backups/fw-0.2.51_acf88fee.bin
  and fw-0.2.51-nommu_e011d597.bin, host tests 1513 checks OK; bench walk-through in firmware/AGENTS.md section 29, not yet loaded.
  Was: UI makeover step 1 (menu rows, USB drive screen, version string out, Phosphor/X-Y
  Display box in ACQ as the interim, strips without Scope/Auto, AUTO SET = spectrum Auto, cards in the final scheme with standard
  names, phosphor warnings top-middle); fw 0.2.51.
- fw-readers (worktree, Opus, started 10-19 13:15): the diagnostics data readers (USB link state, card image identity, calibration
  provenance, power-on count and minutes, persisted restart record, save state, CP15 word, SAVE_REPORT); fw 0.2.52, protocol 1.16.
- fw-touch-review (done 13:28, about 220k tokens, kept for questions): docs/plans/touch-review.md merged c179c49 (20 defects, fix batches); was an adversarial code review of the Scope and Spectrum touch interactions
  (channel drag hit zones, tap-to-zoom, cursors, trigger pointers, spectrum gestures, GT911 read path) after the owner found
  CH1 unmovable while CH2 drags and no drag on the left-edge CH handles; output docs/plans/touch-review.md, fixes as a later batch.
- fw-usb2 (done 11:20, about 355k tokens, retired): 0.2.49 merged; bench pending (section 27).
- fw-cal3 (done 12:15, about 275k tokens, retired): 0.2.50 merged cdc7e2f (wizard, A-B tracker with bow-aware fit, class probe, menu
  reorder); renumbered to protocol 1.15 / row 54 in the merge; bench pending (README 6.4 / 6.5).
- fpga-gen5 (done 03:50, retired): 0x1536 segmented capture + hardware averaging built, simulated, documented (bcccd92..66232a6); pinned
  backups/al3_seg_0x1536_4eaf6811.bin (+ padded 66866c84); not flashed; needs firmware support first.
- docs-gen1 (done 01:50): README cb47f8f, guide/skill 4ed5f53, protocol 8d750bb, releasing/hardware-prep aa84507, AGENTS.md 9004569.
- docs-gen2 (done 02:00): CHANGELOG backfill 0.2.8-0.2.44 committed 6154672 (37 sections; protocol 1.8 has no row/commit, noted there).
- fw-gen7 (done 01:40, about 400k tokens, retired): purge batch merged into main (a86cd6c); the merged main build is numbered 0.2.48.
- fw-mmu (done 01:10, about 330k tokens, retired): fw 0.2.48 MMU + D-cache merged; images pinned backups/fw-0.2.48_*.bin (MMU and nommu).
- bode-design (done 01:05): docs/plans/bode-design.md committed fe5780b.
- fw-cal2 (retired, about 400k tokens): calibration v3 delivered and merged (c20957a, fw 0.2.45 / 1.12 / row 51); handoff section 23.
- fw-phosphor (retired, about 430k tokens): phosphor view delivered and merged (2e73d28, fw 0.2.46 / 1.13 / row 52); handoff section 24.
- mcp-gen4 (idle since 02:00 after H1-H3; CI 39178908638 green): holds the Bode follow-ups and item 4 (0.3.0 bump, on hold).
- mcp-gen3 (retired 01:20 after GET_CAL v2, 1.11, 1.12, 1.13): handoff mcp/AGENTS.md d4d891c.
- fw-gen6 (worktree, branch fw-gen6, about 350k tokens, retired): fw 0.2.44 merged into main (463931d, image d4c0b716): 0x1535 FIR
  support, spectrum strip repaint, CIC2 default, USB suspend/resume rework (protocol 1.11, rows 49-50), generator menu hit-area fix.
  Bench by the coordinator in progress.
- fw-cal (retired at 515k tokens): fw 0.2.43 merged (b27c1ee); handoff firmware/AGENTS.md section 21 merged (5effb7d). Remaining
  README 6.2 bench steps (CH2 drift check, -1 V point, PWM sweep, volts check) are queued above.

- fw-gen5 (retired after 0.2.42): Auto redesign, header, warnings delivered in fw 0.2.42; owner verdict pending.

- fpga-gen4 (retired at about 290k tokens, 10-18 23:40): FPGA 0x1535 built, pinned, host-tested, documented (292a596); not flashed.
  The 100 kHz residual question is answered (DDS spur, see above).
