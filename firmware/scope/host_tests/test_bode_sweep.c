//----------------------------------------------------------------------------------------------------------------------------------
// test_bode_sweep.c - the Bode sweep engine (bode_sweep.c) end to end against a DUT model behind bode_hw: the calibration
// post's square (wiring A: -1.24 / +1.86 V against the probe reference, or wiring B: 1.24 V lower) through 1 kOhm into an
// RC or a resistive divider, the front end per range (nominal volts per code, the offset PWM, 0.7 code noise, 8-bit
// quantisation, a DC window ahead of the offset that pins the sensitive ranges), the decimation as an anti-aliased average.
// Mirrors mcp/tests/test_bode_sim.py: RC within 0.1 dB / 0.5 deg, CH2 down-ranging with splices, Hold, wiring B's
// saturation (auto steps up, Hold stops), STOP, the thru calibration, timing of the non-blocking steps.
//----------------------------------------------------------------------------------------------------------------------------------

#include <math.h>
#include <stdlib.h>

#include "test_util.h"
#include "bode_sweep.h"

#define PI 3.14159265358979323846

typedef struct
{
  //DUT: rs into r into c (c = 0: a divider r / (rs + r) ... modelled as r_load to ground at the output: H = rl / (r + rl))
  double rs, r, c, rl;
  double l;                   //fw 0.2.93: an inductor in the shunt too (a parallel tank; 0 = none)
  uint32 r2_changes;          //fw 0.2.94: CH2 range writes that changed the range
  double lo, hi;              //post levels against the probe reference
  double gain2;               //CH2 front-end gain mismatch
  double window_v[6];         //DC beyond this pins the front end of a range (0 = no limit)
  int    thru;                //CH2 on the DUT input too
  int    rail[2];             //fw 0.2.71: a readout fault: every rail-th sample of the channel reads 255 (0: none)
  int    rail_reads;          //fw 0.2.83: the rails only in the first rail_reads captures (0: every capture)
  int    check;               //fw 0.2.83: bode_cap.check of every capture (BODE_CHK_*)
  uint32 fault_at;            //fw 0.2.83: this capture (1 = the first) has BODE_CHK_FAULT (0: none)
  uint32 flat_from, flat_n;   //fw 0.2.87: captures flat_from .. flat_from + flat_n - 1 are one saturated value on both
                              //channels (the 0x153A first-record artefact)
  double scale2;              //fw 0.2.87: CH2's signal x this between scale2_lo and scale2_hi Hz (0 = 1; the DUT's
  double scale2_lo, scale2_hi;//output changing between sweeps)
  uint32 brk_from, brk_n;     //fw 0.2.89: captures brk_from .. + brk_n - 1 are broken: the samples before brk_at are
  uint32 brk_at;              //0.37 period off in phase (the FPGA-auto record joined from two, bench cells_1235)
  uint32 rail_from;           //fw 0.2.89: the rails only from this capture on (0: from the first)
  uint8  level;               //fw 0.2.91: the trigger level of the arm (CH1 rising)
  int    splice_untrig;       //fw 0.2.91: a capture whose CH1 does not cross the level ends on the time-out and is
                              //joined from two captures (the samples before total / 3 a 0.37 period off)
  uint32 untrig;              //captures that ended on the time-out
  uint32 reads;

  //State
  uint32 now;
  uint32 inc;
  int    gen;
  int    range[2];
  int32  off[2];
  uint32 armed_at, rec_ms;
  int    armed, forced;
  int    fail_arm;
  uint32 arms, ch_writes;
  uint16 buf[2][BODE_MAX_RECORD + 2];
  unsigned rnd;
} dut;

static const double vpd[6] = { 5.0, 2.5, 1.0, 0.5, 0.2, 0.1 };

static double vpl(int r) { return vpd[r] / 29.15; }

static double gauss(dut *d)
{
  double u = 0;
  int i;

  for(i = 0; i < 12; i++)
  {
    d->rnd = d->rnd * 1103515245u + 12345u;
    u += ((d->rnd >> 8) & 0xFFFF) / 65536.0;
  }

  return u - 6.0;
}

static uint32 h_ms(void *ctx) { return ((dut *)ctx)->now; }

static void h_gen(void *ctx, uint32 inc, int on)
{
  dut *d = ctx;

  d->gen = on;
  if(on)
    d->inc = inc;
}

static void h_channel(void *ctx, int c, int r, int32 off)
{
  dut *d = ctx;

  if((c == 1) && (d->range[1] != r))
    d->r2_changes++;
  d->range[c] = r;
  d->off[c] = off;
  d->ch_writes++;
}

static void h_info(void *ctx, int c, int r, bode_chinfo *ci)
{
  (void)ctx; (void)c;
  ci->cal_offset = 1000;
  ci->dc_shift_value = 1000;
  ci->uv_q8 = (uint32)(vpl(r) * 1e6 * 256 + 0.5);
}

static int h_arm(void *ctx, const bode_plan *pp, uint32 total, uint8 level)
{
  dut *d = ctx;

  d->level = level;

  if(d->fail_arm)
    return -1;

  d->armed = 1;
  d->forced = 0;
  d->armed_at = d->now;
  d->rec_ms = (uint32)(((uint64)total * pp->n) / 200000) + 1;
  d->arms++;
  return 0;
}

static int h_poll(void *ctx)
{
  dut *d = ctx;

  return d->armed && (d->forced || ((d->now - d->armed_at) >= d->rec_ms));
}

static void h_force(void *ctx) { ((dut *)ctx)->forced = 1; }

//The steady-state voltages of the DUT input (node 0) and output (node 1) at time t: a sum of the square's odd harmonics
//through the network, each averaged over the output sample's interval (the decimation filter), up to the band edge
static void dut_volts(dut *d, double f, double t, double dt, double band, double *v0, double *v1)
{
  double a0 = 0, a1 = 0, amp = d->hi - d->lo, mean = (d->hi + d->lo) / 2;
  int k;

  if(!d->gen)
  {
    //The pin off: the DUT rests at the post's low level, 1 kOhm to the probe reference... treat as 0 V
    *v0 = *v1 = 0;
    return;
  }

  for(k = 1; k * f < band && k < 4000; k += 2)
  {
    double w = 2 * PI * k * f, bk = 2 * amp / (PI * k);
    double sinc = (dt > 0) ? sin(w * dt / 2) / (w * dt / 2) : 1;
    //Z of the shunt (c parallel rl), the output across it; the input after rs
    double zr = 0, zi = 0, hr, hi, ir, ii, den;

    //y = jwc + 1/rl
    double yr = (d->rl > 0) ? 1 / d->rl : 0, yi = w * d->c - ((d->l > 0) ? 1 / (w * d->l) : 0);
    double ymag = yr * yr + yi * yi;

    zr = yr / ymag;  zi = -yi / ymag;                //1 / y
    if(d->c == 0 && d->rl == 0) { zr = 1e12; zi = 0; }

    //H_out = z / (rs + r + z), H_in = (r + z) / (rs + r + z)
    den = (d->rs + d->r + zr) * (d->rs + d->r + zr) + zi * zi;
    hr = (zr * (d->rs + d->r + zr) + zi * zi) / den;
    hi = (zi * (d->rs + d->r + zr) - zr * zi) / den;
    ir = ((d->r + zr) * (d->rs + d->r + zr) + zi * zi) / den;
    ii = (zi * (d->rs + d->r + zr) - (d->r + zr) * zi) / den;

    //square: high for the first half period: sum bk sin(w t) = bk cos(w t - pi / 2); the sample's interval is centred
    //on t + dt / 2
    {
      double ph = w * (t + dt / 2) - PI / 2;

      a0 += bk * sinc * (ir * cos(ph) - ii * sin(ph));
      a1 += bk * sinc * (hr * cos(ph) - hi * sin(ph));
    }
  }

  //DC: the input sees the post's mean through rs and r into the shunt (rl to the reference: a divider; c: open)
  {
    double dc1 = (d->rl > 0) ? mean * d->rl / (d->rs + d->r + d->rl) : mean;
    double dc0 = (d->rl > 0) ? mean * (d->r + d->rl) / (d->rs + d->r + d->rl) : mean;

    if(d->l > 0)
    {
      //The inductor shorts the output at DC
      dc1 = 0;
      dc0 = mean * d->r / (d->rs + d->r);
    }

    *v0 = dc0 + a0;
    *v1 = dc1 + a1;
  }
}

static int h_read(void *ctx, const bode_plan *pp, uint32 total, bode_cap *cap)
{
  dut *d = ctx;
  double f = d->inc * 50e6 / 4294967296.0, fs = 200e6 / pp->n, dt = (pp->n > 1) ? 1 / fs : 0;
  double band = (pp->n > 1) ? 0.45 * fs : 30e6;
  double sig = (pp->n > 1) ? 0.7 / sqrt((double)pp->n) : 0.7;
  uint32 i;
  int    c;

  int    pass, trig = 0;

  d->armed = 0;

  for(pass = 0; pass < 2; pass++)
  {
  uint16 lo = 0xFFFF, hi = 0;

  for(i = 0; i < total; i++)
  {
    double v[2];

    int    brk = (d->brk_n && ((d->reads + 1) >= d->brk_from) && ((d->reads + 1) < d->brk_from + d->brk_n) &&
                  (i < d->brk_at)) || ((pass == 1) && (i < total / 3));

    dut_volts(d, f, i / fs + (brk ? 0.37 / f : 0.0), dt, band, &v[0], &v[1]);
    if(d->thru)
      v[1] = v[0];

    for(c = 0; c < 2; c++)
    {
      double s2 = (d->scale2 && (f >= d->scale2_lo) && (f <= d->scale2_hi)) ? d->scale2 : 1.0;
      double vv = v[c] * ((c == 1) ? d->gain2 * s2 : 1.0), code;
      double win = d->window_v[d->range[c]];

      code = 128.0 - d->off[c] / 10.0 + vv / vpl(d->range[c]);

      //The front end ahead of the offset injection: a DC level beyond its window pins the record (no noise either)
      if((win > 0) && (fabs((d->hi + d->lo) / 2) > win))
      {
        code = floor(128.0 - d->off[c] / 10.0 + (((d->hi + d->lo) / 2 > 0) ? win : -win) / vpl(d->range[c]) + 0.5);
        if(code < 0) code = 0;
        if(code > 255) code = 255;
        d->buf[c][i] = (uint16)(code * 256);
      }
      else if(pp->n == 1)
      {
        code = floor(code + sig * gauss(d) + 0.5);
        if(code < 0) code = 0;
        if(code > 255) code = 255;
        d->buf[c][i] = (uint16)(code * 256);
      }
      else
      {
        code += sig * gauss(d);
        if(code < 0) code = 0;
        if(code > 255) code = 255;
        d->buf[c][i] = (uint16)floor(code * 256 + 0.5);
      }
    }

    if(d->buf[0][i] < lo) lo = d->buf[0][i];
    if(d->buf[0][i] > hi) hi = d->buf[0][i];
  }

  //CH1 rising through the level somewhere in the record (the generator on)
  trig = d->gen && (lo < (uint32)d->level * 256) && (hi > (uint32)d->level * 256);

  if(trig || !d->splice_untrig || pass)
    break;
  }

  if(!trig)
    d->untrig++;

  //A readout fault (FPGA 0x1539's A cores, or one saturated record): rail values whatever the input
  d->reads++;

  if(d->flat_n && (d->reads >= d->flat_from) && (d->reads < d->flat_from + d->flat_n))
    for(c = 0; c < 2; c++)
      for(i = 0; i < total; i++)
        d->buf[c][i] = 0xFF00;

  for(c = 0; c < 2; c++)
    if(d->rail[c] && (!d->rail_reads || (d->reads <= (uint32)d->rail_reads)) && (d->reads >= d->rail_from))
      for(i = 0; i < total; i += (uint32)d->rail[c])
        d->buf[c][i] = 0xFF00;

  for(c = 0; c < 2; c++)
  {
    cap->x[c] = d->buf[c];
    cap->s[c].cpr_q24 = 65536;
    cap->s[c].zero_q8 = 0;
    cap->s[c].volt_mdb = bode_volt_mdb((uint32)(vpl(d->range[c]) * 1e6 * 256 + 0.5), 1);
    cap->zero_c100[c] = 12800 - d->off[c] * 10;
    cap->uv_q8[c] = (uint32)(vpl(d->range[c]) * 1e6 * 256 + 0.5);
  }

  cap->fir = 1;
  cap->check = (uint8)((d->fault_at && (d->reads == d->fault_at)) ? BODE_CHK_FAULT : d->check);
  cap->triggered = (uint8)trig;
  return 0;
}

static bode_eng eng;
static dut      D;
static bode_hw  HW = { &D, h_ms, h_gen, h_channel, h_info, h_arm, h_poll, h_force, h_read, 1 };

static void dut_rc(dut *d)
{
  memset(d, 0, sizeof(*d));
  d->rs = 1000; d->r = 1600; d->c = 10e-9; d->rl = 0;
  d->lo = -1.24; d->hi = 1.86;
  d->gain2 = 1.0;
  d->rnd = 12345;
}

//Run until the sweep ends; the clock advances 1 ms per step. Returns the steps taken.
static uint32 run(bode_eng *e, uint32 max_ms)
{
  uint32 n = 0;

  while((bode_state(e) == BODE_ST_RUNNING) && (n < max_ms))
  {
    bode_step(e);
    D.now++;
    n++;
  }

  return n;
}

static bode_cfg cfg(double start, double stop, int points, int settle, int ch2)
{
  bode_cfg c;

  c.start_mhz = (uint64)(start * 1000 + 0.5);
  c.stop_mhz  = (uint64)(stop * 1000 + 0.5);
  c.points    = (uint16)points;
  c.settle    = (uint8)settle;
  c.ch2_mode  = (uint8)ch2;
  c.background = 1;
  return c;
}

static double rc_db(double f)  { double x = 2 * PI * f * 1600 * 10e-9; return -10 * log10(1 + x * x); }
static double rc_deg(double f) { return -atan(2 * PI * f * 1600 * 10e-9) * 180 / PI; }

static void test_rc_auto(void)
{
  bode_cfg c = cfg(1000, 300e3, 30, BODE_SETTLE_NORMAL, BODE_CH2_AUTO);
  double worst_db = 0, worst_deg = 0;
  int i, n, bad = 0, spliced = 0;
  uint32 ms;

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  ms = run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  n = bode_count(&eng);
  T_EQ(n, 30);
  T_EQ(bode_done_count(&eng), n);

  for(i = 0; i < n; i++)
  {
    bode_point p;
    double f = bode_plans(&eng)[i].f_mhz / 1000.0, ddb, ddeg;

    T_TRUE(bode_point_get(&eng, i, &p));
    ddb = fabs(p.h_mdb / 1000.0 - rc_db(f));
    ddeg = fabs(p.h_mdeg / 1000.0 - rc_deg(f));
    if(ddb > worst_db) worst_db = ddb;
    if(ddeg > worst_deg) worst_deg = ddeg;
    if(p.flags & BODE_F_BAD)
      bad++;
    if(p.flags & BODE_F_SPLICED)
      spliced++;
  }

  printf("  RC auto 1k-300k/30: %u ms, worst %.3f dB %.3f deg, %d flagged, %d splices, %u switches, CH2 range %d at the end\n",
         ms, worst_db, worst_deg, bad, spliced, bode_switches(&eng), D.range[1]);
  T_TRUE(worst_db < 0.1);
  T_TRUE(worst_deg < 0.5);
  T_EQ(bad, 0);
  T_TRUE(bode_switches(&eng) >= 1);       //the stopband takes CH2 down
  T_TRUE(D.range[1] >= 4);
  T_EQ(D.gen, 0);                         //generator off after the sweep

  //Markers of the table: -3 dB at 9.947 kHz (C 10 nF, R 1.6 kOhm; the source does not count, H = V2 / V1)
  {
    static uint64 f[BODE_MAX_POINTS];
    static int32  m[BODE_MAX_POINTS], ph[BODE_MAX_POINTS];
    static uint8  ok[BODE_MAX_POINTS];
    bode_markers mk;

    for(i = 0; i < n; i++)
    {
      bode_point p;

      bode_point_get(&eng, i, &p);
      f[i] = bode_plans(&eng)[i].f_mhz;
      m[i] = p.h_mdb;
      ph[i] = p.h_mdeg;
      ok[i] = !(p.flags & BODE_F_BAD);
    }

    bode_markers_find(f, m, ph, ok, n, &mk);
    printf("  markers: shape %d, -3 dB %.1f Hz, -45 deg %.1f Hz\n", mk.shape, mk.f3db_mhz[0] / 1000.0, mk.ph_mhz[1] / 1000.0);
    T_EQ(mk.shape, BODE_SHAPE_LOWPASS);
    T_EQ(mk.n3db, 1);
    T_TRUE(fabs(mk.f3db_mhz[0] / 1000.0 / 9947.2 - 1) < 0.02);
    T_TRUE(fabs(mk.ph_mhz[1] / 1000.0 / 9947.2 - 1) < 0.02);
  }
}

static void test_hold(void)
{
  bode_cfg c = cfg(1000, 300e3, 20, BODE_SETTLE_FAST, BODE_CH2_HOLD);
  int i, small = 0;

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  T_EQ(bode_switches(&eng), 0);
  T_EQ(D.range[1], 3);

  for(i = 0; i < bode_count(&eng); i++)
  {
    bode_point p;

    bode_point_get(&eng, i, &p);
    T_EQ(p.r2, 3);
    if(p.flags & BODE_F_CH2_SMALL)
      small++;
  }

  printf("  Hold at range 3: %d points under 3 codes\n", small);
}

static void test_wiring_b(void)
{
  bode_cfg c = cfg(1000, 100e3, 15, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  int i, bad = 0;

  //Wiring B: the DUT 1.24 V below the probe reference; ranges 4 and 5 pin beyond 0.8 and 0.5 V DC (the DUT sits at -0.93 V)
  dut_rc(&D);
  D.lo = -1.24 - 1.24;
  D.hi = 1.86 - 1.24;
  D.window_v[4] = 0.8;
  D.window_v[5] = 0.5;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);

  for(i = 0; i < bode_count(&eng); i++)
  {
    bode_point p;

    bode_point_get(&eng, i, &p);
    T_TRUE(p.r2 <= 3);
    if(p.flags & BODE_F_BAD)
      bad++;
  }

  printf("  wiring B auto: CH2 stays on range <= 3, %d flagged, %u switches\n", bad, bode_switches(&eng));
  T_EQ(bad, 0);

  //Hold on a saturated range stops with the overload error
  c.ch2_mode = BODE_CH2_HOLD;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 5), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_ERROR);
  T_EQ(bode_error(&eng), BODE_ERR_OVERLOAD);
  T_EQ(D.gen, 0);
}

static void test_stop_and_steps(void)
{
  bode_cfg c = cfg(10, 1000, 20, BODE_SETTLE_NORMAL, BODE_CH2_AUTO);
  uint32 n = 0, longest = 0;

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);

  //Steps return at once while settling or capturing: count the arms per step
  while((bode_state(&eng) == BODE_ST_RUNNING) && (bode_done_count(&eng) < 5) && (n < 2000000))
  {
    uint32 a = D.arms;

    bode_step(&eng);
    if(D.arms - a > longest)
      longest = D.arms - a;
    D.now++;
    n++;
  }

  T_EQ(longest, 1);
  T_EQ(bode_state(&eng), BODE_ST_RUNNING);
  bode_stop(&eng, 0);
  T_EQ(bode_state(&eng), BODE_ST_RUNNING);   //finishes the point in progress
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_STOPPED);
  T_TRUE(bode_done_count(&eng) >= 5);
  T_TRUE(bode_done_count(&eng) <= 6);
  printf("  STOP after %d of %d points at %u ms\n", bode_done_count(&eng), bode_count(&eng), D.now);

  //A new sweep with the same plan keeps the table until overwritten; abort ends at once
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  T_TRUE(bode_done_count(&eng) >= 5);
  bode_stop(&eng, 1);
  T_EQ(bode_state(&eng), BODE_ST_STOPPED);
  T_EQ(D.gen, 0);

  //fw 0.2.71: STOP during the pre-scan ends the sweep after that capture, nothing measured
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  bode_stop(&eng, 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_STOPPED);
  T_EQ(bode_error(&eng), BODE_ERR_STOP);
  T_EQ(D.gen, 0);

  //An arm failure ends the sweep with the error
  D.fail_arm = 1;
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_ERROR);
  T_EQ(bode_error(&eng), BODE_ERR_ARM);
  D.fail_arm = 0;
}

//fw 0.2.71 (owner's fail-fast rule): a readout that returns rail values at 5 V/div ends the sweep with
//BODE_ERR_READOUT instead of a table of clipped points; a healthy readout never trips it
static void test_readout_fault(void)
{
  bode_cfg c = cfg(1000, 100e3, 10, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  int ch;

  for(ch = 0; ch < 2; ch++)
  {
    dut_rc(&D);
    D.rail[ch] = 97;
    bode_init(&eng, &HW);
    T_EQ(bode_start(&eng, &c, 0, 3), 0);
    run(&eng, 600000);
    printf("  readout fault on CH%d: state %d, reason %d, ranges %d / %d, %d points measured\n", ch + 1,
           bode_state(&eng), bode_error(&eng), D.range[0], D.range[1], bode_done_count(&eng));
    T_EQ(bode_state(&eng), BODE_ST_ERROR);
    T_EQ(bode_error(&eng), BODE_ERR_READOUT);
    T_EQ(D.range[ch], 0);
    T_EQ(bode_done_count(&eng), 0);
    T_EQ(D.gen, 0);
  }

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
}

//fw 0.2.83 (bench 2026-10-20 10:52): with the capture self-check (FPGA 0x153A+) its verdict decides reason 8, never the
//clip. A clean check with rail samples at 5 V/div is a flagged table, not a fault; a flagged capture ends the sweep at
//any phase; one railed capture (a real clip, a saturated record) leaves no trace in the later points.
static void test_readout_check(void)
{
  bode_cfg c = cfg(1000, 100e3, 10, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  bode_point p;
  int ch, i, n, flagged;

  //Rails in every capture, the self-check clean: no reason 8; the sweep completes with the points flagged clipped
  for(ch = 0; ch < 2; ch++)
  {
    dut_rc(&D);
    D.rail[ch] = 97;
    D.check = BODE_CHK_CLEAN;
    bode_init(&eng, &HW);
    T_EQ(bode_start(&eng, &c, 0, 3), 0);
    run(&eng, 600000);
    printf("  clean self-check, rails on CH%d: state %d, reason %d, %d points measured\n", ch + 1, bode_state(&eng),
           bode_error(&eng), bode_done_count(&eng));
    T_EQ(bode_state(&eng), BODE_ST_DONE);
    T_EQ(bode_error(&eng), BODE_ERR_NONE);
    n = bode_count(&eng);
    T_EQ(bode_done_count(&eng), n);

    for(i = 0, flagged = 0; i < n; i++)
      if(bode_point_get(&eng, i, &p) && (p.flags & (ch ? BODE_F_CLIPPED : BODE_F_CH1_CLIPPED)))
        flagged++;

    T_EQ(flagged, n);
  }

  //A stale overload (bench 2026-10-20 10:52): before 0.2.83 the readout's sticky ADoverload of any earlier readout (a
  //clip in the Scope view, an earlier sweep) marked every capture of the next sweep clipped. Here the earlier sweep
  //railed on both channels; the next one, on the same engine with clean records, has no flagged point at all
  dut_rc(&D);
  D.rail[0] = D.rail[1] = 97;
  D.check = BODE_CHK_CLEAN;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  D.rail[0] = D.rail[1] = 0;
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  n = bode_count(&eng);

  for(i = 0, flagged = 0; i < n; i++)
  {
    T_TRUE(bode_point_get(&eng, i, &p));

    if(p.flags & BODE_F_BAD)
      flagged++;
    else
      T_TRUE(fabs(p.h_mdb / 1000.0 - rc_db(bode_plans(&eng)[i].f_mhz / 1000.0)) < 0.1);
  }

  printf("  clean sweep after a railed one: CH1 range %d, %d of %d points flagged\n", D.range[0], flagged, n);
  T_EQ(flagged, 0);
  T_EQ(D.range[0], 3);

  //One railed capture inside the sweep (the pre-scan's, as the saturated record after a rate change): no point clipped.
  //The pre-scan ranges from that capture, so a railed CH1 pre-scan leaves CH1 on 2.5 V/div (ch1_low on the post's
  //3.1 Vpp): the engine does not range CH1 up again (as the host's _prescan); noted, not changed
  for(ch = 0; ch < 2; ch++)
  {
    dut_rc(&D);
    D.rail[ch] = 97;
    D.rail_reads = 1;
    D.check = BODE_CHK_CLEAN;
    bode_init(&eng, &HW);
    T_EQ(bode_start(&eng, &c, 0, 3), 0);
    run(&eng, 600000);
    T_EQ(bode_state(&eng), BODE_ST_DONE);
    n = bode_count(&eng);
    T_EQ(bode_done_count(&eng), n);

    for(i = 0, flagged = 0; i < n; i++)
    {
      T_TRUE(bode_point_get(&eng, i, &p));

      if(p.flags & (BODE_F_CH1_CLIPPED | BODE_F_CLIPPED))
        flagged++;

      if(!(p.flags & BODE_F_BAD))
        T_TRUE(fabs(p.h_mdb / 1000.0 - rc_db(bode_plans(&eng)[i].f_mhz / 1000.0)) < 0.1);
    }

    printf("  one railed capture on CH%d: ranges %d / %d at the end, %d of %d points clipped\n", ch + 1,
           D.range[0], D.range[1], flagged, n);
    T_EQ(flagged, 0);
  }

  //Without the self-check the same single railed capture is harmless too (the record decides, the next one is clean)
  dut_rc(&D);
  D.rail[0] = 97;
  D.rail_reads = 1;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);

  //A flagged capture ends the sweep with reason 8: in the pre-scan (the first capture) and in the middle of the points
  for(i = 0; i < 2; i++)
  {
    dut_rc(&D);
    D.check = BODE_CHK_CLEAN;
    D.fault_at = i ? 12 : 1;
    bode_init(&eng, &HW);
    T_EQ(bode_start(&eng, &c, 0, 3), 0);
    run(&eng, 600000);
    printf("  self-check fault at capture %u: state %d, reason %d, %d points measured\n", (unsigned)D.fault_at,
           bode_state(&eng), bode_error(&eng), bode_done_count(&eng));
    T_EQ(bode_state(&eng), BODE_ST_ERROR);
    T_EQ(bode_error(&eng), BODE_ERR_READOUT);
    T_EQ(D.reads, D.fault_at);
    T_EQ(D.gen, 0);

    if(!i)
      T_EQ(bode_done_count(&eng), 0);
    else
      T_TRUE(bode_done_count(&eng) > 0);
  }
}

//fw 0.2.87: a flat record (the 0x153A first-record artefact) is taken once more, as the host's _measure does: a flat
//pre-scan no longer leaves CH1 on 2.5 V/div, a flat point record no longer becomes a flagged point
static void test_flat_retake(void)
{
  bode_cfg c = cfg(1000, 100e3, 10, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  bode_point p;
  uint32 base, at;
  int i, n, flagged;

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  base = D.reads;

  for(at = 1; at <= 12; at += 11)
  {
    dut_rc(&D);
    D.flat_from = at;
    D.flat_n = 1;
    bode_init(&eng, &HW);
    T_EQ(bode_start(&eng, &c, 0, 3), 0);
    run(&eng, 600000);
    T_EQ(bode_state(&eng), BODE_ST_DONE);
    n = bode_count(&eng);

    for(i = 0, flagged = 0; i < n; i++)
      if(bode_point_get(&eng, i, &p) && (p.flags & (BODE_F_BAD | BODE_F_CH1_LOW)))
        flagged++;

    printf("  flat capture %u: CH1 range %d, %d of %d points flagged, %u captures (clean sweep %u)\n", (unsigned)at,
           D.range[0], flagged, n, (unsigned)D.reads, (unsigned)base);
    T_EQ(flagged, 0);
    T_EQ(D.range[0], 3);
    T_EQ(D.reads, base + 1);
  }

  //Flat twice (a stuck readout): the pre-scan keeps CH1 on range 1, the points are ch1_low + incoherent (host)
  dut_rc(&D);
  D.flat_from = 1;
  D.flat_n = 1000;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  T_TRUE(bode_point_get(&eng, 0, &p));
  T_TRUE((p.flags & (BODE_F_INCOHERENT | BODE_F_CH1_LOW)) == (BODE_F_INCOHERENT | BODE_F_CH1_LOW));
  T_TRUE(!(p.flags & BODE_F_CH2_OVERLOAD));
  T_EQ(p.r1, 1);
  T_EQ(bode_switches(&eng), 0);
}

//fw 0.2.87: the range plan (bode_sweep.h). Sweeps over the owner's RC (1 kOhm / 15 nF, 100 Hz .. 1 MHz, 25 points)
typedef struct
{
  uint8  r1[BODE_MAX_POINTS], r2[BODE_MAX_POINTS];
  int32  h[BODE_MAX_POINTS];
  uint16 switches;
  uint16 planned;             //fw 0.2.93: planned CH2 range transitions
  uint32 reads;
  int    reused, n, bad;
  int    small;               //fw 0.2.93: points flagged ch2_small
} sweep_rec;

static void sweep_once(const bode_cfg *c, int thru, int hold, sweep_rec *s)
{
  uint32 r0 = D.reads;
  bode_point p;
  int i;

  T_EQ(bode_start(&eng, c, thru, hold), 0);
  s->reused = bode_plan_reused(&eng);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  s->n = bode_count(&eng);
  s->switches = bode_switches(&eng);
  s->planned = bode_planned_switches(&eng);
  s->reads = D.reads - r0;
  s->bad = 0;
  s->small = 0;

  for(i = 0; i < s->n; i++)
  {
    T_TRUE(bode_point_get(&eng, i, &p));
    s->r1[i] = p.r1;
    s->r2[i] = p.r2;
    s->h[i]  = p.h_mdb;
    if(p.flags & BODE_F_BAD)
      s->bad++;
    if(p.flags & BODE_F_CH2_SMALL)
      s->small++;
  }
}

//CH2 range transitions between neighbouring points of a sweep
static int transitions(const sweep_rec *s)
{
  int i, t = 0;

  for(i = 1; i < s->n; i++)
    if(s->r2[i] != s->r2[i - 1])
      t++;

  return t;
}

static int same_ranges(const sweep_rec *a, const sweep_rec *b)
{
  int i;

  if(a->n != b->n)
    return 0;

  for(i = 0; i < a->n; i++)
    if((a->r1[i] != b->r1[i]) || (a->r2[i] != b->r2[i]))
      return 0;

  return 1;
}

static void print_rec(const char *what, const sweep_rec *s)
{
  int i;

  printf("  plan %-22s reused %d, %u unplanned + %u planned switches, %u captures, %d flagged, CH2 ranges", what,
         s->reused, (unsigned)s->switches, (unsigned)s->planned, (unsigned)s->reads, s->bad);
  for(i = 0; i < s->n; i++)
    printf(" %d", s->r2[i]);
  printf("\n");
}

static void dut_rc15(dut *d)
{
  dut_rc(d);
  d->r = 1000;
  d->c = 15e-9;
}

static void test_range_plan(void)
{
  bode_cfg c = cfg(100, 1e6, 25, BODE_SETTLE_NORMAL, BODE_CH2_AUTO), c2;
  sweep_rec a, b, x;
  double worst = 0;
  int i, mid;

  dut_rc15(&D);
  bode_init(&eng, &HW);

  //First sweep: from scratch; second and third: from the plan, no switch, the same ranges and H, no pre-scan
  sweep_once(&c, 0, 3, &a);
  print_rec("first", &a);
  T_EQ(a.reused, 0);
  T_TRUE(a.switches >= 1);
  T_EQ(a.planned, 0);                     //fw 0.2.93: a fresh sweep has no plan: its steps are ranging (unplanned)
  T_EQ(a.bad, 0);

  for(i = 0; i < 2; i++)
  {
    int k;

    sweep_once(&c, 0, 3, &b);
    print_rec(i ? "third" : "second", &b);
    T_EQ(b.reused, 1);
    T_EQ(b.switches, 0);
    T_EQ(b.planned, 2);                   //fw 0.2.93: the RC's 0.5 -> 0.2 -> 0.1 V/div: "2 planned, 0 unplanned"
    T_EQ(b.planned, transitions(&a));
    T_TRUE(same_ranges(&a, &b));
    T_TRUE(b.reads < a.reads);
    T_EQ(b.bad, 0);

    for(k = 0; k < a.n; k++)
      if(abs(a.h[k] - b.h[k]) > worst)
        worst = abs(a.h[k] - b.h[k]);
  }

  printf("  plan: H of the plan sweeps within %.3f dB of the first\n", worst / 1000.0);
  T_TRUE(worst < 50);

  //The DUT's output drops 10 dB at one point (a stopband point, CH2 on a range with room below): one switch down; the
  //entry follows, so the next sweep is quiet again
  for(mid = a.n - 1; (mid > 0) && (a.r2[mid] >= 5); mid--)
    ;
  T_TRUE(mid > 0);
  D.scale2 = 0.3;
  D.scale2_lo = bode_plans(&eng)[mid].f_mhz / 1000.0 * 0.999;
  D.scale2_hi = bode_plans(&eng)[mid].f_mhz / 1000.0 * 1.001;
  sweep_once(&c, 0, 3, &b);
  print_rec("output -10 dB at one pt", &b);
  T_EQ(b.reused, 1);
  T_EQ(b.switches, 1);
  sweep_once(&c, 0, 3, &x);
  print_rec("again", &x);
  T_EQ(x.reused, 1);
  T_EQ(x.switches, 0);
  T_EQ(x.r2[mid], a.r2[mid] + 1);

  //Back up 10 dB: it fits the finer range (no switch, the point valid there)
  D.scale2 = 0;
  sweep_once(&c, 0, 3, &b);
  print_rec("output back", &b);
  T_EQ(b.reused, 1);
  T_EQ(b.switches, 0);
  T_EQ(b.bad, 0);

  //Up 12 dB more at that point: it clips: CH2 one range up there (the splice on the previous point), valid again
  D.scale2 = 4.0;
  sweep_once(&c, 0, 3, &b);
  print_rec("output +12 dB at one pt", &b);
  T_EQ(b.reused, 1);
  T_TRUE(b.switches >= 1);
  T_TRUE(b.r2[mid] <= a.r2[mid]);
  T_EQ(b.bad, 0);
  D.scale2 = 0;

  //A settings change ranges from scratch (Points, Settle, CH2 mode, Hold's range)
  c2 = c;
  c2.points = 24;
  sweep_once(&c2, 0, 3, &b);
  T_EQ(b.reused, 0);
  T_TRUE(b.switches >= 1);
  c2 = c;
  c2.settle = BODE_SETTLE_FAST;
  sweep_once(&c2, 0, 3, &b);
  T_EQ(b.reused, 0);
  sweep_once(&c2, 0, 3, &b);
  T_EQ(b.reused, 1);
  c2.ch2_mode = BODE_CH2_HOLD;
  sweep_once(&c2, 0, 3, &b);
  T_EQ(b.reused, 0);
  sweep_once(&c2, 0, 3, &b);
  T_EQ(b.reused, 1);
  sweep_once(&c2, 0, 4, &b);
  T_EQ(b.reused, 0);

  //A cancelled first sweep leaves an incomplete plan: the next one ranges from scratch, the one after reuses
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 300);
  bode_stop(&eng, 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_STOPPED);
  sweep_once(&c, 0, 3, &b);
  T_EQ(b.reused, 0);
  sweep_once(&c, 0, 3, &b);
  T_EQ(b.reused, 1);

  //A cancelled sweep from a complete plan keeps it
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 300);
  bode_stop(&eng, 0);
  run(&eng, 600000);
  sweep_once(&c, 0, 3, &b);
  T_EQ(b.reused, 1);
  T_EQ(b.switches, 0);

  //The thru sweep ranges from scratch and drops the plan
  D.thru = 1;
  sweep_once(&c, 1, 3, &b);
  T_EQ(b.reused, 0);
  D.thru = 0;
  sweep_once(&c, 0, 3, &b);
  T_EQ(b.reused, 0);
  T_TRUE(same_ranges(&a, &b));

  //fw 0.2.89: the output 20 dB down at a point on 0.5 V/div: from the plan it steps down twice in the same sweep (up
  //to 0.2.88 one range per sweep), and the sweep after is quiet
  dut_rc15(&D);
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &a);
  for(mid = a.n - 1; (mid > 0) && (a.r2[mid] != 3); mid--)
    ;
  T_TRUE(mid > 0);
  D.scale2 = 0.1;
  D.scale2_lo = bode_plans(&eng)[mid].f_mhz / 1000.0 * 0.999;
  D.scale2_hi = bode_plans(&eng)[mid].f_mhz / 1000.0 * 1.001;
  sweep_once(&c, 0, 3, &b);
  print_rec("output -20 dB at one pt", &b);
  T_EQ(b.reused, 1);
  T_EQ(b.switches, 2);
  sweep_once(&c, 0, 3, &x);
  print_rec("again", &x);
  T_EQ(x.switches, 0);
  T_EQ(x.r2[mid], 5);
  T_EQ(x.bad, 0);
  D.scale2 = 0;

  //fw 0.2.89: broken records (a phase jump a third of the way in) on several captures of a plan sweep: each is taken
  //again, no point flagged, no switch, the ranges unchanged
  dut_rc15(&D);
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &a);
  for(i = 0; i < 3; i++)
  {
    D.brk_from = D.reads + 3 + 9 * i;
    D.brk_n = 1;
    D.brk_at = 900;
    sweep_once(&c, 0, 3, &b);
    print_rec("one broken record", &b);
    T_EQ(b.reused, 1);
    T_EQ(b.switches, 0);
    T_EQ(b.bad, 0);
    T_TRUE(same_ranges(&a, &b));
  }
  D.brk_n = 0;

  //fw 0.2.89: one CH2 glitch at the rail on a planned point: measured again on its range, no step up
  sweep_once(&c, 0, 3, &a);
  D.rail[1] = 100000;
  D.rail_from = D.reads + 5;
  D.rail_reads = (int)D.rail_from;
  sweep_once(&c, 0, 3, &b);
  print_rec("one CH2 glitch", &b);
  T_EQ(b.reused, 1);
  T_EQ(b.switches, 0);
  T_TRUE(same_ranges(&a, &b));
  D.rail[1] = 0;
  D.rail_reads = 0;
  D.rail_from = 0;

  //A flat pre-scan (twice: CH1 dead there, range 1): fw 0.2.90 the check's clean captures range CH1 to 0.5 V/div in
  //the same sweep (0.2.89: the sweep ran on 2.5 V/div and only the next one ranged again); the plan is kept
  dut_rc15(&D);
  D.flat_from = 1;
  D.flat_n = 2;
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &b);
  print_rec("flat pre-scan", &b);
  for(i = 0; i < b.n; i++)
    T_EQ(b.r1[i], 3);
  T_EQ(b.bad, 0);
  sweep_once(&c, 0, 3, &x);
  T_EQ(x.reused, 1);
  T_EQ(x.switches, 0);
  T_TRUE(same_ranges(&b, &x));
  D.flat_n = 0;

  //fw 0.2.90: a rail sample in the pre-scan's CH1 record (its peak reads 128 codes on 2.5 V/div: the pre-scan picks
  //5 V/div): the check steps CH1 to 0.5 V/div; a rail sample in the check's first record: measured again, CH1 stays
  for(i = 0; i < 2; i++)
  {
    dut_rc15(&D);
    D.rail[0] = 100000;
    D.rail_from = (uint32)(i + 1);
    D.rail_reads = i + 1;
    bode_init(&eng, &HW);
    sweep_once(&c, 0, 3, &b);
    print_rec(i ? "rail in the check" : "rail in the pre-scan", &b);
    T_EQ(b.r1[0], 3);
    T_EQ(b.bad, 0);
    sweep_once(&c, 0, 3, &x);
    T_EQ(x.reused, 1);
    T_EQ(x.switches, 0);
  }
  D.rail[0] = 0;
  D.rail_from = 0;
  D.rail_reads = 0;

  //fw 0.2.91: a rail sample in a point's CH2 record of a fresh sweep: measured again, no step up (0.2.90 stepped up on
  //one record outside the plan)
  dut_rc15(&D);
  bode_init(&eng, &HW);
  c2 = c;
  c2.points = 24;
  sweep_once(&c2, 0, 3, &b);
  sweep_once(&c, 0, 3, &a);
  D.rail[1] = 100000;
  D.rail_from = D.reads + 14;
  D.rail_reads = (int)D.rail_from;
  sweep_once(&c2, 0, 3, &x);
  D.rail[1] = 0;
  D.rail_from = 0;
  D.rail_reads = 0;
  print_rec("fresh, one CH2 rail", &x);
  T_EQ(x.reused, 0);
  T_EQ(x.switches, b.switches);
  T_TRUE(same_ranges(&x, &b));
  T_EQ(x.bad, 0);

  //fw 0.2.91: CH1 rails in the pre-scan and the check's first two records: CH1 ends on 0.5 V/div all the same (never
  //5 V/div on records that clip once each)
  dut_rc15(&D);
  D.rail[0] = 100000;
  D.rail_from = 1;
  D.rail_reads = 3;
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &b);
  print_rec("CH1 rails in 3 records", &b);
  for(i = 0; i < b.n; i++)
    T_EQ(b.r1[i], 3);
  T_EQ(b.bad, 0);
  D.rail[0] = 0;
  D.rail_from = 0;
  D.rail_reads = 0;

  //fw 0.2.91 (bench 13:14, item c): the post's signal on 0 .. +3 V (the pre-scan's 2.5 V/div, offset 0: codes about
  //129 .. 163, the level 128 under it). An untriggered capture is joined from two (the bench's time-out records); the
  //engine takes it again with the level at the record's own mean, then every capture triggers on CH1's mean
  {
    uint16 nc, nt, nl, no;

    dut_rc15(&D);
    D.lo = 0.1;
    D.hi = 3.1;
    D.splice_untrig = 1;
    bode_init(&eng, &HW);
    sweep_once(&c, 0, 3, &b);
    bode_capture_stats(&eng, &nc, &nt, &nl, &no);
    printf("  plan post 0..3 V: %u captures, %u on the time-out, %u leak retakes, %u other retakes, model %u untriggered\n",
           nc, nt, nl, no, (unsigned)D.untrig);
    print_rec("post 0..3 V", &b);
    for(i = 0; i < b.n; i++)
      T_EQ(b.r1[i], 3);
    T_EQ(b.bad, 0);
    T_TRUE(nt <= 1);
    T_TRUE(nc >= (uint16)b.n);
    sweep_once(&c, 0, 3, &x);
    bode_capture_stats(&eng, &nc, &nt, &nl, &no);
    T_EQ(nt, 0);
    T_EQ(x.reused, 1);
    T_EQ(x.switches, 0);
  }

  //fw 0.2.90 (bench 13:10, item a): the plan sweep's rows carry the first sweep's r1 / r2 point by point, on a divider
  //where CH1 and CH2 sit on different ranges (a swap of the channels' entries would show)
  dut_rc(&D);
  D.c = 0;
  D.r = 991;
  D.rl = 102;
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &a);
  print_rec("divider", &a);
  sweep_once(&c, 0, 3, &b);
  T_EQ(b.reused, 1);
  for(i = 0; i < a.n; i++)
  {
    T_EQ(b.r1[i], a.r1[i]);
    T_EQ(b.r2[i], a.r2[i]);
  }
  T_TRUE(a.r1[0] != a.r2[0]);

  //fw 0.2.90 (item c): AUTO SET's overview (100 Hz .. 10 MHz, 20 points, Fast) does not lend its plan to the final
  //sweep (500 Hz .. 100 kHz, 20 points, Normal); the final sweep's plan serves the RUN after it
  {
    bode_cfg ov = cfg(100, 10e6, 20, BODE_SETTLE_FAST, BODE_CH2_AUTO), fin = cfg(500, 100e3, 20, BODE_SETTLE_NORMAL,
                                                                                BODE_CH2_AUTO);

    dut_rc15(&D);
    bode_init(&eng, &HW);
    sweep_once(&ov, 0, 3, &x);
    sweep_once(&fin, 0, 3, &a);
    print_rec("AUTO SET final", &a);
    T_EQ(a.reused, 0);
    sweep_once(&fin, 0, 3, &b);
    print_rec("RUN after it", &b);
    T_EQ(b.reused, 1);
    T_EQ(b.switches, 0);
    T_TRUE(same_ranges(&a, &b));
    for(i = 0; i < b.n; i++)
      T_EQ(b.r1[i], 3);
  }
}

//fw 0.2.71: a cancelled AUTO SET gives the previous table back (bode_snap)
static void test_snap(void)
{
  static bode_snap snap;
  bode_cfg a = cfg(1000, 100e3, 10, BODE_SETTLE_FAST, BODE_CH2_AUTO), b = cfg(100, 10e6, 20, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  bode_point p0, p1;
  int i, same = 1;

  dut_rc(&D);
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &a, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  bode_snap_save(&eng, &snap);
  bode_point_get(&eng, 3, &p0);

  //The overview runs and is cancelled part way
  T_EQ(bode_start(&eng, &b, 0, 3), 0);
  while(bode_done_count(&eng) < 4)
  {
    bode_step(&eng);
    D.now++;
  }
  bode_snap_restore(&eng, &snap);
  T_EQ(bode_count(&eng), 20);              //not while running
  bode_stop(&eng, 1);
  bode_snap_restore(&eng, &snap);

  T_EQ(bode_state(&eng), BODE_ST_DONE);
  T_EQ(bode_count(&eng), 10);
  T_EQ(bode_done_count(&eng), 10);
  T_EQ(bode_last_cfg(&eng)->stop_mhz, a.stop_mhz);
  T_EQ(bode_current(&eng), -1);
  T_TRUE(bode_point_get(&eng, 3, &p1));
  same = (p0.h_mdb == p1.h_mdb) && (p0.h_mdeg == p1.h_mdeg) && (p0.flags == p1.flags);
  for(i = 0; i < 10; i++)
    same = same && (bode_plans(&eng)[i].m == snap.plans[i].m);
  T_TRUE(same);
}

static void test_thru(void)
{
  bode_cfg c = cfg(1000, 1e6, 20, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  double worst_raw = 0, worst_cal = 0;
  int i;

  //Thru: both probes on the input, CH2 2 % high
  dut_rc(&D);
  D.thru = 1;
  D.gain2 = 1.02;
  bode_init(&eng, &HW);
  T_EQ(bode_thru_state(&eng), BODE_THRU_NONE);
  T_EQ(bode_start(&eng, &c, 1, 3), 0);
  T_EQ(bode_thru_state(&eng), BODE_THRU_MEASURING);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  T_EQ(bode_thru_state(&eng), BODE_THRU_VALID);

  //The same plan on the thru again: raw +0.17 dB, calibrated 0
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);

  for(i = 0; i < bode_count(&eng); i++)
  {
    bode_point p;
    double raw;

    bode_thru_apply(&eng, 0);
    bode_point_get(&eng, i, &p);
    raw = fabs(p.h_mdb / 1000.0 - 20 * log10(1.02));
    bode_thru_apply(&eng, 1);
    bode_point_get(&eng, i, &p);
    if(raw > worst_raw) worst_raw = raw;
    if(fabs(p.h_mdb / 1000.0) > worst_cal) worst_cal = fabs(p.h_mdb / 1000.0);
    T_EQ(p.flags & (BODE_F_NO_CAL | BODE_F_CAL_RANGE), 0);
  }

  printf("  thru: raw within %.4f dB of +0.172, calibrated within %.4f dB of 0\n", worst_raw, worst_cal);
  T_TRUE(worst_raw < 0.02);
  T_TRUE(worst_cal < 0.02);

  //Another plan: the calibration is stale, its points do not match
  c.points = 21;
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  T_EQ(bode_thru_state(&eng), BODE_THRU_STALE);
  run(&eng, 600000);
  {
    bode_point p;
    int no = 0;

    for(i = 0; i < bode_count(&eng); i++)
      if(bode_point_get(&eng, i, &p) && (p.flags & BODE_F_NO_CAL))
        no++;
    T_TRUE(no > 0);
  }

  bode_thru_clear(&eng);
  T_EQ(bode_thru_state(&eng), BODE_THRU_NONE);
}

static void test_divider_floor(void)
{
  //991 / 102 Ohm divider (the bench's): flat -20.6 dB, 0 deg
  bode_cfg c = cfg(1000, 300e3, 10, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  double want = 20 * log10(102.0 / (991.0 + 102.0));
  int i;

  dut_rc(&D);
  D.r = 991; D.c = 0; D.rl = 102;
  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);

  for(i = 0; i < bode_count(&eng); i++)
  {
    bode_point p;

    bode_point_get(&eng, i, &p);
    T_TRUE(fabs(p.h_mdb / 1000.0 - want) < 0.05);
    T_TRUE(abs(p.h_mdeg) < 300);
  }

  printf("  divider: %d points within 0.05 dB of %.2f dB\n", bode_count(&eng), want);

  //Estimate: 50 points 100 Hz - 1 MHz in a few seconds (design 3.7: about 2 s on the device without the background
  //and ranging captures)
  c = cfg(100, 1e6, 50, BODE_SETTLE_NORMAL, BODE_CH2_AUTO);
  printf("  estimate 100 Hz - 1 MHz / 50: %u ms\n", bode_estimate_ms(&c));
  T_TRUE(bode_estimate_ms(&c) > 1000);
  T_TRUE(bode_estimate_ms(&c) < 6000);
  c = cfg(1000, 300e3, 30, BODE_SETTLE_NORMAL, BODE_CH2_AUTO);
  printf("  estimate 1 kHz - 300 kHz / 30: %u ms\n", bode_estimate_ms(&c));
  c = cfg(10, 100e3, 50, BODE_SETTLE_NORMAL, BODE_CH2_AUTO);
  printf("  estimate 10 Hz - 100 kHz / 50: %u ms\n", bode_estimate_ms(&c));
}


//fw 0.2.69: AUTO SET's span from an overview sweep (100 Hz .. 10 MHz, 20 points, Fast, CH2 Auto)
static int overview(uint64 *f, int32 *m, int32 *ph, uint16 *fl, uint8 *have)
{
  bode_cfg c = cfg(100, 1e7, 20, BODE_SETTLE_FAST, BODE_CH2_AUTO);
  int i, n;

  bode_init(&eng, &HW);
  T_EQ(bode_start(&eng, &c, 0, 3), 0);
  run(&eng, 600000);
  T_EQ(bode_state(&eng), BODE_ST_DONE);
  n = bode_count(&eng);
  printf("  overview: %d CH2 range switches\n", bode_switches(&eng));

  for(i = 0; i < n; i++)
  {
    bode_point p;

    have[i] = (uint8)bode_point_get(&eng, i, &p);
    f[i] = bode_plans(&eng)[i].f_mhz;
    m[i] = p.h_mdb;
    ph[i] = p.h_mdeg;
    fl[i] = p.flags;
  }

  return n;
}

//A synthetic table: n log-spaced points f0..f1 of a second-order band-pass (peak gain k, resonance fr, quality q), or a
//first-order high-pass (q = 0) with its corner at fr
static int synth(double f0, double f1, int n, double fr, double q, double k, uint64 *f, int32 *m, int32 *ph, uint16 *fl,
                 uint8 *have)
{
  int i;

  for(i = 0; i < n; i++)
  {
    double fi = f0 * pow(f1 / f0, (double)i / (n - 1)), re, im;

    if(q > 0)
    {
      //H = k (j x / q) / (1 - x^2 + j x / q), x = f / fr
      double x = fi / fr, dr = 1 - x * x, di = x / q, den = dr * dr + di * di;

      re = k * (di * di) / den;
      im = k * (di * dr) / den;
    }
    else
    {
      //H = j x / (1 + j x)
      double x = fi / fr, den = 1 + x * x;

      re = x * x / den;
      im = x / den;
    }

    f[i] = (uint64)(fi * 1000 + 0.5);
    m[i] = (int32)(10 * log10(re * re + im * im) * 1000);
    ph[i] = (int32)(atan2(im, re) * 180 / PI * 1000);
    fl[i] = 0;
    have[i] = 1;
  }

  return n;
}

//fw 0.2.84 (bode-ui.md 3.2, owner 2026-10-20 11:23): absurd points, the median reference, the confirmed peak, CH1 low
static void test_auto_span_absurd(void)
{
  static uint64 f[BODE_MAX_POINTS];
  static int32  m[BODE_MAX_POINTS], ph[BODE_MAX_POINTS], m0[BODE_MAX_POINTS];
  static uint16 fl[BODE_MAX_POINTS];
  static uint8  have[BODE_MAX_POINTS];
  bode_autospan a;
  int n, i, pk;

  //The owner's RC (1 kOhm / 15 nF): clean 1 kHz .. 200 kHz (steps 9..16)
  dut_rc(&D);
  D.r = 1000; D.c = 15e-9;
  n = overview(f, m, ph, fl, have);
  for(i = 0; i < n; i++)
    m0[i] = m[i];
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  T_EQ(a.absurd, 0);
  T_TRUE(a.ref_mdb > -1000);
  T_TRUE(a.ref_mdb < 1000);

  //A first point 6 dB low (a bad record, not absurd): the median of the three lowest gives the reference, same span
  m[0] = m0[0] - 6000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  T_TRUE(a.ref_mdb > -1000);
  T_EQ(a.start_i, 9);
  T_EQ(a.stop_i, 16);

  //A first point 35 dB low (the saturated first record): absurd where the reference is taken, inconsistent
  m[0] = m0[0] - 35000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_INCONSISTENT);
  m[0] = m0[0] + 30000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_INCONSISTENT);
  m[0] = m0[0];

  //One absurd point inside (30 dB up at point 15, 870 kHz): dropped, not a peak, the span as clean
  m[15] = m0[15] + 30000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  T_EQ(a.absurd, 1);
  T_EQ(a.q_x100, 0);
  T_EQ(a.coarse, 0);
  T_EQ(a.start_i, 9);
  T_EQ(a.stop_i, 16);

  //... and 30 dB down: the same
  m[15] = m0[15] - 30000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  T_EQ(a.absurd, 1);
  T_EQ(a.start_i, 9);
  T_EQ(a.stop_i, 16);

  //19 dB is not absurd (a real response can step that far): kept
  m[15] = m0[15] + 19000;
  bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  T_EQ(a.absurd, 0);

  //Two absurd points: inconsistent
  m[15] = m0[15] + 30000;
  m[17] = m0[17] - 30000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_INCONSISTENT);
  m[15] = m0[15];
  m[17] = m0[17];

  //CH1 under 20 codes on most good points: CH1 low; on a few: the span as clean
  for(i = 0; i < n; i++)
    fl[i] |= BODE_F_CH1_LOW;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_CH1_LOW);
  for(i = 3; i < n; i++)
    fl[i] &= (uint16)~BODE_F_CH1_LOW;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  T_EQ(a.start_i, 9);

  //A tank (Q 8.1 at 50.3 kHz) whose peak lacks a valid neighbour: not confirmed
  n = synth(100, 1e7, 20, 50300, 8.1, 0.87, f, m, ph, fl, have);
  for(pk = 0, i = 1; i < n; i++)
    if(m[i] > m[pk])
      pk = i;
  T_TRUE((pk > 0) && (pk < n - 1));
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  fl[pk + 1] = BODE_F_UNSETTLED;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_UNCONFIRMED);
  fl[pk + 1] = 0;
  have[pk - 1] = 0;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_UNCONFIRMED);
  have[pk - 1] = 1;

  //A lone spike 30 dB above a flat divider (an absurd point that would read as a sharp tank): dropped, flat, no pass
  dut_rc(&D);
  D.r = 991; D.c = 0; D.rl = 102;
  n = overview(f, m, ph, fl, have);
  m[10] += 30000;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_FLAT);
  T_EQ(a.coarse, 0);
}

//AUTO SET's passes on a synthetic tank: the overview, then bode_auto_zoom's passes until the span can be set
static void auto_passes(double fr, double q, bode_autospan *a, int *passes)
{
  int32 prev_ref = 0;
  static uint64 f[BODE_MAX_POINTS];
  static int32  m[BODE_MAX_POINTS], ph[BODE_MAX_POINTS];
  static uint16 fl[BODE_MAX_POINTS];
  static uint8  have[BODE_MAX_POINTS];
  bode_cfg zc;
  double lo = 100, hi = 1e7;
  int n;

  for(*passes = 1; ; (*passes)++)
  {
    n = synth(lo, hi, 20, fr, q, 1.0, f, m, ph, fl, have);
    T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, a), BODE_AS_SPAN);
    printf("  tank Q %.1f at %.0f Hz, pass %d (%.0f .. %.0f Hz): f0 %.0f Hz, Q %.2f, coarse %d, steps %d..%d, settle %d\n",
           q, fr, *passes, lo, hi, a->f0_mhz / 1000.0, a->q_x100 / 100.0, a->coarse, a->start_i, a->stop_i, a->settle);
    //fw 0.2.84: a finer pass never reads the peak more than 6 dB lower (bode_view's consistency check)
    if(*passes > 1)
      T_TRUE(a->ref_mdb >= prev_ref - 6000);
    prev_ref = a->ref_mdb;
    if(!bode_auto_zoom(a, *passes, &zc))
      break;
    T_EQ(zc.points, 20);
    T_EQ(zc.settle, (*passes == 1) ? BODE_SETTLE_RESONANT : BODE_SETTLE_HIGHQ);
    lo = zc.start_mhz / 1000.0;
    hi = zc.stop_mhz / 1000.0;
  }
}

static void test_auto_span(void)
{
  static uint64 f[BODE_MAX_POINTS];
  static int32  m[BODE_MAX_POINTS], ph[BODE_MAX_POINTS];
  static uint16 fl[BODE_MAX_POINTS];
  static uint8  have[BODE_MAX_POINTS];
  bode_autospan a;
  bode_cfg zc;
  int n, i, r;

  //The steps: 1 Hz, 10 Hz, 5 MHz, 10 MHz
  T_EQ(bode_step_mhz(0), 1000ull);
  T_EQ(bode_step_mhz(BODE_STOP_MIN), 10000ull);
  T_EQ(bode_step_mhz(BODE_START_MAX), 5000000000ull);
  T_EQ(bode_step_mhz(BODE_STEPS - 1), 10000000000ull);

  //RC low-pass (1600 Ohm / 10 nF, fc 9.95 kHz) through the engine: 500 Hz .. 100 kHz, Settle Normal
  dut_rc(&D);
  n = overview(f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  printf("  auto span RC: %d, fc %.0f Hz, steps %d..%d\n", r, a.f0_mhz / 1000.0, a.start_i, a.stop_i);
  T_EQ(r, BODE_AS_SPAN);
  T_TRUE(fabs(a.f0_mhz / 1000.0 / 9947.0 - 1) < 0.1);
  T_EQ(a.start_i, 8);
  T_EQ(a.stop_i, 15);
  T_EQ(a.settle, BODE_SETTLE_NORMAL);

  //The owner's RC (1 kOhm / 15 nF, fc 10.6 kHz): 1 kHz .. 200 kHz
  dut_rc(&D);
  D.r = 1000; D.c = 15e-9;
  n = overview(f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  printf("  auto span RC 1k / 15n: %d, fc %.0f Hz, steps %d..%d\n", r, a.f0_mhz / 1000.0, a.start_i, a.stop_i);
  T_EQ(r, BODE_AS_SPAN);
  T_EQ(a.start_i, 9);
  T_EQ(a.stop_i, 16);

  //The bench divider: flat, the span kept
  dut_rc(&D);
  D.r = 991; D.c = 0; D.rl = 102;
  n = overview(f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  T_EQ(r, BODE_AS_FLAT);
  T_EQ(a.start_i, 6);
  T_EQ(a.stop_i, 18);

  //A tank, Q 8.1 at 50.3 kHz, on the overview's 20 points: resonant, about a half decade either side
  n = synth(100, 1e7, 20, 50300, 8.1, 0.87, f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  printf("  auto span tank Q 8.1: %d, f0 %.0f Hz, Q %.2f, steps %d..%d, settle %d\n", r, a.f0_mhz / 1000.0,
         a.q_x100 / 100.0, a.start_i, a.stop_i, a.settle);
  T_EQ(r, BODE_AS_SPAN);
  T_TRUE(fabs(a.f0_mhz / 1000.0 / 50300.0 - 1) < 0.15);
  T_TRUE(bode_step_mhz(a.start_i) <= a.f0_mhz / 3);
  T_TRUE(bode_step_mhz(a.stop_i) >= a.f0_mhz * 3);
  T_EQ(a.settle, BODE_SETTLE_RESONANT);

  //Q 50 on a dense table (1.6 % apart): read as Q 36 (the -3 dB crossings by linear interpolation), f0 (1 -+ 4 / Q) =
  //44.5 .. 55.5 kHz -> 20 kHz .. 100 kHz, Settle High Q
  n = synth(10e3, 250e3, 200, 50000, 50, 1.0, f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  printf("  auto span tank Q 50: %d, f0 %.0f Hz, Q %.2f, steps %d..%d, settle %d\n", r, a.f0_mhz / 1000.0,
         a.q_x100 / 100.0, a.start_i, a.stop_i, a.settle);
  T_EQ(r, BODE_AS_SPAN);
  T_TRUE(a.q_x100 > 3400);
  T_EQ(a.start_i, 13);
  T_EQ(a.stop_i, 15);
  T_EQ(a.settle, BODE_SETTLE_HIGHQ);

  //fw 0.2.71, the sharp-tank rule (AUTO SET's passes): Q 50 at 15.3 kHz is resolved on pass 3 (1 % apart), then the
  //design's rule f0 (1 -+ 4 / Q) on the steps and Settle High Q; Q 8.1 on pass 2 or 3 with Settle Resonant; Q 120 on
  //pass 4
  auto_passes(15300, 50, &a, &r);
  T_TRUE(r >= 3);
  T_EQ(a.coarse, 0);
  T_TRUE(fabs(a.f0_mhz / 1000.0 / 15300.0 - 1) < 0.01);
  T_TRUE(fabs(a.q_x100 / 100.0 / 50 - 1) < 0.2);
  T_EQ(a.settle, BODE_SETTLE_HIGHQ);
  T_TRUE(bode_step_mhz(a.start_i) <= a.f0_mhz * (1 - 4.0 / 50));
  T_TRUE(bode_step_mhz(a.stop_i) >= a.f0_mhz * (1 + 4.0 / 50));
  T_EQ(a.start_i, 12);                    //10 kHz .. 20 kHz
  T_EQ(a.stop_i, 13);

  auto_passes(50300, 8.1, &a, &r);
  T_EQ(a.coarse, 0);
  T_TRUE(fabs(a.q_x100 / 100.0 / 8.1 - 1) < 0.15);
  T_EQ(a.settle, BODE_SETTLE_RESONANT);

  auto_passes(15300, 120, &a, &r);
  T_EQ(r, 4);
  T_EQ(a.coarse, 0);
  T_TRUE(fabs(a.q_x100 / 100.0 / 120 - 1) < 0.2);
  T_EQ(a.settle, BODE_SETTLE_HIGHQ);

  //A resolved band needs no pass; the passes stop at BODE_AUTO_PASSES
  a.coarse = 0;
  a.f0_mhz = 15300000;
  T_EQ(bode_auto_zoom(&a, 1, &zc), 0);
  a.coarse = 1;
  T_EQ(bode_auto_zoom(&a, BODE_AUTO_PASSES, &zc), 0);
  T_EQ(bode_auto_zoom(&a, 3, &zc), 1);
  T_EQ(zc.start_mhz, 15300000ull * 1000 / 1030);

  //Q 20: Settle Resonant
  n = synth(10e3, 250e3, 200, 50000, 20, 1.0, f, m, ph, fl, have);
  bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  T_EQ(a.settle, BODE_SETTLE_RESONANT);

  //A high-pass with its corner at 30 Hz: still rising at 100 Hz, Start 10 Hz
  n = synth(100, 1e7, 20, 30, 0, 1.0, f, m, ph, fl, have);
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  T_EQ(r, BODE_AS_SPAN);
  T_EQ(a.start_i, BODE_STOP_MIN);
  T_TRUE(a.stop_i > a.start_i);

  //Nothing but the floor: no response, the span kept
  n = synth(100, 1e7, 20, 50300, 8.1, 0.87, f, m, ph, fl, have);
  for(i = 0; i < n; i++)
    fl[i] = (i & 1) ? BODE_F_NEAR_FLOOR : BODE_F_LOW_SNR;
  r = bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a);
  T_EQ(r, BODE_AS_NO_RESPONSE);
  T_EQ(a.start_i, 6);
  T_EQ(a.stop_i, 18);

  //No point measured at all: the same
  for(i = 0; i < n; i++)
    have[i] = 0;
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_NO_RESPONSE);
  test_auto_span_absurd();
}

//fw 0.2.93 (owner 2026-10-20 13:33, bode-ui.md 3.2 step 3): AUTO SET's CH2 range mode. Hold on one range when the CH2
//levels in the chosen span spread less than 20 dB, at the most sensitive range holding the largest with 1.5 x margin
static void tbl_from_eng(uint64 *f, int32 *v2, uint16 *fl, uint8 *have, int *n)
{
  int i;

  *n = bode_count(&eng);
  for(i = 0; i < *n; i++)
  {
    bode_point p;

    have[i] = (uint8)bode_point_get(&eng, i, &p);
    f[i]  = bode_plans(&eng)[i].f_mhz;
    v2[i] = p.v2_mdb;
    fl[i] = have[i] ? p.flags : 0;
  }
}

static void test_auto_ch2(void)
{
  static uint64 f[BODE_MAX_POINTS], ft[BODE_MAX_POINTS];
  static int32  m[BODE_MAX_POINTS], ph[BODE_MAX_POINTS], v2[BODE_MAX_POINTS];
  static uint16 fl[BODE_MAX_POINTS], flt[BODE_MAX_POINTS];
  static uint8  have[BODE_MAX_POINTS];
  int32  fs[6];
  bode_ac2 st;
  uint8  hr = 0xFF;
  bode_autospan a;
  bode_cfg c;
  sweep_rec x, y;
  double f0;
  int    n, i, r;

  //The rule on a synthetic table (fs: 128 codes on the model's ranges, x1 probe)
  bode_init(&eng, &HW);
  bode_ch2_full_scale(&eng, 1, fs);
  for(r = 0; r < 6; r++)
    T_TRUE(abs(fs[r] - (int32)(20000 * log10(128 * vpl(r)))) < 5);

  for(i = 0; i < 10; i++)
  {
    ft[i] = (uint64)(1000 + 100 * i) * 1000;
    v2[i] = -10000 - 2000 * i;            //-10 .. -28 dBV: 18 dB
    have[i] = 1;
  }
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, 0), BODE_CH2_HOLD);
  //0.316 V x 1.5 = 0.47 V: 0.2 V/div holds 0.88 V, 0.1 V/div 0.44 V (too small)
  T_EQ(hr, 4);
  v2[9] = -30000;                         //20 dB: Auto
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, 0), BODE_CH2_AUTO);
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[8], fs, &hr, 0), BODE_CH2_HOLD);   //that point outside the span
  have[3] = 0;
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[8], ft[0], fs, &hr, 0), BODE_CH2_HOLD);   //either order, a gap
  v2[2] = BODE_MDB_FLOOR;                 //no tone: Auto
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[8], fs, &hr, 0), BODE_CH2_AUTO);
  v2[2] = -14000;
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[0], fs, &hr, 0), BODE_CH2_AUTO);   //one point: Auto
  for(i = 0; i < 10; i++)
    v2[i] = 30000;                        //31.6 V: no range holds it with the margin
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, 0), BODE_CH2_AUTO);

  //The owner's RC (1 kOhm / 15 nF): AUTO SET's overview and span spread more than 20 dB: Auto; RUN at that span then
  //switches only where the plan says (planned), never unplanned
  dut_rc15(&D);
  n = overview(f, m, ph, fl, have);
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 6, 18, &a), BODE_AS_SPAN);
  tbl_from_eng(f, v2, fl, have, &n);
  T_EQ(bode_auto_ch2(f, v2, fl, have, n, bode_step_mhz(a.start_i), bode_step_mhz(a.stop_i), fs, &hr, 0), BODE_CH2_AUTO);
  c = cfg(bode_step_mhz(a.start_i) / 1000.0, bode_step_mhz(a.stop_i) / 1000.0, 50, a.settle, BODE_CH2_AUTO);
  sweep_once(&c, 0, 3, &x);
  print_rec("RC AUTO SET final", &x);
  sweep_once(&c, 0, 3, &y);
  print_rec("RC RUN after it", &y);
  T_EQ(x.reused, 0);
  T_EQ(y.reused, 1);
  T_EQ(y.switches, 0);
  T_TRUE(y.planned >= 1);
  T_EQ(y.planned, transitions(&x));

  //A parallel tank (1 mH || 100 nF, 10 kOhm load, fed through 2 kOhm: f0 15.9 kHz, Q about 17), a narrow span around
  //its peak (f0 / 1.1 .. f0 x 1.1, the levels within about 10 dB): Hold, on 1 V/div (the peak's fundamental about 1.6 V
  //x 1.5 = 2.5 V: 0.5 V/div holds 2.2 V, 1 V/div 4.4 V)
  dut_rc(&D);
  D.r = 1000; D.c = 100e-9; D.l = 1e-3; D.rl = 10000;
  f0 = 1 / (2 * PI * sqrt(D.l * D.c));
  bode_init(&eng, &HW);
  c = cfg(f0 / 1.1, f0 * 1.1, 20, BODE_SETTLE_RESONANT, BODE_CH2_AUTO);
  sweep_once(&c, 0, 3, &x);
  print_rec("tank pass, Auto", &x);
  T_EQ(x.bad, 0);
  tbl_from_eng(f, v2, fl, have, &n);
  {
    int32 lo = v2[0], hi = v2[0];

    for(i = 1; i < n; i++)
    {
      if(v2[i] < lo) lo = v2[i];
      if(v2[i] > hi) hi = v2[i];
    }
    printf("  tank: CH2 %.1f .. %.1f dBV peak in the span (%.1f dB)\n", lo / 1000.0, hi / 1000.0, (hi - lo) / 1000.0);
    T_TRUE(hi - lo < BODE_HOLD_SPREAD_MDB);
  }
  hr = 0xFF;
  T_EQ(bode_auto_ch2(f, v2, fl, have, n, c.start_mhz, c.stop_mhz, fs, &hr, 0), BODE_CH2_HOLD);
  T_EQ(hr, 2);

  //The held sweeps: the final one (fresh) and RUN's (from the plan): no switch of either kind, nothing flagged
  c.ch2_mode = BODE_CH2_HOLD;
  sweep_once(&c, 0, hr, &x);
  print_rec("tank Hold final", &x);
  sweep_once(&c, 0, hr, &y);
  print_rec("tank Hold RUN", &y);
  T_EQ(x.switches, 0);
  T_EQ(x.planned, 0);
  T_EQ(y.reused, 1);
  T_EQ(y.switches, 0);
  T_EQ(y.planned, 0);
  T_EQ(x.bad + y.bad + x.small + y.small, 0);
  for(i = 0; i < y.n; i++)
    T_EQ(y.r2[i], 2);

  //fw 0.2.94: a bad record (clipped, incoherent, ...) is left out of the rule; a near-floor point counts
  for(i = 0; i < 10; i++)
  {
    v2[i] = -10000 - 1000 * i;
    have[i] = 1;
    flt[i] = 0;
  }
  v2[4] = -60000;
  flt[4] = BODE_F_INCOHERENT;
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, &st), BODE_CH2_HOLD);
  T_EQ(st.junk, 1);
  T_EQ(st.n, 9);
  T_EQ(st.reason, BODE_AC2_R_FITS);
  flt[4] = BODE_F_NEAR_FLOOR;
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, &st), BODE_CH2_AUTO);
  T_EQ(st.reason, BODE_AC2_R_SPREAD);
  T_EQ(st.spread_mdb, 50000);
  v2[4] = BODE_MDB_FLOOR;
  T_EQ(bode_auto_ch2(ft, v2, flt, have, 10, ft[0], ft[9], fs, &hr, &st), BODE_CH2_AUTO);
  T_EQ(st.reason, BODE_AC2_R_NO_TONE);

  //fw 0.2.94 (bench 14:19): both tips on the post (a thru), AUTO SET: no corner, Start / Stop kept (20 kHz .. 5 MHz):
  //the rule runs on the flat path too and gives Constant
  dut_rc(&D);
  D.thru = 1;
  n = overview(f, m, ph, fl, have);
  T_EQ(bode_auto_span(f, m, ph, fl, have, n, 13, 20, &a), BODE_AS_FLAT);
  T_EQ(a.start_i, 13);
  T_EQ(a.stop_i, 20);
  tbl_from_eng(f, v2, fl, have, &n);
  hr = 0xFF;
  T_EQ(bode_auto_ch2(f, v2, fl, have, n, bode_step_mhz(a.start_i), bode_step_mhz(a.stop_i), fs, &hr, &st),
       BODE_CH2_HOLD);
  printf("  thru, flat: %d points in the span, %d left out, spread %.1f dB, largest %.1f dBV: Constant on range %d\n",
         st.n, st.junk, st.spread_mdb / 1000.0, st.vmax_mdb / 1000.0, hr);
  T_EQ(hr, 3);                            //the post through 1 kOhm: about 1.3 V peak x 1.5 fits 0.5 V/div
  T_TRUE(st.n >= 8);
}

//fw 0.2.94 (owner 2026-10-20 14:16): CH2 on Constant (Hold) is never written to another range during a sweep, fresh or
//from the plan, not in the pre-scan; a clip or a drop only flags the point
static void test_constant_never_switches(void)
{
  bode_cfg c = cfg(100, 1e6, 25, BODE_SETTLE_NORMAL, BODE_CH2_HOLD);
  sweep_rec x;
  int i, clipped = 0, small = 0;

  //The RC on 0.5 V/div: the stopband drops under 3 codes (ch2_small), never a range change
  dut_rc15(&D);
  D.range[1] = 3;
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 3, &x);
  print_rec("Constant 0.5 V/div fresh", &x);
  T_EQ(D.r2_changes, 0);
  T_EQ(x.switches, 0);
  T_EQ(x.planned, 0);
  T_TRUE(x.small >= 1);
  sweep_once(&c, 0, 3, &x);
  print_rec("Constant 0.5 V/div plan", &x);
  T_EQ(x.reused, 1);
  T_EQ(D.r2_changes, 0);
  T_EQ(x.switches + x.planned, 0);
  for(i = 0; i < x.n; i++)
    T_EQ(x.r2[i], 3);

  //On 0.1 V/div the passband clips: flagged, no step up
  dut_rc15(&D);
  D.range[1] = 5;
  bode_init(&eng, &HW);
  sweep_once(&c, 0, 5, &x);
  print_rec("Constant 0.1 V/div", &x);
  T_EQ(D.r2_changes, 0);
  T_EQ(x.switches + x.planned, 0);
  for(i = 0; i < x.n; i++)
  {
    bode_point p;

    bode_point_get(&eng, i, &p);
    T_EQ(p.r2, 5);
    if(p.flags & BODE_F_CLIPPED)
      clipped++;
    if(p.flags & BODE_F_CH2_SMALL)
      small++;
  }
  T_TRUE(clipped >= 1);
  printf("  Constant 0.1 V/div: %d clipped, %d small, CH2 range writes that changed it: %u\n", clipped, small,
         (unsigned)D.r2_changes);
}

int main(void)
{
  test_rc_auto();
  test_hold();
  test_wiring_b();
  test_stop_and_steps();
  test_readout_fault();
  test_readout_check();
  test_flat_retake();
  test_range_plan();
  test_snap();
  test_thru();
  test_divider_floor();
  test_auto_span();
  test_auto_ch2();
  test_constant_never_switches();
  return t_report("test_bode_sweep");
}
