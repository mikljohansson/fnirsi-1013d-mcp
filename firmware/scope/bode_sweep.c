//----------------------------------------------------------------------------------------------------------------------------------
// bode_sweep.c - the on-device Bode sweep engine (see bode_sweep.h). The structure follows the host tool's BodeRunner
// (mcp/src/fnirsi1013d/bode.py) step by step; each capture the host makes is one request here, and what the host does with
// its result is the handler of that request's phase. Host-tested: host_tests/test_bode_sweep.c (a DUT model behind bode_hw).
//----------------------------------------------------------------------------------------------------------------------------------

#include "bode_sweep.h"

//Quality gates (bode.py; mdB of code levels and of ratios)
#define CH1_LOW_MDB         26021       //20 codes
#define CH2_SMALL_MDB       9542        //3 codes
#define LOW_SNR_MDB         20000
#define NEAR_FLOOR_MDB      20000
#define LEAK_LIMIT_MDB      (-40000)
#define FLAT_C100           5           //0.05 code
#define COLLAPSE_MDB        20000       //a tone below 1/10 of the reference
#define CENTRE_TOL_C100     1200        //re-centre beyond 12 codes from 128
#define FULL_SCALE_C100     10880       //85 % of 128 codes
#define DOWNRANGE_C100      7680        //60 % of 128 codes
#define RELAY_MS            50
#define MAX_SWITCHES        6
#define FIRST_FACTOR        3

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
  return collapsed || ((m->c[1].ac_c100 < FLAT_C100) && (m->c[1].code_mdb < CH2_SMALL_MDB));
}

//CH2's tone vanished on the new range at the same frequency: ref measured it clearly on the previous one
static int collapsed(const bode_meas *ref, const bode_meas *now)
{
  return (ref->c[1].code_mdb >= 0) && !ref->c[1].clipped && (now->c[1].lvl_mdb < ref->c[1].lvl_mdb - COLLAPSE_MDB);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Hardware steps

static void relay_wait(bode_eng *e)
{
  uint32 t = now_ms(e) + RELAY_MS;

  if(!e->relay_pending || after(t, e->relay_until))
    e->relay_until = t;

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

//Range r with the offset centred on a DC level of v_uv microvolts at the BNC (host _set_range: offset 0, then centre)
static void set_range(bode_eng *e, int c, int r, int32 v_uv)
{
  bode_chinfo ci;

  if(r != e->range[c])
    relay_wait(e);

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

static void request(bode_eng *e, int phase, const bode_plan *pp, uint32 settle_ms, uint32 total, int bg)
{
  uint32 now = now_ms(e);
  uint32 start = (e->relay_pending && !after(now, e->relay_until)) ? e->relay_until : now;

  e->relay_pending = 0;
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
  uint16 f = 0;

  if(m->c[0].clipped)                         f |= BODE_F_CH1_CLIPPED;
  if(m->c[1].clipped)                         f |= BODE_F_CLIPPED;
  if(flat2(m, coll))                          f |= BODE_F_CH2_OVERLOAD;
  if(m->c[0].code_mdb < CH1_LOW_MDB)          f |= BODE_F_CH1_LOW;
  if(m->c[1].code_mdb < CH2_SMALL_MDB)        f |= BODE_F_CH2_SMALL;
  if(m->snr2_mdb < LOW_SNR_MDB)               f |= BODE_F_LOW_SNR;
  if(m->leak_mdb > LEAK_LIMIT_MDB)            f |= BODE_F_INCOHERENT;
  if(bode_unsettled(m))                       f |= BODE_F_UNSETTLED;
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
    e->bad_down |= (uint8)(1 << r);       //a saturated range is not tried again on the way down

  set_range(e, 1, r - 1, flat2(from, coll) ? e->v_dc2 : from->c[1].mean_uv);
  e->switches++;
  e->up_r = r;

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

  if(e->cfg.ch2_mode != BODE_CH2_AUTO)
  {
    next_point(e);
    return;
  }

  if(m->c[1].clipped || flat2(m, coll))
  {
    if((r > 0) && (e->switches < MAX_SWITCHES))
    {
      up_step(e, m, coll);
      return;
    }

    next_point(e);
    return;
  }

  //Down-range when the predicted peak on the next range stays under 60 % of full scale
  if((r >= 5) || (e->switches >= MAX_SWITCHES) || (e->bad_down & (1 << (r + 1))) ||
     ((int64)m->c[1].ac_c100 * vpd_mv[r] > (int64)DOWNRANGE_C100 * vpd_mv[r + 1]))
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

  if(e->have_ref2 && (pp->m == e->plans[0].m) && (e->range[1] != 1))
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

static void point_begin(bode_eng *e)
{
  const bode_plan *pp = &e->plans[e->idx];
  uint32 key = (pp->n << 3) | e->range[1];

  e->retries = 0;

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
    else
      for(cand = 1; cand < 6; cand++)
        if((int64)ac * vpd_mv[1] <= (int64)FULL_SCALE_C100 * vpd_mv[cand])
          r = cand;

    t[c] = r;
  }

  for(c = 0; c < 2; c++)
    set_range(e, c, t[c], e->v_dc[c]);

  e->iter = 0;
  measure(e, PH_VERIFY, pp, settle_periods[e->cfg.settle] * 10);
}

static void on_verify(bode_eng *e, const bode_meas *m)
{
  int over = (e->range[1] != 1) && flat2(m, collapsed(&e->ref2, m));
  int changed = 0, c;

  e->iter++;

  if(over && (e->cfg.ch2_mode == BODE_CH2_HOLD))
  {
    finish(e, BODE_ST_ERROR, BODE_ERR_OVERLOAD);
    return;
  }

  for(c = 0; c < 2; c++)
  {
    int cl = c ? (m->c[1].clipped || over) : m->c[0].clipped;

    if(cl && (e->range[c] > 0) && !((c == 1) && (e->cfg.ch2_mode == BODE_CH2_HOLD)))
    {
      if((c == 1) && over)
        e->bad_down |= (uint8)(1 << e->range[1]);

      set_range(e, c, e->range[c] - 1, c ? (over ? e->v_dc2 : m->c[1].mean_uv) : m->c[0].mean_uv);
      changed = 1;
    }
  }

  if(!changed)
    changed = recentre(e, m);

  if(changed && (e->iter < 4))
  {
    measure(e, PH_VERIFY, &e->plans[0], settle_periods[e->cfg.settle] * 10);
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

      if((e->phase == PH_POINT) && recentre(e, m))
      {
        e->retries++;
        measure(e, PH_RECENTRE, pp, point_settle(e));
        return;
      }

      if(bode_unsettled(m))
      {
        e->retries++;
        measure(e, PH_RETRY, pp, settle_periods[e->cfg.settle] * 20);
        return;
      }

      point_final(e, m, coll);
      return;

    case PH_RETRY:
      point_final(e, m, 0);
      return;

    case PH_UP_PREV:
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
      //Still clipped or saturated: one more range up while there is one; else this is the point's result
      if((m->c[1].clipped || flat2(m, 0)) && (e->range[1] > 0) && (e->switches < MAX_SWITCHES))
      {
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
      }
      else if(m->c[1].lvl_mdb > BODE_MDB_FLOOR)
      {
        bode_point *p = &e->pts[e->idx];

        splice(e, p->h_mdb, p->h_mdeg, m);
        p->flags |= BODE_F_SPLICED;
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
  e->k_mdb      = 0;
  e->k_mdeg     = 0;
  e->have_ref2  = 0;
  e->nbg        = 0;
  e->retries    = 0;
  e->relay_pending = 0;
  e->gen_on     = 0;
  e->disturbed  = 1;

  if(thru)
  {
    e->thru.measured = 0;
    e->thru.applied  = 0;
  }

  //Pre-scan: both channels on range 1 (+-11 V at 1x), offsets on 0 V, the first frequency with the first-point settle
  for(i = 0; i < 2; i++)
  {
    e->range[i] = 0xFF;
    set_range(e, i, 1, 0);
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

    if(e->hw->arm(e->hw->ctx, e->mp, e->mtotal))
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

    e->last_fir = cap.fir;

    if(e->mbg)
      on_bg(e, &cap);
    else
    {
      bode_dft_run(e->mp, cap.x[0], cap.x[1], e->work, &d);
      bode_measure(e->mp, &d, cap.s, cap.zero_c100, cap.uv_q8, &m);
      on_measure(e, &m);
    }
  }

  return 1;
}

uint8 bode_state(const bode_eng *e)        { return e->state; }
uint8 bode_error(const bode_eng *e)        { return e->err; }
uint32 bode_sweep_id(const bode_eng *e)    { return e->sweep_id; }
int bode_count(const bode_eng *e)          { return e->np; }
int bode_current(const bode_eng *e)        { return (e->state == BODE_ST_RUNNING) ? e->idx : -1; }
uint16 bode_switches(const bode_eng *e)    { return e->switches; }
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
