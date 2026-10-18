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
#define PC_FEAT_DECIM         8     //acquisition "Filter" selector: AL3 bitstream 0x1534+ (decimation filter)
#define PC_FEAT_MEM_BASE      16    //+ i: memory depth box i (3000 x (i + 1) samples) fits the bitstream

//Settings sector word for the acquisition Filter setting: FPGA_SETTING_OFFSET (192) + 15, unused by 1.00o5
#define PC_SETTING_WORD_DECIM 207
#define PC_SETTING_WORD_SPEC0 208   //fw 0.2.39: spectrum view FFT length, window, scale (0x5E00 marker)
#define PC_SETTING_WORD_SPEC1 209   //fw 0.2.39: spectrum view ref level, dBV (int16)

//Compacted menus (fw 0.2.8, owner: no gaps). The kept rows move up by whole rows; the menu heights in variables.h
//shrink to match. With PC_PROTOCOL=0 the shifts are 0 and nothing moves.
#if PC_PROTOCOL
#define PC_GEN_COMPACT        1     //generator: Power, Freq, Duty (no Wave / Ampl / Offset / Phase rows)
#define PC_GEN_DY             56    //Freq and Duty move up one row
#define PC_TRIG_COMPACT       1     //trigger: no Hold off row
#define PC_TRIG_DY            56    //Always 50% moves up one row
#define PC_CH_COMPACT         1     //channel menu: no FFT row (1.00o5 stores fftenable and draws nothing)
#define PC_CH_DY              56    //Invert, Coupling, V/A move up one row
#else
#define PC_GEN_COMPACT        0
#define PC_GEN_DY             0
#define PC_TRIG_COMPACT       0
#define PC_TRIG_DY            0
#define PC_CH_COMPACT         0
#define PC_CH_DY              0
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
void pc_glue_boot_splash(void);       //boot: show scope.bmp if the card has it, otherwise nothing (no error box)
void pc_glue_ui_mask(int menu);       //PC_MASK_*: paint over what it does not support (current draw buffer)
void pc_glue_ui_capture_start(void);  //fpga_do_conversion started a conversion
void pc_glue_ui_capture_addr(uint32 addr); //0x14 read of the finished UI capture
void pc_glue_ui_capture(void);        //UI finished reading a capture
int  pc_glue_spectrum_display(void);  //fw 0.2.28: spectrum mode drew the trace area (1) instead of the traces
int  pc_glue_spectrum_touch(void);    //touch in the trace area while the spectrum shows: 1 = handled
int  pc_glue_main_menu_row6(uint32 y);//main menu: touch on the Spectrum analyzer row
void pc_glue_main_menu_items(void);   //main menu drawn: add the Spectrum analyzer row
void pc_glue_main_menu_touch(void);   //Spectrum analyzer row tapped: switch the display mode
int  pc_glue_main_menu_height(void);  //main menu box height (variables.h MAIN_MENU_HEIGHT)

//Decimation filter (bitstream 0x1534+, pc_decim.h). With it off or absent every call is a no-op and the 1.00o5 paths run.
extern int16 *pc_decim_src;           //fpga_read_adc_data: converted samples to take instead of bus reads (0 = bus)
void   pc_glue_divider_written(uint32 divider);   //fpga_set_sample_rate wrote 0x0D
void   pc_glue_ui_decim_arm(void);    //scope_acquire_trace_data: the next fpga_do_conversion uses the UI filter setting
void   pc_glue_decim_conversion(void);//fpga_do_conversion, before the sampling reset: write the mode, settle
uint32 pc_glue_decim_words(uint32 samples);       //ring words for a sample count (samples / 2, or samples in 16-bit)
uint32 pc_glue_decim_pre_words(uint32 samples);   //the same for the pretrigger, less the EXT latency shift in 16-bit
uint32 pc_glue_decim_ui_start(uint32 start);      //UI capture start word: (0x14 - pretrigger) mod 8192 in 16-bit
void   pc_glue_decim_read(void *settings, uint32 start);   //fpga_read_sample_data: 16-bit read + convert, odd pass
void   pc_glue_decim_even(void);      //fpga_read_sample_data: second pass takes the even samples
void   pc_glue_decim_tap(void);       //acquisition menu: Filter box touched
void   pc_glue_settings_defaults(void); //fw 0.2.44: scope_reset_config_data (DEFAULT button, sector load failure)
uint16 pc_glue_decim_setting_word(void);         //settings sector word (scope_save_config_data)
uint16 pc_glue_spectrum_setting_word(int i);     //settings sector words 208, 209 (scope_save_config_data)

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
#define PC_SPECTRUM_DISPLAY()   do { if(pc_glue_spectrum_display()) return; } while(0)
#define PC_SETTINGS_DEFAULTS()  pc_glue_settings_defaults()
#define PC_SPECTRUM_TOUCH()     do { if(pc_glue_spectrum_touch()) return; } while(0)
#define PC_MAIN_MENU_ROW6(y)    pc_glue_main_menu_row6(y)
#define PC_MAIN_MENU_ITEMS()    pc_glue_main_menu_items()
#define PC_MAIN_MENU_TOUCH()    pc_glue_main_menu_touch()

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
#define PC_SPECTRUM_DISPLAY()   do { } while(0)
#define PC_SETTINGS_DEFAULTS()  do { } while(0)
#define PC_SPECTRUM_TOUCH()     do { } while(0)
#define PC_MAIN_MENU_ROW6(y)    0
#define PC_MAIN_MENU_ITEMS()    do { } while(0)
#define PC_MAIN_MENU_TOUCH()    do { } while(0)

#endif

//Protocol 1.3 has no snapshot to restore: the power-off save stores the live settings as 1.00o5 does
#define PC_POWER_OFF()          do { } while(0)

#endif /* PC_HOOKS_H */
