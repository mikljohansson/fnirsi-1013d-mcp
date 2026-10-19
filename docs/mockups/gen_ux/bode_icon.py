"""Frequency response main-menu icon candidates: 1 px pixel patterns in the row's icon box (x 0..24 from
MAIN_MENU_XPOS + 15, y relative to the button top, as menu.c's scope_main_menu_wave_icon), shown at 1:1 in a menu row
and at 4x on a pixel grid.

usage: python bode_icon.py <out_dir>
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bode as B  # noqa
from common import *  # noqa

AXIS = "#707070"     # UI_MM_AXIS_COLOR, as the Scope icon's axes


def hline(x0, x1, y):
    return [(x, y) for x in range(x0, x1 + 1)]


def vline(x, y0, y1):
    return [(x, y) for y in range(y0, y1 + 1)]


FRAME = vline(0, 8, 29) + hline(0, 24, 29)        # an L frame: level axis left, frequency axis below

# A: low-pass, flat passband, a two-pixel half-slope step at the knee, then 45 degrees (the -20 dB/decade line)
LOWPASS = hline(2, 10, 12) + [(11, 13), (12, 13)] + [(13 + i, 14 + i) for i in range(12)]          # to (24, 25)

# B: band-pass hump, peak at x 12, skirts at 45 degrees, a three-pixel rounded top
BANDPASS = ([(2 + i, 26 - i) for i in range(7)] + [(9, 18), (10, 17), (11, 16), (12, 16), (13, 16), (14, 17), (15, 18)] +
            [(16 + i, 20 + i) for i in range(7)])                                                     # to (22, 26)

# C: the low-pass of A with the corner marked: a grey dotted vertical at the knee
CORNER = LOWPASS
CORNER_MARK = [(11, y) for y in range(15, 29, 2)]

CANDIDATES = [("A", "Low-pass", LOWPASS, []), ("B", "Band-pass", BANDPASS, []), ("C", "Low-pass, corner marked", CORNER, CORNER_MARK)]


def pixels_svg(pts, colour, scale, ox=0, oy=0):
    return "".join(f'<rect x="{ox + x * scale}" y="{oy + y * scale}" width="{scale}" height="{scale}" fill="{colour}"></rect>'
                   for x, y in pts)


def icon_svg(curve, extra, scale, grid=False):
    w, h = 25 * scale, 32 * scale
    o = [f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" aria-hidden="true" shape-rendering="crispEdges" '
         'style="position: absolute; left: 0; top: 0">']
    if grid:
        o.append(f'<rect x="0" y="0" width="{w}" height="{h}" fill="{DARKGREY}"></rect>')
        for i in range(26):
            o.append(f'<line x1="{i * scale}" y1="0" x2="{i * scale}" y2="{h}" stroke="#2a2a2a" stroke-width="1"></line>')
        for j in range(33):
            o.append(f'<line x1="0" y1="{j * scale}" x2="{w}" y2="{j * scale}" stroke="#2a2a2a" stroke-width="1"></line>')
    o.append(pixels_svg(FRAME, AXIS, scale))
    o.append(pixels_svg(extra, AXIS, scale))
    o.append(pixels_svg(curve, "#ffffff", scale))
    o.append("</svg>")
    return "".join(o)


def menu_row(x, y, curve, extra):
    """one main-menu row at 1:1: the column's dark grey, the icon at +15 / +9 (button top), the two-line label"""
    inner = (box(15, 9, 25, 32, "", icon_svg(curve, extra, 1)) +
             text(93 - round(9 * 7.1 / 2), 10, "Frequency", 16) + text(93 - round(8 * 7.1 / 2), 26, "response", 16))
    return box(x, y, 147, 52, f"background: {DARKGREY}; border: 1px solid {LIGHTGREY}", inner)


def board():
    o = [box(0, 0, 800, 480, "background: #000000")]
    o.append(text(20, 12, "Frequency response menu icon: candidates at 1:1 in the row and at 4x", 16))
    for i, (key, name, curve, extra) in enumerate(CANDIDATES):
        x0 = 20 + i * 260
        o.append(text(x0, 44, f"{key}: {name}" + ("  (chosen)" if key == "A" else ""), 16, PALEYEL if key == "A" else "#ffffff"))
        o.append(menu_row(x0, 72, curve, extra))
        o.append(box(x0, 140, 100, 128, "", icon_svg(curve, extra, 4, grid=True)))
    o.append(text(20, 412, "White: the curve (the row's text colour, black while touched). Grey 0x707070: the axes, as the Scope icon's.", 13, B.NOTE))
    o.append(text(20, 430, "Coordinates from (MAIN_MENU_XPOS + 15, button top); 1 px, no anti-aliasing. Pixel lists: docs/plans/bode-ui.md.", 13, B.NOTE))
    B.write_board("MenuIcon.dc.html", "Frequency response menu icon", "\n".join(o))


if __name__ == "__main__":
    board()
