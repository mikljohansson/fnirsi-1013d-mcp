//----------------------------------------------------------------------------------------------------------------------------------
// cap_hw.c - the target side of FPGA 0x153A's capture stages (cap_stage.h, fw 0.2.79): the 0x63 value at boot (the trained
// one stored with the calibration when it was trained on this bitstream, else the power-on 0x08E4), the 0x36 read after
// every accepted full-rate capture (self-check, GET_HEALTH, event 23), the wizard's training and the stored value
// (sector 708 bytes 488..503, through pc_diag.c's calibration load / save hooks). fw 0.2.105: the trained clean sets
// (bytes 504..511) and the runtime guard (cap_stage.h "Runtime guard").
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "fpga_control.h"
#include "pc_hal.h"
#include "pc_transport.h"
#include "cap_stage.h"
#include "cap_hw.h"
#include "variables.h"

#include <string.h>

static uint16    cap_fpga;                  //running bitstream (fpga_capture_init)
static pc_hw_cap cap_st;                    //GET_HEALTH / GET_CAL
static uint8     cap_training;              //the self-check is off while the wizard trains
static cs_watch  cap_watch;                 //fw 0.2.81: cores flagged 3 times in a minute (PC_CAPF_REPEAT)

//fw 0.2.105: the stored clean sets (cs_sets_pack; saved with the trailer; 0 = unknown: no guard), and the sets of the
//value cap_sets_v as the wizard's trainings left them (they become the stored ones at its Save)
static uint16    cap_stored_sets, cap_sets, cap_sets_v;
static uint8     cap_moved;                 //cores the guard moved since boot (once each)
static uint8     cap_store;                 //the guard's new value waits for fpga_capture_service's sector 708 write
static uint32    cap_sector[128];            //sector 708 for the guard's write (pc_hal_sd_*: 4-byte aligned)

void pc_hal_cap_state(pc_hw_cap *c)
{
  *c = cap_st;
}

int fpga_capture_has(void)
{
  return (cap_st.flags & PC_CAPF_HAS) != 0;
}

uint16_t fpga_capture_value(void)
{
  return cap_st.value;
}

void fpga_capture_write(uint16_t v)
{
  if(!(cap_st.flags & PC_CAPF_HAS))
    return;

  fpga_write_cmd(0x63);
  fpga_write_short((uint16)(v & CS_MASK));
  cap_st.value = (uint16)(v & CS_MASK);

  //Trained = the stored value of this bitstream is in effect (a wizard's new value counts once it is saved)
  if((cap_st.trailer == CS_T_VALID) && (cap_st.stored_fpga == cap_fpga) && (cap_st.stored == cap_st.value))
    cap_st.flags |= PC_CAPF_TRAINED;
  else
    cap_st.flags &= (uint8)~PC_CAPF_TRAINED;
}

//0x36: the 0x63 value, then four 16-bit counts, high bytes first
static void cap_read(uint16_t cnt[CS_CORES])
{
  uint32 k;

  fpga_write_cmd(0x36);
  (void)fpga_read_short();

  for(k=0;k<CS_CORES;k++)
    cnt[k] = fpga_read_short();
}

//Boot (pc_glue_init) and after a calibration load: the stored value when it belongs to this bitstream, else 0x08E4.
//Always written: a FEL load leaves the FPGA with whatever the previous firmware wrote
static void cap_apply(void)
{
  uint16 v = CS_PO;

  cap_st.flags &= (uint8)~PC_CAPF_OTHER;

  if(cap_st.trailer == CS_T_VALID)
  {
    if(cap_st.stored_fpga == cap_fpga)
      v = cap_st.stored;
    else
      cap_st.flags |= PC_CAPF_OTHER;
  }

  fpga_write_cmd(0x64);
  fpga_write_byte(CS_THR_USE);
  fpga_capture_write(v);
}

void fpga_capture_init(uint16_t fpga_version)
{
  cap_fpga = fpga_version;
  cap_st.flags |= PC_CAPF_HAS;
  cap_apply();
}

void fpga_capture_loaded(const uint8_t *sector)
{
  cap_st.trailer = (uint8)cs_trailer_unpack(sector + CS_TRAILER_OFFSET, &cap_st.stored, &cap_st.stored_fpga);

  //The clean sets when they belong to this trailer (a 0.2.79..0.2.104 save leaves none: no guard until a training)
  if((cap_st.trailer != CS_T_VALID) || !cs_ext_unpack(sector, &cap_stored_sets))
    cap_stored_sets = 0;

  cap_sets   = cap_stored_sets;
  cap_sets_v = cap_st.stored;

  //A reload after boot (Restore default settings) applies it at once
  if(cap_st.flags & PC_CAPF_HAS)
    cap_apply();
}

void fpga_capture_save(uint8_t *sector)
{
  if(cap_st.trailer == CS_T_VALID)
  {
    cs_trailer_pack(cap_st.stored, cap_st.stored_fpga, sector + CS_TRAILER_OFFSET);
    cs_ext_pack(sector, cap_stored_sets);
  }
}

void fpga_capture_commit(void)
{
  if(!(cap_st.flags & PC_CAPF_HAS))
    return;

  cap_st.trailer     = CS_T_VALID;
  cap_st.stored      = cap_st.value;
  cap_st.stored_fpga = cap_fpga;
  cap_stored_sets    = (cap_sets_v == cap_st.value) ? cap_sets : 0;
  cap_st.flags       = (uint8)((cap_st.flags & ~(PC_CAPF_OTHER | PC_CAPF_REPEAT | PC_CAPF_MOVED)) | PC_CAPF_TRAINED);

  //A new trained value: the repeat watch starts again
  cs_watch_clear(&cap_watch);
}

//fw 0.2.105: cores that reached the repeat watch, moved once per boot inside their trained clean set (cs_guard) while
//the stored value of this bitstream is in effect and its sets are known; the new value is in effect and stored at once
//(the sector write waits for fpga_capture_service). Returns the cores moved
static uint8 cap_guard(uint8 reached)
{
  uint16 v = cap_st.value, sets = cap_stored_sets;
  uint8  moved;

  reached &= (uint8)~cap_moved;

  //TRAINED: the stored value of this bitstream is in effect, so the stored sets are its sets
  if(!reached || !(cap_st.flags & PC_CAPF_TRAINED))
    return 0;

  moved = cs_guard(&v, &sets, reached);

  if(!moved)
    return 0;

  cap_moved      |= moved;
  cap_sets        = sets;
  cap_sets_v      = v;
  cap_stored_sets = sets;
  cap_st.stored   = v;
  cap_store       = 1;
  fpga_capture_write(v);
  cap_st.flags |= PC_CAPF_MOVED;

  //The moved cores' watch starts again at their new stage (a repeat there asks for Calibrate)
  cs_watch_forget(&cap_watch, moved);

  //Event 23: a = 0x20 | the cores moved, b = the new 0x63 value
  pc_usb_log_fpga_capture((uint8)(0x20 | moved), v);
  return moved;
}

void fpga_capture_service(void)
{
  if(!cap_store)
    return;

  cap_store = 0;

  //Only the trailer and the sets change (bytes 488..511; the 1.00o5 checksum and the v3 block are before them). A
  //failed read writes nothing: the value stays in effect and the next calibration save stores it
  if(pc_hal_sd_read(INPUT_CALIBRATION_SECTOR, 1, (uint8 *)cap_sector))
    return;

  cs_trailer_pack(cap_st.stored, cap_st.stored_fpga, (uint8 *)cap_sector + CS_TRAILER_OFFSET);
  cs_ext_pack((uint8 *)cap_sector, cap_stored_sets);
  (void)pc_hal_sd_write(INPUT_CALIBRATION_SECTOR, 1, (const uint8 *)cap_sector);
}

void fpga_capture_check(void)
{
  uint8 reached;
  uint16 ex[CS_CORES], worst = 0;
  uint32 k;

  if(!(cap_st.flags & PC_CAPF_HAS) || cap_training)
    return;

  cap_read(cap_st.cnt);
  cap_st.reads++;
  cap_st.flags  |= PC_CAPF_READ;
  cap_st.flagged = cs_flagged(cap_st.cnt);

  if(!cap_st.flagged)
  {
    cap_st.flags &= (uint8)~PC_CAPF_FLAGGED;
    return;
  }

  cap_st.flags |= PC_CAPF_FLAGGED;
  cap_st.flagged_n++;

  //fw 0.2.81: an intermittent stage flags now and then (bench 2026-10-20, CH1 A4); a core flagged in 3 captures within a
  //minute keeps failing at its stage: REPEAT (the Diagnostics Calibration verdict asks for Calibrate) and event 23 once
  //per core (a = the cores that reached it, b = the largest excess of this capture), until a training is saved
  reached = cs_watch_add(&cap_watch, cap_st.flagged, pc_hal_ms());

  //fw 0.2.105: first the guard; the cores it could not move ask for Calibrate
  if(reached)
    reached &= (uint8)~cap_guard(reached);

  if(reached)
  {
    cap_st.flags |= PC_CAPF_REPEAT;
    cs_excess(cap_st.cnt, ex);

    for(k=0;k<CS_CORES;k++)
      if(ex[k] > worst)
        worst = ex[k];

    pc_usb_log_fpga_capture(reached, worst);
  }
}

void pc_hal_cap_check(void)
{
  fpga_capture_check();
}

int fpga_capture_train(uint8_t cores, const cs_io *io, cs_train *t)
{
  int rc;

  cap_training = 1;
  rc = cs_train_run(io, cap_st.value, cores, t);
  cap_training = 0;

  //fw 0.2.105: the trained cores' clean sets for the new value (the other cores' only while they belong to the start)
  if(rc == CS_OK)
  {
    uint32 k;

    if(cap_sets_v != t->start)
      cap_sets = 0;

    for(k=0;k<CS_CORES;k++)
      if(t->cores & (1u << k))
        cap_sets = cs_sets_with(cap_sets, k, t->set[k]);

    cap_sets_v = t->value;
  }

  //The result, or back to the value before; event 23 with bit 4 set: a = 0x10 | cores without a clean stage, b = 0x63
  fpga_capture_write((rc == CS_OK) ? t->value : t->start);
  pc_usb_log_fpga_capture((uint8)(0x10 | t->bad), (rc == CS_OK) ? t->value : t->start);
  return rc;
}

void fpga_capture_counts(uint16_t cnt[4])
{
  cap_read(cnt);
}
