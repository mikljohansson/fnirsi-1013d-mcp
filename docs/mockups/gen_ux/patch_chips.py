"""Top bar chips: grey parameter box behind the icon block, no notch; stepper label without the space."""

p = "common.py"
s = open(p, encoding="utf-8").read()
a = s.index("    def chan(x, name, color, on, coupling, probe, vdiv):")
b = s.index("    # Move fast stays in the top bar at its stock slot (owner)")
new_a = '''    def chip(x, w, icon_w, icon_bg, icon_html, params_html, href=None):
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
'''
s = s[:a] + new_a + s[b:]

a = s.index("    edge = (")
end_marker = '    o.append(abtn(546, 5, 80, 35, inner, "background: #000000; border: none; text-align: left"))'
b = s.index(end_marker) + len(end_marker)
new_t = '''    edge = ('<svg width="12" height="15" viewBox="0 0 12 15" style="position: absolute; left: 66px; top: 19px">'
            '<polyline points="1,13 6,13 6,2 11,2" fill="none" stroke="#ffffff" stroke-width="1.5"></polyline></svg>')
    o.append(chip(546, 80, 31, TRIG, text(11, 8, "T", 16, "#000000"),
                  text(36, 1, "Auto", 13) +
                  box(35, 19, 28, 14, "background: #cccc00; border-radius: 2px", text(2, -1, "CH1", 13, "#000000")) + edge))'''
s = s[:a] + new_t + s[b:]
open(p, "w", encoding="utf-8", newline="\n").write(s)

p = "boards.py"
s = open(p, encoding="utf-8").read()
old = 'centred([(f"{digit}: {value}", 13, "#000000"), ("/div", 13, "#000000")]))'
assert s.count(old) == 1
s = s.replace(old, 'centred([(f"{digit}:{value}", 13, "#000000"), ("/div", 13, "#000000")]))')
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
