// test_ui.c - the screens' pure parts (fw 0.2.51): card value formatting and card fit with the device's own font tables
// (ui_fmt.c, font_2.c), the main menu row table, hit areas and mode rule (ui_menu.c). Run natively.

#include <stdio.h>
#include <string.h>

#include "ui_fmt.h"
#include "ui_menu.h"
#include "test_util.h"

extern FONTDATA font_2, font_3;

static char b[64];

static const char *s4(int v, unsigned scale, const char *unit) { ui_fmt_sig4(b, v, scale, unit); return b; }
static const char *pc(int v) { ui_fmt_sig4_fixed(b, v, "%"); return b; }

static void test_sig4(void)
{
  //Frequencies as the readout gives them (hundredths at a prefix)
  T_STR(s4(100000, 4, "Hz"), "1.000kHz");          //1 kHz at 1 MSa/s: 100000 hundredths of Hz
  T_STR(s4(99996, 4, "Hz"), "1.000kHz");           //rounding carries into the next prefix
  T_STR(s4(99994, 4, "Hz"), "999.9Hz");
  T_STR(s4(20000000, 5, "Hz"), "200.0MHz");
  T_STR(s4(123456789, 4, "Hz"), "1.235MHz");
  T_STR(s4(5000, 4, "Hz"), "50.00Hz");
  T_STR(s4(150, 4, "Hz"), "1.50Hz");               //only the digits the input has
  T_STR(s4(7, 4, "Hz"), "70mHz");

  //Times
  T_STR(s4(100000, 2, "s"), "1.000ms");
  T_STR(s4(50000, 2, "s"), "500.0us");
  T_STR(s4(12345, 1, "s"), "123.5ns");
  T_STR(s4(1000, 3, "s"), "10.00ms");

  //Voltages, signed
  T_STR(s4(204000, 3, "V"), "2.040V");
  T_STR(s4(-119000, 3, "V"), "-1.190V");
  T_STR(s4(-2000, 3, "V"), "-20.00mV");
  T_STR(s4(50, 4, "V"), "500mV");
  T_STR(s4(0, 3, "V"), "0V");
  T_STR(s4(2147483647, 3, "V"), "21.47kV");
  T_STR(s4(-2147483647 - 1, 3, "A"), "-21.47kA");

  //Prefix limits: pico below, giga above
  T_STR(s4(1, 0, "s"), "0.01ps");
  T_STR(s4(2000000000, 7, "Hz"), "20000000GHz");

  //Percent, no prefix
  T_STR(pc(5000), "50.00%");
  T_STR(pc(500), "5.00%");
  T_STR(pc(10000), "100.0%");
  T_STR(pc(1), "0.01%");
  T_STR(pc(0), "0%");
}

//Every name with the longest value it can show fits the 118 px card in the 13 px font; the plan's example has margin
static void test_cards(void)
{
  static const char *values[] = { "-1.234mV", "-21.47kV", "999.9MHz", "1.000kHz", "123.5ns", "100.0%", UI_MEAS_NO_PERIOD };
  unsigned i, j;

  T_EQ(strcmp(measurement_names[UI_MEAS_FREQ], "Freq"), 0);
  T_EQ(strcmp(measurement_names[UI_MEAS_VPP], "Vpp"), 0);
  T_EQ(strcmp(measurement_names[UI_MEAS_VAVG], "Vavg"), 0);
  T_EQ(strcmp(measurement_names[7], "Period"), 0);
  T_EQ(strcmp(measurement_names[8], "+Width"), 0);
  T_EQ(strcmp(measurement_names[11], "-Duty"), 0);

  for(i = 0; i < UI_MEAS_COUNT; i++)
    for(j = 0; j < sizeof(values) / sizeof(values[0]); j++)
    {
      int fits = ui_card_fits(&font_2, measurement_names[i], values[j]);

      if(!fits)
        printf("does not fit: %s %s (%u + %u + %u px)\n", measurement_names[i], values[j],
               ui_card_chip_w(&font_2, measurement_names[i]), UI_CARD_GAP, ui_text_width(&font_2, values[j]));
      T_TRUE(fits);
    }

  printf("Freq 1.000kHz: chip %u px, value %u px, card %u px\n", ui_card_chip_w(&font_2, "Freq"),
         ui_text_width(&font_2, "1.000kHz"), UI_CARD_W);
  T_TRUE(ui_card_chip_w(&font_2, "Freq") + UI_CARD_GAP + ui_text_width(&font_2, "1.000kHz") + 15 <= UI_CARD_W);

  //Widths are the font's own advances; fixed-width fonts are not measured
  T_EQ(ui_text_width(&font_2, ""), 0);
  T_TRUE(ui_text_width(&font_3, "Vmax") > ui_text_width(&font_2, "Vmax"));
}

static void test_menu(void)
{
  int row, y;

  //Order and words (plan section 2)
  T_STR(ui_mm_labels[UI_MM_SCOPE].line1, "Scope");
  T_STR(ui_mm_labels[UI_MM_SPECTRUM].line1, "Spectrum");
  T_STR(ui_mm_labels[UI_MM_SPECTRUM].line2, "analyser");
  T_STR(ui_mm_labels[UI_MM_PICTURES].line2, "pictures");
  T_STR(ui_mm_labels[UI_MM_WAVEFORMS].line2, "waveforms");
  T_STR(ui_mm_labels[UI_MM_SETTINGS].line1, "Settings");
  T_STR(ui_mm_labels[UI_MM_DIAGNOSTICS].line1, "Diagnostics");
  T_STR(ui_mm_labels[UI_MM_USB].line1, "USB drive");
  T_EQ(UI_MM_USB, UI_MM_ROWS - 1);

  //Labels fit the button right of the icon (x 45..140) in the 16 px font
  for(row = 0; row < UI_MM_ROWS; row++)
  {
    T_TRUE(ui_text_width(&font_3, ui_mm_labels[row].line1) <= 92);
    T_TRUE(ui_text_width(&font_3, ui_mm_labels[row].line2) <= 92);
  }

  //Hit areas: contiguous, one pitch each, every y of the column maps to exactly the row drawn there
  T_EQ(UI_MM_H, 413);
  T_TRUE(UI_MM_TOP + UI_MM_H <= 480);
  for(y = 0; y < 480; y++)
  {
    int r = ui_mm_row_at(80, (uint32)y);

    if((y < UI_MM_TOP) || (y >= UI_MM_TOP + UI_MM_H))
      T_EQ(r, -1);
    else
    {
      T_EQ(r, (y - UI_MM_TOP) / UI_MM_PITCH);
      T_TRUE((uint32)y >= ui_mm_row_top(r));
      T_TRUE((uint32)y < ui_mm_row_top(r) + UI_MM_PITCH);
    }
  }

  //The drawn button of each row lies inside its own hit area
  for(row = 0; row < UI_MM_ROWS; row++)
  {
    T_EQ(ui_mm_row_at(UI_MM_BTN_X, ui_mm_row_top(row) + UI_MM_BTN_DY), row);
    T_EQ(ui_mm_row_at(UI_MM_BTN_X + UI_MM_BTN_W - 1, ui_mm_row_top(row) + UI_MM_BTN_DY + UI_MM_BTN_H - 1), row);
  }

  //1.00o5's separators at 104 / 163 / 222 / 281 end rows 0..3; the old hit areas this replaces had gaps there
  T_EQ(ui_mm_row_at(80, 104), 0);
  T_EQ(ui_mm_row_at(80, 105), 1);
  T_EQ(ui_mm_row_at(80, 458), UI_MM_USB);
  T_EQ(ui_mm_row_at(80, 459), -1);

  //Left and right edges of the column
  T_EQ(ui_mm_row_at(1, 100), -1);
  T_EQ(ui_mm_row_at(2, 100), 0);
  T_EQ(ui_mm_row_at(148, 100), 0);
  T_EQ(ui_mm_row_at(149, 100), -1);
}

static void test_choose(void)
{
  ui_mm_action a;
  int sp, roll, ph;

  //Spectrum from the scope in roll mode: roll off, warned once, phosphor off
  T_EQ(ui_mm_choose(UI_MM_SPECTRUM, 0, 1, 1, &a), 1);
  T_EQ(a.spectrum, 1);
  T_EQ(a.phosphor, 0);
  T_EQ(a.roll_off, 1);
  T_EQ(a.warn_roll, 1);

  //Without roll: no warning
  T_EQ(ui_mm_choose(UI_MM_SPECTRUM, 0, 0, 0, &a), 1);
  T_EQ(a.spectrum, 1);
  T_EQ(a.roll_off, 0);
  T_EQ(a.warn_roll, 0);

  //Spectrum while in the spectrum: stays, nothing else
  T_EQ(ui_mm_choose(UI_MM_SPECTRUM, 1, 0, 1, &a), 1);
  T_EQ(a.spectrum, 1);
  T_EQ(a.phosphor, 0);
  T_EQ(a.roll_off, 0);

  //Scope from the spectrum: spectrum off, Roll is not switched back on (no action exists for it), variant restored
  for(roll = 0; roll < 2; roll++)
    for(ph = 0; ph < 2; ph++)
    {
      T_EQ(ui_mm_choose(UI_MM_SCOPE, 1, roll, ph, &a), 1);
      T_EQ(a.spectrum, 0);
      T_EQ(a.phosphor, ph);
      T_EQ(a.roll_off, 0);
      T_EQ(a.warn_roll, 0);
    }

  //Utility rows are not mode rows
  for(sp = 0; sp < 2; sp++)
  {
    T_EQ(ui_mm_choose(UI_MM_PICTURES, sp, 0, 0, &a), 0);
    T_EQ(ui_mm_choose(UI_MM_USB, sp, 1, 1, &a), 0);
    T_EQ(a.roll_off, 0);
    T_EQ(ui_mm_choose(-1, sp, 1, 1, &a), 0);
  }
}

int main(void)
{
  test_sig4();
  test_cards();
  test_menu();
  test_choose();
  return t_report("test_ui");
}
