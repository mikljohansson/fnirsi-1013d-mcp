# UI guidelines for the FNIRSI 1013D open firmware

The rules every screen follows. They were settled with the owner on 2026-10-19 while designing the UX makeover
(`docs/plans/ux-makeover.md` is the change list; the mockups are the Design artifact linked from
`docs/plans/task-queue.md`; the adversarial reviews that shaped them are `docs/plans/diagnostics-review.md` and
`docs/plans/ux-review.md`). An agent touching `firmware/scope/menu.c`, `statemachine.c`, a view or a dialog reads this
first. Facts about the panel and the fonts are in `firmware/README.md`.

## 1. Vocabulary

- **Mode**: changes what is measured and which controls exist. Scope, Spectrum analyser, Bode plot (when it ships).
  Modes exclude each other and are chosen from the main menu, which is the only way between them.
- **Variant**: changes how the same measurement is shown. Scope: Normal, Phosphor, X-Y. Spectrum: Spectrum,
  Waterfall, Density. Variants live in the mode's Display control, never in the main menu.
- **Parameter**: a value of the current mode or variant (FFT length, persistence, trace style).
- **Setting**: a device preference that holds across modes (brightness, file message timeout).
- **Utility**: a screen that is not a measurement (saved files, diagnostics, USB drive).
- **Action**: something done once (run/stop, auto set, save, 50 % trigger, calibrate).

## 2. Where things live

- **Main menu** (left column): modes first, utilities below, the row that takes the scope off-line (USB drive) last.
  No marker for the current mode: the screen shows it. No greyed rows for features that do not exist yet.
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

## 3. How things look

- **Colour carries state; words carry meaning.** A box label shows the value; its colour shows the state; a toggle
  never reads "on" or "off". Green = verified or active, amber = needs attention, grey = not available; the word is
  always next to the colour (colour-blind safe). Nothing blinks.
- **Chips**: one construction for the top bar chips and the measurement cards: a block in the channel colour (CH1
  yellow, CH2 cyan; green for ACQ and the trigger; neutral grey for things that belong to no channel) with rounded outer
  corners, drawn over a grey box that holds the parameters or the value, so the join has no notch. The block is as wide
  as its text.
- **Measurement names** as scope makers spell them: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width,
  +Duty, -Duty, Peak, Level, RBW, THD, Noise, Span, Center (fw 0.2.65). Values with four significant digits and a unit, formatted to fit the card.
- **Fonts**: the device has bitmap fonts of 13, 16 and 24 px only (`font_2`, `font_3`, `font_5`), no anti-aliasing,
  no word wrap. 13 px for units and secondary text, 16 px for values and rows, 24 px for headlines. Every line is laid
  out to fit its box at 6.7 px per character (13 px) and 7.1 px (16 px); long sentences are broken by hand.
- **Touch**: new controls are at least 44 px tall and 80 px wide, hit area equal to the drawn area; stock controls keep
  their stock sizes. Pages are reached by visible buttons, never by a swipe. Thin stepper bars extend their hit area
  into the neighbouring block. Everything drawn as a handle (the channel and reference pointers at the left edge, the trigger pointers, the cursors) is draggable from the handle itself; grabbing the trace is the secondary way (owner, 2026-10-19).
- **Dialogs**: a title, one instruction, a progress or result line, two buttons. Primary or confirming action on the
  right, dismissing one on the left. Red only for destructive actions (Clear calibration); everything else green or
  blue. A destructive action always confirms with a sentence that states the consequence. A modal dialog owns the
  screen: taps outside it do nothing, only its buttons leave it.
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

## 5. Process

- Mockups first, at 800x480 with the real fonts and sizes, then an adversarial review against today's firmware for
  lost functions, infeasible values and contradictions, then the firmware batch. The mockups are the acceptance
  reference for the bench walk-through with the owner.
- Consistency and logic outrank muscle memory, but nothing moves without a reason that the plan states.
- Every removed control is listed with where its function went or why it can go.
