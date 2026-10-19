"""Frequency response, variant 2: the DPOS350P chart concept (one pane, two y axes, legend, cursors C1 / C2 with a
readout box and a difference row) drawn in our language: our top bar, right bar, cards and CTRL page unchanged.
Also the revised pre-sweep board (one two-line instruction, owner 2026-10-19).

usage: python bode_v2.py <out_dir>
"""
import math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bode as B  # noqa  (reads <out_dir> from argv as well)
from common import *  # noqa

V_Y, V_DIV, V_N = 60, 58, 6          # one pane y 60..408, six divisions
RO_Y = 356                            # the C1 / C2 readout box's top (bottom left of the pane)


class Axes2:
    """log f as variant 1; magnitude on the left axis, phase on the right axis, each with the warning lane rule"""
    def __init__(self, f0, f1, pts):
        self.l0, self.l1, self.f0, self.f1 = math.log10(f0), math.log10(f1), f0, f1
        self.mtop, self.mdiv = self.scale([p[1] for p in pts], (5.0, 10.0, 20.0))
        self.ptop, self.pdiv = self.scale([p[2] for p in pts], (45.0, 90.0))

    @staticmethod
    def scale(vals, divs):
        mx, mn = max(vals), min(vals)
        for d in divs:
            top = math.ceil(mx / d) * d
            if top - mn <= V_N * d:
                if top - mx < d and top + d - mn <= V_N * d:
                    top += d          # the warning lane, where it still fits
                break
        return top, d

    def x(self, f):
        return B.PX + (math.log10(f) - self.l0) / (self.l1 - self.l0) * B.PW

    def ym(self, db):
        return V_Y + (self.mtop - db) / self.mdiv * V_DIV

    def yp(self, deg):
        return V_Y + (self.ptop - deg) / self.pdiv * V_DIV


def chart(a, pts, markers=True, cursors=()):
    o = []
    dots = lambda x1, y1, x2, y2, gap: (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{LG1}" '
                                        f'stroke-width="1" stroke-dasharray="1 {gap}"></line>')
    for i in range(V_N + 1):
        o.append(dots(B.PX, V_Y + i * V_DIV + 0.5, B.PX + B.PW, V_Y + i * V_DIV + 0.5, 3))
    labels = []
    dec = 1.0
    while dec <= 1e7:
        for k, m in enumerate((1, 2, 5)):
            f = dec * m
            if a.f0 * 0.999 <= f <= a.f1 * 1.001:
                x = round(a.x(f)) + 0.5
                o.append(dots(x, V_Y, x, V_Y + V_N * V_DIV, 7 if k else 2))
                if k == 0:
                    labels.append((x, B.freq_label(f)))
        dec *= 10
    for colour, fy in [(CH2, lambda p: a.ym(p[1])), (B.PHASE, lambda p: a.yp(p[2]))]:
        run = []
        for p in pts + [(None, 0, 0, False)]:
            if p[3]:
                run.append(p)
                continue
            if len(run) > 1:
                o.append(f'<polyline points="{" ".join(f"{a.x(q[0]):.1f},{fy(q):.1f}" for q in run)}" fill="none" '
                         f'stroke="{colour}" stroke-width="1"></polyline>')
            run = []
        for p in pts:
            c = colour if p[3] else B.FLAG
            o.append(f'<rect x="{a.x(p[0]) - 1:.1f}" y="{fy(p) - 1:.1f}" width="3" height="3" fill="{c}"></rect>')
    if markers:
        ref = 20 * math.log10(B.K)
        bw = B.F0 / B.Q
        fl = -bw / 2 + math.sqrt(bw * bw / 4 + B.F0 * B.F0)
        for x, y in [(a.x(fl), a.ym(ref - 3)), (a.x(fl + bw), a.ym(ref - 3)), (a.x(B.F0), a.ym(ref)),
                     (a.x(fl), a.yp(45)), (a.x(fl + bw), a.yp(-45))]:
            o.append(f'<polygon points="{x - 4.5:.1f},{y - 8:.1f} {x + 4.5:.1f},{y - 8:.1f} {x:.1f},{y - 3:.1f}" '
                     'fill="#ffffff"></polygon>')
    for name, (f, db, ph, _) in cursors:
        x = round(a.x(f)) + 0.5
        o.append(f'<line x1="{x}" y1="{V_Y}" x2="{x}" y2="{V_Y + V_N * V_DIV}" stroke="{B.CURSOR}" stroke-width="1" '
                 'stroke-dasharray="1 2"></line>')
        for y in (a.ym(db), a.yp(ph)):
            o.append(f'<rect x="{x - 3.5:.1f}" y="{y - 3.5:.1f}" width="7" height="7" fill="none" stroke="#ffffff" '
                     'stroke-width="1"></rect>')
    html = [f'<svg width="730" height="480" viewBox="0 0 730 480" aria-hidden="true" '
            f'style="position: absolute; left: 0; top: 0">{"".join(o)}</svg>']
    # the flags: draggable handles above the pane, named C1 / C2 (FNIRSI's flags, our cursor colour)
    for name, (f, *_r) in cursors:
        x = round(a.x(f))
        html.append(box(x - 12, 45, 24, 15, f"background: {B.CURSOR}; border-radius: 2px",
                        text(3, -1, name, 13, "#000000")))
    for x, t in labels:
        w = len(t) * 6.7
        html.append(text(round(min(max(x - w / 2, B.PX), B.PX + B.PW - w)), B.FL_Y, t, 13, B.NOTE))
    for i in range(V_N):
        v = a.mtop - i * a.mdiv
        html.append(text(B.PX + 3, V_Y + i * V_DIV + 2, f"{v:g}" + (" dB" if i == 0 else ""), 13, CH2))
        p = a.ptop - i * a.pdiv
        t = f"{p:+g}" if p else "0"
        t += " deg" if i == 0 else ""
        html.append(text(round(B.PX + B.PW - 3 - len(t) * 6.7), V_Y + i * V_DIV + 2, t, 13, B.PHASE))
    # legend, top right, inside the data-free top division
    leg = (f'<svg width="22" height="30" viewBox="0 0 22 30" aria-hidden="true" style="position: absolute; left: 6px; top: 3px">'
           f'<line x1="0" y1="7.5" x2="20" y2="7.5" stroke="{CH2}" stroke-width="2"></line>'
           f'<line x1="0" y1="23.5" x2="20" y2="23.5" stroke="{B.PHASE}" stroke-width="2"></line></svg>' +
           text(32, 2, "|H| dB", 13, CH2) + text(32, 18, "Phase deg", 13, B.PHASE))
    html.append(box(610, 80, 100, 36, f"background: {GREY}; border-radius: 2px", leg))
    return "\n".join(html)


def readout(rows):
    """the C1 / C2 / difference box, bottom left of the pane as on the DPOS350P, in our grey box and 13 px font:
    f white, level in the magnitude colour, phase in the phase colour"""
    o = []
    for i, (name, f, db, ph) in enumerate(rows):
        y = 3 + 16 * i
        o.append(text(6, y, name, 13, B.CURSOR if name != "C2-C1" else "#ffffff"))
        o.append(text(52, y, f, 13))
        o.append(text(122, y, db, 13, CH2))
        o.append(text(184, y, ph, 13, B.PHASE))
    return box(10, RO_Y, 252, 52, f"background: {GREY}; border-radius: 2px", "".join(o))


def v2_view(cursors=False):
    pts = B.sweep_points()
    a = Axes2(*B.SPAN, pts)
    ref = 20 * math.log10(B.K)
    bw = B.F0 / B.Q
    fl = -bw / 2 + math.sqrt(bw * bw / 4 + B.F0 * B.F0)
    curs = []
    body = [B.bode_topbar(run="STOP", gen="50.3kHz", ch1="1V/div", ch2="200mV/div")]
    if cursors:
        i1, i2 = 52, 62
        curs = [("C1", pts[i1]), ("C2", pts[i2])]
    body.append(chart(a, pts, markers=not cursors, cursors=curs))
    if cursors:
        p1, p2 = pts[i1], pts[i2]
        body.append(readout([("C1", B.fmt_f(p1[0]), f"{p1[1]:+.2f}dB", f"{p1[2]:+.1f}deg"),
                             ("C2", B.fmt_f(p2[0]), f"{p2[1]:+.2f}dB", f"{p2[2]:+.1f}deg"),
                             ("C2-C1", B.fmt_f(p2[0] - p1[0]), f"{p2[1] - p1[1]:+.2f}dB", f"{p2[2] - p1[2]:+.1f}deg")]))
    else:
        body.append(B.warn("2 points not settled: choose a longer Settle"))
    body += [B.bode_cards(B.CARD_DEFAULTS_DONE(ref, fl)), B.bode_actions()]
    return "\n".join(body)


def board_first_v1():
    """before the first sweep (revised): the empty panes and one instruction of two lines in the warning-line style"""
    a = B.Axes(100, 1e6)
    cards = [("Level", "no sweep", CH2, B.NOTE), ("-3dB", "no sweep", CH2, B.NOTE), ("Peak", "no sweep", CH2, B.NOTE),
             ("Q", "no sweep", CH2, B.NOTE), ("Sweep", "not run", B.NEUTRAL, B.NOTE), ("Thru", "none", B.NEUTRAL, B.NOTE)]
    l1 = "Connect the post and CH1 to the input, CH2 to the output,"
    l2 = "grounds on the probe clips. Press RUN."
    w = int(max(len(l1), len(l2)) * 7.1) + 24
    instr = box(366 - w // 2, 66, w, 44, "background: #000000; text-align: center",
                f'<span style="display: block; font-size: 16px; line-height: 20px; padding-top: 2px; color: {ORANGE}">'
                f'{l1}<br>{l2}</span>')
    body = [B.bode_topbar(run="STOP", gen="1kHz"), B.grid_svg(a), instr, B.bode_cards(cards), B.bode_actions()]
    B.write_board("BodeFirst.dc.html", "Frequency response, before the first sweep", "\n".join(body))


if __name__ == "__main__":
    B.write_board("BodeV2.dc.html", "Frequency response, variant 2", v2_view(False))
    B.write_board("BodeV2Cursors.dc.html", "Frequency response, variant 2, cursors C1 and C2", v2_view(True))
    board_first_v1()
