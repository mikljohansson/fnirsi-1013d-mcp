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
the right bar's first page is CTRL, T CURSOR, V CURSOR, MEASURES, SAVE PICTURE, RUN/STOP, AUTO SET, SAVE WAVE (the waveform
viewer swaps the last three for previous, next, delete); CTRL switches to the second page: CTRL, V+ CH1 V-, V+ CH2 V-,
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
- **Strips**: they stay, in the record-position slider's row under the grid, above the measurement cards, so they never
  collide with the cards. Spectrum keeps Channel, FFT length, Window, Scale, Ref level, Average and View; Phosphor keeps
  Persistence, Palette and Clear. Both lose the "Scope" field (the menu is the way between modes) and the spectrum
  strip loses Auto (AUTO SET does it).
- **Bottom row**: the measurement cards, as the stock firmware and Atlan's draw them (FNIRSI product photos): the
  Measures button opens the list of up to twelve measurements per channel (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq,
  Cycle, Time+, Time-, Duty+, Duty-; `scopesettings.measuresstate`, drawn by `scope_display_measurements`), the
  ticked ones appear as cards in the channel's colour, CH1's from the left and CH2's from the middle, stacking upward
  over the plot. Unchanged. Roadmap, not now: a spectrum card set (peak frequency and level per channel, THD) and the
  FPGA hardware measurements of docs/plans/roadmap.md section 8 item 6 feeding the same cards.
- **Warnings**: top-middle of the plot in every mode, stating consequence and action **(owner)**; readouts such as
  wfm/s and the spectrum's one-line header stay where they are.

## 4. Visible changes for an existing user

Only these: the main menu order (section 2), the Display box in ACQ, the strips without their Scope and Auto fields,
the version string gone from the top bar, Baseline calibration and X-Y gone from the Settings fold-out, and the new
Diagnostics and Settings screens. Everything else keeps its place and its meaning.

## 5. Settings **(owner)**

- The **Settings fold-out** keeps its shape and its most common rows: screen brightness, grid brightness, confirmation
  prompts, time and date (RTC module only), Other settings. Baseline calibration leaves it (to Diagnostics); X-Y leaves it
  (to Scope's Display variant).
- **Other settings** opens the full Settings screen (design artboard "Settings"): Display, Controls (confirmation
  prompts, cursors locked or free, auto setup variant, move speed), Files (screenshot stamp, reference traces at
  power-on), Clock (RTC only), Start-up (one row "At power-on": open firmware / boot menu then open firmware / stock
  firmware; FEL only in a developer section), and "Restore default settings" with a confirmation that says it keeps the
  calibration and no longer writes the boot-mode byte. The USB-Serial and Dev mode toggles are removed (review 2).

## 6. Diagnostics (design artboards Status, Calibration, Log)

A main-loop display mode (the PC keeps control while it is open), three pages by header buttons. Status: identity
lines, four verdict tiles (Firmware, Restarts, USB, Battery), one calibration verdict line per channel, Calibrate and
Save report. Calibration: per-range table, provenance, corrected mismatch and noise against provisional limits, Check
on post, Undo last calibration, DC shift trim, Clear calibration (confirmed). Log: decoded event log, restart record,
SD state, timing. Details and the readers it needs: `docs/plans/diagnostics-review.md`. The calibration wizard (fw
0.2.50, README 6.4) is launched from Calibrate.

## 7. Implementation order

1. Main menu rows and the USB drive screen text (small, no new data).
2. The Display box in ACQ (Phosphor and X-Y moved in), the strips without Scope and Auto, AUTO SET as the spectrum's
   Auto, hidden right-bar slots in Spectrum, warnings top-middle in phosphor, the version string out of the top bar.
3. Settings fold-out trim and the full Settings screen.
4. Diagnostics as a display mode with the three pages and the firmware readers.

Each step is one firmware generation's batch with its own bench walk-through by the owner; the mockups are the
acceptance reference, and `docs/plans/diagnostics-review.md` section 1 lists what must not be lost from today's screens.
