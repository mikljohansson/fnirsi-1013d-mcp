"""bode-ui.md: the owner's final decisions (2026-10-19 evening): variant 2's chart without legend, cursors C1 / C2 with
the readout in the top division, Frequency axis Log / Linear, "calibration post" in the instruction."""
import sys

P = sys.argv[1]
s = open(P, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


def between(start, end, new):
    """replace from the line starting with start up to (not including) end"""
    global s
    a = s.index(start)
    b = s.index(end, a)
    s = s[:a] + new + s[b:]


rep('''- The RUN/STOP button shows the action a tap does (fw 0.2.68, guidelines section 3). It reads green "RUN" while
  stopped and red "STOP" while running. The top bar chip shows the state.
''', '''- The RUN/STOP button shows the action a tap does (fw 0.2.68, guidelines section 3). It reads green "RUN" while
  stopped and red "STOP" while running. The top bar chip shows the state.

Final decisions on this design **(owner, 2026-10-19 evening)**. Where an earlier draft said otherwise, these win:

- **The chart is variant 2's**: one pane with two y axes, magnitude on the left, phase on the right (section 9 E). It
  has **no legend**. The axes carry their units ("dB" on the left, "deg" on the right), and each curve has its axis
  label's colour (magnitude CH2 cyan, phase pink). Variant 1's two panes are superseded.
- **Two frequency cursors, C1 and C2**, under T CURSOR. The readout sits in the data-free top division, variant 1's
  placement, not in a box over the plot. It has a C2-C1 row (section 2).
- **Frequency axis Log / Linear**, default Log, as a row in the Display fold-out (section 4.1). Magnitude is always
  dB and phase always degrees; neither is configurable.
- **The instruction before the first sweep** says "calibration post" (section 6).
''')

between("| Main menu: the Frequency response row |", "## 2. The screen", '''| Main menu: the Frequency response row | fw 0.2.68's eight rows with the row renamed to two lines, "Frequency" over "response", and fw 0.2.68's icon |
| Before the first sweep: one instruction | the empty pane on the default 100 Hz .. 1 MHz axis, both axes labelled, the two-line instruction top-middle, the cards' "no sweep" texts |
| Action page (final) | a 50 kHz tank, Q 8.1, swept by hand over 5 kHz .. 500 kHz with 100 points and Settle Normal; one pane, dB left, deg right; markers; two unsettled points in grey with their warning (Normal settles Q <= 7, so the warning asks for a longer Settle, which AUTO SET would have chosen); the default cards; the right bar's first page |
| Cursors C1 and C2 | the two flagged cursors, the readout in the top division with the C2-C1 row, and the warning centred right of it |
| Single sweep in progress | point 37 of 100, the dotted line at the point being measured, the cards reading "sweeping", the generator chip showing the tone, chip RUN, button STOP |
| AUTO SET: overview sweep running | the overview on 100 Hz .. 10 MHz (20 points) at 20 dB per division, AUTO SET reading "Auto...", the Sweep card "auto 12/20" |
| CTRL page | CTRL, Display, Sweep, Thru, Grid; the stepper and 50 % TRIG slots empty |
| CTRL: Display fold-out with Frequency axis | Mag + phase, Magnitude, Phase, each with an icon; a separator; Frequency axis Log |
| CTRL: Sweep fold-out | Start and Stop as - / + rows, Points, Settle, CH2 range, then Single sweep with the estimated duration |
| CTRL: Thru fold-out | Measure thru, Apply (switch, with what the thru matches), Clear |
| MEASURES: the mode's list | the stock MEASURES menu with the mode's ten items, six chosen, ranks shown |
| Variant 1 (superseded): two panes | the first proposal's action page, kept on the canvas for reference |

''')

between("## 2. The screen", "## 3. Right bar, first page", '''## 2. The screen

The chart **(owner)**: one pane over the trace area, two y axes, no legend.

- **Pane**: y 60..408, six divisions of 58 px, x 6..726. There are no separate magnitude and phase panes any more;
  fw 0.2.66's `MAG_*` / `PH_*` geometry goes.
- **Left axis, magnitude**: labels at x 9 in the magnitude colour (CH2 cyan), the top one with its unit ("10 dB").
- **Right axis, phase**: labels right-aligned at x 723 in the phase colour (`PHASE_COLOR`), the top one with its unit
  ("+135 deg"). Each curve is drawn in its axis label's colour, which is how a reader tells them apart.
- **Frequency axis**: Log (default) with decade lines and 2 and 5 sub-lines, or Linear (section 4.1). Labels sit at
  y 413 in grey.
- **Cards**: at y 446 (`UI_MS_SPEC_Y`), as in Spectrum. There is no record-position slider in this mode.

**Scales and the top division.** Both axes are chosen from the data, and both keep the top division (y 60..118) free
of data when they can. That division holds the warning line and the cursor readout without covering the curves.

- **Magnitude**: 5, 10 or 20 dB per division, the first that holds the data in six divisions. Then the top line rises
  by one division whenever the maximum is less than a division below it and the data still fits. This is a change to
  `axes()`, which today has no headroom. A resonance peak then lands in the second division, not under the warning.
- **Phase**: 45 or 90 degrees per division, the first that holds the data, with the same headroom rule. Wrapped phase
  spans at most 360 degrees. At 90 degrees per division that is four divisions plus one of headroom, so the lane always
  fits. On the tank it reads +135 .. -135 deg.
- **The gridlines are shared.** Both axes divide the same six divisions, so a gridline carries one label on each side.

**Display variants.** Mag + phase is the overlaid chart. Magnitude and Phase each draw one curve and only its axis,
with the same scale rules.

**Markers** are unchanged. White triangles mark the -3 dB crossings and the resonance peak on the magnitude curve, and
the phase crossings on the phase curve. The point being measured shows as a dotted grey line. Flagged points are grey,
with no joining line.

**Frequency cursors C1 and C2 (T CURSOR, owner).**

- **Drawing**: two dotted vertical lines in the stock cursor colour (`CURSORS_COLOR`). Each has a flag above the pane,
  24 x 15 px at y 45..60, reading "C1" or "C2" in black. Small squares mark where each cursor meets the two curves.
- **Placement**: T CURSOR shows both, as the Scope's T CURSOR shows two time cursors. They first appear at a third and
  two thirds of the axis, snapped to measured points.
- **Touch**: a flag's hit area is 44 x 44 px, and dragging a flag moves its cursor. A tap or drag in the plot moves the
  nearer cursor (the secondary way, guidelines section 3). Move fast in the top bar sets the drag step, as on every
  plot.
- **Values**: cursors snap to measured points and never interpolate, although `bode_interp` exists. A reading is
  always a measurement.

**Cursor readout (owner: in the top division, not over the plot).**

- **Box**: grey, x 44..282, y 62..114, in the top division, right of the "10 dB" label.
- **Rows**: three 13 px rows at a 16 px pitch: C1, C2 and C2-C1. Each has the name in the cursor colour (the
  difference row in white), the frequency in white, the level in the magnitude colour and the phase in the phase
  colour.
- **Example**: "C1 56.17kHz -7.45dB -60.8deg". The widest row, "C2-C1 33.27kHz -13.67dB -129.2deg", fits in 238 px.
- **Flagged points**: when a cursor's point is flagged, its row shows the reason in grey instead of the phase, such as
  "unsettled", "near floor", "CH2 clipped" or "spliced". The C2-C1 row is grey too. A number from a flagged point never
  looks like a good one.
- **The warning beside it**: while the readout shows, the warning line centres in the free width between the readout
  and the right axis's top label (x 282..662, centre 472), still at y 70. A 50-character warning (about 355 px) then
  spans x 295..650. Without the readout, the warning is top-middle as in every mode.

**Before the first sweep** the empty pane shows one instruction of two lines, top-middle in the warning-line style,
saying what to connect and what to press (section 6). The full wiring explanation (the 1 kOhm source, H = CH2 / CH1,
passive DUTs only, wiring A and B) belongs in the user guide, not on the plot. Entering the mode never starts a sweep:
the post may be wired to anything. That keeps fw 0.2.66's rule.

''')

rep('''| T CURSOR | frequency cursor | shows or hides the cursor (section 2) |''',
    '''| T CURSOR | frequency cursors C1 and C2 | shows or hides both cursors and their readout (section 2) |''')

between("### 4.1 Display fold-out", "### 4.2 Sweep fold-out", '''### 4.1 Display fold-out

Three variant rows with icons, the chosen one green, as in Scope and Spectrum:

- **Mag + phase** (two overlaid curves, one dotted): the default.
- **Magnitude** (a flat line that rolls off).
- **Phase** (a dotted falling S).

Then a separator, and **Frequency axis**, Log or Linear, default Log **(owner)**. A tap toggles it. The row sits in
Display because it is how the plot is shown, and the guideline puts a variant's own parameters inside the Display
fold-out. Spectrum's Scale and Ref level are in Analyse, but they set the level axis of a computed spectrum; there is
no analysis fold-out that holds this one.

The axis setting is display only: the sweep's points stay log-spaced (bode-design 3.1). On a Linear axis over a wide
span they bunch at the left. Over the narrow spans that call for a linear axis, such as 20 .. 100 kHz around a tank,
log and linear spacing differ by little. The Linear axis has labels at 1-2-5 steps of the span.

New firmware: the overlaid chart and the single-curve variants, plus three bits in settings word 220. That word holds
0xB100 | settle | CH2 range << 2. Bits 3..4 take the variant, bit 5 the cursors (section 8) and bit 6 the frequency
axis.

''')

between("**Before the first sweep (owner: one short instruction).**", "## 7. Top bar in this mode", '''**Before the first sweep (owner: one short instruction).** fw 0.2.66 draws five centred white lines in 16 px
(`font_3`) at y 150..254 over the empty panes (`bode_view.c` `draw_connection`):

```
Calibration post -> DUT input. CH1 tip on the DUT input, CH2 tip on the DUT output.
The post is a 3.1 Vpp square through 1 kOhm; H = CH2 / CH1 (CH1 must touch the DUT input).
DUT ground on the probe ground clips. Passive DUTs only: nothing that can drive the post.

RUN: repeated sweeps.  AUTO SET: one sweep.  CTRL: Sweep and Thru.
```

The owner found it "rather confusing". It explains the source and the maths instead of saying what to do, and its
last line describes the controls that this plan changes. The replacement **(owner)** is one instruction of two lines,
in the warning line's place and colour (top-middle, orange, 16 px), shown only while no sweep has run:

```
Connect the calibration post and CH1 to the input, CH2 to the output,
grounds on the probe clips. Press RUN.
```

Width: the first line is 69 characters, about 490 px in 16 px. Centred at x 366 it spans x 121..611, inside the
720 px pane and clear of the axis labels at both edges (the "+135 deg" label starts at x 668). The 50-character cap
does not apply: there is no cursor readout before a sweep. The second line ends on RUN, the one action that starts
sweeping. AUTO SET works too, and its label says so. The instruction goes away with the first point measured. A
warning that arrives before then replaces it, as warnings replace each other.

''')

rep('''| T CURSOR | frequency cursor on / off | hidden (`ui_p1_shown(slot, 2)`); a plot tap always places the cursor (`bode_view_touch`) | `ui_p1_shown`: show T CURSOR and MEASURES in mode 2; a new `bv.cursor_on` toggled by the button and saved in word 220 bit 5; plot taps move the cursor only while it is on; the handle; the readout box (section 2) |''',
    '''| T CURSOR | cursors C1 and C2 on / off | hidden (`ui_p1_shown(slot, 2)`); a plot tap always places the one cursor (`bode_view_touch`) | `ui_p1_shown`: show T CURSOR and MEASURES in mode 2; `bv.cursor` becomes two indices plus `bv.cursor_on`, toggled by the button and saved in word 220 bit 5; flags as handles; plot taps move the nearer cursor only while they are on; the readout in the top division with the C2-C1 row; the warning's centre moves right while it shows (section 2) |''')
rep('''| CTRL, Display | the variants | the Display slot (y 65) is the Sweep button | new Display button and fold-out, single-pane layouts (section 4.1) |''',
    '''| CTRL, Display | the variants, the frequency axis | the Display slot (y 65) is the Sweep button | new Display button and fold-out: Mag + phase (overlaid), Magnitude, Phase; Frequency axis Log / Linear (section 4.1) |''')
rep('''| Plot area | panes, markers, cursor | `axes()` without headroom; `draw_connection`'s five lines | warning lane (section 2); Display variants; `draw_connection` replaced by the two-line instruction and the texts of section 6; "sweeping" rule for markers and cards |''',
    '''| Plot area | chart, markers, cursors | two panes (`MAG_*`, `PH_*`), `axes()` without headroom, no right axis; `draw_connection`'s five lines | one pane with the dB axis left and the deg axis right, no legend; both axes scaled with the top-division headroom; Log / Linear frequency axis (section 2); `draw_connection` replaced by the two-line instruction and the texts of section 6; "sweeping" rule for markers and cards |''')
rep('''   checked at 20 characters, which is 134 px in its 140 px box. Pass.''',
    '''   checked at its widest row, "C2-C1 33.27kHz -13.67dB -129.2deg", inside its 238 px box. The instruction's 69
   characters (490 px) fit the 720 px pane. Pass.''')
rep('''**Recommendation: variant 1, two panes, as the default.**''',
    '''**Decided (owner, 2026-10-19 evening): variant 2's chart without the legend, with variant 1's readout placement.**
The owner kept the concern below about a readout box over the data, and moved the readout into the top division. The
legend went because the axes carry their units and the curves their axis colours. My recommendation before the decision
follows, for the record.

**Recommendation (superseded): variant 1, two panes, as the default.**''')
rep('''6. Variant 1 or variant 2 as the default chart? If variant 1, should the overlaid chart and the second cursor come
   later as a Display variant and a T CURSOR extension (section 9 E)?''',
    '''6. Answered **(owner)**: variant 2's chart without the legend, cursors C1 and C2 with the readout in the top division,
   Frequency axis Log / Linear in the Display fold-out.''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("doc patched")
