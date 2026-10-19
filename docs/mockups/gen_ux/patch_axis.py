"""Axis folded into Analyse; the Spectrum CTRL page takes the Scope page's positions."""

p = "boards.py"
s = open(p, encoding="utf-8").read()
a = s.index("def ctrl_spectrum(active=None):")
b = s.index("\n\n", s.index("        trig,\n    ])", a)) + 2
new = '''def ctrl_spectrum(active=None):
    """the Scope page's positions and sizes: CTRL 5, Display 65, Analyse 125, Grid 185 (stock 51 x 50, 60 px pitch),
    82 px steppers at 244 and 334, 50 % TRIG at 425"""
    k = lambda n: "active" if active == n else "normal"
    return "\\n".join([
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

'''
s = s[:a] + new + s[b:]

old = '''    px, py, pw = 520, 110, 214
    rows = [frow(8, 30, pw - 16, "Analyse", "CH1"),
            frow(8, 80, pw - 16, "FFT length", "2048"),
            frow(8, 130, pw - 16, "Window", "Hann"),
            frow(8, 180, pw - 16, "Average", "4"),
            frow(8, 230, pw - 16, "Reset")]
    body = spectrum_view(ctrl_spectrum(active="analysis")) + "\\n" + foldout_panel(px, py, pw, 282, "Analyse", "".join(rows))'''
assert s.count(old) == 1
s = s.replace(old, '''    px, py, pw = 506, 52, 228
    rows = [frow(8, 30, pw - 16, "Analyse", "CH1"),
            frow(8, 80, pw - 16, "FFT length", "2048"),
            frow(8, 130, pw - 16, "Window", "Hann"),
            frow(8, 180, pw - 16, "Average", "4"),
            frow(8, 230, pw - 16, "Scale", "10 dB/div"),
            frow(8, 280, pw - 16, "Ref level", "+10 dBV"),
            frow(8, 330, pw - 16, "Reset")]
    body = spectrum_view(ctrl_spectrum(active="analysis")) + "\\n" + foldout_panel(px, py, pw, 382, "Analyse", "".join(rows))''')
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
