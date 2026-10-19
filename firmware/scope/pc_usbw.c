//----------------------------------------------------------------------------------------------------------------------------------
// USB suspend watch (fw 0.2.49): see pc_usbw.h. Called with IRQs off (USB ISR, timer ISR), so no locking here.
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_usbw.h"

void pc_usbw_init(pc_usbw *w)
{
  uint32_t inferred = w->inferred, reconnects = w->reconnects;

  //Counters are since boot; everything else starts over (the controller was just reset and soft-connected)
  *w = (pc_usbw){ 0 };
  w->inferred   = inferred;
  w->reconnects = reconnects;
}

void pc_usbw_suspend(pc_usbw *w, uint32_t now, uint16_t frame)
{
  if(w->off)
    return;

  w->susp       = 1;
  w->susp_ms    = now;
  w->susp_frame = frame;

  if(w->strikes < 255)
    w->strikes++;
}

void pc_usbw_resume(pc_usbw *w)
{
  if(w->off)
    return;

  w->susp    = 0;
  w->strikes = 0;
}

void pc_usbw_reset(pc_usbw *w)
{
  //A port reset (or the end of the session) ends the suspend; it is not proof that the host is happy with the device
  //(a host retrying enumeration resets again and again), so the strikes stay until SET_CONFIGURATION
  if(w->off)
    return;

  w->susp = 0;
}

void pc_usbw_configured(pc_usbw *w)
{
  if(w->off)
    return;

  w->enumerated = 1;
  w->strikes    = 0;
  w->level      = 0;
}

int pc_usbw_activity(pc_usbw *w, uint8_t cause)
{
  (void)cause;

  if(w->off || !w->susp)
    return 0;

  w->susp    = 0;
  w->strikes = 0;
  w->inferred++;
  return 1;
}

uint32_t pc_usbw_storm_wait(const pc_usbw *w)
{
  return PC_USBW_STORM_QUIET_MS << w->level;
}

int pc_usbw_tick(pc_usbw *w, uint32_t now, uint16_t frame)
{
  uint32_t dt;

  if(w->off)
  {
    if((now - w->off_ms) < PC_USBW_OFF_MS)
      return PC_USBW_NONE;

    //Connected again: wait for the host as after a suspend, without the strikes (the reconnect itself idles the bus)
    //and without arming the long rule until a host enumerates the device
    w->off        = 0;
    w->susp       = 1;
    w->susp_ms    = now;
    w->susp_frame = frame;
    w->strikes    = 0;
    w->enumerated = 0;
    return PC_USBW_CONNECT;
  }

  if(!w->susp)
    return PC_USBW_NONE;

  //SOFs again: the bus is no longer suspended, whether or not the core raised RESUME
  if(frame != w->susp_frame)
  {
    w->susp    = 0;
    w->strikes = 0;
    w->inferred++;
    return PC_USBW_RESUMED;
  }

  dt = now - w->susp_ms;

  if((w->strikes >= PC_USBW_STORM_SUSPENDS) && (dt >= pc_usbw_storm_wait(w)))
  {
    w->why = PC_USBW_WHY_STORM;

    if(w->level < PC_USBW_LEVEL_MAX)
      w->level++;
  }
  else if(w->enumerated && (dt >= PC_USBW_LONG_MS))
    w->why = PC_USBW_WHY_LONG;
  else
    return PC_USBW_NONE;

  w->off    = 1;
  w->off_ms = now;
  w->susp   = 0;
  w->reconnects++;
  return PC_USBW_DISCONNECT;
}
