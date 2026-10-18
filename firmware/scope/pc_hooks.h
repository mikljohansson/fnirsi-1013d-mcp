//----------------------------------------------------------------------------------------------------------------------------------
// pc_hooks.h - the PC protocol's hooks into the 1.00o5 code. With PC_PROTOCOL=0 (or undefined) every hook is an empty
// statement, so the firmware compiles to the same image as without the protocol (make check-reference proves it).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_HOOKS_H
#define PC_HOOKS_H

//UI features the running hardware may lack (fw 0.2.6, firmware/README.md "Hidden menu options"). PC_UI_HAS(f) goes
//into a touch condition, PC_UI_MASK(m) paints over the unsupported parts of a menu just drawn.
#define PC_FEAT_GEN_DDS       1     //generator menu: Atlan DDS bitstreams only (not stock 0x1432)
#define PC_FEAT_GEN_WAVE      2     //SINE / TRGL waveform: no DAC on the 1013D
#define PC_FEAT_GEN_ANALOG    3     //amplitude / offset / phase: no DAC
#define PC_FEAT_RTC           4     //DS3231 module (probed at boot)
#define PC_FEAT_LONG_MEMORY   5     //"Long memory": not implemented in 1.00o5
#define PC_FEAT_EXT_TRIGGER   6     //EXT trigger source: AL3 bitstream 0x1533+ only (calibration pin as input)
#define PC_FEAT_HOLDOFF       7     //trigger hold-off: never sent to the FPGA
#define PC_FEAT_MEM_BASE      16    //+ i: memory depth box i (3000 x (i + 1) samples) fits the bitstream

//Compacted menus (fw 0.2.8, owner: no gaps). The kept rows move up by whole rows; the menu heights in variables.h
//shrink to match. With PC_PROTOCOL=0 the shifts are 0 and nothing moves.
#if PC_PROTOCOL
#define PC_GEN_COMPACT        1     //generator: Power, Freq, Duty (no Wave / Ampl / Offset / Phase rows)
#define PC_GEN_DY             56    //Freq and Duty move up one row
#define PC_TRIG_COMPACT       1     //trigger: no Hold off row
#define PC_TRIG_DY            56    //Always 50% moves up one row
#else
#define PC_GEN_COMPACT        0
#define PC_GEN_DY             0
#define PC_TRIG_COMPACT       0
#define PC_TRIG_DY            0
#endif

#define PC_MASK_GEN_MENU      1
#define PC_MASK_TRIG_CHANNEL  2
#define PC_MASK_TRIG_MENU     3
#define PC_MASK_ACQ_LONG      4
#define PC_MASK_ACQ_MEMORY    5
#define PC_MASK_SYS_RTC       6

#if PC_PROTOCOL

void pc_glue_init(void);              //after the settings are loaded, before usb_device_init()
void pc_glue_poll_main(void);         //top of the main loop
void pc_glue_poll_busy(void);         //UI busy loops and touch scans: no FPGA access
void pc_glue_poll_isr(void);          //1 ms timer interrupt: safe commands and the TX pump only
void pc_glue_ui_service(void);        //main loop, after the MAIN poll: MCP indicator
int  pc_glue_ui_arm_allowed(void);    //0 while a host READ_RAW waits for the UI capture in flight
int  pc_glue_rtc_allowed(void);       //0 while the MCP chip occupies the RTC clock slot
void pc_glue_pass_mark(int n);        //main loop stage reached (first-pass timing, GET_LINK_STATS)
int  pc_glue_ui_has(int feature);     //PC_FEAT_*: 1 when the running hardware supports it
char *pc_glue_version(int form);      //0 "v0.2.7", 1 "fw 0.2.7", 2 full identity line (diagnostics)
void pc_glue_topbar_drawn(void);      //the top bar / RUN-STOP chip was (re)drawn: put the MCP chip back with it
void pc_glue_diag_drawn(void);        //diagnostics screen drawn: identity line
void pc_glue_ext_selected(void);      //EXT trigger chosen in the trigger menu
void pc_glue_roll_label(void);        //acquisition menu: the long (roll) time base box as a state toggle
int  pc_glue_battery_level(int raw, int charging, int shown);   //filtered battery level for the indicator
void pc_glue_ui_mask(int menu);       //PC_MASK_*: paint over what it does not support (current draw buffer)
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
#define PC_PASS_MARK(n)         pc_glue_pass_mark(n)
#define PC_UI_HAS(f)            && pc_glue_ui_has(f)
#define PC_UI_MASK(m)           pc_glue_ui_mask(m)
#define PC_UI_VERSION(d, form)  ((int8 *)pc_glue_version(form))
#define PC_TOPBAR_DRAWN()       pc_glue_topbar_drawn()
#define PC_DIAG_DRAWN()         pc_glue_diag_drawn()
#define PC_EXT_SELECTED()       pc_glue_ext_selected()
#define PC_ROLL_LABEL()         pc_glue_roll_label()

#else

#define PC_BUSY_POLL()          do { } while(0)
#define PC_TIMER_TICK()         do { } while(0)
#define PC_UI_CAPTURE_DONE()    do { } while(0)
#define PC_UI_CAPTURE_START()   do { } while(0)
#define PC_UI_CAPTURE_ADDR(a)   do { } while(0)
#define PC_UI_ARM_ALLOWED()
#define PC_RTC_ALLOWED()
#define PC_PASS_MARK(n)         do { } while(0)
#define PC_UI_HAS(f)
#define PC_UI_MASK(m)           do { } while(0)
#define PC_UI_VERSION(d, form)  (d)
#define PC_TOPBAR_DRAWN()       do { } while(0)
#define PC_DIAG_DRAWN()         do { } while(0)
#define PC_EXT_SELECTED()       do { } while(0)
#define PC_ROLL_LABEL()         do { } while(0)

#endif

//Protocol 1.3 has no snapshot to restore: the power-off save stores the live settings as 1.00o5 does
#define PC_POWER_OFF()          do { } while(0)

#endif /* PC_HOOKS_H */
