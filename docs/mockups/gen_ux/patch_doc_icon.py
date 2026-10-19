"""bode-ui.md: section 1a, the Frequency response main-menu icon (owner request)."""
import sys

P = sys.argv[1]
s = open(P, encoding="utf-8").read()
anchor = "## 2. The screen"
assert s.count(anchor) == 1
s = s.replace(anchor, '''## 1a. Main-menu icon (owner request)

The owner asked for an icon that a filter designer recognises at a glance, replacing fw 0.2.68's two parallel falling
lines. The artboard "Main-menu icon candidates" shows three at 1:1 in the menu row and at 4x on a pixel grid:

- **A, chosen: a low-pass response.** A flat passband, a knee, then a 45 degree line (the -20 dB/decade roll-off),
  over an L frame.
- **B: a band-pass hump.** 45 degree skirts with a three-pixel top. It reads as a peak, but at 25 px it also reads as a
  roof or a spectral line.
- **C: A with the corner marked** by a dotted grey vertical. At 1:1 the mark adds noise and does not help.

A is chosen because the low-pass Bode shape is the textbook picture of a frequency response. It is also the shape the
mode measures first on the bench (the RC low-pass).

**The pixels.** Coordinates are from (MAIN_MENU_XPOS + 15, button top), as `scope_main_menu_wave_icon` uses. The icon
is 1 px with no anti-aliasing, 25 x 22 px (x 0..24, y 8..29).

| Part | Pixels | Colour |
|---|---|---|
| Level axis | x 0, y 8..29 | `UI_MM_AXIS_COLOR` (0x707070), as the Scope icon's axes |
| Frequency axis | y 29, x 0..24 | `UI_MM_AXIS_COLOR` |
| Passband | y 12, x 2..10 | fg |
| Knee | (11, 13), (12, 13): two pixels at half slope, which make the corner read rounded | fg |
| Roll-off | (13, 14) to (24, 25), one pixel per column and row | fg |

fg is the colour `scope_main_menu_item` already sets: white, black while touched, 0x686868 when the row is unavailable.
The axes keep their grey in every state, as the Scope icon's do.

```c
//Frequency response (bode-ui.md 1a, owner): a low-pass magnitude response over an L frame
static void scope_main_menu_bode_icon(uint32 y, uint32 fg)
{
  uint32 x = MAIN_MENU_XPOS + 15;

  display_set_fg_color(UI_MM_AXIS_COLOR);
  display_draw_vert_line(x, y + 8, y + 29);
  display_draw_horz_line(y + 29, x, x + 24);

  display_set_fg_color(fg);
  display_draw_horz_line(y + 12, x + 2, x + 10);
  display_draw_horz_line(y + 13, x + 11, x + 12);
  display_draw_line(x + 13, y + 14, x + 24, y + 25);
}
```

`display_draw_line` at exactly 45 degrees sets one pixel per step, so the roll-off is the twelve pixels of the table.
The call site in `scope_main_menu_item` passes the fg it already computed, as it does for the Scope icon.

''' + anchor)
s = s.replace('''| Main menu row | opens the mode | `ui_mm_labels[UI_MM_BODE]` "Bode plot"; grey and inert when `bode_view_available()` is 0 | label "Frequency" / "response"; a tap on the grey row shows the reason (section 6); entry in roll mode shows the roll note |''',
              '''| Main menu row | opens the mode | `ui_mm_labels[UI_MM_BODE]` "Bode plot"; grey and inert when `bode_view_available()` is 0; two parallel lines as the icon | label "Frequency" / "response"; the low-pass icon of section 1a; a tap on the grey row shows the reason (section 6); entry in roll mode shows the roll note |''')
open(P, "w", encoding="utf-8", newline="\n").write(s)
print("doc patched")
