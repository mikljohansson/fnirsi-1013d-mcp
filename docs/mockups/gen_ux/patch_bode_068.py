"""bode.py: fw 0.2.68 run button (the action), no invented single-sweep chip, fw menu geometry, mode name Frequency response."""
import os

P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bode.py")
s = open(P, encoding="utf-8").read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''    """common.topbar with the mode's values: the RUN / STOP chip (RUN green, STOP red, SNGL orange: a single sweep,
    the colour the stock chip uses for the single trigger), the ACQ chip saying the sweep sets the rate, the generator
    chip showing the tone the sweep drives"""''',
    '''    """common.topbar with the mode's values: the run state chip (fw 0.2.68: the STATE, RUN green or STOP red; a single
    sweep is RUN while it runs), the ACQ chip saying the sweep sets the rate, the generator chip showing the tone the
    sweep drives"""''')
rep('''    elif run == "SNGL":
        new_run = box(127, 22, 38, 18, f"background: {ORANGE}; border-radius: 2px", text(3, 1, "SNGL", 13, "#000000"))
''', "")
rep('''def bode_actions(auto_running=False):
    """the fixed meanings: CTRL, RUN/STOP, AUTO SET (the mode's own auto), T CURSOR (the frequency cursor), V CURSOR
    empty (no level cursor in this mode), MEASURES (the Bode list), SAVE PIC, SAVE WAVE (the sweep table)"""
    o = [box(730, 0, 70, 480, "background: #000000"),
         rbtn(5, [("CTRL", 16)], href="BodeCtrl.dc.html"),
         rbtn(65, [("RUN/", 16), ("STOP", 16)], kind="run")]''',
    '''def run_button(running):
    """fw 0.2.68 (owner exception, ui-guidelines 3): the button shows the ACTION a tap does, green RUN while stopped,
    red STOP while running"""
    if running:
        return abtn(739, 65, 51, 50, centred([("STOP", 16, "#ffffff")]),
                    f"background: {RED}; border: 1px solid {EDGE}; border-radius: 2px")
    return abtn(739, 65, 51, 50, centred([("RUN", 16, "#000000")]),
                f"background: {GREEN}; border: 1px solid {EDGE}; border-radius: 2px")


def bode_actions(auto_running=False, running=False):
    """the fixed meanings: CTRL, RUN/STOP, AUTO SET (the mode's own auto), T CURSOR (the frequency cursor), V CURSOR
    empty (no level cursor in this mode), MEASURES (the mode's list), SAVE PIC, SAVE WAVE (the sweep table)"""
    o = [box(730, 0, 70, 480, "background: #000000"),
         rbtn(5, [("CTRL", 16)], href="BodeCtrl.dc.html"),
         run_button(running or auto_running)]''')
rep('write_board("Main.dc.html", "Bode plot", "\\n".join(body))',
    'write_board("Main.dc.html", "Frequency response", "\\n".join(body))')
rep('"Bode plot, before the first sweep"', '"Frequency response, before the first sweep"')
rep('''    body = [bode_topbar(run="SNGL", gen=fmt_f(FREQS[n]).replace("kHz", "kHz"), ch1="1V/div", ch2="200mV/div"),
            grid_svg(a),
            traces_svg(a, pts, current=FREQS[n]),
            bode_cards(sweeping),
            bode_actions()]''',
    '''    body = [bode_topbar(run="RUN", gen=fmt_f(FREQS[n]), ch1="1V/div", ch2="200mV/div"),
            grid_svg(a),
            traces_svg(a, pts, current=FREQS[n]),
            bode_cards(sweeping),
            bode_actions(running=True)]''')
rep('"Bode plot, single sweep running"', '"Frequency response, single sweep running"')
rep('"Bode plot, AUTO SET running"', '"Frequency response, AUTO SET running"')
rep('"Bode controls"', '"Frequency response controls"')
rep('"Bode controls, Display"', '"Frequency response controls, Display"')
rep('"Bode controls, Sweep"', '"Frequency response controls, Sweep"')
rep('"Bode controls, Thru"', '"Frequency response controls, Thru"')
rep("'Bode measurements</div>')", "'Frequency response measurements</div>')")
rep('"Bode measurements", result_view', '"Frequency response measurements", result_view')

a = s.index("# ================================================================ MainMenu")
b = s.index('if __name__ == "__main__":')
s = s[:a] + open(os.path.join(os.path.dirname(P), "bode_menu_part.py"), encoding="utf-8").read() + "\n\n" + s[b:]
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("bode.py patched")
