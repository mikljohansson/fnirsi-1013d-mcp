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
// Training (the calibration wizard's zero step, input parked at code 128 where the most bits flip): every trained core at
// stage s = 2, 3, 4, 5 in turn, one 200 MSa/s capture of CS_TRAIN_WORDS words each, read 0x36: a clean stage reads 0 for
// that core, a stage inside the AD9288 data transition counts about every word. Per core cs_pick takes the clean stage of
// {3, 4} whose outer neighbour (2 or 5) is clean too (the middle of the clean run), else the clean one of {3, 4}, else a
// clean reserve (2, 5). A B core with no clean stage is measured again with its chip's reference bit set (A-encode
// timing); a core with no clean stage at all fails the training (fail fast: the wizard says "ADC readout fault near code
// 128" and nothing is stored).
//
// Self-check after full-rate captures: a signal moves both cores of a chip alike (they sample the same input 5 ns apart),
// a core captured inside its transition counts alone. cs_excess = a core's count minus its chip partner's; above
// CS_EXCESS_LIMIT the core is flagged (Diagnostics, GET_HEALTH, event 23). Inference, not benched: a large signal near
// 100 MHz could make the cores' counts differ by itself.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAP_STAGE_H
#define CAP_STAGE_H

#include <stdint.h>

#define CS_PO             0x08E4      //0x63 at power-on: CH1 A 4, CH1 B 4, CH2 A 3, CH2 B 4, B by its own encode
#define CS_THR_PO         8           //0x64 at power-on (codes)
#define CS_REF_CH1B       0x1000      //0x63 bit 12
#define CS_REF_CH2B       0x2000      //0x63 bit 13
#define CS_MASK           0x3FFF
#define CS_CORES          4           //0 CH1 A, 1 CH1 B, 2 CH2 A, 3 CH2 B (the 0x36 order)
#define CS_STAGE_MIN      2           //stages tried: 3 and 4 are the allowed pair, 2 and 5 the reserves
#define CS_STAGE_MAX      5
#define CS_NONE           0xFF        //cs_pick: no clean stage
#define CS_TRAIN_WORDS    8000        //words per training capture (200 MSa/s: 40 us)
#define CS_EXCESS_LIMIT   8           //self-check: outliers a core may have more than its chip partner

//1 for bitstreams with the capture stage registers (AL3 0x153A..0x153F)
int      cs_has(uint16_t fpga_version);

//Stage of core (CS_CORES order) in a 0x63 value, and the value with it replaced
uint8_t  cs_stage(uint16_t v, uint32_t core);
uint16_t cs_with(uint16_t v, uint32_t core, uint8_t stage);
//The reference bit of a B core (1 CH1 B, 3 CH2 B), 0 for an A core
uint16_t cs_ref_bit(uint32_t core);

//The stage for one core from its clean stages (bit s = stage s read 0 outliers) and its power-on stage
uint8_t  cs_pick(uint8_t clean, uint8_t po);

//What a training needs from the target (or a host mock): write 0x63 = v, one capture, read 0x36's four counts.
//0 = done, else the capture did not complete
typedef struct
{
  void *ctx;
  int  (*measure)(void *ctx, uint16_t v, uint16_t cnt[CS_CORES]);
} cs_io;

#define CS_OK             0
#define CS_E_DIRTY        1           //a core has no clean stage (t->bad says which)
#define CS_E_IO           2           //a capture did not complete

typedef struct
{
  uint16_t start;                     //0x63 value before the training
  uint16_t value;                     //the result (= start unless CS_OK)
  uint8_t  cores;                     //cores trained (bit i = core i)
  uint8_t  clean[CS_CORES];           //bit s: stage s clean with the own-encode timing
  uint8_t  clean_ref[CS_CORES];       //B cores measured again with the reference bit: bit s clean
  uint8_t  bad;                       //cores without a clean stage
  uint8_t  captures;                  //captures taken
} cs_train;

//Train the cores in mask cores (the parked channel: 0x03 CH1, 0x0C CH2); the others keep their value in start
int      cs_train_run(const cs_io *io, uint16_t start, uint8_t cores, cs_train *t);

//Self-check: per core the count minus its chip partner's (0 when below); the cores above CS_EXCESS_LIMIT as a mask
void     cs_excess(const uint16_t cnt[CS_CORES], uint16_t ex[CS_CORES]);
uint8_t  cs_flagged(const uint16_t cnt[CS_CORES]);

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

#endif /* CAP_STAGE_H */
