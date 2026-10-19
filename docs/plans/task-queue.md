# Task queue

Open items discovered on the bench or requested by the owner, not yet dispatched to an agent. The coordinator dispatches
from here in batches (one agent generation gets a coherent set), and moves items to "Dispatched" with the agent name.
Dates are 2026 unless stated. Bench facts behind each item: `docs/hardware-status.md`.

## Firmware

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
- [ ] **Dead-code purge** (owner dropped the byte-identical rule 10-18): PC_PROTOCOL always on, hidden-option masks
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
- [ ] **Morning bench with the owner (load 0.2.46 first, backups/fw-0.2.46_49a7989b.bin)**: (1) back up SD 700-710; base calibration
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
- [ ] **Bode analyser, device steps D1-D5** after the host version proves the numbers: display mode with magnitude/phase panes,
  BODE_CTRL / READ_BODE. Owner decision: entry from the generator menu (the main menu has no eighth row).
- [ ] **docs/protocol.md tidy** (docs-gen1 findings): row 32 says GET_STATUS bytes 42-43 are zero while rows 41/45 define ext_flags and
  display_mode there (add a "superseded" note); row 49 calls CIC+FIR the eventual default while CIC2 is the default since 0.2.44; row 51
  says x1 or x10 probes for the gain step while README 6.3 says x1 (fw-cal2: range 2 for x1, range 5 for x10, so both are supported:
  align the README). Next firmware agent.
- [ ] **FPGA 0x1536 firmware + MCP support** (after fpga-gen5; fpga/al3/README.md section 6, docs/plans/segmented-capture.md, fpga/al3/AGENTS.md):
  commands 0x57-0x5D, reads 0x2B-0x32 via the 0x1F pointer, segment table (64-bit entries: valid, triggered, trigger offset, 40-bit
  5 ns timestamp), averaging with positions P and shift S at readout; firmware capture modes "Segments" and "Average N" with capture-info
  fields, READ_RAW layouts for a segment set and the averaged record; MCP `scope_capture_segments` / `scope_capture_averaged`; bench check 5
  (an N = 1 average equals the memory's record) first after flashing. Next firmware generation + mcp-gen4/5.
- [ ] **Overnight zero step at 03:48** (docs/hardware-status.md 04:00): after 4.4 h flat, CH2 core A moved -1.7 codes (B unchanged) and CH1
  both cores +0.5 within one 10-minute interval, persisting. Morning: battery/charger state, recalibrate on 0.2.48, re-check the CH2 A/B
  split; if per-core zeros move with temperature or supply, consider a periodic zero re-check (needs an input-independent method) or a
  warm-up note in the calibration procedure.
- [ ] **Calibration v4: background interleave-mismatch tracking** (10-19 morning, docs/hardware-status.md 08:30): the full-rate core
  offset and gain relationship changes over hours (3.3 % gain mismatch last night, 0 % now; zero split 3.9 vs 5.0 codes within 15 min of
  calibration; the 03:48 overnight step), so v3's one-time full-class gains/zeros go stale while the gated class stays within 0.3 code.
  Standard interleaved-ADC fix: estimate offset mismatch (mean A - mean B) and gain mismatch (std A / std B, or the fs/2 spur power) from
  live records when the signal is band-limited (no energy near fs/2 and enough samples), low-pass the estimates over many records, and apply
  them on top of the stored zeros/gains in the full-rate class only. Design first (fw-cal3): detection of valid records, convergence time,
  interaction with the 16-bit path and the spectrum view, a diagnostics readout of the tracked mismatch, host test with a drifting model.
  Owner decision needed on whether the one-time v3 gain step stays (it still fixes the class gain ratio and the gated class).
- [ ] **Calibration wizard (owner approved 10-19)**: replace the fold-out calibration submenu and the two separate flows with one modal
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
- [ ] **Diagnostics view redesign, revision 2** (owner 10-19; adversarial review docs/plans/diagnostics-review.md d95ce0b; design
  https://claude.ai/artifact/M3udheReCiHLQmASaY5qZr, artboards Status / Calibration / Log / Settings): Diagnostics becomes a main-loop
  display mode with three pages by header buttons; Status = identity lines (fw, git, time, protocol, serial, MMU state; FPGA word + features),
  four tiles with verdict words (Firmware: running vs card image; Restarts; USB: connected, bad frames, suspends, reconnects; Battery level
  of 20 + charging + RTC temperature when fitted), one calibration verdict line per channel against the review's provisional limits,
  provenance (FPGA word, fw build, uptime and power-on count at calibration), buttons Calibrate (wizard) and Save report; Calibration page =
  per-range table (PWM centre, volts factor as % from default, zeros A/B per class), corrected gain mismatch, noise vs limits, DC shift
  model inputs, actions Check on post, Undo last calibration, DC shift trim, Clear calibration (confirmed); Log page = decoded event log,
  restart record (persisted), SD state, timing; Settings screen (from Other settings) = Display, Controls, Files, Clock, Start-up rows with
  values as words, Restore default settings (confirmed, keeps calibration, no boot-byte write); "fw: PECO" and the 1.00o5 tables removed;
  fonts 13/16/24 px, buttons >= 44 px, hit area = drawn area. Needs the readers of review section 4 (USB link state, card image identity,
  calibration provenance in the block, power-on counter, persisted restart record, save-state, post check, previous block for Undo, CP15
  word, FPGA build hash). Also from the review: "USB connection" -> "USB drive / SD card" with an explanatory screen; first-run screen;
  remove the USB-Serial and Dev mode toggles. Owner decisions pending on the menu tree (modes vs variants). Next firmware generation.
- [ ] **Trigger position varies by run at decimated rates** (10-18, +11 us / -1 us at 1 MS/s): deterministic test with
  GET_CAPTURE_INFO logging; likely decimation phase / word-to-sample mapping.
- [x] **Digital phosphor** (fw-phosphor, fw 0.2.46 merged 2e73d28; visual bench pending): design in docs/plans/dpos350p-features.md
  section 4; XY mode could get intensity grading from the same map.
- [x] **Bode / frequency response** (owner priority 3): design docs/plans/bode-design.md; host tool done (H1-H3 above); device steps D1-D5 queued.

## MCP / host

- [x] **scope_status docstring** (docs-gen1): names all five display modes (mcp-gen4, with the Bode docs commit).
- [ ] **Bode bench session** (mcp-gen4 hand-off; procedure in mcp/AGENTS.md "Bode bench"): wiring A/B, thru, floor, coherence, the
  1.6 kOhm / 10 nF RC; record the floor, skew and the wiring verdict in docs/hardware-status.md.
- [ ] **Bode follow-ups** (after the bench): CH1 re-ranging when CH1 drops below 20 codes (resonant input nodes), the harmonic fast
  mode (design 1.3), the impedance mode z_series_ohm (design 2.4, coil L / Q / SRF for the detector), an optional PNG plot.
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

- fw-usb2 (done 11:20, about 355k tokens, retired): 0.2.49 merged; bench pending (section 27).
- fw-cal3 (worktree, Opus, started 10-19 10:35): calibration wizard + v4 background interleave-mismatch tracking; fw 0.2.50, protocol 1.14.
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
