# The two UI rows: trigger Type and ACQ Sample (design note)

Status: design and mockups, 2026-10-20 (agent ui-rows, round 2 after the adversarial review); no firmware yet. The rows
come with FPGA 0x153C (`docs/plans/fpga-0x153c.md`, sections 3, 4, 5 and 11) and its firmware generation C (protocol
1.25). The owner's decision of 2026-10-20 (22:07-22:12, `docs/plans/task-queue.md` "UI selection for the epics",
`fpga-0x153c.md` section 11): the device UI grows by exactly two rows, a **Type** row in the trigger menu (Edge, Pulse width,
Nth edge with their parameters) and a **Sample** row in the ACQ menu (Normal, Peak); the hardware counter feeds the
measurement cards with no new control; runt, window, timeout, hold-off and everything else of 0x153C-0x153E stay MCP-only
and never appear on the device. The review's decisions (coordinator as the owner's proxy, 2026-10-20 23:5x, owner veto
pending) are folded in below and listed in section 8.

Mockups: `docs/mockups/ui-rows/` (artboards and PNGs), drawn by `docs/mockups/gen_ux/rows.py`, rendered by
`gen_ux/render.py` (`docs/mockups/README.md`). As with the makeover they are the design language and the acceptance
reference, not a pixel specification (`docs/plans/ux-makeover.md` section 6c), with one exception: the chip glyphs of 2.5
are drawn in the firmware's pixels and are the reference as drawn. The firmware draws the rest with its own fonts and
shapes; the text widths below were measured on the bitmap fonts (`font_2.c` 13 px, `font_3.c` 16 px), not on the
artboards' Arial.

| Board | Shows |
|---|---|
| `TrigMenuEdge` | the trigger menu with the Type row, Edge chosen (today's behaviour, no parameter rows) |
| `TrigMenuPulse` | Type Pulse: the polarity row, the Width row (2us) and the Pulse row (Shorter); mode Normal; the chip's pulse glyph |
| `TrigMenuNth` | Type Nth: the N row (5, in its value box) and the Idle row (50us); the chip's Nth glyph |
| `TrigMenuNthOff` | Type Nth with Idle Off (H = 0, the count runs from the arm) |
| `TrigMenuViewer` | the menu in the waveform viewer: every row inert with grey text, the chosen boxes keep their colour |
| `TrigMenuBode` | the menu in Frequency response: "Not used by the sweep" as the last row |
| `AcqMenuSample` | the ACQ menu with the Sample box (Normal), the Filter off, the five depth chips, the pass time |
| `AcqMenuPeak` | Sample Peak: the Filter and Average boxes greyed, depths above 6000 gone, the note that says why |
| `ScopePeak` | the Scope view with a peak-detect record: one bar per column from min to max, spikes the 500 kSa/s sampling would miss |
| `ScopePulse` | a Pulse width trigger in use: Normal, CH1, pulse glyph; a 40 us pulse's trailing edge on the trigger point |

## 1. The rows at a glance

| Row | Where | States | Parameters | Drives (per arm) |
|---|---|---|---|---|
| Trigger type | trigger menu, sixth row | Edge, Pulse, Nth | Pulse: Width (time), Pulse Shorter / Longer, polarity (the edge row); Nth: N (count), Idle (time or Off) | 0x5E type and longer bit, 0x5F N, 0x60 H; the polarity / counted edge is the edge row's 0x16 (`fpga-0x153c.md` 3.5) |
| Sample | ACQ menu, the fourth box of the Roll / Average / Filter row | Normal, Peak | none | 0x56 bit 1 through SET_DECIM mode 4 (`fpga-0x153c.md` section 4) |

Nothing else changes on the device for 0x153C: the counter's cards are the existing Freq, Period, +Width, +Duty, Vmin,
Vmax and Vpp cards reading from the counter when its source is the card's channel, which makes the frequency, period,
width and duty cards read steadier (no three-crossing minimum, 5 ns resolution; `fpga-0x153c.md` section 5). No card is
added and no control appears for the counter.

**Feature gating** (review S1): there is none. Under the one-bitstream rule (`task-queue.md`, owner 2026-10-20 21:52) the
firmware that has the Type row and the Sample box is built for the bitstream that has them (0x153C); a bitstream without
them gets the mismatch banner, not a greyed row, so neither row carries a "what if the bitstream lacks it" case.

## 2. The trigger menu's Type row

### 2.1 Where it is and why there

The trigger menu (`menu.c` `scope_open_trigger_menu`, 220 x 280 at 508, 48) has five rows at a 56 px pitch: Trigger mode,
Trigger edge, Trigger channel, Trigger position, Always 50 %. The Type row is the **sixth row**, below Always 50 %, in the
same construction (a two-line 16 px label at x + 10, three 22 px boxes from x + 65). It goes last, not next to the edge
row, so that no existing row moves (`docs/ui-guidelines.md` section 5: nothing moves without a reason) and so that the
parameter rows the type needs can hang under it: they are rows seven and eight, which exist only for Pulse and Nth, and
the menu grows downward for them (height 336 for Edge, 430 for the other two; the firmware's `TRIGGER_MENU_HEIGHT` is
already a function). A type change therefore never shifts a row the user already knows.

### 2.2 Wording and states

Label "Trigger" / "type". Boxes "Edge", "Pulse", "Nth" (x + 65 w 44, x + 113 w 50, x + 167 w 40, at y + 297); the chosen
one is filled in the trigger's green with black text, the others are the menu's dark boxes with white text (the row's
construction, one colour for the chosen box as the Type row has no stock per-choice colours to keep). The full names
"Pulse width" and "Nth edge" do not fit three boxes in the 145 px the rows give (measured in font_3: Edge 32, Pulse width
74, Nth edge 57 px, plus padding) and a cycling single box would hide the choices the other rows show, so the boxes carry
the short names and the rows around them say the rest: "Width" and "Pulse" for the pulse width, "N" and "Idle" for the
Nth edge. Edge is today's trigger, bit-identical in the FPGA (`fpga-0x153c.md` 3.2).

**The edge row under Type Pulse** (review S3) reads **"Pulse" / "polarity"** with the boxes **"Positive"** (x + 66, 62 px,
light red as Rising) and **"Negative"** (x + 147, 62 px, light blue as Falling); the symbol slot between them shows the
pulse glyph of the chosen polarity. It is the same setting (0x16), so Rising = Positive and Falling = Negative when the
type changes. Under Nth the row stays "Trigger edge": the counted edge.

### 2.3 The parameter rows and their control pattern

Rows seven and eight are the fold-out's 44 px value rows (the Frequency response Sweep fold-out's Start and Stop,
`bode_view.c` ROW_VALUE2, `gen_ux/bode.py twoway`): the label on the left, the value in pale yellow on the right, a tap on
the left half steps the value down and on the right half up, both ends drawn as - and + keys; each half is about 100 px
wide and the row 44 px tall. This is the pattern the guidelines prescribe for a value in a menu or fold-out: "a CTRL button
that holds a value shows it on the button and opens a fold-out beside the bar with the choices or + / -" (section 2, CTRL
page) and "new controls are at least 44 px tall and 80 px wide, hit area equal to the drawn area" (section 3, Touch). The
slider is for brightness (continuous, no unit); + / - on a ladder is what every other value on the device uses (Span,
Start, Stop, the Settings rows, Ref level). The keypad (`scope_open_keyboard_menu`, the generator's frequency and duty)
is the exception for N below.

- **Pulse** (type 1): row seven "Width", a time from a **1-2-5 ladder from 10ns to 10s** (28 steps: the ladder of
  Span, Start and Stop, guidelines section 2), limited to the steps the current rate can arm (2.4): the - / + stop at the
  rate's ends, steps beyond are not offered (as the grey time/div boxes). Row eight "Pulse" with the values
  **"Shorter" / "Longer"** (a tap toggles, the fold-out's two-state row as CH2 range Auto / Constant): the trigger fires on
  the trailing edge of a pulse shorter than, or longer than, the width (`fpga-0x153c.md` 3.3). The polarity is the row
  of 2.2.
- **Nth** (type 2): row seven "N", an integer **1 .. 65535** (0x5F bits 15..0; 0 acts as 1, so 1 is the floor): the - / +
  step by 1, and the value stands in an outlined box of its own (the generator menu's value box); **a tap on the value
  opens the existing keypad** (`scope_open_keyboard_menu`, 1 .. 65535, nested under the trigger menu as it nests under the
  generator menu; review S8). No held-finger repeat. Row eight "Idle", the time H the level must hold without an edge
  before the count restarts (each burst counted from its first edge), on the **same 1-2-5 ladder with one more step
  below 10ns: "Off"** (H = 0, the count runs from the arm, `fpga-0x153c.md` 3.3; the guide says Off counts from the arm).
  The counted edge is the edge row's.
- Values are written as the ACQ menu and the cards write times (`ui_fmt_sig4`, no padded zeros): "10ns", "2us", "50us",
  "1ms", "10s".

### 2.4 Units, ranges and the time base: nothing is shown that is not armed

The FPGA counts **samples of the trigger stream** (`fpga-0x153c.md` 3.1): on CH1 / CH2 one per memory word, 10 (D + 1) ns
on the legacy 8-bit path and N x 5 ns with N = 2 (floor(D / 2) + 1) on the 16-bit path (Filter on, or Peak; `pc_decim.h`
`pc_decim_n`); on EXT one per 5 ns at every rate. The firmware converts the menu's time with the period it already knows
per rate (`pc_decim.c` geometry; `fpga-0x153c.md` 3.5): **words = round(t / Tw)**; N is written as shown. The rate's
range for the Width is **2 words .. 2^24 - 1 words** (floor 2 words for Shorter and Longer alike: N = 1 degenerates to an
edge trigger, `fpga-0x153c.md` 3.3; on EXT 2 words is the 10ns ladder floor), for the Idle time 1 .. 2^24 - 1 words:

| Rate | Tw (CH, legacy) | Tw (CH, 16-bit) | shortest Width step | longest Width step |
|---|---|---|---|---|
| 200 MSa/s (D 0) | 10 ns | 10 ns | 20ns | 100ms (2^24 - 1 words = 167.8 ms) |
| 500 kSa/s (D 399) | 4.00 us | 2.00 us | 10us (legacy) / 5us | 10s (the ladder's end) |
| 500 Sa/s (D 399999) | 4.00 ms | 2.00 ms | 10ms / 5ms | 10s |
| EXT, any rate | 5 ns | 5 ns | 10ns | 50ms (83.9 ms) |

**No silent clamp** (review B2). The menu offers only the ladder steps inside the rate's range (2.3). When a rate change
(a zoom step, a time/div box, a rate box, the host's SET_TIMEBASE, the Filter or Sample changing the path) puts the Width or
the Idle time outside the new range, **the setting snaps to the nearest ladder step inside it** and the existing snap notice
(`acq_view.c` `acq_notice_set`, the white line "Time/div set to 1us for this rate", 4 s) says **"Width set to 5us for this
rate"** or **"Idle set to 5us for this rate"**. The menu then shows the snapped value, which is what is armed. When one
change fires two notices (a rate box that snaps the time/div and the Width) they are **queued**: the time/div notice first,
the Width's (then the Idle's) after it, each for its 4 s, through the one notice line; a new change replaces the queue.
Nothing is lost and the line keeps its one-sentence construction (joining them would exceed the 420 px box: "Time/div set
to 1us for this rate" is 176 px in font_3 and a joined line with a Width clause runs past it). In roll mode the trigger
is not used; the rows keep their values and nothing snaps until the next armed rate.

### 2.5 The trigger chip in the top bar

The chip (`scope_trigger_settings`: 31 px green block "T", 80 px grey box; line one the mode in 13 px at x + 36, line two
the channel box 28 x 14 at x + 35 and the edge symbol at x + 66) keeps its two lines. Measured in font_2 the mode
words take 27 (Auto), 34 (Single) and 40 (Normal) of the 44 px line, so a type word cannot join the mode there, and the
channel box leaves 14 px on line two: no word fits anywhere on the chip without dropping the mode or the channel, and both
are states the user must see (Normal holds the screen without a trigger; the channel is where the level pointer lives).
The type therefore takes the **edge symbol's slot** as a symbol of the same kind (review decision 2: glyph). **Drawn in the
firmware's pixels** (review S9), 2 px lines in a 14 x 14 field at x + 65, y + 19, the mockups being the reference: Edge keeps
the rising / falling edge; **Pulse width** is one full pulse in its polarity (a positive pulse for Positive, a negative one
for Negative); **Nth edge** is two short edges and one tall. The symbol is the construction the chip already uses for the
trigger's condition (the edge), colour carries no state in it (guidelines section 3), and the menu one tap away holds the
words and the numbers. The chip adds no new string.

### 2.6 The Scope view's "No trigger" hint

`ui_scope_warning` (`ui_ctrl.c`) says "No trigger: check the trigger level" while a running acquisition ends on the auto
time-out. The firmware knows only that no trigger came, not why, so the hint names the settings to check (review S2),
consequence then action (guidelines section 2, Warnings):

| Type | Hint |
|---|---|
| Edge | "No trigger: check the trigger level" (unchanged) |
| Pulse | "No trigger: check the level and Width" |
| Nth, Idle Off | "No trigger: check the level and N" |
| Nth, Idle set | "No trigger: check the level, N and Idle" |

The longest is 240 px in font_3 against the 420 px warning box. `spectrum.c` draws its own copy of the hint
(`spectrum.c` 958); it takes the same function (checklist item 6).

### 2.7 The waveform viewer and Frequency response (review S7)

In the **waveform viewer** the trigger rows show the stored record's trigger and are inert, their texts in LIGHTGREY1 as
today (`menu.c` `waveviewmode`); the Type row and the parameter rows follow: grey text, the chosen boxes keep their colour,
no tap does anything (`TrigMenuViewer`). In **Frequency response** the note "Not used by the sweep" (`bu_trig_note`) is
always the last row, the menu 24 px taller than whatever rows show (`TrigMenuBode`). **One thing did not fit**: with Type
Pulse or Nth the menu is 430 px and the note would make it 454 px from y 48, past the 480 px screen. The board therefore
draws, in Frequency response only, the six rows and the note (360 px) and **not the type's parameter rows**: the sweep
does not use them, the Type row still shows the type, and the parameters are a tap away once the mode is left. If the
coordinator prefers the rows, the alternatives are a 40 px pitch for the two parameter rows in that mode (446 px, still
past the screen) or the note above the Type row; neither was taken.

## 3. The ACQ menu's Sample row

### 3.1 Where it is and how it reads (review B1)

The ACQ menu (`scope_open_acquisition_menu`, 380 x 386 at 250, 48) holds the Roll mode, Average mode and Filter boxes
(90 x 40 at y + 163, x + 8 / 106 / 204: a 98 px pitch) and the Memory column right of the vertical line at x + 304: five
depth chips 3000 .. 15000 at y + 33 + 36 i (`scope_acquisition_memory_deep_select`; depths above 6000 are not drawn while
the record is 16-bit, `pc_mask_rect` at y + 31, 58 x 29) and the pass time at `UI_ACQ_PASS_Y` 218 in two lines ("Pass
time" in grey, "15 ms" in white). The menu becomes **478 px wide (x 250 .. 728, clear of the right bar at 740)**: the
sample-rate and time/div grids and their titles stay where they are, the vertical line and the whole Memory column move
right by 98 px (the line at x + 402, the chips at x + 413, the pass time at x + 410), and **Sample is the fourth box of the
row at x + 302, y + 163, 90 x 40**, in the row's construction (a label line and a value line, green with black text when
on, grey with white text when off). It reads **"Sample" / "Normal"** (grey) or **"Sample" / "Peak"** (green); a tap
toggles. "Normal" is the word the Display variants use for the plain trace; "Peak" is the owner's word and the industry's
(peak detect). Depths above 6000 are not drawn while Sample is Peak, exactly as with the Filter on: Peak runs the 16-bit
path and therefore the same depth limit (`fpga-0x153c.md` section 4: "the memory geometry is the 16-bit one";
`pc_dec_fit_memory` today). No gating (section 1).

### 3.2 Peak and the Filter: greyed, not merged (open point 7 of `fpga-0x153c.md` section 12)

With Sample Peak the **Filter box and the Average box are greyed**: their value stays written (e.g. "Filter" /
"CIC2+FIR") in grey text, the box does not take taps, and a note in the Memory column under the pass time, at y + 254,
says why: **"Peak:" / "Filter and" / "Average" / "not applied"**. (The review asked for two lines, "Peak: Filter and" /
"Average not applied"; they measure 87 and 114 px in font_2 and the column is 76 px wide, 66 px usable, so the same words
stand on four lines of 18 px, y + 254 .. 326, where the column is empty.) When Sample goes back to Normal both boxes come
back as they were.

Why greying and not one state:

- The FPGA keeps them apart: peak is a one-register switch (0x56 bit 1) that forces the 16-bit path by itself and leaves
  0x53 / 0x54 untouched "for the way back" (`fpga-0x153c.md` section 4). The Filter is a kept setting that is temporarily not
  applied; that is exactly the vocabulary of guidelines section 3 ("grey = not available; the word is always next to the
  colour") and section 4 ("every unavailable state has defined text that says why").
- One state (a Filter value "Peak") would make the Filter box mean two things and lose the user's filter choice on the way
  back; the guidelines rejected a setting meaning two things when the Spectrum's memory cap was weighed (section 2, Span
  drives the sample rate), and the owner put Peak in its own row for the same reason (`fpga-0x153c.md` 11.1).
- Average is greyed for a hard reason, not for symmetry: averaging peak words would mix max and min bytes, and the contract
  refuses it (SET_SEQ mode 2 with peak on is E_STATE 7; a UI averaging run is not armed while peak is on, section 4).

**Roll mode**: the Average box greys in roll today (`scope_acquisition_Average_mode_select`) and the Filter is not applied
in roll (`pc_fw_auto_possible`); Sample follows: grey with its kept value, and the existing **"Not in roll mode"**
(`ui_phos_note_roll`) under it in the column. **Spectrum analyser** (review S6): a peak record is pairs of raw codes, not a
signal, so Peak is a Scope setting and **the Spectrum's ACQ menu has no Sample box**: the slot stays empty, as the Filter
slot does on a bitstream without it; the Scope's choice is kept and Spectrum always acquires Normal (the Filter is per
mode, `pc_hal_scope.c` words 207 / 212; Sample is not, it has one home).

### 3.3 What Peak draws and measures (review S10, S4)

- The record holds { max, min } per sample per channel (section 4). The Scope view draws **one vertical bar per column
  from the column's lowest min to its highest max**, in the channel colour (`ScopePeak` board). The drawing loop is the
  linear trace's: the integer sample step per column (no soft-float in the loop; the stock
  `scope_display_channel_peak_trace` walks the samples with a `double` step and is not the base), the column's min and
  max taken over the two bytes of every word the column covers, the colour and the overload red from the caller as today,
  **overload when either byte of a peak word is 0 or 255**.
- **Trace style**: the existing value "Peak" (per-column min / max of the samples) is renamed **"Bars"** (consistency over
  muscle memory, guidelines section 5: Linear, Dots, Bars), since "Peak" now names the Sample state. While Sample is Peak the
  **Trace style button is grey** with its value kept, as it is in X-Y ("Trace style does not apply there", `ctrl_page.c`;
  guidelines section 2, CTRL page): the record is bars whatever the style.
- **Phosphor** accumulates the bars (each column's min .. max hit once per record); **X-Y** uses the midpoints.
- Measurements: Vmax from the max bytes, Vmin from the min bytes, Vpp from both; the others (Vavg, Vrms, the timing
  cards without the counter) from the midpoint trace, which is what the host's `samples` carries for a peak capture
  (section 4). With the counter on the card's channel, Freq, Period, +Width and +Duty come from the counter as in Normal.
- The ACQ chip in the top bar does not change (it shows rate and time/div; Peak is in the menu one tap away, the same as
  the Filter today).

## 4. Settings: how the rows persist

The settings record (`scope_save_config_data`, `scope_functions.c`) keeps the FPGA block at words 192..207; words 202 and
203 are the retired 1.00o5 trigger hold-off slots, written 0 and never read. They become the **trigger type words**:

- word 202: **marker 0xA000 in bits 15..13 (binary 101, checked with mask 0xE000; a sector written before this firmware
  holds 0 and reads as Edge)**, bits 0-1 type (0 Edge, 1 Pulse, 2 Nth), bit 2 Longer, bits 3-7 the Width ladder index (0 =
  10ns .. 27 = 10s), bits 8-12 the Idle ladder index (0 = Off, 1 = 10ns .. 28 = 10s);
- word 203: N (16 bits).

The ladder indexes are stored rather than nanoseconds (10 s does not fit 32 bits of ns, and the ladder is what the menu
steps through); at power-on the index is snapped into the rate's range as in 2.4 (silently: nothing was armed yet).
**Sample** is one bit, in `PC_SETTING_WORD_UI` (word 211, marker 0xC700, bits 0-2 used by Grid and the spectrum's Display
variant): bit 3, with the marker check widened to 0xFFF0. Both go into `settings_seq` so the host's GET_* reads see a
change made on the device, and the Restore default settings action resets them (type Edge, Sample Normal) with the rest.

## 5. Firmware checklist (generation C, after the FPGA contract lands)

1. `fpga_control.c` / `fpga_bus.c`: writers for 0x5E, 0x5F, 0x60 (and the reads 0x34 / 0x37 the host needs), latched like
   0x15-0x17 at any time before 0x01 = 0, no 0x01 window (`fpga-0x153c.md` 3.5); back to Edge is 0x5E = 0. No capability
   check on the device side (section 1).
2. One SET_TRIGGER2 path (`pc_cmds.c`, 24-byte body: type, polarity, flags, source, N, N2_or_H, levels, hold-off) for all
   types; the menu drives its subset: type 1 with the longer flag and N from the Width, type 2 with N and H from the Idle
   time; polarity = the edge row (0x16), source = the channel row. GET_TRIGGER appends the same fields so the host sees
   the device's type and the menu shows a type the host set (shared settings, `settings_seq`); a host width that is not a
   ladder step shows as the nearest step's words (the host's exact value is armed as sent).
3. `pc_decim.c`: the trigger-stream period per source and rate (legacy 10 (D + 1) ns, 16-bit N x 5 ns, EXT 5 ns), the
   time-to-words conversion, the rate's range (2 .. 2^24 - 1 words for the Width, 1 .. for Idle) and the ladder clip; the
   snap on every rate change with the notices of 2.4 through `acq_notice_set`, and the notice queue (time/div first).
4. `menu.c`: the Type row (sixth row, boxes as 2.2), the polarity row under Pulse (2.2), the two parameter rows as
   `twoway` rows with the ladders of 2.3 clipped to the rate's range, the N value box opening `scope_open_keyboard_menu`
   nested under the trigger menu (1 .. 65535), the menu height per type and mode (2.7), the touch map; the viewer's grey
   inert rows; the Frequency response note as the last row.
5. `scope_trigger_settings`: the type glyph in the edge symbol's slot, the pixels of the mockups (2.5), drawn like
   `scope_trigger_symbol`.
6. `ui_ctrl.c` `ui_scope_warning`: the hints of 2.6; `spectrum.c`'s own copy of the hint (line 958) takes the same
   function.
7. Settings words 202 / 203 (marker 0xA000) and the Sample bit (section 4); `scope_restore_config_data` with the marker
   checks and the power-on snap; the defaults; `settings_seq`.
8. `pc_hal_scope.c` / `pc_decim.c`: Sample Peak = SET_DECIM mode 4 (0x53 K = 1, 0x56 bit 1, level << 8 at the arm, no
   shift; section 4), Scope only (no box in Spectrum, Spectrum acquires Normal), not in roll, the memory fit (6000) as
   for the Filter.
9. `menu.c` ACQ menu: width 478, the Memory column moved by 98 px (line, chips, masks, pass time, `UI_ACQ_PASS_X`), the
   Sample box at x + 302, the greyed Filter and Average boxes with the four-line note at y + 254, "Not in roll mode" in roll.
10. The peak record on screen: bars with the linear trace's integer step over the two bytes, overload on either byte at 0
    or 255 (3.3); Trace style "Peak" renamed "Bars" (`ui_trace_names`) and grey while Peak (as in X-Y); Phosphor
    accumulates the bars, X-Y the midpoints; Vmax / Vmin / Vpp from the bytes, the midpoint trace for the rest; the 8-bit
    per-core correction's mean of both cores (section 4) in the conversion.
11. Host side (not this note's scope, listed so the firmware agent sees the pair): `scope_set_trigger(type=..., width=...,
    n=..., idle=...)`, `scope_set_decimation(mode="peak")`, `scope_capture(acquisition="peak")`, GET_CAPTURE_INFO byte 79
    bit 1, READ_RAW layout 3 (`fpga-0x153c.md` 9.3).
12. `docs/guide/screens.md` (the trigger chip, the polarity row, the ACQ menu, Trace style Bars), `docs/protocol.md`,
    `CHANGELOG.md` when it lands; the mockups of this note are the acceptance reference for the bench walk-through.

## 6. Every new string

Trigger menu: "Trigger" / "type"; "Edge", "Pulse", "Nth"; "Pulse" / "polarity" with "Positive" / "Negative"; "Width";
"Pulse" with "Shorter" / "Longer"; "N"; "Idle" with "Off"; "Not used by the sweep" (existing). ACQ menu: "Sample" /
"Normal", "Sample" / "Peak"; "Peak:" / "Filter and" / "Average" / "not applied"; "Not in roll mode" (existing). CTRL page:
Trace style "Bars" (was "Peak"). Plot notices: "Width set to 5us for this rate", "Idle set to 5us for this rate" (the
value is the snapped step). Warnings: "No trigger: check the level and Width", "No trigger: check the level and N", "No
trigger: check the level, N and Idle". The chip adds glyphs, no words.

## 7. Self-review against `docs/ui-guidelines.md`

- **Section 1, Vocabulary.** Both rows are *parameters* of the Scope mode (values of the current mode), not modes,
  variants or settings: the Type row changes what the trigger fires on, the Sample row what each memory word holds. Neither
  is a Variant (how the same measurement is shown), so neither goes near the Display control; the bars of a peak record are
  the record, not a drawing style, which is why the Trace style greys rather than gaining a value, and why its "Peak"
  style is renamed "Bars" so that one word has one meaning. The words used are the guidelines' and the owner's: "Normal"
  (the Display variant's word for the plain trace), "Peak", "Edge", "Pulse", "Nth", "Width", "Idle", "Off"; the
  measurement names stay as the cards spell them.
- **Section 2, Where things live.** "ACQ = acquisition only" and "Trigger = mode, edge, channel, position, Always 50 %":
  the Sample row is acquisition (what the FPGA stores) and sits in ACQ; the trigger's type and its parameters sit in the
  trigger dropdown, one home each (the N keypad is a way of entering the one setting, not a second home); "nothing about
  how the plot looks goes in a dropdown" holds (the bars follow the record). The top bar stays "identical in every mode":
  the chip keeps its two lines and gains a glyph in an existing slot; the ACQ chip is unchanged. The parameter rows use the
  fold-out's "+ / -" pattern and the 1-2-5 ladder that Span, Start and Stop use, and like the time/div boxes they offer only
  what the rate can arm. "Time/div drives the sample rate" is respected: the rate never follows the trigger; a width the
  new rate cannot arm snaps with the same notice construction as the time/div snap, so what is shown is what is armed.
  The Spectrum's "one setting meaning two things" reasoning is reused to keep Peak and the Filter as two settings. Warnings
  sit top-middle and state consequence then action ("No trigger: check the level and Width"); the snap uses the existing
  notice line, not a warning.
- **Section 3, How things look.** "Colour carries state; words carry meaning": the boxes carry their words in every state;
  a greyed box keeps its word in grey with a note that says why; nothing reads "on" / "off" as a toggle (Sample reads
  Normal / Peak; Idle's "Off" is a value, the ladder's zero step). Chips keep their construction (block, grey box, no
  notch); the glyph slot is the existing edge-symbol slot, drawn in 2 px lines in the text colour. Fonts: 16 px labels and
  box texts as the rows above, 13 px notes; every string was measured in font_2 / font_3 to fit its box (the Type and
  polarity boxes, the chip, the Memory column, the 420 px warning box). Touch: the two parameter rows are 44 px tall with
  100 px halves and hit areas equal to the drawn area (the N value box takes its own taps); the Type boxes and the Sample
  box keep the stock sizes of the rows they join (22 px boxes, 90 x 40) as "stock controls keep their stock sizes" allows for
  a row in the stock construction. No swipe, no blinking, no restyle (stock palette and shapes).
- **Section 4, What is shown.** Only values the firmware has: the width, N and Idle are the FPGA's registers in the
  menu's units and only steps the rate can arm are offered or kept; the bars are the record's bytes; no card or readout is
  added for the counter; the hints name the settings to check, not a cause the firmware cannot know. Every unavailable
  state has its text: the greyed Filter and Average ("Peak: Filter and Average not applied"), Sample in roll ("Not in roll
  mode"), the inert rows of the viewer (grey, as the other trigger rows), the Frequency response note. No placeholder for
  things not built (runt, window, timeout, hold-off do not appear; no Sample box where Peak cannot apply). Host commands
  keep leaving the modal menus as today; the keypad nests like the generator's and yields the same way.
- **Section 5, Process.** Mockups first at 800 x 480 (this note and `docs/mockups/ui-rows/`), the adversarial review
  (round 2 folded in), then the firmware batch. Nothing moves without a stated reason: the Type row is appended, its
  parameter rows hang under it and exist only when needed; the ACQ menu widens and its Memory column moves by one box
  pitch so the Sample box can be a stock-size box of the row it joins (3.1). Removed or renamed: the Trace style value
  "Peak" becomes "Bars" (same function, new name, 3.3); nothing else goes.

## 8. Decided (coordinator as the owner's proxy, 2026-10-20 23:5x; owner veto pending)

1. **Exact values**: the 1-2-5 ladder on the device for Width and Idle; exact values from the MCP (`scope_set_trigger`).
2. **The chip**: a glyph in the edge symbol's slot, drawn in the firmware's pixels as the mockups show (2.5).
3. **N**: - / + by 1 and the existing keypad from a tap on the value; no held-finger repeat (2.3).
4. **Peak with Phosphor and X-Y**: Phosphor accumulates the bars, X-Y uses the midpoints (3.3).
5. **Sample in Spectrum**: absent (no box in the Spectrum's ACQ menu; Spectrum acquires Normal) (3.2).
6. **EXT Idle**: Off, then the 10ns ladder floor; no 5ns step (2.4).

Also decided in the same review: no feature gating (section 1), no silent clamp with the snap notice and the queued
notices (2.4), the polarity row under Pulse (2.2), the hints naming the settings (2.6), Trace style "Peak" renamed "Bars"
(3.3), "Off" and the capitalised "Shorter" / "Longer" (2.3), the viewer and Frequency response boards (2.7), the 478 px ACQ
menu with the Sample box on the 98 px pitch (3.1), the peak drawing rules (3.3), the marker value 0xA000 (section 4), the
ScopePeak cards from the drawn spikes.
