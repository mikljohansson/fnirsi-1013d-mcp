"""Review round 3: Analyse row, spectrum without slider, wfm/s card."""

p = "boards.py"
s = open(p, encoding="utf-8").read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)


# (1) Analysis fold-out gains Analyse; the CTRL button reads Analyse
rep('vbtn_at(ys["analysis"], 50, ["Analysis"], "2048 pt"', 'vbtn_at(ys["analysis"], 50, ["Analyse"], "2048 pt"')
rep('''    rows = [frow(8, 30, pw - 16, "FFT length", "2048"),
            frow(8, 80, pw - 16, "Window", "Hann"),
            frow(8, 130, pw - 16, "Average", "4"),
            frow(8, 180, pw - 16, "Reset")]''',
    '''    rows = [frow(8, 30, pw - 16, "Analyse", "CH1"),
            frow(8, 80, pw - 16, "FFT length", "2048"),
            frow(8, 130, pw - 16, "Window", "Hann"),
            frow(8, 180, pw - 16, "Average", "4"),
            frow(8, 230, pw - 16, "Reset")]''')
rep('foldout_panel(px, py, pw, 232, "Analysis", "".join(rows))', 'foldout_panel(px, py, pw, 282, "Analyse", "".join(rows))')

# (3) spectrum: no slider row; the plot takes the space (8 divisions of 49 px), frequency labels under it
rep("SX, SY, SW, SH = 6, 50, 720, 352   # 10 x 8 divisions of 72 x 44 (the header line is gone)",
    "SX, SY, SW, SH = 6, 50, 720, 392   # 10 x 8 divisions of 72 x 49 (no header, no slider row)")
rep("j * 44", "j * 49", 4)
rep("    for j in range(0, 9):", "    for j in range(0, 8):   # the bottom label would sit under the cards")
for lab in ['"0 Hz"', '"50 kHz"', '"100 kHz"']:
    i = s.index(lab)
    a = s.rindex("405", 0, i)
    s = s[:a] + "446" + s[a + 3:]
rep('''                      spectrum_plot(),''', '''                      spectrum_plot(),   # no slider row in the spectrum''')
rep('''                      slider_row(), right])''', '''                      right])''')

# (4) wfm/s as a card with the neutral grey block; the Display fold-out no longer shows it
old = 'cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")], 5, CH1, "1")'
rep(old, old + ', cards([("wfm/s", "55")], 366, "#a0a0a0")', 2)
rep('''            text(10, 368, "55 wfm/s", 16, "#a0a0a0")]''', ''']''')
rep('foldout_panel(px, py, pw, 396, "Display", "".join(rows))', 'foldout_panel(px, py, pw, 366, "Display", "".join(rows))')
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
