# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The firmware (`PC_FW_VERSION` in `firmware/scope/pc_cmds.h`) and the mcp package (`mcp/pyproject.toml`)
share one version from the first tag on. A release tag `vX.Y.Z` requires both to read `X.Y.Z` and a
`## [X.Y.Z] - YYYY-MM-DD` section in this file, whose text becomes the release notes. See Since the 0.3.0 release engineering (rel-gen1) the user-facing release notes are written separately in `docs/release-notes/X.Y.Z.md`; this file stays the development log.
`docs/releasing.md`. Tags below 1.0.0 are published as pre-releases.

## [Unreleased]

Nothing has been tagged yet. Every firmware version below is a development build. It was FEL-loaded
on the owner's unit (AL3A10 FPGA, bitstream 0x1532) and benched as recorded in
`docs/hardware-status.md`. Image hashes in that log come from local builds. A release image is built
by CI from the tag commit and has its own hash.

### Plan for the first tag (v0.3.0)

- Land the rest of the current firmware queue (`docs/plans/roadmap.md` item 0). As of 0.2.7, the fix for
  request corruption under RX back-pressure is still open. That version added counters for it.
- Bench every item, persist the firmware to the card, and run a 30-minute soak with the MCP server polling.
- Set `PC_FW_VERSION` to `0.3.0`, set the mcp package version from `0.1.0` to `0.3.0`, and move the
  entries below into a `## [0.3.0]` section.
- Tag `v0.3.0`. Load the attached scope image over FEL and check its sha256 against `SHA256SUMS`
  (`docs/releasing.md`, section 4).

### Firmware (scope application, `firmware/scope`, base Atlan4 v1.00o5)

Up to 0.2.46 a `PC_PROTOCOL=0` build was byte-identical to Atlan4's v1.00o5 image (`make check-reference`,
CI logs up to 2026-10-18). The owner dropped that rule; 0.2.47 removed the switch and the check.

#### 0.2.78 - 2026-10-19 (protocol 1.23, docs/protocol.md section 14 row 63)

- Host commands leave the scope's menus (owner 23:27; docs/ui-guidelines.md section 4, docs/protocol.md section 14
  row 63): a request that needs the main loop while a menu, fold-out, dialog or screen is open no longer gets E_BUSY.
  Every modal loop leaves by its own cancel path (a menu closes, a dialog answers Cancel, Settings goes Back, the file
  viewers Return, the first-run screen Later, the calibration wizard Cancel between steps, nothing written), nested
  ones one after the other, then the request is answered and the screen says "Host command: menu closed" for 4 s.
  E_BUSY stays, with the reason as detail and text, while a calibration measurement or a file save runs, in USB drive
  mode, and when the UI does not reach its main screen within 1.5 s. GET_LINK_STATS 68-69 count yields and refusals
  (0493025, 888324c).
- MCP: the "close it on the scope and retry" advice only for firmware before 0.2.78; on 0.2.78+ a tool says in its
  caveats that a menu was closed, and E_BUSY messages give the scope's reason. The simulator yields the same way
  (0f63317).

#### 0.2.77 - 2026-10-19 (protocol 1.22, unchanged)

- Spectrum zoom: span drives the sample rate (docs/ui-guidelines.md section 2, owner 2026-10-19 23:09). The span steps
  through 1-2-5 values (1 Hz .. 20 MHz, then the whole 0 .. 25 MHz axis) about the centre; a drag or the slider pans
  the centre. After every zoom or pan step, while running or in Single, the rate is the slowest whose fs/2 covers the
  stop frequency (centre + span/2) and whose capture of N samples takes at most 2.048 s, so zooming in now lowers the
  rate and zooming out raises it, and a pan across a rate boundary moves the rate. The ends are plain stops: the whole
  axis at 50 MSa/s; zooming in stops where the next span would need a capture over 2.048 s or hold fewer than 16 bins.
  A rate picked in the ACQ menu holds until the next step. Stopped, the record keeps its rate ("Stopped: press RUN to
  zoom further"). AUTO SET picks a ladder cell from 0 Hz. The Span and Center cards show the cell; the FFT length row
  reads "(record)" when the record limits it (52c1c76, ladder and tests 3ec9e17, rule 4a85e8a).
#### 0.2.76 - 2026-10-19 (protocol 1.22, unchanged)

- Scope slider: a still tap in the track outside the window now jumps the window there, as in the Spectrum. 0.2.73 ran
  the jump only once the finger had moved past the dead band, so a tap did nothing; the jump now happens at touch-down
  (ccddf2e).
- Scope zoom: time/div drives the sample rate (docs/ui-guidelines.md section 2, owner 22:20-22:24). While running or
  in Single, a tap zoom step or an ACQ menu time/div box sets the time/div and the ladder's rate for it: the fastest rate
  whose record, at the memory depth kept, covers the ten divisions with at least ten samples per division. Zooming out
  past the record lowers the rate, zooming in raises it, and the ends (50 ns/div at 200 MSa/s; 200 ms/div at the roll
  boundary) are plain stops. The ACQ menu's rate boxes stay for a manual choice until the next zoom step; the snap notice
  stays. Stopped, the record keeps its rate and the zoom steps through what it can show ("Stopped: press RUN to zoom
  further" at its limit). A tap no longer enters or leaves roll mode. The host's SET_TIMEBASE by UI index is unchanged
  (da24d91, rule 4eab9cc).
#### 0.2.75 - 2026-10-19 (protocol 1.22, unchanged)

- Spectrum: the colour legends of the Waterfall and Density variants are gone (owner, 2026-10-19 22:28: the blue ->
  red -> white scale is understood and nobody measures from those views). The warning line stays where it was and the
  rest of both variants is unchanged (6a85fb4).

#### 0.2.74 - 2026-10-19 (protocol 1.22, unchanged)

- Slider track geometry, owner decision 7 (docs/plans/slider-overview.md section 2 and 10 F item 7, after looking at
  0.2.73 on the unit): the window is a box again, 1 px white lines on all four sides with a 1 px black margin around
  it, and the track's own top line at y 452 is gone (the plot pane's outline is the separation). The track is x 2..727
  by y 452..471 (20 rows, was 19); the overview is mapped on all 20 rows, dimmed outside the box and in full colour
  inside its lines (y 454..469, 16 rows). Same in the Scope, the Spectrum and the Frequency response; tap to jump, the
  drags and the hit areas are unchanged (de8f49c, mockups 9e8ffdd).

#### 0.2.73 - 2026-10-19 (protocol 1.21, unchanged)

- Slider track overview (docs/plans/slider-overview.md, owner decisions 10-19 21:20 and 21:36): the slider row under
  the plot keeps only its top line; the track is the whole row, x 2..727 and y 453..471, in the Scope, the Spectrum and
  the Frequency response, and the window is two 1 px edge lines over its full height. A drag still moves the window with
  the finger (a 10 px drag is now 82 samples where it was 83: the track is 726 px, was 720) (a5c875e).
- Scope: the track shows the min / max envelope of each enabled channel over the whole record on the plot's own vertical
  scale, in full colour inside the window and half intensity outside; not in roll mode, X-Y or the slow-rate branch with
  1.00o5's fixed window. It replaces the row of yellow "~" glyphs 1.00o5 drew in the track (decoration, not data)
  (5aa5093).
- Spectrum: the track shows the spectrum over the whole displayed axis (0 .. fs/2 capped at 25 MHz) from the FFT the
  view already runs, the shown span in full colour (2044a49).
- Frequency response: the track shows AUTO SET's overview (magnitude; the phase in Phase only), fitted to the track's
  height, flagged points grey; kept until the next AUTO SET or power-off, cleared by Restore default settings, nothing
  before the first AUTO SET; a cancelled AUTO SET keeps the previous one (2b9dad9).
- Tap to jump: a tap on the slider row outside the window centres the window there (all three modes), and ends like a
  drag: the Scope scrolls, the Spectrum pans, the Frequency response sweeps the new span. The cards and the band above
  them keep their taps (b3fa8d2).

#### 0.2.72 - 2026-10-19 (protocol 1.22, docs/protocol.md section 14 row 62)

FPGA bus contract fixes (fw-bus, firmware/AGENTS.md section 48; not benched).
- Every FPGA readout makes the dummy strobe after the 0x1F pointer write (`fpga_bus.c`). The scope trace, the spectrum,
  READ_TRACE and the 16-bit trace were one word (two samples) late with a stale first byte on every bitstream, 0x1539
  included; they now hold the same words as READ_RAW and the averaged trace (5f38d23).
- The UI record start is (trigger address - pretrigger words) mod the ring. 1.00o5's wrap constants were one word off on
  stock and up to hundreds of words off on AL3 / EF2 when the trigger address lay near the ring start (5f38d23).
- The pretrigger and total words are written before the sampling reset as well as inside it, on every bitstream: on
  0x1532..0x1536 a changed pretrigger was lost at slow rates (record shifted by the difference) (5f38d23).
- EXT trigger at the plain (unfiltered) rates: the record is shifted by the ADC pipeline (4 words, 2 at 200 MSa/s), so
  the edge sits at the trigger point instead of 10 samples after it; switching a channel on or off keeps EXT as the
  source (7833312).
- On 0x1538+ a capture during which the sample rate or filter was changed is judged by its data instead of being
  re-taken for the encode self-check's sticky bit; the per-write trace (event 22) is off unless switched on with
  GET_EVENT_LOG, and the writes are counted in GET_LINK_STATS 64-67 (d6de414).
- The ADC core order counts as verified (GET_INFO caps bit 12 clear) only on 0x1538+ that report it at boot; a bitstream
  reporting another convention is flagged and checked by its data (267c1be).
#### 0.2.71 - 2026-10-19 (protocol 1.21, unchanged; READ_BODE reason 8)

- Frequency response: a single sweep shows RUN on the chip (never WAIT, whatever the trigger mode) and STOP on the
  button, its cards read "sweeping" until its table is complete, and STOP ends any sweep after the capture in progress
  (no re-centring, retry or range step follows; a STOP in the pre-scan ends there), owner 10-19 20:00 (92821ad,
  4d6c0e6).
- Frequency response: a pre-scan that reaches 5 V/div with a channel still clipping and its fundamental under 20 codes
  ends the sweep with "ADC readout fault: see Diagnostics", READ_BODE reason 8 (the owner's fail-fast rule; on 0x1539
  the A cores' rail values gave a table of flagged points), mcp BODE_REASONS 8 with a caveat (92821ad, 4d6c0e6, 92ed2b9).
- Frequency response AUTO SET: a cancel (second tap, STOP, leaving) gives the previous table back; a band the overview
  cannot resolve is measured again on finer passes around f0 (f0 / sqrt(10) .. f0 sqrt(10), / 1.1 .. x 1.1, / 1.03 ..
  x 1.03; 20 points each) before the span and Settle are set: a Q 50 tank gets f0 (1 -+ 4 / Q) and High Q instead of
  half a decade (92821ad, 4d6c0e6).
- Frequency response top bar (bode-ui.md 7): ACQ chip "Set by / the sweep" with a one-line dropdown; the generator chip
  shows the tone being measured, its dropdown "Driven by the frequency response sweep" while sweeping; the trigger menu
  adds "Not used by the sweep" (84a2d65, 4d6c0e6).
- Frequency response CTRL page: Trace style Lines (default) / Dots at y 245 (the Scope's construction; flagged points
  stay grey loose dots), Grid moves to y 305; settings word 216 (84a2d65, 4d6c0e6).
- Grid lines mean something (docs/ui-guidelines.md section 3, `bu_fgrid`): log axes at the decades with 2 and 5 lines
  under three decades, linear axes at the label steps, in Frequency response and Spectrum (84a2d65, 4d6c0e6, 3d76e14).
- Spectrum: the slider row under the plot (the shown span as a window over 0 .. min(fs/2, 25 MHz), a drag pans the
  centre), plot y 50..402 in 8 divisions of 44 px, labels at y 406, cards at the Scope's y 426 (3d76e14).
- Frequency response: Move fast sets the cursor drag step; MEASURES changes show at once; before the first sweep the
  READ_BODE header's settings fields are the settings in effect, the ones the estimate describes (4d6c0e6).
#### 0.2.70 - 2026-10-19 (protocol 1.21, unchanged)

- READ_SCREEN (0x0401) is built: the SCREENSHOT capability is set and `scope_screenshot` returns the panel's 800 x 480
  RGB565 framebuffer (`maindisplaybuffer`) in 12 requests of 40 rows; E_BAD_ARG for rows outside 0..480, E_OVERFLOW
  above 41 rows, E_BUSY while a menu is open. Bench pending: T9, the pixel format against the screen (350823e).

#### 0.2.69 - 2026-10-19 (protocol 1.21, unchanged; provisional number)

- Frequency response mode (the on-device Bode plot) redesigned per docs/plans/bode-ui.md and the owner's final decisions
  (firmware/README.md "Frequency response"). The main menu row reads "Frequency" / "response" with a low-pass response
  icon; a tap on the grey row says why (70d3dec). One pane with magnitude in dB on the left axis and phase in degrees on
  the right axis, no legend, curves in their axis colours, both scales from the data with the top division kept free for
  the warning line; Display fold-out: Overlaid, Magnitude only, Phase only, and Frequency axis Log / Linear (5d262b7).
- The right bar keeps its meanings: RUN/STOP = continuous sweeps / stop after the point; AUTO SET = an overview sweep
  (100 Hz .. 10 MHz, 20 points) that sets Start, Stop, Settle and CH2 range and then runs (`bode_auto_span`, 7772b64);
  T CURSOR = cursors C1 and C2 with flags and a C1 / C2 / C2-C1 readout box at the bottom left; MEASURES = the mode's
  list of ten (Level, -3dB, BW, Peak, Q, Ph45, Ph90, Ph180, Sweep, Thru; defaults -3dB, Peak, Q, Level); SAVE WAVE =
  the sweep table as bode/FR00001.CSV; V CURSOR empty (bbf12f1, 5d262b7, 70d3dec).
- CTRL page: Display, Sweep (Single sweep with its estimated duration), Thru, Grid. A slider row under the plot (owner)
  pans Start and Stop over 1 Hz .. 10 MHz and re-sweeps. One two-line instruction before the first sweep; warnings
  rewritten to at most 50 characters with the action (5d262b7).
- Settings: word 220 bits 3..6 (Display variant, cursors, frequency axis), words 213..215 (the MEASURES list); Restore
  default settings now resets the mode's words (70d3dec).
- Time/div is viable only with at least ten samples per division (owner, bench: 100 kSa/s at 10 us/div drew one sample
  per division and a wobbling edge). 200 MSa/s reaches 50 ns/div; 20, 10 and 5 ns/div snap to 50 ns/div with the notice,
  also for a host SET_TIMEBASE (070e250).

#### 0.2.68 - 2026-10-19 (protocol 1.20, unchanged; provisional number)

- UI polish batch 2 (firmware/README.md "Screens: UI polish batch 2"). RUN/STOP follows the host (95c1033): the top bar
  chip and the right bar button take one run state that includes a host acquisition in progress, and every pass redraws
  a stale one; the chip reads RUN while the PC captures. The button now shows the action: green "RUN" while stopped,
  red "STOP" while running (owner exception to red-is-destructive). STOP during a host capture aborts it.
- Main menu: Settings uses the Other settings spanner icon; the Scope sine is 2 px shorter, over grey axes (ec9f319).
- The time/div snap after a sample rate change takes the same path as a manual time/div choice, with a host test
  that both leave the same state (a9daba6). Owner report: after the snap the trace cycled until time/div was set by
  hand once. Bench to confirm.
- Screen brightness: the sun and the brightness slider are one setting (d51dcb7). The sun toggles 100 % and the value
  it came from; the slider shows the current value. The settings sector's max-light byte now holds that value
  (0x80 | value); an older sector with max light on loads as 100 %.
- Calibration wizard: "Step M of N" on the title line. Diagnostics > Calibration: the four buttons in one bottom
  row, Clear calibration last (3dc0ed9).
- Main menu: Bode plot is the third mode row (owner decision; 1972245), entered and left like Scope and Spectrum
  analyser; eight rows at 52 px. The generator menu's "Bode plot" row is gone. Grey on bitstreams without the filter
  and DDS.

#### 0.2.67 - 2026-10-19 (protocol 1.21 provisional, docs/protocol.md section 14 row 61)

- Fixed: the calibration wizard's Zero level step failed every time on FPGA 0x1538 ("CH1: too much noise, range 1 at
  200 MSa/s: core B 7.2 codes", "CH2: the input is not at 0 V"). Cause (bench with CAL_PROBE and a READ_RAW offset
  sweep): at 200 MSa/s some FPGA readout lanes mix the bits of two successive samples that straddle code 0x7F / 0x80,
  giving outliers up to the rails only while a core's mean sits at 126..133; the zero step parked the zero exactly at
  128. The zero is now measured as the mean of two points 12 codes either side of 128 (the PWM split from the range's
  own slope), which is exactly the zero at the centre in the linear model: the stored calibration means the same.
  Rail samples only at full rate with the gated reading at the same PWM quiet and near 128 now say "ADC readout fault
  near code 128 (FPGA 0x1538): flash the fixed bitstream" instead of "the input is not at 0 V". The bitstream fix is
  fpga-gen8's (c22896c).
- Added: CAL_PROBE (0x0402), the calibration wizard's zero measurement from the host through the wizard's own code
  (`cal_range`), nothing stored: one channel and range (mode 0), or the whole zero step in the wizard's screen context
  (mode 1). It returns all eight measurement points and the zero split with PWM, per-core mean, RMS, RMS inside the captures,
  lowest / highest code and clipped samples, plus A/B switches for the FPGA bus readout (settle reads, a write strobe
  before each sample strobe). `firmware/tools/cal_probe.py` runs it and decodes it (28158ea, 448c08c, cf3f9e2, b67219c,
  8802b79).
- Fixed: the request after a CAL_PROBE got E_BUSY "UI busy" (the probe runs seconds inside one main-loop poll and the
  modal watch counted that); the watch restarts after it (b67219c).
- Changed: a failed channel in the wizard's Zero level step gets a small note under its result line: range, rate,
  measurement point, PWM, and per core the raw mean with its lowest..highest code and clipped samples (789da66).
- Fixed: the wizard's measurement batch writes a raw host divider (SET_TIMEBASE raw) and the time base back
  afterwards, as the class probe does (789da66).

#### 0.2.66 - 2026-10-19 (protocol 1.20, docs/protocol.md section 14 row 60)

- Added: a Bode plot on the scope. The generator menu has a fourth row, "Bode plot": it opens a mode that sweeps the
  calibration post's square from Start to Stop and plots H = CH2 / CH1 as magnitude and phase over a log frequency axis,
  with the -3 dB, peak and phase-crossing markers, a cursor (tap or drag on the plot) and cards for f, |H|, phase, the
  -3 dB frequency (or the peak and Q) and the progress. RUN repeats sweeps, STOP ends after the point in progress, AUTO SET
  runs one sweep. The CTRL page holds Sweep (Start and Stop in 1-2-5 steps, Points 10 / 20 / 50 / 100, Settle Fast /
  Normal / Resonant / High Q, CH2 range Auto or Hold, Run sweep) and Thru (measure with both tips on the DUT input, apply,
  clear). The sweep is the host tool's (`scope_bode_sweep`): the same frequency plan, the pre-scan and ranging, the
  background capture, the settle retry, CH2 auto-ranging with the splice and the same quality flags, without a PC.
  Leaving the mode gives the generator, the channels, the time base, the trigger source and RUN / STOP back. The
  settings are kept in settings words 219-220. The wiring is shown in the plot until the first sweep (DUT ground on the
  probe ground clips). Needs the decimation filter (AL3 0x1534+). (bode_dsp.c, bode_sweep.c, bode_view.c, pc_bode.c)
- Added: protocol 1.20, BODE_CTRL (0x0211: settings, run one or repeated sweeps, stop, abort, thru
  measure / apply / clear, open / close the view; answers the state and the estimated duration) and READ_BODE (0x0212:
  the table of the latest sweep, 20 bytes per point: the DDS period M, f, |H|, phase, V1, V2, ranges, flags). GET_STATUS
  `display_mode` 5 = Bode plot; features bit 6 = the bitstream can run it.
- Tests: `host_tests/test_bode.py` checks the device planner against `docs/bode-plan-vectors.json` and 3300 random plans of
  the host tool, and the device's H, levels, noise, leakage and half-record check against `measure_pair` on the
  simulator's RC (within 0.002 dB / 0.002 deg); `host_tests/test_bode_sweep.c` runs the engine on a DUT model (RC,
  divider, wiring B saturation, Hold, STOP, thru). Two new request vectors (`req_bode_ctrl`, `req_read_bode`).

#### 0.2.65 - 2026-10-19 (protocol 1.19, unchanged)

- Measurements v2 (8749ffe; firmware/README.md "Screens: Measurements v2"). MEASURES now opens a list per mode: the
  Scope's twelve measurements per channel, and in the Spectrum analyser (where the slot was empty) Peak, Level, THD and
  Noise per channel plus RBW, Span, Center and Time. The spectrum's choice is saved in settings words 221..223.
- Cards show in the order they were chosen; the defaults now read Freq, Vpp, Vavg. The menu shows each chosen item's
  place. Older settings show their cards in table order.
- The cards keep to one row instead of stacking up over the trace: three per channel (six for CH1 while CH2's block
  is hidden), six in the spectrum. With more chosen, the last place reads "+N more".
- New spectrum cards: THD over harmonics 2..10 ("f0 too low" / "f0 too high" when it cannot be measured), the noise
  floor as the median bin level at the RBW, the shown span and its centre.
- The MEASURES menu is redrawn in the stock look with a title per mode and 44 px rows; Hide shows in the active
  colour instead of red.
- A card source hook (`ui_ms_set_source`) lets a future FPGA frequency / period counter feed the Freq and Period cards
  without UI changes. Nothing uses it yet.

#### 0.2.64 - 2026-10-19 (protocol 1.18 unchanged; provisional number)

- Fixed: after a sample-rate change in the ACQ menu the old time/div stayed although drawn grey, so the trace was cut
  off, zeros showed past the record end, the slider scrolled before the trace and the shape changed (owner, 1 kHz
  cal signal). The time/div is now viable by rule, derived from the record length (the record spans the trace area,
  at least one sample per division) instead of 1.00o5's table; a rate change snaps it to the nearest viable value and
  says "Time/div set to X for this rate"; grey boxes cannot be chosen; memory depth and Filter changes, the plot's tap
  zoom while stopped, SET_TIMEBASE, SET_TRIGGER pretrigger / total and SET_DECIM follow the same rule (5718015).
- Fixed: the trace window stays inside the record, and a record shorter than the screen stops at its end instead of
  drawing zeros; the trigger point drag reaches both ends of the record (5718015, d1f303f).
- Changed: the acquisition Filter is kept per mode (owner): Scope defaults to CIC2 without the FIR (no pulsing step
  overshoot on a square wave), Spectrum to CIC2+FIR; settings word 212. Old settings with the old default load as the
  new defaults; another saved choice is kept for both modes (5818b5b).
- Fixed: the Peak and Pro trace styles wrote past the 730-point trace buffers (29d61f1).
- Added: the record-position slider under the grid is drawn to scale and can be dragged (the window follows the
  finger); a channel can be grabbed on its drawn trace within 40 px, not only on its zero line; a 6 px dead band
  before a drag; handles never tap and a stroke on a trace does not change the time base; the tap bands stay but
  avoid the slider row and the measurement cards (b620113).
- Changed: the spectrum's pan honours Move slow; CTRL fold-out rows act on release (f690e42).

#### 0.2.63 - 2026-10-19 (protocol 1.19, unchanged)

UI polish batch 1, from the owner's walk-through of 0.2.59 (firmware/README.md "Screens: UI polish batch 1"):

- Changed: the main menu's Settings icon is a cogwheel (f58ff29) and the Scope icon a sine wave (70da62a).
- Changed: CTRL Grid is a whole-button toggle, green on and grey off, without the slide switch (bcb3007). The Display
  fold-out's X-Y icon is a tilted figure 8 (f71ede6). Trace style is grey and inert in X-Y (d513498).
- Removed: the Pro trace style. It drew the per-column minimum and maximum as outlines and looked like Linear on a
  normal signal; a saved Pro loads as Linear (8d508cd).
- Fixed: the ACQ menu's pass time collided with the Memory header and the first depth chip; it now sits under the
  column's rule (cf37f5d).
- Fixed: the Settings fold-out's brightness sliders closed the fold-out when the finger left the slider mid-drag; a
  drag now stays with its slider until release (1fb05fd).
- Changed: the Settings screen drops its header hedge and the note beside Restore; the Restore dialog's text fills
  the dialog's width (2830fc2). File message timeout left the fold-out and is set in Settings > Controls (1ecaef3).
- Changed: Diagnostics uses the Settings screen's header; fixed the battery icon being drawn over the page (ba116cf).
  The Calibration page shows the DC shift trim's state as the button colour and drops the DC supply note (d4345e8).
  The Developer page drops the Debug overlay switch; the flag stays for firmware work (32d17a9).
- Changed: the volts calibration shows "Step N of 6" at the top right of the title line (eb51787).
- Added: in Waterfall and Density the Display fold-out names the channel shown and switches between CH1 and CH2
  (6c82e0c).
- Changed: RUN/STOP shows the state, green RUN or amber Stopped, and follows a host start or stop; it stayed green
  when the PC stopped the acquisition (b1fe1fc).

#### 0.2.62 - 2026-10-19 (protocol 1.19, docs/protocol.md section 14 row 58)

- Fixed: on FPGA 0x1537 CH2 core A was garbled in about 40 % of 200 MSa/s captures, with no register write in
  between, and 0.2.61's check (after rate writes only) missed it. Every 200 MSa/s capture is now checked before
  anything reads it (screen, READ_RAW, calibration wizard, class probe) and re-taken when garbled, up to 16 times. On
  0x1532..0x1537 the check reads 250 words of the record (about 0.3 ms); the extra check conversion after each rate
  write is gone (9e7e53d).
- Added: support for FPGA 0x1538, which guarantees the core order (core B first) and checks its ADC encodes itself.
  The firmware reads its status byte (0x33) after every 200 MSa/s capture and checks the encode convention at boot.
  New caps bit 18 ENC_CHECK and features bit 5 (9e7e53d).
- Added: GET_HEALTH grows to 104 bytes with the check's mode, the last 0x33 byte, flags, and counts of captures
  checked, found bad and recovered. Event type 21 now carries the reason (data check, 0x33, boot convention), the
  number of re-arms and the caller (9e7e53d).
- Changed: Diagnostics FPGA line: 0x1537 in amber, "ADC core order differs: recalibrate after flashing 0x1538";
  0x1538 "encode check OK" or the failing pins; 0x1532..0x1536 now say "fixed in 0x1538" (9e7e53d).
- Changed: the READ_RAW / READ_TRACE interleave order is fixed at core B first (`interleave_order` 0) (9e7e53d).

#### 0.2.61 - 2026-10-19 (protocol 1.18, docs/protocol.md section 14 row 57)

- Fixed: after a sample-rate change the FPGA can garble one ADC core's 200 MSa/s data. This happens randomly on AL3
  0x1532..0x1536 and on CH2 core A on 0x1537 (fpga/al3/README.md "Findings" item 3). The wizard failed "too much
  noise" or "the input is not at 0 V" because of it. The firmware now checks one full-rate capture after every rate
  write: before the next UI conversion, before a host ARM, and before every full-rate point of the calibration wizard.
  When a core is garbled it re-writes the rate and checks again, up to 16 times. If the capture never recovers, the
  wizard says "ADC capture timing: the 200 MSa/s data stays garbled".
- Added: GET_LINK_STATS counters 62-63 (checks that found a garbled capture, and those that recovered). The event log
  has two new types: 21 (a garbled capture) and 22 (a trace of every 0x0D, 0x53, 0x56 and 0x01 write, with its
  caller).
- Changed: Diagnostics shows the FPGA line of AL3 0x1532..0x1536 in amber: "ADC capture timing race, fixed in 0x1537".

#### 0.2.60 - 2026-10-19 (protocol 1.17 unchanged)

- Changed: the Zero level instruction asks to disconnect everything, including anything on the calibration post.
- Known issue: on FPGA 0x1535 the Zero level step can fail with about 20 codes of noise on one ADC core per channel at
  200 MSa/s. The likely cause is the bitstream's A/B encode-phase race, which 0x1537 fixes (`fpga/al3/README.md`,
  `firmware/AGENTS.md` section 36). The measurement and the 3-code limit are the same as in 0.2.48.
- Changed: a noise or drift failure in the Zero level step names the range, the sample rate and the ADC core with the
  measured values, e.g. "CH1: too much noise, range 6 at 2 MSa/s: core B 3.3 codes, 1.1 per capture". "Per capture" is
  the noise inside one capture. The difference from the total is what moved between captures (27556e8).

#### 0.2.59 - 2026-10-19 (protocol 1.17 unchanged)

UX makeover step 4 (`docs/plans/ux-makeover.md` sections 6-6c; `firmware/README.md` section 5 "Screens: Diagnostics,
calibration wizard, first run" and 6.4).

- Changed: Diagnostics (Menu > Diagnostics) is a display mode of the main loop with the pages Status, Calibration, Log
  and Developer and a Back button. The PC protocol keeps working while it shows (the old screen was modal: main-loop
  commands answered E_BUSY), and the values update once a second. Status: the identity line (fw, git, commit time,
  protocol, serial, MMU and caches), the FPGA line, verdict tiles for Firmware (running image against the card's),
  Restarts, USB and Battery, the calibration verdict with the class gain ratio and the noise against provisional
  limits, and where the calibration came from; Calibrate and Save report to card. Calibration: the numbers per range
  and channel, the gains, the v4 tracking; Calibrate, Volts calibration, Clear calibration (keeps the volts factors,
  asks first). Log: the event log in words, restarts, SD card and unit counters. Developer: Debug overlay and Enter FEL
  now (asks first, nothing written to the card) (f95dbee, 5e8a5db).
- Changed: the calibration wizard is four screens (Zero level, CH1 gain, CH2 gain, Done) in one modal dialog over the
  dimmed scope: Cancel left, Calibrate right turning into Continue after a pass, retry after a failure, no Skip, Save
  calibration on the last screen; Cancel writes nothing, also during a measurement (95f440e).
- Added: an uncalibrated unit starts with a first-run screen (what was found, what calibrating needs, Later or
  Calibrate now) (5e8a5db).
- Removed: 1.00o5's "Load default settings value" start-up screen and the old diagnostics screen with its DEFAULT,
  RESTORE and RESET buttons and the hidden boot-mode touch areas; where each value went is listed in
  `firmware/README.md` section 5 (5e8a5db).
- Moved: the interim Baseline calibration row of the Settings fold-out (with 1.00o5's volts-calibration boxes beside
  it) and the Settings screen's DC shift trim row (0.2.55) to the Diagnostics pages (Calibrate, Volts calibration, DC
  shift trim); Dev mode is the Developer page's Debug overlay.
#### 0.2.58 - 2026-10-19 (protocol 1.17, docs/protocol.md section 14 row 56)

Support for the AL3 bitstream 0x1536 (segmented capture and hardware averaging, fpga-gen5; not flashed yet).
Built on 0.2.55 / protocol 1.16 and numbered 0.2.58 by the coordinator (0.2.56 is fw-ui2, 0.2.59 fw-ui4); the
protocol row number (56) is provisional, the coordinator renumbers it at the merge. Spec: `docs/protocol.md` section 14 row 56; README section 5 "Segmented
capture and hardware averaging". Nothing is benched.

- Added: version word 0x1536..0x153F gives caps bit 17 SEQ. Older bitstreams see no new FPGA command (65abebf).
- Added: the acquisition menu's Average uses the FPGA's averaging on 0x1536 when the record fits its accumulator
  (legacy records at the 3000-sample depth: filter off or 200 MSa/s): each displayed record is the mean of 2 to 64
  records (as many as fit 40 ms) instead of the per-pass software average. Other cases keep the software average
  (65abebf).
- Added: SET_SEQ / GET_SEQ / READ_SEQ (0x020E-0x0210) and READ_RAW addr_mode 3: host ARM runs N segments with a
  40-bit timestamp per segment, or averages N records per position (up to 65536 with the filter on); STOP ends a
  run and keeps its data (65abebf).
- Changed: GET_STATUS is 48 bytes (bytes 44-47: the bitstream's features word and the UI Average state), GET_DECIM 36
  bytes, GET_CAPTURE_INFO 96 bytes (the run of the capture) (65abebf).
- Changed: on 0x1536 the arm writes 0x0B / 0x0C and the sequencer registers before the sampling reset, so the
  write-clock side loads them at every rate (65abebf).

#### 0.2.56 - 2026-10-19 (protocol 1.16, unchanged)

The second step of the UX makeover (`docs/plans/ux-makeover.md` section 7 step 2; rules in `docs/ui-guidelines.md`;
README section 5 "Screens: CTRL page, fold-outs, chips, warnings, spectrum cards").

- Changed: the right bar's second page is the CTRL page in both modes: Display, Trace style (Scope) or Analyse
  (Spectrum), Grid, compact V+ / CH1 / V- and V+ / CH2 / V- steppers with 45 px of touch per half, 50 % TRIG (Scope;
  the slot is empty in Spectrum). Buttons that hold a value show it and open a fold-out beside the bar; a tap outside
  closes it. The waveform viewer's page keeps 1.00o5's V+ / V- and Show grid (a41ca39; geometry and texts host-tested
  in test_ui, 93f1b68).
- Changed: Display fold-outs with icons. Scope: Normal or X-Y, and a Phosphor switch with Persistence, Palette, Clear
  and the wfm/s readout under it. Phosphor combines with X-Y again (lost in 0.2.51) and is saved; it pauses in roll
  mode ("Not in roll mode") and comes back after. Spectrum: Spectrum, Waterfall, Density, saved (a41ca39).
- Changed: the spectrum's parameters are in the Analyse fold-out (Analyse CH1 / CH2 / both, FFT length, Window,
  Average, Scale, Ref level with - / +, Reset); Scale and Ref level sit below a line in the same fold-out, so both
  CTRL pages keep one geometry (a41ca39).
- Added: Grid on the CTRL page in every mode (a switch, saved); the record-position slider always shows (a41ca39).
- Removed: the phosphor strip, the spectrum strip and the spectrum header line. The spectrum's readouts are cards in
  the bottom row built like the scope's: Peak and Level per channel, RBW, and the waterfall's time span; the filter
  tag is no longer shown (a41ca39).
- Removed: Trace style and the 0.2.51 Display row from the acquisition menu, which holds acquisition only again
  (a41ca39).
- Changed: in the spectrum view T CURSOR, V CURSOR and MEASURES are empty slots; the right bar follows the mode
  (a41ca39).
- Changed: the warning line (overrange, no trigger, no channel, X-Y needs both channels) shows at the top middle of
  the plot in Normal and X-Y too, not only with Phosphor (a41ca39).
- Changed: the top bar chips CH1, CH2, ACQ and the trigger share one construction: a grey box with the coloured block
  over its left end, no notch (aaa0613).
- Settings sector: word 210 bit 3 holds the Phosphor switch, word 211 (0xC700) Grid and the spectrum's Display
  variant.

#### 0.2.55 - 2026-10-19 (protocol 1.16, unchanged)

Step 3 of the UX makeover (`docs/plans/ux-makeover.md` section 5; README section 5 "Settings").

- Changed: the Settings fold-out holds Screen brightness, Grid brightness, File message timeout (was "Notification
  confirmation"; the chip shows "tap" or "0.7 s"), Time and date (with a clock module), Baseline calibration (interim
  until Diagnostics has Calibrate) and Other settings, with contiguous 59 px hit areas (a4664ae, 66a3a70).
- Added: Other settings opens a full Settings screen: Display, Controls (file message timeout, cursors, auto setup),
  Files (screenshot stamp, reference traces at power-on), Clock (clock module, a time and date page), Start-up ("At
  power-on": Open firmware, Boot menu then open firmware, Boot menu then stock firmware) and Restore default settings
  behind a confirmation that says the calibration and the start-up choice are kept. DC shift trim sits there in an
  interim Calibration group until Diagnostics takes it (66a3a70).
- Removed: 1.00o5's Other settings menu with USB-Serial mode (the build forces CDC) and Dev mode (flag kept, back on
  the Diagnostics developer page later); FEL is no longer a start-up choice in the UI. The dev-mode exception to the
  boot-menu rule is gone (66a3a70).
- Changed: restoring default settings (and a settings sector that fails to load) no longer writes the boot-mode byte,
  and keeps USB in CDC mode (66a3a70).
- Changed: "Confirm to delete?" became the confirmation dialog: "Delete from the SD card?", Cancel left and Delete
  right in red, 160 x 44 px buttons 16 px apart (was NO / YES, 74 x 50 px, 11 px apart) (24743a7).

#### 0.2.54 - 2026-10-19 (protocol 1.16, unchanged)

Touch fix batch 1 (`docs/plans/touch-review.md` section c1; defects 1, 3, 4, 5, 8, 9, 10 and part of 19).

- Fixed: the CH1, CH2 and reference pointers at the left edge of the plot are drag handles. A drag that starts within
  a 44 px box around a pointer (x 0..43) moves it; the nearest switched-on pointer wins, a tie goes to the one drawn on
  top (CH1, CH2, ref1..ref8 draw in that order). A tap on a pointer does nothing (it used to change the time base).
  Pointers pinned at the top or bottom edge are grabbed where they are drawn (4bec0ff; geometry host-tested in
  test_touch_pick, 4a3753b).
- Fixed: the zero-line grab on the plot picks the nearest enabled channel (a disabled CH2 no longer blocks CH1; a tie
  goes to CH2), reads the touch-down point rather than the point after the first move, and no longer shifts the
  trigger point with the x component of a channel drag (4bec0ff).
- Fixed: dragging a channel moves the trigger level on it by the same amount, limited so both stay on the grid: the
  level keeps its voltage when the trace hits an edge. With the EXT trigger source the level no longer follows CH2
  (ca7372a).
- Fixed: the new auto setup with Invert on could put a trace position outside the grid (a wrapped subtraction or an
  unlimited addition) and loaded settings were not range-checked; both are limited to the drag range 6..396 now, so a
  channel can always be grabbed and the first drag no longer jumps (d94f09a).

#### 0.2.52 - 2026-10-19 (protocol 1.16, docs/protocol.md section 14 row 55)

- Added: GET_HEALTH (0x000A, answered in every context, also while a menu is open) reports the USB link state now
  (configured, suspended, not configured = no PC, high speed, port open), the CP15 control word (MMU and caches), the
  power-on count and operating time, how the previous power-on ended (settings saved at power-off or not), whether
  the settings changed since the last save, and a stored restart record (b830434).
- Added: the restart record (exception, PC, fault registers) is copied into the settings sector at the boot after a
  crash restart, so it survives a power cycle until it is cleared (GET_HEALTH flag 0x01) (b830434).
- Added: power-on count and operating minutes in spare settings words 224..255; the firmware writes sector 709 once
  per boot to keep them and the restart record (b830434).
- Added: every image carries an identity record at its end; SD_INFO flag 0x02 reads the version, git hash and commit
  time of the image the card boots (also for 0.2.47-0.2.51, and the version alone for older images) (b830434).
- Added: calibration provenance (firmware, FPGA word, uptime, power-on count and operating minutes at calibration;
  DS3231 time when fitted) after the calibration block in sector 708; GET_CAL is 356 bytes (b830434).
- Added: SAVE_REPORT (0x0504) writes `reports/report-<power-on>-<n>.txt` to the card with identity, counters, the
  decoded event log, the restart records and the calibration in words; the Diagnostics "Save report" button will
  call the same function (b830434).
- Added: PC_DEBUG builds restart from RAM on GET_HEALTH flag 0x80, to bench the stored restart record (c8f874b).

#### 0.2.51 - 2026-10-19 (protocol 1.15, unchanged)

The first step of the UX makeover (`docs/plans/ux-makeover.md` section 7 step 1 plus the measurement cards; rules in
`docs/ui-guidelines.md`; README section 5 "Screens").

- Changed: the main menu reads Scope, Spectrum analyser, Saved pictures, Saved waveforms, Settings, Diagnostics, USB
  drive, always all seven rows, with one contiguous 59 px hit area per row (04b6f2c). Phosphor display left the menu.
  The Spectrum analyser row is also offered in roll mode: it switches Roll off and says "Roll off for the spectrum"
  in the warning line; Scope does not switch Roll back on.
- Changed: the USB connection screen is "USB drive": three plain lines on what happens and an Exit button in place of
  the "Off USB" text; same behaviour (1027f90).
- Removed: the version string in the top bar; the diagnostics view keeps the identity (3c66d85).
- Changed: the phosphor view and X-Y mode are chosen in a Display row (Normal / Phosphor / X-Y) under the acquisition
  menu until the CTRL page's Display fold-out exists; X-Y left the System settings fold-out, whose Notification
  confirmation and RTC rows moved up one row (4d74f82). Phosphor drawing in X-Y is not offered until then.
- Removed: the "Scope" fields of the spectrum and phosphor strips and the spectrum strip's Auto field. In the spectrum
  view AUTO SET runs the spectrum's Auto, reads "Auto..." while it runs and cancels on a second tap (79b6b00, 7ff6c68).
- Changed: phosphor warnings sit at the top middle of the plot, as in the spectrum view (79b6b00).
- Changed: measurement cards keep Atlan's layout with the name in a chip built like the top bar chips (full card
  height, rounded outer left corners, no notch), names as scope makers spell them (Freq, Period, +Width, -Width,
  +Duty, -Duty instead of Freg, Cycle, Time+, Time-, Duty+, Duty-), four significant digits ("Freq 1.000kHz"), and
  "no period" instead of "xxx" for timing values without three crossings. A new unit shows Freq, Vpp and Vavg on each
  channel (2f1ab97; formatting and fit host-tested in test_ui, 33cc330).

#### 0.2.50 - 2026-10-19 (protocol 1.15; the fw-cal3 branch numbered it 1.14 / row 53, the merge renumbered it)

- Added: calibration v4, background tracking of the full-rate interleave mismatch. Every 8-bit 200 MSa/s record of a
  calibrated channel (UI or host) feeds a per-channel fit of core A - B = d + e x + q x^2 from neighbouring sample
  pairs on flat parts of the signal (edges and energy near fs/2 are rejected); d and e are applied symmetrically in
  the full-rate class (no per-sample cost, the mean of the cores and the filtered path unchanged), q is reported.
  A 1.7-code step falls under 0.1 code in about 37 s of 200 MSa/s use (7cfc7f7; maths and host test 260144d).
- Added: full-class common-mode tracking. A class probe (about 100 ms: gated, full-rate, gated conversions on the
  live input, front end untouched) runs per channel at most every 30 s while the scope or spectrum view runs with a
  quiet input and no host capture; the full-rate class (200 MSa/s and every filtered capture) is pulled back onto
  the stable gated zero (93087b2).
- Added: GET_CAL answers 288 bytes; bytes 84 and 92..107 report what the next readout applies, 256..287 the tracker
  and probe state (protocol row 54, 5f6bc0d). The diagnostics view shows "Track d .. e .. q .. age" and "Class cm
  .. age" per channel, grey until they ran (4cd60f7).

- Changed: the base calibration is one full-screen wizard (System settings > Baseline calibration), replacing the
  fold-out box and the separate gain prompt: step 1 zero (probes and USB unplugged, warns when a USB host is
  attached, progress per range, a tick or the reason per channel), step 2 gain one channel at a time with a level
  check first ("No signal on CH1: tip on the post?"), the CH2 gain measured at once when its probe is already on the
  post, step 3 a summary in plain words. Retry and Skip per step, Cancel at any step writes nothing, Done saves only
  what changed and shows the one success message. The trigger mode is restored after the calibration (it was left
  in Auto) and the channels' offsets are no longer zeroed. firmware/README.md section 6.4 (48e5737).

#### 0.2.47 - 2026-10-19 (protocol 1.13, unchanged)

- Fixed: reproducible image. The same commit built in two directories (or main and a worktree) gave different
  images: WSL's git cannot read a worktree's Windows gitdir, so the hash was 00000000 and the build time the wall
  clock, both compiled into the code as constants, and GCC's different code for them shifted the whole layout. The
  identity is now data (build_id.c, linked last), the commit hash and the commit time (UTC) from git, the worktree
  gitdir is translated for WSL, and the source directory is mapped out of the debug info: the .bin and the .elf
  are identical across directories (473bd0b, 57d0d26). The identity line reads "git <hash> of <commit time>"
  (was "built <time>"). CI prints the image sha256.
- Fixed: with the decimation filter on, the 16-bit trigger level now goes through the readout's correction (the
  trigger channel's gain16 and corr16), so the trigger sits on the level line; it was about 1 % off at full rate
  (04619ca).
- Removed: UI features that were hidden, now removed in code: the channel-menu FFT switch (2731e52), the
  generator Wave / Ampl / Offset / Phase rows, sub-menus and state (55960cf), Long memory (3a5ffc6), trigger
  hold-off (5906fbe). Settings sector and waveform file slots stay (written 0). The generator's top-bar
  frequency text is always orange.
- Removed: the `PC_PROTOCOL=0` build (8770209), `make check-reference`, `make package` and the Atlan reference
  download in CI (`tools/make_release_sd_package.py build` makes SD packages) (0f84761), and the calibration v2
  `adc_cal` section of docs/protocol-vectors.json (f09e381).

#### 0.2.48 - 2026-10-19 (protocol 1.13)

- Changed: the default acquisition Filter (new settings sectors and the DEFAULT button) is CIC2+FIR instead of CIC2, after the
  0x1535 alias bench (78-83 dB rejection with the FIR); on bitstreams without the FIR the setting acts as CIC2 (coordinator, pc_decim.h).

- Changed: the MMU is on, so the D-cache and the write buffer finally take effect (the C bit was set since 1.00o5 but
  does nothing with the MMU off). Flat 1:1 map: DDR and SRAM A write-back, the scanned framebuffer write-through,
  peripherals, boot ROM and the restart record strongly ordered. Caches are cleaned and switched off at every restart
  and before ENTER_FEL. `make gcc10-nommu` builds the same code with the MMU off (version suffix `-nommu`) for the A/B
  (8cdefda; table builder and host test d486c21; design docs/plans/mmu-dcache.md 6e36a89).
- Added: GET_LINK_STATS counters 58-59 (n = 60): fault status (DFSR / IFSR) and fault address (FAR) of the abort
  that caused the last restart (52fe65a).

#### 0.2.49 - 2026-10-19 (protocol 1.14)

- Fixed: the scope no longer stays lost to the PC after a long idle. On 2026-10-19 (0.2.48) the host suspended the
  device 17 times over 1 h 40 min with the port closed, the device never saw a RESUME interrupt (it has not on any
  build), and the host finally stopped listing it until the cable was replugged. A suspend watch in the 1 ms timer
  interrupt (`pc_usbw.c`, host test `test_usbw`) now treats SOFs, an EP0 setup or an EP2 OUT packet while suspended
  as a resume (0.2.44 only logged the SOF case, from the main loop), and re-enumerates a device the host abandoned:
  D+ pull-up off for 1 s and on again (soft reconnect), after a host's retry storm went quiet for 10 s (doubling
  back-off) or 15 min into a suspend, once per enumeration and doubling up to 8 h until an application opens the
  port again (c20b657, 46d9817, 46f3db2, e77785c).
- Added: GET_LINK_STATS counters 60-61 (n = 62): resumes inferred without a RESUME interrupt, soft reconnects;
  event type 20 (soft disconnect: reason, back-off level, suspends, seconds suspended); type 3 a bits 6-5 = what
  ended an inferred resume. Protocol 1.14, section 14 row 53 (3b50641); mcp decodes them (f3763cc).
- Changed: the CDC interrupt handler handles SUSPEND before RESUME when both are pending (46d9817).
- Fixed (docs): the USB selective suspend powercfg GUID in docs/agent-measurement-guide.md and the fnirsi-scope
  skill was wrong (53bb50f7e206 instead of 53bb8f07e226), so the documented command never changed the setting
  (46e473f).

#### 0.2.46 - 2026-10-19 (protocol 1.13)

- Added: digital phosphor view (main menu row 7 "Phosphor display"): the CH1 / CH2 traces are drawn from per-pixel
  hit counts of the recent records, graded by hit rate, in the normal scope view (grid, cursors, measurements and
  pointers unchanged). Strip under the grid: Persistence 0.2 s / 1 s / 5 s / Infinite, Palette Mono / Heat, Clear,
  Scope, and a wfm/s or warning field. XY mode supported. Settings in settings word 210 (15f15e0; map core and host
  test faca915, 04c60bc; design docs/plans/phosphor-design.md f46e1c5).
- Added: GET_STATUS byte 43 `display_mode` = 4 while the phosphor view is on; protocol 1.13, section 14 row 52
  (8be5f3a).

#### 0.2.45 - 2026-10-19

- Added: calibration data v3 with a gain per ADC core and clock class (c8f17bf). The base calibration
  now has a second step: with both probes on the calibration post, the generator holds the post low
  and then high, and the spans give each core's gain at full rate and gated. The gated mean stays the
  reference of the voltage calibration. 200 MSa/s, filtered (CIC / FIR) and legacy captures then read
  the same volts, and the A/B gain difference behind the full-rate fs/2 spur is corrected. The zero
  step also measures the offset slopes at full rate (c8f17bf).
- Changed: the readout multiplies each core by its gain (code = (raw x gain + offset + dither) >> 16),
  and the 16-bit path takes the full-class gain into its scale (c8f17bf). Same memory accesses per
  sample as 0.2.43.
- Changed: sector 708 block v3 (304 bytes), protocol 1.12, GET_CAL 256 bytes (docs/protocol.md
  section 14 row 51) (3aa3431, c8f17bf). v2 blocks of 0.2.43-0.2.44 are not read: run the base
  calibration again.
- Added: the gain step prompt after a passed zero step, gain failure reasons in the box, and gains,
  their range and lever in the diagnostics view (f5fdf72, c8f17bf).

Sections 0.2.8 to 0.2.44 were backfilled on 2026-10-19 from the git history, `firmware/AGENTS.md` (version
ladder, sections 9, 11, 18-22) and `docs/protocol.md` section 14. Dates are the commit date of the version
bump in the repository's local time (UTC+2); every bump below falls on 2026-10-18 in UTC as well. Where only
a handoff says something and no commit was found, the bullet says so.

#### 0.2.44 - 2026-10-18 (protocol 1.11)

- Added: support for FPGA bitstream 0x1535, the CIC compensation FIR (2e0cd10; protocol 1.11, section 14
  row 49). Caps bit 16, FPGA write 0x56, SET_DECIM flags bit 3 and GET_DECIM report the FIR. The acquisition
  Filter box on 0x1535 steps off, boxcar, boxcar+FIR, CIC2, CIC2+FIR, CIC3, CIC3+FIR, saved in settings word
  207 bit 5. The spectrum header tag shows the FIR only where it is active for that record. The FIR is active
  at 10 MSa/s and slower (N >= 16, N a multiple of 4). The bitstream was built and pinned (e2aec2e, 74fb8a3,
  bba392a) but not yet flashed, so the FIR path is host-tested only.
- Fixed: the spectrum view is no longer repainted over by the scope view's time base redraw, so the strip no
  longer flickers or gets overdrawn (9591d83). `scope_preset_values` now redraws the spectrum and returns, and
  the spectrum display has a re-entry guard.
- Changed: new settings default to the CIC2 filter (e09b25a). A saved settings word is kept, and the DEFAULT
  button restores CIC2.
- Changed: a USB suspend is now a state, not a link reset (45b9b9a; row 50, no protocol change). It no
  longer flushes TX/RX, clears DTR or drops the session. A RESUME ends it, and a bus reset or disconnect
  still resets the link. EnableSuspendM is cleared at enable. GET_LINK_STATS counter 10 no longer counts
  suspends. Event log types 1-4 carry the suspend state and durations, and the main loop logs a resume it
  infers from a moving frame number when no RESUME interrupt came. Why: on 0.2.43 Windows reported an error
  at the first write after about five minutes with the port closed. The bus ring showed suspend, bus reset,
  set-configuration, and the resume counter stayed 0 after 18 suspends (d562be9). Whether this cures that
  drop is not benched (handoff, firmware/AGENTS.md section 22).
- Fixed: the removed generator Wave row no longer takes touches on 0x1533 and later; the tap on the
  moved-up Freq row hit the Wave row's area (e0ee6f3). An audit of the generator, trigger, acquisition and
  channel menus found no other stale touch areas.
- Changed: the base calibration averages 64 full-rate conversions instead of 8, to average out the offset
  PWM ripple (3bfc892, merged in 5effb7d). The 0.2.43 calibration in sector 708 has the 8-conversion zeros,
  so recalibrate on 0.2.44 (docs/hardware-status.md).
- Bench: not benched as a whole at the time of the bump (firmware/AGENTS.md section 22). Loaded over FEL
  later that evening, census clean with inputs shorted (docs/hardware-status.md).

#### 0.2.43 - 2026-10-18 (protocol 1.10)

- Added: calibration data v2, the first per-core model (branch fw-cal, merged in b27c1ee; version bump
  80ab45d). The base calibration measures, per channel, hardware range and ADC core (A and B), the raw zero
  in two clock classes: full (ADCs at full rate, divider 0 and every filtered capture) and gated (the FPGA
  gates the encode clock for divider 1 and up, where the zero moves). It also measures the offset slope per
  core. Zeros are in 1/16 code, slopes Q16, stored as a CRC-32 block in sector 708 byte 128 and bound to
  the 709 PWM centres (e37487a, 29a9065, 6e0e1ee).
- Changed: the readout applies a per-core correction with a dithered fraction instead of one whole-code
  shift, without a division per sample (29a9065). The spectrum view's 16-bit path takes its zero from the
  same correction (7a254e9).
- Changed: GET_CAL grows to 248 bytes (protocol 1.10, section 14 row 48; 594752c, 218288d). Row 48 is
  superseded by calibration v3 in 0.2.45, and v2 blocks are no longer read.
- Added: the diagnostics screen shows the calibration data version, per-core zeros for both clock classes,
  noise, slopes and the correction in use, and says why a calibration failed (ab74d55). The base
  calibration saves after the channel settings are restored (a65d0b9). Nothing changes unless both
  channels pass.
- Bench: with shorted inputs the corrected core means read 127.94 / 127.87 at 1 MSa/s and 127.97 / 127.95 at
  10 kSa/s, so the zero model holds for legacy and filtered captures (d562be9, 2b8d7ab). A residual A/B
  difference that grows with the signal pointed at a per-core gain, which became v3 (81ee613).

#### 0.2.42 - 2026-10-18

- Changed: the spectrum view's Auto is a fixed procedure, independent of the current state (cebb982). It
  probes at 50 MSa/s with averaging, triggers at 50 % on the analysed channel, searches the period 10x slower
  per record until three periods fit, derives span and rate from the fundamental and the band top (RBW <= f0/4),
  confirms once and refines at most once.
- Changed: spectrum warnings state the consequence and the action. Units read MSa/s as in the top bar. The
  header no longer repeats the averaging (cebb982).
- Bench: loaded (f71fc3b). The owner's verdicts on Auto and the warnings were still open at the 0.2.44
  handoff (firmware/AGENTS.md section 20).

#### 0.2.41 - 2026-10-18

- Added: auto trigger mode in the firmware (0fd6a9c; protocol section 7.5). The FPGA's auto time-out counts
  samples and its trigger flag keeps watching after the stop, so at fast rates auto records came back flat
  but flagged as triggered. On Atlan bitstreams auto captures, UI and host, now arm in normal mode. After
  max(100 ms, two record lengths) the firmware writes 0x1A = 0 and reports the record untriggered. Calibration
  and auto setup keep the FPGA's own auto.
- Changed: the spectrum view drops an occasional untriggered record and warns on persistent ones. Zoom in
  steps the rate down when the span is at most fs/16 (0fd6a9c).
- Bench: the 0.2.40 problem case (2 of 6 flat "triggered" records at 10 MS/s) was fixed and verified (39eee13).

#### 0.2.40 - 2026-10-18

- Added: spectrum Average field: Off, 4, 16, 64 captures or Max hold, default 4 (e595a0f). It is an exponential
  dB average applied before drawing and in READ_SPECTRUM (flags bits 4-6), restarted when a bin's meaning
  changes. Averaging removed the window-leakage "wagging tail" of the CIC3 floor (20fe481).
- Changed: the spectrum header is one line with the peak per channel, RBW, Avg and the filter tag. Warnings
  (overrange, no anti-alias filter, stopped) are centred over the plot only while they apply. The acquisition
  menu shows only the "Roll mode" label, coloured by state (e595a0f).

#### 0.2.39 - 2026-10-18

- Changed: in the spectrum view the span drives the time base (11b1729). Zooming out past the whole axis
  steps the time base to the next faster rate, up to fs/2 = 25 MHz. Zooming in past 64 bins steps to the next
  slower rate and keeps the centre. The view waits for a capture at the new rate. Auto measures at 50 MS/s and
  picks the slowest rate whose fs/2 holds 1.25 times the band's top.
- Added: the spectrum FFT length, window, scale, reference level and averaging persist in settings words
  208 and 209. The display mode itself does not persist (11b1729).
- Changed: with the decimation filter on, the header shows a short "CIC3" tag instead of the N text (11b1729).

#### 0.2.38 - 2026-10-18

- Added: the pass time (the last main-loop pass before the menu opened) is shown under the acquisition menu's
  Memory header, so the cost of a memory depth is visible where it is chosen (6ce82d9).

#### 0.2.37 - 2026-10-18

- Changed: the dB conversion of the spectrum view is ARM assembly, 3 uncached accesses per bin instead of
  about 9, byte-identical to the C path under qemu (0f3e743). The handoff gives 4096 points as 10.96 ms run
  plus 1.44 ms dB in DDR and 2048 points as 2.30 ms in SRAM (firmware/AGENTS.md section 20; commit not
  identified).

#### 0.2.36 - 2026-10-18

- Changed: the spectrum axis is capped at 25 MHz (or fs/2), since the front end rolls off near 30 MHz (owner,
  8e1a946). READ_SPECTRUM still returns every bin.

#### 0.2.35 - 2026-10-18

- Changed: spectrum gestures on the plot. A short tap left or right of centre zooms the span out or in, and a
  drag pans the start frequency, with the scope view's thresholds (36149d1). The Span and Start fields give
  way to Auto and Reset. The FFT length is capped at 4096, default 2048.
- Changed: the density view keeps a count and the last-hit capture index per cell and decays at hit and
  draw time, with an intensity ramp (black, dark blue, red, yellow, white) and a rare / frequent legend. No
  fading pass sweeps the screen any more, which removes the visible band (36149d1).

#### 0.2.34 - 2026-10-18

- Changed: with the 0x1534 decimation filter on, the spectrum view transforms the filter's 16-bit records
  (`fx_fft_run_u16`, host-tested to N = 8192) at the rate 200 MHz / N and names the filter instead of the
  aliasing warning (1a9a773). READ_SPECTRUM flags bit 1 and bits 2-3 say which record and filter order.
  On 0x1533 and earlier it behaves as 0.2.33.

#### 0.2.33 - 2026-10-18

- Changed: the spectrum view runs FFTs up to 2048 points from on-chip SRAM A at 0x1000, the same space
  FFT_BENCH borrows (d125a41). On 0.2.31, 2048 points took 2.3 ms in SRAM against 4.9 ms in DDR.
- Bench: the waterfall of 0.2.32 was accepted on this build (e396c3b).

#### 0.2.32 - 2026-10-18

- Added: a View field in the spectrum screen switches between Spectrum, Waterfall and Density (83aa5b5).
  Waterfall draws one RGB565 row per new capture, newest at the top, with a legend and time span. Density
  counts hits along each spectrum line. Histories clear when the axes change and say so in STOP.
  GET_STATUS byte 43 `display_mode` reports 2 or 3. The trace area is cleared with memset bursts.

#### 0.2.31 - 2026-10-18

- Changed: the FFT hot loops (load and first stage, stages, split) are ARM assembly with everything in
  registers and LDRD/STRD/STM pairs, from a per-plan twiddle table (0930c72). Each has a C twin, and
  `make test-arm` runs the GCC 10 build under qemu-arm against numpy and compares byte for byte. The motive
  was the roughly 90 cycles per data access measured with the MMU and D-cache off. FFT_BENCH ran 2.2 to 2.5
  times faster, 4096 points in 10.9 ms (81ff205, firmware/AGENTS.md section 8).
- Bench: clean on FPGA 0x1533, and the owner accepted the spectrum view (f6d4b26).

#### 0.2.30 - 2026-10-18 (protocol 1.9)

Version 0.2.29 was never set in a commit: the version string went from 0.2.28 to 0.2.30 when the decimation
branch merged (a45e500). Protocol 1.8 has no row in `docs/protocol.md` section 14 and no commit that set it
was found (commit not identified).

- Added: decimation filter support for FPGA bitstream 0x1534 (b770356; protocol 1.9, section 14 row 47,
  bitstream built in 6d4c8ad). The FPGA offers a boxcar, CIC2 or CIC3 filter in front of the sample memory,
  with 16-bit samples (writes 0x53-0x55, read 0x29), caps bit 15. New commands GET_DECIM and SET_DECIM. The
  UI gets an acquisition **Filter** setting (off, boxcar, CIC2, CIC3), saved in settings word 207 and shown
  only on 0x1534 and later. It applies to UI acquisitions and host ARM. Calibration and auto setup stay
  legacy. Memory depth with the filter on is at most 8190 samples.
- Changed: the readout converts the 16-bit samples into the 8-bit trace domain, so the screen, the
  measurements and the software trigger work unchanged. READ_TRACE can serve the raw 16-bit buffer. The
  EXT trigger latency is compensated by a shift of pretrigger words (b770356).
- Docs: the protocol row and vectors (5702f7a), README and AGENTS sections (66bd6b4).
- Bench: merged unbenched (180d152). The bitstream was flashed later that evening, and the decimation bench
  (spur gone, alias rejection, noise) ran on 0.2.34 (1eb9800, fbab588, 7eaac49).

#### 0.2.28 - 2026-10-18 (protocol 1.7)

- Added: the on-scope spectrum view, main menu row 6 "Spectrum analyzer", hidden in roll mode (e6a7c4c;
  number formatting in bed2dd4). It switches the trace area to the spectrum of each new capture, UI or host:
  a fixed-point FFT of CH1, CH2 or both, levels in dBV from the calibration chain, peak readout, RBW and
  sample rate, and a tap strip for FFT length, window, dB/div, reference level, span, start and the way
  back. It is a display mode, not a modal menu, so the protocol, RUN/STOP and the top bar keep working.
- Added: READ_SPECTRUM 0x020A and GET_STATUS byte 43 `display_mode` (protocol 1.7, section 14 row 45;
  host side 7f364a8).
- Bench: the owner confirmed it works, with harmonics within 0.4 dB of theory (99f3ccb, firmware/AGENTS.md
  section 8).

#### 0.2.27 - 2026-10-18 (protocol 1.6)

- Added: FFT_BENCH 0x0F02 (efc5170; protocol 1.6, section 14 row 44). It times the on-scope FFT on the
  CH1 trace buffer for N = 1024 to 16384 in DDR and in SRAM A. The fixed-point FFT core `fx_fft.c` came in
  6edb719, host-tested against numpy. The first bench measured 4096 points in 24.3 ms in DDR (firmware/AGENTS.md
  section 8).
- Bench: clean, and the 0.2.26 channel menus were accepted (7e056a6).

#### 0.2.26 - 2026-10-18

- Changed: the channel menu's dead FFT switch is hidden, and Invert, Coupling and V/A move up one row
  (bf0651b). 1.00o5's "Open FFT" stored a flag and drew nothing.

#### 0.2.25 - 2026-10-18

- Fixed: the USB PHY tuning writes now reach the PHY (85a113d). 1.00o5 wrote the RES45 calibration, TX
  amplitude and disconnect threshold to 0x01C13410, the PHYCTL offset of later Allwinner chips. On the
  F1C100s PHYCTL is 0x01C13404 (Linux phy-sun4i-usb and suniv-f1c100s.dtsi). Every earlier image therefore
  ran the PHY at its defaults. This is the fix for the day-long USB drop: cold boots from the card lost the
  EP2 IN pipe within minutes on 0.2.20, 0.2.21 and 0.2.23.
- Added: event type 19, the post-init USB state with the PHY tuning read back (85a113d).
- Bench: FEL load clean (300 x 64 KiB, 0 failed), and the PHY read-back matches the written values (93346b4).
  A cold boot from the card matched the FEL state (ddced9f). A 30-minute cold-boot capture soak ran 35,728
  requests with no failure, drop or restart (02cd58b). 0.2.25 stayed on the SD card for the rest of the
  evening.

#### 0.2.24 - 2026-10-18

- Added: GET_EVENT_LOG appends a 16-entry ring of bus-level events (types 1-4, 7, 8, 12, 13, 15-18), so EP0
  setups and DTR edges cannot evict them (7e5a2e3; section 14 row 42). After four bench runs the 128-entry
  log had held only 107 EP0 setups and 21 DTR edges.
- Changed: an EP0 setup identical to the last logged one is logged at most once per second, since pyserial
  repeats GET_LINE_CODING about five times per open. Counter 39 still counts all (7e5a2e3).

#### 0.2.23 - 2026-10-18

- Added: after every TX abort the firmware logs the EP2 IN state: event type 17 (POWER, TXCSR) and type 18
  (EP2 TX interrupt enabled / ZLP pending, frame number) (25e90fc; row 42). They tell "host stopped taking IN
  data" from "device stopped loading".
- Bench: replug test clean (400 x 64 KiB) (e031838). A cold boot with the cable in caught one TX abort with
  the new diagnostics (72fc79e).

#### 0.2.22 - 2026-10-18

- Changed: `usb_device_init` asserts the USB OTG bus reset and the PHY reset for 1 ms before 1.00o5 releases
  them, and logs the controller state it found as five event type 16 entries (7b6d855; section 14 row 43).
  Up to 0.2.21 the resets were only released, so an SD boot configured whatever the bootloader left.
- Bench: FEL bench clean (1b2850d). The cold-boot failure was not fixed yet (cure in 0.2.25).

#### 0.2.21 - 2026-10-18

- Removed: the USB timer-tick fallback (1a893a9). The 0.2.20 bench and a 30-minute soak showed counter 47 = 0
  and counter 57 = 2176: 1.00o5's interrupt dispatcher never lost a USB interrupt, and the fallback had only
  raced the real interrupt. Counters 47 and 57 stay as retired slots that read 0 (n stays 58), event type 14
  is no longer emitted, and the `-nofb` build target is gone. The 2 ms RX sweep, the TX pump kick, the event
  log, the snapshot and the restart record stay (section 14 rows 38 and 42).
- Changed: a host test checks that the ISR and BUSY polls make no FPGA bus access (6007bb0).
- Bench: on FPGA 0x1533, the FEL load was clean over 4 passes, but a cold boot from the card lost EP2 IN
  within two minutes (d69552c, b74974e). The cause was found in 0.2.25.

#### 0.2.20 - 2026-10-18

- Changed: the tick fallback serves only an endpoint state seen on two consecutive ticks, skips any tick at
  which the USB interrupt is pending, and counts those ticks as races in GET_LINK_STATS counter 57, so
  n = 58 (7f1c0c0; rows 38 and 42; counter named on the host in 929ef53). A `-nofb` build was also made.
- Bench: counter 47 = 0 and counter 57 = 59 in about 90 s. The "lost interrupts" of 0.2.12-0.2.19 were
  races with the real ISR (ac9a5da, e9c9bb1). The 30-minute soak was clean and 0.2.20 was persisted to the
  card (d7cf366).

#### 0.2.19 - 2026-10-18

- Added: the restart record. `_reset` stores LR, SP and CPSR at 0x81A00000, and the undefined, SWI, prefetch
  abort, data abort, reserved and FIQ vectors store their type and registers and then restart instead of
  spinning (55d5c5b). A restart is counted at boot, logged as event type 15, and shown in GET_LINK_STATS
  counters 49-56 (n = 57). ENTER_FEL clears the record. This was built to diagnose the one self-restart seen
  on 0.2.16.
- Added: `make gcc10-nofb` builds an A/B image `0.2.19-nofb` without the USB tick fallback (02288b8). The
  switch was removed in 0.2.21.
- Bench: the A/B pair ran 8 clean passes and a capture soak (04322c3). The 0.2.19 soak was clean (ac9a5da).
  The 0.2.16 restart was not reproduced (handoff, firmware/AGENTS.md section 9).

#### 0.2.18 - 2026-10-18

- Changed: the event-log reset snapshot is re-armed only when GET_EVENT_LOG is sent with request flag bit 0
  (50c9802). Before, every read re-armed it, so a host that reads the log at session open threw the
  evidence away. Other flag bits answer E_BAD_ARG.
- Bench: built and host-tested, not benched alone.

#### 0.2.17 - 2026-10-18

- Changed: quiet boot. Without `scope.bmp` on the card the boot no longer shows a "Failed to open file" box
  for 700 ms; it probes for the file silently and goes on (0c13045). With the file it behaves as 1.00o5.
- Added: GET_LINK_STATS counter 48 = milliseconds spent in that step + 1, so n = 49 (0c13045).
- Bench: owner verdict pending at the time (firmware/AGENTS.md section 8).

#### 0.2.16 - 2026-10-18

- Fixed: the USB tick fallback ran the whole CDC handler for the SOF flag, 300-500 times a second (079f648).
  The SOF flag latches every frame with its interrupt disabled. The 1.00o5 handler is split into an entry and
  a body, and the tick runs the body only for a flag other than SOF.
- Bench: 300 x 64 KiB clean, against one loss in 100 on 0.2.12 and 0.2.14. Under repeated bench traffic the
  device restarted itself once (uptime reset, version unchanged), so the lead went back to 0.2.11 (558b347).
  No reproduction is recorded.

#### 0.2.15 - 2026-10-18

- Changed: the event log ignores SOF for counter 47 and event type 14, logs type 14 at most once per 100 ms,
  grows to 128 entries and appends a frozen snapshot of the 16 events before the first bus reset
  (c4989e5; section 14 row 42). On the 0.2.12 soak the 64-entry ring had held only type 14 events at about
  1000 a second and lost everything before the reset.
- Bench: superseded by 0.2.16 before it was benched, not pinned.

#### 0.2.14 - 2026-10-18

- Changed: the tick fallback serves latched USB flags only after two consecutive ticks without a real USB
  interrupt (d6a6b37). On 0.2.12 the tick had won the dispatch against the real IRQ during traffic, so
  counter 47 (46,033 in 3 minutes) measured that race.
- Docs: EXT trigger ground note. The probe ground sits about 1.24 V above the scope's USB ground, so a
  source on the probe ground reads a low level 1.24 V high; use a series LED and the rising edge (d6a6b37,
  28126aa).
- Bench: counter 47 = 11.8k in 2 minutes because SOF defeats the gate, and 1 in 100 64 KiB responses lost
  (c0614c4). Fixed in 0.2.16.

#### 0.2.13 - 2026-10-18

- Changed: System settings without a DS3231 hide the Time & Date row, and the box is one 59 px row shorter
  (7ebda04). The 0.2.6 mask for that row is gone.
- Fixed: the battery indicator no longer flaps its last green column (7ebda04). The protocol build averages
  the level (1/8 per pass) and, while charging, changes the shown level only when the average moves 1.25
  steps away. Off the charger the 1.00o5 drop-only rule applies to the average.
- Bench: not benched alone, owner verdicts pending at the time (firmware/AGENTS.md section 8).

#### 0.2.12 - 2026-10-18 (protocol 1.5)

- Added: GET_EVENT_LOG 0x0009, the last 64 USB and link events with millisecond timestamps (bus resets,
  suspends, resumes, disconnects, EP0 setups, SET_CONFIGURATION, CLEAR_FEATURE, DTR, RX stall and release,
  TX flush and abort) (e797f25; protocol 1.5, section 14 row 42), answered from any context.
- Added: the 1 ms timer poll also runs the CDC interrupt handler, a fallback for a USB interrupt thought
  lost (e797f25). GET_LINK_STATS counter 47 counts the polls that found flags (n = 48). It was added
  because Windows dropped the device three times (idle with the port closed on 0.2.2, during an SD write on
  0.2.3, during 10 Hz status polling on 0.2.11), each time after one host port reset. Removed in 0.2.21.
- Bench: counter 47 = 46,033 in 3 minutes (a race with the real ISR) and 2 drops in the soak (2140216,
  c0614c4). The "restart" first attributed to this build was withdrawn by the lead (e9c9bb1).

#### 0.2.11 - 2026-10-18

- Fixed: a request sent right after the port is opened is no longer dropped without an answer (1594918).
  The DTR 0 to 1 edge was handled only in the main poll, after the 1 ms timer poll had parsed the host's first
  request, so the DTR-up handling reset the parser and discarded it. Link events are now taken at the start
  of every poll, before new bytes are parsed. The bug dated from 0.1.1.
- Bench: accepted, census clean and 20 of 20 open-then-request (be4d51d, 7a51822). Persisted to the SD card
  in 5.7 s (01fd27f). One USB drop was seen during 10 Hz status polling (57e8018). A clean 30-minute baseline
  soak followed (8a946e6).

#### 0.2.10 - 2026-10-18

- Fixed: the MCP chip no longer flickers now and then while the main loop runs (6d4adfe). It is rendered
  once into a 38x17 pixel array and drawn as one blit of final pixel values, and the clock slot is cleared
  only when the chip goes on or off.
- Changed: the acquisition menu's "ACQ / Long mode" box reads "Roll mode on/off", green when the long time
  base is on, because Atlan's label named the mode a tap switches to and looked dead (6d4adfe).
- Bench: loaded (ba884cd).

#### 0.2.9 - 2026-10-18 (protocol 1.4)

- Added: external trigger on the calibration pin for FPGA bitstream 0x1533 (bebf901; protocol 1.4, section 14
  row 41; bitstream and testbench 526c969, 4601dec). SET_TRIGGER source 2 = EXT, caps bit 3, and GET_STATUS
  byte 42 `ext_flags` with the live level, generator state and edge seen since arm. GET_CAPTURE_INFO flags bit 5
  says an EXT edge was seen. The AL3 family is decoded as 0x1530 to 0x153F from 0x1532 up.
- Changed: selecting EXT switches the generator off, since both use the same pin, and SET_GEN enable while
  the source is EXT answers E_STATE. The UI's EXT button appears only with the cap. The software trigger
  refinement is skipped for EXT (bebf901).
- Bench: loaded before the 0x1533 bitstream existed on the unit (b143db3). The 0x1533 bitstream was flashed later
  that day (76ea6b0) and EXT worked in a loopback test (b45734c).

#### 0.2.8 - 2026-10-18

- Changed: the generator menu is compacted to three rows (Power, Freq, Duty) and the trigger menu to five,
  removing the gaps that the 0.2.6 masks left (0ff64c6). The menu heights, screen save and restore and
  touch bounds follow. The 0.2.6 masks for those rows are gone.
- Bench: loaded; the first request after the FEL load timed out, which was the dropped-request bug fixed in
  0.2.11 (b143db3, 1594918).

#### 0.2.7 - 2026-10-18

- Changed: SD_STAGE is served from the 1 ms timer poll instead of one request per UI pass (d88e9ea).
  On 0.2.3 a 64 KiB stage took about 5.8 s.
- Added: six RX back-pressure counters in GET_LINK_STATS, 47 counters in all (d88e9ea).
- Added: our version is shown on the main screen, the restore screen and the diagnostics screen
  (d88e9ea). The diagnostics screen also shows an identity line with fw, protocol, base, git hash and
  build time.
- Fixed: the MCP chip is part of the top-bar redraw, so it no longer blinks off (d88e9ea).
- Changed: the diagnostics temperature is shown only when a DS3231 answered at boot (d88e9ea).
- Docs: the 1.5 s startup gap is the boot splash (0aea00f).

#### 0.2.6 - 2026-10-18

- Changed: the UI hides what the hardware cannot do (68f7868). This covers the generator Wave, Ampl,
  Offset and Phase rows (there is no DAC), and memory depths above the bitstream's maximum. Long memory,
  the EXT trigger and hold-off are hidden too. Time & Date is hidden when no DS3231 answers.
- Changed: saved settings are fixed up at boot to match (68f7868).
- Bench: census clean on 0.2.6. Visual check of the hidden items is pending (4db7576).

#### 0.2.5 - 2026-10-18

- Fixed: EP2 OUT is unmasked again after a USB suspend, reset or disconnect (5086f7d). RX used to fall
  back to the 2 ms poll sweep after a resume.
- Added: six USB device event counters in GET_LINK_STATS (5086f7d). They count bus resets, suspends,
  resumes, disconnects, EP0 setups and malformed EP0 setups.
- Bench: the idle USB fault seen on 0.2.2 was not reproduced (0ddd08c).

#### 0.2.4 - 2026-10-18

- Added: first-pass stage marks in GET_LINK_STATS, counters 31-34 (b05cdbd). They showed that the 1.5 s
  startup delay is in the init path, not in the first UI pass (5ee4533).

#### 0.2.3 - 2026-10-18

- Changed: the MCP chip sits above RUN/STOP and stays steady (baee5b5).
- Fixed: SET_TIMEBASE applies in one step, without flicker through intermediate values (baee5b5).

#### 0.2.2 - 2026-10-18

- Added: SD sector commands SD_INFO, SD_READ_SECTORS, SD_STAGE and SD_WRITE_SECTORS (0d4d9bb). Writes
  are limited to a window below the first partition. Sectors 16-79 and 708-710 need explicit flags.
- Added: first-pass startup counters (0d4d9bb).
- Bench: the first persist of 0.2.3 to sector 750 failed mid-write and the second succeeded (a7fcf11).

#### 0.2.1 - 2026-10-18

- Fixed: READ_TRACE clips a count past the end of the trace (d9e1ac6).

#### 0.2.0 - 2026-10-18

- Added: protocol 1.3, shared control with the UI (d77298f). There is no LOCAL/REMOTE and no lock.
  It brings settings_seq, RUN/STOP/SINGLE, host single captures shown on screen and the MCP chip.
- Bench: collaborative mode was accepted interactively on 0.2.1 (ecfd226).

#### 0.1.7 - 2026-10-18

- Changed: wrap-safe RX ring helpers, with an all-phase ring test (5de512e).
- Bench: first image persisted to the SD card and booted after a power cycle (9164b18, 30e8958).

#### 0.1.6 - 2026-10-18

- Fixed: each OUT packet is read in one FIFO access run (697e1f6). This fixed request corruption at the
  2 KiB RX ring wrap. A census of 2000 PINGs and 100 x 64 KiB had no errors (3c7b623).

#### 0.1.5 - 2026-10-18

- Added: request-corruption diagnostics GET_BAD_FRAME, the `PC_FIFO_MAP` alternative layout (`-m1`
  builds) and a census mode in `bench_link.py` (c31bd01).

#### 0.1.4 - 2026-10-18

- Added: protocol 1.2 GET_LINK_STATS counters and an RX sweep for lost USB interrupts (aabe64a).

#### 0.1.3 - 2026-10-18

- Fixed: the CDC FIFOs no longer overlap the fixed EP0 FIFO at bytes 0-63 (d0ae338).

#### 0.1.2 - 2026-10-18

- Changed: safe requests are answered from the 1 ms timer IRQ, so PING takes about 1 ms instead of
  about 45 ms (afaee38, e3c5bf8).
- Fixed: EP2 is flushed on a bus reset (afaee38).

#### 0.1.1 - 2026-10-18

- Fixed: CDC FIFO layout inside the 2 KiB FIFO RAM (5a8432b). Large responses had stalled and 64 KiB
  failed on Windows.
- Added: TX recovery, ENTER_FEL and protocol 1.1 main-loop pass times in GET_STATUS (5a8432b).

#### 0.1.0 - 2026-10-18

- Added: the USB control protocol, with frame layer, command layer behind a hardware abstraction layer,
  CDC transport and shared test vectors (9ffd7ba, c5f41c8, 4aa239c). It is enabled by default (193e447).
- Added: Atlan4 v1.00o5 scope source with a plain Makefile and a byte-identical reference build
  (2633b3f).
- Bench: enumeration, identify, REMOTE, readout, front end, timebase, trigger and generator checks
  passed on hardware (0b9c783, a7579d5, a4577f3, f7bd2bf, f9f9349).

### Host library and MCP server (`mcp/`, package version 0.1.0)

- Added: protocol 1.22 (fw 0.2.72): GET_LINK_STATS counters 64-67, `EventLog.fpga_trace` and
  `Scope.event_log(fpga_trace=...)`, `scope_event_log(fpga_trace=...)`, `ext_shift_legacy`; the simulator models the
  ADC pipeline behind a legacy EXT trigger, the trace switch and an exact READ_TRACE (d6de414); 0x1538+ without
  INTERLEAVE_UNVERIFIED (267c1be).
- Added: protocol v1 frame codec, serial/TCP transport and device simulator (0d8b40f, cc1c908, d888fb3).
- Added: high-level client, analysis, FastMCP server and tests (8c80c1e). Also an in-process
  simulator (`--sim`) and the README (a60d35a).
- Fixed: Windows stdio deadlock on the first scipy import (6f43acd).
- Fixed: Windows serial stalls and corruption on large responses (c265992). The port is now never
  reconfigured after open, and reads are driven by `in_waiting`.
- Added: protocol 1.1 support and link robustness work (55f373e).
- Added: protocol 1.2 support (20b4d8b).
- Added: `fnirsi1013d-flash` FEL dev loop with per-command timeout classes (136eb1b), and `--sha256`
  (20b4d8b). ENTER_FEL is retried (8c28543, 0ade410).
- Added: `fnirsi1013d-bench` (8c28543).
- Added: collaborative mode, protocol 1.3, on the host side, in the simulator and in the vectors (f88c9e3).
  Follow-ups: 319904a, 4568913, 51ddb13.
- Removed: the provisional 8 KiB Windows response cap (19ac919).
- Added: per-unit, per-core voltage calibration with `scope_calibrate_volts` and `scope_calibration_info`
  (bf4a4bb, a66353e, a9232a5).
- Added: SD sector commands with `fnirsi1013d-flash --persist` and `--restore` (9e79583). The SD API in
  `Scope` came next, with the `fnirsi1013d-sd` CLI, `scope_sd_info` and a version check before a persist
  (c4b9735). A persist resumes after a USB drop (83b20c9).
- Changed: GET_LINK_STATS names for the fw 0.2.4 and 0.2.5 counters (83b20c9, cf77969).
- Added: agent measurement guide, the fnirsi-scope skill, and Codex and user-scope MCP setup (dc7f4b1).
- Added: protocol 1.7 support (7f364a8). `scope_spectrum` reads the scope's spectrum view, and
  `cross_check` recomputes it on the host from the same capture. `scope_status` shows the display mode.
  `scope_event_log` and the soak tool decode the bus-event ring and event types 16-19.
- Added: `fnirsi1013d-bench --fft` runs FFT_BENCH from the package (d1fe2e9).
- Added: protocol 1.9 decimation filter (0de32dd). `scope_set_decimation` sets it. Captures read the
  16-bit samples, and the simulator has a 0x1534 variant. The measurement guide explains choosing a filter
  for decay curves.
- Added: protocol 1.10 calibration v2 (f2d8008). The volts model uses the scope's per-core zeros for each
  clock class, and `scope_calibration_info` shows them.
- Added: protocol 1.11 compensation FIR (caab0eb). The `+fir` filter choices drive it, and event types 1-4
  carry the USB suspend fields. `fnirsi1013d-soak --mode reopen` runs the suspend / resume bench.
- Changed: protocol 1.12 calibration v3 (62da1e0). The volts model applies the scope's per-core, per-class
  zeros and gains on every read path, and `scope_calibration_info` shows the gains. Calibration v2 is refused.
- Added: protocol 1.13 display modes (8b6a736). `scope_status` names the waterfall, density and phosphor views,
  and the capture tools add a caveat while the phosphor view is on.
- Added: host-driven Bode sweep, `scope_bode_sweep` (tier 3) and `fnirsi1013d-bode` (docs/plans/bode-design.md
  steps H1-H3). Clean-edge tones 50 MHz / M, coherent 16-bit CIC3 + FIR records, single-bin DFT; magnitude, phase,
  markers and Q, quality flags, background floor, CH2 auto-ranging with the splice, thru calibration, CSV / JSON /
  NPZ. Planner, DFT and markers in f0940ee (plan vectors docs/bode-plan-vectors.json), runner and CLI in 363f676,
  the tool in d3a16bb. The simulator has an RC / series RLC DUT on the generator (1d46cf8). Not benched yet.
- Added: protocol 1.15 calibration v4 (fw 0.2.50, 5f6bc0d). GET_CAL parses 288 bytes; the volts model takes the
  tracked full-rate A - B (d, e) and common mode (cm) out of READ_RAW and 16-bit samples as the scope does in its own
  readouts, re-reading GET_CAL at most a second old; `scope_calibration_info` shows the tracker and the class probe in
  words (tracking / frozen / idle, age, d, e, q, cm). The simulator models the drift (`SimConfig.v4`).
- Added: protocol 1.16 diagnostics readers (fw 0.2.52, b830434; protocol row 55, 8614032). `scope_health` (GET_HEALTH: USB state,
  CP15, unit life, settings state, the stored restart record, in words), `scope_sd_info` reports the card image's
  identity and whether it is the running build, `scope_calibration_info` the calibration provenance, and
  `scope_save_report` has the scope write its report file to the card. Codecs for the stored layouts are checked
  against the `diag_layouts` vectors; the simulator answers all four.
- Changed: the package works installed from the wheel, outside a checkout (8ea8886). The server's data dir and
  audit log default to the per-user `~/.fnirsi1013d` (`FNIRSI_MCP_HOME` or `--home`; the specific variables and
  arguments still win) instead of a directory next to the package. The flash image, SD backup dir, `bench_link.py`
  and vectors file default to the checkout's only when running from one; otherwise `--image`, `--backup-dir`,
  `--bench-link` and `--file` are required and their absence is reported before the scope is touched.
  `fnirsi1013d-sd write` gains `--backup-dir`. `fnirsi1013d-mcp --list-tools` prints the tools, the data dir and the
  audit log without a scope.
- Added: CI job `wheel` (0e1d36b): the wheel, built as for a release, installed into a fresh venv on Linux and
  Windows, then `--list-tools` and an MCP stdio session on the simulator (`.github/scripts/wheel_smoke.py`).
- Changed: install and Claude Code / Codex setup for a wheel user, `.mcp.json.example` in place of the tracked
  `.mcp.json` (ef8aa30).
- Fixed: Bode sweep with CH2 saturated by the DUT's DC level (bench 2026-10-19: wiring B puts the DUT 1.24 V below the
  probe reference; on 0.1 V/div the record was flat and centred, and the sweep reported empty points). The point is
  now flagged `ch2_overload`, auto ranging steps CH2 back up, a fixed range stops with the remedy, `ch2_coupling='ac'`
  (`--ch2-coupling ac`) is new, and an offset that runs out of PWM range is a caveat instead of an exception. Wiring A
  (DUT ground on the probe clips) is the documented default after the bench showed both wirings within 0.4 dB
  (416f536).
- Added: protocol 1.17 host side (fw 0.2.58, FPGA 0x1536; 4699593, docs 6bd09aa). `scope_capture_segments` takes many
  triggers in one FPGA run with 5 ns timestamps (circular runs, STOP keeps what the run has), `scope_averaged_capture`
  gains `hardware=true` (records summed in the FPGA, read as exact sums), `scope_status` shows the bitstream's features
  and the UI's Average. The client decodes GET_STATUS 48, GET_DECIM 36 and GET_CAPTURE_INFO 96 bytes and the sequencer
  commands; the simulator has a 0x1536 variant (`al3s`). Not yet run on the unit.
- Added: protocol 1.19 host side (fw 0.2.62, FPGA 0x1538; 81a1a31). `scope_health` gains `encode_check` (GET_HEALTH
  104 bytes: how 200 MSa/s captures are checked for a garbled ADC core, the last 0x33 byte and its failing pins,
  captures checked / garbled / re-taken), capture tools add a caveat while the last full-rate capture was accepted
  garbled, `scope_event_log` decodes event types 21 and 22 in words, `scope_status` lists the `enc_check` feature.
  Simulator variant `al3e` (0x1538).
- Added: protocol 1.20 host side, the on-device Bode sweep (fw 0.2.66; 0a8e94d). `scope_bode_sweep(where='device')`
  lets the scope run the sweep and show its Bode plot, polls READ_BODE and returns the same table, markers and files
  as the host sweep (flags mapped to the same names, f recomputed from the DDS period), with the device's thru
  (`thru_cal='device'`) and `close_view`. Capture tools caveat the Bode view; `scope_status` shows display mode
  `bode` and the `bode` feature. The simulator runs device sweeps with the host's engine, so device and host agree
  there within 0.02 dB / 0.2 deg.
- Added: `scope_report` and `fnirsi1013d.report` (1a3c0aa). A saved result (capture, averaged capture, decay fit,
  segments, FFT, the scope's spectrum, a host or device Bode sweep) becomes a PNG plot and a self-contained HTML page
  (plot, table, caveats, the generating command; no scripts or CDNs) under `~/.fnirsi1013d/reports/`. Matplotlib is
  the optional extra `plot`; without it the page carries the numbers. The guide and the skill tell agents how to
  show them (section 7a "Presenting results").
- Added: protocol 1.21 host side, CAL_PROBE (fw 0.2.67, provisional numbering; 516b76a, b3b9c51). `scope_cal_probe`
  (tier 2, diagnostic) runs the calibration wizard's zero measurement of one channel and range (or the whole zero
  step) on the scope and returns the verdict, the failing point and per point and core the mean, RMS, extremes and
  clipped samples; the simulator models the wizard's points with the CLIP and NOISE checks.

### Tooling, FPGA and documentation

- Added: `firmware/tools/make_sd_image.py` and `mksunxi.py` (2633b3f). They write only the parts you pass
  and never zero sectors 708-710.
- Added: SPI flash tools for the FT2232D, `spiflash_d2xx.py` and `spiflash_ftdi.py` (71774ff, 108df11, fee75d9).
- Added: T8 calibration bench helpers (b712f6c, 133767f, 27e368e).
- Added: AL3 FPGA project sources and a headless Anlogic TD flow. The flow rebuilds Atlan4's 0x1532
  bitstream byte-identical (e790a7d, 42c79dc).
- Fixed: FPGA 0x1539 (b4bd8b0): sample bytes read back near code 128, and across any code boundary where several
  bits change, could come back as a mix of two successive bytes on some ADC cores (0x1532..0x1538: the FPGA changed
  the byte on the clock edge right before the MCU read it, about 10 ns too late). The byte now changes at the start
  of each bus strobe. No firmware change; calibrations taken on 0x1537 / 0x1538 should be redone.
- Added: protocol specification, MCP tool surface, hardware preparation guide and roadmap (9d32933,
  1bd4179, a947e78).
- Added: CI and release workflows, `tools/make_release_sd_package.py` and `docs/releasing.md`.
