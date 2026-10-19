// test_ui.c - the screens' pure parts (fw 0.2.51): card value formatting and card fit with the device's own font tables
// (ui_fmt.c, font_2.c), the main menu row table, hit areas and mode rule (ui_menu.c), the Settings fold-out and screen
// (ui_settings.c, fw 0.2.55): rows, hit areas against the drawn rectangles, values, text fit, start-up choice, dialog;
// the CTRL page, fold-outs, warnings and spectrum cards (ui_ctrl.c, fw 0.2.56).
// Run natively.

#include <stdio.h>
#include <string.h>

#include "ui_fmt.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_ctrl.h"
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
    T_EQ(ui_p1_shown(item, 1), !((item == UI_P1_TCURSOR) || (item == UI_P1_VCURSOR) || (item == UI_P1_MEASURES)));
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

  //RUN/STOP labels fit the 51 px button with 2 px margins (fw 0.2.63): "RUN" in the 16 px font, "Stopped" in the 13 px
  T_TRUE(ui_text_width(&font_3, ui_run_label[1]) <= UI_BAR_W - 4);
  T_TRUE(ui_text_width(&font_2, ui_run_label[0]) <= UI_BAR_W - 4);

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
}

static void test_spec_cards(void)
{
  static const char *values[] = { "25.00MHz", "999.9kHz", "-100.0dBV", "+60.0dBA", "12.21kHz", "999.9s" };
  ui_spec_card c[UI_SC_MAX];
  uint8        shown[2];
  int          n, i;
  unsigned     j;

  shown[0] = 1; shown[1] = 1;
  n = ui_spec_cards(shown, 1, c);
  T_EQ(n, UI_SC_MAX);
  T_EQ(c[0].kind, UI_SC_PEAK);  T_EQ(c[0].ch, 0);
  T_EQ(c[1].kind, UI_SC_LEVEL); T_EQ(c[1].ch, 0);
  T_EQ(c[2].kind, UI_SC_PEAK);  T_EQ(c[2].ch, 1);
  T_EQ(c[3].kind, UI_SC_LEVEL); T_EQ(c[3].ch, 1);
  T_EQ(c[4].kind, UI_SC_RBW);
  T_EQ(c[5].kind, UI_SC_TIME);

  //The last of six cards ends inside the 726 px plot (x 2..727)
  T_TRUE(ui_spec_card_x(UI_SC_MAX - 1) + UI_CARD_W <= 728);
  T_TRUE(UI_SC_Y + UI_CARD_H <= 480);

  shown[0] = 0;
  n = ui_spec_cards(shown, 0, c);
  T_EQ(n, 3);
  T_EQ(c[0].ch, 1);
  T_EQ(c[2].kind, UI_SC_RBW);

  shown[1] = 0;
  n = ui_spec_cards(shown, 0, c);
  T_EQ(n, 1);
  T_EQ(c[0].kind, UI_SC_RBW);

  for(i = 0; i < 4; i++)
    for(j = 0; j < sizeof(values) / sizeof(values[0]); j++)
      T_TRUE(ui_card_fits(&font_2, ui_sc_names[i], values[j]));
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
  return t_report("test_ui");
}
