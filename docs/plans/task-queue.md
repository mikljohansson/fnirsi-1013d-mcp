# Task queue

Open items discovered on the bench or requested by the owner, not yet dispatched to an agent. The coordinator dispatches
from here in batches (one agent generation gets a coherent set), and moves items to "Dispatched" with the agent name.
Dates are 2026 unless stated. Bench facts behind each item: `docs/hardware-status.md`.

## Firmware

- [x] **AUTO SET must fail fast with a helpful message, never loop** (merged 10-20 11:42, bench pending) (owner 10-20 11:23 **(owner)**, after the RC AUTO SET on
  0.2.83 ran 41 sweeps with the relays clicking: "present the user with a failure message that helps them somehow, e.g. the
  connections to the circuit or the circuit itself"; sent to fw-fr3 as part of item (d)): when the overview or a sharp-tank
  pass is inconsistent (peak unconfirmed, reference level drifting between passes, the pass budget exhausted, an absurd point
  at the reference, CH2 ranging bouncing more than six times in a pass) AUTO SET ends after at most the overview plus its passes
  with a note naming the likely cause (wiring: CH1 tip on the DUT input, CH2 tip on the output; the DUT), in STOP with the
  settings kept, never in RUN.

- **First record after a configuration change is garbage on 0x153A** (bench 10-20 10:55-11:16): a decimated capture right after
  the engine's (or the host's) rate / filter / range change can be a whole record of one saturated 16-bit value; the Bode
  engine's pre-scan is always such a capture, so the first sweep after entering the mode ranges CH1 coarse (2.5 V/div,
  ch1_low) and only the next sweep is right. Not seen on 0x1539. Two parts: (a) FPGA / firmware investigation (fpga-gen10 on
  Fable with the firmware's write order from fpga_bus.c: the decimator's state after a mode or shift write on 0x153A, the extra
  ADC latency of 0x153A vs the sampling reset; reproduce with the engine's exact sequence from a PC_DEBUG image); (b) engine
  robustness: the pre-scan steps CH1 up again when its fundamental is under 20 codes and the record is not clipped, and a
  flat / saturated pre-scan record is retaken once (named in the event log), fail fast after that. Also mcp: BODE_REASONS[8]
  text still says "the pre-scan clipped at 5 V/div" (fw-bode2 open point).

- [x] **ACQ: sample rate vs time/div coupling** (fw 0.2.64 viability from the record length, snap notice, grey boxes unavailable; 0.2.69 ten samples per division; 0.2.76 zoom ladder; benched 10-19 22:45 on 0.2.76: owner 'zoom ladder seems to work great'; the 0.2.68 snap-path fix: bench to confirm) (owner 10-19 13:44, 1 kHz cal signal: after changing the sample rate in the ACQ menu
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
- [x] **FIR step overshoot pulsing on the scope trace** (fw 0.2.64: Scope filter defaults to CIC2 without the FIR, Spectrum to CIC2+FIR, per-mode word 212; fpga-gen6 quantified it: overshoot 0.45-6.9 % vs edge phase, 4.03 Hz beat of the 1 kHz DDS at 1 MS/s, spectrum wag = image beating, see Dispatched; bench pending) (owner 10-19 14:05, 1 kHz cal square, any +FIR filter: overshoot "horns" at
  the edges grow and snap back at 3-4 Hz; CIC alone shows a clean square). Known property: fpga/al3/README.md section 4 gives
  6-9 % step overshoot for the CIC + half-band FIR chain (flat to fo/4, sharp cut-off = Gibbs ringing on a non-band-limited edge);
  the pulsing is the edge sliding across the decimated sample grid (DDS 1 kHz vs the decimated rate), inference until simulated.
  fpga-gen6 asked to simulate a sliding-edge square through the 0x1535 chain and quantify. Decided (owner 10-19 14:10): keep CIC2+FIR as
  the default in Spectrum (flat passband, alias rejection) and make CIC2 without FIR the default for the Scope trace, with the Filter
  box per mode; goes into the ACQ coupling batch. Owner also sees the upper spectrum "wag" pass to pass on the same square: fpga-gen6 asked to quantify bin variation vs edge phase (image terms in the transition band, hypothesis).
- **Hardware averaging with the default filter** (fw-seg, owner decision): with CIC2+FIR every slower time base falls back to the
  software average because the smallest memory depth (3000) does not fit the FPGA averaging memory; a 2000-sample depth while
  Average is on would make it work. Decide after the 0x1536 flash session.
  (check: CHANGELOG 0.2.58 / 0.2.64 and hardware-status 10-19 / 10-20: no decision on a 2000-sample depth while Average is on is recorded.)
- [x] **FPGA bus contract fixes on 0x1532-0x1536** (fw-bus, branch fw-bus, fw 0.2.72 provisional / protocol 1.22, firmware/AGENTS.md section 48; bench list there, not benched: dummy strobe in every readout, arm order on every bitstream, legacy EXT shift, EXT kept on channel toggles, core order from 0x33) (was: fpga-gen6 findings via fw-seg, section 35 open items): the A/B core order within a
  word can flip after a sample-rate or filter change (interleave right by chance until bench test T4), the screen readout returns
  data one word late (averaged trace sits 2 samples earlier), writes inside the sampling-reset window can be lost at slow rates on
  the older arm order. Firmware fix batch after fpga-gen6 reports; bench with the owner.
- [x] **CH2 at 0.1 V/div reads nothing when set at the start of a Bode sweep** (resolved 15:20: DC overload of the sensitive ranges by wiring B's -1.2 V reference offset, not firmware; Bode tool to flag it and recommend wiring A, mcp-gen5) (bench 10-19 15:10, docs/hardware-status.md): the
  initial / fixed range path gives 48 uV on a 68 mV signal while 0.2 and 0.5 V/div are right and 0.1 reached by the mid-sweep
  switch is right too. Suspects: the initial SET_CHANNEL sequence for range 0.1 (relay / offset / trace position not applied, or the
  software-zoom boundary), or the host picking the wrong range index. mcp-gen5 to find the sequence difference; coordinator
  reproduces on the device with READ_RAW at 0.1 V/div. Also check the scope screen at 100 mV/div.
- [x] **UI polish batch 1** (fw 0.2.63 items a-n and q, o in 0.2.60, p redone in 0.2.68 as the two-state RUN/STOP button with the spanner Settings icon and the shorter sine; bench pending: hardware-status records the 0.2.59 walk-through only, images 0.2.63 / 0.2.68 loaded since) (owner walk-through of fw 0.2.59, 10-19 15:30-, items as reported; dispatch one Opus agent when the
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
- [x] **Full-rate encode check on every arm** (0.2.62 benched on 0x1538 17:50: 0 garbled captures in 1353; refinement queued: a rate write sets the 0x33 sticky flag once and costs a re-arm, 112 of 5798; clear it on 0x0D or skip the first check after a rate write; and the type 22 write trace floods the 128-entry event ring, make it a debug level; done on branch fw-bus: the bitstream clears bit 0 only at 0x01 = 1, so a capture with a rate / filter write after its reset is judged by its data; type 22 off by default, GET_EVENT_LOG flags switch it, counters GET_LINK_STATS 64-67) (was: (fw-enc dispatched 10-19 16:58 as fw 0.2.62 with fpga-gen7's 0x1538 interface: read 0x33 after every 200 MS/s capture, bit 0 = discard and re-arm, bits 3:2 = 01 order guaranteed B-first, 0xF6 = good; data check on every full-rate arm for 0x1532-0x1537): 0.2.61 checks only after a 0x0D write; the bench shows the bad
  phase is re-rolled at every sampling reset on 0x1537 (40 % of arms). On 0x1538 read the self-check status flag after each arm at
  200 MS/s and re-arm when it is set (no data check needed); on 0x1532-0x1537 run the data check on every full-rate arm instead of
  only after 0x0D. Fix the stored core-order convention (interleave tracker A-B model, 0x53 swap bit) to what 0x1538 guarantees.
  fw-cal4 retired at 400k (handoff section 37); new agent.
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
- [x] **FPGA ADC data capture window (was: MCU-readout bit mixing)** (0x153A merged 10-20 00:30, flash pending) (0x1539 flashed 20:06: readout fixed, capture window still wrong on the A cores; 0x153A = programmable capture phase + training, fpga-gen8) (root cause of the wizard failure, bench 18:35; 0x1539 fix by fpga-gen8 first, features after; fw-cal5 workaround: zero measured off-centre; a flash session follows). Was: **Wizard measurement setup fault** (fw-cal5 running, 10-19 18:00): zero step fails on 0x1538 with clean host data; see
  hardware-status 17:55. Deliverable includes a host-triggerable dry run so the coordinator can verify without the owner.
- [x] **Frequency response mode UI** (0.2.69, bench pending; follow-ups above) (design docs/plans/bode-ui.md + artifact DfydC8HJgnemUw3EHenTGP; owner 19:15: variant 2 one pane two y axes without legend, "calibration post" in the instruction, frequency axis Log/Linear in the Display fold-out, two flagged cursors C1/C2 with the bottom-left readout box and delta row kept (19:25), Display variants Overlaid / Magnitude / Phase; questions 1-5 per the coordinator's recommendations; fw-freq dispatched as 0.2.69 together with the 10 samples/div viability minimum). Was: **Bode mode UI redesign** (owner 10-19 18:50: fw-bode remapped RUN/STOP and AUTO SET to sweep control, which breaks the fixed
  right-bar meanings; a designer (ux-bode, Opus) is producing the Bode mode mockup: action page with the standard slot meanings,
  CTRL page with Display / Sweep / Thru / Grid, cards via MEASURES, docs/plans/bode-ui.md with the control map and a self-review).
  Then a firmware batch implements it; until then the Bode view keeps its interim controls (menu row moves in polish batch 2).
- [x] **Time/div viability: minimum samples per division** (0.2.69: 10 per division, 200 MS/s down to 50 ns/div) (owner 10-19 19:00, bench: 100 kS/s with 10 us/div = one sample per
  division; host captures show the 1 kHz edge spread over the CIC2 step response (93, 108, 145, 155, 161) with the edge samples
  drifting 108 -> 103 -> 99 between captures as the DDS 1 kHz (999.996 Hz, 4 ns per period) creeps across the 10 us sample grid,
  so the interpolated trace wobbles at about 1 Hz; no trigger jitter: the edge index is stable). fw-acq's rule allows one sample
  per division; raise the minimum to about 10 samples per division (10 px per sample) so such zooms are unavailable and the snap
  picks a viable time/div; one constant in tb_view.c plus the host test. Next firmware batch.
- [x] **Bottom scroll bar in Spectrum and Frequency response** (Frequency response slider row fw 0.2.69, Spectrum slider fw 0.2.71; bench pending) (owner 10-19 19:40): the Scope's record slider construction under the
  plot of both modes; Spectrum: the span as a window over 0..fs/2, drag pans the centre; Frequency response: Start..Stop as a window
  over 100 Hz..10 MHz on the log axis, drag pans both and re-sweeps. Designer (ux-bode) updates the boards and bode-ui.md /
  ux-makeover.md first; firmware batch after fw-freq lands (spectrum.c and bode_view.c).
  - [x] **Slider-track overview, firmware batch after the design is approved** (fw 0.2.73 overview in Scope, Spectrum and Frequency response with tap-to-jump, 0.2.74 box geometry; pass_ms 18 measured 10-19 22:07; owner walk-through of firmware/AGENTS.md section 49 pending) (owner 10-19 20:00; design
    `docs/plans/slider-overview.md` by ux-overview, boards in docs/mockups): a mini overview in the twelve rows y 456..467
    of the slider track, dimmed outside the window, full colour inside, the window and touch unchanged. Scope: per-record
    min / max envelope of the whole record (2880 B, about 0.33 ms at 16382 samples, estimate; `scope_code_to_y` factored
    out of `scope_get_y_sample`; none in roll mode, the legacy branch or X-Y). Spectrum: `spec_columns` over the whole
    axis the FFT already computes, no extra FFT (lands with the Spectrum slider). Frequency response: a 400-byte copy of
    AUTO SET's overview in `bv_auto_finish`, drawn fitted to the rows, nothing before the first AUTO SET. Shared pure
    `slider_ov.c` with host tests; bench pass_ms on / off. Owner questions in section 10 E first.
  Owner decisions 21:20 (docs/plans/slider-overview.md section 10 F): dimmed outside / full inside; tap-to-jump as a fast follow
  batch right after; simple lifetime rule for the Frequency response overview; Scope overlaid, enabled channels only; the window
  takes the frame's whole inside (18 px, 14 overview rows), not 14 px. The Scope's stock thumbnail becomes this drawing (no second
  one). Dispatch after fw-freq2 merges (same files: bode_view.c, spectrum.c); regenerate the boards to 18 px in the same batch.
- [x] **Frequency response chart follow-ups** (fw 0.2.71: (a) Trace style Lines/Dots, (b) grid rule in both modes with ui-guidelines.md section 3, (c) header and (e) estimate from the settings in effect, (d) readout-fault stop reason 8; benched 10-19 21:28: header and estimate agree, reason 8 not triggered because the pre-scan stayed below 5 V/div; full check needs 0x153A) (owner 10-19 20:50, after the bench of the device sweep on the RC low-pass; batch
  fw-freq2 together with the follow-ups listed under fw-freq): (a) a Trace style button on the mode's CTRL page like the Scope's,
  Lines (default) / Dots, same fold-out construction; today valid points are joined and coloured per axis and flagged points are
  grey loose dots, which is what the owner saw because the 0x1539 capture fault flagged every point (hardware-status 20:51); keep
  the grey for flagged points. (b) Grid lines that mean something: on the log frequency axis vertical lines at the decades (minor
  at 2 and 5 when the span is narrow enough to need them), on the linear axis at the label steps; horizontal lines at the dB and
  degree divisions the labels name; the Spectrum view's grid the same way (vertical at its frequency labels, horizontal at its
  dB/div); state the rule in ui-guidelines.md section 3. (c) BODE_STATUS consistency: with no sweep since boot the header
  reported the engine's defaults (100 Hz..1 MHz / 50 / Normal) while the estimate (245.7 s) was computed for the view's own
  settings; report one set (the settings in effect, cfg_now) in both. (d) Ranging robustness: the engine treats one rail sample
  as a clip and ranged both channels to 5 V/div on a bitstream with the A-core fault; decide with the owner's fail-fast rule in
  mind: either leave it (0x153A removes the cause) or stop the sweep with a "readout fault" reason when the pre-scan clips at
  5 V/div with a fundamental under 20 codes, instead of producing a flagged table. (e) The estimate text of Single sweep must
  use the same settings as (c).
- [x] **Frequency response small items** (merged 10-20 11:42, bench pending) (next bode_view.c batch after fw-over lands, so the two do not collide): (a) owner decision
  10-19 21:32 **(owner)**: a single sweep started from the Sweep fold-out stays on the CTRL page; the Single sweep button changes
  state to show the sweep running (the same colour/word rule as RUN/STOP: it reads what a tap does, e.g. "Stop sweep" in red with
  the progress on its second line) and another tap stops it, so the user changes parameters and triggers again without leaving
  the fold-out; the page-one RUN/STOP keeps showing STOP meanwhile. (b) After a sweep the generator chip shows the user's
  generator although the pin is off (fw-freq2 open item): show "off" or the user's setting as the engine leaves it. (c) The
  warning "CH1 level low: the DUT loads the 1 kOhm source" also appears when the pre-scan ranged CH1 too coarse (0x1539 fault):
  name both causes or pick by range. mcp: the simulator lacks reason 8 and the header-before-first-sweep rule.
- [x] **Slider track geometry revision** (fw 0.2.74, loaded 10-19 22:19; the owner's verdict on the box geometry is not recorded) (owner 10-19 22:11 after seeing 0.2.73, slider-overview.md decision 7): the window is a box
  again (four 1 px borders, 1 px margin), overview full colour inside and dimmed outside; the track's top border line removed (the
  plot pane's outline is the separation); all three modes; boards regenerated. Dispatch fw-over2 at once (small).
- [x] **Scope tap-to-jump and zoom stepping** (fw 0.2.76, bench pending) (owner 10-19 22:20 on 0.2.73): (a) a tap in the Scope's slider
  track outside the window does nothing (Spectrum works): find why the Scope path is not reached (the tap bands / extended drag
  area / the record-position slider handler order) and fix it with a host test. (b) Tap zoom on the plot: the left side (zoom out)
  does nothing where the slower time/div is not viable at the current rate or exceeds the record, while the right side zooms in.
  Owner: the zoom should step the sample rate together with the time/div so the result is always viable and intuitive, like a bench
  scope where time/div drives the rate: zooming out past the record lowers the rate (memory depth kept), zooming in past ten samples
  per division raises it; the ACQ menu's rate chips stay for a manual choice and the snap notice stays. Apply the same rule to the
  time/div chips in the ACQ menu and to SET_TIMEBASE where the host asks for a time/div (docs/protocol.md note). Design the rule
  in one paragraph in docs/ui-guidelines.md first (what changes, what is kept), then implement. Owner 22:22 **(owner)**: the zoom must be one
  monotonic ladder that never gets stuck: from either end (200 MSa/s at the fastest viable time/div; the slowest rate at the slowest
  time/div before roll) the next tap in that direction does nothing and the opposite direction works at once; no cell reachable by
  zooming that cannot be left by zooming; host test walks the ladder both ways from both ends at every starting rate. Owner 22:24: "weird state" = a rate and time/div
  pair that makes no sense together after stepping to an end and back; so the invariant is on the pair: after any tap sequence the
  rate is the ladder's rate for that time/div (highest rate whose record covers ten divisions with at least ten samples per
  division), a hand-picked ACQ rate holds only until the next zoom step; the test asserts the pair after every sequence.
- **AUTO SET in Frequency response fails about 60 % on 0x1539** (owner 22:20): expected from the capture fault (pre-scan clips at
  5 V/div); re-test on 0x153A before any engine change.
  (check: hardware-status 10-20 holds no Frequency response AUTO SET retest on 0x153A.)
- [x] **Spectrum: remove the colour legends** (fw 0.2.75) (owner 10-19 22:28 **(owner)**): the Density legend (blue -> red ->
  white is understood) and the Waterfall legend with its dB marks go (spectrum.c spec_draw_legend, ux-makeover.md "colour legend
  inside the plot's top right"); nobody measures from those views. Boards and ux-makeover.md updated; the warning line keeps its
  place.
- **ACQ menu: memory depths above 6000 do not come back after Filter -> Off** (owner 10-19 22:45 on 0.2.76): with a filter on the
  depths above 6000 are hidden (the 16-bit record halves the memory, correct); switching back to Filter Off leaves them hidden until
  something else redraws. Recompute the memory chips' availability from the filter state on every Filter change and redraw the
  column; host test. Next ACQ-menu batch.
  (check: CHANGELOG 0.2.64-0.2.80 has no fix for the memory chips after Filter -> Off.)
- [x] **Spectrum zoom ladder** (fw 0.2.77, bench pending) (owner 10-19 23:09 **(owner)**: "let's try your recommendation"; fw-spzoom dispatched): span is the
  parameter in 1-2-5 steps around the centre (tap right narrows, tap left widens, drag pans); the sample rate is derived from the
  stop frequency as the slowest rate whose fs/2 covers it (best RBW for free; the (span, rate) pair is a function of the span, so
  reversible like the Scope ladder); FFT length stays the user's (Analysis fold-out, default 2048), so the span always holds
  between a quarter and a half of the bins; a hand-picked ACQ rate holds until the next zoom or pan step; ends: the full 25 MHz
  axis at 50 MSa/s (coordinator 10-19: plain rule, the stop's rate; 200 MSa/s was a slip), and zooming in stops where the capture time N/fs would exceed about 2 s; a pan that moves the stop across a
  rate boundary moves the rate too. Owner observation on 0.2.76: zoom in never changes the rate today, only zoom out does. Rule
  written into ui-guidelines.md first; host test walks the ladder both ways from both ends at every FFT length, asserting the pair.
- [x] **Host commands leave the modal menus** (fw 0.2.78 / protocol 1.23; benched 10-20 10:29 menu case and 10:31 nested Settings case: answered at once, ui_yields counted; the note itself goes in fw-yield2; other section 53 checks pending) (owner 10-19 23:27: "avoid locking up the device from the perspective of the MCP server
  when it's in menus; if the agent sends commands it just exits all modals, since that wouldn't be destructive; just exiting to
  the base screen"; fw-yield dispatched): today every menu, fold-out and dialog is a modal loop and a host command that needs
  the main loop answers E_BUSY (protocol.md LOCAL_BUSY). New rule: such a command sets a yield flag from the ISR-side receiver;
  every modal loop checks it each iteration and leaves by its cancel path (a confirmation = No, a fold-out = closed, a menu =
  closed, the Settings / Diagnostics screens = Back, the file viewers = Exit) down to the base screen, then the main loop
  answers the request; the screen shows a short top-middle note "Host command: menu closed". Exceptions, where the command still
  gets E_BUSY with a reason text: the calibration wizard while a measurement runs (a cancel between steps is fine), a file save
  in progress, USB drive mode (not on the serial port anyway). Commands that answer from the ISR (PING, GET_INFO, GET_STATUS,
  GET_BATTERY, GET_HEALTH, BODE_STATUS) never yield. Design paragraph in ui-guidelines.md and protocol.md section 14 row first,
  host test with the mock UI, mcp client/server: drop the "close it on the scope and retry" advice where the firmware yields
  (keep it for older firmware by version). Built on branch fw-yield (fw 0.2.78 / protocol 1.23,
  firmware/AGENTS.md section 53); bench list there.
- **Spectrum ladder follow-ups** (fw-spzoom handoff, section 52): (a) show the capture time N/fs somewhere (the Time card is the
  waterfall's history span); (b) make the 2 s zoom-in limit use the real capture time memory depth/fs, or cap the memory depth in
  the Spectrum; (c) re-derive the rate when the FFT length or memory depth changes, not only at the next zoom/pan step. With the
  next Spectrum batch.
- [x] **Capture-stage training must probe the band** (fw 0.2.81; benched 10-20 10:48: wizard stores 0x08E3, the 200 MSa/s band sweep clean on every core) (bench 10-20 10:19): see hardware-status 10:19 and 10:48.
- **Spectrum ladder: pans never change the rate** (owner 10-20 10:27 on fw 0.2.80 **(owner)**: "the spectrum should stay the same when
  scrolling, changes to sample rate and time/div should happen when zooming"; fw-spzoom2 dispatched): the rate is derived from
  span and centre only at zoom steps (and by AUTO SET); a pan (plot drag, slider drag, tap-to-jump) keeps the rate and is clamped
  to the current axis 0..fs/2 (the slider track is that axis); to look higher than the axis, zoom out (raises the rate) then zoom
  in. The 16-bin floor ends zoom-in at a high centre as before. Accepted consequence: a pan to a low centre at a wide span keeps
  the coarse resolution until the next zoom step. Rule paragraph in ui-guidelines.md updated; test_spec_zoom: pans never change
  the rate, the clamp, zoom after a pan re-derives.
- **Remove the "Host command: menu closed" note** (owner 10-20 10:31 **(owner)**: "we don't need the note at all"; fw-yield2
  dispatched): the modal screens still unwind on a host command, silently; the note text, its timer, `pc_glue_yield_note` and the
  Spectrum follow-up item go; the yield counters stay.
- [x] **READ_SCREEN handler** (fw 0.2.70, benched: T9 passed 10-19 21:11, 800x480 in 0.22 s) (needed by the docs screenshots, 10-19 20:55): the protocol row 0x0401 exists (docs/protocol.md, cap
  SCREENSHOT bit 6, simulator has it, `Scope.read_screen` reads 40 rows per request) but the firmware handler is NULL
  (pc_cmds.c "BENCH: T9"). Implement it in all builds: rows of the live framebuffer as RGB565, the header per protocol.md, the
  capability bit set; host test against the vectors; bench: `scope_screenshot` on the unit equals the screen (T9).
- [x] **FPGA review on a Fable agent** (fpga-gen9 on Fable: 0x153A merged 10-20 00:30, flashed 10-20 10:03; capture fix confirmed by the stage map 10:08-10:14, the CH1 A training pick is fw-cap2's) (owner 10-19 20:55: "might want to use a Fable agent to review and fix the FPGA code"):
  fpga-gen8 retires at 810k tokens with its 0x153A handoff; fpga-gen9 runs on Fable: review the AL3 design end to end (ADC
  capture path, clocking, constraints) against the bench record, then finish 0x153A.
- [x] **Preserve the mockup sources** (docs/mockups/ committed 19:50): copy the gen_ux generators and the artboard HTML from the coordinator's scratchpad into
  docs/mockups/ (committed) before the session ends; the makeover artifact itself is on a previous login (ux-makeover.md note).
- [x] **Wizard zero point back at code 128** (fw 0.2.79, bench pending) (owner 10-19 20:30, fail fast): revert 0.2.67's two-point off-centre zero, measure at the
  centre PWM so a bitstream with the capture fault fails the wizard loudly; keep the "ADC readout fault near code 128" wording and
  the CAL_PROBE command; host test updated. Do it in the firmware batch that follows 0x153A (so the wizard keeps passing on the
  owner's unit).
- [~] **Touch fixes batch 1** (batch 1 fw 0.2.54, benched 10-19 16:02 handles work; batch 2 fw 0.2.64 trace grab, dead band, draggable slider, tap bands, bench pending) (after UI step 1 lands; spec = docs/plans/touch-review.md section c1, merged c179c49: pick_left_handle()
  helper with a host test, shared scope_pointer_top(), pick at first touch, no tap on a handle, no trigger-point move during a
  channel drag, triggerchannel == 1 fix, clamp 6..396 in auto-setup and settings load; batch 2 = trace grab by nearest object,
  dead band, record-position slider under the grid draggable (owner 10-19 14:05: dragging the window right scrolls the trace left, the window follows the finger; today the slider has no touch handler, only the trace drag moves the trigger point), tap zones that keep the V/div and time-base tap bands but avoid the slider and cards (owner 10-19: keep the tap bands), after UI step 2; batch 3 = no blocking touch loop, needs an MCP soak). Owner decision
  2026-10-19 13:20 **(owner)**: the channel pointers at the left edge of the grid become drag handles, "handles shown on screen are
  draggable"; the on-trace grab stays as a secondary path with a nearest-enabled-object rule. Owner reproduced: with CH2 dragged to
  the bottom CH1 can be picked up again, but only when the drag starts exactly on the invisible zero line.
  Remains: batch 3 (no blocking touch loop, needs an MCP soak) (check: CHANGELOG 0.2.54-0.2.80 lists no such change).
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
  (check: CHANGELOG 0.2.45 says calibration v2 is superseded by v3; hardware-status 10-19 10:26 shows v3 zeros exact at 0 V, but the PWM sweep and the 1.000 V / 0.010 V volts points are not recorded.)
- [x] **Reproducible firmware builds** (fw-gen7, fw 0.2.47: identity as data from the commit; .bin and .elf identical
  across directories): the same source with the same GCC 10.3.1 gives different images from main and from a worktree
  (0.2.44: d4c0b716 vs 08f9a5ed, 172,941 differing bytes). Likely __FILE__/path strings; add `-ffile-prefix-map=$(abspath .)=.` (and
  `-fdebug-prefix-map`), check for __DATE__/__TIME__, and make CI's image hash match a local main build. Fold into the dead-code purge.
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
- [x] **16-bit trigger level ignores corr16 and the class gain** (fw-gen7, fw 0.2.47, 04619ca; bench pending) (fw-cal2 handoff, section 23): about 1 % level error at full
  rate in filtered captures. Small firmware fix; add a host test.
- [ ] **Phosphor fast capture loop** (design study, fw-phosphor section 24): the view draws one record per pass (about 30 wfm/s);
  a capture loop that reads several records per pass would raise the waveform rate. Measure pass_ms in the view first.
  Remains: the capture loop itself; the design note docs/plans/phosphor-fast-capture.md landed with fw 0.2.58 and the scope-view pass is 15 ms median with the MMU (hardware-status 10-19 08:10).
- [ ] **Gain-per-range check** (fw-cal2 assumption 1, README 6.3 step 7): gains measured on one range are applied to all six; bench
  CH1 at ranges 1/2/3 with the generator static on the post; if spans disagree beyond 0.3 %, store gains per range (80 bytes free).
- [x] **Bode analyser, host steps H1-H3** (docs/plans/bode-design.md fe5780b; done by mcp-gen4: 1d46cf8, f0940ee, 363f676,
  d3a16bb, simulator-tested, bench pending in "MCP / host"): MCP tool `scope_bode_sweep` host-driven with the existing
  commands (SET_GEN raw 50 MHz / M tones, SET_DECIM CIC3+FIR, coherent READ_RAW layout 2, single-bin DFT), thru-calibration, fixed range
  first, then down-ranging; bench first on an RC low-pass R 1.6 kOhm / C 10 nF (fc 9.95 kHz) with DUT ground on the USB shell.
- [x] **Bode analyser, device steps D1-D5** (fw-bode 0.2.66 merged 18:20; owner 18:45: Bode plot is a main-menu mode row after Spectrum analyser at a 52 px pitch, eight rows, removed from the generator pop-over -> fw-polish2 item 6; the earlier "entry from the generator menu" note is superseded). Was: after the host version proves the numbers: display mode with magnitude/phase panes,
  BODE_CTRL / READ_BODE. Owner decision: entry from the generator menu (the main menu has no eighth row).
- [x] **docs/protocol.md tidy** (docs-gen3, merged 6f1ea23 on 10-19) (docs-gen1 findings): row 32 says GET_STATUS bytes 42-43 are zero while rows 41/45 define ext_flags and
  display_mode there (add a "superseded" note); row 49 calls CIC+FIR the eventual default while CIC2 is the default since 0.2.44; row 51
  says x1 or x10 probes for the gain step while README 6.3 says x1 (fw-cal2: range 2 for x1, range 5 for x10, so both are supported:
  align the README). Next firmware agent.
- [x] **FPGA 0x1536 firmware + MCP support** (fw 0.2.58 / protocol 1.17 and mcp-gen5 4699593; bench pending, see the '1.17 on the unit' item; 0x1536 itself was superseded by 0x1537 flashed 10-19 16:31) (after fpga-gen5; fpga/al3/README.md section 6, docs/plans/segmented-capture.md, fpga/al3/AGENTS.md):
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
- [~] **USB: device lost after a long idle on 0.2.48** (0.2.49 merged; reopen soak 3 x 30 min idle 10-19 11:34-13:04 clean, counters 36/37 stayed 1/0) (fw-usb2 10-19: 0.2.49 merged with the suspend watch, inferred resume that acts,
  soft reconnect after a retry storm or 15 min suspended, counters 60-61, event type 20, protocol 1.14; bench plan firmware/AGENTS.md
  section 27; Windows selective suspend on this laptop: AC disabled, DC enabled) (10-19 10:20): port closed from about 08:40 to 10:20, device vanished from the bus
  (no 1209, no re-enumeration), GET_LINK_STATS after replug: suspends (36) = 17, resumes (37) = 0, no restart. The host suspended the idle
  device and it never resumed: RESUME interrupt never fires. Decode the event log (types 2/3 fields of row 50), check MUSB resume
  handling (INTRUSBE resume bit, POWER.Resume, PHY), and the F1C100s USB PHY suspend behaviour; bench with `fnirsi1013d-soak --mode reopen
  --idle 1800`. Overnight 10-minute idles never suspended (36 stayed 1); the long idle did.
  Remains: the long idle (about 2 h with the port closed) ending with the device still on the bus; the two-hour run was stopped after about 65 min at 14:20.
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
- [x] **Measurement cards in the stock scheme** (fw 0.2.51 chip scheme, standard names, Freq fix; card layout again in 0.2.65; bench pending) (owner 10-19): chip with the channel digit in the channel colour + "Metric: value" in
  white, dark grey rounded card; fix the "Freg" label; scope_functions.c scope_display_channel_measurements. Part of the UI batch
  (docs/plans/ux-makeover.md section 3).
- [~] **Measurements v2** (fw 0.2.65: per-mode MEASURES lists, order, defaults Freq / Vpp / Vavg, spectrum THD, noise floor, RBW, Span, Center, Time, words 221-223; bench pending) (ux-review section 6, plan section 3a): per-mode MEASURES lists in spare settings words with default cards
  (a new unit shows none today: scope_reset_config_data sets no cards despite its comment); the cheap on-device set (rise/fall, overshoot,
  top/base, CH1-CH2 delay/phase/gain, cursor-gated statistics, PI decay tau; spectrum H2/H3, THD, SFDR, noise floor, band power, OBW);
  AC RMS beside Vrms; the standard names (Period, +Width, -Width, +Duty, -Duty) and the "Freg" fix. After the UI batch.
  Remains: spectrum H2/H3, SFDR, band power, OBW; AC RMS; CH1-CH2 delay / phase / gain; PI decay tau (check: CHANGELOG 0.2.65 names none of these).
- [x] **README timing fix** (docs-gen3, merged 6f1ea23 on 10-19): firmware/README.md still quotes the pre-MMU FFT times (4096 points 10.9 ms); the MMU bench measured 2.72 ms
  on 0.2.48 (docs/hardware-status.md 08:10). Update the spectrum section's numbers. Next firmware agent.
- [x] **"Confirm to delete?" dialog** (fw 0.2.55, 24743a7: 160 x 44 px buttons 16 px apart; benched 10-19 16:06 select, save and delete work): its two buttons sit 11 px apart (ux-review); apply the dialog rule (44 px, 16 px apart, primary right).
  UI batch step 1.
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
- [x] **Digital phosphor** (fw-phosphor, fw 0.2.46 merged 2e73d28; visual bench pending): design in docs/plans/dpos350p-features.md
  section 4; XY mode could get intensity grading from the same map.
- [x] **Bode / frequency response** (owner priority 3): design docs/plans/bode-design.md; host tool done (H1-H3 above); device steps D1-D5 queued.

## MCP / host

- [x] **fnirsi1013d-bode crashes on a flat CH1 record** (merged 10-20 11:42, bench pending) (bench 10-20 11:16): `measure_pair` divides by `tone(a)` = 0 when the CH1
  record is constant (the 0x153A first-record artefact gave a saturated record): ZeroDivisionError ends the sweep. Guard it:
  a zero or near-zero CH1 tone flags the point ch1_low / incoherent and retries once like a clipped point; never a traceback.

- [x] **Server releases the serial port when idle** (merged 10-20 11:42, bench pending) (coordinator 10-19 22:08): on collaborative firmware the MCP server keeps COM10 open
  from the first tool call until the server exits (`Server.scope`, `drop_connection` only on a transport error), so the coordinator's
  own scripts and `fnirsi1013d-flash` get PermissionError on COM10 after any MCP tool call in the same session, and a FEL reload needs
  the device to vanish first. Close the port after N s without tool calls (default 30 s, env FNIRSI_MCP_IDLE_CLOSE_S, 0 = hold) and
  reopen lazily on the next call; keep the legacy REMOTE idle rule; tests with the simulator; note it in mcp/README.md and the
  agent guide ("the port is free while no tool runs").

- [x] **scope_status docstring** (docs-gen1): names all five display modes (mcp-gen4, with the Bode docs commit).
- [x] **Bode bench session** (10-19 14:35-15:25: RC low-pass and high-pass consistent to 0.14 dB / 1 deg, floor -80 dB, thru +0.3 dB, divider flat: tool accepted; capacitor dispersion is the part; DC overload in wiring B found, wiring A recommended) (was: RC 1k/15n, floor -80 dB, thru +0.3 dB, divider flat -20.5 dB: tool proven; RC capacitor deviation open; 0.1 V/div bug found) (mcp-gen4 hand-off; procedure in mcp/AGENTS.md "Bode bench"): wiring A/B, thru, floor, coherence, the
  1.6 kOhm / 10 nF RC; record the floor, skew and the wiring verdict in docs/hardware-status.md.
- [ ] **Bode follow-ups** (after the bench): CH1 re-ranging when CH1 drops below 20 codes (resonant input nodes), the harmonic fast
  mode (design 1.3), the impedance mode z_series_ohm (design 2.4, coil L / Q / SRF for the detector); the PNG plot is done (scope_report, mcp-gen6).
- [~] **1.19 / 1.20 on the unit, host side** (scope_health on 0x1539 10-19 21:12 and scope_cal_probe 20:25 done) (mcp-gen6 hand-off, mcp/AGENTS.md open item 0a): scope_health encode_check
  on 0x1538; scope_bode_sweep(where='device') on the divider and the RC next to where='host' (within 0.05 dB / 0.3 deg).
  Remains: device vs host Frequency response sweep on the RC and the divider, blocked by the A-core fault until 0x153A (now flashed 10-20 10:03).
- [x] **CAL_PROBE (1.21) host decoder** (mcp-gen6, b3b9c51): `scope_cal_probe`, `CalProbeResp`, simulator model.
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
- **Firmware batches still open**: ACQ memory chips after Filter -> Off; "Spectrum ladder follow-ups" (a)-(c); "Frequency
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

- mcp-gen7 (done 11:38, about 240k tokens, merged 11:40): idle port close after 30 s (FNIRSI_MCP_IDLE_CLOSE_S), Bode flat-record
  retakes and the dead-CH1 guard, reason 8 text, simulator reason 8 / flat-record hook / header rule, docs/mcp-tools.md. The
  session's own MCP server process still runs the old code until the session restarts: the idle close is checked next session.
- fw-fr3 (done 11:40, about 245k tokens, merged 11:42 as fw 0.2.84, handoff firmware/AGENTS.md section 55): Single sweep button
  states on the fold-out (owner 10-19 21:32), the generator setting restored on the pin after a sweep (bode-ui.md 7), CH1-low
  warnings by cause (range, probe roll-off, DUT), AUTO SET fails fast (owner 10-20 11:23): absurd points dropped, reference =
  median of the three lowest valid points, peaks need two neighbours, inconsistent passes end in STOP with one of four texts;
  side effects: a sweep keeps running while a fold-out is open; a Q > 30 tank whose overview point lands on the resonance is
  reported "peak not confirmed: sweep by hand".

- Batch of 2026-10-19 20:58 (owner: "continue working off the roadmap with agents"), all in worktrees, all push their own branch:
  fw-freq2 (Opus, branch fw-freq2, fw 0.2.70 provisional): single sweep visible and STOP cancels, Spectrum slider, top bar in the
  mode, cancelled AUTO SET restores the table, Move fast cursors, MEASURES immediate, sharp-tank span, Trace style Lines/Dots,
  meaningful grid lines in both modes, BODE_STATUS header/estimate consistency, pre-scan readout-fault reason.
  ux-overview (Opus, designer, branch ux-overview): slider-track mini overview design (docs/plans/slider-overview.md, boards in
  docs/mockups), self-review and owner questions; firmware batch after approval.
  docs-site (Sonnet, branch docs-site): mkdocs.yml + Material, docs.yml Pages workflow, guide and article skeletons,
  tools/screenshots.py against the simulator, CI build job.
  fw-screen (Opus, branch fw-screen): READ_SCREEN firmware handler so scope_screenshot works on the unit (docs screenshots).
  fw-bus (Opus, branch fw-bus): FPGA bus contract fixes (one-word-late readout per bitstream, EXT shift, arm order, 0x33 sticky
  after a rate write, type 22 trace to debug level, core-order convention from 0x33 on 0x1538+).
  fpga-gen9 (Fable, branch fpga-gen9 from fpga-gen8's branch, owner 20:55): adversarial review of the AL3 capture path, clocking,
  constraints and the TD initial-value finding, then finish 0x153A (build, every testbench, tb_adccap, contract transcript, docs,
  images for pinning, dist entry, firmware notes); 0x153B = the features branch gen8-features-wip afterwards.
- docs-site (done 21:05, about 180k tokens, merged): mkdocs.yml + Material, docs/index.md, guide/ and articles/ skeletons,
  agent-setup.md, tools/mkdocs_hooks.py (GitHub links for files outside the site, `![..](screenshot:NAME)` placeholders),
  .github/workflows/docs.yml (Pages deploy, no secret) + ci.yml docs job, tools/screenshots.py (--list, --simulator, --port,
  --only, --generator opt-in) with a simulator test. Owner once: Settings > Pages > Source: GitHub Actions (releasing.md section 6).
  Remains: screenshots on the unit after fw-screen, per-screen guide text after 0.2.70, the six articles, agent worked examples.
- fw-freq2 (done 21:25, about 455k tokens, merged as fw 0.2.71 with the renumbering, handoff firmware/AGENTS.md section 47):
  single sweep shows RUN/STOP and "sweeping", STOP ends after the capture in progress; AUTO SET cancel restores the table, sharp
  tanks get up to three finer passes; top bar chips in the mode; Trace style Lines/Dots (word 216), the grid rule in both modes
  (ui-guidelines.md section 3); Spectrum slider (window over 0..min(fs/2, 25 MHz), deviation noted in ux-makeover.md); READ_BODE
  reason 8 "ADC readout fault" when the pre-scan clips at 5 V/div under 20 codes; header = settings in effect before the first
  sweep. Open (queue): the simulator lacks reason 8 and the new header rule (mcp item); a single sweep started from the CTRL page
  leaves the bar on that page where RUN/STOP is not visible (owner to decide: jump back to page one?); after a sweep the generator
  chip shows the user's generator although the pin is off. Loaded 21:30 as backups/fw-0.2.71_1264e26c.bin; walk-through pending.
- fpga-gen9 (Fable, done 10-20 00:25, about 640k tokens, merged 00:30 as bitstream 0x153A, handoff fpga/al3/AGENTS.md "0x153A
  review" and "0x153A status"; images pinned, dist entry unbenched): see hardware-status 00:30. Next for the FPGA: flash + bench;
  0x153B = gen8-features-wip (trigger types, peak detect, counter) on top of this design; wanted: pipeline the divider compare,
  encodes into IOB ODDRs, zero the simulation-only initial values.
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
- fw-yield (done 10-19 23:50, about 420k tokens, merged 2026-10-20 00:02 as fw 0.2.78 / protocol 1.23 row 63, handoff firmware/AGENTS.md
  section 53, pinned backups/fw-0.2.78_195afb82.bin, not loaded: scope off): a main-loop request while the UI is modal sets a yield
  flag, every menu / fold-out / dialog / screen / wizard-between-steps leaves by its cancel path, "Host command: menu closed" 4 s,
  E_BUSY kept with details 2 (no main-loop pass in 1.5 s), 3 (calibration measurement), 4 (file save), 5 (USB drive);
  GET_LINK_STATS 70 counters (68 yields, 69 kept-busy); mcp advice by protocol version, simulator yields; test_modal_yield.
  Open: the Spectrum view does not draw the note yet (queued); BODE_CTRL / READ_BODE are main-loop commands, so a host polling
  a device sweep closes a fold-out the person opens meanwhile; the simulator's link stats keep 31 counters.
- fw-spzoom (done 23:30, about 295k tokens, merged 23:32 as fw 0.2.77, handoff firmware/AGENTS.md section 52, not loaded: the
  scope is off): Spectrum zoom ladder in spec_ladder.c (1-2-5 spans about the centre, rate = slowest whose fs/2 covers the stop
  with a capture of N samples at most 2.048 s, 16-bin minimum, taps at an end do nothing, stopped keeps the record's rate with
  the Scope's notice, Auto lands on a ladder cell from 0 Hz, the full axis at 50 MSa/s = option (a)); test_spec_zoom 685,416
  checks. Open points from the handoff (queue): the Time card shows the waterfall history, nothing shows N/fs; the 2 s limit uses
  N/fs while a capture really takes memory depth/fs (16k at 2 kSa/s = 8 s); an FFT length or memory change does not re-derive the
  rate until the next step; option (b) is one line.
- fw-zoom (done 22:35, about 240k tokens, merged 22:38 as fw 0.2.76, pinned backups/fw-0.2.76_bf551df4.bin, loaded 22:39, handoff
  firmware/AGENTS.md section 50 with the bench list): Scope slider tap jumps at touch-down (0.2.73 only jumped after a 6 px move);
  zoom ladder: each tap or ACQ time/div box sets the time/div with the fastest rate whose record covers ten divisions at ten or
  more samples per division at the current memory depth, a hand-picked rate holds until the next zoom step, ends 50 ns/div at
  200 MSa/s and 200 ms/div at 1 kSa/s, taps at an end do nothing; test_acq_view walks every cell at nine depths. Agent choices
  for the owner: stopped / waveform viewer keep the record's rate and show "Stopped: press RUN to zoom further" at the limit; a
  tap no longer crosses into or out of roll (the ACQ mode button does); SET_TIMEBASE by UI index unchanged (protocol.md note).
- fw-spec (done 22:31, about 70k tokens, merged 22:33 as fw 0.2.75, pinned backups/fw-0.2.75_4b9907dc.bin, not loaded: goes on
  the unit with fw-zoom): Waterfall and Density legends removed (spectrum.c spec_draw_legend gone), section 51.
- fw-over2 (done 22:17, about 120k tokens, merged as fw 0.2.74, loaded 22:19): decision 7 geometry in all three modes, boards
  regenerated, addendum in firmware/AGENTS.md section 49.
- fw-over (done 21:50, about 335k tokens, merged 21:53 as fw 0.2.73, handoff firmware/AGENTS.md section 49, pinned backups/fw-0.2.73_ad7b7e24.bin, not loaded): the slider-track overview per the owner's decisions
  (18 px window, 14 rows, dimmed/full, Scope envelope replacing the stock thumbnail, Spectrum from the existing FFT, Frequency
  response AUTO SET copy), then tap-to-jump as the fast follow on the same branch; boards regenerated to 18 px.
  Result: track x 2..727 by y 453..471 (19 rows, decision 6), slider_ov.c/.h + slider_ov_draw.c shared by the three modes,
  the 1.00o5 "~" characters in the Scope track were decoration, not data (removed); Scope envelope about 0.75 ms per pass at
  2 x 16,382 samples (computed; measure pass_ms on the bench, cache per record if more than 1 ms slower), Spectrum under 0.1 ms,
  Frequency response negligible; tap-to-jump in all three modes (y 450 and below, outside the window); boards regenerated.
  Knock-on: the window maps onto 726 px instead of 720 (host tests updated).
- fw-bus (done 21:40, about 515k tokens, merged 21:47 as fw 0.2.72 / protocol 1.22 row 62, handoff firmware/AGENTS.md section 48,
  bench list B1-B8 there): fpga_bus.c with a per-version dummy-strobe table (every readout one word late on every bitstream, 0x1539
  and stock included; traces and READ_TRACE move two samples earlier and match READ_RAW), exact record-start wrap, EXT shift at
  plain rates (4 words, 2 at 200 MSa/s; GET_CAPTURE_INFO byte 70, GET_DECIM byte 31), EXT kept on channel toggles, 0x0B/0x0C
  before the reset (model: 425/600 lost pretriggers before, 0/11000 after), encode check judges 0x1538+ captures with a rate or
  filter write by their data (no blind re-arm), event type 22 off after boot (GET_EVENT_LOG flags bits 2:1), GET_LINK_STATS 68
  counters, caps bit 12 cleared on 0x1538+ only when boot 0x33 says B first. Open: EXT shift at dividers 1-8 extrapolated (B5
  measures), caps bit 11 (read lag unverified) to clear after B2, docs/mcp-tools.md not regenerated for scope_event_log fpga_trace.
  Pinned backups/fw-0.2.72_7f85890e.bin; load and B1/B2/B6 by the coordinator when the scope is free of menus.
- fw-screen (done 21:10, about 195k tokens, merged as fw 0.2.70; T9 passed 21:11, 0.22 s per screen): READ_SCREEN handler (maindisplaybuffer rows, MAIN only so a
  modal menu answers E_BUSY), SCREENSHOT cap on every known bitstream, vectors, host test, mcp test; bench T9 pending
  (firmware/AGENTS.md section 46).
- ux-overview (done 21:12, about 235k tokens, merged): docs/plans/slider-overview.md (overview in the 12 rows inside the window,
  half brightness outside the window, full inside; Scope min/max envelope 0.33 ms per pass, Spectrum from the FFT it already
  runs, Frequency response keeps a 400-byte copy of the AUTO SET overview), boards regenerated (Scope boards now show the fw
  0.2.64 to-scale window), five owner questions in section 10 E (colours, tap to jump, stale overview, two channels, window
  height; recommendations: dimmed/full, yes later, simple rule, overlaid, keep 14 px). Firmware item queued under "Bottom scroll bar".
  Coordinator note 21:12: the Scope's record slider already draws the whole record as a thumbnail (stock behaviour, visible on the
  0.2.70 screenshot); the firmware batch aligns Spectrum and Frequency response with that drawing instead of adding a second one.
- fpga-gen8 (done 20:57, about 834k tokens, retired; handoff = the last section of fpga/al3/AGENTS.md on branch
  worktree-agent-ac1eb52dff7a62fc7, commits 19d9a5c WIP + e924eba handoff, not merged: main keeps the 0x1539 sources until 0x153A is
  benched). Delivered 0x1539 (flashed 20:06). Found: AD9288 data valid from tPD 6 ns max to tV 2 ns min, so at 200 MS/s each core
  is valid 6 ns of every 10 and the A/B windows overlap about 0.4 ns; 0x1532..0x1539 sampled all four buses at one instant and
  each build landed elsewhere (B bad on 0x1538, A on 0x1539). 0x153A WIP: every ADC pin through an IOB DDR input register with a
  fixed pad delay plus two stages (samples 5..17.5 ns before the write edge in 2.5 ns steps), per-core stage select write 0x63,
  outlier counters read 0x36 / threshold write 0x64, one extra word of ADC latency (EXT offsets grow by one word, 0x53 bits 3/4 no
  effect). Earlier build met timing thinly (clk_200MHz +0.111 ns, sample_write_clock +0.123 ns, 5011 LUT); the latest source (new
  registers stored XOR their defaults) is unbuilt; tb_ext_trigger 1/38 and tb_trigpos 42/882 fail on monitors that predate the
  latency change; fir, long, seg not run; tb_adccap not written; docs not written. Finding for everyone: TD 4.6.8 ignores Verilog
  initial values (every register powers up 0; builds with different initial values were byte-identical), so documented power-on
  values of existing registers may be wrong on the hardware. Features (trigger types, peak detect, counter) on gen8-features-wip
  9de82ca, each passed its own testbench in a scratch copy, full run and docs unfinished; to be rebased as 0x153B.

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
- fw-freq (done 19:45, about 475k tokens, retired; handoff firmware/AGENTS.md section 45): Frequency response UI merged as fw 0.2.69
  (one-pane dual-axis chart, Display variants + Log/Linear axis, C1/C2 cursors with the box, AUTO SET overview span, Single sweep,
  CSV via SAVE WAVE, ten-item MEASURES, slider row, menu icon, "Frequency / response" row; ten samples per division rule; words
  213..215, 220); pinned backups/fw-0.2.69_8babde3b.bin, loaded 19:48. Follow-up batch (owner 10-19 20:00 adds: a single sweep must be visible as running with STOP cancelling it after the point, today STOP has no effect; the Spectrum slider; a mini overview drawn in the slider track where the data exists: the record's min/max envelope in Scope, the full-span spectrum in Spectrum, the AUTO SET overview in Frequency response, design first): top bar in this mode (ACQ chip "Set by the
  sweep", generator chip shows the tone), the Spectrum slider, cancelled AUTO SET restores the previous table, Move fast for
  cursors, MEASURES change immediate, sharp-tank overview span.
- ux-bode (done 19:55, designer, retired): Frequency response mode design (artifact DfydC8HJgnemUw3EHenTGP, docs/plans/bode-ui.md
  6d849f6: chart, cursors, Display variants, icon, slider rows for Spectrum and Frequency response, track 1 Hz..10 MHz).
- mcp-gen6 (done 19:05, about 500k tokens, retired; handoff mcp/AGENTS.md): protocols 1.19 (encode_check in scope_health, events
  21/22), 1.20 (scope_bode_sweep where=device, close_view), 1.21 (scope_cal_probe, 252-byte decoder), fnirsi1013d.report +
  scope_report (PNG + offline HTML), guide 7a "Presenting results", simulator variants al3e; pushed 2fb610e, CI 39290741852 green.
  Bench left: scope_health on 0x1538, device vs host Bode sweep on the RC and divider (expect 0.05 dB / 0.3 deg), scope_cal_probe.
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
