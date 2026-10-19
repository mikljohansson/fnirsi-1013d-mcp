//----------------------------------------------------------------------------------------------------------------------------------
// acq_view.c - the scope's sample rate, time/div and record kept consistent (see acq_view.h; maths in tb_view.c)
//----------------------------------------------------------------------------------------------------------------------------------

#include "acq_view.h"
#include "tb_view.h"
#include "slider_ov.h"
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

int acq_view_rate_free(void)
{
  return !scopesettings.waveviewmode && (scopesettings.runstate || (scopesettings.triggermode == 1));
}

int acq_view_tb_ok(uint32 timeperdiv)
{
  uint32 fs = acq_fs();

  if(!fs || (timeperdiv < ACQ_TB_FIRST) || (timeperdiv > ACQ_TB_LAST))
    return 0;

  if(acq_view_rate_free())
    return tbv_ladder_rate(frequency_per_div[timeperdiv], sample_rate, ACQ_RATES, fpgasettings.totalsamples) >= 0;

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

int32 acq_view_slider_jump(int32 pos, int32 x, int32 y)
{
  uint32 fs = acq_fs();
  int32  sx, sw, dx;

  if(!fs || (scopesettings.timeperdiv < ACQ_TB_FIRST) || (scopesettings.timeperdiv > ACQ_TB_LAST) ||
     scopesettings.xymodedisplay)
    return pos;

  tbv_slider(disp_first_sample, fs, frequency_per_div[scopesettings.timeperdiv], fpgasettings.totalsamples, &sx, &sw);
  dx = sov_jump(x, y, sx, sw);

  return dx ? acq_view_slider_pos(pos, dx) : pos;
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

//fw 0.2.75: time/div index t at the ladder's rate for it (one exists: the callers checked), or at the record's rate
static int acq_view_set_pair(int32 t, int rate_free)
{
  int32 r;

  if(!rate_free)
  {
    acq_view_set(t, acq_fs());
    return ACQ_SET_TB;
  }

  r = tbv_ladder_rate(frequency_per_div[t], sample_rate, ACQ_RATES, fpgasettings.totalsamples);

  if(r < 0)
    return 0;

  if(r == (int32)scopesettings.samplerate)
  {
    acq_view_set(t, sample_rate[r]);
    return ACQ_SET_TB;
  }

  scopesettings.samplerate = (uint8)r;
  acq_view_set(t, sample_rate[r]);
  return ACQ_SET_RATE;
}

int acq_view_set_tb(uint32 timeperdiv)
{
  if(!acq_fs() || (timeperdiv < ACQ_TB_FIRST) || (timeperdiv > ACQ_TB_LAST) || !acq_view_tb_ok(timeperdiv))
    return 0;

  return acq_view_set_pair((int32)timeperdiv, acq_view_rate_free());
}

static void acq_notice_set(const char *head, const char *mid, const char *tail);

int acq_view_zoom(int dir, int rate_free)
{
  uint32 fs = acq_fs();
  int32  t;

  if(!fs || (scopesettings.timeperdiv < ACQ_TB_FIRST) || (scopesettings.timeperdiv > ACQ_TB_LAST))
    return 0;

  if(rate_free)
    t = tbv_zoom_step(scopesettings.timeperdiv, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, sample_rate, ACQ_RATES,
                      fpgasettings.totalsamples);
  else
  {
    t = tbv_zoom_step(scopesettings.timeperdiv, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST,
                      &sample_rate[scopesettings.samplerate], 1, fpgasettings.totalsamples);

    //The record's limit, not the ladder's end: say what would go on
    if((t < 0) && (tbv_zoom_step(scopesettings.timeperdiv, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, sample_rate,
                                 ACQ_RATES, fpgasettings.totalsamples) >= 0))
      acq_notice_set("Stopped: press RUN to zoom further", "", "");
  }

  //The end of the ladder: a plain stop, unless the pair could not show this time/div at all (never left so by the
  //writers, which snap; kept for safety): then the ladder's rate for it
  if(t < 0)
  {
    if(rate_free && !tbv_viable(fs, frequency_per_div[scopesettings.timeperdiv], fpgasettings.totalsamples))
      return acq_view_set_pair(scopesettings.timeperdiv, 1);

    return 0;
  }

  return acq_view_set_pair(t, rate_free);
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
    //"Time/div set to 1us for this rate": the time/div text without its "/div"
    static char tb[12];
    const char *s = (const char *)time_div_texts[t];
    uint32      n = strlen(s), i;

    if((n > 4) && !memcmp(s + n - 4, "/div", 4))
      n -= 4;
    if(n > sizeof(tb) - 1)
      n = sizeof(tb) - 1;
    for(i = 0; i < n; i++)
      tb[i] = s[i];
    tb[n] = 0;

    acq_notice_set("Time/div set to ", tb, " for this rate");
  }

  return 1;
}

//The notice: head, mid and tail joined (no libc: copied by hand), cut to the buffer; shown from the next draw
static void acq_notice_set(const char *head, const char *mid, const char *tail)
{
  const char *part[3] = { head, mid, tail };
  uint32      i, j, k = 0;

  for(j = 0; j < 3; j++)
    for(i = 0; part[j][i] && (k < sizeof(acq_notice_text) - 1); i++)
      acq_notice_text[k++] = part[j][i];
  acq_notice_text[k] = 0;

  acq_notice_state = 1;
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
