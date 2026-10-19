"""bode.py: the magnitude axis from the data (bode_view.c axes) plus one division of headroom (the warning lane), and
the fw 0.2.68 run button in the menu board's scope background."""
import os

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bode.py")
s = open(P, encoding="utf-8").read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''def freq_label(f):''', '''def axes_for(f0, f1, pts):
    """bode_view.c axes() with the proposed headroom: 5, 10 or 20 dB per division, the first that holds the data in
    six divisions with the top line one whole division above the maximum, so the maximum never sits in the top
    division, which is the warning line's lane (y 70..96)"""
    dbs = [p[1] for p in pts]
    if not dbs:
        return Axes(f0, f1, 0.0, 10.0)
    mx, mn = max(dbs), min(dbs)
    for d in (5.0, 10.0, 20.0):
        top = math.ceil(mx / d) * d if mx >= 0 else -math.floor(-mx / d) * d
        if top - mx < d:
            top += d
        if top - mn <= 6 * d:
            break
    return Axes(f0, f1, top, d)


def freq_label(f):''')

rep('''TANK_AXES = Axes(*SPAN, top=0.0, div=10.0)''', '''TANK_AXES = axes_for(SPAN[0], SPAN[1], [(f, *h_bp(f), True) for f in FREQS])''')

rep('''    a = TANK_AXES
    n = 37
    pts = sweep_points(upto=n)''', '''    n = 37
    pts = sweep_points(upto=n)
    a = axes_for(SPAN[0], SPAN[1], pts)''')

rep('''    a = Axes(100, 1e7, top=0.0, div=10.0)
    fr = log_points(100, 1e7, 20)
    n = 12
    pts = []
    for i, f in enumerate(fr[:n]):
        db, ph = h_bp(f)
        pts.append((f, db, ph, db > -58))''', '''    fr = log_points(100, 1e7, 20)
    n = 12
    pts = []
    for i, f in enumerate(fr[:n]):
        db, ph = h_bp(f)
        pts.append((f, db, ph, db > -58))
    a = axes_for(100, 1e7, pts)''')

rep('''    write_board("MainMenu.dc.html", "Main menu with the Frequency response row",
                scope_view(menu_active=True) + "\\n" + "\\n".join(o))''',
    '''    bg = scope_view(menu_active=True)
    old = rbtn(65, [("RUN/", 16), ("STOP", 16)], kind="run")
    assert bg.count(old) == 1
    bg = bg.replace(old, run_button(True))      # fw 0.2.68: the button shows the action, STOP while running
    write_board("MainMenu.dc.html", "Main menu with the Frequency response row", bg + "\\n" + "\\n".join(o))''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("bode.py patched")
