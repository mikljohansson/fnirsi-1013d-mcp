"""bode-ui.md, owner corrections: the C1 / C2 readout box bottom left as variant 2 drew it (the overlap is accepted);
Display = Overlaid / Magnitude only / Phase only, the two-pane layout dropped."""
import sys

P = sys.argv[1]
s = open(P, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


def between(start, end, new):
    global s
    a = s.index(start)
    b = s.index(end, a)
    s = s[:a] + new + s[b:]


rep('''- **Two frequency cursors, C1 and C2**, under T CURSOR. The readout sits in the data-free top division, variant 1's
  placement, not in a box over the plot. It has a C2-C1 row (section 2).''',
    '''- **Two flagged frequency cursors, C1 and C2**, which T CURSOR shows and hides. Their readout box sits at the bottom
  left of the pane with a C2-C1 row, exactly as variant 2 drew it. The box may cover part of the response; the owner
  accepts that (section 2).
- **Display fold-out: Overlaid (the default, the chosen chart), Magnitude only, Phase only.** The two-pane layout is
  dropped from the design (section 4.1).''')

rep('''| Cursors C1 and C2 | the two flagged cursors, the readout in the top division with the C2-C1 row, and the warning centred right of it |''',
    '''| Cursors C1 and C2 | the two flagged cursors and the readout box at the bottom left with the C2-C1 row; the warning top-middle as always |''')
rep('''| CTRL: Display fold-out with Frequency axis | Mag + phase, Magnitude, Phase, each with an icon; a separator; Frequency axis Log |''',
    '''| CTRL: Display fold-out with Frequency axis | Overlaid, Magnitude only, Phase only, each with an icon; a separator; Frequency axis Log |''')

rep('''**Scales and the top division.** Both axes are chosen from the data, and both keep the top division (y 60..118) free
of data when they can. That division holds the warning line and the cursor readout without covering the curves.''',
    '''**Scales and the top division.** Both axes are chosen from the data, and both keep the top division (y 60..118) free
of data when they can. That division holds the warning line without covering the curves.''')

rep('''**Display variants.** Mag + phase is the overlaid chart. Magnitude and Phase each draw one curve and only its axis,
with the same scale rules.''',
    '''**Display variants (owner).** Overlaid is the chart above, both curves on both axes, and the default. Magnitude only
and Phase only each draw one curve and only its axis, over the same pane with the same scale rules. There is no
two-pane layout.''')

between("**Cursor readout (owner: in the top division, not over the plot).**", "**Before the first sweep** the empty pane",
        '''**Cursor readout (owner: the box at the bottom left, as variant 2 drew it).**

- **Box**: grey, x 10..262, y 356..408, inside the pane's bottom left, the position of the DPOS350P's readout. It shows
  while T CURSOR is on.
- **Overlap**: it may cover part of the response, such as a low skirt or the lowest dB label. The owner accepts that,
  and the cursors can be moved or switched off.
- **Rows**: three 13 px rows at a 16 px pitch: C1, C2 and C2-C1. Each has the name in the cursor colour (the
  difference row in white), the frequency in white, the level in the magnitude colour and the phase in the phase
  colour.
- **Example**: "C1 56.17kHz -7.45dB -60.8deg". The widest row, "C2-C1 33.27kHz -13.67dB -129.2deg", fits in 252 px.
- **Flagged points**: when a cursor's point is flagged, its row shows the reason in grey instead of the phase, such as
  "unsettled", "near floor", "CH2 clipped" or "spliced". The C2-C1 row is grey too. A number from a flagged point never
  looks like a good one.
- **Warnings**: the warning line stays top-middle at y 70 in every state. The readout is at the bottom, so they never
  meet.

''')

rep('''| T CURSOR | frequency cursors C1 and C2 | shows or hides both cursors and their readout (section 2) |''',
    '''| T CURSOR | frequency cursors C1 and C2 | toggles both cursors and their readout box **(owner)** (section 2) |''')
rep('''| 65 | Display | Mag+Ph, Mag, Phase | Display fold-out |''',
    '''| 65 | Display | Overlay, Mag, Phase | Display fold-out |''')

between("Three variant rows with icons, the chosen one green, as in Scope and Spectrum:", "Then a separator, and **Frequency axis**",
        '''Three variant rows with icons, the chosen one green, as in Scope and Spectrum **(owner)**:

- **Overlaid** (two curves in one box, one dotted): the default, the chosen chart. The button reads "Overlay": 7
  characters, about 47 px in 13 px, in the 51 px button.
- **Magnitude only** (a flat line that rolls off).
- **Phase only** (a dotted falling S).

''')

rep('''- **The cursor's values are not cards.** They are the cursor readout of section 2, as the Scope's cursor values are
  its cursor box. fw 0.2.66's f, |H| and Phase cards go.''',
    '''- **The cursors' values are not cards.** They are the C1 / C2 readout box of section 2, as the Scope's cursor values
  are its cursor box. fw 0.2.66's f, |H| and Phase cards go.''')
rep('''most 50 characters: centred at x 366 in 16 px that spans at most x 188..544, clear of the cursor readout. In 13 px
(`font_2`, what `bode_view.c` uses) it is narrower still.''',
    '''most 50 characters: centred at x 366 in 16 px that spans at most x 188..544, clear of the axis labels at both edges.
In 13 px (`font_2`, what `bode_view.c` uses) it is narrower still.''')

rep('''flags as handles; plot taps move the nearer cursor only while they are on; the readout in the top division with the C2-C1 row; the warning's centre moves right while it shows (section 2) |''',
    '''flags as handles; plot taps move the nearer cursor only while they are on; the readout box at the bottom left with the C2-C1 row **(owner)** (section 2) |''')
rep('''new Display button and fold-out: Mag + phase (overlaid), Magnitude, Phase; Frequency axis Log / Linear (section 4.1) |''',
    '''new Display button and fold-out: Overlaid, Magnitude only, Phase only; Frequency axis Log / Linear **(owner)** (section 4.1) |''')
rep('''   checked at its widest row, "C2-C1 33.27kHz -13.67dB -129.2deg", inside its 238 px box.''',
    '''   checked at its widest row, "C2-C1 33.27kHz -13.67dB -129.2deg", inside its 252 px box.''')
rep('''   which sits just under the top line. Placing the readout in the warning lane, with the 50-character cap, resolves
   both. The first mockup render showed the conflict.''',
    '''   which sits just under the top line. The first mockup render showed the conflict. The final design **(owner)** puts
   the C1 / C2 box at the bottom left as the DPOS350P does and accepts that it may cover part of the response. The
   warning keeps the free top division to itself.''')
rep('''**Decided (owner, 2026-10-19 evening): variant 2's chart without the legend, with variant 1's readout placement.**
The owner kept the concern below about a readout box over the data, and moved the readout into the top division. The
legend went because the axes carry their units and the curves their axis colours. My recommendation before the decision
follows, for the record.''',
    '''**Decided (owner, 2026-10-19 evening): variant 2's chart without the legend, its cursors and its bottom-left readout
box.** The legend went because the axes carry their units and the curves their axis colours. The readout box stays
where variant 2 drew it, and the owner accepts that it covers part of the response. The Display fold-out offers
Overlaid (default), Magnitude only and Phase only; the two-pane layout is dropped. My recommendation before the decision
follows, for the record.''')
rep('''6. Answered **(owner)**: variant 2's chart without the legend, cursors C1 and C2 with the readout in the top division,
   Frequency axis Log / Linear in the Display fold-out.''',
    '''6. Answered **(owner)**: variant 2's chart without the legend; cursors C1 and C2 with the readout box at the bottom
   left, the overlap accepted; Display Overlaid / Magnitude only / Phase only, no two-pane layout; Frequency axis Log /
   Linear in the Display fold-out.''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("doc patched")
