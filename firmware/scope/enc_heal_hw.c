//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal_hw.c - the target side of the full-rate capture check (enc_heal.h, fw 0.2.62): the mode of the running
// bitstream, the 0x33 read and its boot check, the re-arm of UI / calibration / probe conversions from
// fpga_done_conversion, the 0x0D re-write, the counters and event 21. Host ARM captures are checked by the session
// (pc_acq_service in pc_cmds.c) through the pc_hal_enc_* functions here.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "fpga_control.h"
#include "variables.h"
#include "pc_hooks.h"
#include "pc_hal.h"
#include "pc_seq_hw.h"
#include "pc_transport.h"
#include "enc_heal.h"

#define EH_RING  8192                   //AL3 ring words (every checked bitstream is AL3 0x1532+)

uint8_t          fpga_enc_garbled;
static uint16    eh_version;
static pc_hw_enc eh_st;                 //mode, 0x33 bytes, flags, counters (GET_HEALTH, GET_LINK_STATS)
static eh_cap    eh_ui;                 //re-arms of the UI / calibration / probe conversion in progress
static uint8     eh_rearming;           //fpga_do_conversion called by the re-arm

void pc_hal_enc_state(pc_hw_enc *e)
{
  *e = eh_st;
}

uint8_t pc_hal_enc_status(void)
{
  uint8 s;

  fpga_write_cmd(0x33);
  s = fpga_read_byte();
  eh_st.last33 = s;
  eh_st.flags |= PC_ENCF_READ;
  return s;
}

//0x1532..0x1536: only a rate write re-rolls the phase (0x1537 and 0x1538 re-resolve it at every sampling reset)
void pc_hal_enc_retry(void)
{
  uint8 who = fpga_trace_who;

  if(!eh_affected(eh_version))
    return;

  fpga_trace_who = FPGA_WHO_CHECK;
  fpga_write_cmd(0x0D);
  fpga_write_int(1);
  fpga_write_cmd(0x0D);
  fpga_write_int(0);
  fpga_trace_who = who;
}

void pc_hal_enc_note(uint8_t why, const eh_note *n)
{
  eh_st.checked++;

  if(n->first)
    eh_st.bad++;

  if(!n->bad)
    eh_st.flags &= ~PC_ENCF_GARBLED;

  if(n->done)
  {
    if(n->ok)
      eh_st.recovered++;
    else
    {
      eh_st.flags |= PC_ENCF_GARBLED;
      fpga_enc_garbled = 1;
    }

    pc_usb_log_fpga_heal(eh_event_a(why, n->tries, n->ok), eh_event_b(fpga_trace_who, n->value));
  }
}

void fpga_encode_init(uint16_t fpga_version)
{
  eh_version = fpga_version;
  eh_st.mode = (uint8)eh_mode(fpga_version);

  if(eh_order_differs(fpga_version))
    eh_st.flags |= PC_ENCF_ORDER;

  //0x1538+: the encode convention must be 1 (core order guaranteed, B first: PC_INTERLEAVE_ORDER 0)
  if(eh_st.mode == EH_MODE_FLAG)
  {
    fpga_write_cmd(0x33);
    eh_st.boot33 = fpga_read_byte();

    if(!eh_conv_ok(eh_st.boot33))
    {
      eh_st.flags |= PC_ENCF_CONV_BAD;
      pc_usb_log_fpga_heal(eh_event_a(EH_WHY_CONV, 0, 0), eh_event_b(fpga_trace_who, eh_st.boot33));
    }
  }
}

static uint8_t eh_hw_status(void *ctx)
{
  (void)ctx;
  return pc_hal_enc_status();
}

static void eh_hw_read(void *ctx, uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride)
{
  (void)ctx;
  pc_hal_read_core(core, start, count, dst, stride, EH_RING, 0);
}

void fpga_encode_new_conversion(void)
{
  if(!eh_rearming)
    eh_cap_reset(&eh_ui);
}

int fpga_encode_after_capture(void)
{
  static const eh_io io = { 0, eh_hw_status, eh_hw_read };
  uint32   pre, start;
  uint32_t value;
  eh_note  n;
  int      bad, rearm;

  //Full rate only (divider 0 is always the legacy 8-bit layout). A UI averaging run (0x1536 / 0x1537) leaves lane
  //sums in the ring, not samples: the data check cannot read it; 0x1538's flag covers the whole run
  if((eh_st.mode == EH_MODE_NONE) || (fpga_divider != 0) || ((eh_st.mode == EH_MODE_DATA) && pc_seq_ui_running()))
    return 0;

  //The record as fpga_do_conversion set it up: 0x0B = settriggerpoint / 2 words before 0x14
  pre   = (fpgasettings.settriggerpoint / 2) % EH_RING;
  start = (eh_st.mode == EH_MODE_DATA) ? (fpga_prepare_for_transfer() + EH_RING - pre) % EH_RING : 0;
  bad   = eh_check(&io, eh_st.mode, start, fpgasettings.totalsamples / 2, &value);

  rearm = eh_cap_step(&eh_ui, bad, value, &n);
  pc_hal_enc_note((eh_st.mode == EH_MODE_FLAG) ? EH_WHY_FLAG : EH_WHY_DATA, &n);

  if(rearm != EH_REARM)
    return 0;

  //Discard and re-arm the same conversion (the UI's filter setting again when it was the UI's)
  pc_hal_enc_retry();
  pc_glue_decim_rearm();
  eh_rearming = 1;
  fpga_do_conversion();
  eh_rearming = 0;
  return 1;
}
