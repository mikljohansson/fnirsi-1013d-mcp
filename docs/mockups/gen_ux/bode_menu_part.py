# ================================================================ MainMenu: eight rows at a 52 px pitch (fw 0.2.68)
def mm_icon(key):
    """menu.c's line icons at (MAIN_MENU_XPOS + 15, button top), 1 px; the bitmap icons (pictures, waveforms, the
    settings spanner, diagnostics, USB) as stand-ins of the same size"""
    w = 'stroke="#ffffff" stroke-width="1" fill="none"'
    if key == "Scope":
        sine = [19, 16, 14, 12, 10, 9, 9, 9, 10, 12, 14, 16, 19, 21, 23, 25, 27, 28, 28, 28, 27, 25, 23, 21, 19]
        return ('<line x1="0" y1="19.5" x2="25" y2="19.5" stroke="#707070" stroke-width="1"></line>'
                '<line x1="12.5" y1="8" x2="12.5" y2="30" stroke="#707070" stroke-width="1"></line>'
                f'<polyline points="{" ".join(f"{i + 0.5},{v + 0.5}" for i, v in enumerate(sine))}" {w}></polyline>')
    if key == "Spectrum":
        bars = [4, 7, 22, 9, 5, 14, 4, 6, 10, 3, 5, 2]
        return (f'<line x1="0" y1="30.5" x2="26" y2="30.5" {w}></line>' +
                "".join(f'<line x1="{1.5 + 2 * i}" y1="{29 - b}" x2="{1.5 + 2 * i}" y2="30" {w}></line>'
                        for i, b in enumerate(bars)))
    if key == "Bode":   # scope_main_menu_bode_icon: magnitude above phase, flat then falling
        return (f'<polyline points="0.5,8.5 10.5,8.5 24.5,22.5" {w}></polyline>'
                f'<polyline points="0.5,15.5 5.5,15.5 19.5,29.5 24.5,29.5" {w}></polyline>')
    if key == "Pictures":
        return (f'<rect x="0.5" y="6.5" width="23" height="18" {w}></rect>'
                f'<polyline points="2,23 8,15 12,20 15,17 22,23" {w}></polyline>'
                '<circle cx="18" cy="11" r="2" fill="#ffffff"></circle>')
    if key == "Waveforms":
        return (f'<path d="M2.5,6.5 L16.5,6.5 L22.5,12.5 L22.5,29.5 L2.5,29.5 Z" {w}></path>'
                f'<polyline points="5,21 8,21 8,15 12,15 12,24 15,24 15,18 19,18" {w}></polyline>')
    if key == "Settings":   # Other_settings_icon, a spanner (fw 0.2.68)
        return ('<path d="M18,7 a5,5 0 1,0 3,8 l-3,-1 l-1,-3 l2,-2 Z" fill="#ffffff"></path>'
                '<line x1="17" y1="15" x2="5" y2="27" stroke="#ffffff" stroke-width="3" stroke-linecap="round"></line>')
    if key == "Diagnostics":
        return (f'<rect x="0.5" y="5.5" width="23" height="22" rx="2" {w}></rect>'
                f'<polyline points="3,17 8,17 10,11 13,23 16,14 18,17 21,17" {w}></polyline>')
    return (f'<line x1="11.5" y1="5" x2="11.5" y2="29" {w}></line><polyline points="11.5,23 4.5,16 4.5,12" {w}></polyline>'
            f'<polyline points="11.5,19 18.5,13 18.5,10" {w}></polyline><circle cx="11.5" cy="30" r="2" fill="#ffffff"></circle>'
            '<polygon points="8.5,6 14.5,6 11.5,1" fill="#ffffff"></polygon>')


MM_ROWS = [("Scope", ["Scope"], None), ("Spectrum", ["Spectrum", "analyser"], None),
           ("Bode", ["Frequency", "response"], "Main.dc.html"), ("Pictures", ["Saved", "pictures"], None),
           ("Waveforms", ["Saved", "waveforms"], None), ("Settings", ["Settings"], None),
           ("Diagnostics", ["Diagnostics"], None), ("USB", ["USB drive"], None)]


def board_main_menu():
    """ui_menu.h fw 0.2.68: the column at x 2..148 from y 46, 52 px per row (y 46..461); the button 131 x 35 at x 9,
    9 px below the row top; black separators; the row's hit area is the whole 52 px; the icon at x 17, the label centred
    at x 95 (two lines at +1 and +17, one at +9)"""
    pitch, top, n = 52, 46, len(MM_ROWS)
    o = [box(2, top, 147, n * pitch, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}")]
    for i, (key, label, href) in enumerate(MM_ROWS):
        y = top + i * pitch
        by = 9
        icon = (f'<svg width="26" height="32" viewBox="0 0 26 32" aria-hidden="true" '
                f'style="position: absolute; left: 15px; top: {by}px">{mm_icon(key)}</svg>')
        lines = "".join(text(93 - round(len(t) * 7.1 / 2), by + (1 + 16 * j if len(label) == 2 else 9), t, 16)
                        for j, t in enumerate(label))
        o.append(abtn(2, y, 147, pitch, icon + lines,
                      "background: transparent; border: none; text-align: left", href=href))
        if i:
            o.append(box(9, y - 1, 132, 1, "background: #000000"))
    write_board("MainMenu.dc.html", "Main menu with the Frequency response row",
                scope_view(menu_active=True) + "\n" + "\n".join(o))
