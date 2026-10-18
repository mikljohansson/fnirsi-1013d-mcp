//----------------------------------------------------------------------------------------------------------------------------------
// pc_hooks.h - the PC protocol's hooks into the 1.00o5 code. With PC_PROTOCOL=0 (or undefined) every hook is an empty
// statement, so the firmware compiles to the same image as without the protocol (make check-reference proves it).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_HOOKS_H
#define PC_HOOKS_H

#if PC_PROTOCOL

void pc_glue_init(void);              //after the settings are loaded, before usb_device_init()
void pc_glue_poll_main(void);         //top of the main loop
void pc_glue_poll_busy(void);         //UI busy loops and touch scans: no FPGA access
void pc_glue_poll_isr(void);          //1 ms timer interrupt: safe commands and the TX pump only
void pc_glue_ui_service(void);        //main loop, after the MAIN poll: MCP indicator
int  pc_glue_ui_arm_allowed(void);    //0 while a host READ_RAW waits for the UI capture in flight
int  pc_glue_rtc_allowed(void);       //0 while the MCP chip occupies the RTC clock slot
void pc_glue_ui_capture_start(void);  //fpga_do_conversion started a conversion
void pc_glue_ui_capture_addr(uint32 addr); //0x14 read of the finished UI capture
void pc_glue_ui_capture(void);        //UI finished reading a capture

#define PC_BUSY_POLL()          pc_glue_poll_busy()
#define PC_TIMER_TICK()         pc_glue_poll_isr()
#define PC_UI_CAPTURE_DONE()    pc_glue_ui_capture()
#define PC_UI_CAPTURE_START()   pc_glue_ui_capture_start()
#define PC_UI_CAPTURE_ADDR(a)   pc_glue_ui_capture_addr(a)
#define PC_UI_ARM_ALLOWED()     && pc_glue_ui_arm_allowed()
#define PC_RTC_ALLOWED()        && pc_glue_rtc_allowed()

#else

#define PC_BUSY_POLL()          do { } while(0)
#define PC_TIMER_TICK()         do { } while(0)
#define PC_UI_CAPTURE_DONE()    do { } while(0)
#define PC_UI_CAPTURE_START()   do { } while(0)
#define PC_UI_CAPTURE_ADDR(a)   do { } while(0)
#define PC_UI_ARM_ALLOWED()
#define PC_RTC_ALLOWED()

#endif

//Protocol 1.3 has no snapshot to restore: the power-off save stores the live settings as 1.00o5 does
#define PC_POWER_OFF()          do { } while(0)

#endif /* PC_HOOKS_H */
