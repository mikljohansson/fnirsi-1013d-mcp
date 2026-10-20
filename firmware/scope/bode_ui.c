//----------------------------------------------------------------------------------------------------------------------------------
// bode_ui.c - the Frequency response screen as data (see bode_ui.h). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "bode_ui.h"
#include "ui_fmt.h"

const char *const bu_show_names[BU_SHOW_COUNT]  = { "Overlaid", "Magnitude only", "Phase only" };
const char *const bu_show_values[BU_SHOW_COUNT] = { "Overlay", "Mag", "Phase" };
const char *const bu_axis_names[2]              = { "Log", "Linear" };
const char *const bu_axis_row                   = "Frequency axis";
const char *const bu_trace_names[BU_TRACE_COUNT] = { "Lines", "Dots" };

//----------------------------------------------------------------------------------------------------------------------------------
//Text helpers (no libc)

static char *bu_put(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;
  *p = 0;
  return p;
}

static char *bu_uint(char *p, uint32 v)
{
  char t[11];
  int  n = 0;

  do { t[n++] = (char)('0' + (v % 10)); v /= 10; } while(v);
  while(n) *p++ = t[--n];
  *p = 0;
  return p;
}

//v / 10^dec with dec decimals, rounded from v (already scaled), with a sign: "+1.21", "-60.8", "0.00"
static char *bu_fixed(char *p, int64 v, int dec, int plus)
{
  uint64 a = (uint64)((v < 0) ? -v : v);
  uint32 sc = (dec == 2) ? 100 : 10;
  int k;

  if(a && (v < 0))
    *p++ = '-';
  else if(a && plus)
    *p++ = '+';

  p = bu_uint(p, (uint32)(a / sc));
  *p++ = '.';

  for(k = dec - 1; k >= 0; k--)
  {
    uint32 d = (uint32)(a % sc);

    *p++ = (char)('0' + ((k == 1) ? d / 10 : d % 10));
  }

  *p = 0;
  return p;
}

//Integer division rounding half away from zero
static int64 bu_rdiv(int64 v, int64 d)
{
  return (v >= 0) ? (v + d / 2) / d : -((-v + d / 2) / d);
}

char *bu_fmt_db(char *t, int32 mdb)
{
  return bu_put(bu_fixed(t, bu_rdiv(mdb, 10), 2, 1), "dB");
}

char *bu_fmt_deg(char *t, int32 mdeg)
{
  return bu_put(bu_fixed(t, bu_rdiv(mdeg, 100), 1, 1), "deg");
}

char *bu_fmt_fdiff(char *t, int64 dmhz)
{
  if(dmhz < 0)
  {
    *t++ = '-';
    dmhz = -dmhz;
  }

  return ui_fmt_mhz(t, (uint64)dmhz);
}

char *bu_freq_short(char *t, uint64 mhz)
{
  static const char *const units[3] = { "", "k", "M" };
  uint64 c;                     //hundredths of the unit
  int    u = 0;

  if(mhz >= 1000000000ull)
    u = 2;
  else if(mhz >= 1000000ull)
    u = 1;

  c = (mhz + ((u == 2) ? 5000000ull : (u == 1) ? 5000ull : 5ull)) / ((u == 2) ? 10000000ull : (u == 1) ? 10000ull : 10ull);

  //Three significant digits
  if(c >= 10000)
    c = ((c + 50) / 100) * 100;
  else if(c >= 1000)
    c = ((c + 5) / 10) * 10;

  t = bu_uint(t, (uint32)(c / 100));

  if(c % 100)
  {
    *t++ = '.';
    *t++ = (char)('0' + (c / 10) % 10);
    if(c % 10)
      *t++ = (char)('0' + c % 10);
    *t = 0;
  }

  return bu_put(t, units[u]);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Scales

void bu_scale_pick(int32 mx, int32 mn, const int32 *divs, int ndivs, bu_scale *s)
{
  int i, lane;

  //The first division that holds the data with the free top division; else the first that holds it at all (the free
  //division costs one of six: bode-ui.md section 9 C3)
  for(lane = 1; lane >= 0; lane--)
    for(i = 0; i < ndivs; i++)
    {
      int32 d = divs[i];
      int32 top = ((mx >= 0) ? ((mx + d - 1) / d) * d : -((-mx) / d) * d) + (lane ? d : 0);

      s->div = d;
      s->top = top;

      if(top - mn <= BU_NDIV * d)
        return;
    }

  //Nothing holds it: s is the coarsest division with the top just above the maximum (the low end is clipped)
}

static const int32 bu_mag_divs[3] = { 5000, 10000, 20000 };
static const int32 bu_ph_divs[2]  = { 45000, 90000 };

void bu_scale_mag(int32 mx, int32 mn, bu_scale *s)
{
  bu_scale_pick(mx, mn, bu_mag_divs, 3, s);
}

void bu_scale_phase(int32 mx, int32 mn, bu_scale *s)
{
  bu_scale_pick(mx, mn, bu_ph_divs, 2, s);
}

void bu_scale_empty(bu_scale *mag, bu_scale *ph)
{
  bu_scale_mag(0, -40000, mag);
  bu_scale_phase(90000, -90000, ph);
}

int32 bu_scale_y(const bu_scale *s, int32 v)
{
  int64 y = BU_Y + (((int64)s->top - v) * BU_DIV) / s->div;

  if(y < BU_Y) y = BU_Y;
  if(y > BU_Y + BU_H) y = BU_Y + BU_H;
  return (int32)y;
}

int32 bu_x_log(int64 l0, int64 l1, int64 lf)
{
  int64 x;

  if(l1 <= l0)
    return BU_PX + BU_PW / 2;

  x = ((lf - l0) * BU_PW) / (l1 - l0);
  if(x < 0) x = 0;
  if(x > BU_PW) x = BU_PW;
  return BU_PX + (int32)x;
}

int32 bu_x_lin(uint64 f0, uint64 f1, uint64 f)
{
  int64 x;

  if(f1 <= f0)
    return BU_PX + BU_PW / 2;

  if(f <= f0)
    return BU_PX;

  x = (int64)(((f - f0) * BU_PW) / (f1 - f0));
  if(x > BU_PW) x = BU_PW;
  return BU_PX + (int32)x;
}

uint64 bu_lin_step(uint64 f0, uint64 f1)
{
  static const uint8 m125[3] = { 1, 2, 5 };
  uint64 span = (f1 > f0) ? f1 - f0 : 1, dec = 1;
  int k;

  for(;;)
  {
    for(k = 0; k < 3; k++)
      if(span / (dec * m125[k]) <= 10)
        return dec * m125[k];

    dec *= 10;
  }
}

//The grid rule (bode_ui.h)
static int bu_fgrid_lin(uint64 f0, uint64 f1, bu_gline *g)
{
  uint64 st = bu_lin_step(f0, f1), f = ((f0 + st - 1) / st) * st;
  int    n = 0, i;

  for(; (f <= f1) && (n < BU_GRID_MAX); f += st)
  {
    g[n].f = f;
    g[n].major = 1;
    g[n].label = 1;
    n++;
  }

  if(n > 6)
    for(i = 1; i < n; i += 2)
      g[i].label = 0;

  return n;
}

int bu_fgrid(uint64 f0, uint64 f1, int linear, bu_gline *g)
{
  static const uint8 sub[3] = { 1, 2, 5 };
  uint64 dec;
  int    n = 0, k, minors, labels;

  if(f1 < f0)
  {
    uint64 t = f0;

    f0 = f1;
    f1 = t;
  }

  if(linear || !f0)
    return bu_fgrid_lin(f0, f1, g);

  minors = (f1 < 1000 * f0);
  labels = (f1 < 30 * f0);

  for(dec = 1; (dec <= f1) && (n < BU_GRID_MAX); dec *= 10)
    for(k = 0; (k < 3) && (n < BU_GRID_MAX); k++)
    {
      uint64 f = dec * sub[k];

      if((f < f0) || (f > f1) || (k && !minors))
        continue;

      g[n].f = f;
      g[n].major = (uint8)!k;
      g[n].label = (uint8)(!k || labels);
      n++;
    }

  if(n < 2)
    return bu_fgrid_lin(f0, f1, g);

  return n;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Slider

int32 bu_slider_x(int step)
{
  static const uint16 sub[3] = { 0, 301, 699 };     //log10 of 1, 2, 5 in thousandths

  if(step < 0) step = 0;
  if(step > BODE_STEPS - 1) step = BODE_STEPS - 1;

  return BU_SL_X + (int32)(((uint32)BU_SL_W * (1000u * (uint32)(step / 3) + sub[step % 3]) + 3500) / 7000);
}

//1 Hz .. 10 MHz over the track: seven decades, log2 of the frequency in Q32 against log2 of 1 Hz
int32 bu_slider_x_mhz(uint64 mhz)
{
  int64 l0 = bode_log2_q32(BODE_FMIN_MHZ), l1 = bode_log2_q32(BODE_FMAX_MHZ), lf;

  if(mhz < BODE_FMIN_MHZ) mhz = BODE_FMIN_MHZ;
  if(mhz > BODE_FMAX_MHZ) mhz = BODE_FMAX_MHZ;

  lf = bode_log2_q32(mhz);
  return BU_SL_X + (int32)(((lf - l0) * BU_SL_W + (l1 - l0) / 2) / (l1 - l0));
}

int bu_slider_shift(int start0, int stop0, int32 dx)
{
  int32 target = bu_slider_x(start0) + dx, bd = 0x7FFFFFFF;
  int   i, best = start0;

  for(i = 0; i + (stop0 - start0) <= BODE_STEPS - 1; i++)
  {
    int32 d = bu_slider_x(i) - target;

    if(d < 0) d = -d;
    if(d < bd)
    {
      bd = d;
      best = i;
    }
  }

  return best - start0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Cursor readout

const char *bu_flag_reason(uint16 f)
{
  if(f & BODE_F_CH2_OVERLOAD) return "overload";
  if(f & BODE_F_CLIPPED)      return "CH2 clipped";
  if(f & BODE_F_CH1_CLIPPED)  return "CH1 clipped";
  if(f & BODE_F_UNSETTLED)    return "unsettled";
  if(f & BODE_F_NEAR_FLOOR)   return "near floor";
  if(f & BODE_F_LOW_SNR)      return "low SNR";
  if(f & BODE_F_INCOHERENT)   return "incoherent";
  return "";
}

static void bu_ro_point(bu_ro_row *r, const char *name, uint64 f, int32 m, int32 p, uint16 fl)
{
  bu_put(r->name, name);
  ui_fmt_mhz(r->f, f);
  bu_fmt_db(r->db, m);
  r->flagged = (uint8)((fl & BODE_F_BAD) != 0);
  r->grey = r->flagged;

  if(r->flagged)
    bu_put(r->ph, bu_flag_reason(fl));
  else
    bu_fmt_deg(r->ph, p);
}

void bu_readout(uint64 f1, int32 m1, int32 p1, uint16 fl1, uint64 f2, int32 m2, int32 p2, uint16 fl2, bu_ro_row r[3])
{
  int32 dp = p2 - p1;

  bu_ro_point(&r[0], "C1", f1, m1, p1, fl1);
  bu_ro_point(&r[1], "C2", f2, m2, p2, fl2);

  //The difference, the phase wrapped to (-180, 180]
  while(dp > 180000)   dp -= 360000;
  while(dp <= -180000) dp += 360000;

  bu_put(r[2].name, "C2-C1");
  bu_fmt_fdiff(r[2].f, (int64)f2 - (int64)f1);
  bu_fmt_db(r[2].db, m2 - m1);
  bu_fmt_deg(r[2].ph, dp);
  r[2].flagged = 0;
  r[2].grey = (uint8)(r[0].grey || r[1].grey);
}

//----------------------------------------------------------------------------------------------------------------------------------
//MEASURES

const char *const bu_ms_names[BU_MS_COUNT] =
{
  "Level", "-3dB", "BW", "Peak", "Q", "Ph45", "Ph90", "Ph180", "Sweep", "Thru"
};

int bu_ms_block(int item)
{
  if((item >= BU_MS_PH45) && (item <= BU_MS_PH180))
    return BU_BLOCK_PHASE;

  if(item >= BU_MS_SWEEP)
    return BU_BLOCK_NEUTRAL;

  return BU_BLOCK_MAG;
}

void bu_ms_defaults(uint8 *rank)
{
  int i;

  for(i = 0; i < BU_MS_COUNT; i++)
    rank[i] = 0;

  rank[BU_MS_3DB]   = 1;
  rank[BU_MS_PEAK]  = 2;
  rank[BU_MS_Q]     = 3;
  rank[BU_MS_LEVEL] = 4;
}

//Word w holds items first..first + n - 1, 4 bits each from bit 0
static const uint8 bu_ms_first[BU_MS_WORDS] = { 0, 3, 7 };
static const uint8 bu_ms_n[BU_MS_WORDS]     = { 3, 4, 3 };

uint16 bu_ms_word(const uint8 *rank, int w)
{
  uint16 v = (w == 0) ? BU_MS_MARKER : 0;
  int    i;

  for(i = 0; i < bu_ms_n[w]; i++)
    v |= (uint16)((rank[bu_ms_first[w] + i] & 15) << (4 * i));

  return v;
}

int bu_ms_restore(uint8 *rank, const uint16 *w3)
{
  uint8 r[BU_MS_COUNT];
  int   w, i;

  if((w3[0] & 0xF000) != BU_MS_MARKER)
  {
    bu_ms_defaults(rank);
    return 0;
  }

  for(w = 0; w < BU_MS_WORDS; w++)
  {
    //Bits above the ranks must be clear (the marker in word 0); an erased word reads 0xFFFF
    if((w > 0) && (bu_ms_n[w] < 4) && (w3[w] >> (4 * bu_ms_n[w])))
    {
      bu_ms_defaults(rank);
      return 0;
    }

    for(i = 0; i < bu_ms_n[w]; i++)
    {
      uint8 v = (uint8)((w3[w] >> (4 * i)) & 15);

      if(v > BU_MS_COUNT)
      {
        bu_ms_defaults(rank);
        return 0;
      }

      r[bu_ms_first[w] + i] = v;
    }
  }

  for(i = 0; i < BU_MS_COUNT; i++)
    rank[i] = r[i];

  return 1;
}

//The lowest non-zero of two crossing frequencies
static uint64 bu_lowest(uint64 a, uint64 b)
{
  if(!a) return b;
  if(!b) return a;
  return (a < b) ? a : b;
}

int bu_card_text(int item, const bu_card_in *in, char *t)
{
  const bode_markers *mk = in->mk;
  uint64 f = 0;

  if(item == BU_MS_SWEEP)
  {
    if((in->table == BU_TABLE_NONE) && !in->autoset)
    {
      bu_put(t, "not run");
      return BU_VAL_GREY;
    }

    bu_uint(bu_put(bu_uint(bu_put(t, in->autoset ? "auto " : ""), (uint32)in->done), "/"), (uint32)in->count);
    return BU_VAL_WHITE;
  }

  if(item == BU_MS_THRU)
  {
    switch(in->thru)
    {
      case BU_THRU_APPLIED: bu_put(t, "applied"); return BU_VAL_GREEN;
      case BU_THRU_STALE:   bu_put(t, "stale");   return BU_VAL_AMBER;
      case BU_THRU_OFF:     bu_put(t, "off");     return BU_VAL_GREY;
      default:              bu_put(t, "none");    return BU_VAL_GREY;
    }
  }

  if((in->table == BU_TABLE_NONE) && !in->autoset)
  {
    bu_put(t, "no sweep");
    return BU_VAL_GREY;
  }

  if((in->table != BU_TABLE_COMPLETE) || in->autoset)
  {
    bu_put(t, "sweeping");
    return BU_VAL_GREY;
  }

  switch(item)
  {
    case BU_MS_LEVEL:
      if(mk->shape == BODE_SHAPE_NONE)
        break;
      bu_fmt_db(t, mk->ref_mdb);
      return BU_VAL_WHITE;

    case BU_MS_3DB:
      if(mk->n3db)
        f = mk->f3db_mhz[0];
      break;

    case BU_MS_BW:
      if((mk->n3db == 2) && (mk->f3db_mhz[1] > mk->f3db_mhz[0]))
        f = mk->f3db_mhz[1] - mk->f3db_mhz[0];
      break;

    case BU_MS_PEAK:
      if(mk->have_peak)
        f = mk->peak_mhz;
      break;

    case BU_MS_Q:
      if(!mk->have_q)
        break;
      bu_fixed(t, bu_rdiv(mk->q_x100, 10), 1, 0);
      return BU_VAL_WHITE;

    case BU_MS_PH45:  f = bu_lowest(mk->ph_mhz[0], mk->ph_mhz[1]); break;
    case BU_MS_PH90:  f = bu_lowest(mk->ph_mhz[2], mk->ph_mhz[3]); break;
    case BU_MS_PH180: f = mk->ph_mhz[5];                            break;
    default:          break;
  }

  if(!f)
  {
    bu_put(t, "not found");
    return BU_VAL_GREY;
  }

  ui_fmt_mhz(t, f);
  return BU_VAL_WHITE;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Texts (docs/plans/bode-ui.md section 6)

const char *const bu_warn[BU_W_COUNT] =
{
  "CH2 overloaded by DC: set CH2 range Auto",
  "Sweep stopped: no capture started; tap RUN",
  "Sweep stopped: no capture arrived; tap RUN",
  "Sweep stopped: the PC took over",
  "No frequency in this range: change Start or Stop",
  "Thru sweep: both probe tips on the DUT input",
  "CH2 overrange: set CH2 range Auto or raise V/div",
  "CH1 level low: the DUT loads the 1 kOhm source",
  "Thru does not match this sweep: measure again",
  "DC level beyond the offset range: AC-couple DUT",
  "Auto set: no response: CH2 tip on the DUT output?",
  "Trigger was EXT (the generator's pin): now CH1",
  "Roll off for the frequency response",
  "No corner from 100 Hz to 10 MHz: span kept",
  "Auto set cancelled: settings kept",
  "Nothing to save: run a sweep first",
  "Frequency response needs FPGA 0x1534 or later",
  "Leave the waveform viewer first",
  "Sweep table not saved: check the SD card",
  "ADC readout fault: see Diagnostics",
  "Auto set: no consistent response. Check wiring",
  "Auto set: CH1 level low: CH1 tip on the DUT input?",
  "Auto set: peak not confirmed: sweep by hand",
  "CH1 level low above 2 MHz: the x1 probe rolls off",
};

const char *const bu_instruction[2] =
{
  "Connect the calibration post and CH1 to the input, CH2 to the output,",
  "grounds on the probe clips. Press RUN.",
};

char *bu_warn_unsettled(char *t, int n)
{
  return bu_put(bu_uint(t, (uint32)n), (n > 1) ? " points not settled: choose a longer Settle" :
                                                 " point not settled: choose a longer Settle");
}

char *bu_warn_floor(char *t, int n)
{
  return bu_put(bu_uint(t, (uint32)n), (n > 1) ? " points near the floor: shown grey" : " point near the floor: shown grey");
}


const char *const bu_acq_chip[2] = { "Set by", "the sweep" };
const char *const bu_acq_note    = "The sweep sets the sample rate for each point";
const char *const bu_gen_note    = "Driven by the frequency response sweep";
const char *const bu_trig_note   = "Not used by the sweep";

char *bu_estimate_text(char *t, uint32 ms)
{
  uint32 s = (ms + 500) / 1000;

  t = bu_put(t, "about ");

  if(s < 1)
    s = 1;

  if(s < 100)
    return bu_put(bu_uint(t, s), " s");

  return bu_put(bu_uint(t, (s + 30) / 60), " min");
}

//----------------------------------------------------------------------------------------------------------------------------------
//fw 0.2.84: the Single sweep button, CH1 low's cause, AUTO SET's verdict

int bu_single_button(int running, int single, int autoset, int done, int count, uint32 est_ms, const char **label,
                     char *value)
{
  if(running && autoset)
  {
    *label = "Single sweep";
    bu_put(value, "during AUTO SET");
    return BU_SINGLE_GREY;
  }

  if(running && single)
  {
    *label = "Stop sweep";
    bu_put(bu_uint(bu_put(bu_uint(value, (uint32)((done < 0) ? 0 : done)), " / "), (uint32)((count < 0) ? 0 : count)), "");
    return BU_SINGLE_STOP;
  }

  *label = "Single sweep";

  if(running)
  {
    bu_put(value, "STOP after this sweep");
    return BU_SINGLE_LAST;
  }

  bu_estimate_text(value, est_ms);
  return BU_SINGLE_START;
}

int bu_auto_busy(int autoset, int final, int running)
{
  return (autoset || final) && running;
}

int bu_after_sweep(int run, int single, int done)
{
  return run && !single && done;
}

uint8 bu_view_bits(int active, int run, int single)
{
  return (uint8)((active ? 1 : 0) | ((run && !single) ? 2 : 0));
}

int bu_warn_ch1_low(char *t, const uint8 *r1, const uint64 *f_mhz, const uint16 *flags, const uint8 *have, int n)
{
  static const char *const vpd[BU_CH1_FINE_RANGE] = { "5", "2.5", "1" };
  int i, coarse = BU_CH1_FINE_RANGE, dut = 0, probe = 0;

  for(i = 0; i < n; i++)
  {
    if(!have[i] || !(flags[i] & BODE_F_CH1_LOW))
      continue;

    if(r1[i] < BU_CH1_FINE_RANGE)
    {
      if(r1[i] < coarse)
        coarse = r1[i];
    }
    else if(f_mhz[i] > BU_PROBE_ROLLOFF_MHZ)
      probe = 1;
    else
      dut = 1;
  }

  *t = 0;

  if(coarse < BU_CH1_FINE_RANGE)
    bu_put(bu_put(bu_put(t, "CH1 level low at "), vpd[coarse]), " V/div: sweep again");
  else if(dut)
    bu_put(t, bu_warn[BU_W_CH1_LOW]);
  else if(probe)
    bu_put(t, bu_warn[BU_W_CH1_LOW_HF]);
  else
    return 0;

  return 1;
}

int bu_auto_bounce(const uint8 *r2, const uint8 *have, int n, int switches)
{
  int i, last = -1, dir = 0, rev = 0;

  for(i = 0; i < n; i++)
  {
    int d;

    if(!have[i])
      continue;

    if(last >= 0)
    {
      d = (r2[i] > r2[last]) ? 1 : (r2[i] < r2[last]) ? -1 : 0;

      if(d)
      {
        if(dir && (d != dir))
          rev++;
        dir = d;
      }
    }

    last = i;
  }

  return (rev >= 2) || (rev && (switches >= BU_AUTO_SWITCHES));
}

int bu_auto_verdict(int r, const bode_autospan *a, int pass, int32 prev_ref, int bounce)
{
  if(bounce)
    return BU_W_AUTO_INCONSISTENT;

  switch(r)
  {
    case BODE_AS_NO_RESPONSE: return BU_W_NO_RESPONSE;
    case BODE_AS_CH1_LOW:     return BU_W_AUTO_CH1;
    case BODE_AS_INCONSISTENT: return BU_W_AUTO_INCONSISTENT;
    case BODE_AS_UNCONFIRMED: return BU_W_AUTO_PEAK;
    default:                  break;
  }

  //A finer pass around the band the previous one found: no band there, or its peak read much lower, is inconsistent
  if(pass > 1)
  {
    if(r != BODE_AS_SPAN)
      return BU_W_AUTO_INCONSISTENT;
    if(a->ref_mdb < prev_ref - BU_AUTO_REF_DROP_MDB)
      return BU_W_AUTO_INCONSISTENT;
  }

  //The passes' budget used up without the band resolving
  if((r == BODE_AS_SPAN) && a->coarse && (pass >= BODE_AUTO_PASSES))
    return BU_W_AUTO_PEAK;

  return -1;
}
