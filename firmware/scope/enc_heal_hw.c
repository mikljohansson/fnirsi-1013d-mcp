//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal_hw.c - the target side of the full-rate check (enc_heal.h, fw 0.2.61): one full-rate capture of both
// channels into scratch buffers through raw scratch channels, the retry (0x0D re-write), the counters and the event.
//
// Callers: the calibration wizard before every full-rate measurement (cal_wiz_ui.c); after any 0x0D write, the UI
// acquisition before its next conversion (scope_functions.c) and the host ARM (pc_hal_arm), at divider 0 only.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "timer.h"
#include "fpga_control.h"
#include "variables.h"
#include "pc_hooks.h"
#include "pc_hal.h"
#include "pc_transport.h"
#include "enc_heal.h"

#include <string.h>

static uint8           eh_buf[2][2 * EH_SAMPLES];
static CHANNELSETTINGS eh_ch;           //zero: no compensation, no invert, no dcoffset: raw codes (not CH1 / CH2)
static uint32          eh_count[PC_ENCH_COUNT];

static const uint8 eh_cmds[2][2] = { { 0x20, 0x21 }, { 0x22, 0x23 } };

//Re-issue the rate write: every 0x0D write re-rolls the state (0x1535 random, 0x1537 bench 16:40); the capture re-arms
static void eh_hw_retry(void *ctx)
{
  (void)ctx;

  fpga_write_cmd(0x0D);
  fpga_write_int(1);
  fpga_write_cmd(0x0D);
  fpga_write_int(0);

  if(EH_WAIT_MS)
    timer0_delay(EH_WAIT_MS);
}

static void eh_hw_capture(void *ctx, uint32_t res16[2])
{
  uint32 ch;

  (void)ctx;

  fpga_do_conversion();
  while(fpga_done_conversion() == 0) PC_BUSY_POLL();

  for(ch=0;ch<2;ch++)
  {
    eh_ch.adc1command = eh_cmds[ch][0];
    eh_ch.adc2command = eh_cmds[ch][1];
    eh_ch.tracebuffer = eh_buf[ch];
    fpga_read_sample_data(&eh_ch, 100);
    res16[ch] = eh_residual16(eh_buf[ch], EH_SAMPLES);
  }
}

int fpga_encode_heal(void)
{
  static const eh_ops ops = { 0, eh_hw_retry, eh_hw_capture };
  eh_result r;
  uint32    tm = scopesettings.triggermode;
  uint32    sm = scopesettings.samplemode;
  uint32    sc = scopesettings.samplecount;
  uint32    ns = scopesettings.nofsamples;

  uint8     who = fpga_trace_who;

  fpga_rate_dirty = 0;

  if(fpga_divider != 0)
    return 1;

  fpga_trace_who = FPGA_WHO_CHECK;

  //Trigger circuit off, FPGA auto mode, 500 samples per core (as the calibration's raw reads)
  scopesettings.samplemode  = 0;
  scopesettings.triggermode = 0;
  fpga_set_trigger_mode();
  scopesettings.samplecount = 2 * EH_SAMPLES;
  scopesettings.nofsamples  = EH_SAMPLES;

  eh_run(&ops, &r);

  scopesettings.samplecount = sc;
  scopesettings.nofsamples  = ns;
  scopesettings.samplemode  = (uint8)sm;
  scopesettings.triggermode = (uint8)tm;
  fpga_set_trigger_mode();
  fpga_trace_who = who;

  if(r.bad)
  {
    eh_count[0]++;
    eh_count[1] += r.ok;
    pc_usb_log_fpga_heal((uint8)(r.tries | (r.ok ? 0 : 0x80)), (uint16)((r.first16 > 0xFFFF) ? 0xFFFF : r.first16));
  }

  return r.ok;
}

int fpga_encode_check_dirty(void)
{
  return fpga_rate_dirty ? fpga_encode_heal() : 1;
}

void pc_hal_enc_heal(uint32_t *v)
{
  v[0] = eh_count[0];
  v[1] = eh_count[1];
}
