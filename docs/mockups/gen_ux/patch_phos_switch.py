"""Display fold-out: Normal / X-Y rows, Phosphor as the stock slide switch with its section."""

p = "boards.py"
s = open(p, encoding="utf-8").read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


# CTRL Display button: the view as its value, plus a small graded phosphor marker while the switch is on
rep('def ctrl_scope(active=None, display_val="Normal", back_href="Scope.dc.html"):',
    'def ctrl_scope(active=None, display_val="Normal", back_href="Scope.dc.html", phosphor=False):')
rep('''        vbtn(65, ["Display"], display_val, k("display"), href="ScopeCtrlDisplay.dc.html"),''',
    '''        display_button(display_val, k("display"), phosphor,
                       "ScopeCtrl.dc.html" if active == "display" else "ScopeCtrlDisplay.dc.html"),''')

helpers = r'''
PHOS_MARK = ('<svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true">'
             '<circle cx="5" cy="5" r="5" fill="currentColor" fill-opacity="0.25"></circle>'
             '<circle cx="5" cy="5" r="3.2" fill="currentColor" fill-opacity="0.55"></circle>'
             '<circle cx="5" cy="5" r="1.6" fill="currentColor"></circle></svg>')


def display_button(view, kind, phosphor, href):
    """CTRL-page Display button: label, the view as value, and the phosphor marker under it while the switch is on"""
    fg = "#000000" if kind == "active" else "#ffffff"
    vc = "#000000" if kind == "active" else PALEYEL
    lines = [("Display", 13, fg), (view, 13, vc)]
    if phosphor:
        lines.append((f'<span style="display: inline-flex; color: {vc}">{PHOS_MARK}</span>', 13, vc))
    bg = ACTIVE if kind == "active" else DARKGREY
    return abtn(739, 65, 51, 50, centred(lines), f"background: {bg}; border: 1px solid {EDGE}; border-radius: 2px", href=href)


def slide_switch(on=True, color=GREEN):
    """stock slide toggle (menu.c scope_display_slide_button): 45 x 21, green fill with a darker edge when on,
    grey knob 20 x 13 on the right with four black grip lines; grey fill and knob on the left when off"""
    fill = color if on else "#888888"
    edge = "#008800" if on else GREY
    kx = 21 if on else 4
    grips = "".join(box(kx + 5 + 3 * i, 6, 1, 9, "background: #000000") for i in range(4))
    return ('<span style="position: relative; display: inline-block; width: 45px; height: 21px; flex: none">' +
            box(0, 0, 45, 21, f"background: {fill}; border: 1px solid {edge}; border-radius: 2px") +
            box(kx, 4, 20, 13, f"background: {GREY}") + grips + '</span>')


def switch_row(x, y, w, label, on, icon=""):
    """44 px fold-out row with a slide switch at the right; the whole row toggles"""
    inner = (f'<span style="display: flex; align-items: center; justify-content: space-between; height: 100%; '
             f'padding: 0 10px; font-size: 16px; color: #ffffff">'
             f'<span style="display: flex; align-items: center; gap: 8px">{icon}<span>{label}</span></span>'
             f'{slide_switch(on)}</span>')
    return abtn(x, y, w, 44, inner, f"background: {LIGHTGREY}; border: none; border-radius: 3px; text-align: left")

'''
rep("def foldout_panel(", helpers + "def foldout_panel(")

rep('''    rows = [frow(8, 30, pw - 16, "Normal", href="ScopeCtrl.dc.html", icon=ICON_NORMAL),
            frow(8, 80, pw - 16, "Phosphor", sel=True, icon=ICON_PHOSPHOR),
            frow(8, 130, pw - 16, "X-Y", icon=ICON_XY),
            box(8, 184, pw - 16, 1, f"background: {GREY}"),
            text(10, 192, "Phosphor", 13, LG2),
            frow(8, 214, pw - 16, "Persistence", "1 s"),
            frow(8, 264, pw - 16, "Palette", "Mono"),
            frow(8, 314, pw - 16, "Clear"),
]''',
    '''    rows = [frow(8, 30, pw - 16, "Normal", sel=True, icon=ICON_NORMAL),
            frow(8, 80, pw - 16, "X-Y", icon=ICON_XY),
            box(8, 134, pw - 16, 1, f"background: {GREY}"),
            switch_row(8, 142, pw - 16, "Phosphor", True, icon=ICON_PHOSPHOR),
            frow(8, 192, pw - 16, "Persistence", "1 s"),
            frow(8, 242, pw - 16, "Palette", "Mono"),
            frow(8, 292, pw - 16, "Clear")]''')
rep('ctrl_scope(active="display", display_val="Phos"),', 'ctrl_scope(active="display", phosphor=True),')
rep('foldout_panel(px, py, pw, 366, "Display", "".join(rows))', 'foldout_panel(px, py, pw, 344, "Display", "".join(rows))')
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
