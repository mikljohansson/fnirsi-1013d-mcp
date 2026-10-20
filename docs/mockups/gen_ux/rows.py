"""The two UI rows of docs/plans/ui-rows.md: the trigger menu's Type row (Edge, Pulse width, Nth edge with their
parameters), the ACQ menu's Sample row (Normal, Peak), the top bar trigger chip for the two new types, a peak-detect trace
and a pulse-width trigger in use. Built on the UX makeover generator (common.py, boards.py) so the bars, chips, cards,
menus and fold-out rows are the same construction as the makeover artboards.

usage: python rows.py <out_dir> [board]     writes the artboards (all, or the named one) into <out_dir>
       python render.py <out_dir>           turns them into PNGs (headless Chrome or Edge)

Geometry follows today's firmware: the trigger menu at TRIGGER_MENU_XPOS 508, y 48, 220 wide (menu.c
scope_open_trigger_menu, rows at a 56 px pitch with 22 px boxes), the ACQ menu at 250, 48 (scope_open_acquisition_menu:
Roll / Average / Filter boxes 90 x 40 at y + 163, the Memory column right of the line at x + 304), the trigger chip at
546, 5 (scope_trigger_settings: 31 px block, 80 px box, mode text at x + 36, channel box at x + 35 / y + 19, edge symbol at
x + 66). Text widths below were checked against the bitmap fonts (font_2.c 13 px, font_3.c 16 px).
"""
import os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "."
_argv = sys.argv[:]
sys.argv = [_argv[0], OUT_DIR, "__none__"]       # import boards.py for its helpers without writing its artboards
from common import *  # noqa
import boards  # noqa
sys.argv = _argv

which = _argv[2] if len(_argv) > 2 else "all"


def want(n):
    return which in ("all", n)


def write_board(name, title, body):
    with open(os.path.join(OUT_DIR, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(page(title, body))
    print("wrote", name)


# stock colours the trigger menu uses (fnirsi_1013d_scope.h)
DARKGREEN = "#00bb00"; LDARKBLUE = "#6666ff"; LIGHTRED = "#ff4444"; CH1_TRIG = "#cccc00"; MAGENTA = "#ff00ff"
DIMTEXT = "#9a9a9a"            # a box whose setting is kept but not applied: grey text (the Settings screen's "not available")
NOTE = "#a0a0a0"


# ---------------------------------------------------------------- the trigger chip (top bar) with a type glyph
def glyph(kind, x, y, colour="#ffffff"):
    """14 x 14 symbol in the chip's glyph slot (scope_trigger_symbol draws 11 x 14 at x + 66, y + 19).
    rise / fall: today's edge symbol. pulse+ / pulse-: a positive or negative pulse, the type's symbol for Pulse width
    (the polarity is the edge row's). nth: a burst whose last edge is tall, the symbol for Nth edge."""
    paths = {
        "rise": '<polyline points="1,12 6,12 6,2 13,2"/>',
        "fall": '<polyline points="1,2 6,2 6,12 13,12"/>',
        "pulse+": '<polyline points="1,12 4,12 4,2 10,2 10,12 13,12"/>',
        "pulse-": '<polyline points="1,2 4,2 4,12 10,12 10,2 13,2"/>',
        "nth": '<polyline points="1,12 3,12 3,8 5,8 5,12 7,12 7,8 9,8 9,12 11,12 11,2 13,2"/>',
    }
    return (f'<svg width="14" height="14" viewBox="0 0 14 14" style="position: absolute; left: {x}px; top: {y}px" '
            f'fill="none" stroke="{colour}" stroke-width="1.5">{paths[kind]}</svg>')


def trig_chip(mode="Auto", chan="CH1", kind="rise", active=False):
    """the trigger chip redrawn over the top bar's: the mode on the first line as today, the channel box and the type
    glyph on the second. Measured in font_2: Auto 27, Single 34, Normal 40 px in the 44 px line, so the type cannot
    join the mode as a word; it takes the edge symbol's slot (ui-rows.md section 4)."""
    block = MAGENTA if active else TRIG
    tcol = "#ffffff" if active else "#000000"
    cbg = "#3333ff" if active else CH1_TRIG
    cfg = "#ffffff" if active else "#000000"
    inner = (box(0, 0, 80, 35, f"background: {LIGHTGREY}; border-radius: 3px") +
             box(0, 0, 31, 35, f"background: {block}; border-radius: 3px 0 0 3px", text(11, 8, "T", 16, tcol)) +
             text(36, 1, mode, 13) +
             box(35, 19, 28, 14, f"background: {cbg}; border-radius: 2px", text(2, -1, chan, 13, cfg)) +
             glyph(kind, 65, 19))
    return abtn(546, 5, 80, 35, inner, "background: transparent; border: none; text-align: left")


# ---------------------------------------------------------------- the trigger menu (menu.c scope_open_trigger_menu)
TX, TY, TW = 508, 48, 220


def mbox(x, y, w, label, sel, sel_colour, tx=None):
    """a 22 px choice box of the trigger menu: dark grey with a grey edge, the chosen one filled in its colour. The
    firmware places each text by hand (tx); the artboard centres it, since Arial runs wider than the bitmap font"""
    if sel:
        return box(x, y, w, 22, f"background: {sel_colour}; border-radius: 2px", centred([(label, 15, "#000000")]))
    return box(x, y, w, 22, f"background: {DARKGREY}; border: 1px solid {GREY}; border-radius: 2px",
               centred([(label, 15, "#ffffff")]))


def twoway(x, y, w, label, value):
    """the Frequency response Sweep fold-out's value row (bode.py twoway, bode_view.c ROW_VALUE2): a tap on the left
    half steps the value down, on the right half up; the ends are drawn as 30 x 36 keys"""
    inner = (box(4, 4, 30, 36, f"background: {GREY}; border-radius: 2px", centred([("-", 16, "#ffffff")])) +
             box(w - 34, 4, 30, 36, f"background: {GREY}; border-radius: 2px", centred([("+", 16, "#ffffff")])) +
             text(40, 13, label, 16) +
             f'<div style="position: absolute; right: 40px; top: 13px; font-size: 16px; line-height: 18px; '
             f'color: {PALEYEL}; white-space: nowrap">{value}</div>')
    return abtn(x, y, w, 44, inner, f"background: {LIGHTGREY}; border: none; border-radius: 3px; text-align: left")


def trigger_menu(ttype="Edge", mode="Auto", edge="Rising", chan="CH1", pos="50%", always50=False,
                 width="2us", longer=False, n="5", idle="50us"):
    h = 336 if ttype == "Edge" else 430
    p = [box(0, 0, TW, h, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    for ly in (54, 110, 166, 222, 278):
        p.append(box(10, ly, 200, 1, "background: #000000"))
    for y0, l1, l2 in [(10, "Trigger", "mode"), (66, "Trigger", "edge"), (122, "Trigger", "channel"),
                       (178, "Trigger", "position"), (234, "Always", "50%"), (290, "Trigger", "type")]:
        p.append(text(10, y0, l1, 16) + text(10, y0 + 19, l2, 16))
    # mode: Single (orange), Auto (green), Normal (dark green)
    p.append(mbox(65, 17, 47, "Single", mode == "Single", ORANGE, 68))
    p.append(mbox(116, 17, 36, "Auto", mode == "Auto", TRIG, 120))
    p.append(mbox(156, 17, 54, "Normal", mode == "Normal", DARKGREEN, 160))
    # edge: Rising (light red), Falling (light blue), the edge symbol between them
    p.append(mbox(70, 74, 55, "Rising", edge == "Rising", LIGHTRED, 78))
    p.append(mbox(150, 74, 55, "Falling", edge == "Falling", LDARKBLUE, 156))
    p.append(glyph("rise" if edge == "Rising" else "fall", 131, 78))
    # channel
    for i, (nm, col) in enumerate([("CH1", CH1_TRIG), ("CH2", "#00cccc"), ("EXT", ORANGE)]):
        p.append(mbox(70 + 51 * i, 129, 33, nm, chan == nm, col, 74 + 51 * i))
    # position
    for i, (nm, col) in enumerate([("25%", LDARKBLUE), ("50%", DARKGREEN), ("75%", LIGHTRED)]):
        p.append(mbox(70 + 51 * i, 185, 33, nm, pos == nm, col, 75 + 51 * i))
    # Always 50 %: the stock icon and slide switch
    p.append('<svg width="24" height="24" viewBox="0 0 24 24" style="position: absolute; left: 75px; top: 238px" '
             'fill="none" stroke="#ffffff" stroke-width="1.5"><polyline points="2,18 7,18 7,6 13,6 13,18 18,18 18,6 22,6"/>'
             '<line x1="1" y1="12" x2="23" y2="12" stroke-dasharray="2 2"/></svg>')
    p.append(box(140, 240, 45, 21, "", boards.slide_switch(always50)))
    # the Type row (new): one colour for the chosen type, the trigger's green
    p.append(mbox(65, 297, 44, "Edge", ttype == "Edge", TRIG, 71))
    p.append(mbox(113, 297, 50, "Pulse", ttype == "Pulse", TRIG, 120))
    p.append(mbox(167, 297, 40, "Nth", ttype == "Nth", TRIG, 176))
    # the type's parameters (new): 44 px fold-out rows under the Type row, the menu grows by 94 px
    if ttype == "Pulse":
        p.append(twoway(8, 334, 204, "Width", width))
        p.append(boards.frow(8, 380, 204, "Pulse", "longer" if longer else "shorter"))
    elif ttype == "Nth":
        p.append(twoway(8, 334, 204, "N", n))
        p.append(twoway(8, 380, 204, "Idle", idle))
    return box(TX, TY, TW, h, "", "".join(p))


def trigger_board(name, title, **kw):
    kind = {"Edge": "rise" if kw.get("edge", "Rising") == "Rising" else "fall",
            "Pulse": "pulse+" if kw.get("edge", "Rising") == "Rising" else "pulse-",
            "Nth": "nth"}[kw.get("ttype", "Edge")]
    body = (scope_view(warning=None) + "\n" +
            trig_chip(kw.get("mode", "Auto"), kw.get("chan", "CH1"), kind, active=True) + "\n" + trigger_menu(**kw))
    write_board(name, title, body)


if want("TrigMenuEdge"):
    trigger_board("TrigMenuEdge.dc.html", "Trigger menu, type Edge", ttype="Edge")
if want("TrigMenuPulse"):
    trigger_board("TrigMenuPulse.dc.html", "Trigger menu, type Pulse width", ttype="Pulse", mode="Normal",
                  width="2us", longer=False)
if want("TrigMenuNth"):
    trigger_board("TrigMenuNth.dc.html", "Trigger menu, type Nth edge", ttype="Nth", mode="Normal", n="5", idle="50us")
if want("TrigMenuNthArm"):
    trigger_board("TrigMenuNthArm.dc.html", "Trigger menu, type Nth edge counted from the arm", ttype="Nth",
                  mode="Normal", n="5", idle="from arm")


# ---------------------------------------------------------------- the ACQ menu with the Sample row
AX, AY, AW, AH = 250, 48, 410, 386     # 30 px wider than today (380): the Sample box stands in the Memory column


def acq_box(x, l1, l2, on, applied=True):
    """the Roll / Average / Filter / Sample construction: 90 x 40, green with black text when on, grey with white text
    when off; grey text while the setting is kept but not applied"""
    if on:
        bg, fg = GREEN, "#000000"
    else:
        bg, fg = LIGHTGREY, ("#ffffff" if applied else DIMTEXT)
    return abtn(x, 163, 90, 40, centred([(l1, 13, fg), (l2, 13, fg)]), f"background: {bg}; border: none; border-radius: 3px")


def acq_menu(peak=False, roll=False):
    speeds = ["200MSa/s", "100MSa/s", "50MSa/s", "20MSa/s", "10MSa/s", "5MSa/s", "2MSa/s", "1MSa/s",
              "500KSa/s", "200KSa/s", "100KSa/s", "50KSa/s", "20KSa/s", "10KSa/s", "5KSa/s", "2KSa/s",
              "1KSa/s", "500Sa/s"]
    tdivs = ["200ms/div", "100ms/div", "50ms/div", "20ms/div", "10ms/div", "5ms/div", "2ms/div", "1ms/div",
             "500us/div", "200us/div", "100us/div", "50us/div", "20us/div", "10us/div", "5us/div", "2us/div",
             "1us/div", "500ns/div", "200ns/div", "100ns/div", "50ns/div", "20ns/div", "10ns/div", "5ns/div"]
    p = [box(0, 0, AW, AH, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    p.append(text(111, 8, "Sample Rate", 16))
    for i, s in enumerate(speeds):
        sel = s == "500KSa/s"
        x, y = (i & 3) * 72 + 10, (i >> 2) * 23 + 33
        p.append(abtn(x, y, 68, 20, centred([(s, 13, "#000000" if sel else "#ffffff")]),
                      f"background: {GREEN if sel else MIDGREY}; border: none; border-radius: 2px"))
    p.append(box(8, 155, 288, 1, "background: #000000"))
    p.append(acq_box(8, "Roll", "mode", roll))
    p.append(acq_box(106, "Average", "mode", False, applied=not peak))
    p.append(acq_box(204, "Filter", "CIC2+FIR", not peak, applied=not peak))
    p.append(box(8, 211, AW - 16, 1, "background: #000000"))
    p.append(text(97, 216, "Time per Division", 16))
    for i, s in enumerate(tdivs):
        sel = s == "200us/div"
        x, y = (i & 3) * 72 + 10, (i >> 2) * 23 + 241
        p.append(abtn(x, y, 68, 20, centred([(s, 13, "#000000" if sel else "#ffffff")]),
                      f"background: {GREEN if sel else MIDGREY}; border: none; border-radius: 2px"))
    p.append(box(304, 8, 1, AH - 16, "background: #000000"))
    p.append(text(317, 8, "Memory", 16))
    for i, s in enumerate(["3000", "6000"]):
        sel = i == 0
        p.append(abtn(315, 33 + i * 36, 54, 25, centred([(s, 13, "#000000" if sel else "#ffffff")]),
                      f"background: {GREEN if sel else MIDGREY}; border: none; border-radius: 2px"))
    p.append(text(310, 104, "pass 15 ms", 13, "#ffffff"))
    p.append(text(310, 126, "Filter or Peak:", 13, LG2))
    p.append(text(310, 144, "up to 6000", 13, LG2))
    # the Sample row (new): the fourth box of the y 163 row, in the Memory column
    p.append(acq_box(310, "Sample", "Peak" if peak else "Normal", peak))
    if peak:
        p.append(text(310, 216, "Peak: Filter", 13, LG2))
        p.append(text(310, 234, "and Average", 13, LG2))
        p.append(text(310, 252, "not applied", 13, LG2))
    return box(AX, AY, AW, AH, "", "".join(p))


if want("AcqMenuSample"):
    write_board("AcqMenuSample.dc.html", "Acquisition menu, Sample Normal",
                scope_view(warning=None, acq_active=True) + "\n" + acq_menu())
if want("AcqMenuPeak"):
    write_board("AcqMenuPeak.dc.html", "Acquisition menu, Sample Peak",
                scope_view(warning=None, acq_active=True) + "\n" + acq_menu(peak=True))


# ---------------------------------------------------------------- the Scope view with a peak-detect trace
def peak_bars(zero_y, hi_v, lo_v, px_per_v, colour, spikes=(), seed=3, band=2.0):
    """one vertical bar per column from the window's min to its max: the 1 kHz square of the Scope boards with a noise
    band, and at the spike columns a bar reaching the spike's level (what sampling at 500 kSa/s would miss)"""
    rnd = random.Random(seed)
    hi = max(zero_y - hi_v * px_per_v, 1.0)
    lo = zero_y - lo_v * px_per_v
    level = {}
    cur, k = lo, 0
    edges = list(EDGES)
    for x in range(GW):
        while k < len(edges) and edges[k][0] <= x:
            cur = hi if edges[k][1] == "r" else lo
            k += 1
        level[x] = cur
    o = []
    for x in range(GW):
        y = level[x]
        a, b = y - rnd.uniform(0.5, band), y + rnd.uniform(0.5, band)
        if x in edges_x(edges):
            a, b = min(hi, lo) - band, max(hi, lo) + band
        for sx, sv in spikes:
            if sx == x:
                a = min(a, max(zero_y - sv * px_per_v, 1.0))
        a, b = max(1.0, a), min(GH - 1.0, b)
        o.append(f'<line x1="{x + 0.5}" y1="{a:.1f}" x2="{x + 0.5}" y2="{b:.1f}" stroke="{colour}" stroke-width="1"/>')
    return "".join(o)


def edges_x(edges):
    return {e[0] for e in edges}


PK1_ZERO, PK2_ZERO = 201, 301       # both channels at 1 V/div (50 px per volt): CH1 zero on the centre line


def scope_peak_plot():
    """the 1 kHz square as min / max bars; CH1 carries four spikes to about 2.8 V that 500 kSa/s sampling would miss"""
    extra = peak_bars(PK2_ZERO, 1.87, -1.19, 50.0, CH2, seed=4, band=3.0)
    extra += peak_bars(PK1_ZERO, 1.87, -1.19, 50.0, CH1, band=3.0,
                       spikes=[(61, 2.7), (184, 2.9), (419, 2.6), (662, 2.8)])
    extra += pointers(PK1_ZERO, PK2_ZERO, PK1_ZERO - 47)
    return box(GX, GY, GW, GH, "", scope_grid_svg(extra))


if want("ScopePeak"):
    ch1 = [("Vmax", "2.90V"), ("Vpp", "4.09V"), ("Freq", "1.000kHz")]
    body = "\n".join([topbar(ch1_vdiv="1V/div"), scope_peak_plot(), cards(ch1, 5, CH1, "1"),
                      cards(CH2_CARDS, 366, CH2, "2"),
                      slider_row(layers=scope_square_overview([(CH2, PK2_ZERO, 50.0), (CH1, PK1_ZERO, 50.0)])),
                      rightbar_actions()])
    write_board("ScopePeak.dc.html", "Scope view, Sample Peak", body)


# ---------------------------------------------------------------- a pulse-width trigger in use
def pulse_points(zero_y, hi_v, lo_v, px_per_v, edges, noise=0.6, seed=5):
    rnd = random.Random(seed)
    hi = max(zero_y - hi_v * px_per_v, 1.0)
    lo = zero_y - lo_v * px_per_v
    pts, level, x = [], lo, 0
    for ex, kind in edges:
        while x < ex:
            pts.append((x, level + (0 if level <= 1.0 else rnd.uniform(-noise, noise))))
            x += 4
        pts.append((ex, level))
        level = hi if kind == "r" else lo
        pts.append((ex, level))
        x = ex + 2
    while x <= GW:
        pts.append((x, level + (0 if level <= 1.0 else rnd.uniform(-noise, noise))))
        x += 4
    return " ".join(f"{a:.1f},{b:.1f}" for a, b in pts)


def scope_pulse_plot():
    """CH1 at 1 V/div, zero on the centre line: a 1 kHz square (500 us pulses) with one 40 us pulse whose trailing edge
    sits on the trigger point (the centre line): the Pulse width trigger, shorter than 100 us, fired on it"""
    zero = 201.0
    edges = [(102, "r"), (227, "f"), (342, "r"), (362, "f"), (477, "r"), (602, "f"), (717, "r")]
    extra = (f'<polyline points="{pulse_points(zero, 1.87, -1.19, 50.0, edges)}" fill="none" stroke="{CH1}" '
             'stroke-width="2"></polyline>')
    extra += pointers(zero, None, zero - 1.87 * 50 / 2)
    return box(GX, GY, GW, GH, "", scope_grid_svg(extra))


if want("ScopePulse"):
    ch1 = [("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Duty", "49.8%")]
    body = "\n".join([topbar(ch1_vdiv="1V/div", ch2_on=False), trig_chip("Normal", "CH1", "pulse+"),
                      scope_pulse_plot(), cards(ch1, 5, CH1, "1"), slider_row(), rightbar_actions()])
    write_board("ScopePulse.dc.html", "Scope view, a Pulse width trigger in use", body)
