//----------------------------------------------------------------------------------------------------------------------------------
// ui_meas.c - Measurements v2 (see ui_meas.h). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_meas.h"
#include "ui_fmt.h"
#include "bode_ui.h"

//----------------------------------------------------------------------------------------------------------------------------------
//Selection and display order

int ui_ms_order(const uint8 *rank, int n, uint8 *out)
{
  uint8 used[UI_MS_LIST_MAX] = { 0 };
  int   k = 0, i, best;

  if(n > UI_MS_LIST_MAX)
    n = UI_MS_LIST_MAX;

  //Selection by (rank, index): at most twelve items
  for(;;)
  {
    best = -1;

    for(i = 0; i < n; i++)
      if(rank[i] && !used[i] && ((best < 0) || (rank[i] < rank[best])))
        best = i;

    if(best < 0)
      return k;

    used[best] = 1;
    out[k++] = (uint8)best;
  }
}

void ui_ms_normalise(uint8 *rank, int n)
{
  uint8 order[UI_MS_LIST_MAX];
  int   k = ui_ms_order(rank, n, order), i;

  for(i = 0; i < k; i++)
    rank[order[i]] = (uint8)(i + 1);
}

static int ui_ms_count(const uint8 *rank, int n)
{
  int i, k = 0;

  for(i = 0; i < n; i++)
    if(rank[i])
      k++;

  return k;
}

int ui_ms_toggle(uint8 *rank, int n, int item)
{
  int i, r;

  if((item < 0) || (item >= n))
    return 0;

  ui_ms_normalise(rank, n);

  if(rank[item])
  {
    r = rank[item];
    rank[item] = 0;

    for(i = 0; i < n; i++)
      if(rank[i] > r)
        rank[i]--;

    return 0;
  }

  rank[item] = (uint8)(ui_ms_count(rank, n) + 1);
  return 1;
}

void ui_ms_select_all(uint8 *rank, int n)
{
  int i, k;

  ui_ms_normalise(rank, n);
  k = ui_ms_count(rank, n);

  for(i = 0; i < n; i++)
    if(!rank[i])
      rank[i] = (uint8)++k;
}

void ui_ms_clear(uint8 *rank, int n)
{
  int i;

  for(i = 0; i < n; i++)
    rank[i] = 0;
}

void ui_ms_scope_defaults(uint8 *rank12)
{
  ui_ms_clear(rank12, UI_MEAS_COUNT);
  rank12[UI_MEAS_FREQ] = 1;
  rank12[UI_MEAS_VPP]  = 2;
  rank12[UI_MEAS_VAVG] = 3;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Card fit

int ui_ms_fit(int n, int cap, int *more)
{
  *more = 0;

  if(cap <= 0)
    return 0;

  if(n <= cap)
    return n;

  *more = n - (cap - 1);
  return cap - 1;
}

int ui_ms_scope_cap(int ch, int other_shown)
{
  if((ch == 0) && !other_shown)
    return UI_MS_ROW_CARDS;

  return UI_MS_ROW_CARDS / 2;
}

uint32 ui_ms_card_x(int ch, int slot)
{
  return (ch ? 366 : 5) + UI_MS_PITCH * (uint32)slot;
}

const char *ui_ms_more_text(char *t, int more)
{
  char *p = t;

  *p++ = '+';

  if(more >= 10)
    *p++ = (char)('0' + (more / 10) % 10);

  *p++ = (char)('0' + more % 10);
  *p = 0;
  return t;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Spectrum list

const char *const ui_ms_sp_names[UI_MS_SP_KINDS] = { "Peak", "Level", "THD", "Noise", "RBW", "Span", "Center", "Time",
                                                     "Capture" };

int ui_ms_sp_len(int group)
{
  return (group == UI_MS_SP_VIEW) ? UI_MS_SP_PER_GROUP : UI_MS_SP_CH_ITEMS;
}

static void ui_ms_spec_group_defaults(ui_ms_spec *s, int g)
{
  ui_ms_clear(s->rank[g], UI_MS_SP_PER_GROUP);

  if(g == UI_MS_SP_VIEW)
  {
    s->rank[g][UI_MS_SP_RBW - 4]     = 1;
    s->rank[g][UI_MS_SP_TIME - 4]    = 2;
    s->rank[g][UI_MS_SP_CAPTURE - 4] = 3;
    return;
  }

  s->rank[g][UI_MS_SP_PEAK]  = 1;
  s->rank[g][UI_MS_SP_LEVEL] = 2;

  if(g == 0)
    s->rank[g][UI_MS_SP_THD] = 3;
}

void ui_ms_spec_defaults(ui_ms_spec *s)
{
  int g;

  for(g = 0; g < UI_MS_SP_GROUPS; g++)
    ui_ms_spec_group_defaults(s, g);
}

//The first four ranks of a group (bits 12-15 hold the marker; the view's fifth is in its own word)
uint16 ui_ms_spec_word(const ui_ms_spec *s, int group)
{
  uint16 w = UI_MS_SPEC_MARKER;
  int    i;

  for(i = 0; i < UI_MS_SP_CH_ITEMS; i++)
    w |= (uint16)((s->rank[group][i] & 7) << (3 * i));

  return w;
}

uint16 ui_ms_spec_capture_word(const ui_ms_spec *s)
{
  return (uint16)(UI_MS_SPEC_CAP_MARKER | (s->rank[UI_MS_SP_VIEW][UI_MS_SP_CAPTURE - 4] & 7));
}

int ui_ms_spec_capture_restore(ui_ms_spec *s, uint16 w)
{
  uint8 *rank = s->rank[UI_MS_SP_VIEW];
  int    ok = ((w & 0xFFF8) == UI_MS_SPEC_CAP_MARKER) && ((w & 7) <= UI_MS_SP_PER_GROUP), i, k = 0;

  if(ok)
    rank[UI_MS_SP_CAPTURE - 4] = (uint8)(w & 7);
  else
  {
    //Last in the row: after every chosen view item
    for(i = 0; i < UI_MS_SP_CH_ITEMS; i++)
      if(rank[i] > k)
        k = rank[i];
    rank[UI_MS_SP_CAPTURE - 4] = (uint8)(k + 1);
  }

  ui_ms_normalise(rank, UI_MS_SP_PER_GROUP);
  return ok;
}

int ui_ms_spec_restore(ui_ms_spec *s, int group, uint16 w)
{
  int i, n = ui_ms_sp_len(group);

  if((w & 0xF000) != UI_MS_SPEC_MARKER)
  {
    ui_ms_spec_group_defaults(s, group);
    return 0;
  }

  for(i = 0; i < UI_MS_SP_CH_ITEMS; i++)
    if(((w >> (3 * i)) & 7) > n)
    {
      ui_ms_spec_group_defaults(s, group);
      return 0;
    }

  for(i = 0; i < UI_MS_SP_CH_ITEMS; i++)
    s->rank[group][i] = (uint8)((w >> (3 * i)) & 7);

  //The view's ranks stay as saved until ui_ms_spec_capture_restore adds the fifth (Capture) and renumbers
  if(group == UI_MS_SP_VIEW)
    s->rank[group][UI_MS_SP_CAPTURE - 4] = 0;
  else
    ui_ms_normalise(s->rank[group], n);

  return 1;
}

int ui_ms_spec_kind(int group, int index)
{
  return (group == UI_MS_SP_VIEW) ? UI_MS_SP_RBW + index : index;
}

int ui_ms_spec_cards(const ui_ms_spec *s, const uint8 shown[2], int time_ok, ui_ms_card *out)
{
  ui_ms_card all[UI_MS_SP_GROUPS * UI_MS_SP_PER_GROUP];
  uint8      order[UI_MS_SP_PER_GROUP];
  int        n = 0, g, i, k, more, fit;

  for(g = 0; g < UI_MS_SP_GROUPS; g++)
  {
    if((g < 2) && !shown[g])
      continue;

    k = ui_ms_order(s->rank[g], ui_ms_sp_len(g), order);

    for(i = 0; i < k; i++)
    {
      int kind = ui_ms_spec_kind(g, order[i]);

      if((kind == UI_MS_SP_TIME) && !time_ok)
        continue;

      all[n].kind = (uint8)kind;
      all[n].ch   = (uint8)((g < 2) ? g : 0);
      n++;
    }
  }

  fit = ui_ms_fit(n, UI_MS_ROW_CARDS, &more);

  for(i = 0; i < fit; i++)
    out[i] = all[i];

  if(more)
  {
    out[fit].kind = UI_MS_SP_MORE;
    out[fit].ch   = (uint8)more;
    fit++;
  }

  return fit;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Spectrum maths

//10^(j / 100) in Q24, j = 0..100 (0.1 dB steps of a power ratio over one decade)
static const uint32 ui_ms_pow10_q24[101] =
{
  16777216, 17168008, 17567902, 17977111, 18395852, 18824346, 19262821, 19711510,
  20170650, 20640485, 21121264, 21613241, 22116678, 22631842, 23159005, 23698447,
  24250455, 24815321, 25393344, 25984831, 26590095, 27209458, 27843248, 28491801,
  29155460, 29834578, 30529514, 31240638, 31968326, 32712964, 33474947, 34254679,
  35052572, 35869052, 36704549, 37559508, 38434381, 39329633, 40245738, 41183182,
  42142461, 43124085, 44128574, 45156461, 46208290, 47284619, 48386019, 49513075,
  50666382, 51846554, 53054215, 54290007, 55554583, 56848616, 58172790, 59527809,
  60914389, 62333268, 63785196, 65270944, 66791300, 68347069, 69939077, 71568167,
  73235204, 74941071, 76686673, 78472935, 80300804, 82171250, 84085265, 86043862,
  88048081, 90098984, 92197659, 94345219, 96542801, 98791572, 101092723, 103447475,
  105857077, 108322805, 110845967, 113427901, 116069976, 118773593, 121540186, 124371220,
  127268198, 130232655, 133266164, 136370331, 139546804, 142797267, 146123443, 149527095,
  153010028, 156574090, 160221169, 163953199, 167772160
};

uint64 ui_ms_pow_q24(int32 cdb)
{
  int32  q, r, j, f;
  uint64 v;

  if(cdb < -12000) cdb = -12000;
  if(cdb > 2000)   cdb = 2000;

  //cdb = 1000 q + r, 0 <= r < 1000 (floor division)
  q = (cdb >= 0) ? cdb / 1000 : -((-cdb + 999) / 1000);
  r = cdb - 1000 * q;
  j = r / 10;
  f = r % 10;

  v = ui_ms_pow10_q24[j] + (((uint64)(ui_ms_pow10_q24[j + 1] - ui_ms_pow10_q24[j]) * (uint32)f) / 10);

  for(; q > 0; q--)
    v *= 10;

  for(; q < 0; q++)
    v /= 10;

  return v;
}

static uint64 ui_ms_isqrt64(uint64 v)
{
  uint64 r = 0, b = (uint64)1 << 62;

  while(b > v)
    b >>= 2;

  while(b)
  {
    if(v >= r + b)
    {
      v -= r + b;
      r = (r >> 1) + b;
    }
    else
      r >>= 1;

    b >>= 2;
  }

  return r;
}

#define UI_MS_THD_K_MIN       5
#define UI_MS_THD_HARMONICS   10

int32 ui_ms_thd(const int16 *cdb, uint32 last, uint32 k)
{
  uint64 p1 = 0, ph = 0, ratio;
  int32  ref;
  uint32 n, w, c, lo, hi, m, j;

  if(k < UI_MS_THD_K_MIN)
    return UI_MS_THD_F0_LOW;

  //The three bins around the second harmonic must be on the axis
  if(2 * k + 1 > last)
    return UI_MS_THD_F0_HIGH;

  ref = cdb[k];

  for(j = k - 1; j <= k + 1; j++)
    p1 += ui_ms_pow_q24(cdb[j] - ref);

  for(n = 2; n <= UI_MS_THD_HARMONICS; n++)
  {
    c = n * k;
    if(c + 1 > last)
      break;

    //Search window: the peak bin may be off by half a bin, so harmonic n by n / 2; kept narrow enough that the summed
    //bins of neighbouring harmonics never overlap (2 (w + 1) < k)
    w = n / 2 + 1;
    if(w > (k - 3) / 2)
      w = (k - 3) / 2;

    lo = c - w;
    hi = c + w;
    if(hi + 1 > last)
      hi = last - 1;

    for(m = lo, j = lo; j <= hi; j++)
      if(cdb[j] > cdb[m])
        m = j;

    for(j = m - 1; j <= m + 1; j++)
      ph += ui_ms_pow_q24(cdb[j] - ref);
  }

  ratio = (ph << 24) / p1;

  //sqrt of the Q24 ratio taken in Q40 -> Q20; percent in hundredths
  return (int32)((ui_ms_isqrt64(ratio << 16) * 10000) >> 20);
}

int32 ui_ms_noise_cdb(const int16 *cdb, uint32 first, uint32 last)
{
  static uint16 hist[2601];                         //0.1 dB steps from -200.0 to +60.0 dB
  uint32 k, n = 0, cum = 0, i;

  for(i = 0; i < 2601; i++)
    hist[i] = 0;

  for(k = first; k <= last; k++)
  {
    int32 v = cdb[k];

    if(v < -20000) v = -20000;
    if(v > 6000)   v = 6000;

    hist[(v + 20000) / 10]++;
    n++;
  }

  if(!n)
    return -20000;

  //The middle bin: the ((n + 1) / 2)-th lowest level
  for(i = 0; i < 2600; i++)
  {
    cum += hist[i];
    if(2 * cum >= n + 1)
      break;
  }

  return (int32)i * 10 - 20000 + UI_MS_NOISE_MEDIAN_CDB;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Card sources

static ui_ms_source ui_ms_sources[UI_MS_LIST_MAX];

void ui_ms_set_source(int item, ui_ms_source fn)
{
  if((item >= 0) && (item < UI_MS_LIST_MAX))
    ui_ms_sources[item] = fn;
}

ui_ms_source ui_ms_get_source(int item)
{
  return ((item >= 0) && (item < UI_MS_LIST_MAX)) ? ui_ms_sources[item] : 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//C7 counter: the copy of the counter's latest result and the six timing cards' sources (the card rule: ui_meas.h)

static ui_ms_counter ui_ms_cnt;

void ui_ms_counter_set(const ui_ms_counter *c)
{
  ui_ms_cnt = *c;
}

int ui_ms_counter_text(int item, int ch, char *text)
{
  const ui_ms_counter *c = &ui_ms_cnt;

  //The counter's result is for one channel: the other channel's cards (and anything else) use the record
  if(!c->valid || (c->ch != ch))
    return 0;

  switch(item)
  {
    case UI_MEAS_FREQ:   ui_fmt_mhz(text, c->freq_mhz);  return 1;
    case UI_MEAS_PERIOD: ui_fmt_ps(text, c->period_ps);  return 1;
    case UI_MEAS_PWIDTH: ui_fmt_ps(text, c->width_ps);   return 1;
    case UI_MEAS_NWIDTH: ui_fmt_ps(text, c->low_ps);     return 1;
    //Duty in hundredths of a percent (ppm / 100), rounded
    case UI_MEAS_PDUTY:  ui_fmt_sig4_fixed(text, (int32)((c->duty_ppm + 50) / 100), "%");              return 1;
    case UI_MEAS_NDUTY:  ui_fmt_sig4_fixed(text, (int32)((1000000 - c->duty_ppm + 50) / 100), "%");    return 1;
    default:             return 0;
  }
}

static int ui_ms_cnt_freq(int ch, char *t)   { return ui_ms_counter_text(UI_MEAS_FREQ, ch, t); }
static int ui_ms_cnt_period(int ch, char *t) { return ui_ms_counter_text(UI_MEAS_PERIOD, ch, t); }
static int ui_ms_cnt_pwidth(int ch, char *t) { return ui_ms_counter_text(UI_MEAS_PWIDTH, ch, t); }
static int ui_ms_cnt_nwidth(int ch, char *t) { return ui_ms_counter_text(UI_MEAS_NWIDTH, ch, t); }
static int ui_ms_cnt_pduty(int ch, char *t)  { return ui_ms_counter_text(UI_MEAS_PDUTY, ch, t); }
static int ui_ms_cnt_nduty(int ch, char *t)  { return ui_ms_counter_text(UI_MEAS_NDUTY, ch, t); }

void ui_ms_counter_register(void)
{
  ui_ms_set_source(UI_MEAS_FREQ,   ui_ms_cnt_freq);
  ui_ms_set_source(UI_MEAS_PERIOD, ui_ms_cnt_period);
  ui_ms_set_source(UI_MEAS_PWIDTH, ui_ms_cnt_pwidth);
  ui_ms_set_source(UI_MEAS_NWIDTH, ui_ms_cnt_nwidth);
  ui_ms_set_source(UI_MEAS_PDUTY,  ui_ms_cnt_pduty);
  ui_ms_set_source(UI_MEAS_NDUTY,  ui_ms_cnt_nduty);
}

//----------------------------------------------------------------------------------------------------------------------------------
//MEASURES menu

const char *ui_msm_title(int mode)
{
  if(mode == UI_MS_BODE)
    return "Frequency response measurements";

  return (mode == UI_MS_SPECTRUM) ? "Spectrum measurements" : "Scope measurements";
}

int ui_msm_rows(int mode)
{
  return (mode == UI_MS_SCOPE) ? 5 : (mode == UI_MS_SPECTRUM) ? 3 : 4;
}

//Edge, title band, its separator, then per row 44 px and the separator below it (the last one is the bottom edge)
uint32 ui_msm_height(int mode)
{
  return 1 + UI_MSM_TITLE_H + 1 + (UI_MSM_ROW_H + 1) * (uint32)ui_msm_rows(mode);
}

uint32 ui_msm_top(int mode)
{
  return UI_MSM_BOTTOM - ui_msm_height(mode);
}

//Halves: CH1 x 233..479, the double line 480..481, CH2 x 482..729
#define UI_MSM_HX(h)          ((h) ? 482 : 233)
#define UI_MSM_HW(h)          ((h) ? 248 : 247)

static ui_msm_cell *ui_msm_row(ui_msm_cell *c, uint32 x, uint32 w, uint32 y, int count, int what, int group, int index0,
                               int whats_vary)
{
  int k;

  for(k = 0; k < count; k++)
  {
    uint32 x0 = x + ((uint32)k * (w + 1)) / (uint32)count;
    uint32 x1 = x + ((uint32)(k + 1) * (w + 1)) / (uint32)count - 1;

    c->x     = (uint16)x0;
    c->y     = (uint16)y;
    c->w     = (uint16)(x1 - x0);
    c->h     = UI_MSM_ROW_H;
    c->what  = (uint8)(whats_vary ? what + k : what);
    c->group = (uint8)group;
    c->index = (uint8)(whats_vary ? 0 : index0 + k);
    c++;
  }

  return c;
}

int ui_msm_cells(int mode, ui_msm_cell *out)
{
  ui_msm_cell *c = out;
  uint32 y0 = ui_msm_top(mode) + 1 + UI_MSM_TITLE_H + 1, y;
  int    h, r;

  //Frequency response: one block across the whole width
  if(mode == UI_MS_BODE)
  {
    int k;

    y = y0;
    c = ui_msm_row(c, UI_MSM_HX(0), 729 - UI_MSM_HX(0) + 1, y, 2, UI_MSC_ALL, 0, 0, 1);

    for(r = 0; r < 3; r++)
    {
      y += UI_MSM_ROW_H + 1;
      c = ui_msm_row(c, UI_MSM_HX(0), 729 - UI_MSM_HX(0) + 1, y, 4, UI_MSC_ITEM, 0, 4 * r, 0);
    }

    for(k = 1; k <= 4; k++)
      if(c[-k].index >= BU_MS_COUNT)
        c[-k].what = UI_MSC_EMPTY;

    return (int)(c - out);
  }

  for(h = 0; h < 2; h++)
  {
    y = y0;

    if(mode == UI_MS_SCOPE)
    {
      //Sources, then Select All / Clear / Hide, then twelve items, four per row
      c = ui_msm_row(c, UI_MSM_HX(h), UI_MSM_HW(h), y, 5, UI_MSC_SOURCE, h, 0, 0);
      y += UI_MSM_ROW_H + 1;
      c = ui_msm_row(c, UI_MSM_HX(h), UI_MSM_HW(h), y, 3, UI_MSC_ALL, h, 0, 1);
      y += UI_MSM_ROW_H + 1;

      for(r = 0; r < 3; r++)
      {
        c = ui_msm_row(c, UI_MSM_HX(h), UI_MSM_HW(h), y, 4, UI_MSC_ITEM, h, 4 * r, 0);
        y += UI_MSM_ROW_H + 1;
      }
    }
    else
    {
      //The channel label, Select All, Clear (laid out as the Scope's action row, the first place made the label),
      //then its items
      c = ui_msm_row(c, UI_MSM_HX(h), UI_MSM_HW(h), y, 3, UI_MSC_ALL - 1, h, 0, 1);
      c[-3].what = UI_MSC_LABEL;
      c[-2].what = UI_MSC_ALL;
      c[-1].what = UI_MSC_CLEAR;
      y += UI_MSM_ROW_H + 1;
      c = ui_msm_row(c, UI_MSM_HX(h), UI_MSM_HW(h), y, 4, UI_MSC_ITEM, h, 0, 0);
    }
  }

  //The spectrum's view items across the whole width
  if(mode == UI_MS_SPECTRUM)
    c = ui_msm_row(c, UI_MSM_HX(0), 729 - UI_MSM_HX(0) + 1, y0 + 2 * (UI_MSM_ROW_H + 1), UI_MS_SP_PER_GROUP, UI_MSC_ITEM,
                   UI_MS_SP_VIEW, 0, 0);

  return (int)(c - out);
}

const char *ui_msm_text(int mode, const ui_msm_cell *c)
{
  static const char *const refs[2][4] = { { "Ref 1", "Ref 2", "Ref 3", "Ref 4" }, { "Ref 5", "Ref 6", "Ref 7", "Ref 8" } };
  const char *ch = c->group ? "CH2" : "CH1";

  switch(c->what)
  {
    case UI_MSC_SOURCE:  return c->index ? refs[c->group & 1][(c->index - 1) & 3] : ch;
    case UI_MSC_ALL:     return "Select All";
    case UI_MSC_CLEAR:   return "Clear";
    case UI_MSC_HIDE:    return "Hide";
    case UI_MSC_LABEL:   return ch;
    case UI_MSC_ITEM:
      if(mode == UI_MS_BODE)
        return (c->index < BU_MS_COUNT) ? bu_ms_names[c->index] : "";
      if(mode == UI_MS_SPECTRUM)
        return (ui_ms_spec_kind(c->group, c->index) < UI_MS_SP_MORE) ? ui_ms_sp_names[ui_ms_spec_kind(c->group, c->index)] : "";
      return (c->index < UI_MEAS_COUNT) ? measurement_names[c->index] : "";
    default:             return "";
  }
}

int ui_msm_hit(int mode, uint32 x, uint32 y, ui_msm_cell *c)
{
  ui_msm_cell cells[UI_MSM_CELLS_MAX];
  uint32 top = ui_msm_top(mode);
  int    n, i;

  if((x < UI_MSM_X) || (x >= UI_MSM_X + UI_MSM_W) || (y < top) || (y >= UI_MSM_BOTTOM))
    return -1;

  n = ui_msm_cells(mode, cells);

  for(i = 0; i < n; i++)
    if((x + 1 >= cells[i].x) && (x <= cells[i].x + cells[i].w) && (y + 1 >= cells[i].y) && (y <= cells[i].y + cells[i].h))
    {
      *c = cells[i];
      return 1;
    }

  return 0;
}
