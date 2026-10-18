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

#define GEN_f_MAX 10000000   // max frequency 5Mhz

#define NUMBUF_MAX 12   // + / - + max 10 číslic

void buffer_clear(void);
void buffer_set_sign(char sign);
void buffer_add_digit(char digit);
void buffer_backspace(void);
int32 buffer_to_int32(void);
void buffer_set_number(int32 value);



//==============================================================================
#endif /* GENERATOR_H */
