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
3. Bode plot (owner decision 2026-10-19 18:45, fw 0.2.68: the third mode row; it left the generator menu. Eight
   rows at a 52 px pitch. Grey and inert only on bitstreams without the filter and DDS, or in the viewers)
4. Saved pictures
5. Saved waveforms
6. Settings
7. Diagnostics
8. USB drive

The Spectrum row is always shown, also in roll mode; choosing it switches Roll off and says so once in the top-middle
warning line ("Roll off for the spectrum"); leaving Spectrum does not switch Roll back on **(owner)**. Rows never move or
disappear. Rules: modes first, utilities below, the row that takes the scope off-line (USB drive) last. No "current mode" marker:
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
  empty until spectrum markers exist; SAVE WAVE stays (it saves the time record behind the spectrum, which works today);
  MEASURES is not empty in Spectrum: it opens the spectrum's own measurement list (Peak, Level, RBW, and the spectrum set
  of section 3a) the way it opens the scope's list in Scope.
  AUTO SET shows "Auto..." while the spectrum's Auto runs and a second tap cancels it (today's strip behaviour kept).
  The waveform viewer keeps its previous / next / delete substitutions. The CTRL button is highlighted while page two is
  shown, as today.
- **Right bar, CTRL page, Scope** **(owner)**, top to bottom: CTRL; Display (fold-out: the view, Normal or X-Y, each
  with an icon; below it a **Phosphor** switch, the same slide toggle as in the channel menus, and while it is on the
  rows Persistence, Palette, Clear; so phosphor drawing combines with either view as the firmware allows today); Clear stays in that section at three taps (it matters only with Infinite persistence) **(owner)**; Trace style
  (fold-out: Linear, Dots, Peak, Pro; moved out of ACQ); Grid (toggle, in every mode); the CH1 and CH2 V/div
  Display fold-out rows carry a small icon each (owner): a clean trace line for Normal, a Lissajous loop for X-Y; the
  Phosphor switch row carries a soft graded trace icon; the
  **steppers** (owner): about 82 px each: a 28 px grey bar reading "V+", a 26 px centre in the channel colour reading
  "CH1" or "CH2" (one text line; the value is in the top bar chip), a 28 px grey bar reading "V-": the stock V+ CH1 V-
  controls, compact; each bar's hit area runs to the middle of the centre (about 41 px); the + and - hit areas extend into their half of the
  centre so each gets about 45 px of touch; they step `displayvoltperdiv` exactly as the channel menu's chips do (same
  setting, the stepper is the quick way while watching the trace); 50 % TRIG (action, as today). That fills the eight
  slots exactly.
- **Right bar, CTRL page, Spectrum** **(owner)**: CTRL; Display (fold-out: Spectrum, Waterfall, Density, each with an
  icon, the same button name and slot as in Scope); Analysis (fold-out: FFT
  length, Window, Average, Reset, and first of all **Analyse: CH1 / CH2 / both**, today's strip channel selector that also
  sets the dBV or dBA unit and which channel Auto analyses, then Scale and Ref level, folded in from the former Axis
  button so both CTRL pages share one geometry **(owner)**); Grid; the CH1 and CH2 V/div steppers; 50 % TRIG. The spectrum strip goes away; Span and Start keep their gestures. The waterfall's and density's
  colour legend is drawn at the top-right of the plot as a readout; the waterfall's time span and the phosphor's wfm/s
  are cards. **Spectrum slider (owner, 2026-10-19 19:40)**: the Scope's slider row stays under the plot in Spectrum
  too, with the same construction and the same drag as the Scope (fw 0.2.64: the window follows the finger). It shows
  the shown span as a window over 0..fs/2 at the current sample rate: the left edge at start / (fs/2), the width
  span / (fs/2) of the track. Dragging pans the centre and keeps the span, the same pan as dragging the plot. At full
  span the window fills the track and there is nothing to pan. To make room the plot becomes 8 divisions of 44 px
  (y 50..402), the frequency labels sit at y 406, and the cards stay at the Scope's y 426 between plot and slider.
  The Frequency response mode gets the same row for Start..Stop (`docs/plans/bode-ui.md` section 2). This replaces
  "the record-position slider is hidden in Spectrum". Fold-outs open to the
  left of the bar over the plot's right edge and close on a second tap of their button, a tap outside, or another
  CTRL button; the waveform viewer's CTRL page stays as today. Display, Grid, persistence and palette are saved with the
  settings and restored at power-on (Grid gets a spare settings word). The one-line header goes away too **(owner)**: its
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
  Card scheme **(owner, final)**: Atlan's layout kept (it looks better than stock's): a dark grey rounded card with a
  block in the channel colour at its left holding the measurement name in black, then the value in white; the block is
  built like the top bar chips, full card height, rounded only on its outer left corners, the grey card drawn behind it
  with no notch; the block is as wide as the name (no padding space after it). Names as scope makers spell them:
  Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period (was Cycle), +Width, -Width (were Time+, Time-), +Duty, -Duty (were
  Duty+, Duty-); the "Freg" typo goes with it. Values are formatted to fit the card (four significant digits and a
  unit). The spectrum's cards use the same scheme: [Peak] 1.000kHz and [Level] +2.7dBV in the channel colour, [RBW]
  146Hz with a neutral grey block. Cards stay 118 x 22 px (three per channel per row fit the 726 px plot; the name
  block without padding leaves about 22 px of margin for "Freq 1.000kHz"). The record-position slider row under the grid stays in every mode. Roadmap,
  not now: a spectrum card set (peak frequency and level per channel, THD) and the FPGA hardware measurements of
  docs/plans/roadmap.md section 8 item 6 feeding the same cards.
- **Warnings**: top-middle of the plot in every mode (y 70), stating consequence and
  action **(owner)**; readouts such as wfm/s go into the Display fold-out's phosphor section.

## 3a. Measurements per mode (from the review's inventory, docs/plans/ux-review.md section 6)

Today: twelve metrics per channel computed in the readout over the whole record (pecostm32's table and tiles; Atlan
added the reference-trace sources, Hide and the y 426 position); Vrms includes DC; the timing metrics need three
crossings; a new unit shows no cards because the defaults set none. The makeover gives each mode its own MEASURES list,
stored in spare settings words, with default cards: Scope Freq, Vpp, Vavg (and CH2 Phase once it exists); Spectrum
Peak, Level, THD (and RBW). Tiers for later firmware work (queued as "Measurements v2"): already computed (the twelve,
AC RMS, spectrum peak / level / RBW, wfm/s); cheap on the device (top / base, rise and fall time, overshoot,
whole-period mean and RMS, CH1-CH2 delay, phase and gain, cursor-gated statistics, a pulse-induction decay time
constant; spectrum sub-bin peak, H2 / H3, THD, SFDR, noise floor, SNR, band power, occupied bandwidth; Bode f0, Q,
-3 dB, margins, slope); FPGA (200 MHz counter, every-edge width and duty, running min / max, DVM).

## 4. Visible changes for an existing user

The main menu order (section 2); the CTRL pages rebuilt (Display with icons, Trace style, Grid, the V+ / CH / V- steppers,
50 % TRIG; in Spectrum Display, Analysis, Axis); the spectrum and phosphor strips and the spectrum header gone, their
readouts as cards; Trace style out of ACQ; AUTO SET running the spectrum's Auto in that mode; the top bar chips rebuilt; the version string gone from the top bar; Baseline calibration and X-Y gone
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

## 6b. Buttons in dialogs **(owner)**

The default or primary action sits on the right, the secondary or dismissing one on the left, in every dialog: the
wizard (Cancel left, Calibrate / Continue / Save calibration right), confirmations (Cancel left, the action right) and
the first-run screen (Later left, Calibrate now right). Red is reserved for destructive actions (Clear calibration); Enter FEL is green, since a
power cycle boots the card as usual.

## 6c. How to read the mockups **(owner, 2026-10-19, implementation go)**

The mockups are the UX and the design-language vision, not a pixel specification: implement the flows, the placement
and the behaviour they show with the device's existing drawing language (its fonts, button shapes, colours), and do not
shoehorn them in pixel for pixel. The new full-screen pages (Diagnostics, Settings, wizard, first run, USB drive,
confirmations) keep the mockups' look: black ground, outlined cards and buttons, the state words in colour. The
calibration wizard is tested in its redesigned four-screen form, not in the fw 0.2.50 interim one.

## 7. Implementation order

1. Main menu rows and the USB drive screen text (small, no new data).
2. The CTRL pages (Display fold-outs with icons and the phosphor parameters, Trace style, Grid, the steppers; Display,
   Analyse with the channel selector, Scale and Ref level in Spectrum), the strips and the spectrum header removed with their readouts as cards,
   Trace style out of ACQ, AUTO SET as the spectrum's Auto, hidden right-bar slots in Spectrum, warnings top-middle,
   the version string out of the top bar, the top bar chips and the measurement cards rebuilt.
3. Settings fold-out trim and the full Settings screen.
4. Diagnostics as a display mode with the three pages and the firmware readers.

Each step is one firmware generation's batch with its own bench walk-through by the owner; the mockups are the
acceptance reference, and `docs/plans/diagnostics-review.md` section 1 lists what must not be lost from today's screens.
