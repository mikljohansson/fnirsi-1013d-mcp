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

## 3. The bars and dropdowns **(owner, corrected 2026-10-19 afternoon: move as little as possible)**

What exists today (menu.c `scope_setup_right_control_menu`, `scope_open_acquisition_menu`, `scope_open_trigger_menu`):
the right bar's first page is, top to bottom as drawn, CTRL, RUN/STOP, AUTO SET, T CURSOR, V CURSOR, MEASURES, SAVE PIC,
SAVE WAVE (the waveform viewer swaps RUN/STOP, AUTO SET and SAVE WAVE for previous, next, delete); CTRL switches to the second page: CTRL, V+ CH1 V-, V+ CH2 V-,
50 % TRIG (GRID in the viewer). The ACQ dropdown holds Sample rate, Time per division, Memory, Trace style, Roll,
Average and Filter; the Trigger dropdown holds mode, edge, channel, position and Always 50 %.

- **Top bar**: unchanged, except the version string goes (it lives in Diagnostics) **(owner)**. Move fast stays: it
  belongs with V+ / V- and dragging.
- **Right bar**: unchanged in Scope, both pages. In Spectrum, AUTO SET runs the spectrum's Auto (one Auto per screen);
  T CURSOR and V CURSOR are hidden until spectrum markers exist; SAVE WAVE is hidden until a spectrum export exists;
  MEASURES is hidden. The slots keep their places.
- **Dropdowns**: the ACQ dropdown gains a **Display** box next to Trace style with the values Trace, Phosphor, X-Y
  **(owner)**: Phosphor leaves the main menu and X-Y leaves System settings. Filter, Average, Roll and Memory stay in
  ACQ; 50 % stays in both places (the right-bar button sets the level once, Always 50 % in the trigger menu keeps it).
- **Strips**: they stay, in the record-position slider's row under the grid (y 452); the measurement cards start at y 426
  on the plot and stack upward, so the two never collide. Spectrum keeps Channel, FFT length, Window, Scale, Ref level, Average, View and Reset; Phosphor keeps
  Persistence, Palette and Clear. In Spectrum the CTRL page shows the same V+ / V- / 50 % controls as in Scope (they
  set the input range and trigger level, which the spectrum needs). Both lose the "Scope" field (the menu is the way between modes) and the spectrum
  strip loses Auto (AUTO SET does it).
- **Bottom row**: the measurement cards, as the stock firmware and Atlan's draw them (FNIRSI product photos): the
  Measures button opens the list of up to twelve measurements per channel (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq,
  Cycle, Time+, Time-, Duty+, Duty-; `scopesettings.measuresstate`, drawn by `scope_display_measurements`), the
  ticked ones appear as cards, CH1's from the left and CH2's from the middle (x 366), stacking upward over the plot.
  Card scheme **(owner)**: the stock one, a small chip in the channel colour with the digit 1 or 2 in black, then
  "Metric: value" in white on a dark grey rounded card ("[1] VPP: 2.40V"); today's Atlan drawing (the metric name in a
  coloured box, scope_functions.c `scope_display_channel_measurements`) changes to that, and the "Freg" label typo is
  fixed with it. Otherwise unchanged. Roadmap, not now: a spectrum card set (peak frequency and level per channel, THD) and the
  FPGA hardware measurements of docs/plans/roadmap.md section 8 item 6 feeding the same cards.
- **Warnings**: top-middle of the plot in every mode, stating consequence and action **(owner)**; readouts such as
  wfm/s and the spectrum's one-line header stay where they are.

## 4. Visible changes for an existing user

The main menu order (section 2); the Display box in ACQ; the strips without their Scope and Auto fields; AUTO SET
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
  setup variant; move speed stays in the top bar), Files (screenshot stamp, reference traces at power-on), Clock (RTC
  module on/off, time and date; RTC only), Start-up (one row "At power-on" with the values "Open firmware", "Boot menu,
  then open firmware", "Boot menu, then stock firmware": the stock image always comes with the boot menu), and "Restore
  default settings" with a confirmation that says it keeps the calibration and no longer writes the boot-mode byte. The
  USB-Serial toggle is removed (the build forces CDC). Dev mode and FEL start move to a developer page in Diagnostics
  (Log page, behind a confirmation): the trigger debug overlay and "start FEL at next power-on".

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

Cancel on any screen leaves without writing anything. There is no Skip: a channel that cannot be measured is reported on
its screen and the user decides between retrying and Cancel. Two exceptions the firmware needs: on a bitstream without
the generator (stock 0x1432) the gain screens say "Gain calibration needs the open FPGA bitstream" and Continue leads
to Done, so the zero level can still be saved; and the stock volts-per-range calibration (external DC source,
`scope_input_calibration`) stays reachable as a service action "Volts calibration" on the Diagnostics Calibration page,
since Clear never touches the volts factors and a unit that lost them needs that procedure. Firmware: fw 0.2.50's wizard (README 6.4) has the steps
and checks; its copy, its two-probe gain step and its Skip go, the per-channel gain screens and the final Save come.

## 7. Implementation order

1. Main menu rows and the USB drive screen text (small, no new data).
2. The Display box in ACQ (Phosphor and X-Y moved in), the strips without Scope and Auto, AUTO SET as the spectrum's
   Auto, hidden right-bar slots in Spectrum, warnings top-middle in phosphor, the version string out of the top bar.
3. Settings fold-out trim and the full Settings screen.
4. Diagnostics as a display mode with the three pages and the firmware readers.

Each step is one firmware generation's batch with its own bench walk-through by the owner; the mockups are the
acceptance reference, and `docs/plans/diagnostics-review.md` section 1 lists what must not be lost from today's screens.
