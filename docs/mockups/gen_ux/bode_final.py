"""Frequency response, final design (owner, 2026-10-19 evening): variant 2's chart (one pane, dB axis left, deg axis
right, no legend, curve colours = axis label colours), cursors C1 / C2 with their readout in the data-free top
division, the Display fold-out with the Frequency axis row (Log / Linear), the calibration-post instruction.
Rebuilds every Frequency response board on this chart; the superseded variant 1 action page is kept as BodeV1.

usage: python bode_final.py <out_dir>
"""
import math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bode as B  # noqa
import bode_v2 as V  # noqa
import boards  # noqa
from common import *  # noqa

# the slider row under the plot (owner, 2026-10-19 19:40), as the Scope's: the pane shrinks to six divisions of 56 px
# (y 60..396), frequency labels at y 400, the cards at y 426 (the Scope's), the slider y 452..471, the C1 / C2 box at
# the pane's bottom left y 342..394
V.V_DIV, V.RO_Y, B.FL_Y, B.CARDS_Y = 56, 342, 400, 426
TRACK_F0, TRACK_F1 = 1.0, 1e7         # the Sweep fold-out's full range, 1 Hz..10 MHz (coordinator, owner to veto)


def track_x(f):
    """bu_slider_x for any frequency: x 2..728 is 1 Hz .. 10 MHz on a log scale (BU_SL_X 2, BU_SL_W 726; a point at
    10 MHz is drawn on the last column, 727, as sov_col clips it)"""
    span = math.log10(TRACK_F1) - math.log10(TRACK_F0)
    return OV_X + (math.log10(min(max(f, TRACK_F0), TRACK_F1)) - math.log10(TRACK_F0)) / span * OV_W


# AUTO SET's overview (bode-ui.md 3.2): 20 points 100 Hz .. 10 MHz; on the tank the points below -58 dB are flagged
OVERVIEW_F = B.log_points(100, 1e7, 20)
OVERVIEW = [(f, *B.h_bp(f), B.h_bp(f)[0] > -58) for f in OVERVIEW_F]


PHASE_DIM = "#804080"   # slider_ov.h SOV_PHASE_DIM: the phase colour at half intensity (outside the window)


def fr_curve(pts, i, full, dim, wrap=None):
    """one curve of the overview (slider_ov.c sov_curve, slider_ov_draw.c sov_draw_curve): value i of each point
    (1 the magnitude, 2 the phase) fitted to the 20 rows on its own (the highest valid point on the top row, the lowest
    on the bottom row; the flagged ones when none is valid; the track has no labels, so only the shape counts); valid
    points joined by a line, not across a flagged point nor, for the phase, across a wrap (wrap degrees or more apart);
    a flagged point a single grey pixel without a joining line (as on the chart), clipped to the rows"""
    valid = [p[i] for p in pts if p[3]] or [p[i] for p in pts] or [0.0]
    mx, mn = max(valid), min(valid)

    def row(v):
        if mx == mn:
            return OV_Y + OV_ROWS // 2
        return max(OV_Y, min(OV_Y + OV_ROWS - 1, OV_Y + round((mx - v) / (mx - mn) * (OV_ROWS - 1))))

    layers, seg, flags, prev = [], [], [], None

    def flush():
        if seg:
            layers.append((full, dim, ov_line_runs(seg) if len(seg) > 1 else [(x, r, r) for x, r in seg]))

    for p in pts:
        x, v = min(round(track_x(p[0])), OV_X + OV_W - 1), p[i]
        if not p[3]:
            flags.append((x, row(v), row(v)))
            flush()
            seg, prev = [], None
            continue
        if prev is not None and wrap is not None and abs(v - prev) >= wrap:
            flush()
            seg = []
        seg.append((x, row(v)))
        prev = v
    flush()
    return layers + ([(OV_FLAG, OV_FLAG, flags)] if flags else [])


def fr_overview(pts, show="Overlaid"):
    """docs/plans/slider-overview.md section 5, decision 8 (owner 2026-10-20 13:35, 13:36): the overview AUTO SET kept,
    the curves the plot shows: Overlaid both, the phase (pink) drawn first and the magnitude (cyan) over it, each
    fitted to the track's rows on its own; Magnitude only / Phase only that one curve. Flagged points grey for both"""
    layers = []
    if show != "Magnitude only":
        layers += fr_curve(pts, 2, B.PHASE, PHASE_DIM, wrap=180.0)
    if show != "Phase only":
        layers += fr_curve(pts, 1, CH2, DIM[CH2])
    return layers


def fr_slider(f0, f1, overview=None):
    """Start..Stop as a window over 1 Hz..10 MHz on the log axis, the Scope slider's construction (fw 0.2.74: the track
    y 452..471, the window a box with a 1 px margin); dragging moves Start and Stop together by whole steps
    and re-sweeps, a tap outside the window centres it there. With the overview AUTO SET kept drawn in the track
    (x 209..727: 100 Hz .. 10 MHz), nothing before the first AUTO SET"""
    x0, x1 = round(track_x(f0)), round(track_x(f1))
    return slider_row(x0, x1 - x0, overview or ())


LANE_Y = V.V_Y          # top division y 60..118: no data (headroom on both axes, section 2 of bode-ui.md)
RO_X, RO_W = 44, 238    # cursor readout x 44..282


def chart(a, pts, markers=True, cursors=(), current=None):
    """V.chart without the legend (owner: the axes carry the units, the curves are known by their axis colour)"""
    html = V.chart(a, pts, markers=markers, cursors=cursors)
    i = html.index('<div style="position: absolute; left: 610px; top: 80px')
    html = html[:i].rstrip("\n")
    if current is not None:
        x = round(a.x(current)) + 0.5
        html += (f'\n<svg width="730" height="480" viewBox="0 0 730 480" aria-hidden="true" style="position: absolute; '
                 f'left: 0; top: 0"><line x1="{x}" y1="{V.V_Y}" x2="{x}" y2="{V.V_Y + V.V_N * V.V_DIV}" '
                 f'stroke="{B.CURLINE}" stroke-width="1" stroke-dasharray="1 1"></line></svg>')
    return html


def lane_readout(rows):
    """C1, C2 and C2-C1 in the top division (13 px, 16 px pitch, x 44..276, y 62..112): f white, level in the
    magnitude colour, phase in the phase colour (x 44..282); a flagged point's row in grey with its reason instead of the phase"""
    o = []
    for i, (name, f, db, ph) in enumerate(rows):
        y = 3 + 16 * i
        o.append(text(6, y, name, 13, B.CURSOR if name != "C2-C1" else "#ffffff"))
        o.append(text(50, y, f, 13))
        o.append(text(112, y, db, 13, CH2))
        o.append(text(174, y, ph, 13, B.PHASE))
    return box(RO_X, LANE_Y + 2, RO_W, 52, f"background: {GREY}; border-radius: 2px", "".join(o))


def lane_warning(msg):
    """while the cursor readout shows, the warning centres in the top division's free width right of it
    (x 282..662, centre 472); otherwise top-middle as in every mode"""
    assert len(msg) <= 50
    w = int(len(msg) * 7.1) + 24
    return warning_box(msg, cx=472, y=70, w=w)


def tank_axes(pts):
    return V.Axes2(*B.SPAN, pts)


def view(right, cursors=False, warn=True, cards=None):
    pts = B.sweep_points()
    a = tank_axes(pts)
    ref = 20 * math.log10(B.K)
    bw = B.F0 / B.Q
    fl = -bw / 2 + math.sqrt(bw * bw / 4 + B.F0 * B.F0)
    body = [B.bode_topbar(run="STOP", gen="50.3kHz", ch1="1V/div", ch2="200mV/div")]
    curs = [("C1", pts[52]), ("C2", pts[62])] if cursors else []
    body.append(chart(a, pts, markers=True, cursors=curs))
    if cursors:
        p1, p2 = pts[52], pts[62]
        body.append(V.readout([("C1", B.fmt_f(p1[0]), f"{p1[1]:+.2f}dB", f"{p1[2]:+.1f}deg"),
                                  ("C2", B.fmt_f(p2[0]), f"{p2[1]:+.2f}dB", f"{p2[2]:+.1f}deg"),
                                  ("C2-C1", B.fmt_f(p2[0] - p1[0]), f"{p2[1] - p1[1]:+.2f}dB",
                                   f"{p2[2] - p1[2]:+.1f}deg")]))
    if warn:
        body.append(B.warn("2 points not settled: choose a longer Settle"))
    body += [B.bode_cards(cards or B.CARD_DEFAULTS_DONE(ref, fl)), fr_slider(*B.SPAN, fr_overview(OVERVIEW)), right]
    return "\n".join(body)


def board_main():
    B.write_board("Main.dc.html", "Frequency response", view(B.bode_actions()))


def board_cursors():
    B.write_board("BodeCursors.dc.html", "Frequency response, cursors C1 and C2 (owner: readout box bottom left)", view(B.bode_actions(), cursors=True))


def board_first():
    a = V.Axes2(100, 1e6, [(100, 0.0, 90.0, True), (1e6, -40.0, -90.0, True)])
    cards = [("Level", "no sweep", CH2, B.NOTE), ("-3dB", "no sweep", CH2, B.NOTE), ("Peak", "no sweep", CH2, B.NOTE),
             ("Q", "no sweep", CH2, B.NOTE), ("Sweep", "not run", B.NEUTRAL, B.NOTE), ("Thru", "none", B.NEUTRAL, B.NOTE)]
    l1 = "Connect the calibration post and CH1 to the input, CH2 to the output,"
    l2 = "grounds on the probe clips. Press RUN."
    w = int(max(len(l1), len(l2)) * 7.1) + 24
    instr = box(366 - w // 2, 66, w, 44, "background: #000000; text-align: center",
                f'<span style="display: block; font-size: 16px; line-height: 20px; padding-top: 2px; color: {ORANGE}">'
                f'{l1}<br>{l2}</span>')
    body = [B.bode_topbar(run="STOP", gen="1kHz"), chart(a, [], markers=False), instr, B.bode_cards(cards),
            fr_slider(100, 1e6), B.bode_actions()]
    B.write_board("BodeFirst.dc.html", "Frequency response, before the first sweep", "\n".join(body))


def board_single():
    n = 37
    pts = B.sweep_points(upto=n)
    a = tank_axes(pts)
    cards = [("Level", "sweeping", CH2, B.NOTE), ("-3dB", "sweeping", CH2, B.NOTE), ("Peak", "sweeping", CH2, B.NOTE),
             ("Q", "sweeping", CH2, B.NOTE), ("Sweep", f"{n}/100", B.NEUTRAL, "#ffffff"), ("Thru", "applied", B.NEUTRAL, GREEN)]
    body = [B.bode_topbar(run="RUN", gen=B.fmt_f(B.FREQS[n]), ch1="1V/div", ch2="200mV/div"),
            chart(a, pts, markers=False, current=B.FREQS[n]), B.bode_cards(cards), fr_slider(*B.SPAN, fr_overview(OVERVIEW)),
            B.bode_actions(running=True)]
    B.write_board("BodeSingle.dc.html", "Frequency response, single sweep running", "\n".join(body))


def board_auto():
    fr = B.log_points(100, 1e7, 20)
    n = 12
    pts = [(f, *B.h_bp(f), B.h_bp(f)[0] > -58) for f in fr[:n]]
    a = V.Axes2(100, 1e7, pts)
    cards = [("Level", "sweeping", CH2, B.NOTE), ("-3dB", "sweeping", CH2, B.NOTE), ("Peak", "sweeping", CH2, B.NOTE),
             ("Q", "sweeping", CH2, B.NOTE), ("Sweep", f"auto {n}/20", B.NEUTRAL, "#ffffff"),
             ("Thru", "applied", B.NEUTRAL, GREEN)]
    body = [B.bode_topbar(run="RUN", gen=B.fmt_f(fr[n]), ch1="1V/div", ch2="200mV/div"),
            chart(a, pts, markers=False, current=fr[n]), B.bode_cards(cards), fr_slider(100, 1e7, fr_overview(pts)),
            B.bode_actions(auto_running=True)]
    B.write_board("BodeAuto.dc.html", "Frequency response, AUTO SET running", "\n".join(body))


def ctrl(active=None):
    return B.bode_ctrl(active=active, display_val="Overlay")


ICON_OVERLAID = (boards.SVG0 + '<polyline points="2,4 12,4 17,6 21,10 26,17" fill="none" stroke="currentColor" stroke-width="2"></polyline>'
                 '<path d="M2,7 C12,7 14,16 26,16" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 2"></path></svg>')


def board_ctrl_display():
    px, py, pw = 522, 60, 206
    rw = pw - 16
    rows = [boards.frow(8, 30, rw, "Overlaid", sel=True, icon=ICON_OVERLAID),
            boards.frow(8, 80, rw, "Magnitude only", icon=B.ICON_MAG),
            boards.frow(8, 130, rw, "Phase only", icon=B.ICON_PHASE),
            box(8, 183, rw, 1, f"background: {GREY}"),
            boards.frow(8, 192, rw, "Frequency axis", "Log")]
    body = view(ctrl("display")) + "\n" + boards.foldout_panel(px, py, pw, 244, "Display", "".join(rows))
    B.write_board("BodeCtrlDisplay.dc.html", "Frequency response controls, Display", body)


def board_ctrl():
    B.write_board("BodeCtrl.dc.html", "Frequency response controls", view(ctrl()))


def board_ctrl_sweep():
    px, py, pw = 522, 120, 206
    rw = pw - 16
    rows = [B.twoway(8, 30, rw, "Start", "5kHz"), B.twoway(8, 80, rw, "Stop", "500kHz"),
            boards.frow(8, 130, rw, "Points", "100"), boards.frow(8, 180, rw, "Settle", "Normal"),
            boards.frow(8, 230, rw, "CH2 range", "Auto"), box(8, 283, rw, 1, f"background: {GREY}"),
            B.action_row(8, 292, rw, "Single sweep", "about 3 s", href="BodeSingle.dc.html")]
    body = view(ctrl("sweep")) + "\n" + boards.foldout_panel(px, py, pw, 344, "Sweep", "".join(rows))
    B.write_board("BodeCtrlSweep.dc.html", "Frequency response controls, Sweep", body)


def board_ctrl_thru():
    px, py, pw = 492, 180, 236
    rw = pw - 16
    rows = [B.action_row(8, 30, rw, "Measure thru"), B.switch_row2(8, 80, rw, "Apply", "matches this sweep", True),
            B.action_row(8, 130, rw, "Clear")]
    body = view(ctrl("thru")) + "\n" + boards.foldout_panel(px, py, pw, 182, "Thru: both tips on the input", "".join(rows))
    B.write_board("BodeCtrlThru.dc.html", "Frequency response controls, Thru", body)


def board_measures():
    # reuse bode.py's menu over the final chart: render bode.py's board into a string by temporarily swapping result_view
    saved = B.result_view
    B.result_view = lambda right: view(right)
    try:
        B.board_measures()
    finally:
        B.result_view = saved


def board_v1():
    """the superseded variant 1 action page, kept for reference"""
    a = B.TANK_AXES
    pts = B.sweep_points()
    m, ref, fl, fh = B.tank_markers(a)
    cur = 62
    body = [B.bode_topbar(run="STOP", gen="50.3kHz", ch1="1V/div", ch2="200mV/div"), B.grid_svg(a),
            B.traces_svg(a, pts, markers=m, cursor=cur), B.cursor_box(pts[cur][0], pts[cur][1], pts[cur][2]),
            B.warn("2 points not settled: choose a longer Settle"), B.bode_cards(B.CARD_DEFAULTS_DONE(ref, fl)),
            B.bode_actions()]
    B.write_board("BodeV1.dc.html", "Frequency response, variant 1 (superseded)", "\n".join(body))


if __name__ == "__main__":
    for fn in [board_main, board_cursors, board_first, board_single, board_auto, board_ctrl, board_ctrl_display,
               board_ctrl_sweep, board_ctrl_thru, board_measures]:      # BodeV1 stays as drawn (superseded)
        fn()
