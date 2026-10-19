// test_ui.c - the screens' pure parts (fw 0.2.51): card value formatting and card fit with the device's own font tables
// (ui_fmt.c, font_2.c), the main menu row table, hit areas and mode rule (ui_menu.c), the Settings fold-out and screen
// (ui_settings.c, fw 0.2.55): rows, hit areas against the drawn rectangles, values, text fit, start-up choice, dialog;
// the CTRL page, fold-outs and warnings (ui_ctrl.c, fw 0.2.56); Measurements v2 (ui_meas.c, fw 0.2.65): the MEASURES
// lists, display order, card fit, spectrum cards, THD / noise maths, card sources, the MEASURES menu geometry.
// Run natively.

#include <stdio.h>
#include <string.h>

#include "ui_fmt.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_ctrl.h"
#include "ui_meas.h"
#include "bode_ui.h"
#include "test_util.h"

extern FONTDATA font_2, font_3, font_5;

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
  T_STR(ui_mm_labels[UI_MM_BODE].line1, "Frequency");       //fw 0.2.68: the third mode row; 0.2.69 renamed (owner)
  T_STR(ui_mm_labels[UI_MM_BODE].line2, "response");
  T_EQ(UI_MM_BODE, UI_MM_SPECTRUM + 1);
  T_EQ(UI_MM_PICTURES, UI_MM_BODE + 1);
  T_EQ(UI_MM_ROWS, 8);
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
  T_EQ(UI_MM_H, 416);
  T_EQ(UI_MM_PITCH, 52);
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

  //fw 0.2.68: eight rows of 52 px, the separators at 97 / 149 / ... end rows 0..6; the button keeps 35 px with at least
  //8 px to the separator below and 9 px to the one above
  T_EQ(ui_mm_row_at(80, 97), 0);
  T_EQ(ui_mm_row_at(80, 98), 1);
  T_EQ(ui_mm_row_at(80, 461), UI_MM_USB);
  T_EQ(ui_mm_row_at(80, 462), -1);
  T_TRUE(UI_MM_BTN_DY + UI_MM_BTN_H + 8 <= UI_MM_PITCH);
  T_TRUE(UI_MM_BTN_H >= 35);

  //Left and right edges of the column
  T_EQ(ui_mm_row_at(1, 60), -1);
  T_EQ(ui_mm_row_at(2, 60), 0);
  T_EQ(ui_mm_row_at(148, 60), 0);
  T_EQ(ui_mm_row_at(149, 60), -1);
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

  //fw 0.2.68: Frequency response from the scope, the spectrum, roll mode: the other modes off, Roll off; fw 0.2.69: with
  //its own roll note
  for(sp = 0; sp < 2; sp++)
    for(roll = 0; roll < 2; roll++)
      for(ph = 0; ph < 2; ph++)
      {
        T_EQ(ui_mm_choose(UI_MM_BODE, sp, roll, ph, &a), 1);
        T_EQ(a.bode, 1);
        T_EQ(a.spectrum, 0);
        T_EQ(a.phosphor, 0);
        T_EQ(a.roll_off, roll);
        T_EQ(a.warn_roll, roll);

        //Scope and Spectrum end it
        T_EQ(ui_mm_choose(UI_MM_SCOPE, sp, roll, ph, &a), 1);
        T_EQ(a.bode, 0);
        T_EQ(ui_mm_choose(UI_MM_SPECTRUM, sp, roll, ph, &a), 1);
        T_EQ(a.bode, 0);
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

//----------------------------------------------------------------------------------------------------------------------------------
// fw 0.2.56: CTRL page, fold-outs, value names, warnings, spectrum cards (ui_ctrl.c) and their formatters (ui_fmt.c)

static void test_fmt_spectrum(void)
{
  ui_fmt_mhz(b, 146484);            T_STR(b, "146.5Hz");
  ui_fmt_mhz(b, 1000000);           T_STR(b, "1.000kHz");
  ui_fmt_mhz(b, 976562);            T_STR(b, "976.6Hz");
  ui_fmt_mhz(b, 25000000000ull);    T_STR(b, "25.00MHz");
  ui_fmt_mhz(b, 12207031250ull);    T_STR(b, "12.21MHz");
  ui_fmt_mhz(b, 100000000000ull);   T_STR(b, "100.0MHz");
  ui_fmt_mhz(b, 0);                 T_STR(b, "0Hz");

  ui_fmt_cdb(b, 270, "dBV");        T_STR(b, "+2.7dBV");
  ui_fmt_cdb(b, -10000, "dBV");     T_STR(b, "-100.0dBV");
  ui_fmt_cdb(b, 4, "dBV");          T_STR(b, "0.0dBV");
  ui_fmt_cdb(b, -5, "dBA");         T_STR(b, "-0.1dBA");
  ui_fmt_cdb(b, 6000, "dBV");       T_STR(b, "+60.0dBV");
  ui_fmt_cdb(b, -1234, "dBV");      T_STR(b, "-12.3dBV");
}

static void test_ctrl_page(void)
{
  int    item, sp, prev;
  uint32 y;

  //Every y below CTRL belongs to exactly one control; the areas join and run to the bottom of the bar
  for(sp = 0; sp < 2; sp++)
  {
    prev = UI_CTRL_NONE;

    for(y = 0; y < 480; y++)
    {
      item = ui_ctrl_item_at(y, sp);

      if(y < 58)
        T_EQ(item, UI_CTRL_NONE);
      else if(sp && (y >= ui_ctrl_hit_top(UI_CTRL_TRIG50)))
        T_EQ(item, UI_CTRL_NONE);                //Spectrum: the 50 % slot is empty
      else
      {
        T_TRUE(item != UI_CTRL_NONE);
        T_TRUE((item == prev) || (item == prev + 1));
        prev = item;
      }
    }
  }

  //The drawn controls lie inside their own hit areas
  for(y = UI_CTRL_DISPLAY_Y; y < UI_CTRL_DISPLAY_Y + UI_BTN_H; y++)   T_EQ(ui_ctrl_item_at(y, 0), UI_CTRL_DISPLAY);
  for(y = UI_CTRL_ANALYSIS_Y; y < UI_CTRL_ANALYSIS_Y + UI_BTN_H; y++) T_EQ(ui_ctrl_item_at(y, 1), UI_CTRL_ANALYSIS);
  for(y = UI_CTRL_GRID_Y; y < UI_CTRL_GRID_Y + UI_BTN_H; y++)         T_EQ(ui_ctrl_item_at(y, 0), UI_CTRL_GRID);
  for(y = UI_CTRL_TRIG50_Y; y < UI_CTRL_TRIG50_Y + UI_BTN_H; y++)     T_EQ(ui_ctrl_item_at(y, 0), UI_CTRL_TRIG50);

  //Steppers: 82 px; the V+ bar and the upper half of the centre are V+, the rest V-; each half gets at least 44 px
  T_EQ(UI_STEP_H, 82);
  for(y = 0; y < UI_STEP_BAR_H; y++)
  {
    T_EQ(ui_ctrl_item_at(UI_CTRL_STEP1_Y + y, 0), UI_CTRL_CH1_UP);
    T_EQ(ui_ctrl_item_at(UI_CTRL_STEP1_Y + UI_STEP_H - 1 - y, 0), UI_CTRL_CH1_DOWN);
    T_EQ(ui_ctrl_item_at(UI_CTRL_STEP2_Y + y, 1), UI_CTRL_CH2_UP);
    T_EQ(ui_ctrl_item_at(UI_CTRL_STEP2_Y + UI_STEP_H - 1 - y, 1), UI_CTRL_CH2_DOWN);
  }
  T_EQ(ui_ctrl_item_at(UI_CTRL_STEP1_Y + 40, 0), UI_CTRL_CH1_UP);
  T_EQ(ui_ctrl_item_at(UI_CTRL_STEP1_Y + 41, 0), UI_CTRL_CH1_DOWN);
  for(item = UI_CTRL_CH1_UP; item <= UI_CTRL_CH2_DOWN; item++)
    T_TRUE(ui_ctrl_hit_bottom(item) + 1 - ui_ctrl_hit_top(item) >= 44);

  //The steppers stack between Grid and 50 % TRIG without overlapping them
  T_TRUE(UI_CTRL_STEP1_Y >= UI_CTRL_GRID_Y + UI_BTN_H);
  T_TRUE(UI_CTRL_STEP2_Y >= UI_CTRL_STEP1_Y + UI_STEP_H);
  T_TRUE(UI_CTRL_TRIG50_Y >= UI_CTRL_STEP2_Y + UI_STEP_H);

  //First page: Spectrum leaves the cursor and measurement slots empty, Scope shows all eight
  for(item = UI_P1_CTRL; item <= UI_P1_SAVEWAVE; item++)
  {
    T_EQ(ui_p1_shown(item, 0), 1);
    T_EQ(ui_p1_shown(item, 1), !((item == UI_P1_TCURSOR) || (item == UI_P1_VCURSOR)));   //fw 0.2.65: MEASURES too
    T_EQ(ui_p1_shown(item, 2), item != UI_P1_VCURSOR);   //fw 0.2.69: T CURSOR (C1 / C2) and MEASURES in Frequency response
  }

  //Button texts fit the 51 px buttons (2 px margins) in the 13 px font
  {
    static const char *labels[] = { "Display", "Trace", "style", "Analyse", "Grid", "2048 pt", "4096 pt", "1024 pt" };
    unsigned i;
    int      v, ph;

    for(i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
      T_TRUE(ui_text_width(&font_2, labels[i]) <= UI_BAR_W - 4);
    for(i = 0; i < UI_TRACE_COUNT; i++)
      T_TRUE(ui_text_width(&font_2, ui_trace_names[i]) <= UI_BAR_W - 4);
    for(v = 0; v < 2; v++)
      for(ph = 0; ph < 2; ph++)
        T_TRUE(ui_text_width(&font_2, ui_display_value(0, v, ph)) <= UI_BAR_W - 4);
    for(v = 0; v < UI_SPEC_VIEW_COUNT; v++)
      T_TRUE(ui_text_width(&font_2, ui_display_value(1, v, 0)) <= UI_BAR_W - 4);
    T_STR(ui_display_value(0, UI_VIEW_NORMAL, 0), "Normal");
    T_STR(ui_display_value(0, UI_VIEW_XY, 0), "X-Y");
    T_STR(ui_display_value(1, 0, 1), "Spec");
  }

  //RUN/STOP (fw 0.2.68): the action labels fit the 51 px button with 2 px margins in the 16 px font, the chip texts the
  //38 px chip at 1.00o5's offsets; one view for both: running or a host acquisition is RUN, a UI single armed is WAIT
  {
    int r, w, h;

    T_TRUE(ui_text_width(&font_3, ui_run_action[0]) <= UI_BAR_W - 4);
    T_TRUE(ui_text_width(&font_3, ui_run_action[1]) <= UI_BAR_W - 4);
    T_STR(ui_run_action[0], "RUN");
    T_STR(ui_run_action[1], "STOP");
    for(r = 0; r < 3; r++)
      T_TRUE(ui_text_width(&font_3, ui_run_chip[r]) <= 38 - 2);
    T_EQ((38 - ui_text_width(&font_3, ui_run_chip[UI_RUN_STOP])) / 2, 1);   //centred = 1.00o5's offsets 1, 5, 2
    T_EQ((38 - ui_text_width(&font_3, ui_run_chip[UI_RUN_RUN])) / 2, 5);
    T_EQ((38 - ui_text_width(&font_3, ui_run_chip[UI_RUN_WAIT])) / 2, 2);
    for(r = 0; r < 2; r++)
      for(w = 0; w < 2; w++)
        for(h = 0; h < 2; h++)
          T_EQ(ui_run_view(r, w, h), r ? (w ? UI_RUN_WAIT : UI_RUN_RUN) : (h ? UI_RUN_RUN : UI_RUN_STOP));
    T_STR(ui_run_chip[UI_RUN_STOP], "STOP");
    T_STR(ui_run_chip[UI_RUN_RUN], "RUN");
    T_STR(ui_run_chip[UI_RUN_WAIT], "WAIT");
  }

  //Screen brightness (fw 0.2.68): the sun goes to 100 % and back to where it came from; a value set at 100 by the slider
  //goes back to the remembered one (or the default); the sector byte round-trips; 1.00o5's flag migrates
  {
    uint8  cur, back = 0, v;
    uint16 b;

    for(v = 0; v < 100; v++)
    {
      back = 77;
      cur  = ui_bright_sun(v, &back);
      T_EQ(cur, 100);
      T_EQ(back, v);
      T_EQ(ui_bright_sun(cur, &back), v);
    }
    back = 100;
    T_EQ(ui_bright_sun(100, &back), UI_BRIGHT_DEFAULT);
    back = 35;
    T_EQ(ui_bright_sun(100, &back), 35);

    for(v = 0; v <= 100; v++)
    {
      b = ui_bright_byte(v);
      T_TRUE(b & UI_BRIGHT_SECTOR_NEW);
      cur = 60;
      ui_bright_load(b, &cur, &back);
      T_EQ(cur, 60);
      T_EQ(back, (v < 100) ? v : UI_BRIGHT_DEFAULT);
    }

    cur = 40;  ui_bright_load(1, &cur, &back);  T_EQ(cur, 100);  T_EQ(back, 40);      //1.00o5 max light on
    cur = 40;  ui_bright_load(0, &cur, &back);  T_EQ(cur, 40);   T_EQ(back, 40);      //off
    cur = 100; ui_bright_load(1, &cur, &back);  T_EQ(cur, 100);  T_EQ(back, UI_BRIGHT_DEFAULT);
    cur = 250; ui_bright_load(0, &cur, &back);  T_EQ(cur, UI_BRIGHT_DEFAULT);
  }

  //ACQ menu pass time (fw 0.2.63): inside the Memory column right of its rule at x 304 and left of the menu's edge at
  //380, below the rule at y 211 and above the bottom edge (386 - 8); the label and the widest value fit its width
  T_TRUE(UI_ACQ_PASS_X > 304);
  T_TRUE(UI_ACQ_PASS_X + UI_ACQ_PASS_W < 378);
  T_TRUE(UI_ACQ_PASS_Y > 211);
  T_TRUE(UI_ACQ_PASS_Y + 32 <= 378);
  T_TRUE(ui_text_width(&font_2, ui_acq_pass_label) <= UI_ACQ_PASS_W);
  T_TRUE(ui_text_width(&font_2, "99999 ms") <= UI_ACQ_PASS_W);
}

//A one-way row: the label at the left, the value at the right, 10 px in (and 30 px to spare between them); a two-way
//row: - and + boxes at the ends, the texts 40 px in
static int row_fits(const ui_fold *f, const char *label, const char *value, int twoway)
{
  uint32 w = ui_text_width(&font_3, label) + 12 + ui_text_width(&font_3, value);

  return twoway ? (w + 2 * UI_FOLD_TWOWAY_IN <= ui_fold_row_w(f)) : (w + 2 * UI_FOLD_VALUE_IN + 30 <= ui_fold_row_w(f));
}

//A switch row: the label (16 px font) and its second line (13 px) from UI_FOLD_TEXT_X
static int switch_fits(const ui_fold *f, const char *label, const char *note)
{
  return (UI_FOLD_TEXT_X + ui_text_width(&font_3, label) + 4 <= ui_fold_row_w(f)) &&
         (UI_FOLD_TEXT_X + ui_text_width(&font_2, note) + 4 <= ui_fold_row_w(f));
}

static void check_fold(const ui_fold *f)
{
  uint32 r, y;

  //Inside the trace area, left of the right bar, below the top bar
  T_TRUE(f->x + f->w <= 730);
  T_TRUE(f->x >= 2);
  T_TRUE(f->y >= UI_FOLD_TOP_MIN);
  T_TRUE(f->y + f->h <= 480);

  //Each row's drawn box hits its own row; the hit areas between two rows join; the title does nothing; outside closes
  for(r = 0; r < f->rows; r++)
  {
    uint32 ry = ui_fold_row_y(f, r);

    T_TRUE(ry + UI_FOLD_ROW_H <= f->y + f->h);
    for(y = ry; y < ry + UI_FOLD_ROW_H; y++)
    {
      T_EQ(ui_fold_hit(f, ui_fold_row_x(f), y), (int)r);
      T_EQ(ui_fold_hit(f, ui_fold_row_x(f) + ui_fold_row_w(f) - 1, y), (int)r);
    }

    if((r + 1 < f->rows) && (f->sep != r + 1))
      for(y = ry + UI_FOLD_ROW_H; y < ui_fold_row_y(f, r + 1); y++)
        T_TRUE(ui_fold_hit(f, f->x + 20, y) >= 0);
  }

  T_EQ(ui_fold_hit(f, f->x + 20, f->y + 5), UI_FOLD_IN);
  T_EQ(ui_fold_hit(f, f->x - 1, f->y + 40), UI_FOLD_OUT);
  T_EQ(ui_fold_hit(f, f->x + f->w, f->y + 40), UI_FOLD_OUT);
  T_EQ(ui_fold_hit(f, f->x + 20, f->y + f->h), UI_FOLD_OUT);
  T_EQ(ui_fold_hit(f, 760, 100), UI_FOLD_OUT);       //the right bar

  //Two-way rows: the halves
  T_EQ(ui_fold_dir(f, f->x + 1), -1);
  T_EQ(ui_fold_dir(f, f->x + f->w - 1), 1);
}

static void test_folds(void)
{
  ui_fold f;
  unsigned i;
  char     t[32];

  //Scope Display: Normal, X-Y | Phosphor, Persistence, Palette, Clear (the separator before the Phosphor switch)
  ui_fold_layout(&f, 206, 6, 2, UI_CTRL_DISPLAY_Y);
  check_fold(&f);
  T_EQ(f.x, 522);
  T_EQ(f.y, 52);                                      //the mockup's top
  T_EQ(ui_fold_row_y(&f, 2) - ui_fold_row_y(&f, 1), UI_FOLD_PITCH + UI_FOLD_SEP);
  T_TRUE(switch_fits(&f, "Phosphor", "999 wfm/s"));
  T_TRUE(switch_fits(&f, "Phosphor", ui_phos_note_roll));
  for(i = 0; i < UI_PERSIST_COUNT; i++)
    T_TRUE(row_fits(&f, "Persistence", ui_persist_names[i], 0));
  for(i = 0; i < 2; i++)
    T_TRUE(row_fits(&f, "Palette", ui_palette_names[i], 0));

  //Without the phosphor rows
  ui_fold_layout(&f, 206, 3, 2, UI_CTRL_DISPLAY_Y);
  check_fold(&f);

  //Trace style: four choices beside its button
  ui_fold_layout(&f, 206, UI_TRACE_COUNT, 0, UI_CTRL_ANALYSIS_Y);
  check_fold(&f);
  T_EQ(f.y, 112);

  //Spectrum Display: three variants
  ui_fold_layout(&f, 206, UI_SPEC_VIEW_COUNT, 0, UI_CTRL_DISPLAY_Y);
  check_fold(&f);
  for(i = 0; i < UI_SPEC_VIEW_COUNT; i++)
    T_TRUE(ui_text_width(&font_3, ui_spec_view_names[i]) + 50 <= ui_fold_row_w(&f));

  //Spectrum Analyse: Analyse, FFT length, Window, Average | Scale, Ref level, Reset; moved up to fit
  ui_fold_layout(&f, 228, 7, 4, UI_CTRL_ANALYSIS_Y);
  check_fold(&f);
  T_TRUE(f.y < 112);
  for(i = 0; i < 3; i++)
    T_TRUE(row_fits(&f, "Analyse", ui_spec_chan_names[i], 0));
  T_TRUE(row_fits(&f, "FFT length", "4096", 0));
  for(i = 0; i < 3; i++)
    T_TRUE(row_fits(&f, "Window", ui_spec_window_names[i], 0));
  for(i = 0; i < 5; i++)
    T_TRUE(row_fits(&f, "Average", ui_spec_avg_names[i], 0));
  T_TRUE(row_fits(&f, "Scale", "20 dB/div", 0));
  T_TRUE(row_fits(&f, "Ref level", "-100 dBV", 1));
  T_TRUE(row_fits(&f, "Ref level", "+60 dBA", 1));

  //Too many rows are cut to the maximum; a separator at or past the end is dropped
  ui_fold_layout(&f, 206, 20, 30, 300);
  T_EQ(f.rows, UI_FOLD_ROWS_MAX);
  T_EQ(f.sep, 0);
  check_fold(&f);

  (void)t;
}

static void test_warnings(void)
{
  ui_warn_in w;
  const char *s;

  memset(&w, 0, sizeof(w));
  w.en[0] = 1;
  w.running = 1;
  T_STR(ui_scope_warning(&w), "");

  w.untrig = 1;
  T_STR(ui_scope_warning(&w), "No trigger: check the trigger level");
  w.running = 0;
  T_STR(ui_scope_warning(&w), "");                 //stopped: the frozen record is not a live problem
  w.running = 1;

  w.over[1] = 1;                                   //CH2 off: its flag means nothing
  T_STR(ui_scope_warning(&w), "No trigger: check the trigger level");
  w.en[1] = 1;
  T_STR(ui_scope_warning(&w), "CH2 overrange: increase V/div");
  w.over[0] = 1;
  T_STR(ui_scope_warning(&w), "CH1 overrange: increase V/div");

  w.xy = 1;
  w.en[1] = 0;
  T_STR(ui_scope_warning(&w), "X-Y needs both channels: switch on CH1 and CH2");
  w.en[0] = 0;
  T_STR(ui_scope_warning(&w), "No channel enabled: switch on CH1 or CH2");

  //Every warning fits the 726 px plot in the 13 px font
  s = "X-Y needs both channels: switch on CH1 and CH2";
  T_TRUE(ui_text_width(&font_2, s) <= 700);

  //fw 0.2.78: the note after a host request closed the menus (at most 50 characters, one line in every mode's warning
  //slot: the Scope's 13 px line, the Frequency response's)
  T_STR(ui_yield_note, "Host command: menu closed");
  T_TRUE(strlen(ui_yield_note) <= 50);
  T_TRUE(ui_text_width(&font_2, ui_yield_note) <= 700);
  T_TRUE(ui_text_width(&font_3, ui_yield_note) <= 500);
}

static void test_spec_cards(void)
{
  static const char *values[] = { "25.00MHz", "999.9kHz", "-100.0dBV", "+60.0dBA", "12.21kHz", "999.9s", "100.0%",
                                  "0.01%", "f0 too low", "f0 too high", "more" };
  ui_ms_card c[UI_MS_ROW_CARDS];
  ui_ms_spec s;
  uint8      shown[2];
  int        n, i;
  unsigned   j;

  //Defaults (plan section 3a): CH1 Peak, Level, THD; CH2 Peak, Level; RBW, Time; both channels fill the row exactly
  ui_ms_spec_defaults(&s);
  shown[0] = 1; shown[1] = 1;
  n = ui_ms_spec_cards(&s, shown, 1, c);
  T_EQ(n, UI_MS_ROW_CARDS);
  T_EQ(c[0].kind, UI_MS_SP_PEAK);  T_EQ(c[0].ch, 0);
  T_EQ(c[1].kind, UI_MS_SP_LEVEL); T_EQ(c[1].ch, 0);
  T_EQ(c[2].kind, UI_MS_SP_THD);   T_EQ(c[2].ch, 0);
  T_EQ(c[3].kind, UI_MS_SP_PEAK);  T_EQ(c[3].ch, 1);
  T_EQ(c[4].kind, UI_MS_SP_LEVEL); T_EQ(c[4].ch, 1);
  T_EQ(c[5].kind, UI_MS_SP_MORE);  T_EQ(c[5].ch, 2);       //RBW and Time cut: "+2"

  //Without the waterfall Time drops and everything fits
  n = ui_ms_spec_cards(&s, shown, 0, c);
  T_EQ(n, 6);
  T_EQ(c[5].kind, UI_MS_SP_RBW);

  //The static default in spectrum.c is the same
  {
    ui_ms_spec d = { { { 1, 2, 3, 0 }, { 1, 2, 0, 0 }, { 1, 0, 0, 2 } } };
    T_MEM(&d, &s, sizeof(s));
  }

  //The last of six cards ends inside the 726 px plot (x 2..727)
  T_TRUE(ui_ms_card_x(0, UI_MS_ROW_CARDS - 1) + UI_CARD_W <= 728);
  T_TRUE(UI_MS_SPEC_Y + UI_CARD_H <= 480);

  shown[0] = 0;
  n = ui_ms_spec_cards(&s, shown, 0, c);
  T_EQ(n, 3);
  T_EQ(c[0].ch, 1);
  T_EQ(c[2].kind, UI_MS_SP_RBW);

  shown[1] = 0;
  n = ui_ms_spec_cards(&s, shown, 0, c);
  T_EQ(n, 1);
  T_EQ(c[0].kind, UI_MS_SP_RBW);

  //The user's order is kept: Noise, then Peak for CH1; Center before RBW
  ui_ms_clear(s.rank[0], 4);
  ui_ms_toggle(s.rank[0], 4, UI_MS_SP_NOISE);
  ui_ms_toggle(s.rank[0], 4, UI_MS_SP_PEAK);
  ui_ms_clear(s.rank[2], 4);
  ui_ms_toggle(s.rank[2], 4, UI_MS_SP_CENTER - 4);
  ui_ms_toggle(s.rank[2], 4, UI_MS_SP_RBW - 4);
  shown[0] = 1;
  n = ui_ms_spec_cards(&s, shown, 1, c);
  T_EQ(n, 4);
  T_EQ(c[0].kind, UI_MS_SP_NOISE);
  T_EQ(c[1].kind, UI_MS_SP_PEAK);
  T_EQ(c[2].kind, UI_MS_SP_CENTER);
  T_EQ(c[3].kind, UI_MS_SP_RBW);

  //Settings words: marker, round trip, bad words give the group's defaults
  for(i = 0; i < UI_MS_SP_GROUPS; i++)
  {
    ui_ms_spec t;
    uint16 w = ui_ms_spec_word(&s, i);

    T_EQ(w & 0xF000, UI_MS_SPEC_MARKER);
    ui_ms_spec_defaults(&t);
    T_EQ(ui_ms_spec_restore(&t, i, w), 1);
    T_MEM(t.rank[i], s.rank[i], 4);
  }
  {
    ui_ms_spec t, d;

    ui_ms_spec_defaults(&d);
    memset(&t, 0, sizeof(t));
    T_EQ(ui_ms_spec_restore(&t, 0, 0x0000), 0);           //an old sector: no marker
    T_MEM(t.rank[0], d.rank[0], 4);
    T_EQ(ui_ms_spec_restore(&t, 1, 0xA007), 0);           //rank 7 > 4
    T_MEM(t.rank[1], d.rank[1], 4);
    T_EQ(ui_ms_spec_restore(&t, 2, 0xA000), 1);           //valid: nothing chosen
    T_EQ(t.rank[2][0] | t.rank[2][1] | t.rank[2][2] | t.rank[2][3], 0);
    T_EQ(ui_ms_spec_restore(&t, 0, 0xA000 | (2 << 3) | (2 << 6)), 1);   //ties renumbered in item order
    T_EQ(t.rank[0][1], 1);
    T_EQ(t.rank[0][2], 2);
  }
  T_EQ(UI_MS_SPEC_WORD_CH1, 223);
  T_EQ(UI_MS_SPEC_WORD_CH2, 222);
  T_EQ(UI_MS_SPEC_WORD_VIEW, 221);

  for(i = 0; i < UI_MS_SP_KINDS; i++)
    for(j = 0; j < sizeof(values) / sizeof(values[0]); j++)
    {
      int fits = ui_card_fits(&font_2, ui_ms_sp_names[i], values[j]);

      if(!fits)
        printf("does not fit: %s %s\n", ui_ms_sp_names[i], values[j]);
      T_TRUE(fits);
    }
}

//Selection model: ranks, order, toggle, select all, clear, Scope defaults
static void test_meas_order(void)
{
  uint8 r[12], o[12];
  int   n, i;

  //Defaults show as Freq, Vpp, Vavg (plan 3a), not in table order
  ui_ms_scope_defaults(r);
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 3);
  T_EQ(o[0], UI_MEAS_FREQ);
  T_EQ(o[1], UI_MEAS_VPP);
  T_EQ(o[2], UI_MEAS_VAVG);

  //1.00o5 settings (every chosen byte 1): table order
  memset(r, 0, sizeof(r));
  r[6] = 1; r[2] = 1; r[4] = 1;
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 3);
  T_EQ(o[0], 2); T_EQ(o[1], 4); T_EQ(o[2], 6);

  //Turning one on puts it last; turning one off closes the gap; turning it on again puts it last again
  ui_ms_scope_defaults(r);
  T_EQ(ui_ms_toggle(r, 12, 0), 1);                     //Vmax
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 4); T_EQ(o[3], 0);
  T_EQ(ui_ms_toggle(r, 12, UI_MEAS_VPP), 0);
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 3);
  T_EQ(o[0], UI_MEAS_FREQ); T_EQ(o[1], UI_MEAS_VAVG); T_EQ(o[2], 0);
  T_EQ(r[UI_MEAS_FREQ], 1); T_EQ(r[UI_MEAS_VAVG], 2); T_EQ(r[0], 3); T_EQ(r[UI_MEAS_VPP], 0);
  ui_ms_toggle(r, 12, UI_MEAS_VPP);
  T_EQ(r[UI_MEAS_VPP], 4);

  //Out of range items do nothing
  T_EQ(ui_ms_toggle(r, 12, 12), 0);
  T_EQ(ui_ms_toggle(r, 12, -1), 0);

  //Select All keeps the chosen order and appends the rest in table order; ranks are a permutation of 1..12
  ui_ms_select_all(r, 12);
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 12);
  T_EQ(o[0], UI_MEAS_FREQ); T_EQ(o[3], UI_MEAS_VPP);
  T_EQ(o[4], 1); T_EQ(o[5], 3); T_EQ(o[11], 11);
  {
    int seen = 0;
    for(i = 0; i < 12; i++)
      seen |= 1 << (r[i] - 1);
    T_EQ(seen, 0xFFF);
  }

  ui_ms_clear(r, 12);
  T_EQ(ui_ms_order(r, 12, o), 0);

  //Garbage ranks (a damaged sector) still give a working order and a clean renumbering on the next change
  for(i = 0; i < 12; i++)
    r[i] = (uint8)(255 - i * 7);
  ui_ms_toggle(r, 12, 5);
  n = ui_ms_order(r, 12, o);
  T_EQ(n, 11);
  for(i = 0; i < 11; i++)
    T_EQ(r[o[i]], i + 1);
  T_EQ(o[0], 11);                                      //the lowest garbage rank first
}

//Card fit: one row, CH1 three places (six without the CH2 block), CH2 three; overflow shows "+N" in the last place
static void test_meas_fit(void)
{
  char t[8];
  int  more, fit;

  T_EQ(ui_ms_fit(3, 3, &more), 3); T_EQ(more, 0);
  T_EQ(ui_ms_fit(2, 3, &more), 2); T_EQ(more, 0);
  T_EQ(ui_ms_fit(0, 3, &more), 0); T_EQ(more, 0);
  fit = ui_ms_fit(4, 3, &more);
  T_EQ(fit, 2); T_EQ(more, 2);                         //two values and "+2": nothing cut silently
  T_EQ(fit + more, 4);
  fit = ui_ms_fit(12, 6, &more);
  T_EQ(fit, 5); T_EQ(more, 7);
  T_EQ(ui_ms_fit(5, 0, &more), 0); T_EQ(more, 0);

  T_EQ(ui_ms_scope_cap(0, 1), 3);
  T_EQ(ui_ms_scope_cap(0, 0), 6);
  T_EQ(ui_ms_scope_cap(1, 1), 3);
  T_EQ(ui_ms_scope_cap(1, 0), 3);

  //The places: CH1 from x 5, CH2 from x 366, 120 px apart; no overlap between the blocks, all inside the plot
  T_EQ(ui_ms_card_x(0, 0), 5);
  T_EQ(ui_ms_card_x(1, 0), 366);
  T_TRUE(ui_ms_card_x(0, 2) + UI_CARD_W < ui_ms_card_x(1, 0));
  T_TRUE(ui_ms_card_x(1, 2) + UI_CARD_W <= 728);
  T_TRUE(ui_ms_card_x(0, 5) + UI_CARD_W <= 728);
  T_TRUE(UI_MS_SCOPE_Y + UI_CARD_H <= 452);            //above the record-position slider

  T_STR(ui_ms_more_text(t, 2), "+2");
  T_STR(ui_ms_more_text(t, 11), "+11");
  T_TRUE(ui_card_fits(&font_2, "+11", "more"));
}

//THD and noise floor maths on synthetic level arrays
static void test_meas_spectrum_maths(void)
{
  static int16 db[1025];
  int32 thd;
  int   k;

  //pow: 0 dB = 1, -10 dB = 0.1, +10 dB = 10, -3 dB about 0.501, clamps
  T_EQ(ui_ms_pow_q24(0), 1 << 24);
  T_EQ(ui_ms_pow_q24(-1000), 1677721);
  T_EQ(ui_ms_pow_q24(1000), 167772160);
  T_TRUE(ui_ms_pow_q24(-300) > 8400000 && ui_ms_pow_q24(-300) < 8420000);
  T_TRUE(ui_ms_pow_q24(-305) < ui_ms_pow_q24(-300));
  T_EQ(ui_ms_pow_q24(-20000), ui_ms_pow_q24(-12000));
  T_EQ(ui_ms_pow_q24(9000), ui_ms_pow_q24(2000));

  //A tone at bin 20 (0 dBV over three bins), harmonic 2 at -20 dBc and 3 at -30 dBc on the same three-bin shape:
  //THD = sqrt(0.01 + 0.001) = 10.49 %
  for(k = 0; k <= 1024; k++)
    db[k] = -12000;
  db[19] = -600; db[20] = 0; db[21] = -600;
  db[40] = -2000; db[39] = -2600; db[41] = -2600;
  db[61] = -3000; db[60] = -3600; db[62] = -3600;      //one bin off: found by the search window
  thd = ui_ms_thd(db, 1024, 20);
  T_TRUE((thd >= 1045) && (thd <= 1053));

  //No harmonics: below 0.01 %
  db[40] = db[39] = db[41] = db[61] = db[60] = db[62] = -12000;
  T_TRUE(ui_ms_thd(db, 1024, 20) <= 1);

  //Limits: fundamental too close to DC, second harmonic past the axis
  T_EQ(ui_ms_thd(db, 1024, 4), UI_MS_THD_F0_LOW);
  T_EQ(ui_ms_thd(db, 1024, 600), UI_MS_THD_F0_HIGH);
  T_EQ(ui_ms_thd(db, 40, 20), UI_MS_THD_F0_HIGH);
  T_TRUE(ui_ms_thd(db, 41, 20) >= 0);

  //Noise: median of the bins + 1.59 dB, unaffected by a few strong tones
  for(k = 0; k <= 1024; k++)
    db[k] = (int16)(-8000 + (k % 5) * 100);            //-80.0 .. -76.0 dB evenly: median -78.0
  db[100] = 0; db[200] = 0; db[300] = 0;
  T_EQ(ui_ms_noise_cdb(db, 2, 1024), -7800 + UI_MS_NOISE_MEDIAN_CDB);
  T_EQ(ui_ms_noise_cdb(db, 5, 5), -8000 + UI_MS_NOISE_MEDIAN_CDB);
  db[7] = -30000;                                      //clamped to the histogram's range
  T_EQ(ui_ms_noise_cdb(db, 7, 7), -20000 + UI_MS_NOISE_MEDIAN_CDB);
}

//Card sources: none by default; a registered one is returned for its item only
static void test_src_fn(int ch, char *text) { strcpy(text, ch ? "2" : "1"); }

static void test_meas_sources(void)
{
  int i;

  for(i = 0; i < UI_MEAS_COUNT; i++)
    T_TRUE(ui_ms_get_source(i) == 0);

  ui_ms_set_source(UI_MEAS_FREQ, test_src_fn);
  T_TRUE(ui_ms_get_source(UI_MEAS_FREQ) == test_src_fn);
  T_TRUE(ui_ms_get_source(7) == 0);
  T_TRUE(ui_ms_get_source(-1) == 0);
  T_TRUE(ui_ms_get_source(12) == 0);
  ui_ms_set_source(99, test_src_fn);                   //ignored
  ui_ms_set_source(UI_MEAS_FREQ, 0);
  T_TRUE(ui_ms_get_source(UI_MEAS_FREQ) == 0);
}

//MEASURES menu: 44 px rows, cells inside the box and apart, every tap inside the box on a cell (title aside), texts
//fit their cells, the per-mode title
static void test_meas_menu(void)
{
  ui_msm_cell cells[UI_MSM_CELLS_MAX], c;
  int mode, n, i, j, counts[8];
  uint32 x, y, top;

  T_STR(ui_msm_title(UI_MS_SCOPE), "Scope measurements");
  T_STR(ui_msm_title(UI_MS_SPECTRUM), "Spectrum measurements");
  T_STR(ui_msm_title(UI_MS_BODE), "Frequency response measurements");

  for(mode = 0; mode < 3; mode++)
  {
    top = ui_msm_top(mode);
    n   = ui_msm_cells(mode, cells);

    T_EQ(top + ui_msm_height(mode), UI_MSM_BOTTOM);
    T_TRUE(top >= 46);                                 //below the top bar
    T_TRUE(ui_text_width(&font_3, ui_msm_title(mode)) + 8 <= UI_MSM_W);
    T_EQ(n, (mode == UI_MS_SCOPE) ? 40 : (mode == UI_MS_SPECTRUM) ? 18 : 14);

    memset(counts, 0, sizeof(counts));

    for(i = 0; i < n; i++)
    {
      const char *t = ui_msm_text(mode, &cells[i]);
      const FONTDATA *f = (cells[i].what == UI_MSC_ITEM) ? &font_3 : &font_2;

      counts[cells[i].what]++;
      T_EQ(cells[i].h, UI_MSM_ROW_H);
      T_TRUE(cells[i].w >= 48);
      T_TRUE(cells[i].x > UI_MSM_X);
      T_TRUE(cells[i].x + cells[i].w <= UI_MSM_X + UI_MSM_W - 1);
      T_TRUE(cells[i].y > top + UI_MSM_TITLE_H);
      T_TRUE(cells[i].y + cells[i].h <= UI_MSM_BOTTOM - 1);
      T_TRUE((t[0] != 0) == (cells[i].what != UI_MSC_EMPTY));
      T_TRUE(ui_text_width(f, t) + 4 <= cells[i].w);

      //The channels' cells stay in their half (Frequency response has one block across the width)
      if((mode != UI_MS_BODE) && (cells[i].group < 2) && !((mode == UI_MS_SPECTRUM) && (cells[i].group == UI_MS_SP_VIEW)))
      {
        if(cells[i].group == 0)
          T_TRUE(cells[i].x + cells[i].w <= 480);
        else
          T_TRUE(cells[i].x >= 482);
      }

      for(j = 0; j < i; j++)
        T_TRUE((cells[i].x + cells[i].w <= cells[j].x) || (cells[j].x + cells[j].w <= cells[i].x) ||
               (cells[i].y + cells[i].h <= cells[j].y) || (cells[j].y + cells[j].h <= cells[i].y));
    }

    if(mode == UI_MS_SCOPE)
    {
      T_EQ(counts[UI_MSC_SOURCE], 10);
      T_EQ(counts[UI_MSC_ALL], 2); T_EQ(counts[UI_MSC_CLEAR], 2); T_EQ(counts[UI_MSC_HIDE], 2);
      T_EQ(counts[UI_MSC_ITEM], 24);
    }
    else if(mode == UI_MS_BODE)
    {
      T_EQ(counts[UI_MSC_ALL], 1); T_EQ(counts[UI_MSC_CLEAR], 1); T_EQ(counts[UI_MSC_ITEM], 10);
      T_EQ(counts[UI_MSC_EMPTY], 2); T_EQ(counts[UI_MSC_LABEL], 0); T_EQ(counts[UI_MSC_SOURCE], 0);
    }
    else
    {
      T_EQ(counts[UI_MSC_LABEL], 2); T_EQ(counts[UI_MSC_ALL], 2); T_EQ(counts[UI_MSC_CLEAR], 2);
      T_EQ(counts[UI_MSC_ITEM], 12); T_EQ(counts[UI_MSC_SOURCE], 0); T_EQ(counts[UI_MSC_HIDE], 0);
    }

    //Every point: outside the box -1, the title band 0, everything else a cell that contains it (separators go to the
    //cell left of / above them)
    for(y = 40; y < 480; y++)
      for(x = 200; x < 760; x++)
      {
        int r = ui_msm_hit(mode, x, y, &c);
        int inside = (x >= UI_MSM_X) && (x < UI_MSM_X + UI_MSM_W) && (y >= top) && (y < UI_MSM_BOTTOM);

        if(!inside)
        {
          if(r != -1) T_EQ(r, -1);
          continue;
        }

        if(y < top + 1 + UI_MSM_TITLE_H)
        {
          if(r != 0) T_EQ(r, 0);
          continue;
        }

        if((x == UI_MSM_X) || (x >= UI_MSM_X + UI_MSM_W - 1) || (y >= UI_MSM_BOTTOM - 1))
          continue;                                    //the edge itself

        if(r != 1) { T_EQ(r, 1); printf("no cell at %u,%u mode %d\n", x, y, mode); continue; }
        if(!((x + 1 >= c.x) && (x <= c.x + c.w) && (y + 1 >= c.y) && (y <= c.y + c.h)))
          T_TRUE(0);
      }
  }

  //Scope: the items in table order, four per row, CH1 left; Hide is the third action
  n = ui_msm_cells(UI_MS_SCOPE, cells);
  T_TRUE(ui_msm_hit(UI_MS_SCOPE, cells[8].x + 5, cells[8].y + 5, &c) == 1);
  T_EQ(c.what, UI_MSC_ITEM); T_EQ(c.group, 0); T_EQ(c.index, 0);
  T_STR(ui_msm_text(UI_MS_SCOPE, &c), "Vmax");
  T_EQ(cells[7].what, UI_MSC_HIDE);
  T_EQ(cells[0].what, UI_MSC_SOURCE); T_EQ(cells[0].index, 0);
  T_STR(ui_msm_text(UI_MS_SCOPE, &cells[0]), "CH1");
  T_STR(ui_msm_text(UI_MS_SCOPE, &cells[24]), "Ref 8");
  T_STR(ui_msm_text(UI_MS_SCOPE, &cells[39]), "-Duty");
  T_EQ(cells[39].group, 1);

  //Spectrum: the view's row spans both halves
  n = ui_msm_cells(UI_MS_SPECTRUM, cells);
  T_EQ(cells[n - 4].group, UI_MS_SP_VIEW);
  T_STR(ui_msm_text(UI_MS_SPECTRUM, &cells[n - 4]), "RBW");
  T_STR(ui_msm_text(UI_MS_SPECTRUM, &cells[n - 2]), "Center");
  T_TRUE(cells[n - 4].x < 480 && cells[n - 1].x > 482);
  T_STR(ui_msm_text(UI_MS_SPECTRUM, &cells[0]), "CH1");
  T_STR(ui_msm_text(UI_MS_SPECTRUM, &cells[6]), "Noise");

  //Frequency response: Select All, Clear, then Level .. Thru in table order, the last row's two places empty
  n = ui_msm_cells(UI_MS_BODE, cells);
  T_EQ(cells[0].what, UI_MSC_ALL);
  T_EQ(cells[1].what, UI_MSC_CLEAR);
  T_TRUE(cells[0].x < 300 && cells[1].x + cells[1].w > 700);
  T_STR(ui_msm_text(UI_MS_BODE, &cells[2]), "Level");
  T_STR(ui_msm_text(UI_MS_BODE, &cells[9]), "Ph180");
  T_STR(ui_msm_text(UI_MS_BODE, &cells[11]), "Thru");
  T_EQ(cells[12].what, UI_MSC_EMPTY);
  T_EQ(cells[13].what, UI_MSC_EMPTY);
}

static int in_rect(uint32 x, uint32 y, uint32 x0, uint32 y0, uint32 w, uint32 h)
{
  return (x >= x0) && (x < x0 + w) && (y >= y0) && (y < y0 + h);
}

//Fold-out: rows contiguous at 59 px from y 46, Time and date only with the clock module, Other settings last
static void test_foldout(void)
{
  uint8  ids[UI_FO_MAX_ROWS];
  int    rtc, n;
  uint32 x, y;

  //fw 0.2.63: File message timeout is in Settings > Controls only
  T_EQ(ui_fo_rows(1, ids), 4);
  T_EQ(ids[0], UI_FO_SCREEN);
  T_EQ(ids[1], UI_FO_GRID);
  T_EQ(ids[2], UI_FO_TIME);
  T_EQ(ids[3], UI_FO_OTHER);
  T_EQ(ui_fo_rows(0, ids), 3);
  T_EQ(ids[2], UI_FO_OTHER);
  T_EQ(ui_fo_height(1), 236);
  T_EQ(ui_fo_height(0), 177);
  T_EQ(ui_fo_row_top(0, UI_FO_TIME), 0);
  T_EQ(ui_fo_row_top(1, UI_FO_SCREEN), 46);     //the brightness sliders open beside rows 0 and 1 (y 46, 104)
  T_EQ(ui_fo_row_top(1, UI_FO_GRID), 105);
  T_EQ(ui_fo_row_top(1, UI_FO_TIME), 164);
  T_EQ(ui_fo_row_top(1, UI_FO_OTHER), 223);
  T_EQ(ui_fo_row_top(0, UI_FO_OTHER), 164);

  for(rtc = 0; rtc < 2; rtc++)
  {
    n = ui_fo_rows(rtc, ids);

    for(y = 0; y < 480; y++)
      for(x = 140; x < 400; x += 3)
      {
        int want = -1;

        if((x >= 150) && (x < 394) && (y >= 46) && (y < 46 + (uint32)n * 59))
          want = ids[(y - 46) / 59];

        T_EQ(ui_fo_row_at(rtc, x, y), want);
      }
  }
}

//Settings screen: rectangles inside the screen, below the header, not overlapping; 48 px rows, 44 px targets at
//least 80 px wide; hit area = drawn area at every pixel
static void test_settings_geometry(void)
{
  int    i, j, part;
  uint32 x, y;

  for(i = 0; i < UI_SET_ROWS; i++)
  {
    const ui_set_row *r = &ui_set_rows[i];

    T_TRUE(r->y >= UI_SET_HDR_H);
    T_TRUE(r->x + UI_SET_ROW_W <= 800);
    T_TRUE(r->y + UI_SET_ROW_H <= UI_SET_RESTORE_Y);
    T_TRUE(r->y >= ui_set_groups[r->group].y + 16);     //the 13 px group title clears the row
    T_EQ(r->x, ui_set_groups[r->group].x);

    for(j = 0; j < UI_SET_ROWS; j++)
      if(j != i)
      {
        const ui_set_row *o = &ui_set_rows[j];
        int apart = (r->x + UI_SET_ROW_W <= o->x) || (o->x + UI_SET_ROW_W <= r->x) ||
                    (r->y + UI_SET_ROW_H + 6 <= o->y) || (o->y + UI_SET_ROW_H + 6 <= r->y);
        T_TRUE(apart);
      }

    //A group title sits in the gap above its first row, never over another row
    for(j = 0; j < UI_SET_GROUPS; j++)
      if((ui_set_groups[j].x == r->x) && (ui_set_groups[j].y + 14 > r->y) && (ui_set_groups[j].y < r->y + UI_SET_ROW_H))
        T_TRUE(0);
  }

  T_TRUE(UI_SET_STEP_W >= 80);
  T_TRUE(UI_SET_STEP_H >= 44);
  T_TRUE(UI_SET_BACK_H >= 44);
  T_TRUE(UI_SET_BACK_W >= 80);
  T_TRUE(UI_SET_RESTORE_H >= 44);
  T_TRUE(UI_SET_RESTORE_Y + UI_SET_RESTORE_H <= 480);

  for(y = 0; y < 480; y++)
    for(x = 0; x < 800; x++)
    {
      int want = -1, wpart = UI_SET_P_ROW, got;

      if(in_rect(x, y, UI_SET_BACK_X, UI_SET_BACK_Y, UI_SET_BACK_W, UI_SET_BACK_H))
        want = UI_SET_BACK;
      else if(in_rect(x, y, UI_SET_RESTORE_X, UI_SET_RESTORE_Y, UI_SET_RESTORE_W, UI_SET_RESTORE_H))
        want = UI_SET_RESTORE;
      else
        for(i = 0; i < UI_SET_ROWS; i++)
        {
          const ui_set_row *r = &ui_set_rows[i];
          uint32 mx = ui_set_minus_x(i);

          if(r->kind != UI_SET_K_STEP)
          {
            if(in_rect(x, y, r->x, r->y, UI_SET_ROW_W, UI_SET_ROW_H))
              want = i;
          }
          else if(in_rect(x, y, mx, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H))
          {
            want = i;
            wpart = UI_SET_P_MINUS;
          }
          else if(in_rect(x, y, mx + UI_SET_STEP_W + UI_SET_STEP_GAP, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H))
          {
            want = i;
            wpart = UI_SET_P_PLUS;
          }
        }

      got = ui_set_hit(x, y, &part);
      T_EQ(got, want);

      if(want >= 0)
        T_EQ(part, wpart);
    }

  //The step buttons stay inside their row
  for(i = 0; i < UI_SET_ROWS; i++)
    if(ui_set_rows[i].kind == UI_SET_K_STEP)
      T_TRUE(ui_set_minus_x(i) + 2 * UI_SET_STEP_W + UI_SET_STEP_GAP <= (uint32)ui_set_rows[i].x + UI_SET_ROW_W);
}

static ui_set_state st0(void)
{
  ui_set_state s;

  memset(&s, 0, sizeof(s));
  s.screen = 70;
  s.grid = 40;
  s.rtc_present = 1;
  s.rtc_on = 1;
  s.year = 26;
  s.month = 10;
  s.day = 9;
  s.hour = 7;
  s.minute = 5;
  return s;
}

static const char *val(int row, const ui_set_state *s, int tone)
{
  T_EQ(ui_set_value(row, s, b), tone);
  return b;
}

static void test_settings_values(void)
{
  ui_set_state s = st0();

  T_STR(val(UI_SET_SCREEN, &s, UI_SET_T_ON), "70 %");
  T_STR(val(UI_SET_GRID, &s, UI_SET_T_ON), "40 %");
  s.screen = 100;
  s.grid = 0;
  T_STR(val(UI_SET_SCREEN, &s, UI_SET_T_ON), "100 %");
  T_STR(val(UI_SET_GRID, &s, UI_SET_T_ON), "0 %");

  T_STR(val(UI_SET_FILEMSG, &s, UI_SET_T_ON), "0.7 s");
  s.filemsg_wait = 1;
  T_STR(val(UI_SET_FILEMSG, &s, UI_SET_T_ON), "wait for a tap");

  T_STR(val(UI_SET_CURSORS, &s, UI_SET_T_ON), "move freely");
  s.cursors_locked = 1;
  T_STR(val(UI_SET_CURSORS, &s, UI_SET_T_ON), "move together");

  T_STR(val(UI_SET_AUTOSET, &s, UI_SET_T_ON), "stock");
  s.autoset_new = 1;
  T_STR(val(UI_SET_AUTOSET, &s, UI_SET_T_ON), "improved");

  T_STR(val(UI_SET_STAMP, &s, UI_SET_T_OFF), "nothing");
  s.stamp = 1;
  T_STR(val(UI_SET_STAMP, &s, UI_SET_T_ON), "time");
  s.rtc_on = 0;
  T_STR(val(UI_SET_STAMP, &s, UI_SET_T_ON), "file name");
  s.rtc_on = 1;
  s.rtc_present = 0;
  T_STR(val(UI_SET_STAMP, &s, UI_SET_T_ON), "file name");
  s.rtc_present = 1;

  T_STR(val(UI_SET_REFS, &s, UI_SET_T_OFF), "not loaded");
  s.refs = 1;
  T_STR(val(UI_SET_REFS, &s, UI_SET_T_ON), "loaded");

  T_STR(val(UI_SET_CLOCK, &s, UI_SET_T_ON), "shown");
  T_STR(val(UI_SET_TIME, &s, UI_SET_T_ON), "2026-10-09 07:05");
  s.rtc_on = 0;
  T_STR(val(UI_SET_CLOCK, &s, UI_SET_T_OFF), "not used");
  T_STR(val(UI_SET_TIME, &s, UI_SET_T_NA), "needs the clock");
  s.rtc_present = 0;
  T_STR(val(UI_SET_CLOCK, &s, UI_SET_T_NA), "no clock module");
  T_STR(val(UI_SET_TIME, &s, UI_SET_T_NA), "no clock module");

  s.boot = 0;
  T_STR(val(UI_SET_POWERON, &s, UI_SET_T_ON), "Open firmware");
  s.boot = 4;
  T_STR(val(UI_SET_POWERON, &s, UI_SET_T_ON), "Boot menu, then open firmware");
  s.boot = 5;
  T_STR(val(UI_SET_POWERON, &s, UI_SET_T_ON), "Boot menu, then stock firmware");
  s.boot = 6;
  T_STR(val(UI_SET_POWERON, &s, UI_SET_T_WARN), "Boot menu, then FEL");
  s.boot = 1;
  T_STR(val(UI_SET_POWERON, &s, UI_SET_T_WARN), "Stock firmware, no boot menu");

  //Steps
  T_EQ(ui_set_step(70, 1), 75);
  T_EQ(ui_set_step(97, 1), 100);
  T_EQ(ui_set_step(100, 1), 100);
  T_EQ(ui_set_step(3, 0), 0);
  T_EQ(ui_set_step(0, 0), 0);
  T_EQ(ui_set_step(42, 0), 37);
}

//Start-up: the cycle of the three offered values; anything else (FEL, updater, stock without menu) goes to Open firmware,
//so FEL and the updater are never written from the screen
static void test_boot(void)
{
  int b, tone, n;

  T_EQ(ui_boot_next(0), 4);
  T_EQ(ui_boot_next(4), 5);
  T_EQ(ui_boot_next(5), 0);

  for(b = 0; b < 256; b++)
  {
    uint8 v = ui_boot_next((uint8)b);
    T_TRUE((v == 0) || (v == 4) || (v == 5));

    ui_boot_text((uint8)b, &tone);
    T_EQ(tone, ((b == 0) || (b == 4) || (b == 5)) ? UI_SET_T_ON : UI_SET_T_WARN);
  }

  //From anything, three taps reach every offered value
  for(b = 0; b < 8; b++)
  {
    uint8 v = (uint8)b, seen = 0;

    for(n = 0; n < 3; n++)
    {
      v = ui_boot_next(v);
      seen |= (uint8)((v == 0) ? 1 : (v == 4) ? 2 : 4);
    }

    T_EQ(seen, 7);
  }
}

//Every label with the longest value it can show fits its row in the 16 px font (12 px padding each side, 12 px apart);
//step rows end the value 10 px before the - button; group titles fit the column in the 13 px font
static void test_settings_text(void)
{
  static const uint8 rtc_cases[3][2] = { { 1, 1 }, { 1, 0 }, { 0, 0 } };
  ui_set_state s;
  int          i, k, bit, boot;

  for(i = 0; i < UI_SET_ROWS; i++)
  {
    const ui_set_row *r = &ui_set_rows[i];
    uint32 room = (r->kind == UI_SET_K_STEP) ? (ui_set_minus_x(i) - 10 - r->x - 12) : (UI_SET_ROW_W - 24);

    for(k = 0; k < 3; k++)
      for(bit = 0; bit < 2; bit++)
        for(boot = 0; boot < 8; boot++)
        {
          s = st0();
          s.rtc_present = rtc_cases[k][0];
          s.rtc_on = rtc_cases[k][1];
          s.filemsg_wait = s.cursors_locked = s.autoset_new = s.stamp = s.refs = (uint8)bit;
          s.screen = s.grid = 100;
          s.boot = (uint8)boot;
          ui_set_value(i, &s, b);

          if(ui_text_width(&font_3, r->label) + 12 + ui_text_width(&font_3, b) > room)
          {
            T_TRUE(0);
            printf("  row %d: \"%s\" + \"%s\" does not fit %u px\n", i, r->label, b, (unsigned)room);
          }
        }
  }

  for(i = 0; i < UI_SET_GROUPS; i++)
    T_TRUE(ui_text_width(&font_2, ui_set_groups[i].title) <= UI_SET_ROW_W);

  //Header: the title and Back on one line (fw 0.2.63: no subtitle, no note beside Restore)
  T_TRUE(12 + ui_text_width(&font_5, "Settings") + 16 <= UI_SET_BACK_X);
  T_TRUE(ui_text_width(&font_3, "Restore default settings") + 24 <= UI_SET_RESTORE_W);

  //Time editor: labels left of the - buttons, values between the buttons
  for(i = 0; i < UI_TIME_FIELDS; i++)
    T_TRUE(UI_TIME_LABEL_X + ui_text_width(&font_3, ui_time_labels[i]) + 16 <= UI_TIME_MINUS_X);
  T_TRUE(ui_text_width(&font_5, "2026") / 2 + 8 <= UI_TIME_VALUE_X - (UI_TIME_MINUS_X + UI_TIME_BTN_W));
  T_TRUE(UI_TIME_TOP + UI_TIME_FIELDS * UI_TIME_PITCH <= 480);
  T_TRUE(UI_TIME_TOP >= UI_SET_HDR_H + 16);
}

static void test_time_hit(void)
{
  uint32 x, y;
  int    f, want;

  T_TRUE(UI_TIME_MINUS_X + UI_TIME_BTN_W <= UI_TIME_VALUE_X - 20);
  T_TRUE(UI_TIME_PLUS_X >= UI_TIME_VALUE_X + 20);
  T_TRUE(UI_TIME_BTN_W >= 80);
  T_TRUE(UI_TIME_BTN_H >= 44);

  for(y = 0; y < 480; y++)
    for(x = 0; x < 800; x += 2)
    {
      want = -1;

      for(f = 0; f < UI_TIME_FIELDS; f++)
      {
        uint32 y0 = UI_TIME_TOP + (uint32)f * UI_TIME_PITCH;

        if(in_rect(x, y, UI_TIME_MINUS_X, y0, UI_TIME_BTN_W, UI_TIME_BTN_H))
          want = f * 2;
        else if(in_rect(x, y, UI_TIME_PLUS_X, y0, UI_TIME_BTN_W, UI_TIME_BTN_H))
          want = f * 2 + 1;
      }

      T_EQ(ui_time_hit(x, y), want);
    }
}

//Dialog: buttons 44 px tall, 16 px apart, the dismissing one left, inside the box; only the buttons hit; texts fit
static void test_dialog(void)
{
  const ui_dlg_text *t[2] = { &ui_dlg_restore, &ui_dlg_delete };
  ui_dlg d;
  int    lines, i, k, want;
  uint32 x, y;

  for(lines = 1; lines <= 3; lines++)
  {
    ui_dlg_layout(lines, &d);

    T_EQ(d.action_x - (d.cancel_x + UI_DLG_BTN_W), 16);
    T_TRUE(d.cancel_x < d.action_x);
    T_EQ(UI_DLG_BTN_H, 44);
    T_TRUE(d.cancel_x >= d.x + 16);
    T_TRUE(d.action_x + UI_DLG_BTN_W + 16 <= d.x + d.w);
    T_TRUE(d.btn_y >= d.y + UI_DLG_LINE0_DY + lines * UI_DLG_LINE_PITCH);
    T_TRUE(d.btn_y + UI_DLG_BTN_H <= d.y + d.h);
    T_TRUE(d.y + d.h <= 480);

    for(y = 0; y < 480; y++)
      for(x = 0; x < 800; x += 2)
      {
        want = UI_DLG_NONE;

        if(in_rect(x, y, d.cancel_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H))
          want = UI_DLG_CANCEL;
        else if(in_rect(x, y, d.action_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H))
          want = UI_DLG_ACTION;

        T_EQ(ui_dlg_hit(&d, x, y), want);
      }
  }

  for(k = 0; k < 2; k++)
  {
    T_TRUE((t[k]->nlines >= 1) && (t[k]->nlines <= 3));
    T_TRUE(ui_text_width(&font_5, t[k]->title) + 48 <= UI_DLG_W);
    T_TRUE(ui_text_width(&font_3, t[k]->cancel) + 16 <= UI_DLG_BTN_W);
    T_TRUE(ui_text_width(&font_3, t[k]->action) + 16 <= UI_DLG_BTN_W);
    T_TRUE(t[k]->destructive);

    for(i = 0; i < t[k]->nlines; i++)
      T_TRUE(ui_text_width(&font_3, t[k]->lines[i]) + 48 <= UI_DLG_W);

    //fw 0.2.63 (owner): the body uses the dialog's width, a line breaks only where the next word would not fit
    for(i = 0; i + 1 < t[k]->nlines; i++)
    {
      char   joined[160];
      size_t n = strlen(t[k]->lines[i]), w = 0;

      memcpy(joined, t[k]->lines[i], n);
      joined[n++] = ' ';
      while(t[k]->lines[i + 1][w] && (t[k]->lines[i + 1][w] != ' '))
        joined[n++] = t[k]->lines[i + 1][w++];
      joined[n] = 0;
      T_TRUE(ui_text_width(&font_3, joined) + 48 > UI_DLG_W);
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// fw 0.2.69: the Frequency response screen (bode_ui.c): scales with the free top division, the Log / Linear axis, the
// C1 / C2 readout fit, the MEASURES list words and card texts, the texts' lengths, the fold-outs

//The readout row r fits its columns in font_2: each column ends before the next one starts, the last inside the box
static int ro_fits(const bu_ro_row *r)
{
  return (BU_RO_COL_NAME + ui_text_width(&font_2, r->name) + 4 <= BU_RO_COL_F) &&
         (BU_RO_COL_F + ui_text_width(&font_2, r->f) + 4 <= BU_RO_COL_DB) &&
         (BU_RO_COL_DB + ui_text_width(&font_2, r->db) + 4 <= BU_RO_COL_PH) &&
         (BU_RO_COL_PH + ui_text_width(&font_2, r->ph) + 2 <= BU_RO_W);
}

static void test_bode_scales(void)
{
  bu_scale s, p;
  char t[24];
  int i;

  //Magnitude: a low-pass from 0 to -40 dB: 10 dB / div, the top raised to +10 (the top division free)
  bu_scale_mag(0, -40000, &s);
  T_EQ(s.div, 10000); T_EQ(s.top, 10000);
  T_EQ(bu_scale_y(&s, 0), BU_Y + BU_DIV);
  //The divider: flat at -20.6 dB: 5 dB / div, top -15 (the data in the second division)
  bu_scale_mag(-20400, -20800, &s);
  T_EQ(s.div, 5000); T_EQ(s.top, -15000);
  T_TRUE(bu_scale_y(&s, -20400) >= BU_Y + BU_DIV);
  //55 dB of data: no room for the free division at 10 dB / div, so 20 dB / div with it (bode-ui.md 9 C3)
  bu_scale_mag(0, -55000, &s);
  T_EQ(s.div, 20000); T_EQ(s.top, 20000);
  bu_scale_mag(0, -60000, &s);
  T_EQ(s.div, 20000); T_EQ(s.top, 20000);
  //50 dB keeps 10 dB / div with the free division
  bu_scale_mag(0, -50000, &s);
  T_EQ(s.div, 10000); T_EQ(s.top, 10000);
  //More than 120 dB: 20 dB / div, the low end clipped to the pane
  bu_scale_mag(0, -150000, &s);
  T_EQ(s.div, 20000);
  T_EQ(bu_scale_y(&s, -150000), BU_Y + BU_H);
  //A tank: +1 .. -40 dB
  bu_scale_mag(1000, -40000, &s);
  T_EQ(s.div, 10000); T_EQ(s.top, 20000);

  //Phase: the tank's +90 .. -90 deg: 45 / div, top +135; a low-pass 0 .. -90: 45 / div, top +45
  bu_scale_phase(89000, -89000, &p);
  T_EQ(p.div, 45000); T_EQ(p.top, 135000);
  bu_scale_phase(0, -89000, &p);
  T_EQ(p.div, 45000); T_EQ(p.top, 45000);
  //The whole wrapped range always fits 90 / div with the free division
  bu_scale_phase(180000, -179999, &p);
  T_EQ(p.div, 90000); T_EQ(p.top, 270000);
  T_TRUE(p.top - BU_NDIV * p.div <= -180000);
  //Empty chart: +10 .. -50 dB and +135 .. -135 deg
  bu_scale_empty(&s, &p);
  T_EQ(s.top, 10000); T_EQ(s.div, 10000);
  T_EQ(p.top, 135000); T_EQ(p.div, 45000);

  //Frequency axis
  T_EQ(bu_x_log(0, 100, 0), BU_PX);
  T_EQ(bu_x_log(0, 100, 100), BU_PX + BU_PW);
  T_EQ(bu_x_log(0, 100, 50), BU_PX + BU_PW / 2);
  T_EQ(bu_x_log(0, 100, -5), BU_PX);
  T_EQ(bu_x_lin(20000000, 100000000, 60000000), BU_PX + BU_PW / 2);
  T_EQ(bu_x_lin(20000000, 100000000, 1), BU_PX);
  T_EQ(bu_x_lin(20000000, 100000000, 200000000), BU_PX + BU_PW);
  //Linear grid: 20 .. 100 kHz in 10 kHz steps (8 lines); 100 Hz .. 1 MHz in 100 kHz steps; 5 .. 500 kHz in 50 kHz
  T_EQ(bu_lin_step(20000000, 100000000), 10000000ull);
  T_EQ(bu_lin_step(100000, 1000000000), 100000000ull);
  T_EQ(bu_lin_step(5000000, 500000000), 50000000ull);

  //The slider: 1 Hz .. 10 MHz over the track; the design's spans; a drag shifts both ends by whole steps, kept on it
  T_EQ(bu_slider_x(0), BU_SL_X);
  T_EQ(bu_slider_x(BODE_STEPS - 1), BU_SL_X + BU_SL_W);
  T_EQ(bu_slider_x(11), 386);                      //5 kHz (fw 0.2.73: the track x 2..727, 384 on x 4..723)
  T_EQ(bu_slider_x(17), 593);                      //500 kHz
  T_EQ(bu_slider_x(6), 209);                       //100 Hz
  for(i = 1; i < BODE_STEPS; i++)
    T_TRUE(bu_slider_x(i) > bu_slider_x(i - 1));
  T_EQ(bu_slider_shift(6, 18, 0), 0);
  T_EQ(bu_slider_shift(6, 18, 10), 0);
  T_EQ(bu_slider_shift(6, 18, 25), 1);
  T_EQ(bu_slider_shift(6, 18, 1000), 3);           //Stop at 10 MHz
  T_EQ(bu_slider_shift(6, 18, -1000), -6);         //Start at 1 Hz
  T_EQ(bu_slider_shift(6, 18, 103), 3);            //one decade
  T_TRUE(BU_SL_HIT_Y > BU_CARDS_Y + UI_CARD_H);

  //Short labels and values
  T_STR((bu_freq_short(t, 100000), t), "100");
  T_STR((bu_freq_short(t, 2500000), t), "2.5k");
  T_STR((bu_freq_short(t, 10000000000ull), t), "10M");
  T_STR((bu_freq_short(t, 150000000), t), "150k");
  T_STR((bu_freq_short(t, 1250000000), t), "1.25M");
  T_STR((bu_fmt_db(t, -7454), t), "-7.45dB");
  T_STR((bu_fmt_db(t, 1210), t), "+1.21dB");
  T_STR((bu_fmt_db(t, -4), t), "0.00dB");
  T_STR((bu_fmt_deg(t, -60840), t), "-60.8deg");
  T_STR((bu_fmt_deg(t, 135000), t), "+135.0deg");
  T_STR((bu_fmt_fdiff(t, -33270000), t), "-33.27kHz");
}

static void test_bode_readout(void)
{
  bu_ro_row r[3];
  int i;

  //The design's example and its widest row ("C2-C1 33.27kHz -13.67dB -129.2deg")
  bu_readout(56170000, -7450, -60800, 0, 89440000, -21120, 170000, 0, r);
  T_STR(r[0].name, "C1"); T_STR(r[0].f, "56.17kHz"); T_STR(r[0].db, "-7.45dB"); T_STR(r[0].ph, "-60.8deg");
  T_STR(r[2].name, "C2-C1"); T_STR(r[2].f, "33.27kHz"); T_STR(r[2].db, "-13.67dB");
  T_STR(r[2].ph, "-129.2deg");                 //+230.8 wrapped
  for(i = 0; i < 3; i++)
  {
    T_TRUE(ro_fits(&r[i]));
    T_EQ(r[i].grey, 0);
  }

  //Worst cases: 10 MHz, -100 dB, a negative frequency difference, every flag reason
  bu_readout(10000000000ull, -99990, -179900, 0, 1000, -99990, 179900, BODE_F_CH2_OVERLOAD, r);
  T_TRUE(r[1].flagged && r[1].grey && r[2].grey);
  T_STR(r[1].ph, "overload");
  for(i = 0; i < 3; i++)
    T_TRUE(ro_fits(&r[i]));
  T_STR(r[2].f, "-10.00MHz");

  {
    static const uint16 fl[] = { BODE_F_CLIPPED, BODE_F_CH1_CLIPPED, BODE_F_UNSETTLED, BODE_F_NEAR_FLOOR, BODE_F_LOW_SNR,
                                 BODE_F_INCOHERENT };
    unsigned k;

    for(k = 0; k < sizeof(fl) / sizeof(fl[0]); k++)
    {
      bu_readout(1000, -99990, 0, fl[k], 5000000000ull, -99990, 0, 0, r);
      T_TRUE(r[0].flagged);
      T_TRUE(r[0].ph[0] != 0);
      T_TRUE(ro_fits(&r[0]));
    }
  }

  //A valid point that was spliced reads normally (spliced points are valid)
  bu_readout(1000, 0, 0, BODE_F_SPLICED, 2000, 0, 0, 0, r);
  T_EQ(r[0].flagged, 0);

  //Three rows in the box, which sits in the pane's bottom left
  T_TRUE(BU_RO_ROW0 + 2 * BU_RO_PITCH + 13 <= BU_RO_H);
  T_TRUE(BU_RO_X >= BU_PX && BU_RO_Y + BU_RO_H <= BU_Y + BU_H);
  T_TRUE(ui_text_width(&font_2, "C2") + 4 <= BU_FLAG_W);
}

static void test_bode_cards(void)
{
  uint8 rank[BU_MS_COUNT], back[BU_MS_COUNT], order[UI_MS_LIST_MAX];
  uint16 w[3];
  bode_markers mk;
  bu_card_in in;
  char t[32];
  int i, k;

  //Defaults (owner): -3dB, Peak, Q, Level
  bu_ms_defaults(rank);
  k = ui_ms_order(rank, BU_MS_COUNT, order);
  T_EQ(k, 4);
  T_EQ(order[0], BU_MS_3DB); T_EQ(order[1], BU_MS_PEAK); T_EQ(order[2], BU_MS_Q); T_EQ(order[3], BU_MS_LEVEL);

  //Words: round trip with every item chosen; erased words and bad ranks give the defaults
  ui_ms_select_all(rank, BU_MS_COUNT);
  for(i = 0; i < 3; i++)
    w[i] = bu_ms_word(rank, i);
  T_EQ(w[0] & 0xF000, BU_MS_MARKER);
  memset(back, 0, sizeof(back));
  T_EQ(bu_ms_restore(back, w), 1);
  T_TRUE(memcmp(back, rank, BU_MS_COUNT) == 0);
  w[0] = w[1] = w[2] = 0xFFFF;
  T_EQ(bu_ms_restore(back, w), 0);
  T_EQ(back[BU_MS_3DB], 1);
  w[0] = BU_MS_MARKER | 0xB;                  //rank 11
  w[1] = w[2] = 0;
  T_EQ(bu_ms_restore(back, w), 0);
  w[0] = BU_MS_MARKER; w[1] = 0; w[2] = 0x1000;  //junk above word 215's ranks
  T_EQ(bu_ms_restore(back, w), 0);
  w[0] = BU_MS_MARKER; w[1] = 0; w[2] = 0;       //nothing chosen is valid
  T_EQ(bu_ms_restore(back, w), 1);
  T_EQ(back[BU_MS_3DB], 0);

  //Blocks: magnitude cyan, phase pink, the sweep's own state neutral
  T_EQ(bu_ms_block(BU_MS_LEVEL), BU_BLOCK_MAG);
  T_EQ(bu_ms_block(BU_MS_Q), BU_BLOCK_MAG);
  T_EQ(bu_ms_block(BU_MS_PH45), BU_BLOCK_PHASE);
  T_EQ(bu_ms_block(BU_MS_PH180), BU_BLOCK_PHASE);
  T_EQ(bu_ms_block(BU_MS_SWEEP), BU_BLOCK_NEUTRAL);
  T_EQ(bu_ms_block(BU_MS_THRU), BU_BLOCK_NEUTRAL);

  //Texts per state; every card fits its 118 px (13 px font)
  memset(&mk, 0, sizeof(mk));
  memset(&in, 0, sizeof(in));
  in.mk = &mk;
  in.count = 100;

  in.table = BU_TABLE_NONE;
  T_EQ(bu_card_text(BU_MS_3DB, &in, t), BU_VAL_GREY);   T_STR(t, "no sweep");
  T_EQ(bu_card_text(BU_MS_SWEEP, &in, t), BU_VAL_GREY); T_STR(t, "not run");
  T_EQ(bu_card_text(BU_MS_THRU, &in, t), BU_VAL_GREY);  T_STR(t, "none");

  in.table = BU_TABLE_FILLING;
  in.done = 37;
  T_EQ(bu_card_text(BU_MS_PEAK, &in, t), BU_VAL_GREY);  T_STR(t, "sweeping");
  T_EQ(bu_card_text(BU_MS_SWEEP, &in, t), BU_VAL_WHITE); T_STR(t, "37/100");
  in.autoset = 1; in.done = 12; in.count = 20;
  T_EQ(bu_card_text(BU_MS_SWEEP, &in, t), BU_VAL_WHITE); T_STR(t, "auto 12/20");
  T_TRUE(ui_card_fits(&font_2, "Sweep", t));
  T_EQ(bu_card_text(BU_MS_Q, &in, t), BU_VAL_GREY);     T_STR(t, "sweeping");
  in.autoset = 0; in.count = 100; in.done = 100;

  in.table = BU_TABLE_COMPLETE;
  mk.shape = BODE_SHAPE_RESONANT; mk.ref_mdb = -1210; mk.n3db = 2;
  mk.f3db_mhz[0] = 47290000; mk.f3db_mhz[1] = 53500000;
  mk.have_peak = 1; mk.peak_mhz = 50300000; mk.have_q = 1; mk.q_x100 = 810;
  mk.ph_mhz[0] = 47300000; mk.ph_mhz[1] = 53400000; mk.ph_mhz[5] = 0;
  T_EQ(bu_card_text(BU_MS_LEVEL, &in, t), BU_VAL_WHITE); T_STR(t, "-1.21dB");
  T_EQ(bu_card_text(BU_MS_3DB, &in, t), BU_VAL_WHITE);   T_STR(t, "47.29kHz");
  T_EQ(bu_card_text(BU_MS_BW, &in, t), BU_VAL_WHITE);    T_STR(t, "6.210kHz");
  T_EQ(bu_card_text(BU_MS_PEAK, &in, t), BU_VAL_WHITE);  T_STR(t, "50.30kHz");
  T_EQ(bu_card_text(BU_MS_Q, &in, t), BU_VAL_WHITE);     T_STR(t, "8.1");
  T_EQ(bu_card_text(BU_MS_PH45, &in, t), BU_VAL_WHITE);  T_STR(t, "47.30kHz");
  T_EQ(bu_card_text(BU_MS_PH90, &in, t), BU_VAL_GREY);   T_STR(t, "not found");
  T_EQ(bu_card_text(BU_MS_PH180, &in, t), BU_VAL_GREY);  T_STR(t, "not found");

  in.thru = BU_THRU_APPLIED;
  T_EQ(bu_card_text(BU_MS_THRU, &in, t), BU_VAL_GREEN);  T_STR(t, "applied");
  in.thru = BU_THRU_STALE;
  T_EQ(bu_card_text(BU_MS_THRU, &in, t), BU_VAL_AMBER);  T_STR(t, "stale");
  in.thru = BU_THRU_OFF;
  T_EQ(bu_card_text(BU_MS_THRU, &in, t), BU_VAL_GREY);   T_STR(t, "off");

  //Every item in every state fits its card, with the longest values (10 MHz, -100 dB, Q 999.9)
  mk.ref_mdb = -99990; mk.f3db_mhz[0] = 9999000000ull; mk.f3db_mhz[1] = 10000000000ull; mk.peak_mhz = 9999000000ull;
  mk.q_x100 = 99990; mk.ph_mhz[2] = 9999000000ull; mk.ph_mhz[5] = 9999000000ull;
  for(k = BU_TABLE_NONE; k <= BU_TABLE_COMPLETE; k++)
    for(i = 0; i < BU_MS_COUNT; i++)
    {
      in.table = (uint8)k;
      in.done = 200; in.count = 200;
      bu_card_text(i, &in, t);
      T_TRUE(strlen(t) < 20);
      if(!ui_card_fits(&font_2, bu_ms_names[i], t)) { T_TRUE(0); printf("card %s %s\n", bu_ms_names[i], t); }
      in.autoset = 1;
      bu_card_text(i, &in, t);
      if(!ui_card_fits(&font_2, bu_ms_names[i], t)) printf("card auto %s %s\n", bu_ms_names[i], t);
      in.autoset = 0;
    }
  T_TRUE(ui_card_fits(&font_2, "-3dB", "not found"));
}

static void test_bode_texts(void)
{
  ui_fold f;
  char t[64];
  int i;

  //Warnings: at most 50 characters, inside the width they are budgeted (16 px estimate, 13 px drawn)
  for(i = 0; i < BU_W_COUNT; i++)
  {
    if(strlen(bu_warn[i]) > BU_WARN_MAX) { T_TRUE(0); printf("long warning: %s\n", bu_warn[i]); }
    T_TRUE(ui_text_width(&font_2, bu_warn[i]) <= 355);
  }
  for(i = 1; i <= 200; i++)
  {
    T_TRUE(strlen((bu_warn_unsettled(t, i), t)) <= BU_WARN_MAX);
    T_TRUE(strlen((bu_warn_floor(t, i), t)) <= BU_WARN_MAX);
  }
  T_STR((bu_warn_unsettled(t, 2), t), "2 points not settled: choose a longer Settle");

  //The instruction (owner): two lines in 16 px, centred at x 366 inside the pane and clear of both axes' labels
  T_STR(bu_instruction[0], "Connect the calibration post and CH1 to the input, CH2 to the output,");
  for(i = 0; i < 2; i++)
  {
    uint32 w = ui_text_width(&font_3, bu_instruction[i]);

    T_TRUE(BU_WARN_XC - w / 2 >= BU_PX + 50);
    T_TRUE(BU_WARN_XC + w / 2 <= BU_PX + BU_PW - 60);
  }

  //The Display button's values fit the 51 px button (13 px)
  for(i = 0; i < BU_SHOW_COUNT; i++)
    T_TRUE(ui_text_width(&font_2, bu_show_values[i]) + 2 <= UI_BAR_W);

  //Display fold-out (228 px, as Spectrum's Analyse): three variants with icons, the separator, Frequency axis
  ui_fold_layout(&f, 228, 4, 3, UI_CTRL_DISPLAY_Y);
  check_fold(&f);
  for(i = 0; i < BU_SHOW_COUNT; i++)
    T_TRUE(46 + ui_text_width(&font_3, bu_show_names[i]) + 6 <= ui_fold_row_w(&f));
  printf("axis row %u + %u, rw %u\n", ui_text_width(&font_3, bu_axis_row), ui_text_width(&font_3, "Linear"), ui_fold_row_w(&f));
  for(i = 0; i < 2; i++)
    T_TRUE(row_fits(&f, bu_axis_row, bu_axis_names[i], 0));

  //Sweep fold-out beside its button at y 125: Start, Stop (two-way), Points, Settle, CH2 range, Single sweep
  ui_fold_layout(&f, 206, 6, 5, 125);
  check_fold(&f);
  T_TRUE(row_fits(&f, "Start", "500kHz", 1));
  T_TRUE(row_fits(&f, "Stop", "10MHz", 1));
  T_TRUE(row_fits(&f, "Settle", "Resonant", 0));
  T_TRUE(row_fits(&f, "CH2 range", "Auto", 0));
  T_TRUE(ui_text_width(&font_2, bu_single_running) + 8 <= ui_fold_row_w(&f));
  T_STR((bu_estimate_text(t, 2600), t), "about 3 s");
  T_STR((bu_estimate_text(t, 120), t), "about 1 s");
  T_STR((bu_estimate_text(t, 45000), t), "about 45 s");
  T_STR((bu_estimate_text(t, 150000), t), "about 3 min");

  //Thru fold-out beside its button at y 185
  ui_fold_layout(&f, 206, 3, 0, 185);
  check_fold(&f);
  //Its switch row draws the texts from x 60 (bode_view.c)
  T_TRUE(60 + ui_text_width(&font_2, "other sweep settings") + 4 <= ui_fold_row_w(&f));
  T_TRUE(ui_text_width(&font_2, "Thru: both tips on the input") + 20 <= f.w);

  //fw 0.2.71: the readout fault's warning (owner's fail-fast rule)
  T_STR(bu_warn[BU_W_READOUT], "ADC readout fault: see Diagnostics");

  //fw 0.2.71: Trace style at y 245 (the Scope's construction: "Trace" / "style" / the value in the 51 px button), its
  //fold-out of two choice rows without icons
  for(i = 0; i < BU_TRACE_COUNT; i++)
    T_TRUE(ui_text_width(&font_2, bu_trace_names[i]) + 2 <= UI_BAR_W);
  T_STR(bu_trace_names[BU_TRACE_LINES], "Lines");
  ui_fold_layout(&f, 206, BU_TRACE_COUNT, 0, 245);
  check_fold(&f);
  for(i = 0; i < BU_TRACE_COUNT; i++)
    T_TRUE(14 + ui_text_width(&font_3, bu_trace_names[i]) + 6 <= ui_fold_row_w(&f));

  //fw 0.2.71, the top bar (bode-ui.md 7): the ACQ chip's two lines in its value column, the notes in their boxes, the
  //trigger menu's line inside its 220 px
  for(i = 0; i < 2; i++)
    T_TRUE(BU_ACQ_CHIP_TEXT_X + ui_text_width(&font_2, bu_acq_chip[i]) + 2 <= BU_ACQ_CHIP_W);
  T_TRUE(BU_NOTE_ACQ_X + ui_text_width(&font_3, bu_acq_note) + 2 * BU_NOTE_PAD <= 728);
  T_TRUE(BU_NOTE_GEN_XR - (ui_text_width(&font_3, bu_gen_note) + 2 * BU_NOTE_PAD) >= 300);
  T_TRUE(ui_text_width(&font_2, bu_trig_note) + 20 <= 220);
  T_TRUE(strlen(bu_acq_note) <= BU_WARN_MAX);
  T_TRUE(strlen(bu_gen_note) <= BU_WARN_MAX);
}

//fw 0.2.71, the grid rule (bode_ui.h bu_fgrid, ui-guidelines.md section 3): lines inside the span in order, at most
//BU_GRID_MAX; on the log axis decades always, 2 and 5 under three decades, labelled under 1.5 decades, the linear
//steps when fewer than two lines fall inside; labelled lines far enough apart for their labels
static int grid_x(uint64 f0, uint64 f1, int linear, uint64 f)
{
  return linear ? bu_x_lin(f0, f1, f) : bu_x_log(bode_log2_q32(f0), bode_log2_q32(f1), bode_log2_q32(f));
}

//The Start / Stop steps (bode_sweep.c bode_step_mhz, not linked here)
static uint64 step_mhz(int i)
{
  static const uint8 m125[3] = { 1, 2, 5 };
  uint64 f = (uint64)m125[i % 3] * 1000;
  int k;

  for(k = 0; k < i / 3; k++)
    f *= 10;
  return f;
}

static void grid_check(uint64 f0, uint64 f1, int linear)
{
  bu_gline g[BU_GRID_MAX];
  char   t[24];
  int    n = bu_fgrid(f0, f1, linear, g), k, last = -1000, lastw = 0, labels = 0;

  T_TRUE(n >= 1);
  T_TRUE(n <= BU_GRID_MAX);

  for(k = 0; k < n; k++)
  {
    T_TRUE((g[k].f >= f0) && (g[k].f <= f1));
    if(k)
      T_TRUE(g[k].f > g[k - 1].f);

    if(g[k].label)
    {
      int x = grid_x(f0, f1, linear, g[k].f), w = (int)ui_text_width(&font_2, (bu_freq_short(t, g[k].f), t)) + 14;

      //Centred labels with "Hz" (the Spectrum's; the edge ones are clamped inside the pane, which only moves them apart)
      if((x - last) < (w + lastw) / 2 + 6)
      {
        T_TRUE(0);
        printf("grid labels crowd: %llu .. %llu mHz %s at %d\n", (unsigned long long)f0, (unsigned long long)f1, t, x);
      }

      last = x;
      lastw = w;
      labels++;
    }
  }

  T_TRUE(labels >= 1);
}

static void test_bode_grid(void)
{
  static const uint64 spans[][2] =
  {
    { 100000, 1000000000 }, { 1000000, 200000000 }, { 20000000, 100000000 }, { 44000000, 56000000 },
    { 1000, 10000000000ull }, { 10000, 100000 }, { 5000000, 500000000 },
    { 15300000ull * 1000 / 1030, 15300000ull * 1030 / 1000 },
  };
  bu_gline g[BU_GRID_MAX];
  uint64 f0;
  int    i, n, s;

  //100 Hz .. 1 MHz: the five decades, labelled, no 2 / 5 lines (four decades)
  n = bu_fgrid(100000, 1000000000, 0, g);
  T_EQ(n, 5);
  for(i = 0; i < n; i++)
    T_TRUE(g[i].major && g[i].label);

  //1 kHz .. 200 kHz (2.3 decades): 1k 2k 5k 10k 20k 50k 100k 200k, the 2 and 5 lines minor and unlabelled
  n = bu_fgrid(1000000, 200000000, 0, g);
  T_EQ(n, 8);
  T_EQ(g[1].f, 2000000ull);
  T_EQ(g[1].major, 0);
  T_EQ(g[1].label, 0);
  T_EQ(g[3].label, 1);

  //20 kHz .. 100 kHz (0.7 decades): 20k 50k 100k, all labelled
  n = bu_fgrid(20000000, 100000000, 0, g);
  T_EQ(n, 3);
  for(i = 0; i < n; i++)
    T_TRUE(g[i].label);

  //44 .. 56 kHz: no 1-2-5 line inside, the linear steps (2 kHz: 44 .. 56, seven lines, every other one labelled)
  n = bu_fgrid(44000000, 56000000, 0, g);
  T_EQ(n, 7);
  T_EQ(g[0].f, 44000000ull);
  T_EQ(g[1].label, 0);
  T_EQ(g[2].label, 1);

  //Linear: the label steps
  bu_fgrid(1000000, 200000000, 1, g);
  T_EQ(g[1].f - g[0].f, bu_lin_step(1000000, 200000000));

  for(s = 0; s < (int)(sizeof(spans) / sizeof(spans[0])); s++)
  {
    grid_check(spans[s][0], spans[s][1], 0);
    grid_check(spans[s][0], spans[s][1], 1);
  }

  //Every Start / Stop pair of the steps, both axes
  for(i = 0; i < BODE_STEPS; i++)
    for(s = i + 1; s < BODE_STEPS; s++)
    {
      grid_check(step_mhz(i), step_mhz(s), 0);
      grid_check(step_mhz(i), step_mhz(s), 1);
    }

  //Spectrum spans (linear): zoomed windows and the whole axis
  for(f0 = 0; f0 < 25000000000ull; f0 += 1234567891ull)
  {
    grid_check(f0, f0 + 390625000ull, 1);
    grid_check(f0, 25000000000ull, 1);
  }

  //The slider: any frequency on the track, the steps where bu_slider_x puts them (within a pixel)
  for(i = 0; i < BODE_STEPS; i++)
  {
    int32 d = bu_slider_x_mhz(step_mhz(i)) - bu_slider_x(i);

    T_TRUE((d >= -1) && (d <= 1));
  }
  T_EQ(bu_slider_x_mhz(1), BU_SL_X);
  T_EQ(bu_slider_x_mhz(20000000000ull), BU_SL_X + BU_SL_W);
}
int main(void)
{
  test_sig4();
  test_cards();
  test_menu();
  test_choose();
  test_foldout();
  test_settings_geometry();
  test_settings_values();
  test_boot();
  test_settings_text();
  test_time_hit();
  test_dialog();
  test_fmt_spectrum();
  test_ctrl_page();
  test_folds();
  test_warnings();
  test_spec_cards();
  test_meas_order();
  test_meas_fit();
  test_meas_spectrum_maths();
  test_meas_sources();
  test_meas_menu();
  test_bode_scales();
  test_bode_readout();
  test_bode_cards();
  test_bode_texts();
  test_bode_grid();
  return t_report("test_ui");
}
