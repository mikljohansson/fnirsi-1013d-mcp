//----------------------------------------------------------------------------------------------------------------------------------
// Host test for pc_usbw.c (fw 0.2.49): the USB suspend watch. A small bus model ticks the watch every ms the way the
// 1 ms timer interrupt does, with the frame number frozen while the bus is idle and moving while SOFs arrive.
// Scenarios: suspend -> RESUME interrupt, suspend -> inferred resume (SOFs, EP0, EP2), port reset lead-in, boot without
// a host, long suspend -> soft reconnect (once per enumeration), retry storm -> soft reconnect with back-off.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "pc_usbw.h"

static pc_usbw  W;
static uint32_t now;
static uint16_t frame;
static int      sof;                 //SOFs arrive (frame number moves every ms)
static int      n_resumed, n_disc, n_conn;
static uint32_t t_disc, t_conn;

static void reset_model(uint32_t t0)
{
  W = (pc_usbw){ 0 };
  pc_usbw_init(&W);
  now = t0; frame = 100; sof = 1;
  n_resumed = n_disc = n_conn = 0;
  t_disc = t_conn = 0;
}

//Advance ms by ms; returns at the first non-NONE action if stop is set
static int run(uint32_t ms, int stop)
{
  uint32_t i;
  int a;

  for(i=0;i<ms;i++)
  {
    now++;

    if(sof)
      frame = (uint16_t)((frame + 1) & 0x7FF);

    a = pc_usbw_tick(&W, now, frame);

    if(a == PC_USBW_RESUMED)    n_resumed++;
    if(a == PC_USBW_DISCONNECT) { n_disc++; t_disc = now; }
    if(a == PC_USBW_CONNECT)    { n_conn++; t_conn = now; }

    if(stop && (a != PC_USBW_NONE))
      return a;
  }

  return PC_USBW_NONE;
}

//The host stops SOFs; the core reports SUSPEND 3 ms later
static void host_suspends(void)
{
  sof = 0;
  run(3, 0);
  pc_usbw_suspend(&W, now, frame);
}

//Enumeration after a port reset: SE0 (suspend first at high speed), RESET, SOFs, SET_CONFIGURATION
static void host_enumerates(void)
{
  host_suspends();
  run(5, 0);
  pc_usbw_reset(&W);
  sof = 1;
  run(50, 0);
  pc_usbw_activity(&W, PC_USBW_CAUSE_EP0);
  pc_usbw_configured(&W);
  run(10, 0);
}

static void test_resume_irq(void)
{
  reset_model(1000);
  host_enumerates();
  host_suspends();
  T_EQ(W.susp, 1); T_EQ(W.strikes, 1);
  run(60000, 0);
  T_EQ(n_resumed + n_disc, 0);                                  //one minute idle: nothing
  pc_usbw_resume(&W);
  sof = 1;
  run(100, 0);
  T_EQ(W.susp, 0); T_EQ(W.strikes, 0); T_EQ(n_resumed, 0); T_EQ(W.inferred, 0);
}

static void test_inferred_frame(void)
{
  reset_model(1000);
  host_enumerates();
  host_suspends();
  run(10 * 60000, 0);                                           //10 min suspended, below the long limit
  T_EQ(n_resumed + n_disc, 0);
  sof = 1;                                                      //host resumed; the core raised no RESUME
  T_EQ(run(5, 1), PC_USBW_RESUMED);
  T_EQ(W.susp, 0); T_EQ(W.inferred, 1); T_EQ(W.strikes, 0);
  run(30 * 60000, 0);                                           //active bus for 30 min: no action
  T_EQ(n_resumed, 1); T_EQ(n_disc, 0);

  //Frame number wrap: 2047 -> 0 counts as moved
  reset_model(5);
  sof = 0; frame = 2047;
  pc_usbw_suspend(&W, now, frame);
  frame = 0;
  T_EQ(pc_usbw_tick(&W, ++now, frame), PC_USBW_RESUMED);
}

static void test_inferred_activity(void)
{
  reset_model(1000);
  host_enumerates();
  host_suspends();
  run(2000, 0);
  T_EQ(pc_usbw_activity(&W, PC_USBW_CAUSE_EP0), 1);
  T_EQ(pc_usbw_activity(&W, PC_USBW_CAUSE_EP2), 0);            //already resumed
  T_EQ(W.inferred, 1); T_EQ(W.susp, 0);
  T_EQ(pc_usbw_activity(&W, PC_USBW_CAUSE_EP2), 0);
  host_suspends();
  T_EQ(pc_usbw_activity(&W, PC_USBW_CAUSE_EP2), 1);
  T_EQ(W.inferred, 2);
}

static void test_port_reset_lead_in(void)
{
  //Every port reset at high speed: suspend, RESET a few ms later, enumeration. Ten of them in a session: no action
  int i;

  reset_model(1000);

  for(i=0;i<10;i++)
  {
    host_enumerates();
    run(5000, 0);
  }

  T_EQ(n_resumed + n_disc + n_conn, 0); T_EQ(W.strikes, 0);
  run(24u * 3600u * 1000u / 8u, 0);                            //3 h of activity
  T_EQ(n_resumed + n_disc + n_conn, 0);
}

static void test_no_host(void)
{
  //Boot without a cable (or a charger only): one suspend, never enumerated. Nothing ever fires
  reset_model(700);
  sof = 0;
  pc_usbw_suspend(&W, now, frame);
  run(10u * 3600u * 1000u, 0);
  T_EQ(n_resumed + n_disc + n_conn, 0); T_EQ(W.reconnects, 0);

  //Cable bounce at plug-in into a charger: six suspends in 1 ms, then silence: one storm reconnect, then quiet
  //(the reconnect itself counts as no strike), so the back-off never has to run away
  reset_model(700);
  sof = 0;
  {
    int i;
    for(i=0;i<6;i++)
      pc_usbw_suspend(&W, now, frame);
  }
  run(10u * 3600u * 1000u, 0);
  T_EQ(n_disc, 1); T_EQ(n_conn, 1); T_EQ(t_disc - 700, PC_USBW_STORM_QUIET_MS); T_EQ(W.why, PC_USBW_WHY_STORM);
  T_EQ(t_conn - t_disc, PC_USBW_OFF_MS);
}

static void test_long(void)
{
  uint32_t t0;

  reset_model(1000);
  host_enumerates();
  host_suspends();
  t0 = now;
  T_EQ(run(PC_USBW_LONG_MS - 1, 1), PC_USBW_NONE);             //not one ms early
  T_EQ(run(1, 1), PC_USBW_DISCONNECT);
  T_EQ(now - t0, PC_USBW_LONG_MS); T_EQ(W.why, PC_USBW_WHY_LONG); T_EQ(W.reconnects, 1); T_EQ(W.off, 1);

  //Events while the pull-up is off change nothing
  pc_usbw_suspend(&W, now, frame);
  pc_usbw_reset(&W);
  pc_usbw_configured(&W);
  T_EQ(pc_usbw_activity(&W, PC_USBW_CAUSE_EP0), 0);
  T_EQ(W.enumerated, 1); T_EQ(W.strikes, 1);
  T_EQ(run(PC_USBW_OFF_MS - 1, 1), PC_USBW_NONE);
  T_EQ(run(1, 1), PC_USBW_CONNECT);
  T_EQ(W.susp, 1); T_EQ(W.strikes, 0); T_EQ(W.enumerated, 0);

  //Host asleep: the reconnect is not answered; the suspend that follows changes nothing either. Once per enumeration
  host_suspends();
  run(24u * 3600u * 1000u, 0);
  T_EQ(n_disc, 1); T_EQ(n_conn, 1);

  //The host wakes and enumerates the device again: the long rule is armed again
  host_enumerates();
  T_EQ(n_resumed, 0);
  host_suspends();
  run(PC_USBW_LONG_MS, 0);
  T_EQ(n_disc, 2); T_EQ(W.reconnects, 2);
  run(PC_USBW_OFF_MS, 0);
  T_EQ(n_conn, 2);

  //Resumed in time: no reconnect
  host_enumerates();
  host_suspends();
  run(PC_USBW_LONG_MS - 10, 0);
  sof = 1;
  run(PC_USBW_LONG_MS, 0);
  T_EQ(n_disc, 2); T_EQ(n_resumed, 1);
}

static void test_storm(void)
{
  int i;
  uint32_t last;

  //A host that keeps resetting and never configures the device, then gives up
  reset_model(1000);
  host_enumerates();
  T_EQ(W.enumerated, 1);

  for(i=0;i<3;i++)
  {
    host_suspends();
    run(5, 0);
    pc_usbw_reset(&W);
    sof = 1;
    run(400, 0);
  }

  host_suspends();                                              //gave up: quiet
  last = now;
  T_EQ(W.strikes, 4);
  T_EQ(run(PC_USBW_STORM_QUIET_MS, 1), PC_USBW_DISCONNECT);
  T_EQ(now - last, PC_USBW_STORM_QUIET_MS); T_EQ(W.why, PC_USBW_WHY_STORM); T_EQ(W.level, 1);
  T_EQ(run(PC_USBW_OFF_MS, 1), PC_USBW_CONNECT);

  //The host retries after the reconnect and fails again: the wait doubles
  for(i=0;i<3;i++)
  {
    host_suspends();
    run(5, 0);
    pc_usbw_reset(&W);
    sof = 1;
    run(400, 0);
  }

  host_suspends();
  last = now;
  T_EQ(run(2 * PC_USBW_STORM_QUIET_MS - 1, 1), PC_USBW_NONE);
  T_EQ(run(1, 1), PC_USBW_DISCONNECT);
  T_EQ(now - last, 2 * PC_USBW_STORM_QUIET_MS); T_EQ(W.level, 2);
  run(PC_USBW_OFF_MS, 0);

  //Back-off is capped
  W.level = PC_USBW_LEVEL_MAX;
  T_EQ(pc_usbw_storm_wait(&W), PC_USBW_STORM_QUIET_MS << PC_USBW_LEVEL_MAX);

  //A successful enumeration clears level and strikes
  host_enumerates();
  T_EQ(W.level, 0); T_EQ(W.strikes, 0);

  //Storm ended by SOFs before the quiet time: no reconnect
  for(i=0;i<3;i++)
  {
    host_suspends();
    run(5, 0);
    pc_usbw_reset(&W);
    sof = 1;
    run(400, 0);
  }

  host_suspends();
  run(PC_USBW_STORM_QUIET_MS - 100, 0);
  sof = 1;
  run(PC_USBW_STORM_QUIET_MS, 0);
  T_EQ(n_disc, 2); T_EQ(n_resumed, 1); T_EQ(W.strikes, 0);
}

static void test_init_and_wrap(void)
{
  //usb_device_init (MSC switch) starts over but keeps the counters
  reset_model(1000);
  host_enumerates();
  host_suspends();
  run(PC_USBW_LONG_MS, 0);
  T_EQ(W.off, 1); T_EQ(W.reconnects, 1);
  W.inferred = 7;
  pc_usbw_init(&W);
  T_EQ(W.off, 0); T_EQ(W.susp, 0); T_EQ(W.enumerated, 0); T_EQ(W.reconnects, 1); T_EQ(W.inferred, 7);
  run(PC_USBW_OFF_MS * 2, 0);
  T_EQ(n_conn, 0);                                              //no stray CONNECT after the init

  //ms counter wrap
  reset_model(0xFFFFFFFFu - 5000u);
  host_enumerates();
  host_suspends();
  run(PC_USBW_LONG_MS - 1, 0);
  T_EQ(n_disc, 0);
  run(1, 0);
  T_EQ(n_disc, 1);
}

int main(void)
{
  test_resume_irq();
  test_inferred_frame();
  test_inferred_activity();
  test_port_reset_lead_in();
  test_no_host();
  test_long();
  test_storm();
  test_init_and_wrap();

  return t_report("test_usbw");
}
