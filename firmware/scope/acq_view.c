//----------------------------------------------------------------------------------------------------------------------------------
// acq_view.c - the scope's sample rate, time/div and record kept consistent (see acq_view.h; maths in tb_view.c)
//----------------------------------------------------------------------------------------------------------------------------------

#include "acq_view.h"
#include "tb_view.h"
#include "variables.h"

#include <string.h>

static char   acq_notice_text[48];
static uint8  acq_notice_state;                 //0 none, 1 waiting to be drawn, 2 showing since acq_notice_ms
static uint32 acq_notice_ms;

//The current rate in samples per second, 0 in long mode or out of range
static uint32 acq_fs(void)
{
  if(scopesettings.long_mode || (scopesettings.samplerate >= 18))
    return 0;

  return sample_rate[scopesettings.samplerate];
}

int acq_view_tb_ok(uint32 timeperdiv)
{
  uint32 fs = acq_fs();

  if(!fs || (timeperdiv < ACQ_TB_FIRST) || (timeperdiv > ACQ_TB_LAST))
    return 0;

  return tbv_viable(fs, frequency_per_div[timeperdiv], fpgasettings.totalsamples);
}

void acq_view_clamp_pos(void)
{
  uint32 fs = acq_fs();

  if(!fs || (scopesettings.timeperdiv < ACQ_TB_FIRST) || (scopesettings.timeperdiv > ACQ_TB_LAST))
    return;

  scopesettings.triggerhorizontalposition = tbv_clamp_pos(scopesettings.triggerhorizontalposition, fs,
                                                          frequency_per_div[scopesettings.timeperdiv],
                                                          fpgasettings.totalsamples, fpgasettings.settriggerpoint);
}

int32 acq_view_slider_pos(int32 start, int32 dx)
{
  uint32 fs = acq_fs();
  uint32 fpd;

  if(!fs || (scopesettings.timeperdiv < ACQ_TB_FIRST) || (scopesettings.timeperdiv > ACQ_TB_LAST))
    return start;

  fpd = frequency_per_div[scopesettings.timeperdiv];

  return tbv_clamp_pos(start + tbv_slider_move(dx, fs, fpd, fpgasettings.totalsamples), fs, fpd,
                       fpgasettings.totalsamples, fpgasettings.settriggerpoint);
}

//fw 0.2.68: the one way a short-mode time/div is set, by hand (the ACQ menu's box) or by the snap (acq_view_fit): the
//index and the trigger point through tbv_set, then the next UI pass arms a fresh conversion with them (one in flight
//was armed with the old geometry). The FPGA's 0x0E and the pretrigger / total are written at every arm.
static void acq_view_set(int32 t, uint32 fs)
{
  tbv_tb s;

  s.tpd = (int32)scopesettings.timeperdiv;
  s.pos = scopesettings.triggerhorizontalposition;
  tbv_set(&s, t, fs, frequency_per_div, fpgasettings.totalsamples, fpgasettings.settriggerpoint);

  scopesettings.timeperdiv                = (uint8)s.tpd;
  scopesettings.triggerhorizontalposition = s.pos;

  if(scopesettings.runstate)
    scopesettings.display_data_done = 1;
}

int acq_view_set_tb(uint32 timeperdiv)
{
  uint32 fs = acq_fs();

  if(!fs || (timeperdiv < ACQ_TB_FIRST) || (timeperdiv > ACQ_TB_LAST) || !acq_view_tb_ok(timeperdiv))
    return 0;

  acq_view_set((int32)timeperdiv, fs);
  return 1;
}

int acq_view_fit(int notice)
{
  uint32 fs = acq_fs();
  int32  t;

  if(!fs || (scopesettings.timeperdiv < ACQ_TB_FIRST) || (scopesettings.timeperdiv > ACQ_TB_LAST))
    return 0;

  t = tbv_snap(fs, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, scopesettings.timeperdiv, fpgasettings.totalsamples);

  if((t < 0) || (t == (int32)scopesettings.timeperdiv))
  {
    acq_view_clamp_pos();
    return 0;
  }

  //The snap is a manual set of the snapped value (fw 0.2.68; host_tests/test_tb_view.c: tbv_fit == tbv_set)
  acq_view_set(t, fs);

  if(notice)
  {
    //"Time/div set to 1us for this rate": the time/div text without its "/div" (no libc: copied by hand)
    static const char head[] = "Time/div set to ", tail[] = " for this rate";
    const char *s = (const char *)time_div_texts[t];
    uint32      n = strlen(s), i, k = 0;

    if((n > 4) && !memcmp(s + n - 4, "/div", 4))
      n -= 4;

    for(i = 0; head[i]; i++)
      acq_notice_text[k++] = head[i];
    for(i = 0; (i < n) && (k < sizeof(acq_notice_text) - sizeof(tail)); i++)
      acq_notice_text[k++] = s[i];
    for(i = 0; tail[i]; i++)
      acq_notice_text[k++] = tail[i];
    acq_notice_text[k] = 0;

    acq_notice_state = 1;
  }

  return 1;
}

const char *acq_view_notice(void)
{
  if(acq_notice_state == 1)
  {
    acq_notice_state = 2;
    acq_notice_ms    = timer0ticks;
  }

  if(acq_notice_state != 2)
    return 0;

  if((timer0ticks - acq_notice_ms) >= ACQ_NOTICE_MS)
  {
    acq_notice_state = 0;
    return 0;
  }

  return acq_notice_text;
}
