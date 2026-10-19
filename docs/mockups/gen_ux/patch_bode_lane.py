"""bode.py: the cursor readout in the data-free top division (two lines, x 46..186), warnings capped at 50 characters
(centred at x 366 they span at most x 188..544, clear of the readout)."""
import os

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bode.py")
s = open(P, encoding="utf-8").read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''def cursor_box(f, db, ph):
    """the cursor readout where the scope draws its cursor readout (a grey box at the plot's top left), right of the
    level labels: f, |H| in the magnitude colour, phase in the phase colour"""
    inner = (text(6, 3, f"f {fmt_f(f)}", 13) + text(6, 20, f"|H| {db:+.2f}dB", 13, CH2) +
             text(6, 37, f"Ph {ph:+.1f}deg", 13, PHASE))
    return box(46, 64, 100, 56, f"background: {GREY}; border-radius: 2px", inner)''',
    '''def cursor_box(f, db, ph, flag=None):
    """the cursor readout where the scope draws its cursor readout (a grey box at the plot's top left), inside the top
    division that the axis rule keeps free of data: x 46..186, y 61..95, two 13 px lines: f (and the point's flag in
    grey when it has one), then |H| in the magnitude colour and the phase in the phase colour"""
    l1 = f"f {fmt_f(f)}" + (f' <span style="color: {NOTE}">{flag}</span>' if flag else "")
    inner = (text(6, 1, l1, 13) + text(6, 17, f"{db:+.2f}dB", 13, CH2) + text(72, 17, f"{ph:+.1f}deg", 13, PHASE))
    return box(46, 61, 140, 34, f"background: {GREY}; border-radius: 2px", inner)''')
rep('''def warn(msg):
    w = int(len(msg) * 7.1) + 24''', '''def warn(msg):
    assert len(msg) <= 50, msg          # the cap that keeps the warning clear of the cursor readout
    w = int(len(msg) * 7.1) + 24''')
rep('warn("The trigger was EXT (the generator\'s pin): set to CH1")', 'warn("Trigger was EXT (the generator\'s pin): now CH1")')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("bode.py patched")
