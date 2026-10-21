//----------------------------------------------------------------------------------------------------------------------------------
// roll.h - the roll mode's trace from the 0x153D streaming logger (fw-roll; owner decision 2026-10-21: the long time bases
// draw real decimated samples of both channels from the logger's stream instead of averaging live ADC pin peeks, which
// the 0x153C bitstream removed with reads 0x24..0x27). Pure C, no hardware, no globals: roll_view.c keeps one model for
// the Scope view, host_tests/test_roll.c drives it with synthetic words and with fpga_model.c's stream.
//
// The stream (fpga_log.c, pc_hal_scope.c pc_glue_roll_*): legacy 8-bit layout (K = 0), both channels, 4 bytes per FPGA
// word in the logger's wire order CH1 core B, CH1 core A, CH2 core B, CH2 core A (core B is the earlier sample). The
// divider is chosen per time/div (roll_plan) so that a screen column (50 px per division, 1.00o5's roll geometry) is a
// whole number of words: column period = W words x 2 (D + 1) x 5 ns.
//
// A column is the mean of its 2 W samples per channel, each corrected per core as the capture readout corrects them
// (code = (raw x gain + off) / 65536, adc_cal.h), rounded, limited to 0..255, mirrored when the channel is inverted: a
// boxcar over the column, so the trace is the signal averaged over the column's time, not a sample of it.
//
// The trace: up to ROLL_LEN columns per channel, oldest first (1.00o5's roll record length). Auto: the columns are added
// for ever, the oldest dropped once ROLL_LEN are held. Single / Normal (1.00o5's roll trigger kept, on the stream): no
// column is kept until the trigger channel's column crosses the trigger level in the trigger edge's direction (the level
// code fpga_set_trigger_level computes from the pointer for the short time bases, compared with the columns as drawn,
// inversion included); the crossing column is the first point; after ROLL_LEN columns Single ends (ROLL_DONE) and Normal starts over (empty
// record, waiting again).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ROLL_H
#define ROLL_H

#include <stdint.h>

#define ROLL_LEN          3000      //columns held per channel
#define ROLL_TB_COUNT     9         //roll time bases: timeperdiv 0 (50 s/div) .. 8 (100 ms/div)
#define ROLL_PX_PER_DIV   50        //screen columns per division
#define ROLL_WORD_BYTES   4         //CH1 B, CH1 A, CH2 B, CH2 A

//Trigger modes (scopesettings.triggermode)
#define ROLL_AUTO         0
#define ROLL_SINGLE       1
#define ROLL_NORMAL       2

//State
#define ROLL_WAIT         0         //Single / Normal: waiting for the crossing
#define ROLL_RUN          1         //columns are added
#define ROLL_DONE         2         //Single: the record is full

//roll_feed events
#define ROLL_EV_TRIG      0x01      //the trigger crossing was found
#define ROLL_EV_DONE      0x02      //Single: the record is full (the rest of the words is ignored)
#define ROLL_EV_RESTART   0x04      //Normal: the record was full and starts over (waiting)

#define ROLL_NO_TRIGGER   0xFF      //trig_ch: no trigger channel in the stream (EXT): the model runs as Auto

//Per channel: the readout's correction of the two cores (fpga_prepare_correction's fields) and the inversion
typedef struct
{
  int32_t gain_a, gain_b;           //Q16
  int32_t off_a, off_b;             //Q16 codes
  uint8_t invert;
} roll_corr;

typedef struct
{
  uint8_t *buf[2];                  //CH1, CH2 columns (ROLL_LEN bytes each), oldest first
  uint32_t count;                   //columns held
  uint32_t wpc;                     //words per column
  uint8_t  mode;                    //ROLL_AUTO / _SINGLE / _NORMAL
  uint8_t  trig_ch;                 //0 CH1, 1 CH2, ROLL_NO_TRIGGER
  uint8_t  edge;                    //0 rising, 1 falling
  uint8_t  state;
  int32_t  level;                   //trigger level, trace code
  int32_t  prev;                    //the trigger channel's previous column (-1: none since the start or a gap)
  uint32_t nw;                      //words in the column being built
  uint32_t sum[2][2];               //[channel][0 core A, 1 core B] raw sums of that column
  uint32_t columns;                 //columns completed since the reset (kept or not)
  uint32_t gaps;                    //gaps seen (a partial column dropped)
} roll_model;

//The stream for a roll time base: the 0x0D divider D and the words per column W. 0 done, -1 not a roll time base
int      roll_plan(uint8_t timeperdiv, uint32_t *divider, uint32_t *wpc);

//Start a record: empty, the trigger state for the mode. trig_ch ROLL_NO_TRIGGER (or mode Auto) runs at once
void     roll_reset(roll_model *m, uint8_t *buf1, uint8_t *buf2, uint32_t wpc, uint8_t mode, uint8_t trig_ch, uint8_t edge);

//Words of the stream, oldest first (ROLL_WORD_BYTES each). gap: words are missing before these (the logger's gap flag):
//the column being built is dropped and the trigger needs two new columns. The trigger level is m->level (the caller
//keeps it current). Returns ROLL_EV_* of this call; *added = columns kept
uint32_t roll_feed(roll_model *m, const uint8_t *words, uint32_t n, int gap, const roll_corr corr[2], uint32_t *added);

//One column's code from its sums (n samples per core), exposed for the tests
int32_t  roll_code(uint32_t sum_a, uint32_t sum_b, uint32_t n, const roll_corr *c);

#endif /* ROLL_H */
