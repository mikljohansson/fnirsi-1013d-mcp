# UI guidelines for the FNIRSI 1013D open firmware

The rules every screen follows. They were settled with the owner on 2026-10-19 while designing the UX makeover
(`docs/plans/ux-makeover.md` is the change list; the mockups are the Design artifact linked from
`docs/plans/task-queue.md`; the adversarial reviews that shaped them are `docs/plans/diagnostics-review.md` and
`docs/plans/ux-review.md`). An agent touching `firmware/scope/menu.c`, `statemachine.c`, a view or a dialog reads this
first. Facts about the panel and the fonts are in `firmware/README.md`.

## 1. Vocabulary

- **Mode**: changes what is measured and which controls exist. Scope, Spectrum analyser, Frequency response (fw 0.2.68
  as "Bode plot", renamed in 0.2.69; "Bode" stays the internal name).
  Modes exclude each other and are chosen from the main menu, which is the only way between them.
- **Variant**: changes how the same measurement is shown. Scope: Normal, Phosphor, X-Y. Spectrum: Spectrum,
  Waterfall, Density. Variants live in the mode's Display control, never in the main menu.
- **Parameter**: a value of the current mode or variant (FFT length, persistence, trace style).
- **Setting**: a device preference that holds across modes (brightness, file message timeout).
- **Utility**: a screen that is not a measurement (saved files, diagnostics, USB drive).
- **Action**: something done once (run/stop, auto set, save, 50 % trigger, calibrate).

## 2. Where things live

- **Main menu** (left column): modes first (Scope, Spectrum analyser, Frequency response), utilities below (Saved pictures, Saved
  waveforms, Settings, Diagnostics), the row that takes the scope off-line (USB drive) last; eight rows at 52 px (fw
  0.2.68). No marker for the current mode: the screen shows it. No greyed rows for features that do not exist yet (the
  Frequency response row is grey only where the hardware cannot run it: no filter / DDS bitstream, or in a viewer; a
  tap on it says why).
- **Top bar**: hardware state and global tools, identical in every mode: Menu, CH1, CH2, brightness, ACQ, Move fast,
  Trigger, Generator, battery, MCP chip. No version string (Diagnostics has it). Where a mode drives one of these
  itself (the spectrum's Span sets the time base), the button shows the value and its menu says so instead of fighting
  the mode.
- **Dropdowns** (from the top bar): channel = the input (range, coupling, probe, invert); ACQ = acquisition only
  (sample rate, time per division, memory, roll, average, filter); Trigger = mode, edge, channel, position, Always
  50 %; Generator = the signal source. Nothing about how the plot looks goes in a dropdown.
- **Right bar, first page**: actions on what is shown, in fixed slots: CTRL, RUN/STOP, AUTO SET, T CURSOR, V CURSOR,
  MEASURES, SAVE PIC, SAVE WAVE. A mode that cannot use a slot leaves it empty rather than showing a control that does
  nothing. AUTO SET means the mode's own auto (the spectrum's Auto in Spectrum).
- **Right bar, CTRL page**: how the plot is shown and computed, the same order in every mode: Display (the variants,
  each with an icon, plus the variant's own parameters inside the fold-out), the mode's analysis controls (Trace style
  in Scope; Analysis and Axis in Spectrum), Grid, the V+ / CH / V- steppers for both channels, 50 % TRIG. A CTRL button
  that holds a value shows it on the button and opens a fold-out beside the bar with the choices or + / -.
- **Bottom row**: readouts only, as measurement cards chosen through MEASURES, per mode (Scope: the record
  measurements; Spectrum: peak, level, RBW and the spectrum set). The record-position slider row under the grid stays.
- **Warnings**: top-middle of the plot in every mode, one line stating the consequence and the action ("CH1 overrange:
  increase V+"). Readouts such as wfm/s go with the parameters they belong to, never into a warning.
- **One setting, several homes is fine when each home earns its place.** A value reachable from two places is the same
  setting (the V/div chips in the channel menu, the steppers on the CTRL page and the tap bands at the top and bottom of the plot
  all set `displayvoltperdiv`). The rule is good UX, not strictness: a quick path that helps while watching the trace stays;
  a duplicate that only adds clutter goes (owner, 2026-10-19).
- **Time/div drives the sample rate** (owner, 2026-10-19 22:20-22:24; fw 0.2.76). The Scope's tap zoom (left half of
  the plot one time/div slower, right half one faster) and the ACQ menu's time/div boxes choose a time/div, and while
  the scope can take a new record (running, or Single, where the tap re-arms the shot) the rate comes from one ladder:
  for every time/div the fastest rate whose record, at the memory depth kept, covers the ten divisions with at least
  ten samples per division (`tb_view.c` `tbv_ladder_rate`). After every zoom step the pair is the ladder's, so going to
  either end and back never leaves a slow time/div at a fast rate (a record covering a sliver of the screen) or a fast
  time/div with a handful of samples per division. The ACQ menu's rate boxes stay for a manual choice: a rate set by
  hand holds until the next zoom step or time/div box, which takes the ladder's rate again; when a rate box makes the
  time/div impossible (record too short, or under ten samples per division) the time/div snaps and the snap notice says
  so, as before. The ends are plain stops: zooming in ends at 50 ns/div at 200 MSa/s (20, 10 and 5 ns/div are never
  viable and stay grey), zooming out ends at 200 ms/div (1 kSa/s at 3000 samples) at the roll boundary, which a tap
  never crosses (roll is the ACQ menu's mode button; in roll the tap steps 50 s .. 100 ms/div as before); a tap at an
  end changes nothing and the other direction works at once. Stopped (and in the waveform viewer) the record on screen
  is fixed and keeps its rate: the zoom steps through the time/divs that record can show, and where the ladder would go
  on but the record cannot, the tap says "Stopped: press RUN to zoom further"; the time/div boxes it cannot show are
  grey. The host's SET_TIMEBASE by UI index keeps its documented index-to-rate table (`docs/protocol.md`).
- **Span drives the sample rate** (owner, 2026-10-19 23:09; fw 0.2.77), the Spectrum's counterpart. The view is a span
  from a 1-2-5 ladder (1 Hz .. 20 MHz, then the whole axis 0 .. 25 MHz) around a centre: a tap in the right half of the
  plot narrows the span one step, the left half widens it, both about the centre (kept inside 0 .. 25 MHz); a drag in
  the plot or on the slider pans the centre, and a tap in the slider's track jumps the window there. While a new record
  can be taken (running, or Single) every zoom step sets the rate from the stop frequency (centre + span / 2): the
  slowest UI rate whose fs / 2 covers it and whose capture of the record takes at most 2.048 s
  (`spec_ladder.c` `spl_rate`), and AUTO SET ends on the same pair. **The capture time is the record's, memory depth /
  fs** (fw 0.2.84; 0.2.77-0.2.83 counted the FFT's N samples, so 16k memory at 2 kSa/s took 8 s): the pair is a
  function of the cell and the memory depth only, the FFT length N sets just where zooming in ends (16 bins). The
  invariant holds after every change of its inputs: a memory depth change (ACQ menu depth or Filter, the host) takes the
  rule's rate for the cell at once while the rate is the one the ladder set (a hand-picked rate holds as below), and an
  N change needs nothing. Capping the memory depth to N while in the Spectrum was the other option; it was not taken,
  because it would make one setting mean two things per mode and change the record the Scope and the host see. The
  Capture card (MEASURES) shows memory depth / fs. A cell from 0 Hz holds between a quarter and a half of the N bins
  where neither the capture time nor the slowest rate holds the rate. **A pan never changes the rate** (owner, 2026-10-20 10:24; fw 0.2.81: "the spectrum should stay the
  same when scrolling, changes to sample rate and time/div should happen when zooming"): plot drag, slider drag and
  tap-to-jump keep the rate and are clamped to the current axis 0 .. fs / 2, which is what the slider track shows, so
  the clamp is visible. To look above the axis, zoom out (the rate rises) and zoom in again; the zoom step re-derives
  the rate from the new centre. Accepted consequence: a pan to a low centre at a wide span keeps the coarse resolution
  until the next zoom step. The ends are plain stops: zooming out ends
  at the whole axis at 50 MSa/s; zooming in ends where the next span would need a rate whose capture exceeds 2.048 s,
  or would hold fewer than 16 bins (at a high centre the rate stays high, so that limit ends it first). A tap at an end
  changes nothing and the other direction works at once. After a rate change the view waits for a capture at the new
  rate. FFT length stays the user's (CTRL > Analyse, default 2048, limited by the record, which the fold-out then says).
  A rate picked by hand in the ACQ menu holds until the next zoom step, as in the Scope; the view shows the span on that
  rate's axis as far as it reaches, and a pan clamps the span into that axis. Stopped, the record
  keeps its rate: zoom and pan stay inside its axis and the warning says "Stopped: press RUN to zoom further" where the
  ladder would go on. AUTO SET picks its span by the same ladder, from 0 Hz, so its result is a ladder cell.

## 3. How things look

- **Colour carries state; words carry meaning.** A box label shows the value; its colour shows the state; a toggle
  never reads "on" or "off". Green = verified or active, amber = needs attention, grey = not available; the word is
  always next to the colour (colour-blind safe). Nothing blinks.
- **Chips**: one construction for the top bar chips and the measurement cards: a block in the channel colour (CH1
  yellow, CH2 cyan; green for ACQ and the trigger; neutral grey for things that belong to no channel) with rounded outer
  corners, drawn over a grey box that holds the parameters or the value, so the join has no notch. The block is as wide
  as its text.
- **Measurement names** as scope makers spell them: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width,
  +Duty, -Duty, Peak, Level, RBW, THD, Noise, Span, Center (fw 0.2.65), Time, Capture (fw 0.2.84: one record's capture time). Values with four significant digits and a unit, formatted to fit the card.
- **Fonts**: the device has bitmap fonts of 13, 16 and 24 px only (`font_2`, `font_3`, `font_5`), no anti-aliasing,
  no word wrap. 13 px for units and secondary text, 16 px for values and rows, 24 px for headlines. Every line is laid
  out to fit its box at 6.7 px per character (13 px) and 7.1 px (16 px); long sentences are broken by hand.
- **Touch**: new controls are at least 44 px tall and 80 px wide, hit area equal to the drawn area; stock controls keep
  their stock sizes. Pages are reached by visible buttons, never by a swipe. Thin stepper bars extend their hit area
  into the neighbouring block. Everything drawn as a handle (the channel and reference pointers at the left edge, the trigger pointers, the cursors) is draggable from the handle itself; grabbing the trace is the secondary way (owner, 2026-10-19).
- **Dialogs**: a title, one instruction, a progress or result line, two buttons. Primary or confirming action on the
  right, dismissing one on the left. Red only for destructive actions (Clear calibration); everything else green or
  blue. **Owner exception (2026-10-19, fw 0.2.68): the RUN/STOP button** shows the action a tap does, as on bench
  scopes: green "RUN" while stopped, red "STOP" while running or waiting. The top bar chip beside the trigger shows the
  state (STOP / RUN / WAIT); both follow one run state, whoever changed it (the screen, a single shot, the host). A destructive action always confirms with a sentence that states the consequence. A modal dialog owns the
  screen: taps outside it do nothing, only its buttons leave it.
- **Grid lines mean something** (owner, 2026-10-19 20:50; fw 0.2.71). A grid line sits at a value its axis names.
  Level axes: a horizontal line at every labelled division (dB, deg, V). Frequency axes, Linear: a vertical line at every
  1-2-5 step of the span (at most ten), each labelled, or every other one when there are more than six. Log: a line at
  every decade (dense dots, labelled); the 2 and 5 lines (sparse dots) when the span is under three decades, labelled
  when it is under 1.5 decades; a span holding fewer than two such lines takes the linear steps. Labels sit under their
  lines (clamped inside the plot at its edges). The lines follow Grid; the labels stay. One function draws the rule
  (`bode_ui.c` `bu_fgrid`, host-tested), used by Frequency response and Spectrum. No fixed divisions on a frequency axis.
- **Look**: the stock palette and shapes (black ground, dark blue buttons, pale yellow active, channel colours). The
  makeover reorganises; it does not restyle.

## 4. What is shown

- Only values the firmware has. A number without a source in the protocol or the sources is not drawn; a feature not
  yet built gets no placeholder.
- Every unavailable state has defined text that says why ("FPGA stock 0x1432: no generator, gain calibration not
  possible"), never a blank or a zero.
- Trust decisions are shown as a residual against a limit ("cores agree, 0.2 code"), not as raw numbers; raw numbers
  go to a detail page.
- No start-up messages that lower trust; the first-run screen says what to do and offers Later.
- Long-running screens (Diagnostics) run in the main loop so the PC protocol keeps working; only calibration and
  confirmations are modal.
- **Host commands leave the modal screens** (owner, 2026-10-19 23:27: "if the agent sends commands it just exits all
  modals, since that wouldn't be destructive; just exiting to the base screen"; fw 0.2.78, protocol 1.23). A host
  request that needs the main loop while a menu, fold-out, dialog or screen is open does not get E_BUSY: every modal
  loop checks `PC_UI_YIELD()` once per iteration and leaves by its own cancel path, nested ones one after the other,
  until the base screen is reached and the main loop answers the request. A confirmation answers Cancel / No, a menu
  or fold-out closes as a tap outside it does, Settings and its time page go Back, the file browser and the viewers
  Return, a file message is dismissed, the first-run screen answers Later, the calibration wizard and the volts
  calibration Cancel between steps (nothing written). The yield never takes a path that writes something the user
  did not confirm (the generator keypad closes without the typed number); what the user already changed stays, as
  with the cancel path itself. The unwinding is silent: no note on the screen (fw 0.2.78-0.2.80 showed "Host
  command: menu closed" for 4 s; removed in fw 0.2.81, owner 2026-10-20: not wanted).
- **What does not yield** keeps E_BUSY with the reason as its detail and text, so the host can say why: a calibration
  measurement in progress (the wizard's steps and Save, a volts calibration step), a file save in progress (SAVE PIC,
  SAVE WAVE, the Frequency response CSV, the Diagnostics report), USB drive mode. A new modal loop adds the yield
  check after its touch scan; a new step that must finish wraps itself in `pc_glue_ui_hold`. A UI pass with no yield
  check that takes longer than 1.5 s (auto set, a slow capture) answers E_BUSY "UI did not return to the main
  screen". Commands answered from the timer interrupt (PING, GET_INFO, GET_STATUS, GET_BATTERY, GET_HEALTH, the link
  counters and the event log) never close anything.

## 5. Process

- Mockups first, at 800x480 with the real fonts and sizes, then an adversarial review against today's firmware for
  lost functions, infeasible values and contradictions, then the firmware batch. The mockups are the acceptance
  reference for the bench walk-through with the owner.
- Consistency and logic outrank muscle memory, but nothing moves without a reason that the plan states.
- Every removed control is listed with where its function went or why it can go.
