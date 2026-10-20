//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq_hw.h - the sequencer on the scope: FPGA register writes and reads (pc_hal.h sequencer functions) and the UI's
// averaging (the acquisition menu's Average: the hardware run, or the MCU-summed mean). Target only; the maths is pc_seq.c.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_SEQ_HW_H
#define PC_SEQ_HW_H

#include "types.h"
#include "pc_seq.h"
#include "pc_decim.h"

//Before 0x01 = 1 (pc_hal_arm, fpga_do_conversion): the sequencer registers, so that the write-clock side loads them during
//the reset at every rate (0x0B / 0x0C: fpga_arm_reset); c = NULL or mode 0: 0x57 = 0 when the FPGA may still hold another mode
void   pc_seq_hw_pre_reset(const pc_seq_cfg *c);

//UI acquisition (pc_hal_scope.c glue)
int    pc_seq_ui_conversion(int ui, const pc_decim_cfg *c, uint32 divider);  //1: this conversion is a hardware run
void   pc_seq_ui_pre_reset(void);              //fpga_do_conversion, before the sampling reset
void   pc_seq_ui_service(void);                //main loop: the hardware run's deadline
//0x14 read (addr) of a finished UI conversion: a hardware run counts its records; on the MCU path a triggered first
//record starts the N - 1 further conversions, summed in place (the pass stays within the planned run time plus the
//auto-mode deadline). *triggered as the shown record is
void   pc_seq_ui_done(uint8_t *triggered, uint32 addr);
int    pc_seq_ui_feed(void *settings, int16 *feed, uint32 n);   //fpga_read_sample_data: the mean's corrected codes, 1 = fed
void   pc_seq_ui_read_done(void);              //UI readout of the conversion finished
int    pc_seq_ui_running(void);                //the UI conversion in flight (or just done) is a hardware run

#endif /* PC_SEQ_HW_H */
