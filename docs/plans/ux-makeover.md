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

## 3. The three bars

- **Top bar**: hardware state and global tools, identical in every mode: Menu, channels CH1/CH2, brightness icon,
  ACQ (time base and sample rate), Trigger, Generator, battery, MCP chip. Removed: the version string (it lives in
  Diagnostics) **(owner)**; "Move fast" moves to Settings, Controls. Where a mode drives one of these itself (the
  spectrum's Span sets the time base), the button shows the value and its menu says "set by Span" instead of fighting
  the mode.
- **Right bar**: two pages toggled by **CTRL**, as the stock scope has today **(owner)**: the first page holds actions
  on what is shown (RUN/STOP, AUTO, cursors or markers, measures, save picture, save data, grid, trigger 50 %), the
  second page the parameters of the mode and its variant. Each slot keeps its place and meaning across modes; a slot a
  mode cannot use stays empty rather than showing a control that does nothing.
- **Bottom row**: readouts only (measurements, the spectrum's one-line header stays at the top of the plot). The
  spectrum and phosphor parameter strips move into the right bar's CTRL page **(owner)**; the bottom row returns to the
  plot in every mode.
- **Warnings**: top-middle of the plot in every mode, stating consequence and action **(owner)**; readouts such as
  wfm/s stay with the parameters.

## 4. Per-mode right bar

| Slot | Scope, actions | Scope, CTRL | Spectrum, actions | Spectrum, CTRL | Phosphor variant adds to Scope CTRL |
|---|---|---|---|---|---|
| 1 | RUN/STOP | Display: Trace / Phosphor / X-Y | RUN/STOP | Analyse: CH1 / CH2 / both | Persistence |
| 2 | AUTO (auto setup) | Filter | AUTO (spectrum auto) | FFT length | Palette |
| 3 | Time cursors | Average | Marker (peak) | Window | Clear |
| 4 | Volt cursors | Roll | dB cursor | Scale | |
| 5 | Measures | Trace style | (empty) | Ref level | |
| 6 | Save picture | Memory depth | Save picture | Average | |
| 7 | Save waveform | 50 % trigger | Save spectrum (CSV) | View: Spectrum / Waterfall / Density | |
| 8 | Grid | | Grid | Reset | |

Phosphor's persistence, palette and clear appear in Scope's CTRL page only while Display is Phosphor; X-Y has no extra
parameters. The spectrum's Auto moves from the strip to the AUTO slot (one Auto per screen). Bode's bars are defined
when it ships (docs/plans/bode-design.md section 4 is the starting point).

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
2. Right bar CTRL pages for Scope (Display variant with Phosphor and X-Y moved in) and for Spectrum (strip removed);
   warnings top-middle in phosphor; version string and Move fast out of the top bar.
3. Settings fold-out trim and the full Settings screen.
4. Diagnostics as a display mode with the three pages and the firmware readers.

Each step is one firmware generation's batch with its own bench walk-through by the owner; the mockups are the
acceptance reference, and `docs/plans/diagnostics-review.md` section 1 lists what must not be lost from today's screens.
