//******************************************************************************
/* 
 * File:   generator.c
 * Author: dk
 * 
 * Created on November 26, 2025, 9:52 AM
 */
//******************************************************************************


#include "types.h"
#include "menu.h"
#include "test.h"
#include "scope_functions.h"
#include "statemachine.h"
#include "touchpanel.h"
#include "timer.h"
#include "fpga_control.h"
#include "spi_control.h"
#include "sd_card_interface.h"
#include "display_lib.h"
#include "ff.h"
#include "DS3231.h"

#include "usb_interface.h"
#include "cdc_class.h"
#include "variables.h"

#include "PC_interface.h"

#include "generator.h"

//#include <stdint.h>
//#include <stdbool.h>


char numbuffer[NUMBUF_MAX];
uint8 numbuffer_len = 0;
uint8 numchange = 0;


//------------------------------------------------------------
// Vymaže buffer
//------------------------------------------------------------
void buffer_clear(void)
{
    numbuffer_len = 11;
    //numbuf[0] = '\0';
  while (numbuffer_len) {numbuffer[numbuffer_len] = ' ';numbuffer_len--;}
  numbuffer[0] = '_';
  //numbuf[0] = '\0';
}


//------------------------------------------------------------
// Sets the sign.
// '+' or '-'
// If the buffer starts with a number, the sign is inserted at the beginning.
//------------------------------------------------------------
void buffer_set_sign(char sign)
{
  if (sign != '+' && sign != '-') return;

  if (numbuffer_len == 0) 
  {
    // empty buffer -> insert a sign
    numbuffer[0] = sign;
    numbuffer[1] = '\0';
    numbuffer_len = 1;
    return;
  }

  if (numbuffer[0] == '+' || numbuffer[0] == '-') 
  {
    // the sign already exists → we will overwrite it
    numbuffer[0] = sign;
  } 
  else
  {
    // buffer starts with a number → we put a sign in front of it
    if (numbuffer_len < NUMBUF_MAX - 1) 
    {
      // shift right
      for (int i = numbuffer_len; i > 0; i--) { numbuffer[i] = numbuffer[i - 1];}
      numbuffer[0] = sign;
      numbuffer_len++;
      numbuffer[numbuffer_len] = '\0';
    }
  }
}

//------------------------------------------------------------
// Add a digit to the end
//------------------------------------------------------------
void buffer_add_digit(char digit)
{
  if (digit < '0' || digit > '9') return;

  if (numchange == 1){buffer_clear();  numchange = 0;}
    
  if (numbuffer_len < NUMBUF_MAX - 1) 
  {
    numbuffer[numbuffer_len++] = digit;
    numbuffer[numbuffer_len] = '\0';
  }
}

//------------------------------------------------------------
// Backspace – deletes the last character.
// If it deletes the sign and the numbers remain → buffer without a sign is OK.
//------------------------------------------------------------
void buffer_backspace(void)
{
  if (numbuffer_len == 0) return;

  numbuffer_len--;

  if (numbuffer_len == 0) {
      numbuffer[0] = '\0';
      numbuffer[0] = '_';
    return;
    }

    numbuffer[numbuffer_len] = '\0';
}

//------------------------------------------------------------
// Konverzia buffra na int32
//------------------------------------------------------------
int32 buffer_to_int32(void)
{
    int64 value = 0;
    int sign = 1;  // +1 alebo -1
    uint8 start = 0;

    if (numbuffer_len == 0)
        return 0;

    if (numbuffer[0] == '-') {
        sign = -1;
        start = 1;
    } else if (numbuffer[0] == '+') {
        start = 1;
    }

    for (uint8 i = start; i < numbuffer_len; i++) {
        value = value * 10 + (numbuffer[i] - '0');

        // kontrola pretečenia int32
        if (value > 2147483647LL) {
            if (sign == -1 && value == 2147483648LL)
                return -2147483648;
            return (sign == -1 ? -2147483648 : 2147483647);
        }
    }

    return (int32)(value * sign);
}

//==============================================================================

void buffer_set_number(int32 value)
{
 // clear buffer
  buffer_clear();

  // load the sign
  if (value < 0) { buffer_set_sign('-'); value = -value; } 
    //else { buffer_set_sign('+'); }

  // temporary field for rewriting the number
  char tmp[12];
  int len = 0;

  // special case 0
  if (value == 0) { buffer_add_digit('0'); return; }

  // layout the number from behind
  while (value > 0 && (len < (int)sizeof(tmp))) 
  {
    tmp[len++] = '0' + (value % 10);
    value /= 10;
  }

  // now we rotate the number and put it into the buffer
    for (int i = len - 1; i >= 0; i--) { buffer_add_digit(tmp[i]); }
}


/*
 
 buffer_clear();
if (value < 0)
    value = -value;

 */