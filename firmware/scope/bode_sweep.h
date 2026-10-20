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
#define BODE_CH2_HOLD         1     //the CH2 range the sweep starts with (the channel's V/div)

//Point flags (READ_BODE u16; the host tool's flag names in brackets)
#define BODE_F_CH1_CLIPPED    0x0001    //[ch1_clipped]
#define BODE_F_CLIPPED        0x0002    //[clipped] CH2
#define BODE_F_CH2_OVERLOAD   0x0004    //[ch2_overload] CH2's front end saturated by a DC level (a flat record)
#define BODE_F_CH1_LOW        0x0008    //[ch1_low] CH1 fundamental below 20 codes
#define BODE_F_CH2_SMALL      0x0010    //[ch2_small] CH2 below 3 codes: +-1 dB
#define BODE_F_LOW_SNR        0x0020    //[low_snr] CH2 less than 20 dB above its noise
#define BODE_F_NEAR_FLOOR     0x0040    //[near_floor] CH2 less than 20 dB above the generator-off background
#define BODE_F_UNSETTLED      0x0080    //[unsettled]
#define BODE_F_INCOHERENT     0x0100    //[incoherent] CH1 energy beside the fundamental
#define BODE_F_SPLICED        0x0200    //[spliced] CH2 changed range at this point
#define BODE_F_FIR_OFF        0x0400    //[fir_off]
#define BODE_F_NO_CAL         0x0800    //[no_cal] thru applied, no thru point with this (M, N)
#define BODE_F_CAL_RANGE      0x1000    //[cal_range] thru taken on other ranges
#define BODE_F_CAL_FLAGGED    0x2000    //[cal_flagged] the thru point was itself flagged
#define BODE_F_RETRIED        0x4000    //measured again (re-centred, settle retry or range change)
#define BODE_F_BAD            (BODE_F_CH1_CLIPPED | BODE_F_CLIPPED | BODE_F_CH2_OVERLOAD | BODE_F_LOW_SNR | \
                               BODE_F_NEAR_FLOOR | BODE_F_UNSETTLED | BODE_F_INCOHERENT)

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
  int    (*arm)(void *ctx, const bode_plan *pp, uint32 total);             //0 ok, else the FPGA was not ready
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
uint16 bode_switches(const bode_eng *e);
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
//measured point is near the floor or under the noise, the span is kept.
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
  uint16     switches;
  uint32     t0, t_end;
} bode_snap;

void   bode_snap_save(const bode_eng *e, bode_snap *s);
void   bode_snap_restore(bode_eng *e, const bode_snap *s);

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
  uint16    switches;
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

  bode_thru thru;
  int32     work[BODE_MAX_RECORD];
};

#endif /* BODE_SWEEP_H */
