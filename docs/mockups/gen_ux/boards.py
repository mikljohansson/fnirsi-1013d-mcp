import math, random, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *  # noqa

which = sys.argv[2] if len(sys.argv) > 2 else "all"


def want(n):
    return which == "all" or which == n


# ================================================================ Scope (right bar first page as today)
if want("Scope"):
    write("Scope.dc.html", "Scope view", scope_view())


# ================================================================ CTRL pages (ux-makeover.md section 3, final)
def vbtn(y, label_lines, value=None, kind="normal", href=None, label_colour=None):
    """CTRL-page button at the stock 51 x 50 slot: label lines 13 px, value 13 px in pale yellow"""
    fg = "#000000" if kind in ("active", "run") else "#ffffff"
    lines = [(t, 13, label_colour or fg) for t in label_lines]
    if value is not None:
        lines.append((value, 13, "#000000" if kind == "active" else PALEYEL))
    bg = ACTIVE if kind == "active" else DARKGREY
    return abtn(739, y, 51, 50, centred(lines), f"background: {bg}; border: 1px solid {EDGE}; border-radius: 2px",
                href=href)


def stepper(y, digit, value, colour, h=82, bar=28, centre=26):
    """V/div stepper (owner): grey + bar, centre block in the channel colour, grey - bar; the + and - hit areas
    extend into their half of the centre, so the two buttons tile the whole drawn area"""
    half = h // 2
    plus = abtn(739, y, 51, half,
                box(0, 0, 51, bar, f"background: {GREY}; border-radius: 2px 2px 0 0", centred([("V+", 13, "#ffffff")])) +
                box(0, bar, 51, half - bar, f"background: {colour}"),
                "background: transparent; border: none")
    minus = abtn(739, y + half, 51, h - half,
                 box(0, 0, 51, h - half - bar, f"background: {colour}") +
                 box(0, h - half - bar, 51, bar, f"background: {GREY}; border-radius: 0 0 2px 2px", centred([("V-", 13, "#ffffff")])),
                 "background: transparent; border: none")
    label = box(739, y + bar, 51, centre, "pointer-events: none",
                centred([(f"CH{digit}", 16, "#000000")]))
    return plus + minus + label


def vbtn_at(y, h, label_lines, value=None, kind="normal", href=None):
    fg = "#000000" if kind == "active" else "#ffffff"
    lines = [(t, 13, fg) for t in label_lines]
    if value is not None:
        lines.append((value, 13, "#000000" if kind == "active" else PALEYEL))
    bg = ACTIVE if kind == "active" else DARKGREY
    return abtn(739, y, 51, h, centred(lines), f"background: {bg}; border: 1px solid {EDGE}; border-radius: 2px", href=href)


def ctrl_scope(active=None, display_val="Normal", back_href="Scope.dc.html", phosphor=False):
    """stock 60 px pitch: CTRL 5, Display 65, Trace style 125, Grid 185; 82 px steppers at 244 and 334;
    50 % TRIG at its stock 425"""
    k = lambda n: "active" if active == n else "normal"
    return "\n".join([
        box(730, 0, 70, 480, "background: #000000"),
        rbtn(5, [("CTRL", 16)], kind="active", href=back_href),
        display_button(display_val, k("display"), phosphor,
                       "ScopeCtrl.dc.html" if active == "display" else "ScopeCtrlDisplay.dc.html"),
        vbtn(125, ["Trace", "style"], "Linear", k("trace")),
        vbtn(185, ["Grid"], "on", k("grid")),
        stepper(244, "1", "500mV", CH1),
        stepper(334, "2", "1V", CH2),
        rbtn(425, [("50%", 16), ("TRIG", 16)]),
    ])


def ctrl_spectrum(active=None):
    """the Scope page's positions and sizes: CTRL 5, Display 65, Analyse 125, Grid 185 (stock 51 x 50, 60 px pitch),
    82 px steppers at 244 and 334, 50 % TRIG at 425"""
    k = lambda n: "active" if active == n else "normal"
    return "\n".join([
        box(730, 0, 70, 480, "background: #000000"),
        rbtn(5, [("CTRL", 16)], kind="active", href="Spectrum.dc.html"),
        vbtn(65, ["Display"], "Spec", k("display"),
             href="SpectrumCtrl.dc.html" if active == "display" else "SpectrumCtrlDisplay.dc.html"),
        vbtn(125, ["Analyse"], "2048 pt", k("analysis"),
             href="SpectrumCtrl.dc.html" if active == "analysis" else "SpectrumCtrlAnalysis.dc.html"),
        vbtn(185, ["Grid"], "on", k("grid")),
        stepper(244, "1", "500mV", CH1),
        stepper(334, "2", "1V", CH2),
        rbtn(425, [("50%", 16), ("TRIG", 16)]),
    ])



SVG0 = '<svg width="28" height="20" viewBox="0 0 28 20" aria-hidden="true" style="flex: none">'
ICON_NORMAL = SVG0 + '<path d="M2,10 C6,1 10,1 14,10 S22,19 26,10" fill="none" stroke="currentColor" stroke-width="2"></path></svg>'
ICON_PHOSPHOR = (SVG0 + '<path d="M2,10 C6,1 10,1 14,10 S22,19 26,10" fill="none" stroke="currentColor" stroke-width="8" stroke-opacity="0.2"></path>'
                 '<path d="M2,10 C6,1 10,1 14,10 S22,19 26,10" fill="none" stroke="currentColor" stroke-width="4" stroke-opacity="0.45"></path>'
                 '<path d="M2,10 C6,1 10,1 14,10 S22,19 26,10" fill="none" stroke="currentColor" stroke-width="1.5"></path></svg>')
ICON_XY = (SVG0 + '<g transform="rotate(-20 14 10)"><polyline points="14.0,10.0 15.7,12.2 17.4,14.1 19.0,15.7 20.5,16.7 21.8,17.0 22.9,16.7 23.8,15.7 24.5,14.1 24.9,12.2 25.0,10.0 24.9,7.8 24.5,5.9 23.8,4.3 22.9,3.3 21.8,3.0 20.5,3.3 19.0,4.3 17.4,5.9 15.7,7.8 14.0,10.0 12.3,12.2 10.6,14.1 9.0,15.7 7.5,16.7 6.2,17.0 5.1,16.7 4.2,15.7 3.5,14.1 3.1,12.2 3.0,10.0 3.1,7.8 3.5,5.9 4.2,4.3 5.1,3.3 6.2,3.0 7.5,3.3 9.0,4.3 10.6,5.9 12.3,7.8 14.0,10.0" fill="none" stroke="currentColor" '
           'stroke-width="2"></polyline></g></svg>')


# spectrum Display icons (28 x 20, stroke or fill in the row's text colour)
ICON_SPECTRUM = (SVG0 + '<line x1="2" y1="18" x2="26" y2="18" stroke="currentColor" stroke-width="1.5"></line>' +
                 "".join(f'<line x1="{x}" y1="18" x2="{x}" y2="{y}" stroke="currentColor" stroke-width="2"></line>'
                         for x, y in [(6, 4), (11, 10), (16, 13), (21, 15)]) + '</svg>')
ICON_WATERFALL = (SVG0 + "".join(
    f'<line x1="{x0}" y1="{y}" x2="{x0 + 4}" y2="{y}" stroke="currentColor" stroke-width="2" stroke-opacity="{op}"></line>'
    for y, op in [(4, 1), (9, 0.75), (14, 0.5), (19, 0.3)] for x0 in (3, 10, 17, 22) if x0 + 4 <= 26) + '</svg>')
ICON_DENSITY = (SVG0 + "".join(
    f'<circle cx="{4 + 5 * i}" cy="{3 + 4.5 * j:.1f}" r="1.6" fill="currentColor" fill-opacity="{op}"></circle>'
    for j in range(4) for i in range(5)
    for op in [round(0.25 + 0.75 * max(0.0, 1 - abs(j - (3 - 0.6 * i)) / 2.2), 2)]) + '</svg>')


PHOS_MARK = ('<svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true">'
             '<circle cx="5" cy="5" r="5" fill="currentColor" fill-opacity="0.25"></circle>'
             '<circle cx="5" cy="5" r="3.2" fill="currentColor" fill-opacity="0.55"></circle>'
             '<circle cx="5" cy="5" r="1.6" fill="currentColor"></circle></svg>')


def display_button(view, kind, phosphor, href):
    """CTRL-page Display button: label, the view as value, and the phosphor marker under it while the switch is on"""
    fg = "#000000" if kind == "active" else "#ffffff"
    vc = "#000000" if kind == "active" else PALEYEL
    lines = [("Display", 13, fg), (view, 13, vc)]
    if phosphor:
        lines.append((f'<span style="display: inline-flex; color: {vc}">{PHOS_MARK}</span>', 13, vc))
    bg = ACTIVE if kind == "active" else DARKGREY
    return abtn(739, 65, 51, 50, centred(lines), f"background: {bg}; border: 1px solid {EDGE}; border-radius: 2px", href=href)


def slide_switch(on=True, color=GREEN):
    """stock slide toggle (menu.c scope_display_slide_button): 45 x 21, green fill with a darker edge when on,
    grey knob 20 x 13 on the right with four black grip lines; grey fill and knob on the left when off"""
    fill = color if on else "#888888"
    edge = "#008800" if on else GREY
    kx = 21 if on else 4
    grips = "".join(box(kx + 5 + 3 * i, 6, 1, 9, "background: #000000") for i in range(4))
    return ('<span style="position: relative; display: inline-block; width: 45px; height: 21px; flex: none">' +
            box(0, 0, 45, 21, f"background: {fill}; border: 1px solid {edge}; border-radius: 2px") +
            box(kx, 4, 20, 13, f"background: {GREY}") + grips + '</span>')


def switch_row(x, y, w, label, on, icon=""):
    """44 px fold-out row with a slide switch at the right; the whole row toggles"""
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: #ffffff">'
             f'<span style="display: flex; align-items: center; gap: 8px">{icon}<span>{label}</span></span>'
             f'{slide_switch(on)}</span>')
    return abtn(x, y, w, 44, inner, f"background: {LIGHTGREY}; border: none; border-radius: 3px; text-align: left")

def foldout_panel(x, y, w, h, title, inner):
    """small panel beside the right bar, stock menu colours (dark grey, lighter grey edge)"""
    return box(x, y, w, h, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}",
               text(10, 8, title, 13, LG2) + inner)


def frow(x, y, w, label, value=None, sel=False, href=None, icon=""):
    """44 px fold-out row: optional icon and label left, value right; selected rows green with black text"""
    fg = "#000000" if sel else "#ffffff"
    val = f'<span style="color: {"#000000" if sel else PALEYEL}">{value}</span>' if value is not None else ""
    lab = f'<span style="display: flex; align-items: center; gap: 8px">{icon}<span>{label}</span></span>'
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: {fg}">{lab}{val}</span>')
    return abtn(x, y, w, 44, inner, f"background: {GREEN if sel else LIGHTGREY}; border: none; border-radius: 3px; "
                "text-align: left", href=href)


# ================================================================ ScopeCtrl (CTRL page, final layout)
if want("ScopeCtrl"):
    write("ScopeCtrl.dc.html", "Scope controls", scope_view(right=ctrl_scope()))


# ================================================================ AcqMenu (scope_open_acquisition_menu + Display)
AX, AY, AW, AH = 250, 48, 380, 386   # stock width: Trace style left for the CTRL page


def acq_menu():
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
    p.append(box(8, 155, 296 - 76 + 72, 1, "background: #000000"))
    # Roll / Average / Filter boxes (90 x 40 at y 163)
    for x, l1, l2, on in [(8, "Roll", "mode", False), (106, "Average", "mode", False), (204, "Filter", "CIC2+FIR", True)]:
        p.append(abtn(x, 163, 90, 40, centred([(l1, 13, "#000000" if on else "#ffffff"),
                                               (l2, 13, "#000000" if on else "#ffffff")]),
                      f"background: {GREEN if on else LIGHTGREY}; border: none; border-radius: 3px"))
    p.append(box(8, 211, AW - 16, 1, "background: #000000"))
    p.append(text(97, 216, "Time per Division", 16))
    for i, s in enumerate(tdivs):
        sel = s == "200us/div"
        x, y = (i & 3) * 72 + 10, (i >> 2) * 23 + 241
        p.append(abtn(x, y, 68, 20, centred([(s, 13, "#000000" if sel else "#ffffff")]),
                      f"background: {GREEN if sel else MIDGREY}; border: none; border-radius: 2px"))
    p.append(box(304, 8, 1, AH - 16, "background: #000000"))
    p.append(text(317, 8, "Memory", 16))
    # with the Filter on the firmware hides depths above 8190 samples (filtered records hold at most 8190)
    for i, s in enumerate(["3000", "6000"]):
        sel = i == 0
        p.append(abtn(315, 33 + i * 36, 54, 25, centred([(s, 13, "#000000" if sel else "#ffffff")]),
                      f"background: {GREEN if sel else MIDGREY}; border: none; border-radius: 2px"))
    p.append(text(310, 104, "pass 15 ms", 13, "#ffffff"))   # today's pass-time line under Memory
    p.append(text(310, 126, "Filter on:", 13, LG2))
    p.append(text(310, 144, "up to 6000", 13, LG2))
    return box(AX, AY, AW, AH, "", "".join(p))


if want("AcqMenu"):
    body = scope_view(warning=None, acq_active=True) + "\n" + acq_menu()
    write("AcqMenu.dc.html", "Acquisition menu", body)


# ================================================================ Phosphor
def phosphor_plot():
    zero = 201.0          # CH1 at 1V/div: 50 px per volt, zero on the centre line
    hi, lo = zero - 1.87 * 50, zero + 1.19 * 50
    defs = ('<defs>'
            '<linearGradient id="ph-h" x1="0" y1="0" x2="1" y2="0">'
            '<stop offset="0" stop-color="#00008b" stop-opacity="0"></stop>'
            '<stop offset="0.12" stop-color="#00008b"></stop><stop offset="0.25" stop-color="#0000ff"></stop>'
            '<stop offset="0.36" stop-color="#00ffff"></stop><stop offset="0.43" stop-color="#ffff00"></stop>'
            '<stop offset="0.5" stop-color="#ff0000"></stop><stop offset="0.57" stop-color="#ffff00"></stop>'
            '<stop offset="0.64" stop-color="#00ffff"></stop><stop offset="0.75" stop-color="#0000ff"></stop>'
            '<stop offset="0.88" stop-color="#00008b"></stop>'
            '<stop offset="1" stop-color="#00008b" stop-opacity="0"></stop></linearGradient>'
            '<linearGradient id="ph-v" x1="0" y1="0" x2="0" y2="1">'
            '<stop offset="0" stop-color="#00008b" stop-opacity="0"></stop>'
            '<stop offset="0.2" stop-color="#0000ff"></stop><stop offset="0.35" stop-color="#00ff00"></stop>'
            '<stop offset="0.45" stop-color="#ff0000"></stop><stop offset="0.5" stop-color="#ffffff"></stop>'
            '<stop offset="0.55" stop-color="#ff0000"></stop><stop offset="0.65" stop-color="#00ff00"></stop>'
            '<stop offset="0.8" stop-color="#0000ff"></stop>'
            '<stop offset="1" stop-color="#00008b" stop-opacity="0"></stop></linearGradient>'
            '</defs>')
    parts = [defs]
    level, x = lo, 0
    for ex, kind in EDGES:
        parts.append(f'<rect x="{x}" y="{level - 4.5:.1f}" width="{ex - x}" height="9" fill="url(#ph-v)"></rect>')
        level = hi if kind == "r" else lo
        x = ex
    parts.append(f'<rect x="{x}" y="{level - 4.5:.1f}" width="{GW - x}" height="9" fill="url(#ph-v)"></rect>')
    for ex, kind in EDGES:
        parts.append(f'<rect x="{ex - 8}" y="{hi:.1f}" width="16" height="{lo - hi:.1f}" fill="url(#ph-h)"></rect>')
    parts.append(pointers(201, None, 201 - 17))
    return box(GX, GY, GW, GH, "", scope_grid_svg("".join(parts)))


def phosphor_strip():
    """phosphor.c: STRIP_Y 452, STRIP_H 28, field_x 6, 176, 296, 386 (the Scope field at 386 is gone)"""
    o = [box(2, 452, 726, 28, "background: #000000")]
    for x0, x1, label, val in [(6, 176, "Persistence", "1 s"), (176, 296, "Palette", "Heat"), (296, 386, "", "Clear")]:
        w = x1 - x0 - 4
        if label:
            inner = (f'<span style="display: flex; align-items: baseline; justify-content: center; gap: 6px; '
                     f'height: 100%; padding-top: 2px; box-sizing: border-box">'
                     f'<span style="font-size: 13px; color: #a0a0a0">{label}</span>'
                     f'<span style="font-size: 16px; color: #ffffff">{val}</span></span>')
        else:
            inner = centred([(val, 16, "#ffffff")])
        o.append(abtn(x0, 454, w, 24, inner, f"background: {DARKGREY}; border: 1px solid {GREY}; border-radius: 3px"))
    o.append(text(726 - 70, 456, "55 wfm/s", 16, "#a0a0a0"))
    return "\n".join(o)


if want("Phosphor"):
    body = "\n".join([topbar(ch1_vdiv="1V/div", ch2_on=False), phosphor_plot(),
                      cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")], 5, CH1, "1"), cards([("wfm/s", "55")], 366, "#a0a0a0"),
                      slider_row(), rightbar_actions()])
    write("Phosphor.dc.html", "Phosphor view", body)

# ================================================================ Spectrum (spectrum.c geometry)
SX, SY, SW, SH = 6, 50, 720, 352   # 10 x 8 divisions of 72 x 44; the slider row under it (owner, 19:40)
SPAN_KHZ, NYQ_KHZ = 50.0, 100.0     # shown span 0..50 kHz, zoomed one step from 0..fs/2 at 200 kSa/s


def spectrum_plot():
    ref, scale = 10.0, 10.0

    def ly(level):
        return max(1.0, min(SH - 1.0, (ref - level) / (8 * scale) * SH))

    rnd = random.Random(7)
    pxk = SW / SPAN_KHZ
    peaks = {0: -9.4}
    for n in range(1, int(SPAN_KHZ), 2):
        peaks[n] = 2.7 - 20 * math.log10(n)
    peak_x = {round(n * pxk, 1): lv for n, lv in peaks.items()}
    pts = []
    for xi in range(0, SW + 1):
        x = float(xi)
        lv = -66.0 + rnd.uniform(-2.5, 2.5)
        for px, plv in peak_x.items():
            d = abs(x - px)
            if d < 2.2:
                lv = max(lv, plv - d * d * 6)
        pts.append(f"{x:.1f},{ly(lv):.1f}")
    o = [f'<svg width="{SW}" height="{SH}" viewBox="0 0 {SW} {SH}" style="position: absolute; left: 0; top: 0">',
         f'<rect x="0.5" y="0.5" width="{SW - 1}" height="{SH - 1}" fill="none" stroke="{LG1}" stroke-width="1"></rect>']
    for i in range(1, 10):
        o.append(f'<line x1="{i * 72 + 0.5}" y1="0" x2="{i * 72 + 0.5}" y2="{SH}" stroke="{LG1}" stroke-dasharray="1 4"></line>')
    for j in range(1, 8):
        o.append(f'<line x1="0" y1="{j * 44 + 0.5}" x2="{SW}" y2="{j * 44 + 0.5}" stroke="{LG1}" stroke-dasharray="1 4"></line>')
    o.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="{CH1}" stroke-width="1.5"></polyline>')
    fx, fy = round(pxk, 1), ly(peaks[1])
    o.append(f'<polygon points="{fx - 6},{fy - 14} {fx + 6},{fy - 14} {fx},{fy - 4}" fill="#ffffff"></polygon>')
    o.append('</svg>')
    out = [box(SX, SY, SW, SH, "", "".join(o))]
    for j in range(0, 8):   # the bottom label would sit under the cards
        lv = ref - j * scale
        lab = (f"{lv:+.0f}" if lv != 0 else "0") + (" dBV" if j == 0 else "")
        yy = SY + j * 44 + 2 if j == 0 else SY + j * 44 - 17
        out.append(text(SX + 4, yy, lab, 13, LG2))
    out.append(text(SX, 406, "0 Hz", 13, LG2))
    out.append(text(SX + SW / 2 - 20, 406, "25 kHz", 13, LG2))
    out.append(text(SX + SW - 43, 406, "50 kHz", 13, LG2))
    return "\n".join(out)


def spectrum_strip():
    """spectrum.c: STRIP_Y 436, STRIP_H 44; Auto and Scope fields gone, the remaining seven share the width"""
    fields = [("Channel", "CH1"), ("FFT length", "2048"), ("Window", "Hann"), ("Scale", "10 dB/div"),
              ("Ref level", "+10 dBV"), ("Average", "4"), ("View", "Spectrum"), ("", "Reset")]
    fw = SW // len(fields)
    o = [box(2, 436, 728, 44, "background: #000000")]
    for i, (lab, val) in enumerate(fields):
        x = SX + i * fw
        inner = centred([(lab, 13, LG2), (val, 16, "#ffffff")]) if lab else centred([(val, 16, "#ffffff")])
        if lab == "Ref level":
            inner += text(6, 12, "&lt;", 16) + text(fw - 3 - 16, 12, "&gt;", 16)
        o.append(abtn(x + 1, 439, fw - 3, 38, inner, f"background: #000000; border: 1px solid {GREY}; border-radius: 3px"))
    return "\n".join(o)


def spectrum_slider(start_khz=0.0, span_khz=SPAN_KHZ, nyq_khz=NYQ_KHZ):
    """the Scope's slider row (menu.c: rect 2,452,726,20; the window box 14 px high at y 455) with the shown span as a
    window over 0..fs/2: left edge at start / (fs/2), width span / (fs/2) of the 720 px inside the track; dragging it
    pans the centre, the span stays (owner)"""
    x0 = 5 + start_khz / nyq_khz * 720
    w = max(span_khz / nyq_khz * 720, 12)
    return (box(2, 452, 726, 20, f"border: 1px solid {LG1}") +
            box(round(x0), 455, round(w), 14, f"border: 1px solid {LG2}"))


def spectrum_view(right):
    return "\n".join([topbar(rate="200KSa/s", tb="500us/div", ch2_on=False),
                      spectrum_plot(), spectrum_slider(),   # the slider row: the span over 0..fs/2 (owner)
                      cards([("Peak", "1.000kHz"), ("Level", "+2.7dBV")], 5, CH1, "1"),
                      cards([("RBW", "146Hz")], 245, "#a0a0a0", "S"),
                      right])


if want("Spectrum"):
    write("Spectrum.dc.html", "Spectrum view",
          spectrum_view(rightbar_actions(ctrl_href="SpectrumCtrl.dc.html", spectrum=True)))
if want("SpectrumCtrl"):
    write("SpectrumCtrl.dc.html", "Spectrum controls", spectrum_view(ctrl_spectrum()))

# ================================================================ MainMenu
MENU_ROWS = [("Scope", "Scope.dc.html"), ("Spectrum analyser", "Spectrum.dc.html"), ("Saved pictures", None),
             ("Saved waveforms", None), ("Settings", "SettingsFoldout.dc.html"), ("Diagnostics", "Main.dc.html"),
             ("USB drive", "UsbDrive.dc.html")]


def main_menu(active=None):
    o = [box(2, 46, 147, 7 * 59 + 1, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    for i, (lab, href) in enumerate(MENU_ROWS):
        y = 47 + i * 59
        on = lab == active
        bg = CH1 if on else DARKGREY
        fg = "#000000" if on else "#ffffff"
        inner = (f'<span style="display: flex; align-items: center; height: 100%; padding-left: 12px; '
                 f'font-size: 16px; color: {fg}">{lab}</span>')
        o.append(abtn(3, y, 145, 58, inner, f"background: {bg}; border: none; border-radius: 3px; text-align: left",
                      href=href))
        if i < len(MENU_ROWS) - 1:
            o.append(box(9, y + 58, 133, 1, "background: #000000"))
    return "\n".join(o)


if want("MainMenu"):
    write("MainMenu.dc.html", "Main menu", scope_view(menu_active=True) + "\n" + main_menu())


# ================================================================ SettingsFoldout
def foldout():
    rows = [("Screen brightness", "70", None), ("Grid brightness", "40", None),
            ("File message timeout", "0.7 s", None), ("RTC module", "on", None),
            ("Time and date", "2026-10-19 11:30", None),
            ("Other settings", None, "Settings.dc.html")]
    x0, y0, w = 150, 46, 244
    o = [box(x0, y0, w, len(rows) * 58 + 1, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    for i, (lab, val, href) in enumerate(rows):
        y = y0 + 1 + i * 58
        if val is not None:
            right = (f'<span style="background: {CH1}; color: #000000; font-size: 13px; line-height: 17px; '
                     f'padding: 1px 5px; border-radius: 2px">{val}</span>')
        else:
            right = '<span style="font-size: 16px; color: #ffffff">&gt;</span>'
        inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
                 f'padding: 0 10px 0 12px; font-size: 16px; color: #ffffff"><span>{lab}</span>{right}</span>')
        o.append(abtn(x0 + 1, y, w - 2, 57, inner, f"background: {DARKGREY}; border: none; text-align: left", href=href))
        if i < len(rows) - 1:
            o.append(box(x0 + 9, y + 57, w - 18, 1, "background: #000000"))
    return "\n".join(o)


if want("SettingsFoldout"):
    write("SettingsFoldout.dc.html", "Settings fold-out",
          scope_view(menu_active=True) + "\n" + main_menu(active="Settings") + "\n" + foldout())

# ================================================================ UsbDrive
if want("UsbDrive"):
    o = [box(0, 0, 800, 52, "background: #0b1a33; border-bottom: 1px solid #1f3a66", text(14, 13, "USB drive", 24))]
    o.append(box(470, 110, 250, 180, "background: #aaaaaa; border-radius: 3px", box(7, 9, 236, 162, f"background: {DARKBLUE}")))
    o.append(box(580, 290, 30, 20, "background: #aaaaaa"))
    o.append(box(550, 310, 90, 10, "background: #aaaaaa; border-radius: 2px"))
    o.append(box(80, 190, 180, 130, "background: #aaaaaa; border-radius: 3px", box(8, 10, 164, 110, f"background: {DARKBLUE}")))
    o.append(box(213, 150, 4, 40, "background: #aaaaaa"))
    o.append(box(213, 150, 152, 4, "background: #aaaaaa"))
    o.append(box(361, 150, 4, 106, "background: #aaaaaa"))
    o.append(box(361, 252, 109, 4, "background: #aaaaaa"))
    o.append(text(500, 190, "SD card", 16))
    o.append(text(500, 212, "shown as a drive", 13, LG2))
    o.append(text(110, 238, "Scope", 16))
    o.append(text(110, 260, "PC control paused", 13, LG2))
    for i, s in enumerate(["The SD card is shown to the PC as a drive.",
                           "PC control through the protocol stops until you leave this screen.",
                           "Eject the drive on the PC before you tap Exit."]):
        o.append(text(40, 342 + i * 24, s, 16))
    o.append(dbtn(610, 412, 160, 52, "Exit", href="Scope.dc.html"))
    write("UsbDrive.dc.html", "USB drive", "\n".join(o))

# ================================================================ Wizard (ux-makeover.md 6a: four screens)
DX, DY, DW, DH = 50, 90, 630, 300


def wizard(title, instruction, results, action, action_kind="go", action_href=None, action_disabled=False):
    """title 24 px, the instruction as lines that fit (7.1 px per character at 16 px), result lines, Cancel left
    and the action right; the scope behind is frozen (no warning, static trace)"""
    o = [scope_view(warning=None), dim()]
    parts = [text(24, 20, title, 24)]
    y = 64
    for line in instruction:
        parts.append(text(24, y, line, 16))
        y += 24
    y += 16
    for kind, line, colour in results:
        if kind == "ok":
            parts.append(tick(24, y, True))
        elif kind == "wait":
            parts.append(tick(24, y, False))
        parts.append(text(52 if kind else 24, y, line, 16, colour))
        y += 30
    by = DH - 16 - 52
    parts.append(dbtn(16, by, 160, 52, "Cancel", href="Scope.dc.html"))
    parts.append(dbtn(DW - 16 - 180, by, 180, 52, action, kind=action_kind, href=action_href,
                      disabled=action_disabled))
    o.append(box(DX, DY, DW, DH, f"background: {DARKGREY}; border: 1px solid {LG1}; border-radius: 4px", "".join(parts)))
    return "\n".join(o)


if want("Wizard1"):
    write("Wizard1.dc.html", "Calibration, zero level", wizard(
        "Zero level",
        ["Fully disconnect both probes and the USB cable from the device.", "Tap Calibrate."],
        [("", "USB is connected: unplug it before Calibrate", "#ffd262"),
         ("ok", "CH1: zero level done", "#ffffff"), ("wait", "CH2: measuring range 3 of 6", PALEYEL)],
        "Measuring...", action_kind="off", action_disabled=True))

if want("Wizard2"):
    write("Wizard2.dc.html", "Calibration, CH1 gain", wizard(
        "CH1 gain",
        ["Plug the CH1 probe back in and connect it to the calibration post.",
         "Set it to 1X on the handle. Tap Calibrate."],
        [("ok", "CH1 gain done", "#ffffff")],
        "Continue", action_href="Wizard3.dc.html"))

if want("Wizard3"):
    write("Wizard3.dc.html", "Calibration, CH2 gain", wizard(
        "CH2 gain",
        ["Plug the CH2 probe in and connect it to the calibration post.",
         "Set it to 1X on the handle. Tap Calibrate."],
        [("", "No signal on CH2: tip on the post?", ORANGE)],
        "Calibrate", action_href="Wizard4.dc.html"))

if want("Wizard4"):
    write("Wizard4.dc.html", "Calibration, done", wizard(
        "Done", [],
        [("ok", "CH1 calibrated: cores matched, gain mismatch 1.3 %", CH1),
         ("ok", "CH2 calibrated: cores matched, gain mismatch 0.9 %", CH2)],
        "Save calibration", action_href="Scope.dc.html"))


# ================================================================ Confirm
def diag_header(active):
    o = [box(0, 0, 800, 52, "background: #0b1a33; border-bottom: 1px solid #1f3a66", text(12, 13, "Diagnostics", 24))]
    x = 160
    for lab in ["Status", "Calibration", "Log"]:
        st = ("background: #1f3a66; border: 2px solid #9ecbff; border-radius: 4px; color: #ffffff" if lab == active else
              "background: #0f2447; border: 1px solid #1f3a66; border-radius: 4px; color: #e6e6e6")
        o.append(abtn(x, 4, 112, 44, f'<span style="font-size: 16px">{lab}</span>', st))
        x += 120
    o.append(abtn(696, 4, 96, 44, '<span style="font-size: 16px">Back</span>',
                  "background: #0f2447; border: 1px solid #1f3a66; border-radius: 4px; color: #e6e6e6"))
    return "\n".join(o)


if want("Confirm"):
    o = [diag_header("Calibration")]
    for i in range(6):
        o.append(box(12, 66 + i * 52, 776, 44, "background: #0d0d0d; border: 1px solid #262626; border-radius: 4px"))
    o.append(dim())
    cw, ch = 540, 320
    parts = [text(24, 22, "Clear calibration?", 24),
             text(24, 66, "Zeros and gains are cleared; the volts-per-range factors are kept.", 16),
             text(24, 90, "Readings use defaults until you calibrate again.", 16),
             dbtn(24, ch - 24 - 64, 260, 64, "Cancel", kind="default", href="Calibration.dc.html", size=24),
             dbtn(cw - 24 - 190, ch - 24 - 44, 190, 44, "Clear calibration", kind="danger")]
    o.append(box(130, 80, cw, ch, f"background: {DARKGREY}; border: 1px solid {LG1}; border-radius: 4px", "".join(parts)))
    write("Confirm.dc.html", "Confirm action", "\n".join(o))


# ================================================================ FirstRun
def warn_icon(x, y):
    return ('<svg width="18" height="18" viewBox="0 0 18 18" style="position: absolute; '
            f'left: {x}px; top: {y}px"><circle cx="9" cy="9" r="8" fill="#ffd262"></circle>'
            '<rect x="8" y="4" width="2" height="6" fill="#000000"></rect>'
            '<rect x="8" y="12" width="2" height="2" fill="#000000"></rect></svg>')


if want("FirstRun"):
    o = [box(0, 0, 800, 52, "background: #0b1a33; border-bottom: 1px solid #1f3a66", text(14, 13, "First start", 24))]
    o.append(text(24, 66, "Open firmware 0.2.50, git cdc7e2f, protocol 1.15. FPGA AL3A10, bitstream 0x1535.", 13, "#c9c9c9"))
    o.append(tick(24, 104, True))
    o.append(text(52, 104, "FPGA recognised: bitstream 0x1535 with external trigger and CIC+FIR filters.", 16))
    o.append(tick(24, 140, True))
    o.append(text(52, 140, "No saved settings on the card: defaults used.", 16))
    o.append(warn_icon(24, 176))
    o.append(text(52, 176, "Calibration missing: readings may be several % off.", 16, "#ffd262"))
    o.append(box(24, 222, 752, 1, f"background: {LIGHTGREY}"))
    o.append(text(24, 240, "Calibrate after the scope has been on for 5 minutes.", 16))
    o.append(text(24, 264, "The zero step takes 8 seconds and needs no probes.", 16))
    o.append(text(24, 288, "The gain step needs a probe on the calibration post and the open FPGA bitstream.", 16))
    o.append(text(24, 324, "Later opens the scope. Calibrate is in Menu, Diagnostics.", 13, LG2))
    o.append(dbtn(24, 404, 160, 52, "Later", href="Scope.dc.html"))
    o.append(dbtn(576, 404, 200, 52, "Calibrate now", kind="go", href="Wizard1.dc.html"))
    write("FirstRun.dc.html", "First start", "\n".join(o))


# ================================================================ Developer (from Diagnostics, Log page)
def dev_row(y, label, value, value_colour="#35c26a", href=None, sub=None):
    lab = (f'<span style="display: flex; flex-direction: column"><span>{label}</span>'
           f'<span style="font-size: 13px; color: #9a9a9a">{sub}</span></span>') if sub else f"<span>{label}</span>"
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 12px; font-size: 16px; color: #e6e6e6">{lab}'
             f'<span style="color: {value_colour}">{value}</span></span>')
    return abtn(12, y, 776, 52, inner, "background: #0d0d0d; border: 1px solid #262626; border-radius: 4px; text-align: left",
                href=href)


DEV_ROWS = lambda link: [
    dev_row(68, "Debug overlay", "off", "#9a9a9a", sub="Trigger address and touch coordinates over the top bar"),
    dev_row(128, "Enter FEL now", "asks first", "#ffd262", href="ConfirmFel.dc.html" if link else None)]

if want("Developer"):
    o = [box(0, 0, 800, 52, "background: #0b1a33; border-bottom: 1px solid #1f3a66", text(12, 13, "Developer", 24)),
         text(160, 18, "For firmware work. Normal use needs none of this.", 13, "#c9d6ea"),
         abtn(696, 4, 96, 44, '<span style="font-size: 16px">Back</span>',
              "background: #0f2447; border: 1px solid #1f3a66; border-radius: 4px; color: #e6e6e6", href="Log.dc.html")]
    o += DEV_ROWS(True)
    write("Developer.dc.html", "Developer", "\n".join(o))

if want("ConfirmFel"):
    o = [box(0, 0, 800, 52, "background: #0b1a33; border-bottom: 1px solid #1f3a66", text(12, 13, "Developer", 24))]
    o += DEV_ROWS(False)
    o.append(dim())
    cw, ch = 540, 320
    parts = [text(24, 22, "Enter FEL now?", 24),
             text(24, 66, "The scope restarts into the USB boot loader at once.", 16),
             text(24, 90, "The screen stays dark until a PC loads firmware with sunxi-fel.", 16),
             text(24, 114, "A power cycle boots the card as usual.", 16),
             dbtn(24, ch - 24 - 64, 260, 64, "Cancel", kind="default", href="Developer.dc.html", size=24),
             dbtn(cw - 24 - 190, ch - 24 - 44, 190, 44, "Enter FEL", kind="go")]
    o.append(box(130, 80, cw, ch, f"background: {DARKGREY}; border: 1px solid {LG1}; border-radius: 4px", "".join(parts)))
    write("ConfirmFel.dc.html", "Confirm FEL", "\n".join(o))


# ================================================================ ScopeCtrlDisplay: Display fold-out, Phosphor chosen
if want("ScopeCtrlDisplay"):
    px, py, pw = 528, 52, 206
    rows = [frow(8, 30, pw - 16, "Normal", sel=True, icon=ICON_NORMAL),
            frow(8, 80, pw - 16, "X-Y", icon=ICON_XY),
            box(8, 134, pw - 16, 1, f"background: {GREY}"),
            switch_row(8, 142, pw - 16, "Phosphor", True, icon=ICON_PHOSPHOR),
            frow(8, 192, pw - 16, "Persistence", "1 s"),
            frow(8, 242, pw - 16, "Palette", "Mono"),
            frow(8, 292, pw - 16, "Clear")]
    tb = topbar(ch1_vdiv="1V/div", ch2_on=False)
    body = "\n".join([tb, phosphor_plot(), cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")], 5, CH1, "1"), cards([("wfm/s", "55")], 366, "#a0a0a0"),
                      slider_row(), ctrl_scope(active="display", phosphor=True),
                      foldout_panel(px, py, pw, 344, "Display", "".join(rows))])
    write("ScopeCtrlDisplay.dc.html", "Scope controls, Display", body)

# ================================================================ SpectrumCtrlAnalysis: Analysis fold-out
if want("SpectrumCtrlAnalysis"):
    px, py, pw = 506, 52, 228
    rows = [frow(8, 30, pw - 16, "Analyse", "CH1"),
            frow(8, 80, pw - 16, "FFT length", "2048"),
            frow(8, 130, pw - 16, "Window", "Hann"),
            frow(8, 180, pw - 16, "Average", "4"),
            frow(8, 230, pw - 16, "Scale", "10 dB/div"),
            frow(8, 280, pw - 16, "Ref level", "+10 dBV"),
            frow(8, 330, pw - 16, "Reset")]
    body = spectrum_view(ctrl_spectrum(active="analysis")) + "\n" + foldout_panel(px, py, pw, 382, "Analyse", "".join(rows))
    write("SpectrumCtrlAnalysis.dc.html", "Spectrum controls, Analysis", body)


# ================================================================ SpectrumCtrlDisplay: Display fold-out
if want("SpectrumCtrlDisplay"):
    px, py, pw = 528, 40, 206
    rows = [frow(8, 30, pw - 16, "Spectrum", sel=True, icon=ICON_SPECTRUM),
            frow(8, 80, pw - 16, "Waterfall", icon=ICON_WATERFALL),
            frow(8, 130, pw - 16, "Density", icon=ICON_DENSITY)]
    body = spectrum_view(ctrl_spectrum(active="display")) + "\n" + foldout_panel(px, py, pw, 182, "Display", "".join(rows))
    write("SpectrumCtrlDisplay.dc.html", "Spectrum controls, Display", body)
