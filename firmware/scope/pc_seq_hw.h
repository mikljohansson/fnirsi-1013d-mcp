//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq_hw.h - the 0x1536 sequencer on the scope: FPGA register writes and reads (pc_hal.h sequencer functions) and the
// UI's hardware averaging (the acquisition menu's Average). Target only; the maths is pc_seq.c.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_SEQ_HW_H
#define PC_SEQ_HW_H

#include "types.h"
#include "pc_seq.h"
#include "pc_decim.h"

void   pc_seq_hw_init(int has);                 //pc_glue_init: the bitstream has the sequencer (caps SEQ)

//Before 0x01 = 1 (pc_hal_arm, fpga_do_conversion): 0x0B, 0x0C and the sequencer registers, so that the write-clock side
//loads them during the reset at every rate; c = NULL or mode 0: 0x57 = 0 when the FPGA may still hold another mode
void   pc_seq_hw_pre_reset(const pc_seq_cfg *c, uint16 pre_words, uint16 total_words);

//UI acquisition (pc_hal_scope.c glue)
int    pc_seq_ui_conversion(int ui, const pc_decim_cfg *c, uint32 divider);  //1: this conversion is a hardware run
void   pc_seq_ui_pre_reset(void);              //fpga_do_conversion, before the sampling reset
void   pc_seq_ui_service(void);                //main loop: the run's deadline
void   pc_seq_ui_done(uint8_t *triggered);     //0x14 read of a finished UI conversion: records counted
int    pc_seq_ui_feed(void *settings, int16 *feed, uint32 n);   //fpga_read_sample_data: averaged codes, 1 = fed
void   pc_seq_ui_read_done(void);              //UI readout of the conversion finished
void   pc_seq_trace_replaced(void);            //a host capture was built into the trace buffers
int    pc_seq_sw_average(void);                //0 while the traces hold a hardware average (no software average)
int    pc_seq_ui_running(void);                //the UI conversion in flight (or just done) is an averaging run

#endif /* PC_SEQ_HW_H */
