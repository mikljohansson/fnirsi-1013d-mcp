# UX makeover plan (owner decisions of 2026-10-19)

A reorganisation of the FNIRSI 1013D open firmware's screens, not a visual redesign: colours, fonts, button shapes and
the positions of the top bar, right bar and plot stay as stock users know them. Things move to where they belong.
Owner decisions are marked **(owner)**. The diagnostics and settings screens come from the design review in
`docs/plans/diagnostics-review.md`; the mockups of every new screen live in the Design artifact linked from
`docs/plans/task-queue.md` ("Diagnostics view redesign").

## 1. Vocabulary

- **Mode**: changes what is measured and which controls exist. Scope, Spectrum analyser, Bode plot (when it ships).
- **Variant**: changes how the same measurement is shown. Scope: Trace, Phosphor, X-Y. Spectrum: Spectrum, Waterfall,
  Density.
- **Parameter**: a value of the current mode or variant (FFT length, persistence, filter).
- **Setting**: a device preference that holds across modes (brightness, confirmation prompts).
- **Utility**: a screen that is not a measurement (saved files, diagnostics, USB drive).

## 2. Main menu **(owner)**

Same column, same rows, same style. Top to bottom:

1. Scope
2. Spectrum analyser
3. Bode plot (only once it ships, never a greyed row)
4. Saved pictures
5. Saved waveforms
6. Settings
7. Diagnostics
8. USB drive

Rules: modes first, utilities below, the row that takes the scope off-line (USB drive) last. No "current mode" marker:
the screen shows the mode **(owner)**. The "Scope" fields in the spectrum and phosphor strips go away: the menu is the
only way between modes **(owner)**. "USB connection" becomes "USB drive"; its screen says in plain words that the SD
card is shown to the PC as a drive, that PC control through the protocol stops until the screen is left, and to eject
the drive on the PC first.

## 3. The bars and dropdowns **(owner, final 2026-10-19: consistent and logical over muscle memory)**

The rule: the ACQ and Trigger dropdowns hold what the hardware acquires; the right bar's first page holds what you do
to the plot; its CTRL page holds how the plot is shown and computed; the bottom row holds readouts (the measurement
cards and the record-position slider). Every mode reads the same way and no parameter has two homes. Fold-outs make
the space: a CTRL button that holds a value opens a small panel beside the bar with the value and its choices or + / -.

- **Top bar chips** (owner, 2026-10-19): one construction for CH1, CH2, ACQ and the trigger: a coloured icon block with
  rounded outer corners (CH1 yellow, CH2 cyan, ACQ and trigger green) and a grey parameter box to its right that is drawn
  behind the icon block, so the join has no notch and only the chip's outer corners are rounded. Today the trigger's
  grey box has its own rounded corners and the channel and ACQ chips differ from it.
- **Top bar**: unchanged except the version string goes (it lives in Diagnostics) **(owner)**. Move fast stays in its
  slot: it is a modifier for dragging on the plot in every mode, a global tool by the rule above (owner, after weighing a
  collapsed Save button to make room on the right bar: not worth a tap on every save).
- **Right bar, first page** (both modes): CTRL, RUN/STOP, AUTO SET, T CURSOR, V CURSOR, MEASURES, SAVE PIC, SAVE WAVE
  in today's slots. In Spectrum, AUTO SET runs the spectrum's Auto; T CURSOR, V CURSOR, MEASURES and SAVE WAVE stay
  empty until spectrum markers exist; SAVE WAVE stays, it saves the time record behind the spectrum, which works today.
  AUTO SET shows "Auto..." while the spectrum's Auto runs and a second tap cancels it (today's strip behaviour kept).
  The waveform viewer keeps its previous / next / delete substitutions. The CTRL button is highlighted while page two is
  shown, as today.
- **Right bar, CTRL page, Scope** **(owner)**, top to bottom: CTRL; Display (fold-out: Normal, Phosphor, X-Y; "Normal"
  rather than "Trace" so it cannot be confused with Trace style; with
  Phosphor chosen the same fold-out shows Persistence, Palette, Clear, so there is no phosphor strip); Trace style
  (fold-out: Linear, Dots, Peak, Pro; moved out of ACQ); Grid (toggle, in every mode); the CH1 and CH2 V/div
  Display fold-out rows carry a small icon each (owner): a clean trace line for Normal, a soft graded trace for
  Phosphor, a Lissajous loop for X-Y; the
  **steppers** (owner): about 82 px each: a 28 px grey bar reading "V+", a 26 px centre in the channel colour reading
  "CH1" or "CH2" (one text line; the value is in the top bar chip), a 28 px grey bar reading "V-": the stock V+ CH1 V-
  controls, compact; each bar's hit area runs to the middle of the centre (about 41 px); the + and - hit areas extend into their half of the
  centre so each gets about 45 px of touch; they step `displayvoltperdiv` exactly as the channel menu's chips do (same
  setting, the stepper is the quick way while watching the trace); 50 % TRIG (action, as today). That fills the eight
  slots exactly.
- **Right bar, CTRL page, Spectrum** **(owner)**: CTRL; Display (fold-out: Spectrum, Waterfall, Density, each with an
  icon, the same button name and slot as in Scope); Analysis (fold-out: FFT
  length, Window, Average, Reset); Axis (fold-out: Scale, Ref level); Grid; the CH1 and CH2 V/div steppers; 50 % TRIG. The
  spectrum strip goes away; Span and Start keep their gestures. The one-line header goes away too **(owner)**: its
  readouts become measurement cards in the bottom row like the scope's ("[1] Peak: 1.000kHz", "[1] Level: +2.7dBV",
  "RBW: 146Hz" with a grey chip), chosen through MEASURES like the scope's cards; the filter tag is not shown (it is a
  setting, visible in ACQ). Warnings keep the top-middle position.
- **ACQ dropdown** **(owner)**: Sample rate, Time per division, Memory, Roll, Average, Filter. Acquisition only: Trace
  style leaves it. With the Filter on, depths above 6000 are not offered (filtered records hold at most 8190 samples).
- **Trigger dropdown**: unchanged (mode, edge, channel, position, Always 50 %). 50 % TRIG on the CTRL page sets the
  level once; Always 50 % keeps it.
- **Bottom row**: the measurement cards as the stock firmware draws them (owner, from the FNIRSI product photos): the
  Measures button opens the list of up to twelve measurements per channel (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq,
  Cycle, Time+, Time-, Duty+, Duty-; `scopesettings.measuresstate`, drawn by `scope_display_measurements`); the
  ticked ones appear as cards, CH1's from the left and CH2's from the middle (x 366), stacking upward over the plot.
  Card scheme **(owner)**: the stock one, a small chip in the channel colour with the digit 1 or 2 in black, then
  "Metric: value" in white on a dark grey rounded card ("[1] Vpp: 2.40V"); the metric names are the firmware's table
  (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Time+, Time-, Duty+, Duty-) and values are formatted to fit the card's
  about 92 px of text (four significant digits and a unit, "500us" not "500.0us"); today's Atlan drawing changes to that and
  the "Freg" label typo is fixed with it. The record-position slider row under the grid stays in every mode. Roadmap,
  not now: a spectrum card set (peak frequency and level per channel, THD) and the FPGA hardware measurements of
  docs/plans/roadmap.md section 8 item 6 feeding the same cards.
- **Warnings**: top-middle of the plot in every mode (y 70, under the spectrum header), stating consequence and
  action **(owner)**; readouts such as wfm/s go into the Display fold-out's phosphor section.

## 4. Visible changes for an existing user

The main menu order (section 2); the CTRL pages rebuilt around fold-outs (Display, Trace style, Grid, V/div per channel,
and in Spectrum View, Analysis, Axis); the spectrum and phosphor strips gone; Trace style out of ACQ; AUTO SET
running the spectrum's Auto in that mode; the version string gone from the top bar; Baseline calibration and X-Y gone
from the Settings fold-out; the measurement cards in the stock chip scheme; the new Diagnostics screens with Calibrate,
Undo, Clear (zeros and gains only, volts factors kept) and the developer page; the new four-screen wizard; the full
Settings screen; the USB drive screen text. Everything else keeps its place and its meaning.

## 5. Settings **(owner)**

- The **Settings fold-out** keeps its shape and its most common rows: screen brightness, grid brightness, confirmation
  prompts, time and date (RTC module only), Other settings. Baseline calibration leaves it (to Diagnostics); X-Y leaves it
  (to Scope's Display variant).
- **Other settings** opens the full Settings screen (design artboard "Settings"): Display (screen and grid brightness),
  Controls (file message timeout, which is what 1.00o5's "confirmation" toggle really sets; cursors locked or free; auto
  setup variant), Files (screenshot stamp, reference traces at power-on), Clock (RTC
  module on/off, time and date; RTC only), Start-up (one row "At power-on" with the values "Open firmware", "Boot menu,
  then open firmware", "Boot menu, then stock firmware": the stock image always comes with the boot menu), and "Restore
  default settings" with a confirmation that says it keeps the calibration and no longer writes the boot-mode byte. The
  USB-Serial toggle is removed (the build forces CDC). Dev mode moves to a developer page in Diagnostics (from the Log
  page): "Debug overlay" (today's dev mode: trigger address and touch coordinates drawn over the top bar) as a toggle,
  and "Enter FEL now" behind a confirmation, which does what the USB command ENTER_FEL does (reboot into the USB boot
  loader at once, the card untouched); the boot-mode byte is never written for FEL from the UI, since boot mode 2
  would start FEL on every power-on.

## 6. Diagnostics (design artboards Status, Calibration, Log)

A main-loop display mode (the PC keeps control while it is open), three pages by header buttons. Status: identity
lines, four verdict tiles (Firmware, Restarts, USB, Battery), one calibration verdict line per channel, Calibrate and
Save report. Calibration: per-range table, provenance, corrected mismatch and noise against provisional limits, Check
on post, Undo last calibration, DC shift trim, Clear calibration (confirmed). Log: decoded event log, restart record,
SD state, timing. Details and the readers it needs: `docs/plans/diagnostics-review.md`. The calibration wizard (fw
0.2.50, README 6.4) is launched from Calibrate.

## 6a. Calibration wizard **(owner, 2026-10-19 afternoon)**

Four screens, one modal flow, the same two buttons on every screen, nothing saved before the last one. No extra
sentences: each screen is a title, one instruction, a progress or result line, and two buttons.

| Screen | Title | Instruction | Progress / result | Buttons |
|---|---|---|---|---|
| 1 | Zero level | "Fully disconnect both probes and the USB cable from the device. Tap Calibrate." | per-channel progress while running, then "Zero level done" (or the one-line reason on failure, Calibrate stays to retry) | Cancel, Calibrate (becomes Continue after success) |
| 2 | CH1 gain | "Plug the CH1 probe back in and connect it to the calibration post. Set it to 1X on the handle. Tap Calibrate." | progress, then "CH1 gain done" (or "No signal on CH1: tip on the post?" and Calibrate stays) | Cancel, Calibrate (becomes Continue) |
| 3 | CH2 gain | "Plug the CH2 probe in and connect it to the calibration post. Set it to 1X on the handle. Tap Calibrate." | as screen 2 for CH2 | Cancel, Calibrate (becomes Continue) |
| 4 | Done | the plain-word summary per channel (cores matched to x code, gain mismatch y %) | | Cancel, Save calibration |

Screen 1 shows "USB is connected: unplug it before Calibrate" in amber while a host is attached. While a step runs the
action button reads "Measuring..." and is greyed; Cancel stays. The Done screen is the success message in plain words,
one line per channel ("CH1 calibrated: cores matched, gain mismatch 1.3 %"), no jargon. The dimmed scope behind the
dialog is frozen (no live trace, no warning). Cancel on any screen leaves without writing anything. There is no Skip: a channel that cannot be measured is reported on
its screen and the user decides between retrying and Cancel. Two exceptions the firmware needs: on a bitstream without
the generator (stock 0x1432) the gain screens say "Gain calibration needs the open FPGA bitstream" and Continue leads
to Done, so the zero level can still be saved; and the stock volts-per-range calibration (external DC source,
`scope_input_calibration`) stays reachable as a service action "Volts calibration" on the Diagnostics Calibration page,
since Clear never touches the volts factors and a unit that lost them needs that procedure. Firmware: fw 0.2.50's wizard (README 6.4) has the steps
and checks; its copy, its two-probe gain step and its Skip go, the per-channel gain screens and the final Save come.

## 7. Implementation order

1. Main menu rows and the USB drive screen text (small, no new data).
2. The CTRL pages with fold-outs (Display with the phosphor parameters, Trace style, Grid, V/div per channel; View,
   Analysis, Axis in Spectrum), the strips removed, Trace style out of ACQ, AUTO SET as the spectrum's Auto, hidden
   right-bar slots in Spectrum, warnings top-middle in phosphor, the version string out of the top bar.
3. Settings fold-out trim and the full Settings screen.
4. Diagnostics as a display mode with the three pages and the firmware readers.

Each step is one firmware generation's batch with its own bench walk-through by the owner; the mockups are the
acceptance reference, and `docs/plans/diagnostics-review.md` section 1 lists what must not be lost from today's screens.
