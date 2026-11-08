//******************************************************************************
/* 
 * File:   generator.h
 * Author: dk
 *
 * Created on November 26, 2025, 9:52 AM
 */
//******************************************************************************
#ifndef GENERATOR_H
#define GENERATOR_H
//==============================================================================

#include "gen_pat.h"

//The generator menu's frequency limit is SET_GEN's (gen_pat.h GP_FREQ_MAX_MHZ, 25 MHz = 8 clocks), and the settings check
//takes the same one (settings_check.h SREC_GEN_MAX_HZ): a frequency set either way survives the power-off save and the
//next boot (fw-genlimit; up to 0.3.1 the menu and the check had 10 MHz and refused a host's 12.5 MHz record at boot)
#define GEN_f_MAX ((int)(GP_FREQ_MAX_MHZ / 1000u))

#define NUMBUF_MAX 12   // + / - + max 10 číslic

void buffer_clear(void);
void buffer_set_sign(char sign);
void buffer_add_digit(char digit);
void buffer_backspace(void);
int32 buffer_to_int32(void);
void buffer_set_number(int32 value);



//==============================================================================
#endif /* GENERATOR_H */
