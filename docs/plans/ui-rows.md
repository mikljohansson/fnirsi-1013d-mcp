# The two UI rows: trigger Type and ACQ Sample (design note)

Status: design and mockups, 2026-10-20 (agent ui-rows); no firmware yet. The rows come with FPGA 0x153C
(`docs/plans/fpga-0x153c.md`, sections 3, 4, 5 and 11) and its firmware generation C (protocol 1.25). The owner's decision
of 2026-10-20 (22:07-22:12, `docs/plans/task-queue.md` "UI selection for the epics", `fpga-0x153c.md` section 11): the
device UI grows by exactly two rows, a **Type** row in the trigger menu (Edge, Pulse width, Nth edge with their
parameters) and a **Sample** row in the ACQ menu (Normal, Peak); the hardware counter feeds the measurement cards with no
new control; runt, window, timeout, hold-off and everything else of 0x153C-0x153E stay MCP-only and never appear on the
device.

Mockups: `docs/mockups/ui-rows/` (artboards and PNGs), drawn by `docs/mockups/gen_ux/rows.py`, rendered by
`gen_ux/render.py` (`docs/mockups/README.md`). As with the makeover they are the design language and the acceptance
reference, not a pixel specification (`docs/plans/ux-makeover.md` section 6c). The firmware draws them with its own fonts and
shapes; the text widths below were measured on the bitmap fonts (`font_2.c` 13 px, `font_3.c` 16 px), not on the artboards'
Arial.

| Board | Shows |
|---|---|
| `TrigMenuEdge` | the trigger menu with the Type row, Edge chosen (today's behaviour, no parameter rows) |
| `TrigMenuPulse` | Type Pulse: the Width row (2us) and the Pulse row (shorter); mode Normal; the chip's pulse glyph |
| `TrigMenuNth` | Type Nth: the N row (5) and the Idle row (50us); the chip's Nth glyph |
| `TrigMenuNthArm` | Type Nth with Idle "from arm" (H = 0) |
| `AcqMenuSample` | the ACQ menu with the Sample box, Normal |
| `AcqMenuPeak` | Sample Peak: the Filter and Average boxes greyed, the note that says why |
| `ScopePeak` | the Scope view with a peak-detect record: one bar per column from min to max, spikes the 500 kSa/s sampling would miss |
| `ScopePulse` | a Pulse width trigger in use: Normal, CH1, pulse glyph; a 40 us pulse's trailing edge on the trigger point |

## 1. The rows at a glance

| Row | Where | States | Parameters | Drives (per arm) |
|---|---|---|---|---|
| Trigger type | trigger menu, sixth row | Edge, Pulse, Nth | Pulse: Width (time), Pulse shorter / longer; Nth: N (count), Idle (time or "from arm") | 0x5E type and longer bit, 0x5F N, 0x60 H; the polarity / counted edge is the edge row's 0x16 (`fpga-0x153c.md` 3.5) |
| Sample | ACQ menu, the fourth box of the Roll / Average / Filter row | Normal, Peak | none | 0x56 bit 1 through SET_DECIM mode 4 (`fpga-0x153c.md` section 4) |

Nothing else changes on the device for 0x153C: the counter's cards are the existing Freq, Period, +Width, +Duty, Vmin,
Vmax and Vpp cards reading from the counter when its source is the card's channel, which makes the frequency, period,
width and duty cards read steadier (no three-crossing minimum, 5 ns resolution; `fpga-0x153c.md` section 5). No card is
added and no control appears for the counter.

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
the short names and the parameter rows under them say the rest: "Width" and "Pulse" for the pulse width, "N" and "Idle"
for the Nth edge. Edge is today's trigger, bit-identical in the FPGA (`fpga-0x153c.md` 3.2).

### 2.3 The parameter rows and their control pattern

Rows seven and eight are the fold-out's 44 px value rows (the Frequency response Sweep fold-out's Start and Stop,
`bode_view.c` ROW_VALUE2, `gen_ux/bode.py twoway`): the label on the left, the value in pale yellow on the right, a tap on
the left half steps the value down and on the right half up, both ends drawn as - and + keys; each half is about 100 px
wide and the row 44 px tall. This is the pattern the guidelines prescribe for a value in a menu or fold-out: "a CTRL button
that holds a value shows it on the button and opens a fold-out beside the bar with the choices or + / -" (section 2, CTRL
page) and "new controls are at least 44 px tall and 80 px wide, hit area equal to the drawn area" (section 3, Touch). The
slider is for brightness (continuous, no unit); the generator's keypad exists for one entry only (frequency, duty) and the
guidelines do not mention it; + / - on a ladder is what every other value on the device uses (Span, Start, Stop, the
Settings rows, Ref level).

- **Pulse** (type 1): row seven "Width", a time from a **1-2-5 ladder from 10ns to 10s** (28 steps: the ladder of
  Span, Start and Stop, guidelines section 2). Row eight "Pulse" with the values "shorter" / "longer" (a tap toggles, the
  fold-out's two-state row as CH2 range Auto / Constant): the trigger fires on the trailing edge of a pulse shorter than,
  or longer than, the width (`fpga-0x153c.md` 3.3). The pulse's polarity is the **edge row**: Rising = a positive pulse
  (high), Falling = a negative pulse, as bench scopes couple slope and polarity; the row keeps its label and symbols, and
  the user guide says what it means per type.
- **Nth** (type 2): row seven "N", an integer **1 .. 65535** (0x5F bits 15..0; 0 acts as 1, so 1 is the floor): a tap
  steps by 1, and a finger held on a half repeats the step after 0.5 s at 5 per second, 50 per second after 2 s (new
  touch behaviour, nothing on the device repeats today; see the questions). Row eight "Idle", the time H the level must
  hold without an edge before the count restarts (each burst counted from its first edge), on the **same 1-2-5 ladder
  with one more step below 10ns: "from arm"** (H = 0, the count runs from the arm, `fpga-0x153c.md` 3.3). The counted
  edge is the edge row's.
- Values are written as the ACQ menu and the cards write times (`ui_fmt_sig4`, no padded zeros): "10ns", "2us", "50us",
  "1ms", "10s".

### 2.4 Units, ranges and the time base

The FPGA counts **samples of the trigger stream** (`fpga-0x153c.md` 3.1): on CH1 / CH2 one per memory word, 10 (D + 1) ns
on the legacy 8-bit path and N x 5 ns with N = 2 (floor(D / 2) + 1) on the 16-bit path (Filter on, or Peak; `pc_decim.h`
`pc_decim_n`); on EXT one per 5 ns at every rate. The firmware converts the menu's time with the period it already knows
per rate (`pc_decim.c` geometry; `fpga-0x153c.md` 3.5): **words = round(t / Tw), clamped to 1 .. 2^24 - 1** for the Width
and the Idle time; N is written as shown. The setting itself is the time, so a time base change keeps it: the words are
recomputed at every arm from the rate in effect (as the trigger level's 0x55 is), and the clamp applies at the two ends:

| Rate | Tw (CH, legacy) | Tw (CH, 16-bit) | shortest width | longest width |
|---|---|---|---|---|
| 200 MSa/s (D 0) | 10 ns | 10 ns | 10ns | 167.8 ms (2^24 - 1 words) |
| 500 kSa/s (D 399) | 4.00 us | 2.00 us | 4us (legacy) / 2us | 67 s / 33 s (the ladder ends at 10s) |
| 500 Sa/s (D 399999) | 4.00 ms | 2.00 ms | 4ms / 2ms | beyond the ladder |
| EXT, any rate | 5 ns | 5 ns | 10ns (ladder floor) | 83.9 ms |

While a clamp is in effect the plot's notice line (the white snap notice of `acq_view.c`, "Time/div set to 1us for this
rate") says so for 4 s after the change, as the time/div snap does: **"Width 2us: 4ms is the shortest at this rate"**,
**"Width 10s: 168ms is the longest at this rate"**, the same for "Idle". The menu row keeps showing the set time (the
setting is not changed by the rate, so zooming out and back gives the width back), and the firmware arms with the clamped
words. In roll mode the trigger is not used (nothing to convert); the rows stay as they are.

### 2.5 The trigger chip in the top bar

The chip (`scope_trigger_settings`: 31 px green block "T", 80 px grey box; line one the mode in 13 px at x + 36, line two
the channel box 28 x 14 at x + 35 and the edge symbol 11 x 14 at x + 66) keeps its two lines. Measured in font_2 the mode
words take 27 (Auto), 34 (Single) and 40 (Normal) of the 44 px line, so a type word cannot join the mode there, and the
channel box leaves 14 px on line two: no word fits anywhere on the chip without dropping the mode or the channel, and both
are states the user must see (Normal holds the screen without a trigger; the channel is where the level pointer lives).
The type therefore takes the **edge symbol's slot** as a symbol of the same kind: Edge keeps today's rising / falling
symbol; **Pulse width** draws a pulse in its polarity (a positive pulse for Rising, a negative one for Falling); **Nth
edge** draws a burst of short edges with the last one tall. The symbol is the construction the chip already uses for the
trigger's condition (the edge), colour carries no state in it (guidelines section 3), and the menu one tap away holds the
words and the numbers. The chip adds no new string. (The alternative, the type word in place of the mode word, is listed in
section 8 for the owner.)

### 2.6 The Scope view's "No trigger" hint

`ui_scope_warning` (`ui_ctrl.c`) says "No trigger: check the trigger level" while a running acquisition ends on the auto
time-out. The hint names what the trigger waits for per type, consequence then action (guidelines section 2, Warnings):

| Type | Hint |
|---|---|
| Edge | "No trigger: check the trigger level" (unchanged) |
| Pulse, shorter | "No trigger: no pulse shorter than 2us: check Width" |
| Pulse, longer | "No trigger: no pulse longer than 2us: check Width" |
| Nth, Idle from arm | "No trigger: fewer than 5 edges: check N" |
| Nth, Idle set | "No trigger: no burst of 5 edges: check N and Idle" |

The value in the hint is the set one (the time as the row shows it, N as set); the hint is one line of 16 px and the longest
("No trigger: no pulse shorter than 2us: check Width", with a three-step value such as 500us) measures 316 px in font_3
against the 420 px warning box.

## 3. The ACQ menu's Sample row

### 3.1 Where it is and how it reads

The ACQ menu (`scope_open_acquisition_menu`, 380 x 386 at 250, 48) holds the Roll mode, Average mode and Filter boxes
(90 x 40 at y + 163) and the Memory column right of the line at x + 304. Sample is the **fourth box of that row**, in
the same construction (a label line and a value line, green with black text when on, grey with white text when off), standing
in the Memory column at x + 310 under the Memory depths; the menu widens by 30 px (380 to 410) so the box keeps the row's
size (stock 90 x 40 rather than a shrunken new size, guidelines section 3) without touching the three stock boxes or the
columns. It reads **"Sample" / "Normal"** (grey) or **"Sample" / "Peak"** (green); a tap toggles. "Normal" is the word
the Display variants use for the plain trace; "Peak" is the owner's word and the industry's (peak detect).

The pass-time line and the Memory note stay; the note becomes **"Filter or Peak:" / "up to 6000"** because Peak runs the
16-bit path and therefore the same depth limit as the Filter (`fpga-0x153c.md` section 4: "the memory geometry is the
16-bit one"; `pc_dec_fit_memory` today).

### 3.2 Peak and the Filter: greyed, not merged (open point 7 of `fpga-0x153c.md` section 12)

With Sample Peak the **Filter box and the Average box are greyed**: their value stays written (e.g. "Filter" /
"CIC2+FIR") in grey text, the box does not take taps, and a three-line note under the Sample box says why: **"Peak:
Filter" / "and Average" / "not applied"**. When Sample goes back to Normal both come back as they were.

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

Roll mode: the Average box greys in roll today (`scope_acquisition_Average_mode_select`) and the Filter is not applied in
roll (`pc_fw_auto_possible`); Sample follows the Average box: grey in roll with its value kept, the note under it reading
the existing **"Not in roll mode"** (`ui_phos_note_roll`). Spectrum analyser: a peak record is pairs of raw codes, not a
signal, so Peak is a Scope setting: in Spectrum the box is grey with the value **"Scope only"**, the Scope's choice is kept and
Spectrum always acquires Normal (the Filter is already per mode, `pc_hal_scope.c` words 207 / 212; Sample is not, it has
one home).

### 3.3 What Peak draws and measures

- The record holds { max, min } per sample per channel (section 4). The Scope view draws **one vertical bar per column
  from the column's lowest min to its highest max**, in the channel colour (`ScopePeak` board); the stock
  `scope_display_channel_peak_trace` already draws per-column min / max bars from samples, so the bar drawing is one
  source change (the column's min and max come from the two bytes instead of one). While Sample is Peak the **Trace style
  button is grey** with its value kept, as it is in X-Y ("Trace style does not apply there", `ctrl_page.c`; guidelines
  section 2, CTRL page): the record is bars whatever the style.
- Measurements: Vmax from the max bytes, Vmin from the min bytes, Vpp from both; the others (Vavg, Vrms, the timing
  cards without the counter) from the midpoint trace, which is what the host's `samples` carries for a peak capture
  (section 4). With the counter on the card's channel, Freq, Period, +Width and +Duty come from the counter as in Normal.
- The ACQ chip in the top bar does not change (it shows rate and time/div; Peak is in the menu one tap away, the same as
  the Filter today).

## 4. Settings: how the rows persist

The settings record (`scope_save_config_data`, `scope_functions.c`) keeps the FPGA block at words 192..207; words 202 and
203 are the retired 1.00o5 trigger hold-off slots, written 0 and never read. They become the **trigger type words**:

- word 202: marker in the top byte (so a sector written before this firmware, which holds 0, reads as Edge), bits 0-1 type
  (0 Edge, 1 Pulse, 2 Nth), bit 2 longer, bits 3-7 the Width ladder index (0 = 10ns .. 27 = 10s), bits 8-12 the Idle ladder
  index with 0 = "from arm" (1 = 10ns .. 28 = 10s);
- word 203: N (16 bits).

The ladder indexes are stored rather than nanoseconds (10 s does not fit 32 bits of ns, and the ladder is what the menu
steps through). **Sample** is one bit, in `PC_SETTING_WORD_UI` (word 211, marker 0xC700, bits 0-2 used by Grid and the
spectrum's Display variant): bit 3, with the marker check widened to 0xFFF0. Both go into `settings_seq` so the host's
GET_* reads see a change made on the device, and the Restore default settings action resets them (type Edge, Sample
Normal) with the rest.

## 5. Firmware checklist (generation C, after the FPGA contract lands)

1. `fpga_control.c` / `fpga_bus.c`: writers for 0x5E, 0x5F, 0x60 (and the reads 0x34 / 0x37 the host needs), latched like
   0x15-0x17 at any time before 0x01 = 0, no 0x01 window (`fpga-0x153c.md` 3.5); back to Edge is 0x5E = 0.
2. One SET_TRIGGER2 path (`pc_cmds.c`, 24-byte body: type, polarity, flags, source, N, N2_or_H, levels, hold-off) for all
   types; the menu drives its subset: type 1 with the longer flag and N from the Width, type 2 with N and H from the Idle
   time; polarity = the edge row (0x16), source = the channel row. GET_TRIGGER appends the same fields so the host sees
   the device's type and the menu shows a type the host set (shared settings, `settings_seq`).
3. `pc_decim.c`: the trigger-stream period per source and rate (legacy 10 (D + 1) ns, 16-bit N x 5 ns, EXT 5 ns) and the
   time-to-words conversion with the 1 .. 2^24 - 1 clamp; the notice strings of 2.4 through `acq_notice_set`.
4. `menu.c`: the Type row (sixth row, boxes as 2.2), the two parameter rows as `twoway` rows with the ladders of 2.3, the
   menu height per type, the touch map; the Idle row's "from arm" step; N's held-finger repeat (a new touch behaviour,
   contained in the two rows).
5. `scope_trigger_settings`: the type glyph in the edge symbol's slot (2.5), drawn like `scope_trigger_symbol`.
6. `ui_ctrl.c` `ui_scope_warning`: the hints of 2.6, with the set value formatted by `ui_fmt_sig4`.
7. Settings words 202 / 203 and the Sample bit (section 4); `scope_restore_config_data` with the marker checks; the
   defaults; `settings_seq`.
8. `pc_hal_scope.c` / `pc_decim.c`: Sample Peak = SET_DECIM mode 4 (0x53 K = 1, 0x56 bit 1, level << 8 at the arm, no
   shift; section 4), the Scope-only rule in Spectrum, not in roll, the memory fit (6000) as for the Filter.
9. `menu.c` ACQ menu: width 410, the Sample box, the greyed Filter and Average boxes with the three-line note, the Memory
   note "Filter or Peak:".
10. The peak record on screen: bars from the two bytes (`scope_display_channel_peak_trace` as the base), Trace style grey
    while Peak (as in X-Y), Vmax / Vmin / Vpp from the bytes, the midpoint trace for the rest; the 8-bit per-core
    correction's mean of both cores (section 4) in the conversion.
11. Host side (not this note's scope, listed so the firmware agent sees the pair): `scope_set_trigger(type=..., width=...,
    n=..., idle=...)`, `scope_set_decimation(mode="peak")`, `scope_capture(acquisition="peak")`, GET_CAPTURE_INFO byte 79
    bit 1, READ_RAW layout 3 (`fpga-0x153c.md` 9.3).
12. `docs/guide/screens.md` (the trigger chip and the ACQ menu paragraphs), `docs/protocol.md`, `CHANGELOG.md` when it
    lands; the mockups of this note are the acceptance reference for the bench walk-through.

## 6. Every new string

Trigger menu: "Trigger" / "type"; "Edge", "Pulse", "Nth"; "Width"; "Pulse" with "shorter" / "longer"; "N"; "Idle" with
"from arm" (the times and N are values). ACQ menu: "Sample" / "Normal", "Sample" / "Peak"; "Filter or Peak:" / "up to
6000"; "Peak: Filter" / "and Average" / "not applied"; "Scope only"; "Not in roll mode" (existing). Plot notices: "Width
2us: 4ms is the shortest at this rate", "Width 10s: 168ms is the longest at this rate" (and "Idle ..."). Warnings: "No
trigger: no pulse shorter than 2us: check Width", "No trigger: no pulse longer than 2us: check Width", "No trigger: fewer
than 5 edges: check N", "No trigger: no burst of 5 edges: check N and Idle". The chip adds glyphs, no words.

## 7. Self-review against `docs/ui-guidelines.md`

- **Section 1, Vocabulary.** Both rows are *parameters* of the Scope mode (values of the current mode), not modes,
  variants or settings: the Type row changes what the trigger fires on, the Sample row what each memory word holds. Neither
  is a Variant (how the same measurement is shown), so neither goes near the Display control; the bars of a peak record are
  the record, not a drawing style, which is why the Trace style greys rather than gaining a value. The words used are the
  guidelines' and the owner's: "Normal" (the Display variant's word for the plain trace), "Peak", "Edge", "Pulse",
  "Nth", "Width", "Idle", and the measurement names stay as the cards spell them.
- **Section 2, Where things live.** "ACQ = acquisition only" and "Trigger = mode, edge, channel, position, Always 50 %":
  the Sample row is acquisition (what the FPGA stores) and sits in ACQ; the trigger's type and its parameters sit in the
  trigger dropdown, one home each; "nothing about how the plot looks goes in a dropdown" holds (the bars follow the
  record). The top bar stays "identical in every mode": the chip keeps its two lines and gains a glyph in an existing
  slot; the ACQ chip is unchanged. The parameter rows use the fold-out's "+ / -" pattern and the 1-2-5 ladder that Span,
  Start and Stop use. "Time/div drives the sample rate" is respected: the width is kept in time and re-converted, the rate
  never follows the trigger. The Spectrum's "one setting meaning two things" reasoning is reused to keep Peak and the
  Filter as two settings. Warnings sit top-middle and state consequence then action ("No trigger: ...: check Width");
  the clamp notice uses the existing snap-notice line, not a warning.
- **Section 3, How things look.** "Colour carries state; words carry meaning": the boxes carry their words in every state;
  a greyed box keeps its word in grey with a note that says why; nothing reads "on" / "off" (Sample reads Normal / Peak).
  Chips keep their construction (block, grey box, no notch); the glyph slot is the existing edge-symbol slot, drawn in the
  text colour. Fonts: 16 px labels and 13 px box texts as the rows above; every string was measured in font_2 / font_3 to
  fit its box (the Type boxes, the chip, the 420 px warning box). Touch: the two parameter rows are 44 px tall with 100 px
  halves and hit areas equal to the drawn area; the Type boxes and the Sample box keep the stock sizes of the rows they
  join (22 px boxes, 90 x 40) as "stock controls keep their stock sizes" allows for a row in the stock construction. No
  swipe, no blinking, no restyle (stock palette and shapes).
- **Section 4, What is shown.** Only values the firmware has: the width, N and Idle are the FPGA's registers in the
  menu's units; the bars are the record's bytes; no card or readout is added for the counter. Every unavailable state has
  its text: the greyed Filter and Average ("Peak: Filter and Average not applied"), Sample in Spectrum ("Scope only") and in
  roll ("Not in roll mode"), the clamped width (the notice). No placeholder for things not built (runt, window, timeout,
  hold-off do not appear). Host commands keep leaving the modal menus as today; the new rows add nothing modal.
- **Section 5, Process.** Mockups first at 800 x 480 (this note and `docs/mockups/ui-rows/`), then the adversarial
  review, then the firmware batch. Nothing moves: the Type row is appended, its parameter rows hang under it and exist only
  when needed; the ACQ menu widens by 30 px without moving a box. No control is removed, so there is nothing to list under
  "every removed control".

## 8. Questions the rules do not settle (for the owner)

1. **Exact values.** The 1-2-5 ladder is the device's pattern, but a pulse-width trigger sometimes needs a value between two
   steps (normal pulses of 500 us, a runt of 450 us), and N above 10 by single taps is slow. Options: accept the ladder on
   the device (exact values from the MCP, which is the owner's split anyway); or a long press on a value row that opens the
   generator's keypad for that value (a second home that earns its place). This note assumes the ladder.
2. **The chip's symbol or a word.** Section 2.5 puts a glyph in the edge symbol's slot because no word fits beside the mode
   and the channel. The alternative is the type word ("Pulse", "Nth") in place of the mode word on line one, losing Auto /
   Normal from the chip while a non-edge type is set.
3. **N's held-finger repeat** is new touch behaviour (nothing on the device repeats today). Alternative: N stepping on a
   ladder (1 .. 10 by one, then 10 % steps), which cannot reach every N.
4. **Peak with Phosphor and X-Y.** Phosphor could accumulate the bars (each column's min .. max hit once per record) or
   use the midpoints; X-Y of a peak record has no obvious meaning. Proposed: Phosphor draws the bars; X-Y uses the
   midpoints and the guide says so. Not mockupped.
5. **Sample in Spectrum**: "Scope only" (this note) against a per-mode Sample like the Filter, where Spectrum would simply
   never offer Peak. Both keep one home; the first shows the box with a reason, the second hides a value that cannot apply.
6. **Width and Idle on EXT**: the stream is 5 ns at every rate, so the ladder's 10ns floor is two EXT samples and the top
   is 83.9 ms; whether the Idle row should allow 5ns on EXT (one sample) is a detail for the implementation.
