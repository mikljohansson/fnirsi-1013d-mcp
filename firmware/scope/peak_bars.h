//----------------------------------------------------------------------------------------------------------------------------------
// peak_bars.h - a peak-detect record on screen as one vertical bar per column (ui-sample, docs/plans/ui-rows.md 3.3).
//
// A peak record (FPGA 0x153C, SET_DECIM filter 4, fw-c6) holds per sample { max (high byte), min (low byte) } raw codes
// of the window; the processed trace is the corrected midpoint (pc_hal_scope.c pc_glue_decim_read). The Scope view draws
// each column from the lowest min to the highest max of the samples the column covers, walking the record with the
// linear trace's step held as an integer (Q16 samples per column, converted once per pass: no soft-float per column).
// Column c covers samples floor(p) .. floor(p + step) - 1 with p = first + c x step, at least floor(p) (zoomed in, several
// columns show the same sample's bar). The two bytes go through the mean-core full-class correction the midpoint took
// (pc_decim.h pc_peak_code), so the bar brackets the trace; with the channel inverted min and max swap.
//
// Integer only, no globals, no hardware: built for the scope and on the host (host_tests/test_peak_bars.c). The
// drawing (scope_functions.c) and the phosphor map (phosphor.c) take the columns from pb_next; the colour and the
// overload red stay the caller's (ADoverload, set at the read by pc_peak_overload: either byte at 0 or 255).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PEAK_BARS_H
#define PEAK_BARS_H

#include <stdint.h>

#include "phos_map.h"

typedef struct
{
  const uint16_t *pairs;      //the record: (max << 8) | min per sample, raw codes
  uint32_t        n;          //samples in it
  uint32_t        pos_q16;    //the next column's first sample, Q16
  uint32_t        step_q16;   //samples per column, Q16 (>= 1)
  uint32_t        gain;       //pc_peak_gain of the two cores (Q16)
  int32_t         off;        //pc_peak_off
  int             invert;     //the channel is inverted (code 255 - c, as fpga_read_adc_data)
} pb_walk;

//Samples per column in Q16 from the linear trace's step (disp_sample_step x 65536, rounded by the caller), at least 1
uint32_t pb_step_q16(uint32_t samples_per_col_q16);

//Start a walk at sample `first`
void pb_start(pb_walk *w, const uint16_t *pairs, uint32_t n, uint32_t first, uint32_t step_q16, uint32_t gain, int32_t off,
              int invert);

//The next column's corrected codes 0..255: lo from the lowest min byte, hi from the highest max byte (lo <= hi as the
//FPGA's pairs hold max >= min; the drawing orders the two screen rows itself). Returns 0 past the record's end.
int  pb_next(pb_walk *w, int32_t *lo, int32_t *hi);

//The Scope view's drawing: columns x0 .. x1 - 1 from the walk, each code through to_y (the channel's code-to-screen
//mapping) and one vertical run per column from the upper to the lower row (y0 <= y1) through vline. Returns the
//columns drawn (fewer when the record ends first)
typedef struct
{
  int32_t (*to_y)(void *ctx, int32_t code);
  void    (*vline)(void *ctx, int32_t x, int32_t y0, int32_t y1);
  void     *ctx;
} pb_draw_cb;

uint32_t pb_draw(pb_walk *w, int32_t x0, int32_t x1, const pb_draw_cb *cb);

//1 when any pair of the record has a byte at a rail (pc_peak_overload's rule over the record)
int  pb_overload(const uint16_t *pairs, uint32_t n);

//Phosphor (docs/plans/ui-rows.md 3.3, decided 4): add the record's bars to a hit-count map, each column's rows min .. max
//once per record, for map columns x0 .. x1 - 1 (screen x - PHOS_X0) walked from w; lut: code -> map row (phosphor.c's
//table from 1.00o5's mapping). Returns the cells touched.
uint32_t pb_phos_record(phos_map *m, pb_walk *w, const uint16_t *lut, int32_t x0, int32_t x1, uint32_t inc);

#endif
