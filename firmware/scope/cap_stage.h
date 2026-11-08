//----------------------------------------------------------------------------------------------------------------------------------
// cap_stage.h - FPGA 0x153A+ ADC capture stages: training, self-check, the stored value (fw 0.2.79, firmware/AGENTS.md
// sections 54 and 97; fpga/al3/README.md "Changes" sections 10 and 22, docs/protocol.md 5.1). Pure C, no globals,
// host-buildable (host_tests/test_cap_stage.c); the target glue is cap_hw.c.
//
// 0x153A samples every ADC pin in an IOB register on the 200 MHz clock and takes one of six 2.5 ns stages against the
// write clock edge. 0x153A..0x153E took one stage per core; since 0x153F (fpga-153f, README section 22: the AD9288s
// release both output buses of a chip after the same edge, so per-core instants paired the cores one conversion apart
// at most dividers) both cores of a chip are captured at one instant, the chip's stage, used at full rate only (divider
// 0, any filter, peak detect; at the divided rates the instant is fixed). Write 0x63 (two bytes, high first): bits 2:0
// CH1, 8:6 CH2 stage (both cores of the chip). Bits 5:3 and 11:9 (the old B fields) and 13:12 (the old B reference
// bits) select nothing; they are stored and read back through 0x36. The firmware writes the chip's stage into the B
// field too (cs_with_chip), so cs_stage of a B core, the texts "CH1 A3 B3" and the per-core GET_CAL / GET_HEALTH
// fields read the chip's stage, and bits 15:12 = 0 (CS_MASK). Power-on 0x06DB (stage 3 on both chips). Write 0x64:
// outlier threshold, power-on 8 codes. Read 0x36 (10 bytes): the 0x63 value, then 16-bit outlier counts CH1 A, CH1 B,
// CH2 A, CH2 B (still per core: words whose sample differs from the core's previous word by more than the threshold,
// counted while the capture writes, cleared by 0x01 = 1). 0x63 is a plain register (zaklad.v cap_sel_x): it keeps its
// value over sampling resets, so it is written at boot (a FEL load leaves the FPGA as it was) and after a training only.
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
// Per chip (fw-153f): cs_pass captures both cores of a trained chip at stage s, the sums, q and the quiet sets stay per
// core (GET_CAL 364..431), and the chip's set is the intersection quiet[A] & quiet[B]: a stage is usable when both
// cores are quiet at it. Pick and tie pass run on it (GET_CAL reports it for both cores). A chip
// with no common quiet stage fails the training (CS_E_DIRTY; t->bad: the cores without a quiet stage of their own, or
// both when each has some and none is shared). The reference pass (a B core with no clean stage measured again with
// its chip's bit 12 / 13) is gone with the B timing it selected.
//
// Noise bound (fw-capmetric2): the quiet rule is relative, so data that are wrong alike at every stage (bench
// 2026-10-22 17:20: the residual read stale ring words, q16 about 43000 per capture on the A cores and 240..265 on the
// B cores, flat over the stages) passed it. A trained core's best outlier-clean stage must score at most
// CS_RESID_MAX_Q16 per capture, else the training fails with CS_E_RESID (nothing stored, the wizard says so). For noise
// of sigma codes rms the median-3 residual gives q16 = 16 E / N of about 12.8 sigma^2 (simulated, 8-bit rounding, sigma
// >= 1; 5.1 at 0.5): the zero step's full-rate limit, 3 codes rms (ACAL_MAX_NOISE16_FULL), is 115, and 128 is sigma =
// 3.2. Bench (capwin sweep, 200 MSa/s): 3.8..3.9 at CH1's clean stages, 18..19 at CH2 B's (1.1 codes rms), so a working
// unit stays 7x below it.
//
// Pick (cs_pick, fw 0.2.105): per chip since 0x153F, among its clean stages, the one with the highest score: the width
// of its run of adjacent clean stages, then 3 or 4 before a reserve (2, 5), then the distance to the nearer end of its
// run. A run of odd width has one centre. A run of even width has two centres with the same score, and the power-on
// stage is no tie-break any more (0.2.81 took it, and the power-on CH1 A4 was the stage that garbled). "The centre
// whose outer neighbour is clean too" cannot separate them on a widest run: in a run of 4 both outer neighbours (2, 5)
// are clean, in a run of 2 both are outside the run (dirty or untried). So evidence decides, the tie pass: every chip
// whose pick is such a tie (cs_tie: the two stages) is captured CS_TIE_REPS more times at each of the two stages at the
// band's CS_TIE_POINTS centre points (codes 126, 128, 130, where 0x7F / 0x80 flips every bit; the chips without a tie
// at their pick meanwhile), and a stage that counts there on either core is dirty for the chip: the pick is made again
// on the smaller clean set. When both stay clean the lower stage is taken (the earlier sample; the only stage seen to
// garble beside clean ones on this design was the upper centre, CH1 A4, on 0x153A and 0x153D: inference from one unit).
// 2..5 clean -> 3 unless 3 counts in the tie pass; {2,3,5} -> 3, {2,3,4} -> 3, {3,4,5} -> 4, {3,4} -> 3, {4,5} -> 4,
// {2,5} -> 2.
//
// A chip with no common clean stage fails the training (fail fast: the wizard names the cores and nothing is stored).
// The chip's set (cs_train.set, both cores, after the tie pass) is reported (GET_CAL 364..431), not stored.
//
// Self-check after full-rate captures, a diagnostic only (fw-csbusy, owner 2026-11-08: "Only the calibration dialog
// should write calibration data"): a core captured inside its transition garbles alone near 0x7F / 0x80 and counts tens
// to hundreds of outliers while its chip partner, sampling the same input 5 ns apart, counts 0 or a few. That is the
// only signature this check flags: cs_flagged = a core whose partner counted at most CS_QUIET_PARTNER and whose count
// exceeds the partner's by more than CS_EXCESS_LIMIT. A chip whose two cores both count more than CS_QUIET_PARTNER
// (cs_busy) is not checkable: the signal itself crosses the threshold and the cores' gain mismatch (about 2 %) makes their
// counts differ by itself. Bench: 2026-11-08 fw 0.3.1, a 24.576 MHz clock on CH1, both cores about 43000, 11087 of 19060
// captures flagged by the old absolute limit; 354e676's relative limit (max(8, partner / 8), abstain over 1024) still
// flagged a 12.5 MHz square at 100 mV/div AC (92 codes pk-pk) 5 times in 5 s. The bench log since 0x153F has 0 genuine
// flags (0 of 885, 7562 and 35703 captures). 0x153F: a chip stage inside a transition both buses share garbles both cores
// alike, which no partner rule flags (the training's absolute counts and q catch it).
//
// The self-check's threshold (fw-genlimit): the counters count jumps over 0x64 codes, so a clean signal whose per-core
// steps (10 ns apart) cluster at the threshold splits by noise and the cores' gain mismatch: one core counts, its partner
// hardly. Bench 2026-11-08 19:41 (735d82a): 12.5 MHz from the calibration post at 100 mV/div AC (92 codes pk-pk) steps
// 28..36 codes per core around 32; CH1 B counted 9..27 against A's 0..1 and 17 of 305 captures were flagged, while both
// cores folded to about 1 code rms (max 4). So the self-check counts at CS_THR_USE 48 and the training keeps 32
// (cap_hw.c writes 0x64 = 32 around cs_train_run and 48 back after it; nothing else writes 0x64 but DEBUG_FPGA). Why 48:
// the 2026-10-20 sweep read 16..48 alike (0 on a clean stage, hundreds or 65535 on a bad one), so a garble still counts;
// the garbles are rails and jumps of tens to 128 codes at 0x7F / 0x80 (a bitwise mix of 0x7F and 0x80 is at least 48
// codes off in about 5 of 8 words against 6 of 8 at 32: inference, bits drawn alike), and the bench signal's largest step
// plus noise (36 + 4) stays 8 codes below it. A clean signal stepping about 48 codes per core (the same square near 120
// codes pk-pk) can still be flagged: a diagnostic only, as above.
//
// What a flag does: GET_HEALTH 106 bit 3 / 107 / 120, Diagnostics; a core flagged in CS_REPEAT_N captures within
// CS_REPEAT_MS (cs_watch) sets REPEAT (the Diagnostics Calibration verdict and GET_HEALTH ask for Calibrate) and logs
// event 23 once. Nothing acts on it: the runtime guard of fw 0.2.105 (moved a flagged chip inside its stored clean set
// and wrote sector 708) is gone with the stored sets, and the firmware never changes 0x63 from the self-check. Every
// guard move on the owner's unit since 0x153F was a false alarm.
//
// Ownership (fw-guardfix, bench 2026-10-22): the self-check reads the 0x63 value the FPGA holds with the counts (0x36's
// first two bytes). A capture taken at a value the firmware did not write (DEBUG_FPGA: fpga/al3/tools/capwin_sweep.py,
// a manual sweep) still updates the counts, but it does not feed cs_watch (no REPEAT) and nothing is logged.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAP_STAGE_H
#define CAP_STAGE_H

#include <stdint.h>

#define CS_PO             0x06DB      //0x63 at power-on (0x153F): stage 3 on both chips, the B fields 3 too
#define CS_THR_PO         8           //0x64 at power-on (codes)
#define CS_THR_TRAIN      32          //0x64 while the wizard trains (since 0.2.80): CH2's analog noise at range 0 near
                                      //code 128 is about 3 codes rms and saturates the counters at 8; 16, 24, 32, 40 and
                                      //48 all read 0 on a clean stage and hundreds (CH1 A4) or 65535 (CH2 B2) on a bad
                                      //one (bench 2026-10-20 10:08-10:14)
#define CS_THR_USE        48          //0x64 for the self-check after full-rate captures (fw-genlimit; 32 up to 0.3.1):
                                      //the highest benched value of that sweep, and clear of a clean edge's steps
                                      //("Self-check" below: a 12.5 MHz square stepping 28..36 codes per core)
#define CS_MASK           0x0FFF      //0x63 as the firmware writes and compares it (bits 15:12 select nothing, written 0)
#define CS_TRAILER_MASK   0x3FFF      //the stored trailer's value field (0x153A..0x153E's reference bits: format only)
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
#define CS_RESID_MAX_Q16  128         //fw-capmetric2: the most a core's best quiet-input stage may score per capture
                                      //(1/16 code^2 per sample; below "Noise bound"), else CS_E_RESID
#define CS_TIE_REPS       16          //tie pass: captures per candidate stage and point
#define CS_STAGES         (CS_STAGE_MAX - CS_STAGE_MIN + 1)
#define CS_EXCESS_LIMIT   8           //self-check: outliers a core may have more than its quiet chip partner
#define CS_QUIET_PARTNER  8           //fw-csbusy: the most a partner may count for the check (else not checkable)

//Stage of core (CS_CORES order) in a 0x63 value, and the value with it replaced (one field); cs_with_chip: both fields
//of chip ch (0 CH1, 1 CH2), as the firmware writes every value since 0x153F
uint8_t  cs_stage(uint16_t v, uint32_t core);
uint16_t cs_with(uint16_t v, uint32_t core, uint8_t stage);
uint16_t cs_with_chip(uint16_t v, uint32_t ch, uint8_t stage);

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

//The smallest q of the stages with sum 0 (index s - CS_STAGE_MIN); 0xFFFFFFFF when no stage has sum 0
uint32_t cs_q_best(const uint32_t sum[CS_STAGES], const uint32_t q[CS_STAGES]);

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
#define CS_E_DIRTY        1           //a chip has no stage clean on both cores (t->bad says which cores)
#define CS_E_IO           2           //a capture did not complete
#define CS_E_RESID        3           //fw-capmetric2: a core's best stage is noisier than a quiet input can be (t->bad)

typedef struct
{
  uint16_t start;                     //0x63 value before the training
  uint16_t value;                     //the result (= start unless CS_OK)
  uint8_t  cores;                     //cores trained (bit i = core i; whole chips: 0x03, 0x0C)
  uint8_t  clean[CS_CORES];           //bit s: the core counted 0 outliers with its chip at stage s
  uint8_t  quiet[CS_CORES];           //fw-capmetric: bit s: stage s quiet for the core (cs_quiet)
  uint8_t  bad;                       //cores of a chip without a common quiet stage (CS_E_DIRTY), over the noise
                                      //bound (CS_E_RESID)
  uint16_t captures;                  //captures taken
  uint32_t sum[CS_CORES][CS_STAGES];  //0x36 counts summed over the band (index s - CS_STAGE_MIN)
  uint32_t q[CS_CORES][CS_STAGES];    //fw-capmetric: sum of E / CS_RESID_DIV over the band
  uint8_t  tie[CS_CORES];             //tie pass: the chip's two stages measured (bits, both cores; 0 = no tie)
  uint32_t tie_sum[CS_CORES][2];      //their counts summed per core, lower stage first
  uint8_t  set[CS_CORES];             //the chip's set after the tie pass, the same in both cores (GET_CAL)
} cs_train;

//Train the chips of the cores in mask cores (the parked channel: 0x03 CH1, 0x0C CH2; one core names its chip); the
//other chip keeps its value in start
int      cs_train_run(const cs_io *io, uint16_t start, uint8_t cores, cs_train *t);

//Self-check: per core the count minus its chip partner's (0 when below); cs_flagged: the cores whose partner counted at
//most CS_QUIET_PARTNER and whose excess is above CS_EXCESS_LIMIT, as a mask; cs_busy: both cores of every chip whose
//cores both count more than CS_QUIET_PARTNER (not checkable; "Self-check" above)
void     cs_excess(const uint16_t cnt[CS_CORES], uint16_t ex[CS_CORES]);
uint8_t  cs_flagged(const uint16_t cnt[CS_CORES]);
uint8_t  cs_busy(const uint16_t cnt[CS_CORES]);

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

//Texts (no libc), each returns the pointer to its NUL: "CH1 A3 B3" (ch 0 / 1; the two fields as stored, equal in
//every value the firmware writes since 0x153F), "CH1 A3 B3, CH2 A3 B3", the core name "CH2 A"
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

//Bytes 504..511 held the trained clean sets for the runtime guard (fw 0.2.105 .. 354e676); fw-csbusy neither reads nor
//writes them, and its calibration save leaves them 0 (an older firmware then finds no sets and has no guard)

#endif /* CAP_STAGE_H */
