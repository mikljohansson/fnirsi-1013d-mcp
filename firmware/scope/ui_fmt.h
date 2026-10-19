//----------------------------------------------------------------------------------------------------------------------------------
// ui_fmt.h - text measuring and value formatting for the screens (fw 0.2.51): the measurement cards' four-significant-digit
// values and their chip layout (docs/ui-guidelines.md section 3, docs/plans/ux-makeover.md section 3). Pure C, no globals,
// host-buildable (host_tests/test_ui.c, linked with the real font tables).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_FMT_H
#define UI_FMT_H

#include "types.h"
#include "font_structs.h"

//Width in pixels of a string in a variable-width font (0 for a fixed-width font); characters the font lacks count 0
uint32 ui_text_width(const FONTDATA *font, const char *s);

//A value given in hundredths of the unit with the magnitude prefix `scale` (0 p, 1 n, 2 u, 3 m, 4 none, 5 k, 6 M, 7 G),
//the fixed point 1.00o5's measurements use (scope_print_value). Written with at most four significant digits, never
//finer than the input's own resolution, with the prefix that puts one to three digits before the point:
//100000 at scale 4 with "Hz" -> "1.000kHz", 50 at scale 4 with "V" -> "500mV", 0 -> "0V". Returns the end (the NUL).
char *ui_fmt_sig4(char *p, int32 value, uint32 scale, const char *unit);

//The same without a prefix (scale 4 kept), for percentages: 5000 with "%" -> "50.00%"
char *ui_fmt_sig4_fixed(char *p, int32 value, const char *unit);

//A frequency in mHz (the spectrum's unit) with four significant digits: 146484 -> "146.5Hz", 1000000 -> "1.000Hz"
char *ui_fmt_mhz(char *p, uint64 mhz);

//A level in centi-dB with one decimal and a sign, then the unit: 270 -> "+2.7dBV", -10000 -> "-100.0dBV", 4 -> "0.0dBV"
char *ui_fmt_cdb(char *p, int32 cdb, const char *unit);

//Measurement card: UI_CARD_W x UI_CARD_H, the name in a chip of the source's colour (name + UI_CARD_PAD each side, full
//card height), the value UI_CARD_GAP after the chip; a card fits when the value ends UI_CARD_PAD before the right edge
#define UI_CARD_W          118
#define UI_CARD_H          22
#define UI_CARD_PAD        3
#define UI_CARD_GAP        6

uint32 ui_card_chip_w(const FONTDATA *font, const char *name);
int    ui_card_fits(const FONTDATA *font, const char *name, const char *value);

//The twelve scope measurements as scope makers spell them, in 1.00o5's order (scopesettings.measuresstate index):
//Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width, +Duty, -Duty
#define UI_MEAS_COUNT      12
#define UI_MEAS_VAVG       2
#define UI_MEAS_VPP        4
#define UI_MEAS_FREQ       6
extern const char *measurement_names[UI_MEAS_COUNT];

//The timing measurements need three crossings in the record; with fewer they say so
#define UI_MEAS_NO_PERIOD  "no period"

#endif
