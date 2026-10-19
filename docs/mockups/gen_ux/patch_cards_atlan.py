"""Cards: Atlan layout built like the top-bar chips; standard measurement names."""

p = "common.py"
s = open(p, encoding="utf-8").read()
a = s.index("def cards(items, xstart, color, digit):")
b = s.index("CH1_CARDS = ")
new = '''def cards(items, xstart, color, digit=None):
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
    return "\\n".join(o)


'''
s = s[:a] + new + s[b:]


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('CH1_CARDS = [("Vmin", "-1.19V"), ("Freq", "1.000kHz"), ("Duty+", "50.0%")]',
    'CH1_CARDS = [("Vmin", "-1.19V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")]')
rep('CH2_CARDS = [("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("Time+", "500us")]',
    'CH2_CARDS = [("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Width", "500us")]')
open(p, "w", encoding="utf-8", newline="\n").write(s)

p = "boards.py"
t = open(p, encoding="utf-8").read()
old = 'cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("Duty+", "50.0%")], 5, CH1, "1")'
assert t.count(old) == 2, t.count(old)
t = t.replace(old, 'cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("+Duty", "50.0%")], 5, CH1, "1")')
open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched")
