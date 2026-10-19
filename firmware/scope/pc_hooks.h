//----------------------------------------------------------------------------------------------------------------------------------
// pc_hooks.h - the PC protocol's hooks into the 1.00o5 code: each macro marks one place where the protocol, the views or
// the UI changes reach into a 1.00o5 file (firmware/README.md section 5 lists them).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_HOOKS_H
#define PC_HOOKS_H

//UI features the running hardware may lack (fw 0.2.6, firmware/README.md "Hidden menu options"). PC_UI_HAS(f) goes
//into a touch condition, PC_UI_MASK(m) paints over the unsupported parts of a menu just drawn.
#define PC_FEAT_GEN_DDS       1     //generator menu: Atlan DDS bitstreams only (not stock 0x1432)
#define PC_FEAT_RTC           4     //DS3231 module (probed at boot)
#define PC_FEAT_EXT_TRIGGER   6     //EXT trigger source: AL3 bitstream 0x1533+ only (calibration pin as input)
#define PC_FEAT_DECIM         8     //acquisition "Filter" selector: AL3 bitstream 0x1534+ (decimation filter)
#define PC_FEAT_MEM_BASE      16    //+ i: memory depth box i (3000 x (i + 1) samples) fits the bitstream

//Settings sector word for the acquisition Filter setting: FPGA_SETTING_OFFSET (192) + 15, unused by 1.00o5
#define PC_SETTING_WORD_DECIM 207
#define PC_SETTING_WORD_SPEC0 208   //fw 0.2.39: spectrum view FFT length, window, scale (0x5E00 marker)
#define PC_SETTING_WORD_SPEC1 209   //fw 0.2.39: spectrum view ref level, dBV (int16)
#define PC_SETTING_WORD_PHOS  210   //fw 0.2.46: phosphor view persistence, palette (0x9A00 marker)

//PC_UI_MASK(m) items. Menus are compact (fw 0.2.8, owner: no gaps): what the 1013D lacks is not laid out at all.
#define PC_MASK_TRIG_CHANNEL  2     //trigger menu: EXT source box (0x1533+)
#define PC_MASK_ACQ_FILTER    4     //acquisition menu: the Filter box
#define PC_MASK_ACQ_MEMORY    5     //acquisition menu: memory depths the bitstream holds, pass time


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
void pc_glue_main_menu_mode(int row); //fw 0.2.51: main menu Scope or Spectrum analyser row chosen (ui_menu.h)
void pc_glue_acq_display_draw(void);  //fw 0.2.51: acquisition menu: the Display row (Normal / Phosphor / X-Y)
void pc_glue_acq_display_tap(void);   //fw 0.2.51: acquisition menu: touch on the Display row
int  pc_glue_autoset(void);           //fw 0.2.51: AUTO SET tapped: the spectrum view's Auto (1 = handled there)
int  pc_glue_autoset_running(void);   //fw 0.2.51: the spectrum view's Auto runs: AUTO SET reads "Auto..."
int  pc_glue_phosphor_on(void);       //fw 0.2.46: the phosphor view replaces the CH1 / CH2 polylines
void pc_glue_phosphor_draw(void);     //after the channel blocks: paint the phosphor maps and the strip
int  pc_glue_phosphor_xy(uint32 index); //XY branch: 1 = the phosphor view drew it
void pc_glue_phosphor_strip(void);    //scope_preset_values: the phosphor strip over the slider

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
uint16 pc_glue_phosphor_setting_word(void);      //settings sector word 210 (scope_save_config_data)

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
#define PC_MAIN_MENU_MODE(row) pc_glue_main_menu_mode(row)
#define PC_ACQ_DISPLAY_DRAW()   pc_glue_acq_display_draw()
#define PC_ACQ_DISPLAY_TAP()    pc_glue_acq_display_tap()
#define PC_AUTOSET()            pc_glue_autoset()
#define PC_AUTOSET_RUNNING()    pc_glue_autoset_running()
#define PC_PHOSPHOR_SKIP()      if(!pc_glue_phosphor_on())
#define PC_PHOSPHOR_DRAW()      pc_glue_phosphor_draw()
#define PC_PHOSPHOR_XY(i, last) do { if(pc_glue_phosphor_xy(i)) (i) = (last); } while(0)
#define PC_PHOSPHOR_STRIP()     pc_glue_phosphor_strip()

//fw 0.2.58 (0x1536 sequencer, pc_seq_hw.h): fpga_do_conversion writes the geometry and the sequencer registers before
//the sampling reset; the software average is skipped while the traces hold a hardware average
void   pc_seq_ui_pre_reset(void);
int    pc_seq_sw_average(void);
#define PC_SEQ_PRE_RESET()      pc_seq_ui_pre_reset()
#define PC_SW_AVERAGE()         && pc_seq_sw_average()


//Protocol 1.3 has no snapshot to restore: the power-off save stores the live settings as 1.00o5 does. fw 0.2.52: the
//save that follows closes the session in the unit life record (pc_diag.c)
void pc_glue_power_off(void);
#define PC_POWER_OFF()          pc_glue_power_off()

//fw 0.2.52 diagnostics readers (pc_diag.c, protocol row 55): the unit life record in settings words 224..255 is stored by
//every settings save; in-session save results; the calibration provenance trailer in sector 708 bytes 432..487
void pc_glue_settings_store(uint16 *w);
void pc_glue_settings_saved(int32 rc);
void pc_glue_cal_loaded(const uint8 *sector);
void pc_glue_cal_save(uint8 *sector);
#define PC_SETTINGS_STORE(w)    pc_glue_settings_store(w)
#define PC_SETTINGS_SAVED(rc)   pc_glue_settings_saved(rc)
#define PC_CAL_LOADED(b)        pc_glue_cal_loaded(b)
#define PC_CAL_SAVE(b)          pc_glue_cal_save(b)

//fw 0.2.48 (pc_mmu.h): main, right after 1.00o5's cache enables and before interrupts: flat page table, MMU on, so the
//D-cache and the write buffer take effect
#if PC_MMU
#include "pc_mmu.h"
#define PC_MMU_ENABLE()         pc_mmu_enable()
#else
#define PC_MMU_ENABLE()         do { } while(0)
#endif

#endif /* PC_HOOKS_H */
