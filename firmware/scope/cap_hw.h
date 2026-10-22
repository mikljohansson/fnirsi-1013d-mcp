//----------------------------------------------------------------------------------------------------------------------------------
// cap_hw.h - FPGA 0x153A capture stages on the target (cap_hw.c; the pure part is cap_stage.h)
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAP_HW_H
#define CAP_HW_H

#include <stdint.h>
#include "cap_stage.h"

void     fpga_capture_init(uint16_t fpga_version);   //pc_glue_init: 0x64 = 8, 0x63 = the stored value or 0x08E4
void     fpga_capture_loaded(const uint8_t *sector); //calibration load: the trailer at sector 708 byte 488 (applied)
void     fpga_capture_save(uint8_t *sector);         //calibration save: the trailer back (a valid one only)
void     fpga_capture_write(uint16_t v);             //0x63 = v (0x153A+ only)
uint16_t fpga_capture_value(void);                   //0x63 in effect
int      fpga_capture_has(void);                     //the running bitstream has the stages (0x153A+)
void     fpga_capture_commit(void);                  //the wizard's Save: the value in effect becomes the stored one
void     fpga_capture_check(void);                   //after an accepted full-rate capture: read 0x36, flag; a core
                                                     //flagged 3 times in a minute: moved once inside its trained set
                                                     //(MOVED), else REPEAT; event 23 once. Only captures at
                                                     //the 0x63 value the firmware wrote count (0x36's first
                                                     //word; a DEBUG_FPGA write owns the stages meanwhile)
void     fpga_capture_counts(uint16_t cnt[4]);       //read 0x36's counts (training captures)
void     fpga_capture_service(void);                 //MAIN context: store a value the runtime guard moved to (sector 708
                                                     //bytes 488..511)

//Train cores (cap_stage.h mask) through io (park the input at a band point, one capture at 200 MSa/s with 0x63 = v);
//the result (or the value before) is written; event 23 a = 0x10 | the cores without a clean stage, b = the value in
//effect. CS_OK / CS_E_*
int      fpga_capture_train(uint8_t cores, const cs_io *io, cs_train *t);

#endif /* CAP_HW_H */
