# The UX makeover

The open firmware started from Atlan4's 1.00o5 interface, which grew feature by feature on top of the stock FNIRSI
screens. By 2026-10-19 the project had added a spectrum analyser, a phosphor view, a new calibration and many
diagnostics, each placed where there was room. That day the owner and the agents rebuilt the interface as one
system. This article explains how: mockups first, adversarial reviews, and one rule above habit, consistency over
muscle memory. It ends with the rules that came out of it.

Sources: the [UI guidelines](../ui-guidelines.md), the [UX makeover plan](../plans/ux-makeover.md), the
[UX review](../plans/ux-review.md), the [Diagnostics review](../plans/diagnostics-review.md),
[CHANGELOG.md](../../CHANGELOG.md) (fw 0.2.51 to 0.2.59) and the [bench log](../hardware-status.md).

## Before the makeover

The first UI rule came early, on 2026-10-18, from the owner: hide what the hardware cannot do, and compact the menus
rather than paint over gaps. The generator menu hid its Wave, Amplitude, Offset and Phase rows (the 1013D has no DAC)
and shrank from 393 to 169 px; the trigger menu went from 336 to 280 px (fw 0.2.6 to 0.2.8).

The trigger for the makeover was the diagnostics screen. On 2026-10-19 at 11:00 the owner pointed out that it showed
the unused stock compensation tables and "fw: PECO" as the FPGA. A redesign proposal was drawn as a design artifact.

## The first review: Diagnostics

A reviewer agent took the proposal apart as a sceptical electrical engineer (`diagnostics-review.md`). Its verdict:
the proposal was "a PC dashboard drawn at 800x480, not a scope screen". The main findings:

- About a third of the values did not exist in the firmware or could not be measured on the device: a calibration
  timestamp on units without a clock, a board temperature that is really the RTC module's, battery volts to two
  decimals from a 6-bit reading.
- A row labelled "Start-up: scope view" was 1.00o5's "Default start", which cycles the boot-mode byte in sector 710.
  A curious tap could leave the unit booting the stock firmware or FEL, which looks like a dead scope without
  `sunxi-fel`.
- Three destructive actions, DEFAULT, RESTORE and RESET, acted on one tap with no confirmation, two of them 8 px apart.
- The screen was modal. Its "live" numbers were stale, and while it was open the PC got E_BUSY.
- Status tiles were green on the exact counter pattern (17 suspends, 0 resumes) that had preceded a USB loss that
  morning.

Most fixes were subtractions, plus a list of small firmware readers. Diagnostics became a main-loop display mode, so
the PC keeps control while it shows.

## The second review: the whole interface

The UX plan then covered every screen. The first boards moved CTRL from the top of the right bar to the bottom, put
nine slots where there were eight, and replaced the measurement tiles with one line. The second review
(`ux-review.md`) began: "the boards are a clean picture of a scope that does not exist yet". It listed 24 lost
functions, each with where it lived in the code and where it should go. Among them:

- the CTRL page's CH1 and CH2 V+ / V- buttons, "the reason CTRL exists in 1.00o5";
- the record-position slider under the grid;
- Move fast, which is toggled in the middle of a drag;
- the volts-per-range calibration with an external DC source. "Clear calibration" also reset the volts factors, and
  nothing in the new flow restored them (finding I1).

It also checked every text against the device's bitmap fonts. Several lines did not fit; the Log page's bottom row
needed about 920 px of the 776 available.

The owner rewrote the plan that afternoon and in the passes after it. The top bar and the right bar kept their stock
positions, and the CTRL page kept its V+ / V- controls; Clear calibration keeps the volts factors; the volts
calibration became a service action. The review's findings were marked "Superseded" rather than deleted, so the
reasoning stays visible. Further passes checked revision 3 of the boards and the final plan.

## Consistency over muscle memory

The final plan settles one question explicitly: "consistent and logical over muscle memory" (owner, 2026-10-19). The
rule for placement:

- the ACQ and Trigger dropdowns hold what the hardware acquires;
- the right bar's first page holds what you do to the plot, in fixed slots;
- its CTRL page holds how the plot is shown and computed, in the same order in every mode;
- the bottom row holds readouts.

Where this moved something, the plan says why. Trace style left the ACQ menu for the CTRL page, since it is about how
the plot looks. X-Y left the Settings fold-out to become a Display variant of the Scope. The spectrum and phosphor
strips went, their readouts became cards, and Spectrum's AUTO SET runs the spectrum's own Auto. Where the stock layout
was already right it stayed: the eight right-bar slots at their stock positions, the V+ / V- steppers, Move fast in
the top bar.

The plan also defines the words. A **mode** changes what is measured (Scope, Spectrum analyser, Frequency response)
and is chosen only from the main menu. A **variant** changes how the same measurement is shown (Normal, Phosphor, X-Y)
and lives in the mode's Display control. Then **parameter**, **setting**, **utility** and **action**. With those words
every placement question has an answer.

## The build, in four steps

Each step was one firmware batch with its own walk-through by the owner, with the mockups as the acceptance reference.

| Step | Firmware | What |
|---|---|---|
| 1 | 0.2.51 | main menu rows, the "USB drive" screen, the version string out of the top bar, the measurement cards |
| 3 | 0.2.55 | the Settings fold-out and a full Settings screen; delete confirmation |
| 2 | 0.2.56 | the CTRL page with fold-outs and icons, the steppers, Grid, the strips removed, one construction for the top bar chips |
| 4 | 0.2.59 | Diagnostics as a main-loop mode, the four-screen calibration wizard, the first-run screen |

Some details from these steps:

- **Cards.** Atlan4's card layout stayed, with the name in a block built like the top bar chips. Names are spelled as
  scope makers spell them: Freq, Period, +Width, -Width, +Duty, -Duty instead of Freg, Cycle, Time+, Time-, Duty+,
  Duty-. A new unit shows Freq, Vpp and Vavg.
- **Start-up.** One row, "At power-on", with three values in words: Open firmware; Boot menu, then open firmware; Boot
  menu, then stock firmware. FEL is no longer a start-up choice in the UI. Restoring defaults no longer writes the
  boot-mode byte.
- **Confirmation.** "Confirm to delete?" with NO / YES buttons of 74 x 50 px, 11 px apart, became "Delete from the SD
  card?" with Cancel left and Delete right in red, 160 x 44 px, 16 px apart.
- **Wizard.** Four screens, a title, one instruction, a result line and two buttons each; nothing written before Save
  calibration.

On 2026-10-19 at 16:02 the owner walked through fw 0.2.59: the menu, chips, handles, CTRL page, spectrum, Settings and
Diagnostics all worked, and a polish list was queued. The wizard's zero step failed, which turned out to be an FPGA
fault (see the [capture-fault article](adc-capture-fault.md)).

## Rules that came later

The guidelines kept growing as the owner used the result.

- **RUN/STOP shows the action** (fw 0.2.68). The general rule puts the confirming action on the right and keeps red for
  destructive actions. The owner made one exception, as on bench scopes: the button reads green "RUN" while stopped and
  red "STOP" while running. The chip beside the trigger shows the state.
- **Grid lines mean something** (fw 0.2.71). A grid line sits at a value its axis names. Log frequency axes get a line
  at every decade, with 2 and 5 lines when the span is under three decades. One function draws the rule for both
  frequency views and is host-tested.
- **Time/div drives the sample rate** (fw 0.2.76). A tap zoom picks the time/div, and the rate follows from one ladder:
  the fastest rate whose record covers the ten divisions with at least ten samples per division. The owner's verdict:
  the zoom ladder "seems to work great". The Spectrum got the same idea with its span (fw 0.2.77), and a pan never
  changes the rate (fw 0.2.81).
- **The slider track shows the whole** (fw 0.2.73). The row under the plot draws the whole record, spectrum or overview
  sweep, with the window over it, and a tap jumps there. The owner found the first window drawing "doesn't work quite
  well"; fw 0.2.74 brought back a box with a margin.
- **Host commands leave the modal screens** (fw 0.2.78). The owner: "if the agent sends commands it just exits all
  modals, since that wouldn't be destructive; just exiting to the base screen". Every modal loop leaves by its own cancel path when the host needs the
  main loop. Only a calibration measurement, a file save or USB drive mode still answer E_BUSY, with the reason.

## The rules, in short

From [docs/ui-guidelines.md](../ui-guidelines.md):

- **Colour carries state; words carry meaning.** Green is verified or active, amber needs attention, grey not
  available, and a word always sits next to the colour. Nothing blinks.
- **Fonts.** The device has bitmap fonts of 13, 16 and 24 px only, with no word wrap. Every line is laid out to fit its
  box at 6.7 px per character (13 px) or 7.1 px (16 px).
- **Touch.** New controls are at least 44 px tall and 80 px wide, with the hit area equal to the drawn area. Pages are
  reached by visible buttons, never by a swipe. What is drawn as a handle can be dragged by the handle.
- **Dialogs.** A title, one instruction, a progress or result line, two buttons. A destructive action always confirms
  with a sentence that states the consequence.
- **What is shown.** Only values the firmware has. Every unavailable state has text that says why. Trust decisions
  show a residual against a limit ("cores agree, 0.2 code"); raw numbers go to a detail page.
- **Process.** Mockups first, at 800 x 480 with the real fonts; then an adversarial review against today's firmware
  for lost functions, impossible values and contradictions; then the firmware batch. Nothing moves without a reason
  the plan states, and every removed control is listed with where its function went.

The makeover reorganised; it did not restyle. The black ground, the dark blue buttons and the channel colours of the
stock firmware stayed.
