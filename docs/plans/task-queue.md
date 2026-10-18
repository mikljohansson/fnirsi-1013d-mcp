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
- [ ] **USB suspend/resume handling** (10-18 22:48, fw 0.2.43 FEL-loaded, PHY fix present): after an idle period with the port closed the
  device logged suspend -> bus_reset -> set_config and the host's first write hit Windows error 22 (device failed before the open); the
  reconnect a second later worked. usb_resumes has been 0 on every build while usb_suspends count: the device never sees or handles the
  resume, so the host resets the port instead. Investigate MUSB suspend/resume (INTRUSB resume enable, POWER.SuspendM/Resume, PHY state
  during suspend, clocks), make resume work, and bench with the soak tool in idle-closed-port mode (open, close, wait 2-5 min, open).
- [ ] **MMU + D-cache** (big lever: FFT 4096 points 10.9 ms uncached; flat 1:1 map with the framebuffer/DMA regions
  uncached; measure pass time and FFT_BENCH before/after). Separate agent, careful bench.
- [ ] **Dead-code purge** (owner dropped the byte-identical rule 10-18): PC_PROTOCOL always on, hidden-option masks
  replaced by removal (FFT switch, generator Wave/Ampl/Offset/Phase, Long memory, hold-off, 1014D remnants), unused files,
  check-reference target and reference downloads removed from the Makefile/CI/docs. Do after the spectrum/calibration
  work lands to avoid conflicts.
- [ ] **Calibration v3: per-core, per-class gain** (fw-cal2, after fw-gen6's 0.2.44 merges): design in firmware/AGENTS.md section 21;
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
- [ ] **Reproducible firmware builds**: the same source with the same GCC 10.3.1 gives different images from main and from a worktree
  (0.2.44: d4c0b716 vs 08f9a5ed, 172,941 differing bytes). Likely __FILE__/path strings; add `-ffile-prefix-map=$(abspath .)=.` (and
  `-fdebug-prefix-map`), check for __DATE__/__TIME__, and make CI's image hash match a local main build. Fold into the dead-code purge.
- [ ] **CH2 reads +1.0..1.5 codes at range 6 with shorted input right after calibration** (10-18 late): channel offset with the cores
  agreeing; see docs/hardware-status.md late evening and the overnight log. Decide after the log: PWM/relay drift at hw range 5 vs probe.
- [ ] **Morning bench with the owner (0.2.44)**: spectrum strip repaint loops, generator menu rows, Filter box steps + DEFAULT = CIC2,
  recalibrate on 0.2.44 (64 full-rate conversions), flash FPGA 0x1535 (backup first) and bench CIC2+FIR (GET_DECIM bytes 26-27 = 0xE2 0x01
  at 10 MSa/s).
- [ ] **Trigger position varies by run at decimated rates** (10-18, +11 us / -1 us at 1 MS/s): deterministic test with
  GET_CAPTURE_INFO logging; likely decimation phase / word-to-sample mapping.
- [ ] **Digital phosphor** (roadmap, owner priority 2 after the spectrum analyser): design in docs/plans/dpos350p-features.md
  section 4; XY mode could get intensity grading from the same map.
- [ ] **Bode / frequency response** (owner priority 3): host tool first, then on-scope; design in the same document.

## MCP / host

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

- fw-cal2 (worktree, Opus, started 10-18 23:20): calibration v3 per-core/per-class gain, fw 0.2.45, protocol 1.12, row 51.
- fw-phosphor (worktree, Opus, started 10-18 23:20): digital phosphor display mode, fw 0.2.46, protocol 1.13, row 52.
- mcp-gen3: protocol 1.11 host side (FIR bits, event fields, soak --mode reopen).
- fw-gen6 (worktree, branch fw-gen6, about 350k tokens, retired): fw 0.2.44 merged into main (463931d, image d4c0b716): 0x1535 FIR
  support, spectrum strip repaint, CIC2 default, USB suspend/resume rework (protocol 1.11, rows 49-50), generator menu hit-area fix.
  Bench by the coordinator in progress.
- fw-cal (retired at 515k tokens): fw 0.2.43 merged (b27c1ee); handoff firmware/AGENTS.md section 21 merged (5effb7d). Remaining
  README 6.2 bench steps (CH2 drift check, -1 V point, PWM sweep, volts check) are queued above.

- fw-gen5 (retired after 0.2.42): Auto redesign, header, warnings delivered in fw 0.2.42; owner verdict pending.

- fpga-gen4 (retired at about 290k tokens, 10-18 23:40): FPGA 0x1535 built, pinned, host-tested, documented (292a596); not flashed.
  The 100 kHz residual question is answered (DDS spur, see above).
- mcp-gen3: idle, holds item 4 (version bump).
