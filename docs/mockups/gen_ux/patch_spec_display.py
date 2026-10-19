"""Spectrum CTRL: View renamed Display, with its own fold-out board and icons."""

p = "boards.py"
s = open(p, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''        vbtn_at(ys["view"], 50, ["View"], "Spec", k("view")),''',
    '''        vbtn_at(ys["view"], 50, ["Display"], "Spec", k("display"), href="SpectrumCtrlDisplay.dc.html"),''')

icons = '''
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

'''
rep("def foldout_panel(", icons + "def foldout_panel(")

s += '''

# ================================================================ SpectrumCtrlDisplay: Display fold-out
if want("SpectrumCtrlDisplay"):
    px, py, pw = 528, 40, 206
    rows = [frow(8, 30, pw - 16, "Spectrum", sel=True, icon=ICON_SPECTRUM),
            frow(8, 80, pw - 16, "Waterfall", icon=ICON_WATERFALL),
            frow(8, 130, pw - 16, "Density", icon=ICON_DENSITY)]
    body = spectrum_view(ctrl_spectrum(active="display")) + "\\n" + foldout_panel(px, py, pw, 182, "Display", "".join(rows))
    write("SpectrumCtrlDisplay.dc.html", "Spectrum controls, Display", body)
'''
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
