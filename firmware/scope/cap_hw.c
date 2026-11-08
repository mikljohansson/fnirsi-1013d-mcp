//----------------------------------------------------------------------------------------------------------------------------------
// cap_hw.c - the target side of FPGA 0x153A's capture stages (cap_stage.h, fw 0.2.79): the 0x63 value at boot (the trained
// one stored with the calibration when it was trained on this bitstream, else the power-on 0x06DB), the 0x36 read after
// every accepted full-rate capture (self-check, GET_HEALTH, event 23; never after a gated one: fpga_capture_check), the wizard's training and the stored value
// (sector 708 bytes 488..503, through pc_diag.c's calibration load / save hooks: written by the wizard's Save only).
// fw-153f: one stage per chip (0x153F). fw-csbusy (owner 2026-11-08): the self-check is a diagnostic only; the runtime
// guard of fw 0.2.105 (moved a flagged chip and wrote sector 708) and its stored sets are gone. fw-genlimit: the
// self-check counts at 0x64 = CS_THR_USE 48, the training at CS_THR_TRAIN 32 (cap_stage.h "The self-check's threshold").
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "fpga_control.h"
#include "pc_hal.h"
#include "pc_hooks.h"
#include "pc_cmds.h"
#include "pc_transport.h"
#include "cap_stage.h"
#include "cap_hw.h"

static pc_hw_cap cap_st;                    //GET_HEALTH / GET_CAL
static uint8     cap_training;              //the self-check is off while the wizard trains
static cs_watch  cap_watch;                 //fw 0.2.81: cores flagged 3 times in a minute (PC_CAPF_REPEAT)

static pc_hw_cap_train cap_train[2];          //1.28: the last training per channel (GET_CAL 364..431)

void pc_hal_cap_state(pc_hw_cap *c)
{
  *c = cap_st;
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
  if((cap_st.trailer == CS_T_VALID) && (cap_st.stored_fpga == PC_FW_FPGA_VERSION) && (cap_st.stored == cap_st.value))
    cap_st.flags |= PC_CAPF_TRAINED;
  else
    cap_st.flags &= (uint8)~PC_CAPF_TRAINED;
}

//0x36: the 0x63 value the FPGA holds, then four 16-bit counts, high bytes first. Returns the 0x63 value
static uint16 cap_read(uint16_t cnt[CS_CORES])
{
  uint16 v;
  uint32 k;

  fpga_write_cmd(0x36);
  v = fpga_read_short();

  for(k=0;k<CS_CORES;k++)
    cnt[k] = fpga_read_short();

  return (uint16)(v & CS_MASK);
}

//0x64, the outlier threshold of the 0x36 counters: CS_THR_USE for the self-check, CS_THR_TRAIN while the wizard trains
//(fw-genlimit; cap_stage.h). The only writer of 0x64 besides DEBUG_FPGA
static void cap_thr(uint8 thr)
{
  if(!(cap_st.flags & PC_CAPF_HAS))
    return;

  fpga_write_cmd(0x64);
  fpga_write_byte(thr);
}

//Boot (pc_glue_init) and after a calibration load: the stored value when it was trained on the built-for bitstream
//(PC_FW_FPGA_VERSION), else the power-on CS_PO.
//Always written: a FEL load leaves the FPGA with whatever the previous firmware wrote
static void cap_apply(void)
{
  uint16 v = CS_PO;

  cap_st.flags &= (uint8)~PC_CAPF_OTHER;

  if(cap_st.trailer == CS_T_VALID)
  {
    if(cap_st.stored_fpga == PC_FW_FPGA_VERSION)
      v = cap_st.stored;
    else
      cap_st.flags |= PC_CAPF_OTHER;
  }

  cap_thr(CS_THR_USE);
  fpga_capture_write(v);
}

void fpga_capture_init(void)
{
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
  cap_st.stored_fpga = PC_FW_FPGA_VERSION;
  cap_st.flags       = (uint8)((cap_st.flags & ~(PC_CAPF_OTHER | PC_CAPF_REPEAT)) | PC_CAPF_TRAINED);

  //A new trained value: the repeat watch starts again
  cs_watch_clear(&cap_watch);
}

void fpga_capture_check(void)
{
  uint8 reached;
  uint16 ex[CS_CORES], worst = 0, hw;
  uint32 k;

  if(!(cap_st.flags & PC_CAPF_HAS) || cap_training)
    return;

  //fw-capgate (bench 2026-11-06): the counts compare the two cores of a chip, which see a signal alike only while the
  //ADCs run at 200 MSa/s (divider 0, any filter, peak detect: the cores sample 5 ns apart). Filter Off at a divided rate
  //gates the encodes: the cores sample at least half a record sample apart, a real edge counts on one core alone and
  //was flagged (a 1 kHz square at 200 kSa/s: excess 15 and 43; the guard moved CH2 B and CH1 A off their trained
  //stages and stored that, REPEAT then ended every Frequency response sweep with reason 8). The callers reach this
  //after every accepted capture since the 0x33 check covers every capture (fw-gated, 2026-10-22). A gated capture is
  //not read: no count, no flag, no REPEAT; GET_HEALTH keeps the last full-rate read
  if(!pc_glue_adc_full_rate())
    return;

  hw = cap_read(cap_st.cnt);
  cap_st.reads++;
  cap_st.flags  |= PC_CAPF_READ;

  //fw-csbusy (owner 2026-11-08): a core is flagged only against a quiet partner; a chip whose cores both count is not
  //checkable (cap_stage.h "Self-check"): no flagged_n, no repeat watch, no REPEAT, no event
  cap_st.flagged = cs_flagged(cap_st.cnt);

  if(!cap_st.flagged)
  {
    cap_st.flags &= (uint8)~PC_CAPF_FLAGGED;
    return;
  }

  cap_st.flags |= PC_CAPF_FLAGGED;
  cap_st.flagged_n++;

  //fw-guardfix (bench 2026-10-22): a capture taken at a 0x63 value the firmware did not write (a DEBUG_FPGA write: the
  //capture-window sweep, a manual sweep) says nothing about the firmware's stages. The counts above stay (GET_HEALTH
  //shows what the FPGA counted), but the capture does not count toward REPEAT and nothing is logged: whoever wrote 0x63
  //owns the stages until it holds the firmware's value again
  if(hw != cap_st.value)
    return;

  //fw 0.2.81: an intermittent stage flags now and then (bench 2026-10-20, CH1 A4); a core flagged in 3 captures within a
  //minute keeps failing at its stage: REPEAT (the Diagnostics Calibration verdict asks for Calibrate) and event 23 once
  //per core (a = the cores that reached it, b = the largest excess of this capture), until a training is saved.
  //fw-csbusy: advice only, nothing changes 0x63 or the stored calibration
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

void pc_hal_cap_train(uint8_t ch, pc_hw_cap_train *t)
{
  *t = cap_train[ch & 1];
}

//1.28 (fw-capmetric): what GET_CAL reports of a channel's training (cores 2 ch, 2 ch + 1; the wizard trains one
//channel at a time: 0x03 or 0x0C). fw-153f: q and the sums per core, the set the chip's (both cores alike)
static void cap_train_note(const cs_train *t, int rc)
{
  pc_hw_cap_train *n;
  uint32 c, k, s;

  c = (t->cores & 0x03) ? 0 : 2;
  n = &cap_train[c / 2];
  n->flags = (uint8)(PC_CAPT_RAN | ((rc != CS_OK) ? PC_CAPT_FAILED : 0));

  for(k=0;k<2;k++)
  {
    n->set[k] = t->set[c + k];

    for(s=0;s<CS_STAGES;s++)
    {
      n->q[k][s]   = t->q[c + k][s];
      n->sum[k][s] = t->sum[c + k][s];
    }
  }
}

int fpga_capture_train(uint8_t cores, const cs_io *io, cs_train *t)
{
  int rc;

  //The training counts at its own threshold (32, as since 0.2.80), the self-check after it at CS_THR_USE
  cap_training = 1;
  cap_thr(CS_THR_TRAIN);
  rc = cs_train_run(io, cap_st.value, cores, t);
  cap_thr(CS_THR_USE);
  cap_training = 0;
  cap_train_note(t, rc);

  //The result, or back to the value before; event 23 with bit 4 set: a = 0x10 | cores without a clean stage, b = 0x63
  fpga_capture_write((rc == CS_OK) ? t->value : t->start);
  pc_usb_log_fpga_capture((uint8)(0x10 | t->bad), (rc == CS_OK) ? t->value : t->start);
  return rc;
}

void fpga_capture_counts(uint16_t cnt[4])
{
  (void)cap_read(cnt);
}
