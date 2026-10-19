"""Bode plot mode mockups (docs/plans/bode-ui.md). Reuses the UX makeover generator (common.py, boards.py) so the bars,
chips, cards and fold-outs are the same construction as the Scope and Spectrum artboards.

usage: python bode.py <out_dir>          writes the Bode artboards into <out_dir>

Geometry follows fw 0.2.66 bode_view.c: magnitude pane y 60..276 (6 x 36 px), phase pane y 292..408 (4 x 29 px), log
frequency axis x 6..726, decade labels at y 413, cards at y 446 (UI_MS_SPEC_Y), warning line top-middle.
"""
import math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "."
_argv = sys.argv[:]
sys.argv = [_argv[0], OUT_DIR, "__none__"]       # import boards.py for its helpers without writing its artboards
from common import *  # noqa
import boards  # noqa
sys.argv = _argv

vbtn, frow, foldout_panel, slide_switch, SVG0 = boards.vbtn, boards.frow, boards.foldout_panel, boards.slide_switch, boards.SVG0

PX, PW = 6, 720
MAG_Y, MAG_DIV = 60, 36
PH_Y, PH_DIV = 292, 29
MAG_H, PH_H = 6 * MAG_DIV, 4 * PH_DIV
FL_Y, CARDS_Y = 413, 446
PHASE = "#ff80ff"          # bode_view.c PHASE_COLOR
FLAG = "#707070"           # FLAG_COLOR: flagged points
NOTE = "#a0a0a0"           # NOTE_COLOR: labels
CURLINE = "#606060"        # CUR_COLOR: the point being measured
CURSOR = "#00aa11"         # CURSORS_COLOR (stock cursors)
NEUTRAL = "#a0a0a0"        # neutral card block (RBW in the spectrum)
AMBER = "#ffd262"


def write_board(name, title, body):
    with open(os.path.join(OUT_DIR, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(page(title, body))
    print("wrote", name)


# ---------------------------------------------------------------- top bar in the Bode mode
def bode_topbar(run="STOP", gen="1kHz", ch1="1V/div", ch2="1V/div", menu_active=False):
    """common.topbar with the mode's values: the run state chip (fw 0.2.68: the STATE, RUN green or STOP red; a single
    sweep is RUN while it runs), the ACQ chip saying the sweep sets the rate, the generator chip showing the tone the
    sweep drives"""
    s = topbar(rate="Set by", tb="the sweep", ch1_vdiv=ch1, ch2_vdiv=ch2, menu_active=menu_active,
               acq_href=None, menu_href="MainMenu.dc.html")
    old_run = box(127, 22, 38, 18, f"background: {GREEN}; border-radius: 2px", text(6, 1, "RUN", 13, "#000000"))
    assert s.count(old_run) == 1
    if run == "STOP":
        new_run = box(127, 22, 38, 18, f"background: {RED}; border-radius: 2px", text(3, 1, "STOP", 13, "#ffffff"))
    else:
        new_run = old_run
    s = s.replace(old_run, new_run)
    old_gen = text(7, 18, "1kHz", 13)
    assert s.count(old_gen) == 1
    return s.replace(old_gen, text(4, 18, gen, 13))


# ---------------------------------------------------------------- right bar, first page
def run_button(running):
    """fw 0.2.68 (owner exception, ui-guidelines 3): the button shows the ACTION a tap does, green RUN while stopped,
    red STOP while running"""
    if running:
        return abtn(739, 65, 51, 50, centred([("STOP", 16, "#ffffff")]),
                    f"background: {RED}; border: 1px solid {EDGE}; border-radius: 2px")
    return abtn(739, 65, 51, 50, centred([("RUN", 16, "#000000")]),
                f"background: {GREEN}; border: 1px solid {EDGE}; border-radius: 2px")


def bode_actions(auto_running=False, running=False):
    """the fixed meanings: CTRL, RUN/STOP, AUTO SET (the mode's own auto), T CURSOR (the frequency cursor), V CURSOR
    empty (no level cursor in this mode), MEASURES (the mode's list), SAVE PIC, SAVE WAVE (the sweep table)"""
    o = [box(730, 0, 70, 480, "background: #000000"),
         rbtn(5, [("CTRL", 16)], href="BodeCtrl.dc.html"),
         run_button(running or auto_running)]
    if auto_running:
        o.append(rbtn(125, [("Auto...", 13)], kind="active"))
    else:
        o.append(rbtn(125, [("AUTO", 16), ("SET", 16)], href="BodeAuto.dc.html"))
    o += [rbtn(185, [("T CU", 16), ("RSOR", 16)]),
          rbtn(305, [("MEAS", 16), ("URES", 16)], href="BodeMeasures.dc.html"),
          rbtn(365, [("SAVE", 16), ("PIC", 16)]),
          rbtn(425, [("SAVE", 16), ("WAVE", 16)])]
    return "\n".join(o)


# ---------------------------------------------------------------- right bar, CTRL page
def bode_ctrl(active=None, display_val="Mag+Ph", sweep_val="100 pt", thru_val="On"):
    """the guideline order: CTRL, Display, the mode's analysis controls (Sweep, Thru), Grid; the stepper slots and
    50 % TRIG stay empty (the sweep ranges both channels; there is no trigger level to set)"""
    k = lambda n: "active" if active == n else "normal"
    back = lambda n, board: "BodeCtrl.dc.html" if active == n else board
    return "\n".join([
        box(730, 0, 70, 480, "background: #000000"),
        rbtn(5, [("CTRL", 16)], kind="active", href="Main.dc.html"),
        vbtn(65, ["Display"], display_val, k("display"), href=back("display", "BodeCtrlDisplay.dc.html")),
        vbtn(125, ["Sweep"], sweep_val, k("sweep"), href=back("sweep", "BodeCtrlSweep.dc.html")),
        vbtn(185, ["Thru"], thru_val, k("thru"), href=back("thru", "BodeCtrlThru.dc.html")),
        vbtn(245, ["Grid"], "on", k("grid")),
    ])


# ---------------------------------------------------------------- data: a parallel-tank band-pass and its sweep
F0, Q, K = 50300.0, 8.1, 0.87


def h_bp(f):
    """second-order band-pass K (s w0 / Q) / (s^2 + s w0 / Q + w0^2): |H| in dB, phase in degrees"""
    x = f / F0
    re, im = 1 - x * x, x / Q
    mag = K * (x / Q) / math.hypot(re, im)
    ph = 90.0 - math.degrees(math.atan2(im, re))
    return 20 * math.log10(mag), ph


def log_points(f0, f1, n):
    return [f0 * (f1 / f0) ** (i / (n - 1)) for i in range(n)]


def fmt_f(f):
    """four significant digits, as ui_fmt_mhz"""
    for div, unit in [(1e6, "MHz"), (1e3, "kHz"), (1.0, "Hz")]:
        if f >= div:
            v = f / div
            dec = 3 if v < 10 else 2 if v < 100 else 1 if v < 1000 else 0
            return f"{v:.{dec}f}{unit}"
    return f"{f:.3f}Hz"


class Axes:
    def __init__(self, f0, f1, top=0.0, div=10.0):
        self.l0, self.l1, self.top, self.div = math.log10(f0), math.log10(f1), top, div
        self.f0, self.f1 = f0, f1

    def x(self, f):
        return PX + (math.log10(f) - self.l0) / (self.l1 - self.l0) * PW

    def ym(self, db):
        return MAG_Y + (self.top - db) / self.div * MAG_DIV

    def yp(self, deg):
        return PH_Y + (180.0 - deg) / 360.0 * PH_H


def axes_for(f0, f1, pts):
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


def freq_label(f):
    if f >= 1e6:
        return f"{f / 1e6:g}M"
    if f >= 1e3:
        return f"{f / 1e3:g}k"
    return f"{f:g}"


def grid_svg(a, grid=True):
    o = []
    dots = lambda x1, y1, x2, y2, gap: (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{LG1}" '
                                        f'stroke-width="1" stroke-dasharray="1 {gap}"></line>')
    if grid:
        for i in range(7):
            y = MAG_Y + i * MAG_DIV + 0.5
            o.append(dots(PX, y, PX + PW, y, 3))
        for i in range(5):
            y = PH_Y + i * PH_DIV + 0.5
            o.append(dots(PX, y, PX + PW, y, 3))
    labels = []
    dec = 1.0
    while dec <= 1e7:
        for k, m in enumerate((1, 2, 5)):
            f = dec * m
            if f < a.f0 * 0.999 or f > a.f1 * 1.001:
                continue
            x = round(a.x(f)) + 0.5
            if grid:
                o.append(dots(x, MAG_Y, x, MAG_Y + MAG_H, 7 if k else 2))
                o.append(dots(x, PH_Y, x, PH_Y + PH_H, 7 if k else 2))
            if k == 0:
                labels.append((x, freq_label(f)))
        dec *= 10
    o.append(f'<line x1="{PX}" y1="284.5" x2="{PX + PW}" y2="284.5" stroke="{GREY}" stroke-width="1"></line>')
    html = [f'<svg width="730" height="480" viewBox="0 0 730 480" aria-hidden="true" '
            f'style="position: absolute; left: 0; top: 0">{"".join(o)}</svg>']
    for x, t in labels:
        w = len(t) * 6.7
        lx = min(max(x - w / 2, PX), PX + PW - w)
        html.append(text(round(lx), FL_Y, t, 13, NOTE))
    for i in range(6):
        v = a.top - i * a.div
        html.append(text(PX + 3, MAG_Y + i * MAG_DIV + 2, f"{v:g}" + (" dB" if i == 0 else ""), 13, NOTE))
    for i in range(4):
        html.append(text(PX + 3, PH_Y + i * PH_DIV + 2, f"{180 - 90 * i}" + (" deg" if i == 0 else ""), 13, NOTE))
    return "\n".join(html)


def traces_svg(a, pts, current=None, markers=(), cursor=None):
    """pts: (f, dB, deg, ok); valid points joined by 1 px lines with 3 x 3 dots, flagged points grey without lines;
    markers: (x, y) white triangles pointing down; current: the frequency being measured (dotted grey line);
    cursor: index of the cursor's point"""
    o = []
    for f, db, ph, ok in pts:
        if not ok:
            x = a.x(f)
            o.append(f'<rect x="{x - 1:.1f}" y="{a.ym(db) - 1:.1f}" width="3" height="3" fill="{FLAG}"></rect>')
            o.append(f'<rect x="{x - 1:.1f}" y="{a.yp(ph) - 1:.1f}" width="3" height="3" fill="{FLAG}"></rect>')
    for colour, fy in [(CH2, lambda p: a.ym(p[1])), (PHASE, lambda p: a.yp(p[2]))]:
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
            if p[3]:
                o.append(f'<rect x="{a.x(p[0]) - 1:.1f}" y="{fy(p) - 1:.1f}" width="3" height="3" fill="{colour}"></rect>')
    for x, y in markers:
        o.append(f'<polygon points="{x - 4.5:.1f},{y - 8:.1f} {x + 4.5:.1f},{y - 8:.1f} {x:.1f},{y - 3:.1f}" '
                 'fill="#ffffff"></polygon>')
    if current is not None:
        x = round(a.x(current)) + 0.5
        o.append(f'<line x1="{x}" y1="{MAG_Y}" x2="{x}" y2="{PH_Y + PH_H}" stroke="{CURLINE}" stroke-width="1" '
                 'stroke-dasharray="1 1"></line>')
    if cursor is not None:
        f, db, ph, _ = pts[cursor]
        x = round(a.x(f)) + 0.5
        o.append(f'<line x1="{x}" y1="{MAG_Y}" x2="{x}" y2="{PH_Y + PH_H}" stroke="{CURSOR}" stroke-width="1" '
                 'stroke-dasharray="1 2"></line>')
        # the handle, draggable (ui-guidelines 3): above the magnitude pane, like the trigger position pointer
        o.append(f'<polygon points="{x - 7:.1f},49 {x + 7:.1f},49 {x:.1f},58" fill="{CURSOR}"></polygon>')
        for y in (a.ym(db), a.yp(ph)):
            o.append(f'<rect x="{x - 3.5:.1f}" y="{y - 3.5:.1f}" width="7" height="7" fill="none" stroke="#ffffff" '
                     'stroke-width="1"></rect>')
    return (f'<svg width="730" height="480" viewBox="0 0 730 480" aria-hidden="true" '
            f'style="position: absolute; left: 0; top: 0">{"".join(o)}</svg>')


def cursor_box(f, db, ph, flag=None):
    """the cursor readout where the scope draws its cursor readout (a grey box at the plot's top left), inside the top
    division that the axis rule keeps free of data: x 46..186, y 61..95, two 13 px lines: f (and the point's flag in
    grey when it has one), then |H| in the magnitude colour and the phase in the phase colour"""
    l1 = f"f {fmt_f(f)}" + (f' <span style="color: {NOTE}">{flag}</span>' if flag else "")
    inner = (text(6, 1, l1, 13) + text(6, 17, f"{db:+.2f}dB", 13, CH2) + text(72, 17, f"{ph:+.1f}deg", 13, PHASE))
    return box(46, 61, 140, 34, f"background: {GREY}; border-radius: 2px", inner)


def bode_cards(items):
    """cards in the makeover scheme (118 x 22, block in the colour of what the value belongs to, as wide as the name,
    the grey card drawn behind it), one row at y 446 from x 5 at a 120 px pitch, six at most ("+N" in the sixth
    place when more are chosen, as ui_ms_fit)"""
    o = []
    for i, (name, val, block, vcol) in enumerate(items[:6]):
        inner = (f'<span style="display: flex; align-items: center; height: 100%">'
                 f'<span style="display: inline-flex; align-items: center; height: 100%; padding: 0 3px; '
                 f'background: {block}; border-radius: 3px 0 0 3px; font-size: 13px; color: #000000; '
                 f'white-space: nowrap">{name}</span>'
                 f'<span style="margin-left: 6px; font-size: 13px; color: {vcol}; white-space: nowrap">{val}</span></span>')
        o.append(box(5 + 120 * i, CARDS_Y, 118, 22, f"background: {LIGHTGREY}; border-radius: 3px", inner))
    return "\n".join(o)


def warn(msg):
    assert len(msg) <= 50, msg          # the cap that keeps the warning clear of the cursor readout
    w = int(len(msg) * 7.1) + 24
    return warning_box(msg, cx=366, y=70, w=w)


# ---------------------------------------------------------------- the tank sweep (set by hand: 5 kHz .. 500 kHz, 100 points, Settle Normal)
SPAN = (5e3, 5e5)
FREQS = log_points(SPAN[0], SPAN[1], 100)
UNSETTLED = {48, 50}          # two points near the peak failed the half-record check (Settle Normal covers Q <= 7)


def sweep_points(upto=None):
    pts = []
    for i, f in enumerate(FREQS):
        if upto is not None and i >= upto:
            break
        db, ph = h_bp(f)
        ok = i not in UNSETTLED
        if not ok:
            db += 0.6
            ph += 4.0
        pts.append((f, db, ph, ok))
    return pts


def tank_markers(a):
    ref = 20 * math.log10(K)
    bw = F0 / Q
    fl = -bw / 2 + math.sqrt(bw * bw / 4 + F0 * F0)
    fh = fl + bw
    m = [(a.x(fl), a.ym(ref - 3)), (a.x(fh), a.ym(ref - 3)), (a.x(F0), a.ym(ref)),
         (a.x(fl), a.yp(45)), (a.x(fh), a.yp(-45))]
    return m, ref, fl, fh


TANK_AXES = axes_for(SPAN[0], SPAN[1], [(f, *h_bp(f), True) for f in FREQS])
# card colours: magnitude results in the magnitude trace's colour, phase results in the phase colour, the sweep's own
# state on a neutral block; state words coloured (green verified, amber attention, grey not available)
CARD_DEFAULTS_DONE = lambda ref, fl: [
    ("Level", f"{ref:+.2f}dB", CH2, "#ffffff"), ("-3dB", fmt_f(fl), CH2, "#ffffff"),
    ("Peak", fmt_f(F0), CH2, "#ffffff"), ("Q", f"{Q:.1f}", CH2, "#ffffff"),
    ("Sweep", "100/100", NEUTRAL, "#ffffff"), ("Thru", "applied", NEUTRAL, GREEN)]


# ================================================================ Main: the action page with a finished sweep
def board_main():
    a = TANK_AXES
    pts = sweep_points()
    m, ref, fl, fh = tank_markers(a)
    cur = 62
    body = [bode_topbar(run="STOP", gen="50.3kHz", ch1="1V/div", ch2="200mV/div"),
            grid_svg(a),
            traces_svg(a, pts, markers=m, cursor=cur),
            cursor_box(pts[cur][0], pts[cur][1], pts[cur][2]),
            warn("2 points not settled: choose a longer Settle"),
            bode_cards(CARD_DEFAULTS_DONE(ref, fl)),
            bode_actions()]
    write_board("Main.dc.html", "Frequency response", "\n".join(body))


# ================================================================ BodeFirst: before the first sweep
def wiring_panel():
    """the connection text with a small drawing, over the empty panes (bode_view.c draw_connection)"""
    x0, y0, w, h = 66, 92, 600, 300
    s = []
    ln = lambda x1, y1, x2, y2, c="#ffffff", wd=2: (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{c}" '
                                                   f'stroke-width="{wd}"></line>')
    # post -> 1 kOhm (inside the scope) -> DUT input node -> DUT -> output node
    s.append(f'<circle cx="70" cy="70" r="7" fill="none" stroke="#ffffff" stroke-width="2"></circle>')
    s.append(ln(77, 70, 230, 70))
    s.append(f'<rect x="230" y="40" width="140" height="80" rx="3" fill="none" stroke="#ffffff" stroke-width="2"></rect>')
    s.append(ln(370, 70, 520, 70))
    s.append(f'<circle cx="170" cy="70" r="4" fill="{CH1}"></circle>')
    s.append(ln(170, 70, 170, 104, CH1))
    s.append(f'<circle cx="460" cy="70" r="4" fill="{CH2}"></circle>')
    s.append(ln(460, 70, 460, 104, CH2))
    # DUT ground to the probe ground clips
    s.append(ln(300, 120, 300, 146))
    s.append(ln(284, 146, 316, 146))
    s.append(ln(289, 151, 311, 151))
    s.append(ln(294, 156, 306, 156))
    svg = (f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" aria-hidden="true" '
           f'style="position: absolute; left: 0; top: 0">{"".join(s)}</svg>')
    parts = [svg,
             text(30, 82, "Calibration post", 13), text(30, 98, "3.1 Vpp, 1 kOhm", 13, NOTE),
             text(286, 66, "DUT", 24),
             text(140, 108, "CH1 tip", 13, CH1), text(432, 108, "CH2 tip", 13, CH2),
             text(322, 140, "Probe ground clips", 13, NOTE),
             text(30, 186, "Calibration post to the DUT input. CH1 tip on the DUT input,", 16),
             text(30, 208, "CH2 tip on the DUT output, DUT ground on both probe ground clips.", 16),
             text(30, 230, "Passive DUTs only: nothing that can drive the post.", 16),
             text(30, 264, "RUN sweeps continuously. AUTO SET finds the span and sweeps.", 13, NOTE),
             text(30, 280, "CTRL, Sweep: Start, Stop, Points, Single sweep.", 13, NOTE)]
    return box(x0, y0, w, h, f"background: #000000; border: 1px solid {LIGHTGREY}; border-radius: 3px", "".join(parts))


def board_first():
    a = Axes(100, 1e6)
    cards = [("Level", "no sweep", CH2, NOTE), ("-3dB", "no sweep", CH2, NOTE), ("Peak", "no sweep", CH2, NOTE),
             ("Q", "no sweep", CH2, NOTE), ("Sweep", "not run", NEUTRAL, NOTE), ("Thru", "none", NEUTRAL, NOTE)]
    body = [bode_topbar(run="STOP", gen="1kHz"),
            grid_svg(a),
            wiring_panel(),
            warn("Trigger was EXT (the generator's pin): now CH1"),
            bode_cards(cards),
            bode_actions()]
    write_board("BodeFirst.dc.html", "Frequency response, before the first sweep", "\n".join(body))


# ================================================================ BodeSingle: a single sweep in progress
def board_single():
    n = 37
    pts = sweep_points(upto=n)
    a = axes_for(SPAN[0], SPAN[1], pts)
    sweeping = [("Level", "sweeping", CH2, NOTE), ("-3dB", "sweeping", CH2, NOTE), ("Peak", "sweeping", CH2, NOTE),
                ("Q", "sweeping", CH2, NOTE), ("Sweep", f"{n}/100", NEUTRAL, "#ffffff"),
                ("Thru", "applied", NEUTRAL, GREEN)]
    body = [bode_topbar(run="RUN", gen=fmt_f(FREQS[n]), ch1="1V/div", ch2="200mV/div"),
            grid_svg(a),
            traces_svg(a, pts, current=FREQS[n]),
            bode_cards(sweeping),
            bode_actions(running=True)]
    write_board("BodeSingle.dc.html", "Frequency response, single sweep running", "\n".join(body))


# ================================================================ BodeAuto: AUTO SET's overview sweep
def board_auto():
    fr = log_points(100, 1e7, 20)
    n = 12
    pts = []
    for i, f in enumerate(fr[:n]):
        db, ph = h_bp(f)
        pts.append((f, db, ph, db > -58))
    a = axes_for(100, 1e7, pts)
    cards = [("Level", "sweeping", CH2, NOTE), ("-3dB", "sweeping", CH2, NOTE), ("Peak", "sweeping", CH2, NOTE),
             ("Q", "sweeping", CH2, NOTE), ("Sweep", f"auto {n}/20", NEUTRAL, "#ffffff"),
             ("Thru", "applied", NEUTRAL, GREEN)]
    body = [bode_topbar(run="RUN", gen=fmt_f(fr[n]), ch1="1V/div", ch2="200mV/div"),
            grid_svg(a),
            traces_svg(a, pts, current=fr[n]),
            bode_cards(cards),
            bode_actions(auto_running=True)]
    write_board("BodeAuto.dc.html", "Frequency response, AUTO SET running", "\n".join(body))


# ================================================================ CTRL page and its fold-outs
def result_view(right):
    a = TANK_AXES
    pts = sweep_points()
    m, ref, fl, fh = tank_markers(a)
    return "\n".join([bode_topbar(run="STOP", gen="50.3kHz", ch1="1V/div", ch2="200mV/div"), grid_svg(a),
                      traces_svg(a, pts, markers=m), warn("2 points not settled: choose a longer Settle"),
                      bode_cards(CARD_DEFAULTS_DONE(ref, fl)), right])


def board_ctrl():
    write_board("BodeCtrl.dc.html", "Frequency response controls", result_view(bode_ctrl()))


ICON_BOTH = (SVG0 + '<polyline points="2,3 12,3 17,4 21,6 26,9" fill="none" stroke="currentColor" stroke-width="2"></polyline>'
             '<path d="M2,12 C12,12 14,18 26,18" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 2"></path></svg>')
ICON_MAG = (SVG0 + '<polyline points="2,4 13,4 18,6 22,10 26,17" fill="none" stroke="currentColor" stroke-width="2"></polyline></svg>')
ICON_PHASE = (SVG0 + '<path d="M2,4 C13,4 15,16 26,16" fill="none" stroke="currentColor" stroke-width="2" '
              'stroke-dasharray="2 2"></path></svg>')


def board_ctrl_display():
    px, py, pw = 522, 60, 206
    rows = [frow(8, 30, pw - 16, "Mag + phase", sel=True, icon=ICON_BOTH),
            frow(8, 80, pw - 16, "Magnitude", icon=ICON_MAG),
            frow(8, 130, pw - 16, "Phase", icon=ICON_PHASE)]
    body = result_view(bode_ctrl(active="display")) + "\n" + foldout_panel(px, py, pw, 182, "Display", "".join(rows))
    write_board("BodeCtrlDisplay.dc.html", "Frequency response controls, Display", body)


def twoway(x, y, w, label, value):
    """a - / + row (bode_view.c ROW_VALUE2): the left half steps down, the right half up; both ends drawn as grey
    30 x 36 keys"""
    inner = (box(4, 4, 30, 36, f"background: {GREY}; border-radius: 2px", centred([("-", 16, "#ffffff")])) +
             box(w - 34, 4, 30, 36, f"background: {GREY}; border-radius: 2px", centred([("+", 16, "#ffffff")])) +
             text(40, 13, label, 16) +
             f'<div style="position: absolute; right: 40px; top: 13px; font-size: 16px; line-height: 18px; '
             f'color: {PALEYEL}; white-space: nowrap">{value}</div>')
    return abtn(x, y, w, 44, inner, f"background: {LIGHTGREY}; border: none; border-radius: 3px; text-align: left")


def action_row(x, y, w, label, sub=None, href=None):
    lines = [(label, 16, "#ffffff")] + ([(sub, 13, NOTE)] if sub else [])
    return abtn(x, y, w, 44, centred(lines), f"background: {LIGHTGREY}; border: none; border-radius: 3px", href=href)


def board_ctrl_sweep():
    px, py, pw = 522, 120, 206
    rw = pw - 16
    rows = [twoway(8, 30, rw, "Start", "5kHz"),
            twoway(8, 80, rw, "Stop", "500kHz"),
            frow(8, 130, rw, "Points", "100"),
            frow(8, 180, rw, "Settle", "Normal"),
            frow(8, 230, rw, "CH2 range", "Auto"),
            box(8, 283, rw, 1, f"background: {GREY}"),
            action_row(8, 292, rw, "Single sweep", "about 3 s", href="BodeSingle.dc.html")]
    body = result_view(bode_ctrl(active="sweep")) + "\n" + foldout_panel(px, py, pw, 344, "Sweep", "".join(rows))
    write_board("BodeCtrlSweep.dc.html", "Frequency response controls, Sweep", body)


def switch_row2(x, y, w, label, sub, on):
    inner = (text(10, 5, label, 16) + text(10, 25, sub, 13, NOTE) +
             f'<span style="position: absolute; right: 10px; top: 11px">{slide_switch(on)}</span>')
    return abtn(x, y, w, 44, inner, f"background: {LIGHTGREY}; border: none; border-radius: 3px; text-align: left")


def board_ctrl_thru():
    px, py, pw = 492, 180, 236
    rw = pw - 16
    rows = [action_row(8, 30, rw, "Measure thru"),
            switch_row2(8, 80, rw, "Apply", "matches this sweep", True),
            action_row(8, 130, rw, "Clear")]
    body = (result_view(bode_ctrl(active="thru")) + "\n" +
            foldout_panel(px, py, pw, 182, "Thru: both tips on the input", "".join(rows)))
    write_board("BodeCtrlThru.dc.html", "Frequency response controls, Thru", body)


# ================================================================ MEASURES: the Bode list (ui_meas.h menu geometry)
def board_measures():
    X, W, TITLE, ROW = 232, 499, 28, 44
    items = [["Level", "-3dB", "BW", "Peak"], ["Q", "Ph45", "Ph90", "Ph180"], ["Sweep", "Thru", None, None]]
    chosen = {"Level": 1, "-3dB": 2, "Peak": 3, "Q": 4, "Sweep": 5, "Thru": 6}
    rows = 4
    h = 1 + TITLE + 1 + (ROW + 1) * rows
    top = 478 - h
    o = [box(0, 0, W, h, "background: #000000"),
         box(1, 1, W - 2, TITLE, f"background: {DARKGREY}",
             f'<div style="text-align: center; font-size: 16px; line-height: 18px; padding-top: 6px; color: #ffffff">'
             'Frequency response measurements</div>')]
    y = 1 + TITLE + 1
    half = (W - 2 - 1) // 2
    for i, lab in enumerate(["Select All", "Clear"]):
        o.append(abtn(1 + i * (half + 1), y, half if i == 0 else W - 2 - half - 1, ROW,
                      centred([(lab, 13, "#000000")]), f"background: {NEUTRAL}; border: none"))
    y += ROW + 1
    cw = (W - 2 - 3) // 4
    for r in items:
        for c, name in enumerate(r):
            x = 1 + c * (cw + 1)
            w = cw if c < 3 else W - 2 - 3 * (cw + 1)
            if name is None:
                o.append(box(x, y, w, ROW, f"background: {DARKGREY}"))
                continue
            rank = chosen.get(name)
            inner = centred([(name, 16, "#ffffff" if rank else GREY)])
            if rank:
                inner += text(4, 2, str(rank), 13, LG2)
            o.append(abtn(x, y, w, ROW, inner, f"background: {DARKGREY}; border: none"))
        y += ROW + 1
    o.append(box(0, 0, W, h, "border: 1px solid #ffffff; pointer-events: none"))
    menu = box(X, top, W, h, "", "".join(o))
    write_board("BodeMeasures.dc.html", "Frequency response measurements", result_view(bode_actions()) + "\n" + menu)


# ================================================================ MainMenu: eight rows at a 52 px pitch (fw 0.2.68)
def mm_icon(key):
    """menu.c's line icons at (MAIN_MENU_XPOS + 15, button top), 1 px; the bitmap icons (pictures, waveforms, the
    settings spanner, diagnostics, USB) as stand-ins of the same size"""
    w = 'stroke="#ffffff" stroke-width="1" fill="none"'
    if key == "Scope":
        sine = [19, 16, 14, 12, 10, 9, 9, 9, 10, 12, 14, 16, 19, 21, 23, 25, 27, 28, 28, 28, 27, 25, 23, 21, 19]
        return ('<line x1="0" y1="19.5" x2="25" y2="19.5" stroke="#707070" stroke-width="1"></line>'
                '<line x1="12.5" y1="8" x2="12.5" y2="30" stroke="#707070" stroke-width="1"></line>'
                f'<polyline points="{" ".join(f"{i + 0.5},{v + 0.5}" for i, v in enumerate(sine))}" {w}></polyline>')
    if key == "Spectrum":
        bars = [4, 7, 22, 9, 5, 14, 4, 6, 10, 3, 5, 2]
        return (f'<line x1="0" y1="30.5" x2="26" y2="30.5" {w}></line>' +
                "".join(f'<line x1="{1.5 + 2 * i}" y1="{29 - b}" x2="{1.5 + 2 * i}" y2="30" {w}></line>'
                        for i, b in enumerate(bars)))
    if key == "Bode":   # scope_main_menu_bode_icon: magnitude above phase, flat then falling
        return (f'<polyline points="0.5,8.5 10.5,8.5 24.5,22.5" {w}></polyline>'
                f'<polyline points="0.5,15.5 5.5,15.5 19.5,29.5 24.5,29.5" {w}></polyline>')
    if key == "Pictures":
        return (f'<rect x="0.5" y="6.5" width="23" height="18" {w}></rect>'
                f'<polyline points="2,23 8,15 12,20 15,17 22,23" {w}></polyline>'
                '<circle cx="18" cy="11" r="2" fill="#ffffff"></circle>')
    if key == "Waveforms":
        return (f'<path d="M2.5,6.5 L16.5,6.5 L22.5,12.5 L22.5,29.5 L2.5,29.5 Z" {w}></path>'
                f'<polyline points="5,21 8,21 8,15 12,15 12,24 15,24 15,18 19,18" {w}></polyline>')
    if key == "Settings":   # Other_settings_icon, a spanner (fw 0.2.68)
        return ('<path d="M18,7 a5,5 0 1,0 3,8 l-3,-1 l-1,-3 l2,-2 Z" fill="#ffffff"></path>'
                '<line x1="17" y1="15" x2="5" y2="27" stroke="#ffffff" stroke-width="3" stroke-linecap="round"></line>')
    if key == "Diagnostics":
        return (f'<rect x="0.5" y="5.5" width="23" height="22" rx="2" {w}></rect>'
                f'<polyline points="3,17 8,17 10,11 13,23 16,14 18,17 21,17" {w}></polyline>')
    return (f'<line x1="11.5" y1="5" x2="11.5" y2="29" {w}></line><polyline points="11.5,23 4.5,16 4.5,12" {w}></polyline>'
            f'<polyline points="11.5,19 18.5,13 18.5,10" {w}></polyline><circle cx="11.5" cy="30" r="2" fill="#ffffff"></circle>'
            '<polygon points="8.5,6 14.5,6 11.5,1" fill="#ffffff"></polygon>')


MM_ROWS = [("Scope", ["Scope"], None), ("Spectrum", ["Spectrum", "analyser"], None),
           ("Bode", ["Frequency", "response"], "Main.dc.html"), ("Pictures", ["Saved", "pictures"], None),
           ("Waveforms", ["Saved", "waveforms"], None), ("Settings", ["Settings"], None),
           ("Diagnostics", ["Diagnostics"], None), ("USB", ["USB drive"], None)]


def board_main_menu():
    """ui_menu.h fw 0.2.68: the column at x 2..148 from y 46, 52 px per row (y 46..461); the button 131 x 35 at x 9,
    9 px below the row top; black separators; the row's hit area is the whole 52 px; the icon at x 17, the label centred
    at x 95 (two lines at +1 and +17, one at +9)"""
    pitch, top, n = 52, 46, len(MM_ROWS)
    o = [box(2, top, 147, n * pitch, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    for i, (key, label, href) in enumerate(MM_ROWS):
        y = top + i * pitch
        by = 9
        icon = (f'<svg width="26" height="32" viewBox="0 0 26 32" aria-hidden="true" '
                f'style="position: absolute; left: 15px; top: {by}px">{mm_icon(key)}</svg>')
        lines = "".join(text(93 - round(len(t) * 7.1 / 2), by + (1 + 16 * j if len(label) == 2 else 9), t, 16)
                        for j, t in enumerate(label))
        o.append(abtn(2, y, 147, pitch, icon + lines,
                      "background: transparent; border: none; text-align: left", href=href))
        if i:
            o.append(box(9, y - 1, 132, 1, "background: #000000"))
    bg = scope_view(menu_active=True)
    old = rbtn(65, [("RUN/", 16), ("STOP", 16)], kind="run")
    assert bg.count(old) == 1
    bg = bg.replace(old, run_button(True))      # fw 0.2.68: the button shows the action, STOP while running
    write_board("MainMenu.dc.html", "Main menu with the Frequency response row", bg + "\n" + "\n".join(o))


if __name__ == "__main__":
    for fn in [board_main, board_main_menu, board_first, board_ctrl, board_ctrl_display, board_ctrl_sweep,
               board_ctrl_thru, board_single, board_auto, board_measures]:
        fn()
