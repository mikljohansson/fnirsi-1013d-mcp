# Adversarial review: UX makeover mockups against today's firmware

Reviewer brief (coordinator, 2026-10-19): an independent review of every artboard of the "1013D UX Makeover Screens"
design (Scope, ScopeCtrl, ScopeCtrlPhosphor, MainMenu, Spectrum, SpectrumCtrl, SettingsFoldout, UsbDrive, Wizard1-3,
Confirm, FirstRun, Main = Diagnostics Status, Calibration, Log, Settings) against `docs/plans/ux-makeover.md` (the
plan), `docs/plans/diagnostics-review.md` (the previous review, not repeated here unless a board ignores it) and the
firmware on main: `firmware/scope/menu.c`, `statemachine.c`, `variables.h`, `scope_functions.c`, `pc_hal_scope.c`,
`fnirsi_1013d_scope.c`, `spectrum.c`, `phosphor.c`, `firmware/README.md` sections 5 and 6.4-6.5, `docs/protocol.md`
section 14. Positions in the boards were extracted from their HTML; positions in the firmware are cited by function.
Inferences are marked.

Verdict: the boards are a clean picture of a scope that does not exist yet, and the plan's first promise ("the
positions of the top bar, right bar and plot stay as stock users know them") is broken by the boards themselves. The
right bar moved CTRL from the top slot to the bottom and shifted every button, so habit taps now hit a different
function; the CTRL page silently drops the CH1/CH2 V+/V- buttons that are its whole content today; the bottom row
replaces the record-position slider and the measurement tiles with one line for one channel; and several settings
and developer functions are promised "elsewhere" and drawn nowhere. The diagnostics boards follow the previous review
well, but they still overflow at the device's font widths, show values that need readers, and give contradictory
verdicts between pages. The worst functional gap is calibration: "Clear calibration" also wipes the volts-per-range
factors, and nothing in the new flow restores them.

## 0. Status after the plan correction (2026-10-19 afternoon)

The owner rewrote `ux-makeover.md` sections 3-4 and added 6a while this review was written: the top bar (minus the
version string), the right bar and both CTRL pages stay as today in Scope; the strips stay in the slider row without
their Scope field and the spectrum's Auto (AUTO SET runs it); measurement cards stay; Phosphor and X-Y become a Display
box in the ACQ dropdown; the wizard becomes four screens without Skip. Findings about the first boards' right bar,
CTRL page, bottom row and strips are marked **Superseded** below and kept so the reasoning stays visible. Resolved by
the correction: L1, L3, L4, L5, L6, L12, L16, L17, L21, L23, I2, the right-bar rows of 2.2-2.4 and section 3, and
decisions D1-D5, D8 and D13. Still open: L2, L7-L11, L13-L15, L18-L20, L22, L24, I1, I3-I5, the diagnostics rows of
2.2-2.4, and the new findings N2-N7 in section 2.5 (N1 is resolved by revision 3 of the boards).

## 1. Lost functions

Every control, readout, gesture or screen that exists today and has no place in the boards or the plan, worst first.
"Kept implicitly" items are listed after the table: the plan says they stay, but no board shows them and some are
affected by the new layout.

| # | Today (where) | What it does | In the boards / plan | Where it should go, or why it can go |
|---|---|---|---|---|
| L1 | CTRL page: CH1 V+ / V-, CH2 V+ / V- (`scope_setup_right_control_menu` state 1, `scope_channel_sensitivity_control`; touch in `handle_right_volts_div_menu_touch`, y 93-155, 178-238, 258-320, 343-403) | One-tap V/div change per channel without opening a menu; the reason CTRL exists in 1.00o5 | Gone. ScopeCtrl's eight slots are all parameters | **Superseded** (plan section 3 keeps it as today). Was: Keep on the Scope CTRL page (D2). For a stock user this is the most used button pair after RUN/STOP  |
| L2 | Volts-per-range ("input divider") calibration with an external DC source: "Set 300mV DC and press OK" .. "Set 15V DC" (`menu.c` `scope_show_Input_calibration` .. `scope_show_calibrating_15V_text`; started after the wizard when `calibrationfail`, `statemachine.c` "if(cal_wizard_run() && calibrationfail)") | Sets `input_calibration[]`, the volts factor per range. Due when sector 708 did not load (`scope_load_input_calibration_data`, default branch) and after RESET | No board. Wizard1-3 have three steps; FirstRun says calibration "takes about two minutes and needs both probes and the calibration post" | A fourth wizard step, shown only when due, that says it needs an adjustable DC source up to 15 V; and Clear calibration stops resetting the volts factors (I1) |
| L3 | Record-position slider under the grid, y 452-472 (`scope_draw_grid`, `kurzor_position` window of 181 or 223 px) | Shows which part of the record is on screen; follows the trigger point drag | The bottom row at y 453-477 now holds measurements | **Superseded** (plan section 3 keeps it as today). Was: Keep it. The phosphor strip reuses that row today (`scope_functions.c` "the phosphor strip over the grid's record-position slider"); once the strips move to the CTRL page the row is the slider's again  |
| L4 | Measurement tiles: up to 12 per channel, CH1 block and CH2 block at x 366, 3 per row, rows growing upward from y 426 (`scope_display_channel_measurements`); source per channel = the channel or REF1-4 / REF5-8; per-channel Hide (`scope_open_measures_menu`) | Up to 24 simultaneous readouts, of live or reference traces | The bottom row shows 4 values of CH1 in one line; no CH2, no REF source, no Hide | **Superseded** (plan section 3 keeps it as today). Was: Keep the tiles in the plot as today. One line holds about 6 values of one channel at the drawn spacing, so it cannot replace them  |
| L5 | Move fast / slow, top bar x 496 (`scope_move_speed`; touch in `scan_for_touch`, MOVE_FAST range) | Drag speed of traces, cursors and levels, toggled while positioning | Plan: Settings, Controls, "move speed". Settings board: no such row. Top bar: the slot x 491-546 is left empty | **Superseded** (plan section 3 keeps it as today). Was: Keep it in the top bar where the boards leave its slot empty. It is toggled mid-drag; three taps deep it is useless (D5)  |
| L6 | Waveform viewer mode of the scope screen: Next wave / Prev wave / Delete wave in the RUN, AUTO and SAVE WAVE slots, Show grid on the CTRL page, MENU turned into a return arrow (`scope_setup_right_control_menu` waveviewmode branches, `scope_main_return_button`) | Browsing saved waveforms with the live-scope controls | No board. The new order puts RUN in slot 1 and Save waveform in slot 7, so the viewer's substitutions land elsewhere | **Superseded** (plan section 3 keeps it as today). Was: Draw the viewer's bar (D8). Recommendation: today's substitutions in today's slots  |
| L7 | File browser right bar: Return, Select all, Select, Delete, Page up, Page down (`scope_setup_right_file_menu`); item view bottom bar: return, delete, previous, next, file name or RTC stamp (`scope_setup_bottom_file_menu`); "Confirm to delete?" NO / YES (`handle_confirm_delete`) | Managing saved pictures and waveforms | No board; the plan only renames the rows | Keep, but bring "Confirm to delete?" to the new confirmation pattern: its NO and YES are 74 x 50 px, 11 px apart |
| L8 | DC shift calibration pages (Other settings "DC shift calibration" on, then CTRL: right bar states 2-4 show centre, size and value per channel, V+/V- adjust them, "Next page", "END cal." saves through `scope_save_input_calibration_data`) | Manual trim of the trace offset scaling while watching the live trace | Plan and Calibration board: a "DC shift trim" button on the Diagnostics Calibration page | The trim needs the live trace. Launch it from Diagnostics but run it on the scope screen, as today |
| L9 | Calibration v4 tracking lines in today's diagnostics: "Track d .. e .. q .. age" and "Class cm .. (n) age" per channel (`menu.c` `scope_diagnostic_v4`; README 6.5 "Readout") | The live A-B and common-mode corrections of fw 0.2.50 | The Calibration board has none. The previous review said not to draw tracking "until it exists"; it exists since 0.2.50 | One line per channel on the Calibration page, in words: "Tracking: A-B +0.12 code, slope +0.31 %, 3 s ago" |
| L10 | Phosphor in X-Y (README "Phosphor view": "XY mode works the same way with one map") | Graded X-Y persistence | ScopeCtrl's Display cycles Trace / Phosphor / X-Y, so phosphor X-Y is no longer reachable | X-Y as its own toggle beside Display, or a fourth Display value (D7) |
| L11 | RTC On/Off (`scope_system_settings_ONOFF_RTC_item`, touch y 405-470 of the RTC menu) | Shows the clock in the top bar; stamps file names and FAT times | Neither the fold-out nor the Settings screen has it | Settings, Clock: "Show clock and stamp files: on / off" |
| L12 | RTC clock in the top bar at x 123, y 3 (`fnirsi_1013d_scope.c` "READ and show RTC time"; the MCP chip borrows the slot while a host is connected, `pc_hal_scope.c` `pc_draw_chip_into`) | Time of day | The plan's top-bar list omits it; the boards show only MCP | **Superseded** (plan section 3 keeps it as today). Was: Keep the shared slot and say so in the plan  |
| L13 | RTC editor: hour, minute, day, month, year with < > buttons (RTC menu) | Setting the clock | Fold-out and Settings show a "Time and date" row; no editor board | Keep today's editor; the plan should say the row opens it |
| L14 | Dev mode (Other settings): "DEV mode" at 670,50, trigger position values, raw trigger address over the top bar at 500,10 (`scope_functions.c`), touch coordinates in the generator and keypad menus (`scope_test_TP`); it also lets a stock or FEL "Default start" skip the boot menu (`statemachine.c` `close_open_menus`: "if (dev_mode == 0) ... boot_menu_start \|= 0x04") | Developer overlay, and an unguarded boot path | Settings board: "Developer options (FEL start, debug overlay): Diagnostics, Log page." The Log board has nothing of the kind | Draw the developer page or record that the overlay is dropped; the boot side effect is D11 |
| L15 | Default start = FEL (sector 710) | Bench loop without the boot menu | Plan: "FEL only in a developer section"; no board has one | The developer page of L14, with a confirmation saying the next power-on shows a black screen until a PC runs sunxi-fel |
| L16 | ACQ tab state: orange tab and "Long" in roll mode, "avg" with averaging (`scope_acqusition_settings`) | Roll and averaging visible from any page | A plain green "ACQ" tab; Roll and Average show only on the CTRL page | **Superseded** (plan section 3 keeps it as today). Was: Keep the tab marks; roll changes everything on the screen and must be visible on the actions page  |
| L17 | Battery percentage inside the icon when not charging (`scope_battery_status`, `display_decimal(708, 4, level*5)`) | Charge level as a number | The icon only | **Superseded** (plan section 3 keeps it as today). Was: Keep, or drop on purpose; the previous review called the scale coarse, so dropping is defensible  |
| L18 | Touch test "Xtouch / Ytouch" on today's diagnostics (`scope_open_diagnostic_view`) | Checking the GT911 mapping | On no Diagnostics page | The developer page of L14, or drop |
| L19 | USB screen: leave by tapping the scope picture (x 90-250, y 210-320); refusal "Not allowed! Please unmount disk in PC first!" while mounted (`scope_setup_usb_screen`, `scope_display_error`) | Safe exit from mass storage | UsbDrive board: an Exit button bottom right, no refused state | Keep and draw the refusal ("Eject the drive on the PC first"); keep the picture tappable for one release |
| L20 | Restore screen state "Need input calibration procedure !" (`scope_setup_restore_screen`, `calibrationfail`) | Says the volts factors are defaults | FirstRun has one "Calibration missing" line | FirstRun says when the DC-source step (L2) will be needed |
| L21 | Spectrum strip Ref level: left half of the field steps down, right half up (README "Spectrum view" field table) | Two-way ref level over -100..+60 dBV | A 62 px "Ref level" button; the boards do not say how it goes down | **Superseded** (plan section 3 keeps it as today). Was: Halves of 62 px are 31 px targets: give it an up/down pop-up, or a drag on the dB axis  |
| L22 | Spectrum span: tap left/right of the plot centre zooms, drag pans (README "Gestures on the plot") | The only span control | Plan section 3 names "the spectrum's Span" as if it were a control; no board has one | Keep the gestures and say in the plan that span has no button, or add Span to the CTRL page |
| L23 | Pass time "pass NN ms" under the acquisition menu's Memory header (README "Pass time in the acquisition menu") | The cost of a memory depth, where it is chosen | ScopeCtrl duplicates Memory depth without it | **Superseded** (plan section 3 keeps it as today). Was: Memory depth in the ACQ menu only (D2), or the pass time on the button  |
| L24 | Hidden but live touch areas on today's diagnostics: New autosetup, DC shift calibration, Boot menu, Default start, Lock move cursors at x 328-378, y 30-220 (`handle_diagnostic_view_touch`; their drawing is commented out) | A blind tap changes the boot mode | Not in the boards (correct) | The main-loop rewrite must not carry these hit areas over; until then they are a hazard on today's screen |

Kept implicitly (the plan says these stay in their menus; no board shows them): the channel menu (Open CH, Invert,
Coupling, V/A mode, Probe mode, the V/div list, REF mode, MATH mode; `scope_open_channel_menu`), the REF menu (REF 1-8),
the MATH menu, the trigger menu (mode Auto / Single / Normal, edge, source CH1 / CH2 / EXT, position 25 / 50 / 75 %,
Always 50 %; `scope_open_trigger_menu`), the generator menu (Power, Freq, Duty) with its numeric keypad (MIN / Default
/ MAX; `scope_open_keyboard_menu`), the acquisition menu (sample rate, time per division, memory depth, Trace, Roll,
Average, Filter; `scope_open_acquisition_menu`), the brightness icon (kept at x 75), the MCP and RUN / WAIT / STOP chips
(kept at x 127), the trigger edge symbol (kept), the cursor readout box at 25,49 (`scope_display_cursor_measurements`),
the plot gestures (drag a trace near its marker, a reference trace at x < 60, the trigger level at x > 660, cursors,
the trigger point; the X-Y drag; `scan_for_touch`), and the bootloader's boot menu (Atlan's binary, outside this
firmware: it cannot be restyled and keeps its own look).

## 2. Infeasible or inconsistent

### 2.1 Severity 1: wrong for the user or the unit

**I1. "Clear calibration" erases more than it says, and nothing brings it back.** Today's RESET resets
`input_calibration[]` to the 1.00o5 defaults `signal_adjusters`, the PWM centres to 860, the core compensations and the
DC shift values, drops the v3 block and sets `calibrationfail = 1` (`statemachine.c` `handle_diagnostic_view_touch`,
RESET branch). The Confirm board says "Both channels go back to uncalibrated. Readings may be several % off until you
run Calibrate again." Running Calibrate (Wizard1-3) restores zeros and gains, not the volts factors: those need the
external DC procedure (L2), which most owners cannot run. Change: Clear calibration clears the v3 block and the PWM
centres and keeps the volts factors; if it must clear them, the confirmation says "The volts-per-range factors also
return to defaults. Restoring them needs an adjustable DC supply up to 15 V."

**I2. Superseded (plan section 3 keeps the right bar). The right bar contradicted the plan's first paragraph.** Plan: "positions of the top bar, right bar and plot stay
as stock users know them". Today: 8 slots of 51 x 50 px at a 60 px pitch, CTRL at the top (y 5), then RUN/STOP 65,
AUTO SET 125, T CURSOR 185, V CURSOR 245, MEASURES 305, SAVE PIC 365, SAVE WAVE 425 (`menu.c` button functions). Boards:
9 slots of 62 x 48 px at a 53 px pitch, CTRL at the bottom (y 428), RUN/STOP 4, AUTO 57, Time cursors 110, Volt cursors
163, Measures 216, Save picture 269, Save waveform 322, Grid 375. Section 3 has the consequences.

**I3. FirstRun's promise is false on exactly the units that see it.** A unit without a loaded sector 708 has
`calibrationfail = 1`, so the wizard's Done hands over to the external DC procedure (L2). FirstRun says "about two
minutes ... both probes and the calibration post". It also offers "Calibrate now" on a screen shown at power-on while
telling the user to wait 5 minutes. Change: show minutes since power-on ("The scope has been on 1 min; 5 recommended")
and state the DC-source step when it is due.

**I4. "Confirmation prompts" is a different setting under a misleading name.** Today's "Notification confirmation"
(`confirmationmode`) only decides whether file messages ("Failed to create file" and the like) wait for a tap
(`scope_functions.c` `scope_display_file_status_message`). The fold-out and Settings boards call it "Confirmation
prompts: on / shown", which reads as "switch off the are-you-sure screens" just as the boards introduce confirmations
for Clear calibration and Restore defaults. Change: "File messages: wait for a tap / close by themselves", and no
setting ever disables a destructive confirmation.

**I5. "At power-on: Stock FNIRSI firmware" does not do what it says.** Closing Other settings forces the boot menu on
for any non-zero default start unless dev mode is on (`statemachine.c` `close_open_menus`, "fnirsi and FEL firmware
always with boot menu for user"). The stock choice is therefore "boot menu, then stock firmware". Name the value so or
drop the rule; with dev mode removed (plan section 5) the rule alone decides it.

### 2.2 Severity 2: the board shows something the firmware cannot produce

| Board, element | What the firmware has | Change |
|---|---|---|
| Log, "Serial line settings 115200 8N1" | An EP0 setup event logs bRequest, bmRequestType and wValue low only (protocol row 42, type 5); the SET_LINE_CODING data stage is not logged | "PC set the line coding", or drop |
| Log, "Bus reset, high speed" | Type 1 carries no speed; POWER bits appear only in type 3 (rows 42, 53) | "Bus reset"; the speed needs reader 1 of the previous review |
| Log, "Suspend, POWER 0x60, frame 700" | Row 42 documents no a / b fields for type 2 | Check `usb_interface.c` before drawing; otherwise "Suspend" |
| Log, "Power-on, cold boot" | No event type marks power-on; cold vs warm is counter 49 (row 38); the first entries are type 16 / 19 init states | Synthesize the line from counter 49 and the first type 16, and say so in the decoder |
| Log, "FFT 4096 points 2.7 ms" | Only FFT_BENCH builds measure FFT time; README "Spectrum view" Costs: 10.9 ms for 4096 points (2.3 ms is 2048 points from SRAM) | Drop; the number is also wrong |
| Log, "Readout 0.3 us per byte" | A documentation constant (AGENTS.md hardware facts), not measured | Drop |
| Log and ScopeCtrlPhosphor, "Screen pass 15 ms", "66 wfm/s" at 3000 samples | README "Phosphor view": "about 30 per second at the default 3000 samples" on "the scope view's 30 ms pass" | Realistic sample data, or the owner will hold the firmware to 66 |
| Status, "MMU and caches on"; "PC connected"; Firmware tile "starts 0.2.25 from the card"; "Calibrated on fw 0.2.48, FPGA 0x1534, 12 min after power-on, 1 power-on ago"; "FPGA changed since calibration" | Need readers 10, 1, 2, 3 and 4 of `diagnostics-review.md` section 4; none is on main | Mark each in the board as "needs reader N" so the implementation order is honest |
| Status, "Cores agree 0.2 code at 0 V", updating once a second | The wizard computes the zero step's worst A-B at calibration time (README 6.4); the docs do not say the block stores it; nothing measures it live at 0 V | Label it "at calibration" and store it, or show the v4 tracker's d with its age |
| Calibration, Undo last calibration and Check on post; Status and Log, Save report to card; Log, restart record "keeps them on the card through a power cycle" | Readers 8, 7, an SD text writer and reader 5; none exists | Mark as new work |
| Scope, warning "CH1 overrange: increase V/div" and Vpp "clipped" | README documents overrange warnings only for the spectrum and phosphor views; the scope view has no warning line and the measurement code no clipped state (`scope_display_vpp`) | New feature; say so in plan step 2 |
| **Superseded** by the corrected plan. Spectrum, Marker "peak", dB cursor, "Save spectrum" (CSV); the plan now hides those slots until the features exist | The peak triangle is always drawn (`spectrum.c` `spec_glyph_marker`); there is no spectrum cursor and no CSV writer (the only "csv" in the tree is a commented-out call in `menu.c`) | Per the plan's own rule ("a slot a mode cannot use stays empty"), leave these slots empty until built |
| **Superseded** by the corrected plan. Scope and Spectrum, Grid button (GRID stays a viewer-only button) | Grid on/off exists only in the waveform viewer (`scope_draw_grid` is skipped only when `waveviewmode` and not `gridenable`); the live view always draws it, grid brightness 0 hides it | New function; drop it and free the ninth slot (D1) |
| FirstRun, "An amber Uncalibrated chip stays in the top bar" | No board shows where; the top bar's only free space is the Move fast slot | Place it or drop the sentence |
| Spectrum, ACQ "500KSa/s 200us/div" with "RBW 146 Hz" and a 0-100 kHz axis | RBW = 1.5 fs / N (README "Auto" step 4): 146 Hz at N 2048 is about 200 kSa/s; 500 kSa/s would show 0-250 kHz at full span | Fix the sample data; the canvas note says "ACQ shows the rate the span chose" |

### 2.3 Severity 2: contradictions between boards and with the plan

- **Superseded** by the corrected plan. **Phosphor parameters.** Plan section 4: "Phosphor variant adds to Scope CTRL" Persistence, Palette and Clear in
  rows 1-3, and "each slot keeps its place and meaning across modes". ScopeCtrlPhosphor instead replaces Roll (slot 4)
  with Persist and Trace style (slot 5) with Palette, and puts Clear in slot 8. The board's choice is the sensible one
  (roll mode and the Trace selector do not apply in phosphor: README "Phosphor view"); fix the plan's table.
- **Superseded** by the corrected plan. **Variant selector slot.** Scope's variant (Display) is CTRL slot 1, Spectrum's (View) slot 7; Average is slot 3 in
  Scope and slot 6 in Spectrum. That breaks the plan's slot rule. Put View in slot 1 and Average in slot 3 of
  SpectrumCtrl.
- **Superseded** by the corrected plan. **50 % trigger.** Plan section 3 lists "trigger 50 %" on the actions page; the section 4 table and the boards put it
  on CTRL slot 7. Today it is on the CTRL page at y 425 (`scope_50_percent_trigger_button`). Keep it on CTRL and fix
  section 3.
- **Superseded** by the corrected plan. **wfm/s.** Plan section 3: "readouts such as wfm/s stay with the parameters". ScopeCtrlPhosphor draws "66 wfm/s" in
  the bottom row at x 652, where CH2's measurements would go.
- **Warnings against the spectrum header.** The Scope warning sits at x 122-602, y 62-88. The spectrum's header line
  is at y 50 and 18 px tall. With warnings top-middle in every mode they overlap the header in the spectrum
  (inference from the coordinates). Put warnings at y 70 in every mode.
- **Settings promised, not drawn.** Plan section 5 Controls lists move speed; the Settings board has no such row. The
  Settings board sends developer options to the Log page; the Log board has none.
- **The fold-out "keeps its shape" but not its order.** Other settings moves from row 3 to row 5 (today: Screen
  brightness, Grid brightness, Other settings, Baseline calibration, X-Y mode, Notification confirmation, RTC;
  `scope_open_system_settings_menu`).
- **Calibration verdicts disagree between pages.** Status shows CH2 all green ("Calibrated v3", "Cores agree", "Class
  ratio 0.9 %"). The Calibration page shows CH2 noise "2.1 full Check" and a gated gain mismatch of 2.1 %, above the
  previous review's 1 % limit and not marked. CH1's "1.3 % full" is also above that limit, and Wizard3 reports the
  same 1.3 % as a success. README 6.4 defines the wizard's gain number as the full-class |G_A / G_B - 1| before
  correction, so either the Calibration page's word "corrected" is wrong or the number is a different quantity (D14).
- **Superseded** by the corrected plan. Section 6a redefines the wizard; N3 has what remains. **Wizard boards against the implemented wizard.** README 6.4 (fw 0.2.50): full screen, grey frame 10,10,780,460,
  buttons 150 x 52 at y 400 (Cancel x 30, Skip x 230, Retry x 430, main action x 620); step 1 offers Skip when a bound
  block exists and shows an orange USB warning; step 2's no-signal state offers Retry / Skip / Cancel, no Measure.
  Wizard1-3 draw a 650 x 420 panel over a live-looking scope (top bar, warning and right bar visible) with buttons at
  x 56 / 212 / 368 / 524, y 382. Wizard1 shows Start while "Measuring CH2 range 3 of 6" runs; Wizard2 shows Measure
  beside Retry in the no-signal state, and its second button has no label. The boards and the firmware disagree on
  every coordinate; one of them has to change.
- **Visual language.** Plan, first paragraph: "not a visual redesign: colours, fonts, button shapes ... stay".
  Diagnostics, Settings, Confirm, FirstRun and UsbDrive use a navy header (#0b1a33), navy buttons (#0f2447, #1f3a66)
  with borders, a new green / amber / red set and Verdana. The right bar changes from grey rounded 51 px tiles with
  16 px text to 62 px dark tiles with a border and 13 px text. Either that sentence goes or the new screens use the
  stock palette (dark grey boxes, yellow active, green toggles; `menu.c` main and system menus).
- **Unavailable states.** The previous review's finding 13 (stock FPGA, no RTC, v1 calibration, no card, FEL-loaded)
  is still drawn only in the best case. The Clock group says "(RTC module fitted)" and nothing else varies.
- **Spelling.** Firmware "Spectrum analyzer" (`pc_hal_scope.c`), boards "Spectrum analyser". Pick one.

### 2.4 Severity 3: text that does not fit the bitmap fonts

Widths estimated at 6.7 px per character for font_2 (13 px), 7.1 px for font_3 (16 px), 11.5 px for font_5 (24 px).
The firmware has no word wrap: `display_text` (`display_lib.c`) draws one line, so every wrapped HTML paragraph in the
boards is line-breaking work the boards hide.

| Board, text | Space | Width | Fix |
|---|---|---|---|
| Calibration, bottom row: Calibrate, Check on post, Undo last calibration, DC shift trim, Clear calibration (min 140 px, 18 px padding, 12 px gaps) | 776 px | about 810 px; Clear calibration is pushed against its neighbour, under the 16 px rule of the Confirm pattern | Clear calibration on its own row, or DC shift trim off the row (it runs on the scope screen, L8) |
| Status, bottom row: Calibrate, Save report to card, "Values update once a second. PC control stays active on this screen." | 776 px | about 800 px | Shorten the note to "Updates every second" |
| Log, bottom row: two buttons and "The report holds identity, counters, this log and the calibration block as one text file." | 776 px | about 920 px | Drop the note, or put it above the buttons |
| Status, "Calibrated v3 bound, gains measured" per channel column | about 225 px | about 240 px | "Calibrated v3" alone; the rest on the Calibration page |
| Status, provenance line (113 characters) | 754 px | 757 px | Shorten |
| Calibration, column header "Zero A / B gated" | 92 px column | 107 px | "Zero gated" with "A / B" as a sub-header |
| Calibration, "0.13 gated, 2.1 full Check (limits 0.3 / 1.0)", "centre 870, size 100, value 59136 (model inputs)", "range 2, lever 88 codes, both levels seen" | 236 px | 275-322 px | Two lines each, or shorter words |
| **Superseded** by the corrected plan. ScopeCtrl / SpectrumCtrl values in font_3, today's right-bar font (`scope_control_button`): "10 dB/div", "Ref level", "Waterfall", "CH1 + CH2" (firmware text, `spectrum.c`) | 62 px button with a 1 px border and rounded corners | 64 px | font_2 and short forms: "10dB", "Ref", "Wfall", "1+2" |
| **Superseded** by the corrected plan. The same values in font_2, as drawn | 62 px | 60 px | Fits with no margin; acceptable only without the border |
| Main menu, "Spectrum analyser" with today's row icon (label at x 60, `scope_main_menu_*`) | 147 px column | about 181 px | The board drops the icons; if they stay, two lines as today |

Checked and inside their boxes: the Scope top bar, the main menu rows without icons, the fold-out, the Settings rows
(2 x 358 px), the Wizard texts, Confirm, FirstRun and UsbDrive.

### 2.5 New findings against the corrected plan

**N1. The Display box does not fit the ACQ dropdown as drawn today.** The dropdown is 380 x 386 px at x 250
(`variables.h` ACQ_MENU_*). Its left part holds the sample-rate grid (y 8-155), a row of three 90 x 40 boxes at y 163
(Roll, Average, Filter; `scope_open_acquisition_menu`) and the time-per-division grid from y 216. Its right column
(x 315-369) holds Memory (from y 33, 36 px pitch) and Trace (y 241-374, four 54 x 25 boxes;
`scope_acquisition_trace_select`). There is no free 54 px column and no free 90 x 40 box for a three-value Display
selector (inference from those coordinates). Options: widen the dropdown by one 64 px column (it can grow to x 730),
or make Display a fourth box in the y 163 row by narrowing the three boxes to 68 px (font_3 "Average" is 50 px).
Revision 3 of the boards (AcqMenu) takes the first option: the box is 464 px wide (x 250-714, clear of the right bar
at 730) with a Display column of three 68 x 44 boxes at x 638. That fits. The firmware change is ACQ_MENU_WIDTH plus
the screen save/restore and touch area it sizes, and the column's touch handling in `handle_acquisition_menu_touch`.

**N7. AcqMenu (revision 3) shows memory depths the firmware hides in that state.** It draws Filter = CIC2+FIR and
offers 9000, 12000 and 15000 under Memory. With the Filter on, those depths are hidden because the 16-bit layout holds
at most 8190 samples (README "Removed and hidden menu options"). Draw the board with them hidden, or with the Filter
off.

**N2. Display = Trace / Phosphor / X-Y still loses phosphor X-Y (L10), and roll mode needs a rule.** Phosphor works in
X-Y today (README "Phosphor view"). Roll mode excludes phosphor and the spectrum (README "Removed and hidden menu
options"). With Display and Roll in the same dropdown, the plan must say what Roll on does while Display is Phosphor.
Recommendation: Roll on sets Display to Trace and hides the Phosphor value, per the hide-what-cannot-work rule.

**N3. Wizard section 6a without Skip blocks units it cannot fully calibrate.** fw 0.2.50's step 2 on a non-AL3
bitstream says "The gain step needs the AL3 FPGA: gains kept." and goes on (README 6.4); the stock bitstream 0x1432
has no generator to hold post levels (commands 0x50-0x52). Section 6a has no Skip and no such branch, so a public user
on the stock bitstream can only Cancel and never saves even the zero step. Change: without the generator, screens 2
and 3 become one line "Gain needs the AL3 FPGA: earlier gains kept" with Continue. Section 6a still has no
volts-per-range step either (L2, I1, I3).

**N4. Plan section 5 still lists "move speed" under Settings, Controls** while section 3 keeps Move fast in the top
bar. Give it one home: the top bar.

**N5. Plan section 5 still says "confirmation prompts"** for the file-message timeout (I4), and still removes Dev mode
without saying what replaces the boot-menu rule that depends on it (I5, L14).

**N6. Plan section 4 ("Only these" visible changes) is incomplete.** Not listed: Diagnostics as a main-loop screen
with three pages, what Clear calibration clears (I1), the new wizard (6a), "Restore default settings" no longer
writing the boot byte, the USB drive screen, and the spectrum's AUTO SET meaning. Each changes what an existing user
sees or gets.

## 3. Risky changes for existing users

| Change | Habit that breaks | Worth it? |
|---|---|---|
| **Superseded** by the corrected plan. CTRL from the top slot to the bottom, every action shifted up | A habit tap on CTRL (y 5-55) now hits RUN/STOP and stops the acquisition; a tap on RUN/STOP (y 65-115) now hits AUTO (y 57-105) and runs auto setup, which throws away V/div, time base and trigger; SAVE PIC (y 365-415) lands on Save waveform or Grid | **No.** The two worst outcomes are destructive and silent. Keep today's eight slots, pitch and order (D1) |
| **Superseded** by the corrected plan. CH1/CH2 V+/V- removed from CTRL | A V/div change needs the channel menu: 3 taps instead of 1 | **No** (L1) |
| **Superseded** by the corrected plan. 50 % TRIG from y 425 to y 322 | A habit tap lands on Memory depth or Trace style | No; with D1 it stays at slot 8 |
| **Superseded** by the corrected plan. ACQ-menu items duplicated on the CTRL page (Filter, Average, Roll, Trace style, Memory depth) | Two homes for one setting; the spectrum warning "Aliasing possible: set Acquisition > Filter" names one of them | Only if the ACQ menu loses them; otherwise no (D2) |
| Main menu: Spectrum row 1 to 2, Phosphor row removed, Settings row 3 to 5, Picture view 4 to 3, Waveform view 5 to 4 (today `pc_glue_main_menu_items`, `scope_main_menu_*`) | Settings and the viewers move; Diagnostics and USB keep rows 6 and 7 | Yes. A one-time cost, and the risky USB row stays last |
| Spectrum row hidden in roll mode (README "Removed and hidden menu options") under a fixed new order | Rows below it move up by one, only in roll mode | No: keep the row and leave roll mode on entry (D10) |
| Phosphor from a menu row to CTRL > Display | One more tap, but reachable from the screen | Yes |
| **Superseded** by the corrected plan. Spectrum fields from the strip to the CTRL page | One more tap (CTRL) per change; the plot gains the strip's row | Yes |
| **Superseded** by the corrected plan. Move fast out of the top bar | The speed toggle mid-drag becomes unreachable | **No** (L5) |
| Fold-out: Baseline calibration and X-Y mode leave, Other settings moves to the last row | Calibrate becomes Menu > Diagnostics > Calibrate, the same tap count | Yes, with Other settings kept at row 3 |
| USB drive exit from the picture to a button at bottom right | The first exit attempt misses | Yes, if the picture stays tappable for a release |
| Spectrum: AUTO SET runs the spectrum Auto; T CURSOR, V CURSOR, MEASURES and SAVE WAVE hidden (plan section 3) | Today AUTO SET in the spectrum view runs the scope auto setup (1.00o5 right column, README "Spectrum view"); a user who relied on that loses it there | Yes: one Auto per screen, and the hidden slots keep their places |
| X-Y from System settings and Phosphor from the main menu to a Display box in ACQ | X-Y users look in the fold-out, phosphor users in the menu | Yes, if N1 and N2 are solved |
| "Notification confirmation" renamed "Confirmation prompts" | Users switch it off to silence messages and believe they switched off safety prompts, or the reverse | **No**, rename (I4) |
| **Superseded** by the corrected plan. Measurements from in-plot tiles to a one-line bottom row | CH2 and REF measurements and anything beyond about 6 values disappear | **No** (L4) |

## 4. Open decisions for the owner

1. **Superseded** by the corrected plan. **D1 Right bar geometry.** Recommendation: keep today's eight slots at a 60 px pitch with CTRL on top and today's
   order (CTRL, RUN/STOP, AUTO, T cursor, V cursor, Measures, Save picture, Save waveform), drop the live Grid button,
   and map the spectrum's analogues onto the same slots.
2. **Superseded** by the corrected plan. **D2 What the Scope CTRL page holds.** Recommendation: CH1 V+/V-, CH2 V+/V- and 50 % trigger as today, plus Display
   in the remaining slot; Filter, Average, Roll, Trace style and Memory depth stay only in the ACQ menu with its pass
   time, so no setting has two homes.
3. **Superseded** by the corrected plan. **D3 Phosphor parameters without free Scope CTRL slots** (follows from D2). Recommendation: keep a one-line
   phosphor strip in the slider row while phosphor is on, which today's users already know.
4. **Superseded** by the corrected plan. **D4 Measurements.** Recommendation: keep today's tiles in the plot and the record-position slider under it.
5. **Superseded** by the corrected plan. **D5 Move fast.** Recommendation: keep it in the top-bar slot at x 496 that the boards already leave empty.
6. **D6 What Clear calibration clears.** Recommendation: zeros, gains and the v3 block only, keeping the volts
   factors, and a DC-source step in the wizard for units where `calibrationfail` is set.
7. **D7 X-Y and phosphor.** Recommendation: X-Y as its own toggle next to Display, so phosphor X-Y keeps working.
8. **Superseded** by the corrected plan. **D8 Waveform viewer bar.** Recommendation: draw a board for it with today's substitutions in today's slots.
9. **D9 Visual language of the new screens.** Recommendation: stock palette and bitmap fonts for Diagnostics,
   Settings, Confirm, FirstRun and USB drive, or strike "not a visual redesign" from the plan; do not ship both.
10. **D10 Spectrum row in roll mode.** Recommendation: always show it and switch roll off on entry with a warning
    line, so the menu rows never move.
11. **D11 Boot-menu rule and dev mode.** Recommendation: keep "stock firmware always with the boot menu" for everyone,
    name the Start-up values to match, and put FEL plus the debug overlay on a developer page in Diagnostics behind a
    confirmation.
12. **D12 Bode row.** Eight rows of 59 px need 472 px below y 46; 434 px are there. Recommendation: decide now to
    shrink the rows to 54 px when Bode ships, so the rows move once, not twice.
13. **Superseded** by the corrected plan. **D13 Spectrum actions that do not exist (Marker, dB cursor, Save spectrum).** Recommendation: draw the slots empty
    until each feature is built, per the plan's own rule.
14. **D14 Which calibration quantity and limit each page shows.** Recommendation: one table of quantities (mismatch
    measured at calibration, residual after correction, live tracker value) with one limit each, used by the wizard
    summary, Status and Calibration alike.

## 5. Final pass: revision 3 boards

Boards checked: Scope, ScopeCtrl, AcqMenu, Phosphor, Spectrum, SpectrumCtrl, Wizard1-4, Developer, ConfirmFel, against
`ux-makeover.md` sections 3, 4, 5 and 6a as of f6230f0, against today's firmware, against each other and for text
width. Not re-checked: MainMenu, SettingsFoldout, Settings, Confirm, FirstRun, UsbDrive and the three Diagnostics
pages. Skipped on request: where the Display box lives (owner decision).

### 5.1 What revision 3 and the plan now get right

- **Scope and ScopeCtrl match today to the pixel.** The right bar has eight 51 x 50 slots at 60 px pitch from y 5 with
  today's labels. The CTRL page has CH1 V+ at y 99 with its chip at 155 and V- at 185, CH2 at 264 / 320 / 350, and
  50 % TRIG at 425 (`CH1_VOLT_DIV_MENU_YPOS` 99, `CH2_VOLT_DIV_MENU_YPOS` 264, `scope_channel_sensitivity_control`).
  The top bar matches too: Move fast is at x 496, the MCP chip sits in the clock slot and the version string is gone.
  The record-position slider is at y 452 (a 726 x 20 box with a 181 px window, as in `scope_draw_grid`), and the cards
  stack up from y 426 with CH2 at x 366. L1, L3-L5, L12 and L16 are closed on the boards.
- **Spectrum and SpectrumCtrl.** The strip is back in the slider row with the plan's eight fields. Ref level shows its
  `<` and `>` halves, which closes L21. The ACQ tab reads 200KSa/s and 500us/div, consistent with RBW 146 Hz at N 2048
  and a 0-100 kHz axis. T CURSOR, V CURSOR, MEASURES and SAVE WAVE are empty in their slots. The CTRL page is the
  Scope one. The y 70 warning slot now clears the y 48 header.
- **Phosphor.** The strip in the slider row has Persistence, Palette and Clear, with "55 wfm/s" at its right end, as
  plan section 3 says.
- **AcqMenu.** The box is 464 px wide with the Display column at x 638, which closes N1. With the Filter on, Memory
  offers only 3000 and 6000 and says "Filter on: up to 6000", which closes N7.
- **At plan level**, sections 4, 5 and 6a now close several findings. I1 is closed: Clear touches zeros and gains
  only. L2 is closed by the "Volts calibration" service action. I4 is closed by "file message timeout". I5 is closed:
  stock firmware always comes with the boot menu. N3 is closed by the stock-bitstream branch, N4 and N5 by the
  section 5 rewrite, and N6 by the section 4 list. L11 (RTC on/off) and L14-L15 (the developer page) are closed too.
  Note that the procedure 6a names `scope_input_calibration` does not exist. Today it is the
  `scope_show_Input_calibration` box at `CALIBRATION_MENU_XPOS` 395, y 222, beside the System settings menu, driven by
  `calibrationopen` 2-9 in `handle_main_menu_touch`. Launched from a Diagnostics page it needs a host screen of its
  own.

### 5.2 Findings, worst first

**F1. "Start FEL at next power-on" is not one power-on, and the copy promises the wrong screen (Developer,
ConfirmFel).** Boot mode 2 in byte 31 of sector 710 makes Atlan's bootloader jump to FEL on every boot until the byte
changes (`firmware/README.md` section 3, boot-mode table). The open firmware cannot clear the byte, because it never
runs while that mode is set. The plan's boot rule (section 5: the boot menu is forced for anything but the open
firmware; today `close_open_menus`, "boot_menu_start |= 0x04" when dev mode is off) also means the next power-on shows
the boot menu with FEL preselected and a 2 s timer. It does not go straight to "the screen stays dark". The user can
escape by touching another button within 2 s.

Change the copy to: "Every power-on shows the boot menu with FEL selected and starts FEL after 2 seconds (dark
screen, PC with sunxi-fel needed). Touch PECO in the boot menu to start the open firmware instead. Set At power-on
back to Open firmware to stop this."

Also consider whether the row is needed at all. ENTER_FEL (README "Bench loop without the boot menu") already
reaches FEL from a running scope without touching sector 710.

**F2. The cards' text does not fit the card in the drawn font, and the names are inconsistent (Scope, Phosphor,
AcqMenu).**
- **Space:** a card is 118 px. The chip covers x+3..x+19 and the text starts at x+23, which leaves about 92 px for
  "NAME: value".
- **Fit:** values such as "Time+: 500.0us", "Cycle: 100.0ms" and "Vavg: -12.3mV" are 13-14 characters. In font_2 that
  is 87-94 px, so the longer ones overflow. Today's tiles draw the name in `font_0`, a fixed-width 8 x 16 font
  (`font_0.c`, 16 bytes per glyph, inferred 8 px advance), in which 14 characters are 112 px.
- **Names:** the boards mix "VMIN" and "VPP" with "Freq" and "Duty+". The plan's example is "VPP: 2.40V". Today's
  names are mixed case (`measurement_names`, including the "Freg" typo).
- **Change:** one case for all twelve names, font_2, no space after the colon, and a check of the longest
  `make_value_to_text` output for each metric against 92 px.

**F3. "Trace" means two things in one dropdown (AcqMenu).** The Trace style column is headed "Trace" (Linear, Dots,
Peak, Pro), and the Display column's first value is also "Trace". A user who picks "Trace" in Display has not chosen a
trace style. Change: head the column "Trace style", as plan section 3 calls it, or name the Display value "Normal" or
"Y-t". This holds wherever the Display control ends up.

**F4. The pass time is missing from AcqMenu.** Today the dropdown shows "pass NN ms" under the Memory header (README
"Pass time in the acquisition menu", the `PC_MASK_ACQ_MEMORY` glue). The board has the header at y 56 and 3000 at
y 81, with no line between them. This reopens L23. Keep the line.

**F5. The wizard still floats over a live-looking scope (Wizard1-4).** The panel is 630 x 300 at 50,90. Behind it the
board keeps the top bar, the right bar, the cards and the orange warning at y 70-96, which sticks 6 px under the
panel's top edge. In the firmware the wizard owns the screen, so touches outside its buttons do nothing (README 6.4).
Showing RUN/STOP and the menus invites taps that are ignored. fw 0.2.50 draws a full-screen frame at 10,10,780,460.
Change: either the full-screen frame, or dim the background, as Confirm and ConfirmFel do.

**F6. Wizard copy needs line breaks the firmware does not do, and Done departs from 6a's words.**
- **Line breaks:** screen 2's instruction is 112 characters and screen 3's 105. At 7.1 px that is about 795 and
  745 px for a 582 px text box. Each needs two pre-broken lines, because `display_text` does not wrap. Screen 1's 80
  characters (568 px) fit.
- **Done screen:** Wizard4 says "Measured during calibration; the scope corrects these:" and "CH1: A-B 0.2 code at
  0 V, gain mismatch 1.3 %, class ratio 1.1 %". Plan 6a says "cores matched to x code, gain mismatch y %" in plain
  words, and "class ratio" is new jargon.
- **Change:** use 6a's words, or say what the class ratio means to a user. D14 (which quantity, which limit) is still
  open: 1.3 % is above the previous review's 1 % provisional limit and the screen shows no verdict.

**F7. Wizard1 loses the USB-connected warning, and the running state is ambiguous.** fw 0.2.50 detects a USB host from
the MUSB frame number and shows "USB is connected: the PC ground can shift the zero by about a code. Unplug it, or
Start anyway." (README 6.4). The board's static instruction tells the user to unplug, but nothing confirms they did.
6a's "no extra sentences" should not remove a state line. Keep it in the result-line slot before the user taps
Calibrate.

While measuring ("CH2: measuring range 3 of 6"), the board still shows a Calibrate button. Its background is the
panel's own colour, so it is unclear whether it is disabled. Draw it disabled, or hide it, while the step runs.

**F8. Spectrum Auto has lost its progress and cancel feedback.** Today the strip's Auto field reads "Auto..." during
the 1-2 s probe, and any touch cancels it (README "Auto", fw 0.2.42). With Auto moved to AUTO SET there is no field to
show progress. Change: AUTO SET stays highlighted while Auto runs, and the warning slot says "Auto: touch to cancel".

**F9. SAVE WAVE hidden in the spectrum removes a working function (inference).** Today the spectrum view keeps
1.00o5's right column (README "Spectrum view"), so SAVE WAVE saves the time record behind the spectrum. Plan section 3
hides it "until a spectrum export exists". A user who wants the raw record of what the spectrum shows then has to
leave the mode first. Keep SAVE WAVE in the spectrum, saving the waveform as today. If it stays hidden, the plan
should say this is a loss, not a placeholder.

**F10. The Developer page does not cover everything dev mode did.** Today dev mode also does three things the page
does not mention:
- it shows touch coordinates in the generator and keypad menus (`scope_test_TP`);
- it draws the raw trigger address over the top bar at x 500-550, y 0-40, on top of the Move fast button
  (`scope_functions.c`, the `dev_mode` block);
- it lets a non-open-firmware default start skip the boot menu.

The page has one "Trigger debug overlay" row. Say whether that row brings back all three, and move the address
readout off Move fast. Today's diagnostics Xtouch / Ytouch readout (L18) has no home either; the Developer page is
the place for it.

**F11. Small consistency items.**
- **CTRL colour:** the active CTRL page draws its CTRL button orange (#ef9311) on ScopeCtrl and SpectrumCtrl. Today
  `scope_control_button` uses the active colour only while the button is pressed. If orange marks "page 2" from now
  on, add it to plan section 4's list of visible changes.
- **Plan wording:** section 5 still says the Developer page is "behind a confirmation". On the boards the page opens
  directly, and only Start FEL asks first, which is reasonable. Fix the plan's sentence.
- **Look:** the Developer and ConfirmFel boards use the navy look of the Diagnostics pages, which is open decision D9.
