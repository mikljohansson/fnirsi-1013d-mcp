//----------------------------------------------------------------------------------------------------------------------------------
// test_acq_view.c - the Scope's rate / time/div / record glue (acq_view.c, fw 0.2.76) on the host, with the firmware's
// tables and stub globals: the owner's tap in the record slider's track (fw 0.2.73 did nothing), the Spectrum's tap,
// and the zoom ladder (docs/ui-guidelines.md section 2, "Time/div drives the sample rate"): from every (rate, time/div)
// cell and at every memory depth, a zoom step lands on the ladder's pair or is a plain stop at the ladder's end; walks
// to both ends and back never get stuck and never leave the ladder; stopped, the record's rate is kept.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "acq_view.h"
#include "tb_view.h"
#include "slider_ov.h"
#include "variables.h"

//The firmware's tables (variables.c), short-mode part used by acq_view.c
const uint32 frequency_per_div[35] =
{
  1, 2, 4, 8, 20, 40, 80, 200, 400, 800, 2000,
  5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000,
  5000000, 10000000, 20000000, 50000000, 100000000, 200000000
};

const uint32 sample_rate[29] =
{
  200000000, 100000000, 50000000, 20000000, 10000000, 5000000, 2000000, 1000000, 500000, 200000, 100000, 50000, 20000,
  10000, 5000, 2000, 1000, 500,
  10000, 4000, 2000, 1000, 400, 200, 100, 40, 20, 10, 4
};

const int8 *time_div_texts[35] =
{
  "50s/div", "20s/div", "10s/div", "5s/div", "2s/div", "1s/div", "500ms/div", "200ms/div", "100ms/div", "50ms/div",
  "20ms/div",
  "200ms/div", "100ms/div", "50ms/div", "20ms/div", "10ms/div", "5ms/div", "2ms/div", "1ms/div", "500us/div",
  "200us/div", "100us/div", "50us/div", "20us/div", "10us/div", "5us/div", "2us/div", "1us/div", "500ns/div",
  "200ns/div", "100ns/div", "50ns/div", "20ns/div", "10ns/div", "5ns/div"
};

SCOPESETTINGS   scopesettings;
FPGASETTINGS    fpgasettings;
volatile uint32 timer0ticks;
int32           disp_first_sample;

static const uint16 totals[] = { 3000, 6000, 9000, 12000, 15000, 8190, 16382, 1000, 250 };

static void state(uint32 total, int rate, int tpd, int running)
{
  memset(&scopesettings, 0, sizeof(scopesettings));
  memset(&fpgasettings, 0, sizeof(fpgasettings));
  fpgasettings.totalsamples    = (uint16)total;
  fpgasettings.settriggerpoint = (uint16)(total / 2);
  scopesettings.samplerate     = (uint8)rate;
  scopesettings.timeperdiv     = (uint8)tpd;
  scopesettings.runstate       = (uint8)running;
  scopesettings.triggermode    = 0;
  scopesettings.triggerhorizontalposition = 362;
  acq_view_clamp_pos();
  //No notice pending or showing from an earlier case: start it, then let its time run out
  (void)acq_view_notice();
  timer0ticks += ACQ_NOTICE_MS;
  (void)acq_view_notice();
}

static int32 ladder(uint32 total, int32 t)
{
  return tbv_ladder_rate(frequency_per_div[t], sample_rate, ACQ_RATES, total);
}

//The fastest and slowest short time/divs the ladder has at this depth
static int32 ladder_end(uint32 total, int dir)
{
  int32 t = (dir > 0) ? ACQ_TB_LAST : ACQ_TB_FIRST;

  while((t >= ACQ_TB_FIRST) && (t <= ACQ_TB_LAST) && (ladder(total, t) < 0))
    t -= dir;

  return t;
}

//The current pair is the ladder's and consistent: viable, the trigger point inside its limits
static void check_pair(uint32 total)
{
  int32 t = scopesettings.timeperdiv, lo, hi;

  T_TRUE((t >= ACQ_TB_FIRST) && (t <= ACQ_TB_LAST));
  T_EQ(scopesettings.samplerate, ladder(total, t));
  T_TRUE(tbv_viable(sample_rate[scopesettings.samplerate], frequency_per_div[t], total));
  tbv_pos_limits(sample_rate[scopesettings.samplerate], frequency_per_div[t], total, total / 2, &lo, &hi);
  T_TRUE((scopesettings.triggerhorizontalposition >= lo) && (scopesettings.triggerhorizontalposition <= hi));
}

//The ladder itself: monotonic (a faster time/div never takes a slower rate), every pair viable, the fastest rate that is
static void test_ladder(void)
{
  uint32 k, total;
  int32  t, r, prev;

  for(k = 0; k < sizeof(totals) / sizeof(totals[0]); k++)
  {
    total = totals[k];
    prev  = ACQ_RATES;

    for(t = ACQ_TB_FIRST; t <= ACQ_TB_LAST; t++)
    {
      if((r = ladder(total, t)) < 0)
        continue;

      T_TRUE(tbv_viable(sample_rate[r], frequency_per_div[t], total));
      if(r > 0)
        T_TRUE(!tbv_viable(sample_rate[r - 1], frequency_per_div[t], total));
      T_TRUE(r <= prev);
      prev = r;
    }
  }

  //The owner's ends at the default depth: 50 ns/div at 200 MSa/s; 200 ms/div at 1 kSa/s (the roll boundary)
  T_EQ(ladder_end(3000, 1), 31);
  T_EQ(ladder(3000, 31), 0);
  T_EQ(ladder(3000, 32), -1);
  T_EQ(ladder_end(3000, -1), ACQ_TB_FIRST);
  T_EQ(sample_rate[ladder(3000, ACQ_TB_FIRST)], 1000);
  T_EQ(sample_rate[ladder(16382, 18)], 1000000);            //1 ms/div at 16k: 1 MSa/s, 1000 samples a division
}

//One zoom step from every cell (running): the ladder's pair for the next time/div with one, or a plain stop that
//changes nothing where there is none
static void test_step_every_cell(void)
{
  uint32 k, total;
  int    rate, tpd, dir;

  for(k = 0; k < sizeof(totals) / sizeof(totals[0]); k++)
    for(total = totals[k], rate = 0; rate < ACQ_RATES; rate++)
      for(tpd = ACQ_TB_FIRST; tpd <= ACQ_TB_LAST; tpd++)
        for(dir = -1; dir <= 1; dir += 2)
        {
          int32 want = tbv_zoom_step(tpd, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, sample_rate, ACQ_RATES, total);
          int32 pos, r;

          state(total, rate, tpd, 1);
          pos = scopesettings.triggerhorizontalposition;
          r   = acq_view_zoom(dir, 1);

          //A start no zoom step could reach (the pair not viable) at an end: the ladder's rate for it
          if((want < 0) && !tbv_viable(sample_rate[rate], frequency_per_div[tpd], total) && (ladder(total, tpd) >= 0))
          {
            T_TRUE(r != 0);
            T_EQ(scopesettings.timeperdiv, tpd);
            check_pair(total);
            continue;
          }

          if(want < 0)
          {
            T_EQ(r, 0);
            T_EQ(scopesettings.timeperdiv, tpd);
            T_EQ(scopesettings.samplerate, rate);
            T_EQ(scopesettings.triggerhorizontalposition, pos);
            T_TRUE(acq_view_notice() == 0);
            continue;
          }

          T_TRUE(r != 0);
          T_EQ(scopesettings.timeperdiv, want);
          if((total >= 1000) && (ladder(total, tpd) >= 0))
            T_EQ(want, tpd + dir);                          //no holes in the 1-2-5 ladder from 1000 samples
          check_pair(total);
          T_EQ(r, (scopesettings.samplerate == rate) ? ACQ_SET_TB : ACQ_SET_RATE);
          T_TRUE(acq_view_notice() == 0);                   //a zoom step is not a snap
        }
}

//Walks (owner 22:22 / 22:24): from every starting cell, to the fast end, to the slow end, back to the fast end and
//again: every step on the ladder, the ends plain stops, never stuck
static void test_walks(void)
{
  uint32 k, total;
  int    rate, tpd, leg, n;

  for(k = 0; k < sizeof(totals) / sizeof(totals[0]); k++)
    for(total = totals[k], rate = 0; rate < ACQ_RATES; rate++)
      for(tpd = ACQ_TB_FIRST; tpd <= ACQ_TB_LAST; tpd++)
      {
        int32 fast = ladder_end(total, 1), slow = ladder_end(total, -1);

        state(total, rate, tpd, 1);

        for(leg = 0; leg < 5; leg++)
        {
          int dir = (leg & 1) ? -1 : 1;
          int32 end = (dir > 0) ? fast : slow;
          uint8 sr;
          int32 pos;

          for(n = 0; (n < 40) && acq_view_zoom(dir, 1); n++)
            check_pair(total);

          T_TRUE(n < 40);
          T_EQ(scopesettings.timeperdiv, end);

          //Started at this end with a rate set by hand (viable): no step was taken, the hand rate holds
          if((leg == 0) && (n == 0) && (scopesettings.samplerate == rate))
            T_TRUE(tbv_viable(sample_rate[rate], frequency_per_div[end], total));
          else
            check_pair(total);

          //The end is a plain stop: the next tap changes nothing, the opposite direction works at once
          sr  = scopesettings.samplerate;
          pos = scopesettings.triggerhorizontalposition;
          T_EQ(acq_view_zoom(dir, 1), 0);
          T_EQ(scopesettings.timeperdiv, end);
          T_EQ(scopesettings.samplerate, sr);
          T_EQ(scopesettings.triggerhorizontalposition, pos);
          if(fast != slow)
          {
            T_TRUE(acq_view_zoom(-dir, 1) != 0);
            check_pair(total);
            T_TRUE(acq_view_zoom(dir, 1) != 0);
            T_EQ(scopesettings.timeperdiv, end);
            check_pair(total);
          }
        }
      }
}

//A rate chosen by hand holds until the next zoom step, which re-derives the ladder's rate; the ACQ menu's time/div
//boxes take the ladder's pair while the rate is free
static void test_hand_rate_and_boxes(void)
{
  uint32 k, total;
  int    rate, tpd;

  for(k = 0; k < sizeof(totals) / sizeof(totals[0]); k++)
    for(total = totals[k], rate = 0; rate < ACQ_RATES; rate++)
      for(tpd = ACQ_TB_FIRST; tpd <= ACQ_TB_LAST; tpd++)
      {
        //The rate box: the time/div snaps when needed (with the notice), the rate stays as chosen
        state(total, 0, tpd, 1);
        scopesettings.samplerate = (uint8)rate;
        if(acq_view_fit(1))
          T_TRUE(acq_view_notice() != 0);
        T_EQ(scopesettings.samplerate, rate);

        if(acq_view_zoom(1, 1) || acq_view_zoom(-1, 1))
          check_pair(total);

        //The time/div box at this cell
        state(total, rate, ACQ_TB_FIRST, 1);
        T_EQ(acq_view_tb_ok(tpd), ladder(total, tpd) >= 0);
        if(ladder(total, tpd) >= 0)
        {
          T_TRUE(acq_view_set_tb(tpd) != 0);
          T_EQ(scopesettings.timeperdiv, tpd);
          check_pair(total);
        }
        else
        {
          T_EQ(acq_view_set_tb(tpd), 0);
          T_EQ(scopesettings.timeperdiv, ACQ_TB_FIRST);
        }

        //Single counts as free (the box re-arms with it), the viewer never does
        state(total, rate, tpd, 0);
        scopesettings.triggermode = 1;
        T_TRUE(acq_view_rate_free());
        scopesettings.waveviewmode = 1;
        T_TRUE(!acq_view_rate_free());
      }

  //Long mode: none of this applies
  state(3000, 0, 20, 1);
  scopesettings.long_mode = 1;
  T_EQ(acq_view_zoom(1, 1), 0);
  T_EQ(acq_view_tb_ok(20), 0);
}

//Stopped: the record's rate is kept; the zoom steps through what the record can show, and at the record's limit (not
//the ladder's end) it says so
static void test_stopped(void)
{
  uint32 k, total;
  int    rate, tpd, dir;

  for(k = 0; k < sizeof(totals) / sizeof(totals[0]); k++)
    for(total = totals[k], rate = 0; rate < ACQ_RATES; rate++)
      for(tpd = ACQ_TB_FIRST; tpd <= ACQ_TB_LAST; tpd++)
      {
        if(!tbv_viable(sample_rate[rate], frequency_per_div[tpd], total))
          continue;

        for(dir = -1; dir <= 1; dir += 2)
        {
          int32 want = tbv_zoom_step(tpd, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, &sample_rate[rate], 1, total);
          int32 more = tbv_zoom_step(tpd, dir, frequency_per_div, ACQ_TB_FIRST, ACQ_TB_LAST, sample_rate, ACQ_RATES, total);
          const char *n;

          state(total, rate, tpd, 0);
          T_EQ(acq_view_zoom(dir, 0), (want < 0) ? 0 : ACQ_SET_TB);
          T_EQ(scopesettings.samplerate, rate);
          T_EQ(scopesettings.timeperdiv, (want < 0) ? tpd : want);
          T_TRUE(tbv_viable(sample_rate[rate], frequency_per_div[scopesettings.timeperdiv], total));

          n = acq_view_notice();
          if((want < 0) && (more >= 0))
          {
            T_TRUE(n != 0);
            if(n)
              T_STR(n, "Stopped: press RUN to zoom further");
          }
          else
            T_TRUE(n == 0);
        }

        //A stopped box: only what the record can show
        state(total, rate, tpd, 0);
        T_EQ(acq_view_tb_ok(ACQ_TB_FIRST), tbv_viable(sample_rate[rate], frequency_per_div[ACQ_TB_FIRST], total));
      }
}

//ui-type: two notice slots, the time/div's first, each for its ACQ_NOTICE_MS; nothing lost, a new change replaces the queue
static void test_notice_queue(void)
{
  state(3000, 7, 27, 1);

  //One change, two notices: the time/div's shows first, the Width's after its time
  acq_view_notice_queue(0, "Time/div set to ", "1us", " for this rate");
  acq_view_notice_queue(1, "Width set to ", "5us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
  timer0ticks += ACQ_NOTICE_MS - 1;
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
  timer0ticks += 1;
  T_STR(acq_view_notice(), "Width set to 5us for this rate");
  timer0ticks += ACQ_NOTICE_MS - 1;
  T_STR(acq_view_notice(), "Width set to 5us for this rate");
  timer0ticks += 1;
  T_TRUE(acq_view_notice() == 0);

  //A trigger notice alone shows at once; a second one replaces it
  acq_view_notice_queue(1, "Idle set to ", "5us", " for this rate");
  T_STR(acq_view_notice(), "Idle set to 5us for this rate");
  acq_view_notice_queue(1, "Idle set to ", "10us", " for this rate");
  T_STR(acq_view_notice(), "Idle set to 10us for this rate");
  timer0ticks += ACQ_NOTICE_MS;
  T_TRUE(acq_view_notice() == 0);

  //A new time/div notice drops a pending trigger notice (a new change replaces the queue) and replaces a showing one,
  //which then has its own full time
  acq_view_notice_queue(0, "Time/div set to ", "1us", " for this rate");
  acq_view_notice_queue(1, "Width set to ", "5us", " for this rate");
  acq_view_notice_queue(0, "Time/div set to ", "2us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 2us for this rate");
  timer0ticks += ACQ_NOTICE_MS;
  T_TRUE(acq_view_notice() == 0);
  acq_view_notice_queue(0, "Time/div set to ", "1us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
  timer0ticks += 1000;
  acq_view_notice_queue(0, "Time/div set to ", "5us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 5us for this rate");
  timer0ticks += ACQ_NOTICE_MS - 1;
  T_STR(acq_view_notice(), "Time/div set to 5us for this rate");
  timer0ticks += 1;
  T_TRUE(acq_view_notice() == 0);

  //A trigger notice queued while the time/div's shows waits for it
  acq_view_notice_queue(0, "Time/div set to ", "1us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
  timer0ticks += 2000;
  acq_view_notice_queue(1, "Width set to ", "5us", " for this rate");
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
  timer0ticks += 2000;
  T_STR(acq_view_notice(), "Width set to 5us for this rate");
  timer0ticks += ACQ_NOTICE_MS;
  T_TRUE(acq_view_notice() == 0);
}

//The snap notice keeps its words
static void test_snap_notice(void)
{
  state(3000, 0, 20, 1);                                    //200 MSa/s at 200 us/div: the record is far too short
  T_EQ(acq_view_fit(1), 1);
  T_STR(acq_view_notice(), "Time/div set to 1us for this rate");
}

//The owner's tap (10-19 22:20 on 0.2.73): the Scope's slider track at y 460, left of the window. The touch-down alone
//(a still tap, no drag after it) moves the window to centre on the finger; fw 0.2.76 calls this at touch-down
//(statemachine.c scan_for_touch), 0.2.73 only once a drag had begun. Above the track (the area beside the cards) and
//inside the window nothing jumps.
static void scope_window(int32 *x, int32 *w)
{
  uint32 fs = sample_rate[scopesettings.samplerate], fpd = frequency_per_div[scopesettings.timeperdiv];
  int32  xend;

  tbv_window(fpgasettings.settriggerpoint, scopesettings.triggerhorizontalposition, fs, fpd,
             fpgasettings.totalsamples, &disp_first_sample, &xend);
  tbv_slider(disp_first_sample, fs, fpd, fpgasettings.totalsamples, x, w);
}

static void test_owner_tap(void)
{
  static const struct { uint16 total; uint8 rate, tpd; } cells[] =
  {
    { 3000, 5, 25 },                                        //5 MSa/s, 5 us/div
    { 16382, 5, 26 },                                       //the deep record of walk-through 49 step 3
    { 3000, 0, 28 },                                        //200 MSa/s, 500 ns/div
    { 16382, 0, 31 },                                       //50 ns/div: an 8 px window
    { 6000, 8, 21 },
  };
  uint32 i;

  for(i = 0; i < sizeof(cells) / sizeof(cells[0]); i++)
  {
    int32 x, w, x2, w2, tap, pos, c;

    state(cells[i].total, cells[i].rate, cells[i].tpd, 0);
    scope_window(&x, &w);
    T_TRUE(w < SOV_W);
    T_TRUE(x > SOV_X);                                      //the window starts right of the track's left end

    //Left of the window, in the track
    tap = SOV_X + (x - SOV_X) / 3;
    pos = scopesettings.triggerhorizontalposition;
    scopesettings.triggerhorizontalposition = acq_view_slider_jump(pos, tap, 460);
    T_TRUE(scopesettings.triggerhorizontalposition != pos);
    scope_window(&x2, &w2);
    T_EQ(w2, w);
    c = x2 + w2 / 2;
    T_TRUE((x2 == SOV_X) || ((c - tap <= 2) && (tap - c <= 2)));   //centred on the finger, or at the track's start
    T_TRUE(x2 < x);

    //Inside the window now: a second still tap does not move it
    pos = scopesettings.triggerhorizontalposition;
    T_EQ(acq_view_slider_jump(pos, x2 + w2 / 2, 460), pos);

    //The area beside the cards (y 436..449) and the plot above: never a jump
    T_EQ(acq_view_slider_jump(pos, SOV_X + SOV_W - 3, 440), pos);
    T_EQ(acq_view_slider_jump(pos, SOV_X + SOV_W - 3, SOV_TAP_Y - 1), pos);

    //Right of it, at the track's far end: the window ends at the track's end (the record's end)
    scopesettings.triggerhorizontalposition = acq_view_slider_jump(pos, SOV_X + SOV_W - 1, 470);
    scope_window(&x2, &w2);
    T_TRUE(x2 + w2 >= SOV_X + SOV_W - 1);
  }

  //X-Y: no slider
  state(3000, 5, 25, 0);
  scopesettings.xymodedisplay = 1;
  T_EQ(acq_view_slider_jump(100, 10, 460), 100);
}

//The Spectrum's tap, the same rule (spectrum.c spectrum_touch: sov_jump at touch-down on the drawn window): a window of
//a quarter of the track at its right, a tap at y 460 left of it
static void test_spectrum_tap(void)
{
  int32 wx = SOV_X + SOV_W - SOV_W / 4, ww = SOV_W / 4, dx;

  dx = sov_jump(100, 460, wx, ww);
  T_EQ(wx + dx + ww / 2, 100);
  T_EQ(sov_jump(wx + 5, 460, wx, ww), 0);
  T_EQ(sov_jump(100, SOV_TAP_Y - 1, wx, ww), 0);
}

//fw 0.2.77: the time/div the Spectrum pairs with the rate its span sets: viable at that rate (no snap, no notice), and
//the Scope ladder's own time/div for the rate wherever the ladder has one, nearest the current one
static void test_tb_for_rate(void)
{
  uint32 d;
  int32  r, t, cur, u;

  for(d = 0; d < sizeof(totals) / sizeof(totals[0]); d++)
    for(r = 0; r < ACQ_RATES; r++)
      for(cur = ACQ_TB_FIRST; cur <= ACQ_TB_LAST; cur += 3)
      {
        int on_ladder = 0;

        state(totals[d], 0, cur, 1);
        t = acq_view_tb_for_rate((uint32)r);

        for(u = ACQ_TB_FIRST; u <= ACQ_TB_LAST; u++)
          if(ladder(totals[d], u) == r)
            on_ladder = 1;

        if(t < 0)
        {
          T_EQ(on_ladder, 0);
          continue;
        }
        T_TRUE((t >= ACQ_TB_FIRST) && (t <= ACQ_TB_LAST));
        T_TRUE(tbv_viable(sample_rate[r], frequency_per_div[t], totals[d]));
        if(on_ladder)
        {
          T_EQ(ladder(totals[d], t), r);
          for(u = ACQ_TB_FIRST; u <= ACQ_TB_LAST; u++)
            if(ladder(totals[d], u) == r)
              T_TRUE(((u > cur) ? u - cur : cur - u) >= ((t > cur) ? t - cur : cur - t));
        }
      }

  T_EQ(acq_view_tb_for_rate(ACQ_RATES), -1);
}

//fw 0.2.84 (owner, 0.2.76): the ACQ menu's Memory column. A filter hides the depths the 16-bit layout cannot hold; Off
//offers every depth the bitstream holds again. The menu redraws the column from this on every Filter tap
//(pc_glue_decim_tap), so a cycle through the filters ends where it began.
static void test_mem_chips(void)
{
  static const uint32 maxes[] = { 3000, 6000, 8190, 16382, 16384 };
  uint32 k, i, f;

  T_EQ(acq_view_mem_chips(16382, 0, 8190), 0x1Fu);          //Atlan's AL3 record: 3000 .. 15000
  T_EQ(acq_view_mem_chips(16382, 1, 8190), 0x03u);          //a filter: 3000, 6000
  //ui-sample: Sample Peak runs the same 16-bit layout (pc_hal_scope.c pc_dec_ui_wide passes it as filter_on): two chips,
  //with the Filter off too; back to Normal with the Filter off, all five again
  T_EQ(acq_view_mem_chips(16382, 0 || 1, 8190), 0x03u);
  T_EQ(acq_view_mem_chips(16382, 0 || 0, 8190), 0x1Fu);
  T_EQ(acq_view_mem_chips(3000, 0, 8190), 0x01u);           //stock bitstream: 3000 only
  T_EQ(acq_view_mem_chips(16382, 0, 0), 0x1Fu);             //the filter limit only counts with a filter on

  for(k = 0; k < sizeof(maxes) / sizeof(maxes[0]); k++)
  {
    uint32 off = acq_view_mem_chips(maxes[k], 0, 8190);

    for(i = 0; i < ACQ_MEM_CHIPS; i++)
      T_EQ((off >> i) & 1, (3000 * (i + 1) <= maxes[k]) ? 1u : 0u);
    T_EQ(off >> ACQ_MEM_CHIPS, 0u);

    //Off, a filter, the next ones, Off again (the Filter box's cycle): on is a subset, Off restores the whole set
    for(f = 0; f < 8; f++)
    {
      uint32 on = acq_view_mem_chips(maxes[k], (int)(f & 3), 8190);

      T_EQ(on & ~off, 0u);
      T_EQ(acq_view_mem_chips(maxes[k], 0, 8190), off);
      if(f & 3)
        T_EQ(on, acq_view_mem_chips((maxes[k] < 8190) ? maxes[k] : 8190, 0, 8190));
    }
  }
}

int main(void)
{
  test_mem_chips();
  test_ladder();
  test_step_every_cell();
  test_walks();
  test_hand_rate_and_boxes();
  test_stopped();
  test_snap_notice();
  test_notice_queue();
  test_owner_tap();
  test_spectrum_tap();
  test_tb_for_rate();

  return t_report("test_acq_view");
}
