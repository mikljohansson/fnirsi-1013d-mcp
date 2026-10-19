"""bode-ui.md: owner feedback of 2026-10-19 evening (CTRL kept its meaning, one-line pre-sweep instruction, the name's
fit, variant 2 and the comparison)."""
import sys

P = sys.argv[1]
s = open(P, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''AUTO SET to sweep control and was entered from the generator menu. This plan is the change list for the implementation
batch.''', '''AUTO SET to sweep control and was entered from the generator menu. CTRL kept its meaning in fw 0.2.66: its page
already holds Sweep, Thru and Grid. So the remap to undo is RUN/STOP and AUTO SET only. The owner also found the text on
the plot before the first sweep "rather confusing". This plan is the change list for the implementation batch.''')

rep('''Names: the mode, its main-menu row and its MEASURES title say "Frequency response". The plot is a Bode plot, and the
protocol and host names stay as they are (BODE_CTRL, READ_BODE, `scope_bode_sweep`, `bode_*.c`).''',
    '''Names: the mode, its main-menu row and its MEASURES title say "Frequency response". "Bode" stays the internal name:
files, the protocol and the host tool (BODE_CTRL, READ_BODE, `scope_bode_sweep`, `bode_*.c`).

The name's fit in the main menu: "Frequency response" is 18 characters, about 128 px in the 16 px font. The row's label
is centred at x 95, right of the icon, which ends at x 42, inside a button that ends at x 140. One line has about 90 px,
so the name does not fit on one line. Two 16 px lines do fit: "Frequency" (9 characters, about 64 px) over "response"
(8 characters, about 57 px). That is how fw 0.2.68 already draws "Spectrum" over "analyser" (`ui_mm_labels`, line 2
at +17 px in the 35 px button). No 13 px line is needed. The main-menu artboard shows it.''')

rep('''| Before the first sweep: wiring | the empty panes on the default 100 Hz .. 1 MHz axis, the wiring panel, the cards' "no sweep" texts, the EXT note on the warning line |''',
    '''| Before the first sweep: one instruction | the empty panes on the default 100 Hz .. 1 MHz axis, the two-line instruction top-middle, the cards' "no sweep" texts |''')
rep('''| MEASURES: the mode's list | the stock MEASURES menu with the mode's ten items, six chosen, ranks shown |''',
    '''| MEASURES: the mode's list | the stock MEASURES menu with the mode's ten items, six chosen, ranks shown |
| Variant 2: one pane, two y axes, legend | the DPOS350P chart in our language (section 9 F); bars, cards and CTRL as variant 1 |
| Variant 2: cursors C1 and C2 with the difference row | the two flagged cursors and their readout box |

On the canvas, variant 1's action page sits beside the two variant 2 boards, with a note on the comparison.''')

rep('''- **Before the first sweep** a panel over the empty panes shows the wiring. It draws the calibration post, the DUT, the
  CH1 and CH2 tips and the ground clips, with three 16 px lines and two 13 px lines (section 6). Entering the mode
  never starts a sweep: the post may be wired to anything. That keeps fw 0.2.66's rule.''',
    '''- **Before the first sweep** the empty panes show one instruction of two lines, top-middle in the warning-line
  style, saying what to connect and what to press (section 6). The full wiring explanation (the 1 kOhm source,
  H = CH2 / CH1, passive DUTs only, wiring A and B) belongs in the user guide, not on the plot. Entering the mode
  never starts a sweep: the post may be wired to anything. That keeps fw 0.2.66's rule.''')

rep('''(86 characters; the clip advice is in the wiring panel) |''', '''(86 characters; the clip advice goes to the user guide) |''')

a = s.index("The wiring panel (16 px, then 13 px in grey):")
b = s.index("## 7. Top bar in this mode")
s = s[:a] + '''**Before the first sweep (owner: one short instruction).** fw 0.2.66 draws five centred white lines in 16 px
(`font_3`) at y 150..254 over the empty panes (`bode_view.c` `draw_connection`):

```
Calibration post -> DUT input. CH1 tip on the DUT input, CH2 tip on the DUT output.
The post is a 3.1 Vpp square through 1 kOhm; H = CH2 / CH1 (CH1 must touch the DUT input).
DUT ground on the probe ground clips. Passive DUTs only: nothing that can drive the post.

RUN: repeated sweeps.  AUTO SET: one sweep.  CTRL: Sweep and Thru.
```

The owner found it "rather confusing". It explains the source and the maths instead of saying what to do, and its
last line describes the controls that this plan changes. The replacement is one instruction of two lines, in the
warning line's place and colour (top-middle, orange, 16 px), shown only while no sweep has run:

```
Connect the post and CH1 to the input, CH2 to the output,
grounds on the probe clips. Press RUN.
```

The longer line is 57 characters, about 405 px, centred at x 366 (x 163..568). The 50-character cap does not apply:
there is no cursor readout before a sweep. The second line ends on RUN, the one action that starts sweeping. AUTO SET
works too, and its label says so. The instruction goes away with the first point measured. A warning that arrives
before then replaces it, as warnings replace each other.

''' + s[b:]

rep('''`axes()` without headroom; the wiring text names AUTO SET as one sweep | warning lane (section 2); Display variants; wiring panel and texts (section 6); "sweeping" rule for markers and cards |''',
    '''`axes()` without headroom; `draw_connection`'s five lines | warning lane (section 2); Display variants; `draw_connection` replaced by the two-line instruction and the texts of section 6; "sweeping" rule for markers and cards |''')

rep('''7. **Fonts and widths.** Only 13 and 16 px are used, plus 24 px for "DUT" in the wiring drawing. Every line is''',
    '''7. **Fonts and widths.** Only 13 and 16 px are used. Every line is''')

rep('''### E. Questions for the owner''', '''### F. Variant 2: FNIRSI's DPOS350P chart in our language

The owner asked for a second variant that takes the DPOS350P's Bode screen where it suits our hardware and guidelines.
Its top controls and bottom parameter boxes are adapted to our language, not copied. Variant 2 keeps our top bar,
right bar, cards, MEASURES and CTRL page exactly as variant 1, and changes only the chart.

| DPOS350P element | Variant 2 | Why |
|---|---|---|
| One pane, magnitude on a left y axis, phase on a right y axis | kept: one pane y 60..408, six divisions of 58 px; dB labels left in the magnitude colour, deg labels right in the phase colour | the chart is what the owner finds interesting; both curves get the full height |
| dB / deg legend at the top right | kept, as a grey box inside the top division | with two axes the reader needs the key |
| Cursors C1 and C2, flagged at the top | kept: two dotted cursors in the cursor colour, flags named C1 / C2 above the pane, dragged by the flag | matches the Scope's two time cursors under T CURSOR |
| Readout box, bottom left: per cursor f, dB, deg, plus a difference row | kept: grey box x 10..262, y 356..408, rows C1, C2 and C2-C1 in 13 px; f white, dB in the magnitude colour, deg in the phase colour | the difference row gives the slope and the phase shift between two frequencies |
| Top bar: Return, Calibration, Screenshot, Cursor, Loop, play / pause icon | mapped: Menu, Thru on the CTRL page, SAVE PIC, T CURSOR, RUN/STOP; the run state is our chip | our bars stay as on Scope and Spectrum (owner) |
| Bottom boxes: Start frequency, Stop frequency, Frequency count | mapped to the Sweep fold-out's Start, Stop and Points | parameters live on the CTRL page; we have no bottom parameter bar by decision |
| Movement (Slow move) | mapped to the top bar's Move fast | it is the same thing: the drag step of what is moved on the plot |
| Amplitude, Offset | left out | our generator is a fixed 3.1 Vpp square on the calibration post, with no amplitude or offset |

The phase axis scales from the data in 45 or 90 degrees per division, with the same headroom rule as the magnitude
where it still fits. On the tank it reads +135 .. -135 deg.

**Recommendation: variant 1, two panes, as the default.**

- **Own scales.** Each curve has its own axis, so nobody has to check a legend to know which numbers belong to which
  curve.
- **Phase reads directly.** The phase grid is fixed at 90 degrees per division, so a -90 or -180 crossing is read off
  the grid.
- **Nothing covers data.** The empty top division holds the warning and the cursor readout. Variant 2's readout box
  and legend sit on the data: on the tank the box covers the magnitude's low-frequency skirt, and any curve that sits
  low at the left meets the same box.
- **Edges stay readable.** Two sets of 13 px labels crowd both edges of variant 2's pane. Variant 1 has one label column.

Variant 2's strengths are real. Both curves get twice the height, and features line up in one place: the phase crossing
under the magnitude peak. Two of its ideas are worth taking later, in either variant:

1. **A second cursor with a C2-C1 row.** It matches the Scope's two time cursors under T CURSOR. In variant 1 its
   three rows do not fit the 36 px top division, so the box would go in the phase pane's top left or the division grows.
2. **The overlaid chart as a third Display variant** ("Overlaid" next to Mag + phase, Magnitude and Phase). It uses the
   same data and controls, so it is drawing work only. The owner can then choose per measurement.

### E. Questions for the owner''')

rep('''5. Should Saved waveforms list the CSV tables? That needs a viewer for them.''',
    '''5. Should Saved waveforms list the CSV tables? That needs a viewer for them.
6. Variant 1 or variant 2 as the default chart? If variant 1, should the overlaid chart and the second cursor come
   later as a Display variant and a T CURSOR extension (section 9 F)?''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("doc patched")
