//----------------------------------------------------------------------------------------------------------------------------------
// cap_stage.h - FPGA 0x153A ADC capture stages: training, self-check, the stored value (fw 0.2.79, firmware/AGENTS.md
// section 54; fpga/al3/README.md "Changes" section 10, docs/protocol.md 5.1). Pure C, no globals, host-buildable
// (host_tests/test_cap_stage.c); the target glue is cap_hw.c.
//
// 0x153A samples every ADC pin in an IOB register on the 200 MHz clock and takes, per core, one of six 2.5 ns stages
// against the write clock edge that core is encoded on. Write 0x63 (two bytes, high first): bits 2:0 CH1 A, 5:3 CH1 B,
// 8:6 CH2 A, 11:9 CH2 B stage; bit 12 / 13 = CH1 B / CH2 B timed by the chip's A encode (AD9288 data-align mode).
// Power-on 0x08E4. Write 0x64: outlier threshold, power-on 8 codes. Read 0x36 (10 bytes): the 0x63 value, then 16-bit
// outlier counts CH1 A, CH1 B, CH2 A, CH2 B (words whose sample differs from the core's previous word by more than the
// threshold, counted while the capture writes, cleared by 0x01 = 1). 0x63 is a plain register (zaklad.v cap_sel_x): it
// keeps its value over sampling resets, so it is written at boot (a FEL load leaves the FPGA as it was) and after a
// training only.
//
// Training (the calibration wizard's zero step; fw 0.2.81 band training, fw 0.2.105 denser band and tie pass): the quiet
// input is parked by the offset PWM at CS_BAND_POINTS codes where many bits flip (the carries since fw-capmetric, below;
// cs_band_pwm from the zero step's gated 2 MSa/s slope points), and at every point every trained core is captured at stage
// s = 2, 3, 4, 5 in turn, CS_BAND_REPS 200 MSa/s captures of CS_TRAIN_WORDS words each; the 0x36 counts are summed per
// core and stage over all points and captures. A stage is clean when its sum is 0: a stage inside the AD9288 data
// transition counts at once, one at its edge garbles only at some codes and in some captures (bench 2026-10-20: CH1 A4 at
// codes 127..135 only), which 0.2.79's one capture at one code missed and 0.2.81's 6 points x 2 captures missed too
// (bench 2026-10-22 on 0x153D: 2..5 all clean, 4 picked, and 4 garbles a real signal: 146 outliers in 8 captures, the
// counter at twice the frequency; 2, 3 and 5 count 0 over 30 captures from 1 kHz to 7.3 MHz).
//
// Quiet stages (fw-capmetric, fpga/al3/README.md section 18 "Firmware metric for the training and the guard"): the 0x36
// counters see only jumps over 32 codes, but the capture errors next to a clean stage are bitwise mixes of two
// neighbouring codes at a carry (0x5F / 0x60 -> 0x6E, 0x97 / 0x98 -> 0x9F), a few codes off in a few % of the samples
// (sweep 2026-10-22 11:23: CH1 A5 and CH2 A4 counted 0 on the quiet input and garbled). So the band parks the input at
// the carries, codes 8k - 0.5 for k = 8 .. 24 (63.5 .. 191.5; cs_band_code16 in 1/16 code), where the noise flips 4 or
// more bits, CS_BAND_REPS 2 captures per stage and point, and after each capture the trained cores' CS_RESID_WORDS words
// from the record's middle are read (READ_RAW's path) and scored by cs_resid: the sum of the squared differences to the
// 3-sample median, E. Per core and stage q = the sum over points and captures of E / CS_RESID_DIV (1/16 code^2 per
// sample). A stage is quiet (cs_quiet) when its 0x36 sum is 0 (the outlier rule stays the first gate) and q <= the
// smallest q of the core's outlier-clean stages x 5 / 4 + 4. Pick, tie pass and the stored set run on the quiet set; a
// stage-independent error (the ADC's own, class b of the sweep) raises every stage's q alike and changes nothing. The
// tie pass is unchanged (0x36 counts at codes 126, 128, 130).
//
// Pick (cs_pick, fw 0.2.105): per core, among its clean stages, the one with the highest score: the width of its run of
// adjacent clean stages, then 3 or 4 before a reserve (2, 5), then the distance to the nearer end of its run. A run of
// odd width has one centre. A run of even width has two centres with the same score, and the power-on stage is no
// tie-break any more (0.2.81 took it, and the power-on CH1 A4 was the stage that garbled). "The centre whose outer
// neighbour is clean too" cannot separate them on a widest run: in a run of 4 both outer neighbours (2, 5) are clean, in
// a run of 2 both are outside the run (dirty or untried). So evidence decides, the tie pass: every core whose pick is
// such a tie (cs_tie: the two stages) is captured CS_TIE_REPS more times at each of the two stages at the band's
// CS_TIE_POINTS centre points (codes 126, 128, 130, where 0x7F / 0x80 flips every bit; the cores without a tie at their
// pick meanwhile), and a stage that counts there is dirty: the pick is made again on the smaller clean set. When both
// stay clean the lower stage is taken (the earlier sample; the only stage seen to garble beside clean ones on this
// design was the upper centre, CH1 A4, on 0x153A and 0x153D: inference from one unit). 2..5 clean -> 3 unless 3 counts
// in the tie pass; {2,3,5} -> 3, {2,3,4} -> 3, {3,4,5} -> 4, {3,4} -> 3, {4,5} -> 4, {2,5} -> 2.
//
// A B core with no clean stage is measured again with its chip's reference bit set (A-encode timing); a core with no
// clean stage at all fails the training (fail fast: the wizard says "ADC readout fault near code 128" and nothing is
// stored). The clean set of the timing in use (cs_train.set, after the tie pass) is stored with the value (cs_ext_*): it
// bounds the runtime guard.
//
// Self-check after full-rate captures: a signal moves both cores of a chip alike (they sample the same input 5 ns apart),
// a core captured inside its transition counts alone. cs_excess = a core's count minus its chip partner's; above
// CS_EXCESS_LIMIT the core is flagged (Diagnostics, GET_HEALTH, event 23). Inference, not benched: a large signal near
// 100 MHz could make the cores' counts differ by itself.
//
// Runtime guard (fw 0.2.105, cs_guard; cap_hw.c): every full-rate capture (200 MSa/s, any filter, peak detect) and the
// counter need all four cores clean, so a stage the quiet training passed but a real signal garbles cannot wait
// for the next calibration. A core flagged in CS_REPEAT_N captures within CS_REPEAT_MS (cs_watch) while the trained value
// of this bitstream is in effect and its stored clean set is known is moved, at most once per boot: its stage is removed
// from the stored clean set and cs_pick chooses again from what is left. So it never leaves the trained set, and the set
// only shrinks: it cannot oscillate, also not over boots. The new value is written to 0x63, stored in sector 708 at once
// (MAIN context, fpga_capture_service) and logged (event 23 a = 0x20 | the moved cores, b = the new value). A core with
// nothing left keeps its stage and asks for Calibrate (REPEAT) as before.
//
// Ownership (fw-guardfix, bench 2026-10-22): the self-check reads the 0x63 value the FPGA holds with the counts (0x36's
// first two bytes). A capture taken at a value the firmware did not write (DEBUG_FPGA: fpga/al3/tools/capwin_sweep.py,
// a manual sweep) still updates the counts, but it neither feeds cs_watch (no REPEAT) nor reaches the guard, and nothing
// is logged; the guard also acts only while the FPGA holds the stored value of this bitstream. Whoever wrote 0x63 owns
// the stages until it holds the firmware's value again.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAP_STAGE_H
#define CAP_STAGE_H

#include <stdint.h>

#define CS_PO             0x08E4      //0x63 at power-on: CH1 A 4, CH1 B 4, CH2 A 3, CH2 B 4, B by its own encode
#define CS_THR_PO         8           //0x64 at power-on (codes)
#define CS_THR_USE        32          //0x64 as the firmware writes it: CH2's analog noise at range 0 near code 128 is
                                      //about 3 codes rms and saturates the counters at 8; 16..48 all read 0 on a clean
                                      //stage and hundreds on a bad one (bench 2026-10-20 10:14)
#define CS_REF_CH1B       0x1000      //0x63 bit 12
#define CS_REF_CH2B       0x2000      //0x63 bit 13
#define CS_MASK           0x3FFF
#define CS_CORES          4           //0 CH1 A, 1 CH1 B, 2 CH2 A, 3 CH2 B (the 0x36 order)
#define CS_STAGE_MIN      2           //stages tried: 3 and 4 are the allowed pair, 2 and 5 the reserves
#define CS_STAGE_MAX      5
#define CS_NONE           0xFF        //cs_pick: no clean stage
#define CS_TRAIN_WORDS    8000        //words per training capture (200 MSa/s: 40 us)
#define CS_BAND_POINTS    17          //training: offset PWM points at the carries 8k - 0.5, k = CS_BAND_K0 .. 24
                                      //(cs_band_code16; fw-capmetric, 11 points 120..140 before)
#define CS_BAND_K0        8           //the first point's k: code 63.5
#define CS_BAND_REPS      2           //training: captures per stage and point (4 before fw-capmetric)
#define CS_TIE_CODE       126         //tie pass: codes CS_TIE_CODE, + 2, + 4 (126, 128, 130), from the top down
#define CS_TIE_POINTS     3
#define CS_RESID_WORDS    2048        //training: words per core read for cs_resid, from the record's middle
#define CS_RESID_DIV      ((CS_RESID_WORDS - 2) / 16)   //q per capture = E / CS_RESID_DIV (N / 16, N = n - 2)
#define CS_TIE_REPS       16          //tie pass: captures per candidate stage and point
#define CS_STAGES         (CS_STAGE_MAX - CS_STAGE_MIN + 1)
#define CS_EXCESS_LIMIT   8           //self-check: outliers a core may have more than its chip partner

//Stage of core (CS_CORES order) in a 0x63 value, and the value with it replaced
uint8_t  cs_stage(uint16_t v, uint32_t core);
uint16_t cs_with(uint16_t v, uint32_t core, uint8_t stage);
//The reference bit of a B core (1 CH1 B, 3 CH2 B), 0 for an A core
uint16_t cs_ref_bit(uint32_t core);

//The stage for one core from its clean stages (bit s = stage s summed 0 outliers): the widest run of clean stages, then
//3 or 4 before a reserve, then the distance to the nearer end of the run, then the lower stage. CS_NONE without a clean
//stage. cs_tie: the stages (bits) that score equal at the top when there are two or more (the tie pass measures them),
//else 0
uint8_t  cs_pick(uint8_t clean);
uint8_t  cs_tie(uint8_t clean);

//Training band: the code of point i in 1/16 code ((CS_BAND_K0 + i) x 8 - 0.5: 1016, 1144, .. 3064) and the offset PWM
//that parks the input at code16, interpolated from two gated points (pwm_lo reads mean16_lo, pwm_hi reads mean16_hi,
//1/16 code; a lower PWM reads a higher code). -1 when the points do not respond (mean16_lo <= mean16_hi or
//pwm_hi <= pwm_lo)
uint32_t cs_band_code16(uint32_t i);
int32_t  cs_band_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi, uint32_t code16);

//Residual of one core's capture: for j = 1 .. n - 2, d = x[j] - median(x[j - 1], x[j], x[j + 1]); the sum of d^2 (at
//most 4094 x 65025 for n = 4096). 0 for n < 3
uint32_t cs_resid(const uint8_t *x, uint32_t n);

//The quiet stages of one core (bit s = stage s) from its 0x36 sums and q per stage (index s - CS_STAGE_MIN): sum 0 and
//q <= q_min x 5 / 4 + 4, q_min the smallest q of the stages with sum 0. 0 when no stage has sum 0
uint8_t  cs_quiet(const uint32_t sum[CS_STAGES], const uint32_t q[CS_STAGES]);

//What a training needs from the target (or a host mock): park the input at code16 (1/16 code: the offset PWM and its
//settling), and one capture: write 0x63 = v, capture, read 0x36's four counts and, for every core in resid (a mask),
//cs_resid of its CS_RESID_WORDS words from the record's middle into e[core] (the others untouched). 0 = done, else the
//step did not complete
typedef struct
{
  void     *ctx;
  uint32_t points;                    //band points (CS_BAND_POINTS)
  uint32_t reps;                      //captures per stage and point (CS_BAND_REPS)
  uint32_t tie_reps;                  //tie pass: captures per candidate stage and point (CS_TIE_REPS; 0 = no tie pass)
  int      (*park)(void *ctx, uint32_t code16);
  int      (*measure)(void *ctx, uint16_t v, uint8_t resid, uint16_t cnt[CS_CORES], uint32_t e[CS_CORES]);
} cs_io;

#define CS_OK             0
#define CS_E_DIRTY        1           //a core has no clean stage (t->bad says which)
#define CS_E_IO           2           //a capture did not complete

typedef struct
{
  uint16_t start;                     //0x63 value before the training
  uint16_t value;                     //the result (= start unless CS_OK)
  uint8_t  cores;                     //cores trained (bit i = core i)
  uint8_t  clean[CS_CORES];           //bit s: stage s counted 0 outliers with the own-encode timing
  uint8_t  clean_ref[CS_CORES];       //B cores measured again with the reference bit: bit s clean
  uint8_t  quiet[CS_CORES];           //fw-capmetric: bit s: stage s quiet (cs_quiet), own-encode timing
  uint8_t  quiet_ref[CS_CORES];       //the same for the reference pass
  uint8_t  ref;                       //cores measured in the reference pass
  uint8_t  bad;                       //cores without a quiet stage
  uint16_t captures;                  //captures taken
  uint32_t sum[CS_CORES][CS_STAGES];      //0x36 counts summed over the band, own-encode timing (index s - CS_STAGE_MIN)
  uint32_t sum_ref[CS_CORES][CS_STAGES];  //the same for the reference pass (its B cores only)
  uint32_t q[CS_CORES][CS_STAGES];        //fw-capmetric: sum of E / CS_RESID_DIV over the band, own-encode timing
  uint32_t q_ref[CS_CORES][CS_STAGES];    //the same for the reference pass
  uint8_t  tie[CS_CORES];             //tie pass: the two stages measured (bits; 0 = no tie)
  uint32_t tie_sum[CS_CORES][2];      //their counts summed, lower stage first
  uint8_t  set[CS_CORES];             //the quiet stages of the timing in use after the tie pass (the guard's bound)
} cs_train;

//Train the cores in mask cores (the parked channel: 0x03 CH1, 0x0C CH2); the others keep their value in start
int      cs_train_run(const cs_io *io, uint16_t start, uint8_t cores, cs_train *t);

//Self-check: per core the count minus its chip partner's (0 when below); the cores above CS_EXCESS_LIMIT as a mask
void     cs_excess(const uint16_t cnt[CS_CORES], uint16_t ex[CS_CORES]);
uint8_t  cs_flagged(const uint16_t cnt[CS_CORES]);

//In operation (fw 0.2.81): a core the self-check flags in CS_REPEAT_N captures within CS_REPEAT_MS keeps failing at its
//trained stage (the Diagnostics Calibration verdict asks for Calibrate, event 23 once). cs_watch_add takes the flagged
//mask of one capture and the ms tick and returns the cores that reached it with this capture (each once, until
//cs_watch_clear: a new trained value saved)
#define CS_REPEAT_N       3
#define CS_REPEAT_MS      60000u

typedef struct
{
  uint32_t t[CS_CORES][CS_REPEAT_N - 1];   //ms ticks of the core's last flags, oldest first
  uint8_t  n[CS_CORES];                    //of them valid
  uint8_t  repeat;                         //cores that reached it
} cs_watch;

void     cs_watch_clear(cs_watch *w);
uint8_t  cs_watch_add(cs_watch *w, uint8_t flagged, uint32_t now_ms);
void     cs_watch_forget(cs_watch *w, uint8_t cores);   //the cores' history and repeat bit (the guard moved them)

//Clean sets of the four cores in 16 bits: core k's stages 2..5 in bits 4k .. 4k + 3
uint16_t cs_sets_pack(const uint8_t set[CS_CORES]);
uint8_t  cs_sets_core(uint16_t sets, uint32_t core);    //as a cs_pick mask (bit s = stage s)
uint16_t cs_sets_with(uint16_t sets, uint32_t core, uint8_t set);

//Runtime guard: every core in cores whose stage in *value is in its clean set (in *sets) gets that stage removed from
//the set and cs_pick's choice from the rest; a core with nothing left is not moved (its set unchanged). Returns the
//cores moved; *value and *sets updated
uint8_t  cs_guard(uint16_t *value, uint16_t *sets, uint8_t cores);

//Texts (no libc), each returns the pointer to its NUL: "CH1 A4 B4" (ch 0 / 1; "B4 A-clk" with the reference bit),
//"CH1 A4 B4, CH2 A3 B4", the core name "CH2 A"
char    *cs_text_ch(char *p, uint16_t v, uint32_t ch);
char    *cs_text(char *p, uint16_t v);
char    *cs_core_name(char *p, uint32_t core);

//Stored with the calibration: sector 708 bytes 488..503, after the provenance trailer and outside the v3 block, so the
//zero / gain data and their version stay as 0.2.45+ read them (an older firmware's 708 save clears it, as the
//provenance): u32 'ACAP', u16 version 1, u16 length 16, u16 the 0x63 value, u16 the FPGA version word it was trained on,
//u32 zlib.crc32 of the 12 bytes before
#define CS_TRAILER_OFFSET 488
#define CS_TRAILER_BYTES  16
#define CS_TRAILER_MAGIC  0x50414341u   //"ACAP"
#define CS_T_NONE         0
#define CS_T_VALID        1
#define CS_T_BAD          2           //magic present, version, length or CRC wrong

void     cs_trailer_pack(uint16_t v, uint16_t fpga, uint8_t *out);
int      cs_trailer_unpack(const uint8_t *in, uint16_t *v, uint16_t *fpga);   //CS_T_*; *v, *fpga 0 unless VALID

//fw 0.2.105: the clean sets after it, sector 708 bytes 504..511 (the trailer stays version 1, so 0.2.79+ read it as
//before; their saves clear this): u16 the sets (cs_sets_pack), u16 0, u32 zlib.crc32 of bytes 488..499 followed by
//504..507 (the trailer without its CRC, then the sets: they belong to that trailer only). sector = the whole 512-byte
//sector; unpack 1 = valid (*sets 0 otherwise)
#define CS_EXT_OFFSET     504
#define CS_EXT_BYTES      8

void     cs_ext_pack(uint8_t *sector, uint16_t sets);
int      cs_ext_unpack(const uint8_t *sector, uint16_t *sets);

#endif /* CAP_STAGE_H */
