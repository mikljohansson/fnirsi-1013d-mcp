"""Apply the owner's final CTRL layout (ux-makeover.md section 3, final 2026-10-19) to boards.py."""

P = "boards.py"
s = open(P, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# ScopeCtrl: new CTRL page
rep('''if want("ScopeCtrl"):
    write("ScopeCtrl.dc.html", "Scope controls", scope_view(right=rightbar_ctrl()))''',
    '''if want("ScopeCtrl"):
    write("ScopeCtrl.dc.html", "Scope controls", scope_view(right=ctrl_scope()))''')

# AcqMenu: Trace style and Display columns go, stock width
rep("AX, AY, AW, AH = 250, 48, 464, 386   # stock 380 wide; the Display column widens it by 84 px",
    "AX, AY, AW, AH = 250, 48, 380, 386   # stock width: Trace style left for the CTRL page")
a = s.index('    p.append(text(325, 216, "Trace", 16))')
b = s.index('    return box(AX, AY, AW, AH, "", "".join(p))')
s = s[:a] + s[b:]

# Phosphor: no strip, the stock slider row
rep("phosphor_strip(), rightbar_actions()])", "slider_row(), rightbar_actions()])")

# Spectrum: no strip, slider row stays; SpectrumCtrl gets the new page
rep('text(370, 48, "CIC2+FIR", 16), spectrum_plot(), spectrum_strip(), right])',
    'text(370, 48, "CIC2+FIR", 16), spectrum_plot(), slider_row(), right])')
rep('''    write("SpectrumCtrl.dc.html", "Spectrum controls", spectrum_view(rightbar_ctrl(back_href="Spectrum.dc.html")))''',
    '''    write("SpectrumCtrl.dc.html", "Spectrum controls", spectrum_view(ctrl_spectrum()))''')

# CTRL page helpers go before the ScopeCtrl section
helpers = r'''
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


def ctrl_scope(active=None, display_val="Trace", back_href="Scope.dc.html"):
    k = lambda n: "active" if active == n else "normal"
    return "\n".join([
        box(730, 0, 70, 480, "background: #000000"),
        rbtn(5, [("CTRL", 16)], kind="active", href=back_href),
        vbtn(65, ["Display"], display_val, k("display"), href="ScopeCtrlDisplay.dc.html"),
        vbtn(125, ["Trace", "style"], "Linear", k("trace")),
        vbtn(185, ["Grid"], "on", k("grid")),
        vbtn(245, ["CH1", "V/div"], "500mV", k("ch1"), href="ScopeCtrlVdiv.dc.html",
             label_colour=None if active == "ch1" else CH1),
        vbtn(305, ["CH2", "V/div"], "1V", k("ch2"), label_colour=CH2),
        rbtn(365, [("50%", 16), ("TRIG", 16)]),
    ])


def ctrl_spectrum(active=None):
    k = lambda n: "active" if active == n else "normal"
    return "\n".join([
        box(730, 0, 70, 480, "background: #000000"),
        rbtn(5, [("CTRL", 16)], kind="active", href="Spectrum.dc.html"),
        vbtn(65, ["View"], "Spec", k("view")),
        vbtn(125, ["Analysis"], "2048 pt", k("analysis"), href="SpectrumCtrlAnalysis.dc.html"),
        vbtn(185, ["Axis"], "10dB", k("axis")),
        vbtn(245, ["Grid"], "on", k("grid")),
        vbtn(305, ["CH1", "V/div"], "500mV", k("ch1"), label_colour=CH1),
        vbtn(365, ["CH2", "V/div"], "1V", k("ch2"), label_colour=CH2),
        rbtn(425, [("50%", 16), ("TRIG", 16)]),
    ])


def foldout_panel(x, y, w, h, title, inner):
    """small panel beside the right bar, stock menu colours (dark grey, lighter grey edge)"""
    return box(x, y, w, h, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}",
               text(10, 8, title, 13, LG2) + inner)


def frow(x, y, w, label, value=None, sel=False, href=None):
    """44 px fold-out row: label left, value right; selected rows green with black text"""
    fg = "#000000" if sel else "#ffffff"
    val = f'<span style="color: {"#000000" if sel else PALEYEL}">{value}</span>' if value is not None else ""
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: {fg}"><span>{label}</span>{val}</span>')
    return abtn(x, y, w, 44, inner, f"background: {GREEN if sel else LIGHTGREY}; border: none; border-radius: 3px; "
                "text-align: left", href=href)

'''
rep("# ================================================================ ScopeCtrl (second page as today)",
    helpers + "\n# ================================================================ ScopeCtrl (CTRL page, final layout)")

# new boards at the end
s += r'''

# ================================================================ ScopeCtrlVdiv: CH1 V/div fold-out
if want("ScopeCtrlVdiv"):
    px, py, pw, ph = 548, 214, 186, 156
    inner = (text(10, 30, "500mV/div", 24, CH1) +
             abtn(10, 64, 78, 44, centred([("-", 24, "#ffffff")]), f"background: {LIGHTGREY}; border: none; border-radius: 3px") +
             abtn(98, 64, 78, 44, centred([("+", 24, "#ffffff")]), f"background: {LIGHTGREY}; border: none; border-radius: 3px") +
             text(10, 120, "Probe 1X (CH1 menu)", 13, LG2))
    body = scope_view(right=ctrl_scope(active="ch1")) + "\n" + foldout_panel(px, py, pw, ph, "CH1 V/div", inner)
    write("ScopeCtrlVdiv.dc.html", "Scope controls, CH1 V/div", body)

# ================================================================ ScopeCtrlDisplay: Display fold-out, Phosphor chosen
if want("ScopeCtrlDisplay"):
    px, py, pw = 528, 52, 206
    rows = [frow(8, 30, pw - 16, "Trace", href="ScopeCtrl.dc.html"),
            frow(8, 80, pw - 16, "Phosphor", sel=True),
            frow(8, 130, pw - 16, "X-Y"),
            box(8, 184, pw - 16, 1, f"background: {GREY}"),
            text(10, 192, "Phosphor", 13, LG2),
            frow(8, 214, pw - 16, "Persistence", "1 s"),
            frow(8, 264, pw - 16, "Palette", "Mono"),
            frow(8, 314, pw - 16, "Clear"),
            text(10, 368, "55 wfm/s", 16, "#a0a0a0")]
    tb = topbar(ch1_vdiv="1V/div", ch2_on=False)
    body = "\n".join([tb, phosphor_plot(), cards([("VPP", "3.06V"), ("Freq", "1.00kHz"), ("Duty+", "50.0%")], 5, CH1, "1"),
                      slider_row(), ctrl_scope(active="display", display_val="Phos"),
                      foldout_panel(px, py, pw, 396, "Display", "".join(rows))])
    write("ScopeCtrlDisplay.dc.html", "Scope controls, Display", body)

# ================================================================ SpectrumCtrlAnalysis: Analysis fold-out
if want("SpectrumCtrlAnalysis"):
    px, py, pw = 520, 110, 214
    rows = [frow(8, 30, pw - 16, "FFT length", "2048"),
            frow(8, 80, pw - 16, "Window", "Hann"),
            frow(8, 130, pw - 16, "Average", "4"),
            frow(8, 180, pw - 16, "Reset")]
    body = spectrum_view(ctrl_spectrum(active="analysis")) + "\n" + foldout_panel(px, py, pw, 232, "Analysis", "".join(rows))
    write("SpectrumCtrlAnalysis.dc.html", "Spectrum controls, Analysis", body)
'''
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("boards.py patched")
