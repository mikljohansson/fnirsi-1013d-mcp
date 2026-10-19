"""Last review items (plan after 4d65b15) and Move fast leaving the top bar."""


def patch(path, pairs):
    s = open(path, encoding="utf-8").read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:80], s.count(a))
        s = s.replace(a, b)
    open(path, "w", encoding="utf-8", newline="\n").write(s)
    print("patched", path)


patch("common.py", [
    ('''    # Move fast stays (owner)
    o.append(abtn(496, 5, 44, 35, text(5, 1, "Move", 13) + text(10, 17, "fast", 13),
                  f"background: {DARKBLUE}; border: none; border-radius: 2px; text-align: left"))
''', '''    # Move fast left the top bar for Settings > Controls > Drag speed (owner): x 496 stays empty
'''),
    ('''    o.append(rbtn(365, [("SAVE", 16), ("PIC", 16)]))
    if not spectrum:
        o.append(rbtn(425, [("SAVE", 16), ("WAVE", 16)]))''',
     '''    o.append(rbtn(365, [("SAVE", 16), ("PIC", 16)]))
    o.append(rbtn(425, [("SAVE", 16), ("WAVE", 16)]))   # in Spectrum it saves the time record'''),
    ('CH1_CARDS = [("VMIN", "-1.19V"), ("Freq", "1.00kHz"), ("Duty+", "50.0%")]',
     'CH1_CARDS = [("Vmin", "-1.19V"), ("Freq", "1.000kHz"), ("Duty+", "50.0%")]'),
    ('CH2_CARDS = [("VPP", "3.06V"), ("Freq", "1.00kHz")]',
     'CH2_CARDS = [("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("Time+", "500us")]'),
])

PH = 'cards([("VPP", "3.06V"), ("Freq", "1.00kHz"), ("Duty+", "50.0%")], 5, CH1, "1")'
PHN = 'cards([("Vpp", "3.06V"), ("Freq", "1.000kHz"), ("Duty+", "50.0%")], 5, CH1, "1")'
s = open("boards.py", encoding="utf-8").read()
assert s.count(PH) == 2
s = s.replace(PH, PHN)
open("boards.py", "w", encoding="utf-8", newline="\n").write(s)

s = open("boards.py", encoding="utf-8").read()
a = s.index("def wizard(title, instruction, results")
b = s.index("# ================================================================ Confirm")
wiz = r'''def wizard(title, instruction, results, action, action_kind="go", action_href=None, action_disabled=False):
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


'''
s = s[:a] + wiz + s[b:]

a = s.index("def dev_row(")
b = s.index("# ================================================================ ScopeCtrlVdiv")
dev = r'''def dev_row(y, label, value, value_colour="#35c26a", href=None, sub=None):
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
             dbtn(cw - 24 - 190, ch - 24 - 44, 190, 44, "Enter FEL", kind="danger")]
    o.append(box(130, 80, cw, ch, f"background: {DARKGREY}; border: 1px solid {LG1}; border-radius: 4px", "".join(parts)))
    write("ConfirmFel.dc.html", "Confirm FEL", "\n".join(o))


'''
s = s[:a] + dev + s[b:]
open("boards.py", "w", encoding="utf-8", newline="\n").write(s)

patch("boards.py", [
    ('def ctrl_scope(active=None, display_val="Trace", back_href="Scope.dc.html"):',
     'def ctrl_scope(active=None, display_val="Normal", back_href="Scope.dc.html"):'),
    ('rows = [frow(8, 30, pw - 16, "Trace", href="ScopeCtrl.dc.html"),',
     'rows = [frow(8, 30, pw - 16, "Normal", href="ScopeCtrl.dc.html"),'),
    ('''    p.append(text(310, 112, "Filter on:", 13, LG2))''',
     '''    p.append(text(310, 104, "pass 15 ms", 13, "#ffffff"))   # today's pass-time line under Memory
    p.append(text(310, 126, "Filter on:", 13, LG2))'''),
    ('''    p.append(text(310, 130, "up to 6000", 13, LG2))''',
     '''    p.append(text(310, 144, "up to 6000", 13, LG2))'''),
])
