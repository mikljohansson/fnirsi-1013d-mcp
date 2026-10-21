//----------------------------------------------------------------------------------------------------------------------------------
// bode_sweep.h - the on-device Bode sweep engine (fw 0.2.66, docs/plans/bode-design.md step D2): the host tool's runner
// (mcp/src/fnirsi1013d/bode.py BodeRunner) as a non-blocking state machine for the main loop. Per sweep: pre-scan at the
// first frequency on range 1, CH1 / CH2 ranging and offset centring, then per point the background capture (generator off,
// once per decimation and CH2 range), the measurement with re-centring and the half-record settle retry, the quality flags,
// and CH2 auto-ranging with the splice. Thru calibration per (M, N).
//
// Pure C, host-buildable: the hardware is a table of functions (bode_hw); the scope's is in pc_bode.c, the host test's
// (host_tests/test_bode_sweep.c) a DUT model. bode_step does at most one capture step per call and returns at once while a
// settle time or a capture runs, so the UI and the PC protocol keep running during a sweep of any length.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef BODE_SWEEP_H
#define BODE_SWEEP_H

#include "types.h"
#include "bode_dsp.h"

//Settle presets (bode-design 3.3): stimulus periods before each record; three times as many at the first point and after
//the generator was off
#define BODE_SETTLE_FAST      0     //3 periods
#define BODE_SETTLE_NORMAL    1     //10
#define BODE_SETTLE_RESONANT  2     //50
#define BODE_SETTLE_HIGHQ     3     //200
#define BODE_SETTLE_COUNT     4

#define BODE_CH2_AUTO         0     //down-ranging with the splice, up-ranging on a clip
#define BODE_CH2_HOLD         1     //the CH2 range the sweep starts with (the channel's V/div); the screen says Constant
                                    //(fw 0.2.94): no CH2 range write during the sweep, a clip or a drop only flags the point

//Sweep state
#define BODE_ST_IDLE          0     //no sweep since the view opened (or cleared)
#define BODE_ST_RUNNING       1
#define BODE_ST_DONE          2
#define BODE_ST_STOPPED       3     //STOP or the host: the points measured so far stay
#define BODE_ST_ERROR         4

//Why a sweep ended early (BODE_ST_ERROR / BODE_ST_STOPPED)
#define BODE_ERR_NONE         0
#define BODE_ERR_ARM          1     //the FPGA did not become ready (0x05)
#define BODE_ERR_TIMEOUT      2     //no capture within twice the record time + 1 s, also after FORCE
#define BODE_ERR_OVERLOAD     3     //CH2 range held and its front end saturated (a flat record): see the warning
#define BODE_ERR_PLAN         4     //no point in the requested range
#define BODE_ERR_HOST         5     //the PC armed its own capture during the sweep
#define BODE_ERR_STOP         6     //stopped (STOP, leaving the view, BODE_CTRL stop)
#define BODE_ERR_NO_HW        7     //no decimation filter or no DDS generator on this bitstream
#define BODE_ERR_READOUT      8     //the ADC readout returns garbage, not the signal. fw 0.2.83: the capture self-check
                                    //flagged a capture of the sweep (BODE_CHK_FAULT); without a self-check (before FPGA
                                    //0x153A) the 0.2.71 rule: the pre-scan at 5 V/div, the record at a rail, the
                                    //fundamental under 20 codes. A clean self-check never gives it (a small signal is
                                    //ch1_low / clipped on the points)

//The capture self-check of one capture (bode_cap.check, fw 0.2.83)
#define BODE_CHK_NONE         0     //the bitstream has none (before 0x153A): the record's own rails decide reason 8
#define BODE_CHK_CLEAN        1     //checked: nothing flagged
#define BODE_CHK_FAULT        2     //0x36 outliers flagged on this capture, or the repeat watch (PC_CAPF_REPEAT) is set

//Thru calibration state
#define BODE_THRU_NONE        0
#define BODE_THRU_MEASURING   1     //the running sweep is a thru measurement
#define BODE_THRU_VALID       2     //stored; matches the current plan
#define BODE_THRU_STALE       3     //stored; the plan (start, stop, points) changed since

//Capture result handed back by the hardware
typedef struct
{
  const uint16 *x[2];         //raw records of CH1 and CH2 (16-bit words, or 8-bit codes x 256 for legacy points)
  bode_scale    s[2];
  int32         zero_c100[2]; //the code of 0 V x 100 of each channel as captured
  uint32        uv_q8[2];     //uV per code x 256 at the BNC
  uint8         fir;          //the FIR was active in this capture
  uint8         check;        //BODE_CHK_* of this capture (fw 0.2.83; no state of an earlier capture)
  uint8         triggered;    //fw 0.2.91: the capture started on a trigger (0: on the FPGA's auto time-out or FORCE)
} bode_cap;

typedef struct
{
  int32  cal_offset;          //PWM centre of the range (dc_calibration_offset)
  int32  dc_shift_value;      //offset counts per 100 codes x ... (the UI's offset scale: codes = dcoffset x 100 / value)
  uint32 uv_q8;               //uV per code x 256 at the BNC on that range
} bode_chinfo;

typedef struct
{
  void   *ctx;
  uint32 (*ms)(void *ctx);
  void   (*gen)(void *ctx, uint32 phase_inc, int on);                      //generator on at the increment (duty 50 %) or off
  void   (*channel)(void *ctx, int c, int range, int32 dcoffset);          //enable, DC, range 0..5, offset
  void   (*chan_info)(void *ctx, int c, int range, bode_chinfo *ci);
  int    (*arm)(void *ctx, const bode_plan *pp, uint32 total, uint8 level);  //0 ok, else the FPGA was not ready;
                                                                           //fw 0.2.91: CH1 rising at this code
  int    (*poll)(void *ctx);                                               //1 done, 0 running, < 0 the host took over
  void   (*force)(void *ctx);
  int    (*read)(void *ctx, const bode_plan *pp, uint32 total, bode_cap *cap);  //0 ok
  int    has_fir;
} bode_hw;

typedef struct
{
  int32  h_mdb, h_mdeg;       //H as measured with the splice (before the thru calibration)
  int32  v1_mdb, v2_mdb;      //fundamentals, dBV peak at the tips (V2 with the splice)
  int32  snr2_mdb, leak_mdb;
  uint16 flags;
  uint8  r1, r2;              //hardware ranges
  uint8  done;                //measured in the latest sweep
  uint8  retries;
} bode_point;

typedef struct
{
  uint8  measured;            //a thru sweep completed
  uint8  applied;             //divide it out of later sweeps
  uint16 count;
  uint64 start_mhz, stop_mhz; //the plan it was measured with
  uint16 points;
  uint32 m[BODE_MAX_POINTS];
  uint32 n[BODE_MAX_POINTS];
  int32  h_mdb[BODE_MAX_POINTS], h_mdeg[BODE_MAX_POINTS];
  uint8  r1[BODE_MAX_POINTS], r2[BODE_MAX_POINTS];
  uint16 flags[BODE_MAX_POINTS];
} bode_thru;

//The settings of a sweep (the CTRL page's, BODE_CTRL's)
typedef struct
{
  uint64 start_mhz, stop_mhz;
  uint16 points;
  uint8  settle;              //BODE_SETTLE_*
  uint8  ch2_mode;            //BODE_CH2_*
  uint8  background;          //generator-off captures for the near-floor flag (default on)
} bode_cfg;

typedef struct bode_eng bode_eng;

//One engine (static on the target)
void   bode_init(bode_eng *e, const bode_hw *hw);
bode_eng *bode_engine(void);                                  //the target's instance (pc_bode.c)

//Start a sweep with cfg (thru: the sweep is a thru measurement). The CH2 range for Hold is hold_r2 (the channel's
//hardware range now). Returns 0, or BODE_ERR_PLAN.
int    bode_start(bode_eng *e, const bode_cfg *cfg, int thru, int hold_r2);
//Stop after the capture in progress (STOP: the point keeps that capture's result, no re-centring, retry or range change
//follows; fw 0.2.71, before: after the whole point), or at once (abort: leaving the view)
void   bode_stop(bode_eng *e, int abort);
//Main loop: advance the sweep; returns 1 when something changed that the display should show (a point, the state)
int    bode_step(bode_eng *e);

//State
uint8  bode_state(const bode_eng *e);
uint8  bode_error(const bode_eng *e);
uint32 bode_sweep_id(const bode_eng *e);
int    bode_count(const bode_eng *e);                         //planned points of the latest sweep
int    bode_done_count(const bode_eng *e);                    //measured so far in it
int    bode_current(const bode_eng *e);                       //index of the point being measured, -1 = none
uint32 bode_elapsed_ms(const bode_eng *e);
uint32 bode_estimate_ms(const bode_cfg *cfg);                 //duration estimate of a sweep (design 3.7, device overheads)
uint16 bode_switches(const bode_eng *e);                      //CH2 range steps taken (unplanned: ranging, a clip, a drop)
uint16 bode_planned_switches(const bode_eng *e);              //fw 0.2.93: CH2 range transitions the range plan prescribed
uint8  bode_exhausted(const bode_eng *e);                     //bit c: channel c's offset reached its limit
const bode_cfg  *bode_last_cfg(const bode_eng *e);
const bode_plan *bode_plans(const bode_eng *e);

//A point as shown / read: H with the thru calibration applied when it is on (and its flags), 0 when not measured
int    bode_point_get(const bode_eng *e, int i, bode_point *out);

//Thru calibration
bode_thru *bode_thru_cal(bode_eng *e);
uint8  bode_thru_state(const bode_eng *e);                    //BODE_THRU_*
void   bode_thru_apply(bode_eng *e, int on);
void   bode_thru_clear(bode_eng *e);
int    bode_is_thru_sweep(const bode_eng *e);

//READ_BODE / BODE_CTRL bodies (docs/protocol.md section 14 row 60): the 32-byte header and 20-byte rows, little-endian.
//view: bit 0 the Bode view shows, bit 1 RUN repeats sweeps.
#define BODE_HDR_BYTES        32
#define BODE_ROW_BYTES        20
#define BODE_ROW_NOT_MEASURED 0x8000    //row flags: the point was not measured in the latest sweep
void   bode_proto_header(const bode_eng *e, uint8 view, uint8 *p);
//fw 0.2.71: the header's settings fields (bytes 14..25: settle, CH2 range mode, start, stop, requested points) from c
void   bode_proto_settings(const bode_cfg *c, uint8 *p);
uint32 bode_proto_rows(const bode_eng *e, uint32 first, uint32 max, uint8 *p);   //rows written

//The CTRL page's Start / Stop steps (fw 0.2.66): index i is {1, 2, 5}[i % 3] x 10^(i / 3) Hz, 0 = 1 Hz .. 21 = 10 MHz
#define BODE_STEPS            22
#define BODE_START_MAX        20          //5 MHz
#define BODE_STOP_MIN         3           //10 Hz
uint64 bode_step_mhz(int i);

//AUTO SET's span (fw 0.2.69, docs/plans/bode-ui.md 3.2): from an overview sweep (100 Hz .. 10 MHz, 20 points; f, H and
//flags of n points, have[i] = measured) the Start / Stop step indices and the Settle preset.
//  low- or high-pass with a -3 dB crossing fc: fc / 10 .. 10 fc
//  resonant (or a phase through both +45 and -45 deg): f0 / 10 .. 10 f0 for Q <= 2, f0 / sqrt(10) .. f0 sqrt(10) for
//  Q <= 10, f0 (1 -+ 4 / Q) above; Settle Normal for Q <= 7, Resonant for Q <= 34, High Q above
//  a band narrower than two point spacings is not resolved (its Q reads low): Q at least 1 / (2 ln r) for points r
//  apart and at least 2, Settle one preset up (fw 0.2.71; AUTO SET measures such a band again, bode_auto_zoom)
//  still changing at the low end (the first two valid points differ by more than 1 dB or 5 deg): Start 10 Hz
//The span is snapped outwards to the steps. BODE_AS_FLAT: no corner, the span is kept; BODE_AS_NO_RESPONSE: every
//measured point is near the floor, under the noise or (fw 0.2.99) ch2_small, the span is kept.
//fw 0.2.84 (bode-ui.md 3.2): an absurd point (more than 20 dB from both its valid neighbours; an end point: from both of
//the next two valid points) is dropped; the reference level is the median of the three lowest-frequency valid points.
//Failures (AUTO SET ends in STOP, settings kept; owner 2026-10-20 11:23): BODE_AS_INCONSISTENT two or more absurd points,
//or one among the three lowest-frequency valid points (where the reference is taken); BODE_AS_UNCONFIRMED a resonance
//whose peak point lacks a valid point on either side (a peak needs its two adjacent points); BODE_AS_CH1_LOW CH1 under
//20 codes on more than half of the good points.
#define BODE_AS_SPAN          0
#define BODE_AS_FLAT          1
#define BODE_AS_NO_RESPONSE   2
#define BODE_AS_INCONSISTENT  3
#define BODE_AS_UNCONFIRMED   4
#define BODE_AS_CH1_LOW       5
#define BODE_AS_ABSURD_MDB    20000     //an absurd point: more than 20 dB from both neighbours

typedef struct
{
  uint8  start_i, stop_i, settle;
  uint32 q_x100;              //the Q used (0: none)
  uint64 f0_mhz;              //the corner or resonance found (0: none)
  uint8  coarse;              //fw 0.2.71: the band was narrower than two point spacings (Q a lower bound)
  int32  ref_mdb;             //fw 0.2.84: the reference level (a resonance: its peak), for the passes' consistency check
  uint8  absurd;              //fw 0.2.84: absurd points dropped
} bode_autospan;

//fw 0.2.71: an unresolved band (coarse) is measured again on finer passes around f0 before the span is set (pass 2
//f0 / sqrt(10) .. f0 sqrt(10), pass 3 f0 / 1.1 .. f0 x 1.1, pass 4 f0 / 1.03 .. f0 x 1.03, 20 points each): after
//`passes` passes (1 = the overview) the settings of the next one, 1; 0 when the span can be set (resolved, or
//BODE_AUTO_PASSES done)
#define BODE_AUTO_PASSES      4
int    bode_auto_zoom(const bode_autospan *a, int passes, bode_cfg *c);

int    bode_auto_span(const uint64 *f_mhz, const int32 *mag_mdb, const int32 *ph_mdeg, const uint16 *flags,
                      const uint8 *have, int n, int start_i, int stop_i, bode_autospan *out);

//fw 0.2.71: a copy of the table (the plan, the points, the settings and the outcome of the latest sweep), so a cancelled
//AUTO SET gives the previous table back (bode-ui.md 3.2 step 6). Restore only while no sweep runs.
typedef struct
{
  bode_cfg   cfg;
  bode_plan  plans[BODE_MAX_POINTS];
  bode_point pts[BODE_MAX_POINTS];
  int        np;
  uint8      state, err, exhausted;
  uint16     switches, planned;
  uint32     t0, t_end;
} bode_snap;

void   bode_snap_save(const bode_eng *e, bode_snap *s);
void   bode_snap_restore(bode_eng *e, const bode_snap *s);

//fw 0.2.87: the range plan (owner 2026-10-20 11:51 / 11:52, bode-ui.md section 10). With the table the engine keeps,
//per point, the CH1 / CH2 range and offset the point's result was measured on and the splice factor in effect. A sweep
//with the same span, points, Settle and CH2 mode (and, in Hold, the same CH2 range) as the plan's, however it was
//started (RUN, Single sweep, AUTO SET's final sweep), skips the pre-scan and starts every point on its planned ranges;
//CH2 switches only when the point clips (up) or its level drops below the floor (down: the predicted peak on the next
//range under 30 % of full scale, half the fresh sweep's 60 %), and that point's entry follows. The first sweep after a
//settings change, a sweep after a cancelled one that left the plan incomplete, and a thru sweep range from scratch. A
//plan is not kept (the next sweep ranges from scratch) when CH1 clips, when CH1 is dead (flat or without a tone), when
//the first point's CH1 would fit a finer range under 60 % of full scale (the pre-scan was fooled, e.g. by the 0x153A
//first-record artefact: bench 2026-10-20 12:05, sweep 1), or (fw 0.2.90) when a point below 2 MHz has CH1 under 20
//codes on a range with a finer one left. The header's switch count shows the effect.
typedef struct
{
  uint8  have;                //measured with the plan's settings
  uint8  r[2];
  int16  off[2];
  int32  k_mdb, k_mdeg;       //the splice factor applied to this point
} bode_rplan;

int    bode_plan_reused(const bode_eng *e);                   //the latest sweep started from the range plan
//fw 0.2.91: the latest sweep's captures, those that ended on the time-out (no trigger), and the records taken again
//for CH1 leak (a joined record) or for another reason (suspect, time-out); BODE_CTRL bytes 40..47
void   bode_capture_stats(const bode_eng *e, uint16 *caps, uint16 *timeouts, uint16 *leak, uint16 *other);

//fw 0.2.93 (owner 2026-10-20 13:33, bode-ui.md 3.2 step 3; fw 0.2.94 Hold is "Constant" on the screen, owner 14:16):
//AUTO SET's CH2 range mode. The CH2 levels (v2, dBV peak at the tip) of the measured points inside lo..hi (the chosen
//span; the overview's and the passes' points) are compared: when they spread less than BODE_HOLD_SPREAD_MDB (one range
//covers them) the result is BODE_CH2_HOLD on the most sensitive range whose full scale (fs_mdb[r], range r's 128 codes
//peak at the tip in dBV) is at least 1.5 x the largest level (*hold_r2); else BODE_CH2_AUTO. Fewer than two points in
//the span, a point without a tone (BODE_MDB_FLOOR) or a largest level that no range holds with the margin: Auto.
//fw 0.2.94: a point whose record was bad (BODE_CH2_JUNK: clipped, saturated, unsettled, incoherent; the level is not the
//circuit's) is left out; near_floor / low_snr / ch2_small points count (their level is small, which is the point).
//st (may be 0) gets the inputs of the verdict for BODE_CTRL bytes 50..53.
#define BODE_HOLD_SPREAD_MDB  20000
#define BODE_HOLD_MARGIN_MDB  3522      //20 log10 1.5
#define BODE_CH2_JUNK         (BODE_F_CH1_CLIPPED | BODE_F_CLIPPED | BODE_F_CH2_OVERLOAD | BODE_F_UNSETTLED | \
                               BODE_F_INCOHERENT)
typedef struct
{
  uint8  verdict;             //BODE_CH2_*, BODE_AC2_NONE before the first AUTO SET
  uint8  reason;              //BODE_AC2_R_*
  uint8  n;                   //points that counted
  uint8  junk;                //points left out (a bad record)
  int32  spread_mdb;          //largest - smallest level of the points that counted (0 for fewer than two)
  int32  vmax_mdb;
} bode_ac2;
#define BODE_AC2_NONE         0xFF
#define BODE_AC2_R_FITS       0     //Constant: one range holds the span
#define BODE_AC2_R_SPREAD     1     //Auto: 20 dB or more
#define BODE_AC2_R_FEW        2     //Auto: fewer than two points in the span
#define BODE_AC2_R_NO_TONE    3     //Auto: a point without a tone
#define BODE_AC2_R_TOO_BIG    4     //Auto: no range holds the largest level with the margin
int    bode_auto_ch2(const uint64 *f_mhz, const int32 *v2_mdb, const uint16 *flags, const uint8 *have, int n,
                     uint64 lo_mhz, uint64 hi_mhz, const int32 *fs_mdb, uint8 *hold_r2, bode_ac2 *st);
//CH2's full scale per range at the tip (dBV peak, 128 codes) with the probe index of bode_volt_mdb, for bode_auto_ch2
void   bode_ch2_full_scale(const bode_eng *e, uint8 probe_index, int32 *fs_mdb);

//Sizes for the target's static instance
struct bode_eng
{
  const bode_hw *hw;
  bode_cfg  cfg;
  bode_plan plans[BODE_MAX_POINTS];
  bode_point pts[BODE_MAX_POINTS];
  int32     bg_mdb[BODE_MAX_POINTS][6];       //background CH2 level per point and CH2 range (BODE_MDB_FLOOR - 1: none)
  int       np;
  uint8     state, err, thru_sweep;
  uint32    sweep_id;
  uint32    t0, t_end;

  //The run
  int       idx;                              //point being measured
  int       prev;                             //the previous point's index (-1: none)
  uint8     phase, mstate, iter;
  uint8     range[2];
  int32     dcoffset[2];
  uint8     exhausted, bad_down, disturbed, gen_on, stop_req, hold_r2;
  uint8     relay_pending, ms_coll, last_fir, last_check;
  uint32    relay_until;
  uint16    switches;                         //unplanned CH2 range steps (the header's count, MAX_SWITCHES)
  uint16    planned;                          //fw 0.2.93: planned CH2 range transitions (plan_apply between points)
  int32     k_mdb, k_mdeg;                    //the splice factor applied to later points
  int32     v_dc[2], v_dc2;                   //DC at the BNC (uV) from the pre-scan on range 1
  uint8     have_ref2;
  bode_meas ref2;                             //the pre-scan's range-1 measurement (first frequency)
  bode_meas ms;                               //the point's current measurement
  int       up_r;                             //range before an up / down step
  uint8     retries;
  uint32    bg_done[24];                      //(n << 3) | range of background captures taken
  uint8     nbg;

  //The capture in flight
  const bode_plan *mp;
  uint32    mtotal;
  uint32    deadline;
  uint8     mbg, forced;
  uint8     retake;                           //fw 0.2.87: this capture is the retake of a suspect record
  uint32    msettle;                          //its settle time (ms), for the retake
  uint8     clip_again;                       //fw 0.2.89: the planned point's clip was measured again
  uint8     vclip;                            //fw 0.2.90: the pre-scan check: bit c, channel c's clip measured again
  uint8     vbad[2];                          //fw 0.2.90: per channel, the ranges that clipped in the check
  int32     v1_uv;                            //fw 0.2.91: CH1's DC level as last measured (the trigger level)
  uint8     have_v1;
  uint16    n_caps, n_timeout, n_leak, n_retake;   //fw 0.2.91: the latest sweep's capture statistics

  bode_thru thru;

  //The range plan (fw 0.2.87)
  bode_rplan rplan[BODE_MAX_POINTS];
  bode_cfg  rp_cfg;                           //the settings it belongs to
  uint8     rp_set;                           //rp_cfg holds a plan's settings (entries may be incomplete)
  uint8     rp_hold;                          //CH2's range in Hold
  uint8     reuse;                            //this sweep started from the plan
  uint8     rp_bad_down;                      //fw 0.2.91: CH2 ranges a step down failed into in the plan's sweeps

  int32     work[BODE_MAX_RECORD];
};

#endif /* BODE_SWEEP_H */
