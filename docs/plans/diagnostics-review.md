# Adversarial review: diagnostics redesign, settings placement and the menu tree

Reviewer brief (coordinator, 2026-10-19): an independent, sceptical electrical-engineering review of the diagnostics
redesign proposal (artboards "Diagnostics", "Calibration detail" and "Current" of the design artifact linked from
`docs/plans/task-queue.md`, item "Diagnostics view redesign"), the owner's settings question and the device's menu tree.
Every claim about what the firmware can report cites the file or protocol row that was checked. Inferences are marked.

Sources read: the three artboards; `docs/hardware-status.md` 2026-10-18 late evening to 2026-10-19 11:25;
`docs/protocol.md` 8.2 (GET_INFO, GET_STATUS, GET_BATTERY), 8.7 (GET_CAL), section 14 rows 28, 38, 40, 42, 48-53;
`firmware/README.md` sections 3, 5 (removed/hidden options, spectrum, phosphor), 6.1-6.3; `docs/plans/task-queue.md`;
`firmware/scope/menu.c`, `statemachine.c`, `scope_functions.c`, `power_and_battery.c`, `adc_cal.c`, `build_id.c`,
`pc_hal_scope.c`, the font tables `font_2.c`, `font_3.c`, `font_5.c`, and `Makefile`.

Verdict in one paragraph: the proposal is a large improvement in intent (identity in the header, "fw: PECO" gone,
calibration shown as a model instead of 1.00o5 tables) but it is a PC dashboard drawn at 800x480, not a scope screen.
About a third of its values do not exist in the firmware or cannot be measured on the device, several green states are
shown on the exact conditions that broke the bench this week, one control is mislabelled in a way that can make the
unit boot the stock firmware or FEL, and three one-tap destructive actions sit in the bottom bar. The fixes are
mostly subtractions plus a handful of small firmware readers (section 4).

## 1. Findings, ranked by severity

### S1. Findings that mislead the user or put the unit at risk

**1. "Start-up: scope view" is not what that control does.** The row is 1.00o5's "Default start" item. It cycles the
bootloader's default image in the boot-mode byte of sector 710: 0 open firmware, 1 stock FNIRSI firmware, 2 FEL
(`statemachine.c` "touch default start", the `boot_menu_start & 0x03` cycle; `menu.c`
`scope_system_settings_open_other_settings_menu` reads it from `STARTUP_CONFIG_ADDRESS`; boot modes in
`firmware/README.md` section 3). "Boot menu at power-on" is bit 2 of the same byte. A curious user who taps "Start-up"
to see what changes can leave the unit booting the stock firmware or, worse, FEL, which looks like a dead scope to
anyone without `sunxi-fel`. Change: remove both rows from Diagnostics. In Settings, show one row "At power-on" with the
words "Open firmware", "Boot menu, then open firmware", "Stock FNIRSI firmware". Offer FEL only in a developer
section, and confirm any change with a sentence that says what the next power-on will do.

**2. Three destructive actions are one tap away, side by side, with no confirmation.** Today DEFAULT, RESTORE and
RESET act on the first touch (`statemachine.c`, diagnostics loop, hit areas x 40-90, 140-190, 240-290, y 420-460).
DEFAULT does more than its name says: `scope_reset_config_data` writes sector 710 immediately (boot mode 4, the boot
menu on) and reloads the calibration from SD, discarding a calibration change still in RAM (`scope_functions.c`, end of
`scope_reset_config_data`). RESET sets PWM centres 860 and zero compensations in RAM; the next power-off saves those
into sector 709, after which the v3 block in 708 no longer binds and the unit is effectively uncalibrated. "Restore
calibration" only reloads what is on the card, so after that power-off there is nothing left to restore. The
mockup keeps all three in the bottom bar, 36 px tall, Restore and Reset 8 px apart. Change: take all three off the
main diagnostics screen (section 2 says where they go). Every destructive action gets a confirmation screen that
states the consequence in words, with Cancel as the large default and the action button at least 44 px tall and 16 px
away from anything else.

**3. The mockup shows values the firmware cannot produce.** Each needs a different value or must be dropped:

| Mockup value | What the firmware has | Change |
|---|---|---|
| Calibration "v3, 2026-10-19 08:15", gain step "08:16" | The v3 block has no time field (row 51 layout). Most units have no clock: the DS3231 is an add-on module, detected at boot (`firmware/README.md` "Removed and hidden menu options"). | Show provenance that exists or can be stored without a clock: firmware build and FPGA word at calibration, power-on minutes at calibration, power-on count since (section 4, items 3-4). Show the date only when a DS3231 is present. |
| "Board 31 °C" | The DS3231's own temperature register, only with the module (`menu.c` `scope_open_diagnostic_view`, `PC_FEAT_RTC`; README table "Temperature (diagnostics screen)"). Not a board, ADC or front-end temperature. | Label it "RTC module 31 °C" and hide the line without the module. Do not use it as a warm-up criterion. |
| "4.08 V", "92 %" | A 6-bit KEYADC reading, about 31 mV per step by the firmware's own charging offset of 7 steps = 0.21875 V, corrected for screen brightness, then mapped to a level of 0..20 (`power_and_battery.c` `battery_check_status`). GET_BATTERY marks the scale [U] (protocol 8.2). | Show "Battery 18 of 20, charging". Two decimals of volts is false precision from an uncalibrated 6-bit reading. |
| "High speed" | No protocol field reports the bus speed (GET_LINK_STATS rows 28/38/53). The MUSB POWER bit 4 holds it and only appears inside event type 3 (row 53). | Needs a reader (section 4, item 1). |
| "0 CRC errors" | Counters 12/13 are the PC protocol's header and payload CRC failures (row 28). USB packet CRC errors are handled by the controller and are invisible to firmware. | Label "0 bad frames" and count 12 + 13 + 14 (oversize) + 15 (version). |
| "Boots fw 0.2.25 from card" | SD_INFO reports for sector 750 only the eGON magic, checksum state, length and stored checksum (row 40), not a version. | Needs a reader (section 4, item 2). The identity is readable: `build_id.c` is linked last, so the image's last bytes hold its commit hash and time. |
| "Settings and calibration saved" | Settings reach sector 709 only in the power-off interrupt (`firmware/README.md` 6.1; "saved at power-off" throughout section 5). There is no dirty flag and no record of the last save's result. | Drop the line or add the reader (section 4, item 6). |
| "Tracking (live)", "updated 12 s ago", "tracking compensates" | Calibration v4 tracking is not on main; fw-cal3 is designing it (task-queue "Calibration v4", "Dispatched"). | Do not draw it until it exists. Reserve the space and show nothing, not a placeholder. |
| "Full-rate drift 0.7" | Nothing measures it on the device. The 0.7 code is a host measurement with shorted inputs (hardware-status 10:45, FIR bench), and it is a common-mode shift of both cores. Interleave tracking estimates the A-B difference and the gain ratio, so even v4 will not see a common-mode zero drift. | Drop it. A zero drift needs a known 0 V input. The post-based check in section 4, item 7 can verify gain and A-B agreement. |
| "Zeros verified" | GET_CAL byte 73 has: v3 loaded, bound to the PWM centres, divider gated, gains measured, gains carried over (row 51). Nothing verifies anything after the calibration. | Replace with states that exist: "Calibrated (v3)", "Calibration not bound: run calibration", "Gains not measured", "Gains carried over from an earlier calibration". |

**4. Diagnostics is modal, so its "live" numbers are stale and the PC loses the scope while it is open.** The view runs
in its own `while(1)` touch loop (`statemachine.c`, "Stay in this mode as long as the return is not touched"). No main
loop passes run, nothing is captured, and main-loop commands answer E_BUSY (AGENTS.md "Bench coordination"; protocol
row 30). So "15 ms per pass" is the last pass before the screen opened, uptime does not advance on screen, and any
tracking value cannot update. A developer running the MCP who opens Diagnostics to check the link breaks the link.
Change: make Diagnostics a main-loop display mode like the spectrum and phosphor views (`firmware/README.md` "Spectrum
view": "a display mode of the main loop, not a modal menu, so the MCP's polling ... keep working"), redrawing changed
values at 1 Hz. Only Calibrate and confirmation screens are modal. If that is too much work for the next generation,
label every value that comes from the main loop "when this screen opened".

### S2. Findings that hide the facts needed to judge health or trust

**5. The status colours are green on the failure signatures of this week's bench.** Every tile has a green dot. The
USB tile says "17 suspends, 0 resumes", the exact counter pair that preceded the device vanishing at 10:20 (row 53,
"Why"; hardware-status 11:25). The SD tile is green while the card boots 0.2.25 and the unit runs a FEL-loaded 0.2.48,
so a power cycle silently returns to old firmware (hardware-status 10-18 late evening: "The SD card still boots 0.2.25;
a power cycle returns to it"). Change: one written rule per state, and a word next to every colour, per the project's
convention that warnings state consequence and action (`firmware/README.md` spectrum "Warnings", phosphor strip
warnings). Rules:

| Tile | Green "OK" | Amber "Check", with the sentence shown | Red "Fault" |
|---|---|---|---|
| Firmware | running image = card image | "Running fw X from RAM. A power cycle starts fw Y from the card." | |
| Restarts | counter 49 = 0 | | counter 49 > 0: "Restarted N times since power-on: exception <type> at <PC>" (counters 49-59, row 38) |
| USB | configured, frame errors 0 | soft reconnects (61) > 0: "Link was lost and re-established N times"; frame errors > 0: "N corrupt requests from the PC" | |
| Calibration | v3, bound, gains measured, FPGA word unchanged since calibration | not bound, gains missing, FPGA word changed, calibrated before 5 minutes of warm-up | no calibration (v1): "Uncalibrated: readings may be several % off; run Calibrate" |
| Battery | level >= 4 of 20 or charging | level < 4: "Low battery: readings and SD writes may fail" (inference: the 1.00o5 code treats the bottom of the scale as end of operation) | |

Owner decision 2026-10-20 18:12 (fw 0.2.100), superseding the Restarts row: OK = everything normal, Check = something to
fix at some point, Fault = something critically bad. Only a real exception (types 1..6) is a crash; a deliberate restart
(type 0, a jump to the image entry: a FEL load, Enter FEL, an update, a self-restart) is normal. OK "No crashes." "Up 27
min."; Check, a stored record of a crash before this run: "Crashed once before this run: data abort." "If it happens
again, save a report." "Up 27 min."; Fault, counter 49 > 0 with a real exception: "Crashed 2 times this run: data abort
at 0x80014a2c." "Details on the Log page." No exception jargon on the Status page; the Log page keeps the details.

Colour-blind safety: the green and amber chosen (#35c26a, #ffd262) are hard to tell apart for protan and deutan
viewers at an 8 px dot. The word ("OK", "Check", "Fault") carries the state and the colour repeats it. Drop the dots.
The value text itself takes the state colour, as the project's boxes do ("box labels show the value and colour shows
the state").

**6. Raw numbers are shown where a residual against a limit is what decides trust.** "Zero split A-B, gated 5.8
codes" is the uncorrected hardware difference between the two ADC cores. It is large on a healthy unit (CH1 5.35 codes,
`firmware/README.md` 6.1) and the v3 correction removes it. Showing 5.8 next to 0.0 for CH2 invites the reading that
CH1 is broken. What a user needs is the residual after correction and whether it is inside a limit. Gains "1.000 /
0.999" are four numbers where one mismatch percentage decides. The lever "88 codes, range 2" only proves the probe was
on the post. Change: the main screen shows per channel one line of verdicts; the numbers go to the detail screen. The
limits below are provisional. The bench values they are based on are cited. The limits themselves are inference until
a second unit has been measured.

| Quantity (per channel) | Shown as | OK | Check | Basis |
|---|---|---|---|---|
| Core A-B residual, gated, at 0 V | code | <= 0.5 | > 0.5 | overnight log 0.0-0.3 (hardware-status 04:00); requires the check in section 4 item 7 or shorted inputs |
| Core gain mismatch per class, after correction | % | <= 1 % | > 1 % | firmware rejects at 8 % (row 51 "cores differ"); uncorrected full class was 3.3 % (hardware-status 10-18) |
| Class gain ratio, full / gated | % from 1 | <= 2 % | > 2 % | firmware rejects outside 0.9..1.1 (row 51); measured 1.1 % |
| Noise RMS, gated / full | code | <= 0.3 / <= 1.0 | above | stored 0.13 / 0.75 on CH1; the mockup's CH2 2.1 would be "Check" |

Owner decision 2026-10-20 18:13 (fw 0.2.100), superseding the noise row: the noise rows measure the calibration's
open-input condition, so their limits are the zero step's own, "Noise RMS, inputs open", gated 1.0 code, full rate 3.0
codes (`adc_cal.h` `ACAL_MAX_NOISE16_*`, used by the wizard and Diagnostics). Bench, stored worst range per core: full
CH1 B 1.19, CH2 A 2.75 codes (5 V/div, the high-impedance ranges the worst; 0.5..0.6 from a low-impedance source),
gated at most 0.50 / 0.38. The "provisional" note is gone; the class gain ratio limit (2 %) is unchanged.
| PWM centre per range | counts | within 860 +- 100 | outside | 860 is the 1.00o5 default (`statemachine.c` RESET); range is inference |
| Volts factor per range | % from the 1.00o5 default for that range | <= 3 % | > 3 % | defaults `signal_adjusters`; 3 % is inference |

Show the detail values with their unit and, where it helps, in volts at the current range (one code at range 2 is
about 35 mV from the 28.5 codes/V gain at range 2, hardware-status 10-18).

**7. Missing: what decides "is this measurement trustworthy".** Calibration validity depends on conditions the
screen does not record:

- **FPGA bitstream at calibration.** The 08:15 calibration was made on 0x1534; the flash got 0x1535 at 10:40 and the
  16-bit path then read 0.7 code off (hardware-status 10:45). The block should store the FPGA word and the screen
  should say "FPGA changed since calibration: recalibrate".
- **Warm-up at calibration.** Atlan recommends 5 minutes (hardware-status 10-18, fw-cal on CH2 drift); the full-rate
  class moved within 15 minutes of calibration (hardware-status 08:30). Store the uptime at calibration and say
  "Calibrated N min after power-on" in amber below 5 minutes.
- **Firmware build at calibration**, to tell a calibration from a different correction model apart.
- **Power-on count and operating hours since calibration**, as an age without a clock.

**8. Missing: what makes a bug report useful.** The project is public, and most owners have no MCP setup. The screen
should hold, and be able to write to the SD card as one text file ("Save report"), everything an issue needs:

- Identity: fw version, git hash and commit time (exists, `build_id.c`), protocol, FPGA word and variant, caps, the
  USB serial string (GET_INFO bytes 56-71; missing from the mockup), MMU and D-cache state (missing; the merge bug
  fixed in 39b98c4 shipped an image with the cache off and only the CP15 word revealed it, hardware-status 08:05).
- Restart record: count, exception type, PC, fault status and address (counters 49-59, row 38). It survives only a
  warm restart, so a crash followed by the user's power cycle is lost. It needs persisting (section 4, item 5).
- The event log (GET_EVENT_LOG, row 42), decoded to words, last 16 entries, on its own page.
- SD state: card size and sector 16/80/750 checks (SD_INFO, row 40), the card image's identity, settings sector
  checksum valid or defaults loaded.
- The calibration block in full (the detail screen) and the GET_CAL flags.

An MCP tool returning the same report as JSON keeps both paths identical.

**9. The footer statement about DC shift is false.** "Stock ADC compensation and DC-shift tables are not used with
v3" is half right. The 1.00o5 core compensations are unused once a v3 block is bound (row 51, GET_CAL bytes 74-79).
`dc_shift_value` still sets the 0 V target T of the v3 correction (`adc_cal.c` `out->shift`, `zero_q8`), and
`dc_shift_size` maps the offset to screen pixels for the trace and trigger marker (`fpga_control.c`, `pc_hal_scope.c`).
Change: say "1.00o5 core compensations: not used (v3 active)" and show the DC shift values in the detail screen as
inputs of the model.

### S3. Readability and interaction on the real panel

**10. The typography does not exist on the device.** The mockup uses 11 px uppercase letter-spaced labels, 12 px
text and 20 px monospaced values with anti-aliasing. The firmware has bitmap fonts of 13 px (`font_2`, average advance
6.7 px), 16 px (`font_3`, 7.1 px, digits 7 px) and 24 px (`font_5`, 11.5 px), plus 8 px and 32 px (`font_*.c`
FONTDATA). Mapping 11-12 px text to `font_2` makes it about 20 % wider, and several lines overflow:

| Text | Space in the mockup | Width in font_3 |
|---|---|---|
| "Settings and calibration saved" (tile) | 170 px | about 213 px |
| "Host connected, 0 CRC errors" (tile) | 170 px | about 200 px |
| "-0.259 / -0.256 codes per count (full), -0.259 / -0.261 (gated)" (detail) | about 260 px | about 450 px |
| "131.0 / 125.1" (detail column) | about 78 px | about 91 px (88 px in font_2) |

Change: `font_2` is the smallest size, used only for units and secondary labels. Values that decide something use
`font_3`. Tile headlines use `font_5`. No uppercase letter-spacing. Each tile holds at most three lines of 24
characters. Digits are 7 px in `font_3`, so right-aligned columns line up without a monospaced font.

**11. Touch targets and gestures.** The bottom-bar buttons are 36 px and the toggles 34 px tall, below the 44 px
minimum for a finger on this panel. The current firmware is worse than it looks: its DEFAULT/RESTORE/RESET buttons are
drawn 90 px wide but react only on a 50 x 40 px area (`statemachine.c`, hit tests above). "Event log and USB counters:
swipe left" introduces a gesture no other screen uses; elsewhere a drag pans the trace or the spectrum (`firmware/README.md` "Gestures on the plot").
Change: all buttons at least 44 px tall and 80 px wide, hit area equal to the drawn area, and the pages reached by
visible page buttons ("Status", "Calibration", "Log") in the header, never by a swipe.

**12. Too much on one screen, in four unrelated roles.** The main artboard holds about 45 values, 5 settings and 5
actions: health, calibration internals, preferences and maintenance. A user looking for "is it OK" has to read all of
it. Change: three pages of one modal-free view. "Status" holds identity, four tiles with verdict words, one calibration
verdict line per channel, and the buttons Calibrate and Save report. "Calibration" holds the per-range table,
provenance, thresholds and the maintenance actions. "Log" holds the event log and the restart record. Preferences
leave Diagnostics entirely (section 2).

**13. Unavailable values have no defined rendering.** The mockup shows a fully featured AL3 unit with a DS3231. Public
users will open it with the stock bitstream 0x1432 (no generator, so the gain step cannot hold static post levels: row
51 needs commands 0x50-0x52), without the RTC module, with a v1 calibration, FEL-loaded without a card, or on an older
image. Change: every field gets a defined "not available" text that states why, never a blank or a 0:

| Condition | Shown |
|---|---|
| No DS3231 | temperature line hidden; calibration age as power-on count only |
| Stock FPGA 0x1432 | "FPGA stock 0x1432: no generator, no filter; gain calibration not possible" |
| v1 calibration only | calibration verdict red, numbers hidden, Calibrate highlighted |
| v3 present but not bound | "Saved calibration does not match the current offsets: run Calibrate" (the existing text "v3 data stale: run base cal", `menu.c` `scope_diagnostic_adc_cal`, in plain words) |
| Gains not measured | "Gains not measured: probes were not on the post" |
| No SD card (FEL) | SD tile "No card: settings will not be saved" |
| Field from a newer firmware | not drawn (the host side decodes by length, rows 38 and 51) |

**14. Small consistency items.** The amber colour of "new", "scope view" and "free" on the setting toggles marks a
non-default value as a warning; the project uses green for "on" (`firmware/README.md`, fw 0.2.10 "Roll mode" toggle).
"Autosetup: new" and "DC shift calibration: stock" mean nothing to a new user (both are 1.00o5 internals). "Processor
15 ms per pass" is a performance figure without a reference; show it on the Status page as "Screen updates about 66
per second" only with the memory depth it applies to, or move it to the Log page. The "Current" artboard is not
today's screen: the start-up toggles in its right column are commented out in `scope_open_diagnostic_view`, and
the right column shows the v3 calibration data instead; reviewers comparing the two should know that.

## 2. Settings placement and a complete settings screen

**Recommendation: no preferences and no "Settings to defaults" on Diagnostics.** Diagnostics answers "is the unit
healthy and are its readings trustworthy"; a preference switched there changes what is being diagnosed. Calibration
is the exception that belongs there, because the user decides to calibrate from the calibration verdict and sees the
result on the same page. So:

- **Calibrate** (opens the wizard, task-queue "Calibration wizard"): on the Diagnostics Status page. The "Baseline
  calibration" row leaves the System settings fold-out.
- **Restore calibration**: remove. Since fw 0.2.43 the calibration writes 708 and 709 at once (`firmware/README.md`
  6.2), so the card always holds the latest calibration and "restore" only undoes an unsaved RESET. Replace it with
  **Undo last calibration** on the Calibration page, which needs the previous block kept on the card (section 4,
  item 8). That covers the real failure: a calibration made cold or with a probe off the post.
- **Reset calibration**: keep, renamed "Clear calibration", on the Calibration page behind a confirmation that says:
  "Readings use uncalibrated defaults until you calibrate again. The saved calibration is lost at the next
  power-off."
- **Settings to defaults**: on the Settings screen, renamed "Restore default settings", with a confirmation that says
  it keeps the calibration. It must stop writing the boot-mode byte (`scope_reset_config_data` writes sector 710
  today); start-up behaviour is its own setting.
- **DC shift calibration** (manual trim of the DC shift values, a 1.00o5 service tool): on the Calibration page as a
  service action, not a preference.

**The complete settings screen** replaces the System settings fold-out and its "Other settings" fold-out. Every
setting the firmware stores today (`scope_save_config_data` in `scope_functions.c`; menus in `menu.c`) falls in one
of four places:

| Setting (storage) | Today | Proposed place |
|---|---|---|
| Screen brightness (`screenbrightness`) | System settings | **Settings, Display** |
| Grid brightness (`gridbrightness`) | System settings | **Settings, Display** |
| Max brightness (`maxlight`) | top-bar icon | top bar (quick control) |
| Trace style Linear / Dots / Peak / Pro (`ACQ_trace`) | acquisition menu | acquisition menu |
| X-Y mode (`xymodedisplay`) | System settings | main menu, as a display mode (section 3) |
| Notification confirmation (`confirmationmode`) | System settings | **Settings, Controls** |
| Lock move cursors (`lockcursors`) | Other settings | **Settings, Controls**, words "Cursors: move freely / locked" |
| Move speed (`movespeed`) | top-bar button | top bar |
| New autosetup (`new_autosetup`) | Other settings | **Settings, Controls**, words "Auto setup: improved / original" |
| Add a tag to BMP (`tag_in_BMP`) | Other settings | **Settings, Files**, "Stamp screenshots with time or name" |
| Load Ref on start (`ref_on_startup`) | Other settings | **Settings, Files**, "Load reference traces at power-on" |
| RTC on/off (`onoffRTC`), Time & Date | System settings (DS3231 only) | **Settings, Clock** (DS3231 only, hidden without it) |
| Boot menu, Default start (sector 710 byte 31) | Other settings | **Settings, Start-up**, one row "At power-on" (finding 1) |
| USB-Serial mode (`USB_CH340`) | Other settings | **remove**: the default build forces CDC at boot (`Makefile` `PC_FORCE_CDC ?= 1`, `pc_glue_init`), so the saved value is ignored and the toggle lies |
| Dev mode (`dev_mode`) | Other settings | **remove**, or a developer page in Diagnostics: it draws trigger debug numbers over the trace (`fnirsi_1013d_scope.c`, `scope_functions.c`) |
| DC shift calibration (`dc_shift_cal`) | Other settings | Diagnostics, Calibration page (above) |
| Channel, trigger, time base, generator, acquisition, filter, measurements, cursors, math, reference slots, always-50 % trigger | their menus | stay in their menus (they are acquisition state, not preferences) |
| Spectrum and phosphor fields (words 208-210) | their strips | stay in their strips |

That leaves ten rows plus the defaults button: Display (2 sliders), Controls (3), Files (2), Clock (1-2, DS3231
only), Start-up (1). **It fits one screen**: two columns of five or six rows at 52 px is 312 px, plus a 40 px header
with page title and a 56 px bottom bar with "Restore default settings" and "Back". Group titles in `font_2`, row text
in `font_3`, each value as a word in the state colour, a tap on the row opens its choices in place. A second page is
only needed when undetectable hardware mods get switches (`firmware/README.md` "No option needs a manual switch yet").

## 3. Menu tree

**Recommended tree.** The main menu keeps its 147 px column of seven 59 px rows (`variables.h` `MAIN_MENU_*`):

1. **Spectrum analyser** (reads "Scope view" while on, as today)
2. **Phosphor display** (same)
3. **X-Y display** (same; moved from System settings, because it is a display mode that excludes the other two)
4. **Saved files** (one viewer with "Screenshots" and "Waveforms" buttons in its header, replacing the Picture view and
   Waveform view rows)
5. **Settings** (the full screen of section 2)
6. **Diagnostics** (Status, Calibration and Log pages; Calibrate and Save report on Status)
7. **USB drive**, second line "SD card"

Reasons: the order runs from what is used every session (views) to what takes the unit off-line (USB drive) last, so
the risky row is furthest from the thumb's habitual target. Views come first because the views exclude each other
and the user switches between them most. Merging the two viewers saves the row that X-Y needs.

**The rename of "USB connection".** The cable carries the PC control protocol at all times; this row switches the USB
port to mass storage, which disconnects the PC control port and the MCP (`menu.c` `scope_setup_usb_screen`:
`usb_device_disable`, `USB_CH340 = 0`, `usb_device_init`). Call it **"USB drive"** with "SD card" under it. The screen it
opens says, in `font_3`: "The SD card is now a USB drive on the PC. PC control (MCP) is disconnected until you leave
this screen. Eject the drive on the PC before tapping Exit." The current text "Off USB" says neither.

**Taps for common actions** (tap on the menu button counted):

| Action | Today | Recommended |
|---|---|---|
| Spectrum or phosphor on/off | 2 | 2 |
| X-Y mode | 3 (Menu, System settings, X-Y) | 2 |
| Open saved screenshots or waveforms | 2 | 2, plus 1 to switch type |
| Calibrate | 3 (Menu, System settings, Baseline calibration) | 3 (Menu, Diagnostics, Calibrate) |
| Screen brightness | 4 (Menu, System settings, row, slider) | 3 (Menu, Settings, slider) |
| Check health before a measurement | 2, then decode tables | 2, read four verdict words |
| Restore default settings | 3, no confirmation | 4 with confirmation |
| Save a bug report to the card | not possible | 3 |
| SD card to the PC | 2 | 2 |

**Alternative (cheaper, no viewer work):** keep the Picture view and Waveform view rows and leave X-Y in Settings, in
the order Spectrum, Phosphor, Saved screenshots, Saved waveforms, Settings, Diagnostics, USB drive. It costs one tap
for X-Y and leaves a display mode among the preferences; choose it only if merging the viewers does not fit the next
firmware generation.

**First run.** On the first boot without a bound v3 block (today the restore screen says "RUN baseline calibration
!", `menu.c` `scope_setup_restore_screen`), show one screen: the identity line; three checks in words (FPGA recognised
with its features; settings loaded from the card or defaults used; calibration missing); the sentence "Calibrate
after the scope has been on for 5 minutes. It takes about two minutes and needs both probes and the calibration
post."; and two buttons, "Calibrate now" and "Later". "Later" goes to the scope view, where an amber "Uncalibrated"
chip stays in the top bar until a calibration is bound. Its tap opens Diagnostics. Nothing else is asked at first run:
the defaults (CIC2 plus FIR where available, owner-verified 11:00) are the right ones for a new user.

## 4. Firmware readers the redesign needs that do not exist

Existing and sufficient: battery level and charging (GET_BATTERY), DS3231 temperature (`readtemperature`, module
only), uptime and pass time (GET_STATUS 16, 28), restart record and fault registers (GET_LINK_STATS 49-59), USB event
counters and reconnects (35-40, 60, 61), frame error counters (12-15), the event log (GET_EVENT_LOG), identity
(GET_INFO including the serial string), calibration data and flags (GET_CAL v3, row 51), card size and boot-sector
checks (SD_INFO, row 40), caps. Missing, verified against the rows and sources above:

1. **USB link state now**: bus speed (POWER bit 4), configured, suspended now. No GET_* field carries it (rows 28, 38,
   53); only event type 3 logs POWER bits.
2. **Card image identity**: commit hash and time of the image at sector 750, read from the image tail where
   `build_id.c` is linked last. SD_INFO returns only magic, checksum state, length and stored checksum (row 40).
3. **Calibration provenance in the block**: firmware build, FPGA version word, uptime at calibration and the power-on
   count at calibration. The v3 block (row 51, 304 bytes) has none of them; this is a block v4 field set (fw-cal3).
4. **Power-on count and operating minutes**, persisted in spare settings words (108..119 or 211..255,
   `firmware/README.md` "Removed and hidden menu options").
5. **Persisted restart record**: copy counters 49-59 to the card at the next boot after a restart, so a crash survives
   the user's power cycle (counters reset on a cold boot, row 38).
6. **Settings save state**: changed since the last save, and the result of the last power-off save. Nothing records
   either today.
7. **Calibration check on the post**: a firmware step that repeats the 08:30 post-level check (gated and full class,
   both cores, at both generator levels) and reports the A-B residual and class agreement against the limits of
   finding 6. Without it no "verified" state can exist. AL3 bitstreams only (needs the generator).
8. **Previous calibration block** kept on the card for "Undo last calibration". Inference: sector 711 is unused by the
   Atlan boot chain (`firmware/README.md` section 3 lists 708-710 and 750); verify against the updater before use.
9. **Live interleave tracking values** (calibration v4, fw-cal3), only once they exist.
10. **CP15 control word** (MMU and caches) outside FFT_BENCH, for example in GET_INFO or a GET_LINK_STATS slot.
11. **FPGA build identity**: the FPGA reports only the version word from command 0x06; the configuration flash is not
    reachable from the MCU. A read-only build-hash register in `fpga/al3` would let two 0x1535 builds be told apart.
    Note that 0x2A and up are taken by segmented capture on 0x1536 (`fpga/al3/AGENTS.md`).

Not a reader but a prerequisite for findings 4 and 12: Diagnostics as a main-loop display mode instead of a modal loop.
