import math, random, sys, os

OUT = sys.argv[1] if len(sys.argv) > 1 else "."

# stock colour defines (firmware/scope/fnirsi_1013d_scope.h)
CH1 = "#ffff00"; CH2 = "#00ffff"; TRIG = "#00ff00"; ORANGE = "#ff8000"; RED = "#ff0000"
DARKGREY = "#181818"; LIGHTGREY = "#333333"; MIDGREY = "#383838"; GREY = "#444444"; LG1 = "#606060"; LG2 = "#bbbbbb"
DARKBLUE = "#000078"; PALEYEL = "#ffff80"; ACTIVE = "#ef9311"; GREEN = "#00ff00"; EDGE = "#505050"
FONT = "Arial, 'Liberation Sans', sans-serif"


def page(title, body):
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{title}</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
<style>
body{{margin:0;background:#000}}
a{{color:#9ecbff}}a:hover{{color:#ffffff}}
</style>
</helmet>
<div style="position: relative; width: 800px; height: 480px; box-sizing: border-box; background: #000000; color: #ffffff; font-family: {FONT}; font-size: 16px; overflow: hidden">
{body}
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":800,"height":480}}}}'>
class Component extends DCLogic {{
  renderVals() {{
    return {{}};
  }}
}}
</script>
</body>
</html>
'''


def write(name, title, body):
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(page(title, body))
    print("wrote", name)


def box(x, y, w, h, style="", inner=""):
    return (f'<div style="position: absolute; left: {x}px; top: {y}px; width: {w}px; height: {h}px; '
            f'box-sizing: border-box; {style}">{inner}</div>')


def text(x, y, s, size=16, color="#ffffff", extra=""):
    return (f'<div style="position: absolute; left: {x}px; top: {y}px; font-size: {size}px; line-height: {size + 2}px; '
            f'color: {color}; white-space: nowrap; {extra}">{s}</div>')


def abtn(x, y, w, h, inner, style, href=None, disabled=False):
    base = (f"position: absolute; left: {x}px; top: {y}px; width: {w}px; height: {h}px; box-sizing: border-box; "
            f"padding: 0; font: inherit; {style}")
    if href:
        return f'<a href="{href}" style="{base}; text-decoration: none; display: block">{inner}</a>'
    dis = " disabled" if disabled else ""
    return f'<button type="button"{dis} style="{base}">{inner}</button>'


def centred(lines):
    """lines: list of (text, size, colour); stacked and centred in the parent"""
    inner = "".join(f'<span style="display: block; font-size: {s}px; line-height: {s + 1}px; color: {c}">{t}</span>'
                    for t, s, c in lines)
    return ('<span style="display: flex; flex-direction: column; justify-content: center; align-items: center; '
            f'height: 100%; text-align: center">{inner}</span>')


# ---------------------------------------------------------------- top bar (variables.h positions)
def topbar(rate="500KSa/s", tb="200us/div", menu_active=False, ch1_vdiv="500mV/div", ch2_on=True,
           ch2_vdiv="1V/div", acq_active=False, acq_href="AcqMenu.dc.html", menu_href="MainMenu.dc.html"):
    o = []
    mbg = PALEYEL if menu_active else DARKBLUE
    mfg = "#000000" if menu_active else "#ffffff"
    icon = ''.join(box(6 + (i % 2) * 9, 11 + (i // 2) * 9, 7, 7, f"background: {mfg}") for i in range(4))
    o.append(abtn(0, 3, 70, 38, icon + text(27, 10, "MENU", 16, mfg),
                  f"background: {mbg}; border: none; border-radius: 3px", href=menu_href))
    rays = ''.join(
        f'<line x1="{21.5 + 8 * math.cos(a):.1f}" y1="{19 + 8 * math.sin(a):.1f}" x2="{21.5 + 12 * math.cos(a):.1f}" '
        f'y2="{19 + 12 * math.sin(a):.1f}" stroke="#ffffff" stroke-width="2"></line>'
        for a in [k * math.pi / 4 for k in range(8)])
    sun = ('<svg width="43" height="38" viewBox="0 0 43 38" style="position: absolute; left: 0; top: 0">'
           '<circle cx="21.5" cy="19" r="5" fill="#ffffff"></circle>' + rays + '</svg>')
    o.append(abtn(75, 3, 43, 38, sun, "background: #000000; border: none"))
    # MCP chip directly above the RUN/STOP chip
    o.append(box(127, 3, 38, 17, f"background: {GREEN}; border-radius: 2px", text(4, 1, "MCP", 13, "#000000")))
    o.append(box(127, 22, 38, 18, f"background: {GREEN}; border-radius: 2px", text(6, 1, "RUN", 13, "#000000")))

    def chip(x, w, icon_w, icon_bg, icon_html, params_html, href=None):
        """one chip: a grey parameter box drawn first, from under the icon block to the right end, then the
        coloured icon block over its left part; only the chip's outer corners are rounded, so there is no notch"""
        inner = (box(0, 0, w, 35, f"background: {LIGHTGREY}; border-radius: 3px") +
                 box(0, 0, icon_w, 35, f"background: {icon_bg}; border-radius: 3px 0 0 3px", icon_html) + params_html)
        return abtn(x, 5, w, 35, inner, "background: transparent; border: none; text-align: left", href=href)

    def chan(x, name, color, on, coupling, probe, vdiv):
        tab = color if on else GREY
        tfg = "#000000" if on else LG2
        fg = "#ffffff" if on else LG2
        return chip(x, 103, 30, tab, text(3, 9, name, 13, tfg),
                    text(38, 2, coupling, 13, fg) + text(64, 2, probe, 13, fg) + text(38, 18, vdiv, 13, fg))

    o.append(chan(174, "CH1", CH1, True, "DC", "1X", ch1_vdiv))
    o.append(chan(281, "CH2", CH2, ch2_on, "DC", "1X", ch2_vdiv if ch2_on else "off"))
    tab_bg = "#ffffff" if acq_active else GREEN
    o.append(chip(388, 103, 30, tab_bg, text(2, 9, "ACQ", 13, "#000000"), text(38, 2, rate, 13) + text(38, 18, tb, 13),
                  href=acq_href))
    # Move fast stays in the top bar at its stock slot (owner)
    o.append(abtn(496, 5, 44, 35, text(5, 1, "Move", 13) + text(10, 17, "fast", 13),
                  f"background: {DARKBLUE}; border: none; border-radius: 2px; text-align: left"))
    edge = ('<svg width="12" height="15" viewBox="0 0 12 15" style="position: absolute; left: 66px; top: 19px">'
            '<polyline points="1,13 6,13 6,2 11,2" fill="none" stroke="#ffffff" stroke-width="1.5"></polyline></svg>')
    o.append(chip(546, 80, 31, TRIG, text(11, 8, "T", 16, "#000000"),
                  text(36, 1, "Auto", 13) +
                  box(35, 19, 28, 14, "background: #cccc00; border-radius: 2px", text(2, -1, "CH1", 13, "#000000")) + edge))
    wave = ('<svg width="23" height="14" viewBox="0 0 23 14" style="position: absolute; left: 0; top: 0">'
            '<polyline points="2,11 7,11 7,3 13,3 13,11 18,11 18,3 21,3" fill="none" stroke="#00ff00" '
            'stroke-width="1.5"></polyline></svg>')
    inner = (box(1, 1, 25, 16, f"border: 1px solid {GREEN}; border-radius: 2px", wave) +
             text(29, 1, "50%", 13) + text(7, 18, "1kHz", 13))
    o.append(abtn(630, 5, 62, 35, inner,
                  f"background: #000000; border: 1px solid {GREEN}; border-radius: 2px; text-align: left"))
    # battery 18 of 20 (701, 5, 25, 13)
    o.append(box(701, 5, 25, 13, "border: 1px solid #ffffff; border-radius: 1px", box(2, 2, 19, 7, f"background: {GREEN}")))
    o.append(box(698, 9, 3, 5, "background: #ffffff"))
    # version string removed (owner): nothing at (698, 24)
    return "\n".join(o)


# ---------------------------------------------------------------- right bar (menu.c: x 739, 51 x 50)
def rbtn(y, lines, kind="normal", href=None):
    if kind == "run":
        bg, fg = GREEN, "#000000"
    elif kind == "active":
        bg, fg = ACTIVE, "#000000"
    elif kind == "chan1":
        bg, fg = CH1, "#000000"
    else:
        bg, fg = DARKGREY, "#ffffff"
    ls = [(t, s, fg) for t, s in lines]
    return abtn(739, y, 51, 50, centred(ls), f"background: {bg}; border: 1px solid {EDGE}; border-radius: 2px",
                href=href)


def rightbar_actions(ctrl_href="ScopeCtrl.dc.html", spectrum=False):
    o = [box(730, 0, 70, 480, "background: #000000"),
         rbtn(5, [("CTRL", 16)], href=ctrl_href),
         rbtn(65, [("RUN/", 16), ("STOP", 16)], kind="run"),
         rbtn(125, [("AUTO", 16), ("SET", 16)])]
    if not spectrum:
        o += [rbtn(185, [("T CU", 16), ("RSOR", 16)]),
              rbtn(245, [("V CU", 16), ("RSOR", 16)]),
              rbtn(305, [("MEAS", 16), ("URES", 16)])]
    o.append(rbtn(365, [("SAVE", 16), ("PIC", 16)]))
    o.append(rbtn(425, [("SAVE", 16), ("WAVE", 16)]))   # in Spectrum it saves the time record
    return "\n".join(o)


def rightbar_ctrl(back_href="Scope.dc.html"):
    o = [box(730, 0, 70, 480, "background: #000000"),
         rbtn(5, [("CTRL", 16)], kind="active", href=back_href)]
    for y0, name, col in [(99, "CH1", CH1), (264, "CH2", CH2)]:
        o.append(rbtn(y0, [("V+", 24)]))
        o.append(box(739, y0 + 56, 51, 24, f"background: {col}; border-radius: 2px", text(14, 3, name, 13, "#000000")))
        o.append(rbtn(y0 + 86, [("V-", 24)]))
    o.append(rbtn(425, [("50%", 16), ("TRIG", 16)]))
    return "\n".join(o)


# ---------------------------------------------------------------- scope grid (menu.c scope_draw_grid)
GX, GY, GW, GH = 2, 48, 726, 403   # centre lines at x 364, y 249 (relative 362, 201); 50 px divisions


def scope_grid_svg(extra=""):
    o = [f'<svg width="{GW}" height="{GH}" viewBox="0 0 {GW} {GH}" style="position: absolute; left: 0; top: 0">']
    o.append(f'<rect x="0.5" y="0.5" width="{GW - 1}" height="{GH - 1}" fill="none" stroke="{LG1}" stroke-width="1"></rect>')
    o.append(f'<line x1="0" y1="201.5" x2="{GW}" y2="201.5" stroke="{LG1}" stroke-width="1"></line>')
    o.append(f'<line x1="362.5" y1="0" x2="362.5" y2="{GH}" stroke="{LG1}" stroke-width="1"></line>')
    for x in range(12, GW, 50):
        if x != 362:
            o.append(f'<line x1="{x + 0.5}" y1="6" x2="{x + 0.5}" y2="{GH - 3}" stroke="{LG1}" stroke-width="1" '
                     'stroke-dasharray="1 4"></line>')
    for y in range(51, GH, 50):
        if y != 201:
            o.append(f'<line x1="2" y1="{y + 0.5}" x2="{GW - 2}" y2="{y + 0.5}" stroke="{LG1}" stroke-width="1" '
                     'stroke-dasharray="1 4"></line>')
    o.append(extra)
    o.append('</svg>')
    return "".join(o)


# 1 kHz at 200 us/div, 50 px per division: period 250 px, rising edge on the centre line
EDGES = sorted([(112 + 250 * k, "r") for k in range(3)] + [(237 + 250 * k, "f") for k in range(2)])


def square_points(zero_y, hi_v, lo_v, px_per_v, noise=0.6, seed=1):
    rnd = random.Random(seed)
    hi = max(zero_y - hi_v * px_per_v, 1.0)
    lo = zero_y - lo_v * px_per_v
    pts, level, x = [], lo, 0
    for ex, kind in EDGES:
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


def pointers(ch1_zero, ch2_zero=None, trig_y=None):
    p = f'<polygon points="1,{ch1_zero - 6} 11,{ch1_zero} 1,{ch1_zero + 6}" fill="{CH1}"></polygon>'
    if ch2_zero is not None:
        p += f'<polygon points="1,{ch2_zero - 6} 11,{ch2_zero} 1,{ch2_zero + 6}" fill="{CH2}"></polygon>'
    if trig_y is not None:
        p += f'<polygon points="{GW - 1},{trig_y - 6} {GW - 11},{trig_y} {GW - 1},{trig_y + 6}" fill="{TRIG}"></polygon>'
    p += f'<polygon points="356,1 368,1 362,10" fill="{ORANGE}"></polygon>'
    return p


def warning_box(msg, cx=365, y=70, w=420):
    return box(cx - w // 2, y, w, 26, "background: #000000; text-align: center",
               f'<span style="font-size: 16px; line-height: 26px; color: {ORANGE}">{msg}</span>')


# CH1 500mV/div (100 px per volt), zero one division above the centre: the +1.87 V level clips at the top.
# CH2 1V/div (50 px per volt), zero two divisions below the centre. Levels -1.19 / +1.87 V (hardware-status 08:30).
CH1_ZERO, CH2_ZERO = 151, 301


def scope_plot(warning="CH1 overrange: increase V/div"):
    extra = (f'<polyline points="{square_points(CH2_ZERO, 1.87, -1.19, 50.0, seed=2)}" fill="none" stroke="{CH2}" '
             'stroke-width="2"></polyline>')
    extra += (f'<polyline points="{square_points(CH1_ZERO, 1.87, -1.19, 100.0)}" fill="none" stroke="{CH1}" '
              'stroke-width="2"></polyline>')
    extra += pointers(CH1_ZERO, CH2_ZERO, CH1_ZERO - 34)
    o = [box(GX, GY, GW, GH, "", scope_grid_svg(extra))]
    if warning:
        o.append(warning_box(warning))
    return "\n".join(o)


# ---------------------------------------------------------------- the slider row and its overview (docs/plans/slider-overview.md)
# fw 0.2.72 (owner 10-19 21:36, slider-overview.md 10 F item 6; firmware slider_ov.h): one line at y 452 over x 2..727
# parts the track from the plot (no frame sides or bottom); the track is x 2..727 (726 columns, TBV_SLIDER_X / _W,
# BU_SL_X / _W) and y 453..471 (19 rows); the window is two 1 px lines, its left and right edges, over the track's full
# height. The overview uses every row and column: dimmed outside the window, full colour inside it; the edges are drawn
# last, over it.
OV_X, OV_W, OV_Y, OV_ROWS = 2, 726, 453, 19
WIN_Y, WIN_H = 453, 19
DIM = {CH1: "#808000", CH2: "#008080"}     # the channel colours at half intensity (outside the window)
OV_FLAG = "#707070"                         # bode_view.c FLAG_COLOR: a flagged Frequency response point


def ov_row(frac):
    """the overview row of a fraction 0 (top of the plot) .. 1 (bottom), clipped to the 19 rows"""
    return max(OV_Y, min(OV_Y + OV_ROWS - 1, OV_Y + int(frac * OV_ROWS)))


def ov_line_runs(pts):
    """a line through (x, row) points as one vertical run per column (r0 <= r1), the way the firmware joins a trace:
    each column from the previous column's row to its own"""
    runs = {}
    for (xa, ra), (xb, rb) in zip(pts, pts[1:]):
        prev = ra
        for x in range(xa, xb + 1):
            r = round(ra + (rb - ra) * (x - xa) / (xb - xa)) if xb != xa else rb
            lo, hi = min(prev, r), max(prev, r)
            if x in runs:
                lo, hi = min(lo, runs[x][0]), max(hi, runs[x][1])
            runs[x] = (lo, hi)
            prev = r
    return sorted((x, a, b) for x, (a, b) in runs.items())


def ov_svg(win_x, win_w, layers):
    """layers drawn in order, each (full colour, dim colour, [(x, r0, r1), ...]): a column inside the window
    (win_x <= x < win_x + win_w) in the full colour, outside it dimmed; equal neighbouring columns merge into one rect"""
    rects = []
    for full, dimc, runs in layers:
        cur = None
        for x, r0, r1 in sorted(runs) + [(None, None, None)]:
            col = None if x is None else (full if win_x <= x < win_x + win_w else dimc)
            key = (r0, r1, col)
            if cur and x is not None and key == cur[1] and x == cur[0] + cur[2]:
                cur[2] += 1
                continue
            if cur:
                cx, (a, b, c), n = cur
                rects.append(f'<rect x="{cx}" y="{a - 452}" width="{n}" height="{b - a + 1}" fill="{c}"></rect>')
            cur = [x, key, 1] if x is not None else None
    return ('<svg width="730" height="20" viewBox="0 0 730 20" aria-hidden="true" shape-rendering="crispEdges" '
            f'style="position: absolute; left: 0; top: 452px">{"".join(rects)}</svg>')


def slider_row(win_x=189, win_w=350, layers=()):
    """the record-position slider under the grid (menu.c scope_draw_slider_row, fw 0.2.72: the line at y 452, the
    overview in the track y 453..471, the window's two edge lines; fw 0.2.64 tbv_slider window). The Scope boards'
    window: 3000 samples at 500 kSa/s, 200 us/div, the trigger at the record's middle on the centre line: first sample
    776 -> x 2 + 776 x 726 / 3000 = 189, 1449 samples on screen -> 350 px"""
    return (box(2, 452, 726, 1, f"background: {LG1}") + (ov_svg(win_x, win_w, layers) if layers else "") +
            box(win_x, WIN_Y, 1, WIN_H, f"background: {LG2}") +
            box(win_x + win_w - 1, WIN_Y, 1, WIN_H, f"background: {LG2}"))


def scope_square_overview(chans, total=3000):
    """the record's min / max envelope for the Scope boards' 1 kHz square (rising edges at record samples 500 k,
    falling at 500 k + 250; on the plot x = (sample - 776) / 2, EDGES above). chans: [(colour, zero_y, px_per_v)] in
    drawing order (CH2 first, CH1 over it, as the traces). Each column covers total / 726 samples plus the previous
    column's last one (so the columns join); its min and max go
    through the plot's own y mapping (screen y 49..448, clipped as scope_get_y_sample clips) to the 19 rows"""
    layers = []
    for colour, zero, ppv in chans:
        hi, lo = max(1.0, zero - 1.87 * ppv), zero + 1.19 * ppv
        runs = []
        for c in range(OV_W):
            s0, s1 = c * total / OV_W, (c + 1) * total / OV_W
            # the previous column's last sample joins the columns: a step shows as a vertical
            ys = {hi if (k % 500) < 250 else lo for k in range(int(s0) - 1, int(s1) + 1) if (s0 - 1 <= k < s1) and k >= 0}
            r = [ov_row((v + GY - 49) / 400.0) for v in ys]
            runs.append((OV_X + c, min(r), max(r)))
        layers.append((colour, DIM[colour], runs))
    return layers


SCOPE_OVERVIEW = [(CH2, CH2_ZERO, 50.0), (CH1, CH1_ZERO, 100.0)]


def cards(items, xstart, color, digit=None):
    """measurement cards (owner, final): a dark grey rounded card 118 x 22 drawn first, then a block in the channel
    colour over its left end, full card height, rounded only on its outer left corners and only as wide as the name
    plus 3 px each side; the name in black 13 px inside it, the value in white 13 px to its right.
    CH1 from x 5, CH2 from x 366, three per row, rows stacking upward from y 426"""
    o = []
    x, y = xstart, 426
    for name, val in items:
        if x > xstart + 359:
            x, y = xstart, y - 24
        inner = (f'<span style="display: flex; align-items: center; height: 100%">'
                 f'<span style="display: inline-flex; align-items: center; height: 100%; padding: 0 3px; '
                 f'background: {color}; border-radius: 3px 0 0 3px; font-size: 13px; color: #000000; '
                 f'white-space: nowrap">{name}</span>'
                 f'<span style="margin-left: 6px; font-size: 13px; color: #ffffff; white-space: nowrap">{val}</span></span>')
        o.append(box(x, y, 118, 22, f"background: {LIGHTGREY}; border-radius: 3px", inner))
        x += 120
    return "\n".join(o)


CH1_CARDS = [("Vmin", "-1.19V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")]
CH2_CARDS = [("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Width", "500us")]


def scope_view(warning="CH1 overrange: increase V/div", right=None, menu_active=False, acq_active=False):
    return "\n".join([topbar(menu_active=menu_active, acq_active=acq_active), scope_plot(warning),
                      cards(CH1_CARDS, 5, CH1, "1"), cards(CH2_CARDS, 366, CH2, "2"),
                      slider_row(layers=scope_square_overview(SCOPE_OVERVIEW)),
                      right if right else rightbar_actions()])


def dim():
    return box(0, 0, 800, 480, "background: rgba(0, 0, 0, 0.65)")


# ---------------------------------------------------------------- dialog helpers
def dbtn(x, y, w, h, label, kind="normal", href=None, disabled=False, size=16):
    if kind == "go":
        st = "background: #1b4d2e; border: 1px solid #2f8a52; border-radius: 4px; color: #ffffff"
    elif kind == "danger":
        st = "background: #4d1b1b; border: 1px solid #a33a3a; border-radius: 4px; color: #ffffff"
    elif kind == "default":
        st = "background: #1f3a66; border: 2px solid #9ecbff; border-radius: 4px; color: #ffffff"
    elif kind == "off":
        st = f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}; border-radius: 4px; color: {LG1}"
    else:
        st = f"background: {LIGHTGREY}; border: 1px solid {LG1}; border-radius: 4px; color: #ffffff"
    inner = (f'<span style="display: flex; align-items: center; justify-content: center; height: 100%; '
             f'font-size: {size}px">{label}</span>')
    return abtn(x, y, w, h, inner, st, href=href, disabled=disabled)


def tick(x, y, ok=True):
    if ok:
        return ('<svg width="18" height="18" viewBox="0 0 18 18" style="position: absolute; '
                f'left: {x}px; top: {y}px"><polyline points="2,9 7,14 16,3" fill="none" stroke="#35c26a" '
                'stroke-width="3"></polyline></svg>')
    return ('<svg width="18" height="18" viewBox="0 0 18 18" style="position: absolute; '
            f'left: {x}px; top: {y}px"><circle cx="9" cy="9" r="6" fill="none" stroke="#9a9a9a" '
            'stroke-width="2"></circle></svg>')
