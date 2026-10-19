"""boards.py: the Spectrum gets the Scope's record-position slider row under the plot (owner, 2026-10-19 19:40).
The slider is the shown span as a window over 0..fs/2. The plot shrinks to 8 divisions of 44 px (y 50..402), the
frequency labels move to y 406, the cards stay at y 426 and the slider takes y 452..472 as in the Scope. The example is
zoomed one step (0..50 kHz of 0..100 kHz at 200 kSa/s), so the window covers the left half of the track."""
import os

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "boards.py")
s = open(P, encoding="utf-8").read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('SX, SY, SW, SH = 6, 50, 720, 392   # 10 x 8 divisions of 72 x 49 (no header, no slider row)',
    'SX, SY, SW, SH = 6, 50, 720, 352   # 10 x 8 divisions of 72 x 44; the slider row under it (owner, 19:40)\n'
    'SPAN_KHZ, NYQ_KHZ = 50.0, 100.0     # shown span 0..50 kHz, zoomed one step from 0..fs/2 at 200 kSa/s')
rep('''    pxk = SW / 100.0
    peaks = {0: -9.4}
    for n in range(1, 100, 2):''', '''    pxk = SW / SPAN_KHZ
    peaks = {0: -9.4}
    for n in range(1, int(SPAN_KHZ), 2):''')
rep('''    for j in range(1, 8):
        o.append(f'<line x1="0" y1="{j * 49 + 0.5}" x2="{SW}" y2="{j * 49 + 0.5}" stroke="{LG1}" stroke-dasharray="1 4"></line>')''',
    '''    for j in range(1, 8):
        o.append(f'<line x1="0" y1="{j * 44 + 0.5}" x2="{SW}" y2="{j * 44 + 0.5}" stroke="{LG1}" stroke-dasharray="1 4"></line>')''')
rep('''        yy = SY + j * 49 + 2 if j == 0 else SY + j * 49 - 17''', '''        yy = SY + j * 44 + 2 if j == 0 else SY + j * 44 - 17''')
rep('''    out.append(text(SX, 452, "0 Hz", 13, LG2))
    out.append(text(SX + SW / 2 - 20, 452, "50 kHz", 13, LG2))
    out.append(text(SX + SW - 50, 452, "100 kHz", 13, LG2))''',
    '''    out.append(text(SX, 406, "0 Hz", 13, LG2))
    out.append(text(SX + SW / 2 - 20, 406, "25 kHz", 13, LG2))
    out.append(text(SX + SW - 43, 406, "50 kHz", 13, LG2))''')
rep('''def spectrum_view(right):''', '''def spectrum_slider(start_khz=0.0, span_khz=SPAN_KHZ, nyq_khz=NYQ_KHZ):
    """the Scope's slider row (menu.c: rect 2,452,726,20; the window box 14 px high at y 455) with the shown span as a
    window over 0..fs/2: left edge at start / (fs/2), width span / (fs/2) of the 720 px inside the track; dragging it
    pans the centre, the span stays (owner)"""
    x0 = 5 + start_khz / nyq_khz * 720
    w = max(span_khz / nyq_khz * 720, 12)
    return (box(2, 452, 726, 20, f"border: 1px solid {LG1}") +
            box(round(x0), 455, round(w), 14, f"border: 1px solid {LG2}"))


def spectrum_view(right):''')
rep('''                      spectrum_plot(),   # no slider row in the spectrum''',
    '''                      spectrum_plot(), spectrum_slider(),   # the slider row: the span over 0..fs/2 (owner)''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("boards.py patched")
