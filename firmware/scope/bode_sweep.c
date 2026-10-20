//----------------------------------------------------------------------------------------------------------------------------------
// bode_sweep.c - the on-device Bode sweep engine (see bode_sweep.h). The structure follows the host tool's BodeRunner
// (mcp/src/fnirsi1013d/bode.py) step by step; each capture the host makes is one request here, and what the host does with
// its result is the handler of that request's phase. Host-tested: host_tests/test_bode_sweep.c (a DUT model behind bode_hw).
//----------------------------------------------------------------------------------------------------------------------------------

#include "bode_sweep.h"

//Quality gates (bode.py; mdB of code levels and of ratios). The measurement's own flags and their thresholds are in
//bode_dsp.c (bode_meas_flags, fw 0.2.87)
#define CH1_LOW_MDB         26021       //CH1_LOW_CODES 20 (the 0.2.71 readout rule)
#define NEAR_FLOOR_MDB      20000       //NEAR_FLOOR_DB 20
#define COLLAPSE_MDB        20000       //COLLAPSE_RATIO 0.1: a tone below 1/10 of the reference
#define CENTRE_TOL_C100     1200        //re-centre beyond 12 codes from 128
#define FULL_SCALE_C100     10880       //85 % of 128 codes
#define DOWNRANGE_C100      7680        //60 % of 128 codes
#define RELAY_MS            50
#define MAX_SWITCHES        6
#define FIRST_FACTOR        3
#define REAL_CLIP_MDB       30103       //fw 0.2.91: 32 codes peak (a quarter of full scale)
#define VERIFY_MAX          8           //captures of the pre-scan's check (4 up to 0.2.89, 6 in 0.2.90)
#define PLAN_FLOOR_C100     3840        //fw 0.2.87: 30 % of 128 codes, the range plan's floor (bode_sweep.h)
#define PLAN_CH1_FMAX_MHZ   2000000000ull   //fw 0.2.90: ch1_low below 2 MHz (above, the x1 probe rolls off) drops a plan

//Nominal V/div per hardware range in mV (the host's RANGE_VOLTS_PER_DIV ratios)
static const uint16 vpd_mv[6] = { 5000, 2500, 1000, 500, 200, 100 };

static const uint16 settle_periods[BODE_SETTLE_COUNT] = { 3, 10, 50, 200 };

//Request phases: what the result of the capture in flight is for
#define PH_NONE             0
#define PH_PRESCAN          1           //first frequency, both channels on range 1
#define PH_VERIFY           2           //the ranges chosen, checked (up to four captures)
#define PH_BG               3           //generator off, the background at this rate and CH2 range
#define PH_POINT            4
#define PH_RECENTRE         5           //again after moving an offset
#define PH_RETRY            6           //again with twice the settle time (the halves disagreed)
#define PH_UP_PREV          7           //CH2 one range up: the previous point again, for the splice
#define PH_UP_POINT         8           //CH2 one range up: this point again
#define PH_DOWN             9           //CH2 one range down: this point again, for the splice
#define PH_CLIP_AGAIN       10          //fw 0.2.89: a planned point clipped: once more on the same range

#define M_IDLE              0
#define M_SETTLE            1
#define M_ARMED             2

static uint32 now_ms(bode_eng *e)
{
  return e->hw->ms(e->hw->ctx);
}

static int after(uint32 now, uint32 t)
{
  return (int32)(now - t) >= 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Measurement helpers (the host's Measurement properties)

static int flat2(const bode_meas *m, int collapsed)
{
  return bode_flat2(m, collapsed);
}

//CH2's tone vanished on the new range at the same frequency: ref measured it clearly on the previous one
static int collapsed(const bode_meas *ref, const bode_meas *now)
{
  return (ref->c[1].code_mdb >= 0) && !ref->c[1].clipped && (now->c[1].lvl_mdb < ref->c[1].lvl_mdb - COLLAPSE_MDB);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Hardware steps

//A front-end write's settling (RELAY_SETTLE_S): fw 0.2.87 the waits add up as the host's sleeps do (bode.py _set_range
//sleeps after the offset write and again at its end: 100 ms after a range change with the offset centred, 50 ms after
//an offset move). Up to 0.2.86 they overlapped into one 50 ms; the bench sweep of 2026-10-20 12:05 flagged unsettled /
//incoherent exactly on the points after a CH2 range change, where the host, waiting longer, flagged none (inference:
//the offset PWM's filter still settling in the record)
static void relay_wait(bode_eng *e)
{
  uint32 now = now_ms(e);

  if(e->relay_pending && !after(now, e->relay_until))
    e->relay_until += RELAY_MS;
  else
    e->relay_until = now + RELAY_MS;

  e->relay_pending = 1;
}

static void set_channel(bode_eng *e, int c, int r, int32 off)
{
  e->hw->channel(e->hw->ctx, c, r, off);
  e->range[c]    = (uint8)r;
  e->dcoffset[c] = off;
}

//Move the trace down by codes_c100 / 100 codes (positive: the record's mean sits that far above 128): dcoffset +=
//codes x dc_shift_value / 100, clamped to the PWM's 0..2000. 0 when the offset had no room (noted once per channel).
static int shift_offset(bode_eng *e, int c, int32 codes_c100)
{
  bode_chinfo ci;
  int32 want, lo, hi, d;
  int   ok;

  e->hw->chan_info(e->hw->ctx, c, e->range[c], &ci);

  want = e->dcoffset[c] + (int32)(((int64)codes_c100 * ci.dc_shift_value + ((codes_c100 >= 0) ? 5000 : -5000)) / 10000);
  lo   = -ci.cal_offset;
  hi   = 2000 - ci.cal_offset;
  d    = (want < lo) ? lo : (want > hi) ? hi : want;
  ok   = (d == want);

  if(d != e->dcoffset[c])
  {
    set_channel(e, c, e->range[c], d);
    relay_wait(e);
  }

  if(!ok)
    e->exhausted |= (uint8)(1 << c);

  return ok;
}

//Range r with the offset centred on a DC level of v_uv microvolts at the BNC (host _set_range: offset 0, then centre;
//the offset write's wait, then RELAY_SETTLE_S at the end)
static void set_range(bode_eng *e, int c, int r, int32 v_uv)
{
  bode_chinfo ci;

  set_channel(e, c, r, 0);
  e->hw->chan_info(e->hw->ctx, c, r, &ci);

  if(ci.uv_q8)
    shift_offset(e, c, (int32)(((int64)v_uv * 25600) / (int64)ci.uv_q8));

  relay_wait(e);
}

static void gen_set(bode_eng *e, const bode_plan *pp, int on)
{
  e->hw->gen(e->hw->ctx, pp ? pp->phase_inc : 0, on);

  if((on != 0) != (e->gen_on != 0))
    e->disturbed = 1;

  e->gen_on = (uint8)(on != 0);
}

//Periods x 10 for the next point: three times the preset after a disturbance (start, generator toggled)
static uint32 point_settle(bode_eng *e)
{
  uint32 k = e->disturbed ? FIRST_FACTOR : 1;

  e->disturbed = 0;
  return settle_periods[e->cfg.settle] * k * 10;
}

//fw 0.2.91: the trigger level for the next capture, CH1's expected record mean in codes: the code of 0 V on CH1's range
//and offset plus CH1's DC level as last measured (0 V before the first measurement)
static uint8 trig_code(bode_eng *e)
{
  bode_chinfo ci;
  int32 code_c100;

  if(e->range[0] > 5)
    return 128;

  e->hw->chan_info(e->hw->ctx, 0, e->range[0], &ci);
  code_c100 = 12800 - (int32)(((int64)e->dcoffset[0] * 10000) / ci.dc_shift_value);

  if(e->have_v1 && ci.uv_q8)
    code_c100 += (int32)(((int64)e->v1_uv * 25600) / (int64)ci.uv_q8);

  code_c100 = (code_c100 + 50) / 100;
  return (uint8)((code_c100 < 2) ? 2 : (code_c100 > 253) ? 253 : code_c100);
}

static void request(bode_eng *e, int phase, const bode_plan *pp, uint32 settle_ms, uint32 total, int bg)
{
  uint32 now = now_ms(e);
  uint32 start = (e->relay_pending && !after(now, e->relay_until)) ? e->relay_until : now;

  e->relay_pending = 0;
  e->msettle  = settle_ms;
  e->phase    = (uint8)phase;
  e->mp       = pp;
  e->mtotal   = total;
  e->mbg      = (uint8)bg;
  e->deadline = start + settle_ms;
  e->mstate   = M_SETTLE;
}

static void measure(bode_eng *e, int phase, const bode_plan *pp, uint32 periods_x10)
{
  request(e, phase, pp, bode_settle_ms(pp, periods_x10), bode_total(pp), 0);
}

static void finish(bode_eng *e, uint8 state, uint8 err)
{
  if(e->gen_on)
    gen_set(e, 0, 0);

  e->state  = state;
  e->err    = err;
  e->mstate = M_IDLE;
  e->phase  = PH_NONE;
  e->idx    = -1;
  e->t_end  = now_ms(e);

  //A completed thru sweep becomes the calibration
  if(e->thru_sweep && (state == BODE_ST_DONE))
  {
    bode_thru *t = &e->thru;
    int i;

    t->count     = (uint16)e->np;
    t->start_mhz = e->cfg.start_mhz;
    t->stop_mhz  = e->cfg.stop_mhz;
    t->points    = e->cfg.points;

    for(i = 0; i < e->np; i++)
    {
      t->m[i]      = e->plans[i].m;
      t->n[i]      = e->plans[i].n;
      t->h_mdb[i]  = e->pts[i].h_mdb;
      t->h_mdeg[i] = e->pts[i].h_mdeg;
      t->r1[i]     = e->pts[i].r1;
      t->r2[i]     = e->pts[i].r2;
      t->flags[i]  = e->pts[i].flags;
    }

    t->measured = 1;
    t->applied  = 1;
  }

  e->thru_sweep = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The sweep

static void point_begin(bode_eng *e);

//Re-centre a channel whose mean is far from code 128 (host _recentre_if_needed). 1 when something moved.
static int recentre(bode_eng *e, const bode_meas *m)
{
  int c, moved = 0;

  for(c = 0; c < 2; c++)
  {
    int32 d = m->c[c].centre_c100 - 12800;

    if(((d > CENTRE_TOL_C100) || (d < -CENTRE_TOL_C100)) && !(e->exhausted & (1 << c)))
      if(shift_offset(e, c, d))
        moved = 1;
  }

  return moved;
}

//The point's result from a measurement (host _result)
static void result(bode_eng *e, int i, const bode_meas *m, int coll, int retries)
{
  bode_point *p = &e->pts[i];
  const bode_plan *pp = &e->plans[i];
  int32 bg = e->bg_mdb[i][e->range[1] % 6];
  uint16 f = bode_meas_flags(m, coll);

  if((pp->n >= 16) && !(pp->n & 3) && e->hw->has_fir && !e->last_fir)
                                              f |= BODE_F_FIR_OFF;
  if((bg > BODE_MDB_FLOOR) && (m->c[1].lvl_mdb < bg + NEAR_FLOOR_MDB))
                                              f |= BODE_F_NEAR_FLOOR;
  if(retries)                                 f |= BODE_F_RETRIED;

  p->h_mdb    = m->h_mdb + e->k_mdb;
  p->h_mdeg   = bode_wrap_mdeg(m->h_mdeg + e->k_mdeg);
  p->v1_mdb   = m->c[0].lvl_mdb;
  p->v2_mdb   = m->c[1].lvl_mdb + e->k_mdb;
  p->snr2_mdb = m->snr2_mdb;
  p->leak_mdb = m->leak_mdb;
  p->flags    = f;
  p->r1       = e->range[0];
  p->r2       = e->range[1];
  p->retries  = (uint8)retries;
  p->done     = 1;

  //The range plan's entry (fw 0.2.87): the ranges, offsets and splice factor of this result. CH1 clipped or dead, or the
  //first point's CH1 fitting a finer range under 60 % of full scale (a fooled pre-scan): the plan is not kept
  if(e->rp_set && !e->thru_sweep)
  {
    bode_rplan *rp = &e->rplan[i];
    int r1 = e->range[0];

    if(m->c[0].clipped || bode_ch1_dead(m) ||
       ((i == 0) && (r1 < 5) && ((int64)m->c[0].ac_c100 * vpd_mv[r1] < (int64)DOWNRANGE_C100 * vpd_mv[r1 + 1])) ||
       ((m->c[0].code_mdb < CH1_LOW_MDB) && (r1 < 5) && (e->plans[i].f_mhz < PLAN_CH1_FMAX_MHZ)))
      e->rp_set = 0;

    rp->have   = 1;
    rp->r[0]   = e->range[0];
    rp->r[1]   = e->range[1];
    rp->off[0] = (int16)e->dcoffset[0];
    rp->off[1] = (int16)e->dcoffset[1];
    rp->k_mdb  = e->k_mdb;
    rp->k_mdeg = e->k_mdeg;
  }
}

static void splice(bode_eng *e, int32 old_mdb, int32 old_mdeg, const bode_meas *again)
{
  e->k_mdb  = old_mdb - again->h_mdb;
  e->k_mdeg = bode_wrap_mdeg(old_mdeg - again->h_mdeg);
}

static void next_point(bode_eng *e)
{
  e->prev = e->idx;
  e->idx++;

  if(e->stop_req || (e->idx >= e->np))
  {
    finish(e, e->stop_req ? BODE_ST_STOPPED : BODE_ST_DONE, e->stop_req ? BODE_ERR_STOP : BODE_ERR_NONE);
    return;
  }

  point_begin(e);
}

//CH2 one range up after a clip or a saturated record (host _auto_range, first branch); `from` is the measurement that
//failed (the point's, or the last try)
static void up_step(bode_eng *e, const bode_meas *from, int coll)
{
  int r = e->range[1];      //> 0 and within the switch budget (the callers check)

  if(flat2(from, coll))
  {
    e->bad_down |= (uint8)(1 << r);       //a saturated range is not tried again on the way down
    if(e->rp_set && !e->thru_sweep)
      e->rp_bad_down |= (uint8)(1 << r);  //fw 0.2.91: nor by the plan's later sweeps
  }

  set_range(e, 1, r - 1, flat2(from, coll) ? e->v_dc2 : from->c[1].mean_uv);
  e->switches++;
  e->up_r = r;
  e->clip_again = 0;

  if(e->prev >= 0)
  {
    gen_set(e, &e->plans[e->prev], 1);
    measure(e, PH_UP_PREV, &e->plans[e->prev], point_settle(e));
  }
  else
  {
    gen_set(e, &e->plans[e->idx], 1);
    measure(e, PH_UP_POINT, &e->plans[e->idx], point_settle(e));
  }
}

//After the point's result: CH2 ranging (host _auto_range)
static void auto_range(bode_eng *e, const bode_meas *m, int coll)
{
  int r = e->range[1];

  //STOP: the point keeps this result, no range change follows; nor on a record without a stimulus (host _auto_range)
  if((e->cfg.ch2_mode != BODE_CH2_AUTO) || e->stop_req || bode_ch1_dead(m))
  {
    next_point(e);
    return;
  }

  if(m->c[1].clipped || flat2(m, coll))
  {
    //fw 0.2.91: a step up needs a second record on the same range that clips (or is saturated) too; one such record is
    //a bad record, measured again (0.2.89: from the plan only; bench 13:14 still had fresh sweeps stepping up on one)
    if(!e->clip_again)
    {
      e->clip_again = 1;
      e->retries++;
      measure(e, PH_CLIP_AGAIN, &e->plans[e->idx], settle_periods[e->cfg.settle] * 10);
      return;
    }

    if((r > 0) && (e->switches < MAX_SWITCHES))
    {
      up_step(e, m, coll);
      return;
    }

    next_point(e);
    return;
  }

  //Down-range when the predicted peak on the next range stays under 60 % of full scale; from the range plan only under
  //its floor, 30 % (fw 0.2.87)
  if((r >= 5) || (e->switches >= MAX_SWITCHES) || (e->bad_down & (1 << (r + 1))) ||
     ((int64)m->c[1].ac_c100 * vpd_mv[r] > (int64)(e->reuse ? PLAN_FLOOR_C100 : DOWNRANGE_C100) * vpd_mv[r + 1]))
  {
    next_point(e);
    return;
  }

  set_range(e, 1, r + 1, m->c[1].mean_uv);
  e->switches++;
  e->up_r = r;
  measure(e, PH_DOWN, &e->plans[e->idx], point_settle(e));
}

//The point's measurement is final (after the re-centring and settle retries)
static void point_final(bode_eng *e, const bode_meas *m, int coll)
{
  e->ms      = *m;
  e->ms_coll = (uint8)coll;
  result(e, e->idx, m, coll, e->retries);
  auto_range(e, m, coll);
}

//The pre-scan's range-1 measurement at the first frequency against a measurement there on another CH2 range (host
//_ref_check). Returns 1 for a collapsed tone; sets the overload error for a held range.
static int ref_check(bode_eng *e, const bode_plan *pp, const bode_meas *m, int *stop)
{
  int coll = 0;

  *stop = 0;

  if(e->have_ref2 && (pp->m == e->plans[0].m) && (e->range[1] != 1) && (e->cfg.ch2_mode == BODE_CH2_AUTO))
    coll = collapsed(&e->ref2, m);

  if(flat2(m, coll) && (e->cfg.ch2_mode == BODE_CH2_HOLD))
  {
    finish(e, BODE_ST_ERROR, BODE_ERR_OVERLOAD);
    *stop = 1;
  }

  return coll;
}

static int bg_has(bode_eng *e, uint32 key)
{
  int i;

  for(i = 0; i < e->nbg; i++)
    if(e->bg_done[i] == key)
      return 1;

  return 0;
}

//The point's planned ranges, offsets and splice factor (fw 0.2.87); the front end's waits as set_range's. fw 0.2.93: a
//CH2 range change between two points of the sweep is a planned switch (the first point's write is not: it only moves
//the channel from wherever the last sweep left it)
static void plan_apply(bode_eng *e, int i)
{
  const bode_rplan *rp = &e->rplan[i];
  int c;

  for(c = 0; c < 2; c++)
  {
    if(rp->r[c] != e->range[c])
    {
      if((c == 1) && (i > 0))
        e->planned++;

      set_channel(e, c, rp->r[c], rp->off[c]);
      relay_wait(e);
      relay_wait(e);
    }
    else if(rp->off[c] != e->dcoffset[c])
    {
      set_channel(e, c, rp->r[c], rp->off[c]);
      relay_wait(e);
    }
  }

  e->k_mdb  = rp->k_mdb;
  e->k_mdeg = rp->k_mdeg;
}

static void point_begin(bode_eng *e)
{
  const bode_plan *pp = &e->plans[e->idx];
  uint32 key;

  if(e->reuse)
    plan_apply(e, e->idx);

  key = (pp->n << 3) | e->range[1];
  e->retries = 0;
  e->clip_again = 0;

  //Background: generator off, one capture per rate and CH2 range, long enough for every point at this rate
  if(e->cfg.background && !bg_has(e, key) && (e->nbg < (int)(sizeof(e->bg_done) / sizeof(e->bg_done[0]))))
  {
    uint32 total = 0;
    int    i;

    for(i = 0; i < e->np; i++)
      if((e->plans[i].n == pp->n) && (bode_total(&e->plans[i]) > total))
        total = bode_total(&e->plans[i]);

    e->bg_done[e->nbg++] = key;
    gen_set(e, 0, 0);
    request(e, PH_BG, pp, 1, total, 1);
    return;
  }

  gen_set(e, pp, 1);
  measure(e, PH_POINT, pp, point_settle(e));
}

//The ranges from the pre-scan capture on range 1 (host _prescan)
static void on_prescan(bode_eng *e, const bode_meas *m)
{
  int    c, t[2];
  const bode_plan *pp = &e->plans[0];

  if(e->stop_req)
  {
    finish(e, BODE_ST_STOPPED, BODE_ERR_STOP);
    return;
  }

  e->ref2      = *m;
  e->have_ref2 = 1;
  e->v_dc[0]   = m->c[0].mean_uv;
  e->v_dc[1]   = m->c[1].mean_uv;
  e->v_dc2     = m->c[1].mean_uv;
  e->disturbed = 0;

  for(c = 0; c < 2; c++)
  {
    int32 ac = (m->c[c].ac_c100 > 100) ? m->c[c].ac_c100 : 100;   //at least one code
    int   r = 1, cand;

    if((c == 1) && (e->cfg.ch2_mode == BODE_CH2_HOLD))
      r = e->hold_r2;
    else if((c == 0) && bode_ch1_dead(m))
      r = 1;            //nothing to range on (flat even after the retake, or no tone): host _prescan
    else
      for(cand = 1; cand < 6; cand++)
        if((int64)ac * vpd_mv[1] <= (int64)FULL_SCALE_C100 * vpd_mv[cand])
          r = cand;

    t[c] = r;
  }

  for(c = 0; c < 2; c++)
    set_range(e, c, t[c], e->v_dc[c]);

  e->iter    = 0;
  e->vclip   = 0;
  e->vbad[0] = e->vbad[1] = 0;
  measure(e, PH_VERIFY, pp, settle_periods[e->cfg.settle] * 10);
}

//Without a capture self-check (BODE_CHK_NONE): range 0 (5 V/div) and the record still at a rail, the fundamental under
//CH1_LOW (20 codes); CH2 only when it is not a flat record (a DC overload has its own rule). With a self-check its
//verdict decides (bode_step), never the clip: a clean check with a small signal is ch1_low, not a fault (bench
//2026-10-20 10:52)
static int readout_fault(bode_eng *e, const bode_meas *m)
{
  int c;

  if(e->last_check != BODE_CHK_NONE)
    return 0;

  for(c = 0; c < 2; c++)
    if((e->range[c] == 0) && m->c[c].clipped && (m->c[c].code_mdb < CH1_LOW_MDB) && !((c == 1) && flat2(m, 0)))
      return 1;

  return 0;
}

//fw 0.2.90: the most sensitive range from r on where the record's peak (ac, measured on r) stays inside 85 % of full
//scale, stopping before a range that clipped in this check
static int finer_range(const bode_meas *m, int c, int r, uint8 bad)
{
  int32 ac = (m->c[c].ac_c100 > 100) ? m->c[c].ac_c100 : 100;
  int   t = r;

  while((t < 5) && !(bad & (1 << (t + 1))) && ((int64)ac * vpd_mv[r] <= (int64)FULL_SCALE_C100 * vpd_mv[t + 1]))
    t++;

  return t;
}

static void on_verify(bode_eng *e, const bode_meas *m)
{
  int hold = (e->cfg.ch2_mode == BODE_CH2_HOLD);
  int over = (hold || (e->range[1] != 1)) && flat2(m, hold ? 0 : collapsed(&e->ref2, m));
  int changed = 0, again = 0, c;

  e->iter++;

  //host _prescan: CH1 flat even after the retake: nothing to range on, keep the ranges
  if(bode_ch1_dead(m))
  {
    e->iter = VERIFY_MAX;
    over = 0;
  }

  if(over && (e->cfg.ch2_mode == BODE_CH2_HOLD))
  {
    finish(e, BODE_ST_ERROR, BODE_ERR_OVERLOAD);
    return;
  }

  //fw 0.2.71 (owner's fail-fast rule), bitstreams without the self-check: 5 V/div (the last range down) still clipping
  //with a fundamental under 20 codes cannot be the post's 3.1 Vpp (about 10 codes there): the readout returns rail
  //values. The sweep ends rather than producing a flagged table (bench 2026-10-19 20:51, FPGA 0x1539's A-core fault).
  if(readout_fault(e, m))
  {
    finish(e, BODE_ST_ERROR, BODE_ERR_READOUT);
    return;
  }

  //fw 0.2.90 (bench 2026-10-20 13:10: CH1 left on 2.5 V/div, CH2 on 2.5 V/div for a whole sweep): the check judges
  //each channel like a point does. A clip (CH2: or a saturated record) is measured again on the same range before
  //the channel steps up, and that range is not tried again in this check; an unclipped channel steps to the finest
  //range its peak fits (85 % of full scale), so one bad pre-scan record no longer leaves a channel coarse
  for(c = 0; (c < 2) && !bode_ch1_dead(m); c++)
  {
    int    cl = c ? (m->c[1].clipped || over) : m->c[0].clipped;
    uint8 *bad = &e->vbad[c];
    int    r = e->range[c], t;

    if((c == 1) && (e->cfg.ch2_mode == BODE_CH2_HOLD))
      continue;

    //fw 0.2.91: a record that clips because it sits off centre is re-centred, not stepped up
    if(cl && !over && (e->range[c] > 0) &&
       ((m->c[c].centre_c100 - 12800 > CENTRE_TOL_C100) || (m->c[c].centre_c100 - 12800 < -CENTRE_TOL_C100)) &&
       !(e->exhausted & (1 << c)) && ((int64)m->c[c].ac_c100 <= (int64)FULL_SCALE_C100))
    {
      if(shift_offset(e, c, m->c[c].centre_c100 - 12800))
      {
        changed = 1;
        continue;
      }
    }

    if(cl)
    {
      if(!(e->vclip & (1 << c)))
      {
        e->vclip |= (uint8)(1 << c);
        again = 1;
      }
      else if(r > 0)
      {
        //fw 0.2.91: a range is barred only by a clip with a signal of at least a quarter of full scale behind it (32
        //codes peak); a rail sample on a small tone, twice, still steps up but the range stays open for the way back
        if(over || (m->c[c].code_mdb >= REAL_CLIP_MDB))
          *bad |= (uint8)(1 << r);
        if((c == 1) && over)
          e->bad_down |= (uint8)(1 << r);       //a saturated range is not tried again on the way down either
        e->vclip &= (uint8)~(1 << c);
        set_range(e, c, r - 1, c ? (over ? e->v_dc2 : m->c[1].mean_uv) : m->c[0].mean_uv);
        changed = 1;
      }

      continue;
    }

    e->vclip &= (uint8)~(1 << c);
    t = finer_range(m, c, r, *bad);

    if(t != r)
    {
      set_range(e, c, t, c ? m->c[1].mean_uv : m->c[0].mean_uv);
      changed = 1;
    }
  }

  if(!changed && !again && !bode_ch1_dead(m))
    changed = recentre(e, m);

  if((changed || again) && (e->iter < VERIFY_MAX) && !e->stop_req)
  {
    measure(e, PH_VERIFY, &e->plans[0], settle_periods[e->cfg.settle] * 10);
    return;
  }

  if(e->stop_req)
  {
    finish(e, BODE_ST_STOPPED, BODE_ERR_STOP);
    return;
  }

  e->idx  = 0;
  e->prev = -1;
  point_begin(e);
}

static void on_bg(bode_eng *e, const bode_cap *cap)
{
  const bode_plan *pp = &e->plans[e->idx];
  int32  cpr_mdb = bode_mdb_u64(cap->s[1].cpr_q24) - 144494;
  int    i;

  for(i = 0; i < e->np; i++)
  {
    int32 raw;

    if(e->plans[i].n != pp->n)
      continue;

    bode_dft_level(&e->plans[i], cap->x[1], &raw);
    e->bg_mdb[i][e->range[1] % 6] = (raw <= BODE_MDB_FLOOR / 2) ? BODE_MDB_FLOOR : raw + cpr_mdb + cap->s[1].volt_mdb;
  }

  if(e->stop_req)
  {
    finish(e, BODE_ST_STOPPED, BODE_ERR_STOP);
    return;
  }

  gen_set(e, pp, 1);
  measure(e, PH_POINT, pp, point_settle(e));
}

static void on_measure(bode_eng *e, const bode_meas *m)
{
  const bode_plan *pp = e->mp;
  int stop, coll;

  switch(e->phase)
  {
    case PH_PRESCAN:
      on_prescan(e, m);
      return;

    case PH_VERIFY:
      on_verify(e, m);
      return;

    case PH_POINT:
    case PH_RECENTRE:
      coll = ref_check(e, pp, m, &stop);
      if(stop)
        return;

      if((e->phase == PH_POINT) && !e->stop_req && !bode_ch1_dead(m) && recentre(e, m))
      {
        e->retries++;
        measure(e, PH_RECENTRE, pp, point_settle(e));
        return;
      }

      if(bode_unsettled(m) && !e->stop_req)
      {
        e->retries++;
        measure(e, PH_RETRY, pp, settle_periods[e->cfg.settle] * 20);
        return;
      }

      point_final(e, m, coll);
      return;

    case PH_RETRY:
    case PH_CLIP_AGAIN:
      point_final(e, m, 0);
      return;

    case PH_UP_PREV:
      if(e->stop_req)
      {
        finish(e, BODE_ST_STOPPED, BODE_ERR_STOP);
        return;
      }

      //The previous point on the new range: the splice scales every later point onto the sweep's first range
      if(!m->c[0].clipped && !m->c[1].clipped && (m->c[1].lvl_mdb > BODE_MDB_FLOOR))
      {
        bode_point *pr = &e->pts[e->prev];

        splice(e, pr->h_mdb, pr->h_mdeg, m);
        pr->flags |= BODE_F_SPLICED;
      }

      gen_set(e, &e->plans[e->idx], 1);
      measure(e, PH_UP_POINT, &e->plans[e->idx], point_settle(e));
      return;

    case PH_UP_POINT:
      //Still clipped or saturated: one more range up while there is one (fw 0.2.91: confirmed by a second record on
      //this range); else this is the point's result
      if((m->c[1].clipped || flat2(m, 0)) && (e->range[1] > 0) && (e->switches < MAX_SWITCHES) && !e->stop_req)
      {
        if(!e->clip_again)
        {
          e->clip_again = 1;
          e->retries++;
          measure(e, PH_UP_POINT, &e->plans[e->idx], settle_periods[e->cfg.settle] * 10);
          return;
        }

        up_step(e, m, 0);
        return;
      }

      result(e, e->idx, m, 0, e->retries + 1);
      next_point(e);
      return;

    case PH_DOWN:
      //The prediction failed (clip, saturation): back, and that range is not tried again
      if(m->c[1].clipped || flat2(m, collapsed(&e->ms, m)))
      {
        set_range(e, 1, e->up_r, e->ms.c[1].mean_uv);
        e->bad_down |= (uint8)(1 << (e->up_r + 1));

        //fw 0.2.91: the plan remembers it, so its sweeps do not try that range again (a down step that fails every
        //sweep churned the relays: e.g. the offset cannot centre a large DC level on the finer range)
        if(e->rp_set && !e->thru_sweep)
          e->rp_bad_down |= (uint8)(1 << (e->up_r + 1));
      }
      else if(m->c[1].lvl_mdb > BODE_MDB_FLOOR)
      {
        bode_point *p = &e->pts[e->idx];

        splice(e, p->h_mdb, p->h_mdeg, m);
        p->flags |= BODE_F_SPLICED;

        //From the plan: the point dropped below the floor, so it starts on the new range next time (the new factor
        //scales that range onto the first)
        if(e->reuse && e->rp_set)
        {
          bode_rplan *rp = &e->rplan[e->idx];
          int r = e->range[1];

          rp->r[1]   = e->range[1];
          rp->off[1] = (int16)e->dcoffset[1];
          rp->k_mdb  = e->k_mdb;
          rp->k_mdeg = e->k_mdeg;

          //fw 0.2.89: still under the floor: on down in this sweep (up to 0.2.88 one range per sweep, so a point that
          //had stepped up twice took two more sweeps to come back)
          if(!e->stop_req && (r < 5) && (e->switches < MAX_SWITCHES) && !(e->bad_down & (1 << (r + 1))) &&
             ((int64)m->c[1].ac_c100 * vpd_mv[r] <= (int64)PLAN_FLOOR_C100 * vpd_mv[r + 1]))
          {
            e->ms = *m;
            set_range(e, 1, r + 1, m->c[1].mean_uv);
            e->switches++;
            e->up_r = r;
            measure(e, PH_DOWN, &e->plans[e->idx], point_settle(e));
            return;
          }
        }
      }

      next_point(e);
      return;

    default:
      return;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Public

void bode_init(bode_eng *e, const bode_hw *hw)
{
  uint32 i;

  for(i = 0; i < sizeof(*e); i++)
    ((uint8 *)e)[i] = 0;

  e->hw   = hw;
  e->idx  = -1;
  e->prev = -1;
  e->cfg.start_mhz  = 100000;
  e->cfg.stop_mhz   = 1000000000;
  e->cfg.points     = 50;
  e->cfg.settle     = BODE_SETTLE_NORMAL;
  e->cfg.ch2_mode   = BODE_CH2_AUTO;
  e->cfg.background = 1;
}

//The range plan matches these settings and is complete (fw 0.2.87)
static int plan_matches(const bode_eng *e, const bode_cfg *c, uint8 hold, int n)
{
  int i;

  if(!e->rp_set || (e->rp_cfg.start_mhz != c->start_mhz) || (e->rp_cfg.stop_mhz != c->stop_mhz) ||
     (e->rp_cfg.points != c->points) || (e->rp_cfg.settle != c->settle) || (e->rp_cfg.ch2_mode != c->ch2_mode) ||
     ((c->ch2_mode == BODE_CH2_HOLD) && (e->rp_hold != hold)))
    return 0;

  for(i = 0; i < n; i++)
    if(!e->rplan[i].have)
      return 0;

  return 1;
}

int bode_start(bode_eng *e, const bode_cfg *cfg, int thru, int hold_r2)
{
  bode_plan plans[BODE_MAX_POINTS];
  int  n, i, j, same;

  n = bode_plan_sweep(cfg->start_mhz, cfg->stop_mhz, cfg->points, plans, BODE_MAX_POINTS);
  if(n < 1)
  {
    e->state = BODE_ST_ERROR;
    e->err   = BODE_ERR_PLAN;
    return BODE_ERR_PLAN;
  }

  //A new frequency plan clears the table; the same plan keeps it (RUN overwrites point by point)
  same = (n == e->np);
  for(i = 0; same && (i < n); i++)
    same = (plans[i].m == e->plans[i].m);

  for(i = 0; i < n; i++)
    e->plans[i] = plans[i];

  for(i = 0; i < BODE_MAX_POINTS; i++)
  {
    if(!same)
      e->pts[i].done = 0;

    for(j = 0; j < 6; j++)
      e->bg_mdb[i][j] = BODE_MDB_FLOOR - 1;
  }

  e->np         = n;
  e->cfg        = *cfg;
  if(e->cfg.settle >= BODE_SETTLE_COUNT)
    e->cfg.settle = BODE_SETTLE_NORMAL;
  e->thru_sweep = (uint8)(thru != 0);
  e->hold_r2    = (uint8)((hold_r2 >= 0) && (hold_r2 < 6) ? hold_r2 : 5);
  e->sweep_id++;
  e->state      = BODE_ST_RUNNING;
  e->err        = BODE_ERR_NONE;
  e->t0         = now_ms(e);
  e->t_end      = 0;
  e->idx        = 0;
  e->prev       = -1;
  e->iter       = 0;
  e->exhausted  = 0;
  e->bad_down   = 0;
  e->stop_req   = 0;
  e->switches   = 0;
  e->planned    = 0;
  e->k_mdb      = 0;
  e->k_mdeg     = 0;
  e->nbg        = 0;
  e->retries    = 0;
  e->relay_pending = 0;
  e->retake     = 0;
  e->n_caps     = 0;
  e->n_timeout  = 0;
  e->n_leak     = 0;
  e->n_retake   = 0;
  e->gen_on     = 0;
  e->disturbed  = 1;

  if(thru)
  {
    e->thru.measured = 0;
    e->thru.applied  = 0;
  }

  //The range plan (fw 0.2.87): the same settings start from it; other settings start a new one; a thru sweep drops it
  e->reuse = (uint8)(!thru && plan_matches(e, &e->cfg, e->hold_r2, n));

  if(thru)
    e->rp_set = 0;
  else if(!e->reuse)
  {
    for(i = 0; i < BODE_MAX_POINTS; i++)
      e->rplan[i].have = 0;

    e->rp_bad_down = 0;

    e->rp_cfg  = e->cfg;
    e->rp_hold = e->hold_r2;
    e->rp_set  = 1;
  }

  if(e->reuse)
  {
    //No pre-scan: every point starts on its planned ranges (the first write goes out whatever the channel shows); the
    //pre-scan's reference (ref2, v_dc2) of the sweep that made the plan stays
    e->range[0] = e->range[1] = 0xFF;
    e->bad_down = e->rp_bad_down;
    point_begin(e);
    return 0;
  }

  e->have_ref2 = 0;

  //Pre-scan: both channels on range 1 (+-11 V at 1x), offsets on 0 V, the first frequency with the first-point settle.
  //fw 0.2.94: CH2 on Constant (Hold) stays on its range: the sweep never writes another (owner 2026-10-20 14:16)
  for(i = 0; i < 2; i++)
  {
    e->range[i] = 0xFF;
    set_range(e, i, ((i == 1) && (e->cfg.ch2_mode == BODE_CH2_HOLD)) ? e->hold_r2 : 1, 0);
  }

  gen_set(e, &e->plans[0], 1);
  measure(e, PH_PRESCAN, &e->plans[0], settle_periods[e->cfg.settle] * FIRST_FACTOR * 10);
  return 0;
}

void bode_stop(bode_eng *e, int abort)
{
  if(e->state != BODE_ST_RUNNING)
    return;

  if(abort || (e->mstate == M_IDLE))
    finish(e, BODE_ST_STOPPED, BODE_ERR_STOP);
  else
    e->stop_req = 1;
}

int bode_step(bode_eng *e)
{
  uint32 now;
  int    r;

  if((e->state != BODE_ST_RUNNING) || (e->mstate == M_IDLE))
    return 0;

  now = now_ms(e);

  if(e->mstate == M_SETTLE)
  {
    if(!after(now, e->deadline))
      return 0;

    if(e->hw->arm(e->hw->ctx, e->mp, e->mtotal, trig_code(e)))
    {
      finish(e, BODE_ST_ERROR, BODE_ERR_ARM);
      return 1;
    }

    e->mstate   = M_ARMED;
    e->forced   = 0;
    e->deadline = now_ms(e) + 2 * (bode_record_us(e->mp, e->mtotal) / 1000) + 1000;
    return 0;
  }

  r = e->hw->poll(e->hw->ctx);

  if(r < 0)
  {
    finish(e, BODE_ST_ERROR, BODE_ERR_HOST);
    return 1;
  }

  if(!r)
  {
    if(after(now, e->deadline))
    {
      if(e->forced)
      {
        finish(e, BODE_ST_ERROR, BODE_ERR_TIMEOUT);
        return 1;
      }

      e->hw->force(e->hw->ctx);
      e->forced   = 1;
      e->deadline = now + 200;
    }

    return 0;
  }

  {
    bode_cap  cap;
    bode_dft  d;
    bode_meas m;

    e->mstate = M_IDLE;

    if(e->hw->read(e->hw->ctx, e->mp, e->mtotal, &cap))
    {
      finish(e, BODE_ST_ERROR, BODE_ERR_HOST);
      return 1;
    }

    e->last_fir   = cap.fir;
    e->last_check = cap.check;

    //Statistics of the generator-on captures (a background capture has no signal to trigger on)
    if(!e->mbg && (e->n_caps < 0xFFFF))
      e->n_caps++;
    if(!e->mbg && !cap.triggered && (e->n_timeout < 0xFFFF))
      e->n_timeout++;

    //fw 0.2.83: the capture self-check flagged this capture (or a core keeps failing): the data is not the signal,
    //whatever the phase (fail fast, as the 0.2.71 rule)
    if(cap.check == BODE_CHK_FAULT)
    {
      finish(e, BODE_ST_ERROR, BODE_ERR_READOUT);
      return 1;
    }

    //fw 0.2.87 (host _measure / _background): a suspect record (CH1 flat or without a tone, CH2 constant; a constant
    //background record) is taken once more with the same settle time, as the 0x153A first-record artefact looks so
    if(e->mbg)
    {
      uint32 i;
      int    flat = 1;

      for(i = 1; flat && (i < e->mtotal); i++)
        flat = (cap.x[1][i] == cap.x[1][0]);

      if(flat && !e->retake)
      {
        e->retake = 1;
        request(e, e->phase, e->mp, e->msettle, e->mtotal, 1);
        return 1;
      }

      e->retake = 0;
      on_bg(e, &cap);
    }
    else
    {
      bode_dft_run(e->mp, cap.x[0], cap.x[1], e->work, &d);
      bode_measure(e->mp, &d, cap.s, cap.zero_c100, cap.uv_q8, &m);

      //CH1's DC level for the next trigger level (fw 0.2.91)
      if(!bode_ch1_dead(&m))
      {
        e->v1_uv   = m.c[0].mean_uv;
        e->have_v1 = 1;
      }

      //fw 0.2.89: an incoherent CH1 too. On the device that is a record joined from two captures (an FPGA-auto record
      //that started on the time-out, bench 2026-10-20 cells_1235: leak -3 .. -36 dB in 5 of 8 such records, none
      //with the CH1 trigger the engine now arms with, pc_bode.c); the host's triggered captures never show it. fw 0.2.91:
      //and a record that ended on the time-out (no trigger: the kind that is joined), with the level from its own mean
      if((bode_suspect(&m) || bode_incoherent(&m) || !cap.triggered) && !e->retake && !e->stop_req)
      {
        if(bode_incoherent(&m) && (e->n_leak < 0xFFFF))
          e->n_leak++;
        else if(e->n_retake < 0xFFFF)
          e->n_retake++;

        e->retake = 1;
        e->retries++;
        request(e, e->phase, e->mp, e->msettle, e->mtotal, 0);
        return 1;
      }

      e->retake = 0;
      on_measure(e, &m);
    }
  }

  return 1;
}

void bode_snap_save(const bode_eng *e, bode_snap *s)
{
  int i;

  s->cfg = e->cfg;
  s->np  = e->np;

  for(i = 0; i < BODE_MAX_POINTS; i++)
  {
    s->plans[i] = e->plans[i];
    s->pts[i]   = e->pts[i];
  }

  s->state     = e->state;
  s->err       = e->err;
  s->exhausted = e->exhausted;
  s->switches  = e->switches;
  s->planned   = e->planned;
  s->t0        = e->t0;
  s->t_end     = e->t_end;
}

void bode_snap_restore(bode_eng *e, const bode_snap *s)
{
  int i;

  if(e->state == BODE_ST_RUNNING)
    return;

  e->cfg = s->cfg;
  e->np  = s->np;

  for(i = 0; i < BODE_MAX_POINTS; i++)
  {
    e->plans[i] = s->plans[i];
    e->pts[i]   = s->pts[i];
  }

  e->state     = s->state;
  e->err       = s->err;
  e->exhausted = s->exhausted;
  e->switches  = s->switches;
  e->planned   = s->planned;
  e->t0        = s->t0;
  e->t_end     = s->t_end;
  e->idx       = -1;
}

uint8 bode_state(const bode_eng *e)        { return e->state; }
uint8 bode_error(const bode_eng *e)        { return e->err; }
uint32 bode_sweep_id(const bode_eng *e)    { return e->sweep_id; }
int bode_count(const bode_eng *e)          { return e->np; }
int bode_current(const bode_eng *e)        { return (e->state == BODE_ST_RUNNING) ? e->idx : -1; }
uint16 bode_switches(const bode_eng *e)    { return e->switches; }
uint16 bode_planned_switches(const bode_eng *e) { return e->planned; }
int bode_plan_reused(const bode_eng *e)    { return e->reuse; }

void bode_capture_stats(const bode_eng *e, uint16 *caps, uint16 *timeouts, uint16 *leak, uint16 *other)
{
  *caps     = e->n_caps;
  *timeouts = e->n_timeout;
  *leak     = e->n_leak;
  *other    = e->n_retake;
}
uint8 bode_exhausted(const bode_eng *e)    { return e->exhausted; }
const bode_cfg *bode_last_cfg(const bode_eng *e)   { return &e->cfg; }
const bode_plan *bode_plans(const bode_eng *e)     { return e->plans; }
bode_thru *bode_thru_cal(bode_eng *e)      { return &e->thru; }
int bode_is_thru_sweep(const bode_eng *e)  { return e->thru_sweep; }

int bode_done_count(const bode_eng *e)
{
  int i, n = 0;

  for(i = 0; i < e->np; i++)
    if(e->pts[i].done)
      n++;

  return n;
}

uint32 bode_elapsed_ms(const bode_eng *e)
{
  if(!e->t0 && !e->sweep_id)
    return 0;

  return ((e->state == BODE_ST_RUNNING) ? e->hw->ms(e->hw->ctx) : e->t_end) - e->t0;
}

uint32 bode_estimate_ms(const bode_cfg *cfg)
{
  bode_plan plans[BODE_MAX_POINTS];
  uint32 per = settle_periods[(cfg->settle < BODE_SETTLE_COUNT) ? cfg->settle : 1] * 10, t = 0, lastn = 0;
  int    n = bode_plan_sweep(cfg->start_mhz, cfg->stop_mhz, cfg->points, plans, BODE_MAX_POINTS), i;

  if(n < 1)
    return 0;

  //Per point: about 15 ms of readout and DFT, the settle time, the record (design 3.7); the first point's longer
  //settle; the pre-scan and its check; one background capture per rate; a few relay switches
  for(i = 0; i < n; i++)
  {
    t += 15 + bode_settle_ms(&plans[i], (i == 0) ? per * FIRST_FACTOR : per) + bode_record_us(&plans[i], bode_total(&plans[i])) / 1000;

    if(cfg->background && (plans[i].n != lastn))
      t += 15 + bode_record_us(&plans[i], bode_total(&plans[i])) / 1000 + bode_settle_ms(&plans[i], per * FIRST_FACTOR);

    lastn = plans[i].n;
  }

  t += 2 * (15 + bode_settle_ms(&plans[0], per) + bode_record_us(&plans[0], bode_total(&plans[0])) / 1000) + 4 * RELAY_MS;

  if(cfg->ch2_mode == BODE_CH2_AUTO)
    t += 3 * (RELAY_MS + 15 + bode_record_us(&plans[n - 1], bode_total(&plans[n - 1])) / 1000);

  return t;
}

//Index of the thru point with this (M, N), -1 = none
static int thru_find(const bode_thru *t, uint32 m, uint32 n)
{
  int i;

  for(i = 0; i < t->count; i++)
    if((t->m[i] == m) && (t->n[i] == n))
      return i;

  return -1;
}

uint8 bode_thru_state(const bode_eng *e)
{
  const bode_thru *t = &e->thru;

  if(e->thru_sweep && (e->state == BODE_ST_RUNNING))
    return BODE_THRU_MEASURING;

  if(!t->measured)
    return BODE_THRU_NONE;

  return ((t->start_mhz == e->cfg.start_mhz) && (t->stop_mhz == e->cfg.stop_mhz) && (t->points == e->cfg.points)) ?
         BODE_THRU_VALID : BODE_THRU_STALE;
}

void bode_thru_apply(bode_eng *e, int on)
{
  if(e->thru.measured)
    e->thru.applied = (uint8)(on != 0);
}

void bode_thru_clear(bode_eng *e)
{
  e->thru.measured = 0;
  e->thru.applied  = 0;
  e->thru.count    = 0;
}

int bode_point_get(const bode_eng *e, int i, bode_point *out)
{
  const bode_thru *t = &e->thru;

  if((i < 0) || (i >= e->np) || !e->pts[i].done)
    return 0;

  *out = e->pts[i];

  //The thru calibration divides out (not from the thru sweep itself)
  if(t->measured && t->applied && !(e->thru_sweep && (e->state == BODE_ST_RUNNING)))
  {
    int k = thru_find(t, e->plans[i].m, e->plans[i].n);

    if(k < 0)
      out->flags |= BODE_F_NO_CAL;
    else
    {
      out->h_mdb  -= t->h_mdb[k];
      out->h_mdeg  = bode_wrap_mdeg(out->h_mdeg - t->h_mdeg[k]);

      if((t->r1[k] != out->r1) || (t->r2[k] != out->r2))
        out->flags |= BODE_F_CAL_RANGE;

      if(t->flags[k] & BODE_F_BAD)
        out->flags |= BODE_F_CAL_FLAGGED;
    }
  }

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// READ_BODE / BODE_CTRL bodies (docs/protocol.md section 14 row 60)

static void put16(uint8 *p, uint32 v) { p[0] = (uint8)v; p[1] = (uint8)(v >> 8); }
static void put32(uint8 *p, uint32 v) { put16(p, v); put16(p + 2, v >> 16); }

//mdB / mdeg -> centi, rounded half away from zero, clamped to int16
static int16 centi(int32 v)
{
  v = (v >= 0) ? (v + 5) / 10 : -((-v + 5) / 10);
  return (int16)((v > 32767) ? 32767 : (v < -32768) ? -32768 : v);
}

void bode_proto_settings(const bode_cfg *c, uint8 *p)
{
  p[14] = c->settle;
  p[15] = c->ch2_mode;
  put32(p + 16, (uint32)((c->start_mhz + 5) / 10));
  put32(p + 20, (uint32)((c->stop_mhz + 5) / 10));
  put16(p + 24, c->points);
}

void bode_proto_header(const bode_eng *e, uint8 view, uint8 *p)
{
  uint32 i;

  for(i = 0; i < BODE_HDR_BYTES; i++)
    p[i] = 0;

  put32(p + 0, e->sweep_id);
  p[4] = e->state;
  p[5] = e->err;
  p[6] = (uint8)((view & 0x03) | (e->thru_sweep ? 0x04 : 0) | ((e->exhausted & 3) << 3));
  p[7] = (uint8)(bode_thru_state(e) | (e->thru.applied ? 0x80 : 0));
  put16(p + 8, (uint32)e->np);
  put16(p + 10, (uint32)bode_done_count(e));
  put16(p + 12, (uint32)(uint16)(int16)bode_current(e));
  bode_proto_settings(&e->cfg, p);
  put16(p + 26, e->switches);
  put32(p + 28, bode_elapsed_ms(e));
}

uint32 bode_proto_rows(const bode_eng *e, uint32 first, uint32 max, uint8 *p)
{
  uint32 i, n = 0;

  for(i = first; (i < (uint32)e->np) && (n < max); i++, n++, p += BODE_ROW_BYTES)
  {
    bode_point pt;
    uint32 k;

    for(k = 0; k < BODE_ROW_BYTES; k++)
      p[k] = 0;

    put32(p + 0, e->plans[i].m);
    put32(p + 4, (uint32)((e->plans[i].f_mhz + 5) / 10));

    if(!bode_point_get(e, (int)i, &pt))
    {
      put16(p + 18, BODE_ROW_NOT_MEASURED);
      continue;
    }

    put16(p + 8,  (uint32)(uint16)centi(pt.h_mdb));
    put16(p + 10, (uint32)(uint16)centi(pt.h_mdeg));
    put16(p + 12, (uint32)(uint16)centi(pt.v1_mdb - 3010));     //peak -> rms
    put16(p + 14, (uint32)(uint16)centi(pt.v2_mdb - 3010));
    p[16] = pt.r1;
    p[17] = pt.r2;
    put16(p + 18, pt.flags);
  }

  return n;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Start / Stop steps and AUTO SET's span (fw 0.2.69)

uint64 bode_step_mhz(int i)
{
  static const uint8 m125[3] = { 1, 2, 5 };
  uint64 f;
  int k;

  if(i < 0) i = 0;
  if(i >= BODE_STEPS) i = BODE_STEPS - 1;

  f = (uint64)m125[i % 3] * 1000;
  for(k = 0; k < i / 3; k++)
    f *= 10;

  return f;
}

static int32 auto_abs(int32 d)
{
  return (d < 0) ? -d : d;
}

static int32 auto_med3(int32 a, int32 b, int32 c)
{
  if(a > b) { int32 t = a; a = b; b = t; }
  if(b > c) b = c;
  return (a > b) ? a : b;
}

int bode_auto_span(const uint64 *f, const int32 *m, const int32 *p, const uint16 *fl, const uint8 *have, int n,
                   int start_i, int stop_i, bode_autospan *o)
{
  static int32 mm[BODE_MAX_POINTS];
  static int   vi[BODE_MAX_POINTS];
  uint8 ok[BODE_MAX_POINTS], absurd[BODE_MAX_POINTS];
  bode_markers mk;
  int   i, k, nv, good = 0, ch1low = 0, nabs = 0, abs_low = 0, a = -1, b = -1, changing = 0, coarse = 0;
  uint64 lo = 0, hi = 0, f0 = 0;
  uint32 q = 0;

  o->start_i = (uint8)start_i;
  o->stop_i  = (uint8)stop_i;
  o->settle  = BODE_SETTLE_NORMAL;
  o->q_x100  = 0;
  o->f0_mhz  = 0;
  o->coarse  = 0;
  o->ref_mdb = 0;
  o->absurd  = 0;

  if(n > BODE_MAX_POINTS)
    n = BODE_MAX_POINTS;

  for(i = 0, nv = 0; i < n; i++)
  {
    ok[i] = (uint8)(have[i] && !(fl[i] & BODE_F_BAD));
    absurd[i] = 0;

    if(have[i] && !(fl[i] & (BODE_F_NEAR_FLOOR | BODE_F_LOW_SNR)))
    {
      good++;
      if(fl[i] & BODE_F_CH1_LOW)
        ch1low++;
    }

    if(ok[i])
      vi[nv++] = i;
  }

  if(!good)
    return BODE_AS_NO_RESPONSE;

  //fw 0.2.84: CH1 too small on most points: the ratio means nothing (owner 2026-10-20 11:23)
  if(2 * ch1low > good)
    return BODE_AS_CH1_LOW;

  //fw 0.2.84: absurd points (a bad record): more than 20 dB from both valid neighbours; an end point from both of the
  //next two valid points. One is dropped; two or more, or one where the reference is taken, make the pass inconsistent.
  for(k = 0; k < nv; k++)
  {
    int32 y = m[vi[k]];
    int   ka, kb;

    if((k > 0) && (k < nv - 1))           { ka = k - 1;  kb = k + 1; }
    else if((nv >= 3) && (k == 0))        { ka = 1;      kb = 2; }
    else if((nv >= 3) && (k == nv - 1))   { ka = nv - 2; kb = nv - 3; }
    else
      continue;

    if((auto_abs(y - m[vi[ka]]) > BODE_AS_ABSURD_MDB) && (auto_abs(y - m[vi[kb]]) > BODE_AS_ABSURD_MDB))
    {
      absurd[vi[k]] = 1;
      nabs++;
      if(k < 3)
        abs_low = 1;
    }
  }

  o->absurd = (uint8)nabs;

  if((nabs >= 2) || abs_low)
    return BODE_AS_INCONSISTENT;

  for(i = 0, nv = 0; i < n; i++)
  {
    if(absurd[i])
      ok[i] = 0;

    mm[i] = m[i];

    if(ok[i])
    {
      vi[nv++] = i;

      if(a < 0)
        a = i;
      else if(b < 0)
        b = i;
    }
  }

  //The reference level: the median of the three lowest-frequency valid points (the markers take the first)
  if(nv >= 3)
    mm[vi[0]] = auto_med3(m[vi[0]], m[vi[1]], m[vi[2]]);

  bode_markers_find(f, mm, p, ok, n, &mk);
  o->ref_mdb = mk.ref_mdb;

  //A resonance's peak counts only with a valid point on either side of it (two adjacent points confirm it)
  if(mk.shape == BODE_SHAPE_RESONANT)
  {
    int pk = vi[0];

    for(k = 1; k < nv; k++)
      if(mm[vi[k]] > mm[pk])
        pk = vi[k];

    if((pk == 0) || (pk >= n - 1) || !ok[pk - 1] || !ok[pk + 1])
      return BODE_AS_UNCONFIRMED;
  }

  //fw 0.2.84: the magnitude from the reference (the median) to the third valid point, so one bad first record does not
  //read as a slope; the phase from the first two
  if((a >= 0) && (b >= 0))
  {
    int32 dm = (nv >= 3) ? m[vi[2]] - mm[a] : m[b] - m[a], dp = bode_wrap_mdeg(p[b] - p[a]);

    changing = (dm > 1000) || (dm < -1000) || (dp > 5000) || (dp < -5000);
  }

  if((mk.shape == BODE_SHAPE_RESONANT) || (mk.ph_mhz[0] && mk.ph_mhz[1]))
  {
    //f0: the peak, else the middle (in log f) of the +45 and -45 deg crossings; Q from the -3 dB width, else from the
    //+-45 deg crossings (a second-order band-pass passes them at its -3 dB points)
    uint64 fa = mk.ph_mhz[0], fb = mk.ph_mhz[1];

    if(mk.have_peak)
      f0 = mk.peak_mhz;
    else
      f0 = bode_exp2_round((bode_log2_q32(fa) + bode_log2_q32(fb)) / 2);

    if(mk.have_q)
      q = mk.q_x100;
    else if(fa && fb && (fa != fb))
      q = (uint32)((f0 * 100) / ((fa > fb) ? fa - fb : fb - fa));

    //A band narrower than two point spacings is not resolved: its Q reads low. The true Q is then at least what two
    //spacings allow, 1 / (2 ln r) for points r apart, and at least 2 (half a decade either side); Settle one preset up
    //from that (the overview's 20 points over five decades are 1.8x apart: a Q 8 tank's band is an eighth of f0;
    //fw 0.2.71: AUTO SET then measures the band on finer passes, bode_auto_zoom)
    if((a >= 0) && (n > 1) && (mk.n3db == 2))
    {
      int64 sp = (bode_log2_q32(f[n - 1]) - bode_log2_q32(f[0])) / (n - 1);

      if((sp > 0) && ((bode_log2_q32(mk.f3db_mhz[1]) - bode_log2_q32(mk.f3db_mhz[0])) < 2 * sp))
      {
        uint64 lb = ((uint64)7213 << 32) / ((uint64)sp * 100);      //100 / (2 ln 2 log2 r)

        coarse = 1;
        if(q < lb)
          q = (uint32)((lb < 100000) ? lb : 100000);
      }
    }

    if(coarse && (q <= 200))
      q = 201;

    if(q <= 200)
    {
      lo = f0 / 10;
      hi = f0 * 10;
    }
    else if(q <= 1000)
    {
      lo = (f0 * 1000) / 3162;
      hi = (f0 * 3162) / 1000;
    }
    else
    {
      uint64 d = (f0 * 400) / q;

      lo = f0 - d;
      hi = f0 + d;
    }

    o->settle = (q <= 700) ? BODE_SETTLE_NORMAL : (q <= 3400) ? BODE_SETTLE_RESONANT : BODE_SETTLE_HIGHQ;
    if(coarse && (o->settle < BODE_SETTLE_HIGHQ))
      o->settle++;

    //A band-pass always rises at its low end: the still-changing rule is for the other shapes
    changing = 0;
    o->q_x100 = q;
    o->coarse = (uint8)coarse;
  }
  else if(((mk.shape == BODE_SHAPE_LOWPASS) || (mk.shape == BODE_SHAPE_HIGHPASS)) && mk.n3db)
  {
    f0 = mk.f3db_mhz[0];
    lo = f0 / 10;
    hi = f0 * 10;
  }

  if(changing)
  {
    lo = bode_step_mhz(BODE_STOP_MIN);                 //10 Hz
    if(!hi)
      hi = bode_step_mhz(stop_i);
  }

  if(!lo || !hi)
    return BODE_AS_FLAT;

  o->f0_mhz = f0;

  //Outwards to the steps, inside 1 Hz .. 10 MHz
  start_i = 0;
  for(i = 0; i < BODE_STEPS; i++)
    if(bode_step_mhz(i) <= lo)
      start_i = i;

  stop_i = BODE_STEPS - 1;
  for(i = BODE_STEPS - 1; i >= 0; i--)
    if(bode_step_mhz(i) >= hi)
      stop_i = i;

  if(start_i > BODE_START_MAX) start_i = BODE_START_MAX;
  if(stop_i < BODE_STOP_MIN)   stop_i = BODE_STOP_MIN;
  if(stop_i <= start_i)        stop_i = start_i + 1;

  o->start_i = (uint8)start_i;
  o->stop_i  = (uint8)stop_i;
  return BODE_AS_SPAN;
}

//fw 0.2.71 (bode-ui.md 3.2, the sharp-tank rule): the next of AUTO SET's passes for an unresolved band, each 20 points
//around the f0 found so far: pass 2 f0 / sqrt(10) .. f0 sqrt(10) (12.9 % apart, Settle Resonant), pass 3 f0 / 1.1 ..
//f0 x 1.1 (1.0 % apart, Settle High Q), pass 4 f0 / 1.03 .. f0 x 1.03 (0.3 %, High Q). A band is resolved when it spans
//two point spacings: up to Q 50 on pass 3, Q 160 on pass 4. Returns 1 with the next pass's settings, 0 when the span can
//be set (resolved, or four passes done).
int bode_auto_zoom(const bode_autospan *a, int passes, bode_cfg *c)
{
  static const uint16 ratio_x1000[3] = { 3162, 1100, 1030 };
  uint64 lo, hi;
  uint32 r;

  if(!a->coarse || !a->f0_mhz || (passes < 1) || (passes >= BODE_AUTO_PASSES))
    return 0;

  r  = ratio_x1000[passes - 1];
  lo = (a->f0_mhz * 1000) / r;
  hi = (a->f0_mhz * r) / 1000;

  if(lo < BODE_FMIN_MHZ) lo = BODE_FMIN_MHZ;
  if(hi > BODE_FMAX_MHZ) hi = BODE_FMAX_MHZ;

  c->start_mhz  = lo;
  c->stop_mhz   = hi;
  c->points     = 20;
  c->settle     = (passes == 1) ? BODE_SETTLE_RESONANT : BODE_SETTLE_HIGHQ;
  c->ch2_mode   = BODE_CH2_AUTO;
  c->background = 1;
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// AUTO SET's CH2 range mode (fw 0.2.93, owner 2026-10-20 13:33; bode_sweep.h)

#define CODES128_MDB        42144       //20 log10 128: a channel's full scale in codes peak

void bode_ch2_full_scale(const bode_eng *e, uint8 probe_index, int32 *fs_mdb)
{
  bode_chinfo ci;
  int r;

  for(r = 0; r < 6; r++)
  {
    e->hw->chan_info(e->hw->ctx, 1, r, &ci);
    fs_mdb[r] = ci.uv_q8 ? CODES128_MDB + bode_volt_mdb(ci.uv_q8, probe_index) : BODE_MDB_FLOOR;
  }
}

static int ac2_end(bode_ac2 *st, int verdict, int reason)
{
  if(st)
  {
    st->verdict = (uint8)verdict;
    st->reason  = (uint8)reason;
  }

  return verdict;
}

int bode_auto_ch2(const uint64 *f, const int32 *v2, const uint16 *fl, const uint8 *have, int n, uint64 lo, uint64 hi,
                  const int32 *fs, uint8 *hold_r2, bode_ac2 *st)
{
  bode_ac2 dummy;
  int32 vmax = 0, vmin = 0;
  int   i, r, cnt = 0, notone = 0;

  if(!st)
    st = &dummy;

  st->n = st->junk = 0;
  st->spread_mdb = st->vmax_mdb = 0;

  if(lo > hi)
  {
    uint64 t = lo;

    lo = hi;
    hi = t;
  }

  for(i = 0; i < n; i++)
  {
    if(!have[i] || (f[i] < lo) || (f[i] > hi))
      continue;

    //A bad record's level is not the circuit's
    if(fl[i] & BODE_CH2_JUNK)
    {
      if(st->junk < 255)
        st->junk++;
      continue;
    }

    //A point without a tone: no level to hold
    if(v2[i] <= BODE_MDB_FLOOR)
    {
      notone = 1;
      continue;
    }

    if(!cnt || (v2[i] > vmax)) vmax = v2[i];
    if(!cnt || (v2[i] < vmin)) vmin = v2[i];
    cnt++;
  }

  st->n          = (uint8)((cnt < 255) ? cnt : 255);
  st->spread_mdb = cnt ? vmax - vmin : 0;
  st->vmax_mdb   = vmax;

  if(notone)
    return ac2_end(st, BODE_CH2_AUTO, BODE_AC2_R_NO_TONE);

  if(cnt < 2)
    return ac2_end(st, BODE_CH2_AUTO, BODE_AC2_R_FEW);

  if(vmax - vmin >= BODE_HOLD_SPREAD_MDB)
    return ac2_end(st, BODE_CH2_AUTO, BODE_AC2_R_SPREAD);

  //The most sensitive range that holds the largest level with the margin
  for(r = 5; r >= 0; r--)
    if((fs[r] > BODE_MDB_FLOOR) && (vmax + BODE_HOLD_MARGIN_MDB <= fs[r]))
    {
      *hold_r2 = (uint8)r;
      return ac2_end(st, BODE_CH2_HOLD, BODE_AC2_R_FITS);
    }

  return ac2_end(st, BODE_CH2_AUTO, BODE_AC2_R_TOO_BIG);
}
