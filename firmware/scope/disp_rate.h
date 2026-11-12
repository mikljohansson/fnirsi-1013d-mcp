//----------------------------------------------------------------------------------------------------------------------------------
// disp_rate.h - the time scale of the record on screen (fw-hostdt, firmware/AGENTS.md section 103). Pure C, integer only,
// host-tested (host_tests/test_ui.c; host_tests/test_session.c through mock_hal.c's trace build).
//
// The Scope view draws the record in the trace buffers with the UI's pair, scopesettings.timeperdiv and samplerate
// (scope_functions.c disp_xpos_per_sample: 50 x fpd / fs pixels per sample). A host's raw divider (SET_TIMEBASE mode 1)
// writes only the FPGA's 0x0D: the records taken while it runs, the host's and the UI's own in RUN (a UI arm writes no
// 0x0D), have another sample period than the UI's pair says. Every readout of CH1 / CH2 into the trace buffers notes the
// period the record ran at (disp_rate_note from pc_glue_decim_read: the divider the FPGA holds and the decimation N the
// record was taken with). The drawing keeps the UI's pixels per sample, so one division of the drawn trace lasts the
// UI's time/div x (record period / UI period), and every time or frequency the screen shows for that record comes from
// the record's period (the ACQ chip's rate and time/div, the time cursors, the Freq / Period / Width cards, the
// spectrum's fs). A record taken at the UI's period ("foreign" 0) keeps the 1.00o5 table paths unchanged.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef DISP_RATE_H
#define DISP_RATE_H

#include "types.h"

//One sample period unit: the FPGA's 200 MHz write clock, 5 ns
#define DISP_RATE_PS_UNIT  5000u

//A record of CH1 / CH2 was read into the trace buffers: `divider` = the 0x0D value it ran at, `n` = the decimation N it
//was filtered with (pc_decim.h; 0 for the legacy 8-bit layout). Its sample period is n x 5 ns, or (divider + 1) x 5 ns.
void   disp_rate_note(uint32 divider, uint32 n);

//The record on screen is the one the UI's pair describes (a waveform file from before fw-hostdt, boot)
void   disp_rate_forget(void);

//What was noted: 1 with the divider and N (the waveform file stores them), 0 when nothing is
int    disp_rate_noted(uint32 *divider, uint32 *n);

//The record's sample period in 5 ns units (0: none noted, the UI's pair describes the record)
uint32 disp_rate_p5(void);

//The 0x0D divider of the record on screen; ui_divider (the UI table's) when none was noted
uint32 disp_rate_divider(uint32 ui_divider);

//1 when the record on screen ran at another sample period than the UI's pair (ui_divider = sample_rate_settings[rate])
int    disp_rate_foreign(uint32 ui_divider);

//A time measured on the drawn trace with the UI's scale (ps) as the record's time: ps x record period / UI period
uint64 disp_rate_scale_ps(uint64 ps, uint32 ui_divider);

//The record's time of `samples_q16` samples (Q16, the measurements' period / high / low times), in ps; 0 with none noted
uint64 disp_rate_samples_ps(uint32 samples_q16);

//1 / the record's time of `period_q16` samples, in mHz (0 for a period 0 or none noted)
uint64 disp_rate_freq_mhz(uint32 period_q16);

//A time per division in ps as the ACQ chip writes it: "5us/div", "2.5us/div", "200ms/div" (the table's texts for its
//values; three significant digits, the chip's text column holds no more, trailing zeros dropped). Returns the end.
char  *disp_rate_tdiv_text(char *p, uint64 tdiv_ps);

//The record's sample rate as the ACQ chip writes it: "20MSa/s", "500KSa/s", "66.7MSa/s" (acquisition_speed_texts'
//spelling for its values; three significant digits); "--" with none noted
char  *disp_rate_rate_text(char *p);

#endif
