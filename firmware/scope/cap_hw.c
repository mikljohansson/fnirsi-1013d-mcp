//----------------------------------------------------------------------------------------------------------------------------------
// cap_hw.c - the target side of FPGA 0x153A's capture stages (cap_stage.h, fw 0.2.79): the 0x63 value at boot (the trained
// one stored with the calibration when it was trained on this bitstream, else the power-on 0x08E4), the 0x36 read after
// every accepted full-rate capture (self-check, GET_HEALTH, event 23), the wizard's training and the stored value
// (sector 708 bytes 488..503, through pc_diag.c's calibration load / save hooks).
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "fpga_control.h"
#include "pc_hal.h"
#include "pc_transport.h"
#include "cap_stage.h"
#include "cap_hw.h"

#include <string.h>

static uint16    cap_fpga;                  //running bitstream (fpga_capture_init)
static pc_hw_cap cap_st;                    //GET_HEALTH / GET_CAL
static uint8     cap_training;              //the self-check is off while the wizard trains
static cs_watch  cap_watch;                 //fw 0.2.81: cores flagged 3 times in a minute (PC_CAPF_REPEAT)

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

  //A reload after boot (Restore default settings) applies it at once
  if(cap_st.flags & PC_CAPF_HAS)
    cap_apply();
}

void fpga_capture_save(uint8_t *sector)
{
  if(cap_st.trailer == CS_T_VALID)
    cs_trailer_pack(cap_st.stored, cap_st.stored_fpga, sector + CS_TRAILER_OFFSET);
}

void fpga_capture_commit(void)
{
  if(!(cap_st.flags & PC_CAPF_HAS))
    return;

  cap_st.trailer     = CS_T_VALID;
  cap_st.stored      = cap_st.value;
  cap_st.stored_fpga = cap_fpga;
  cap_st.flags       = (uint8)((cap_st.flags & ~(PC_CAPF_OTHER | PC_CAPF_REPEAT)) | PC_CAPF_TRAINED);

  //A new trained value: the repeat watch starts again
  cs_watch_clear(&cap_watch);
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

  //The result, or back to the value before; event 23 with bit 4 set: a = 0x10 | cores without a clean stage, b = 0x63
  fpga_capture_write((rc == CS_OK) ? t->value : t->start);
  pc_usb_log_fpga_capture((uint8)(0x10 | t->bad), (rc == CS_OK) ? t->value : t->start);
  return rc;
}

void fpga_capture_counts(uint16_t cnt[4])
{
  cap_read(cnt);
}
