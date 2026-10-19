"""bode-ui.md sections 2 and 8, ux-makeover.md section 3: the slider row under the plot in Spectrum and Frequency
response (owner, 2026-10-19 19:40)."""
import sys

B, M = sys.argv[1], sys.argv[2]


def edit(path, pairs):
    s = open(path, encoding="utf-8").read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:80], s.count(a))
        s = s.replace(a, b)
    open(path, "w", encoding="utf-8", newline="\n").write(s)


edit(B, [
    ('''- **Pane**: y 60..408, six divisions of 58 px, x 6..726. There are no separate magnitude and phase panes any more;''',
     '''- **Pane**: y 60..396, six divisions of 56 px, x 6..726 (58 px before the slider row came). There are no separate
  magnitude and phase panes any more;'''),
    ('''  y 413 in grey.
- **Cards**: at y 446 (`UI_MS_SPEC_Y`), as in Spectrum. There is no record-position slider in this mode.''',
     '''  y 400 in grey.
- **Cards**: at y 426 (`UI_MS_SCOPE_Y`), as in the Scope, between the plot and the slider.
- **Slider row (owner, 2026-10-19 19:40)**: y 452..472, the Scope's record-position slider in the same construction
  (rect 2, 452, 726 x 20; the window a 14 px box at y 455) and the same drag (fw 0.2.64: the window follows the finger).

**The slider: Start..Stop over the sweepable range (owner).**

- **Track**: 100 Hz .. 10 MHz on a log scale over the 720 px inside the track, the range the owner named. The window
  spans Start..Stop on the same scale; 5 kHz .. 500 kHz draws x 250..538.
- **Drag**: the window follows the finger. Start and Stop move together by whole steps of the 1-2-5 table, the same
  number of steps each. That keeps their ratio exactly when the span is a whole number of decades, and within one step
  otherwise. Both stop at the track ends, so the window never changes width by being pushed.
- **While dragging**: the frequency axis and its labels follow, and points of the current table that fall inside the
  new span stay drawn (they are measurements). Nothing is swept yet.
- **On release, a sweep in progress restarts at the new span.** The point in progress is dropped (`bode_stop(e, 1)`),
  the table is cleared and the sweep starts again from the new Start. RUN keeps repeating at the new span.
- **On release while stopped**, one sweep runs at the new span, as Single sweep does, and the run state goes back to
  STOP at its end. A pan asks for data at the new span, and a stopped table at the old span would no longer match the
  axis.
- **Consequences**: the new span is a screen change, so the host's exact settings stop applying (`host_cfg_valid` = 0).
  A thru for the old span turns stale, and its card and warning say so. Start and Stop are saved in word 219 as from
  the fold-out.
- **Edge case**: a Start below 100 Hz (the fold-out allows 1 Hz) pins the window's left edge to the track's left end.
  It is drawn without its left border, so the window reads as running off the track. Owner question 7.
- **During AUTO SET's overview** the window spans the whole track (100 Hz .. 10 MHz) and dragging is ignored.

The row costs the pane 12 px (two px per division) and moves the cards up to the Scope's y 426. The C1 / C2 readout box
moves with the pane's bottom, to y 342..394.'''),
    ('''- **Box**: grey, x 10..262, y 356..408, inside the pane's bottom left, the position of the DPOS350P's readout. It shows''',
     '''- **Box**: grey, x 10..262, y 342..394, inside the pane's bottom left, the position of the DPOS350P's readout. It shows'''),
    ('''| Main menu row | opens the mode |''',
     '''| Slider row | Start..Stop over 100 Hz .. 10 MHz; drag pans both, then re-sweeps | none (no slider in the mode) | the Scope's slider row at y 452..472 drawn from Start / Stop; the drag handler (fw 0.2.64's follow-the-finger rule) steps both indices together; on release `bode_stop(e, 1)` and `bv_start`, single when stopped; pane to six divisions of 56 px, labels y 400, cards y 426 (section 2) **(owner)** |
| Main menu row | opens the mode |'''),
    ('''6. Answered **(owner)**:''', '''7. A Start below the slider's 100 Hz left end: let the window run off the track as drawn, extend the track to
   1 Hz (the fold-out's lowest Start; the window gets narrower for the same span), or limit Start to 100 Hz?
6. Answered **(owner)**:'''),
])

edit(M, [
    ('''  are cards. The record-position slider is hidden in Spectrum (it shows nothing useful there). Fold-outs open to the''',
     '''  are cards. **Spectrum slider (owner, 2026-10-19 19:40)**: the Scope's slider row stays under the plot in Spectrum
  too, with the same construction and the same drag as the Scope (fw 0.2.64: the window follows the finger). It shows
  the shown span as a window over 0..fs/2 at the current sample rate: the left edge at start / (fs/2), the width
  span / (fs/2) of the track. Dragging pans the centre and keeps the span, the same pan as dragging the plot. At full
  span the window fills the track and there is nothing to pan. To make room the plot becomes 8 divisions of 44 px
  (y 50..402), the frequency labels sit at y 406, and the cards stay at the Scope's y 426 between plot and slider.
  The Frequency response mode gets the same row for Start..Stop (`docs/plans/bode-ui.md` section 2). This replaces
  "the record-position slider is hidden in Spectrum". Fold-outs open to the'''),
])
print("docs patched")
