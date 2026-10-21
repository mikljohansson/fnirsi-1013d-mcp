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
#define PC_SETTING_WORD_TRIG  202   //ui-type: the trigger Type row, words 202 / 203 (marker 0xA000; trig_ui.h), the retired
                                    //1.00o5 hold-off slots
#define PC_SETTING_WORD_DECIM 207
#define PC_SETTING_WORD_SPEC0 208   //fw 0.2.39: spectrum view FFT length, window, scale (0x5E00 marker)
#define PC_SETTING_WORD_SPEC1 209   //fw 0.2.39: spectrum view ref level, dBV (int16)
#define PC_SETTING_WORD_PHOS  210   //fw 0.2.46: phosphor view persistence, palette (0x9A00 marker); 0.2.56: the switch
#define PC_SETTING_WORD_UI    211   //fw 0.2.56: Grid, the spectrum's Display variant (0xC700 marker)
#define PC_SETTING_WORD_DECIM_SPEC 212   //fw 0.2.64: the Spectrum's Filter (0xD500 marker; 207 is the Scope's)
#define PC_SETTING_WORD_BODE0 219   //fw 0.2.66: Bode plot start, stop, points (0xB000 marker)
#define PC_SETTING_WORD_BODE1 220   //fw 0.2.66: Bode plot settle, CH2 range (0xB100 marker); fw 0.2.69: Display variant,
                                    //cursors, frequency axis (bits 3..6)
#define PC_SETTING_WORD_BODE_MS 213 //fw 0.2.69: Frequency response MEASURES list, words 213..215 (bode_ui.h)
#define PC_SETTING_WORD_BODE2 216   //fw 0.2.71: Frequency response Trace style (0xB200 marker, bit 0)
//217: fw 0.2.84, the Spectrum MEASURES Capture card's rank (ui_meas.h UI_MS_SPEC_WORD_CAPTURE, 0xCA00 marker)

//PC_UI_MASK(m) items. Menus are compact (fw 0.2.8, owner: no gaps): what the 1013D lacks is not laid out at all.
#define PC_MASK_TRIG_CHANNEL  2     //trigger menu: EXT source box (0x1533+)
#define PC_MASK_ACQ_FILTER    4     //acquisition menu: the Filter box
#define PC_MASK_ACQ_MEMORY    5     //acquisition menu: memory depths the bitstream holds, pass time


void pc_glue_init(void);              //after the settings are loaded, before usb_device_init()
void pc_glue_poll_main(void);         //top of the main loop
void pc_glue_poll_busy(void);         //UI busy loops and touch scans: no FPGA access
void pc_glue_poll_isr(void);          //1 ms timer interrupt: safe commands and the TX pump only

//fw 0.2.78 (protocol 1.23, docs/ui-guidelines.md section 4): a host request that needs the main loop closes the modal
//screens. Every modal loop (menus, fold-outs, dialogs, the Settings screen, the file viewers, the first-run screen, the
//wizard between steps) checks PC_UI_YIELD() once per iteration, after its touch scan, and when it is 1 leaves by its own
//cancel path (a dialog = Cancel / No, a menu or fold-out = closed, a screen = Back / Exit / Later); never by a path that
//writes something the user did not confirm. Steps that must finish set a hold reason (E_BUSY with it, no yield):
//prev = pc_glue_ui_hold(PC_HOLD_*); ...; pc_glue_ui_hold(prev). The codes are the E_BUSY details (pc_cmds.h PC_BUSY_*).
#define PC_HOLD_CAL           3     //a calibration measurement (wizard step, volts calibration step)
#define PC_HOLD_SAVE          4     //a file save (SAVE PIC / SAVE WAVE, the report, the Bode CSV)
#define PC_HOLD_USB_DRIVE     5     //the USB drive screen (the serial port is gone anyway)
int  pc_glue_ui_yield(void);          //1: a host request waits for the base screen: leave by the cancel path
int  pc_glue_ui_hold(int why);        //set the hold reason (0 none), returns the previous one
#define PC_UI_YIELD()           pc_glue_ui_yield()
void pc_glue_ui_service(void);        //main loop, after the MAIN poll: MCP indicator
int  pc_glue_ui_arm_allowed(void);    //0 while a host READ_RAW waits for the UI capture in flight
int  pc_glue_host_busy(void);         //fw 0.2.68: a host acquisition in progress (the RUN/STOP chip and button read RUN)
void pc_glue_host_stop(void);         //fw 0.2.68: the RUN/STOP button's STOP during a host acquisition: abort it
int  pc_glue_rtc_allowed(void);       //0 while the MCP chip occupies the RTC clock slot
void pc_glue_pass_mark(int n);        //main loop stage reached (first-pass timing, GET_LINK_STATS)
void pc_glue_counter_pass(void);      //C7 counter: main loop after the capture readout: the 0x35 read (fpga_counter.h)
#define PC_COUNTER_PASS()       pc_glue_counter_pass()
int  pc_glue_ui_has(int feature);     //PC_FEAT_*: 1 when the running hardware supports it
int  pc_glue_fpga_mismatch(void);     //fw-one: the FPGA runs another bitstream than PC_FW_FPGA_VERSION (the notice screen)
void pc_glue_topbar_drawn(void);      //the top bar / RUN-STOP chip was (re)drawn: put the MCP chip back with it
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
int  pc_glue_autoset(void);           //fw 0.2.51: AUTO SET tapped: the spectrum view's Auto (1 = handled there)
int  pc_glue_autoset_running(void);   //fw 0.2.51: the spectrum view's Auto runs: AUTO SET reads "Auto..."
int  pc_glue_phosphor_on(void);       //fw 0.2.46: the phosphor view replaces the CH1 / CH2 polylines
void pc_glue_phosphor_draw(void);     //after the channel blocks: paint the phosphor maps
int  pc_glue_phosphor_xy(uint32 index); //XY branch: 1 = the phosphor view drew it
void pc_glue_scope_pass(void);        //fw 0.2.56: scope_display_trace_data starts: take the overrange flags
void pc_glue_scope_warning(void);     //fw 0.2.56: scope_display_trace_data ends: the warning line, top middle

//Decimation filter (bitstream 0x1534+, pc_decim.h). With it off or absent every call is a no-op and the 1.00o5 paths run.
extern int16 *pc_decim_src;           //fpga_read_adc_data: converted samples to take instead of bus reads (0 = bus)
void   pc_glue_ui_decim_arm(void);    //scope_acquire_trace_data: the next fpga_do_conversion uses the UI filter setting
void   pc_glue_decim_conversion(void);//fpga_do_conversion, before the sampling reset: write the mode, settle
void   pc_glue_decim_rearm(void);     //the full-rate check re-arms the last conversion: its filter setting again
uint32 pc_glue_decim_words(uint32 samples);       //ring words for a sample count (samples / 2, or samples in 16-bit)
uint32 pc_glue_decim_pre_words(uint32 samples);   //the same for the pretrigger, less the EXT latency shift in 16-bit
uint32 pc_glue_ui_read_start(uint32 addr);        //UI record start word: (0x14 - the 0x0B pretrigger words) mod the ring
void   pc_glue_decim_read(void *settings, uint32 start);   //fpga_read_sample_data: 16-bit read + convert, odd pass
void   pc_glue_decim_even(void);      //fpga_read_sample_data: second pass takes the even samples
void   pc_glue_peak_extremes(void *settings); //fpga_read_sample_data: a peak record's Vmax / Vmin / Vpp from its bytes
void   pc_glue_decim_tap(void);       //acquisition menu: Filter box touched
//ui-sample: the ACQ menu's Sample row and the peak-detect record on screen (docs/plans/ui-rows.md section 3)
void   pc_glue_acq_row_draw(void);    //the Average, Filter and Sample boxes and the Memory column's note (ui_acq_row_build)
int    pc_glue_acq_box_live(int box); //1 when that box of the row takes a tap (drawn and not grey; UI_ACQ_* in ui_ctrl.h)
void   pc_glue_sample_tap(void);      //the Sample box touched: Normal <-> Peak
int    pc_glue_sample_peak(void);     //1 while Sample Peak is applied (Scope view, not roll): Trace style greys
int    pc_glue_peak_record(int ch, const uint16 **pairs, uint32 *n, uint32 *gain, int32 *off); //its pairs, or 0
void   pc_glue_settings_defaults(void); //fw 0.2.44: scope_reset_config_data (DEFAULT button, sector load failure)
uint16 pc_glue_decim_setting_word(void);         //settings sector word 207, the Scope Filter (scope_save_config_data)
uint16 pc_glue_decim_spec_setting_word(void);    //fw 0.2.64: word 212, the Spectrum Filter
uint16 pc_glue_spectrum_setting_word(int i);     //settings sector words 208, 209 (scope_save_config_data)
uint16 pc_glue_phosphor_setting_word(void);      //settings sector word 210 (scope_save_config_data)
uint16 pc_glue_ui_setting_word(void);            //settings sector word 211 (scope_save_config_data)
//ui-type: the trigger menu's Type row (trig_ui.h; docs/plans/ui-rows.md section 2)
uint32 pc_glue_trig_tw5(void);                   //the trigger stream's word period for the UI acquisition now, 5 ns units
void   pc_glue_trig_ui_apply(void);              //the menu changed the row: the registers through fpga_trig_write (SET_TRIGGER2's path)
void   pc_glue_trig_rate_check(void);            //the menu opens: the Width / Idle snapped into the rate's range, with the notice
int    pc_glue_trig_params(void);                //1 when the menu shows the type's parameter rows (Pulse / Nth, not in Frequency response)

//fw 0.2.66: the Bode plot (bode_view.c), a mode opened from the generator menu
int    pc_glue_bode_pass(void);                  //scope_acquire_trace_data: 1 = the Bode plot shows (no UI acquisition)
int    pc_glue_bode_available(void);             //fw 0.2.68: the main menu's Bode plot row can open the view
uint16 pc_glue_bode_setting_word(int i);         //settings sector words 219, 220 (i 0, 1), 213..215 (i 2..4), 216 (i 5)
void   pc_glue_view_opening(void);               //the picture / waveform viewer is about to open: the Bode plot ends

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
#define PC_TOPBAR_DRAWN()       pc_glue_topbar_drawn()
#define PC_EXT_SELECTED()       pc_glue_ext_selected()
#define PC_ROLL_LABEL()         pc_glue_roll_label()
#define PC_SPECTRUM_DISPLAY()   do { if(pc_glue_spectrum_display()) return; } while(0)
#define PC_SETTINGS_DEFAULTS()  pc_glue_settings_defaults()
#define PC_SPECTRUM_TOUCH()     do { if(pc_glue_spectrum_touch()) return; } while(0)
#define PC_MAIN_MENU_MODE(row) pc_glue_main_menu_mode(row)
#define PC_AUTOSET()            pc_glue_autoset()
#define PC_AUTOSET_RUNNING()    pc_glue_autoset_running()
#define PC_PHOSPHOR_SKIP()      if(!pc_glue_phosphor_on())
#define PC_PHOSPHOR_DRAW()      pc_glue_phosphor_draw()
#define PC_PHOSPHOR_XY(i, last) do { if(pc_glue_phosphor_xy(i)) (i) = (last); } while(0)
#define PC_SCOPE_PASS()         pc_glue_scope_pass()
#define PC_SCOPE_WARNING()      pc_glue_scope_warning()
#define PC_BODE_PASS()          do { if(pc_glue_bode_pass()) return; } while(0)
#define PC_VIEW_OPENING()       pc_glue_view_opening()

//fw 0.2.58 (0x1536 sequencer, pc_seq_hw.h): fpga_do_conversion writes the geometry and the sequencer registers before
//the sampling reset; the software average is skipped while the traces hold a hardware average
void   pc_seq_ui_pre_reset(void);
#define PC_SEQ_PRE_RESET()      pc_seq_ui_pre_reset()


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
