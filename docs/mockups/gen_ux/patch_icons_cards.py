"""Display row icons; spectrum header replaced by bottom-row cards."""
import math

p = "boards.py"
s = open(p, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


# ---- frow with an optional icon
rep('''def frow(x, y, w, label, value=None, sel=False, href=None):
    """44 px fold-out row: label left, value right; selected rows green with black text"""
    fg = "#000000" if sel else "#ffffff"
    val = f'<span style="color: {"#000000" if sel else PALEYEL}">{value}</span>' if value is not None else ""
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: {fg}"><span>{label}</span>{val}</span>')''',
    '''def frow(x, y, w, label, value=None, sel=False, href=None, icon=""):
    """44 px fold-out row: optional icon and label left, value right; selected rows green with black text"""
    fg = "#000000" if sel else "#ffffff"
    val = f'<span style="color: {"#000000" if sel else PALEYEL}">{value}</span>' if value is not None else ""
    lab = f'<span style="display: flex; align-items: center; gap: 8px">{icon}<span>{label}</span></span>'
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: {fg}">{lab}{val}</span>')''')

# ---- the three Display icons (28 x 20, stroke in the row's text colour)
WAVE = "M2,10 C6,1 10,1 14,10 S22,19 26,10"
pts = " ".join(f"{14 + 11 * math.sin(t / 40 * 2 * math.pi):.1f},{10 + 7 * math.sin(2 * t / 40 * 2 * math.pi):.1f}"
               for t in range(41))
icons = f'''
SVG0 = '<svg width="28" height="20" viewBox="0 0 28 20" aria-hidden="true" style="flex: none">'
ICON_NORMAL = SVG0 + '<path d="{WAVE}" fill="none" stroke="currentColor" stroke-width="2"></path></svg>'
ICON_PHOSPHOR = (SVG0 + '<path d="{WAVE}" fill="none" stroke="currentColor" stroke-width="8" stroke-opacity="0.2"></path>'
                 '<path d="{WAVE}" fill="none" stroke="currentColor" stroke-width="4" stroke-opacity="0.45"></path>'
                 '<path d="{WAVE}" fill="none" stroke="currentColor" stroke-width="1.5"></path></svg>')
ICON_XY = (SVG0 + '<g transform="rotate(-20 14 10)"><polyline points="{pts}" fill="none" stroke="currentColor" '
           'stroke-width="2"></polyline></g></svg>')

'''
rep("def foldout_panel(", icons + "def foldout_panel(")
rep('''    rows = [frow(8, 30, pw - 16, "Normal", href="ScopeCtrl.dc.html"),
            frow(8, 80, pw - 16, "Phosphor", sel=True),
            frow(8, 130, pw - 16, "X-Y"),''',
    '''    rows = [frow(8, 30, pw - 16, "Normal", href="ScopeCtrl.dc.html", icon=ICON_NORMAL),
            frow(8, 80, pw - 16, "Phosphor", sel=True, icon=ICON_PHOSPHOR),
            frow(8, 130, pw - 16, "X-Y", icon=ICON_XY),''')

# ---- spectrum: header gone, plot takes the space, readouts as cards
rep("SX, SY, SW, SH = 6, 66, 720, 344   # 10 x 8 divisions of 72 x 43",
    "SX, SY, SW, SH = 6, 50, 720, 352   # 10 x 8 divisions of 72 x 44 (the header line is gone)")
rep('''        o.append(f'<line x1="0" y1="{j * 43 + 0.5}" x2="{SW}" y2="{j * 43 + 0.5}" stroke="{LG1}" stroke-dasharray="1 4"></line>')''',
    '''        o.append(f'<line x1="0" y1="{j * 44 + 0.5}" x2="{SW}" y2="{j * 44 + 0.5}" stroke="{LG1}" stroke-dasharray="1 4"></line>')''')
rep("        yy = SY + j * 43 + 2 if j == 0 else SY + j * 43 - 17",
    "        yy = SY + j * 44 + 2 if j == 0 else SY + j * 44 - 17")
rep('    out.append(text(SX, 413, "0 Hz", 13, LG2))', '    out.append(text(SX, 405, "0 Hz", 13, LG2))')
rep('    out.append(text(SX + SW / 2 - 20, 413, "50 kHz", 13, LG2))', '    out.append(text(SX + SW / 2 - 20, 405, "50 kHz", 13, LG2))')
rep('    out.append(text(SX + SW - 50, 413, "100 kHz", 13, LG2))', '    out.append(text(SX + SW - 50, 405, "100 kHz", 13, LG2))')
rep('''                      text(6, 48, "CH1 ^ 1.000 kHz +2.7 dBV", 16, CH1), text(250, 48, "RBW 146 Hz", 16),
                      text(370, 48, "CIC2+FIR", 16), spectrum_plot(), slider_row(), right])''',
    '''                      spectrum_plot(),
                      cards([("Peak", "1.000kHz"), ("Level", "+2.7dBV")], 5, CH1, "1"),
                      cards([("RBW", "146Hz")], 245, "#a0a0a0", "S"),
                      slider_row(), right])''')
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
