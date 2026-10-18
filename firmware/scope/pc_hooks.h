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
int  pc_glue_remote(void);            //1 while the host owns the scope
void pc_glue_remote_service(void);    //main loop while REMOTE
void pc_glue_ui_capture(void);        //UI finished reading a capture
void pc_glue_power_off(void);         //top of the power-off ISR, before the settings are saved

#define PC_BUSY_POLL()        pc_glue_poll_busy()
#define PC_TIMER_TICK()       pc_glue_poll_isr()
#define PC_UI_CAPTURE_DONE()  pc_glue_ui_capture()
#define PC_POWER_OFF()        pc_glue_power_off()

#else

#define PC_BUSY_POLL()        do { } while(0)
#define PC_TIMER_TICK()       do { } while(0)
#define PC_UI_CAPTURE_DONE()  do { } while(0)
#define PC_POWER_OFF()        do { } while(0)

#endif

#endif /* PC_HOOKS_H */
