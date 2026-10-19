# ================================================================ Wizard (ux-makeover.md 6a: four screens)
DX, DY, DW, DH = 50, 90, 630, 300


def wizard(title, instruction, results, action, action_kind="go", action_href=None, action_disabled=False):
    """title 24 px, one instruction 16 px, progress/result lines, Cancel left and the action right"""
    o = [scope_view(warning=None), dim()]
    parts = [text(24, 20, title, 24),
             f'<div style="position: absolute; left: 24px; top: 64px; width: {DW - 48}px; font-size: 16px; '
             f'line-height: 22px; color: #ffffff">{instruction}</div>']
    y = 140
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
        "Fully disconnect both probes and the USB cable from the device. Tap Calibrate.",
        [("ok", "CH1: zero level done", "#ffffff"), ("wait", "CH2: measuring range 3 of 6", PALEYEL)],
        "Calibrate", action_kind="off", action_disabled=True))

if want("Wizard2"):
    write("Wizard2.dc.html", "Calibration, CH1 gain", wizard(
        "CH1 gain",
        "Plug the CH1 probe back in and connect it to the calibration post. Set it to 1X on the handle. Tap Calibrate.",
        [("ok", "CH1 gain done", "#ffffff")],
        "Continue", action_href="Wizard3.dc.html"))

if want("Wizard3"):
    write("Wizard3.dc.html", "Calibration, CH2 gain", wizard(
        "CH2 gain",
        "Plug the CH2 probe in and connect it to the calibration post. Set it to 1X on the handle. Tap Calibrate.",
        [("", "No signal on CH2: tip on the post?", ORANGE)],
        "Calibrate", action_href="Wizard4.dc.html"))

if want("Wizard4"):
    write("Wizard4.dc.html", "Calibration, done", wizard(
        "Done",
        "Both channels are measured. Save calibration keeps the result.",
        [("ok", "CH1: cores matched to 0.2 code, gain mismatch 1.3 %", CH1),
         ("ok", "CH2: cores matched to 0.2 code, gain mismatch 0.9 %", CH2)],
        "Save calibration", action_href="Scope.dc.html"))


