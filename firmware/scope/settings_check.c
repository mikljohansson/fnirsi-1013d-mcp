//----------------------------------------------------------------------------------------------------------------------------------
// settings_check.c - the stored settings record checked before it is applied (settings_check.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "settings_check.h"

//Word offsets (variables.h; scope_functions.c checks they agree at compile time)
#define W_CH1        8      //CHANNEL1_SETTING_OFFSET
#define W_CH2        32     //CHANNEL2_SETTING_OFFSET
#define W_TRIG       56     //TRIGGER_SETTING_OFFSET
#define W_OTHER      80     //OTHER_SETTING_OFFSET
#define W_CURSOR     120    //CURSOR_SETTING_OFFSET
#define W_MEAS       136    //MEASUREMENT_SETTING_OFFSET
#define W_CAL        168    //CALIBRATION_SETTING_OFFSET: PWM centres, 6 per channel
#define W_CAL_C      184    //CALIBRATION_SETTING_OFFSET_C: 1.00o5 ADC compensations, 2 per channel
#define W_FPGA       192    //FPGA_SETTING_OFFSET

//The UI's limits (the SET_* commands enforce the same ones: pc_cmds.c)
#define TB_LAST      34     //time_div_texts: 0..10 roll, 11..34 short (acq_view.h ACQ_TB_FIRST..ACQ_TB_LAST)
#define TB_FIRST     11
#define RATE_LAST    28     //sample_rate_settings[29]
#define RATES_SHORT  18     //acq_view.h ACQ_RATES
#define TOTAL_MIN    80     //SET_TRIGGER: total 80..max_total, even; pretrigger + 2 <= total

typedef struct
{
  uint16_t word;
  uint16_t max;             //0..max (all of these are unsigned in scopesettings / fpgasettings)
} srec_limit;

//Fields applied as indices, enums or switches. Not here: positions and the trigger level (clamped on load or by the
//drawing), Trace style (style 3 migrates to Linear), the 1.00o5 max-light byte (ui_bright_load), the math mode and
//sources, the measurement ranks, the retired generator / hold-off slots and the marked words 202..223 (each restore
//takes its defaults without the marker or out of range).
static const srec_limit srec_limits[] =
{
  //Channel 1, channel 2: enable, coupling, probe (magnification index), V/A, display V/div, sample V/div, the retired
  //FFT switch, invert (SET_CHANNEL: enable / coupling / invert 0..1, range and probe 0..6)
  { W_CH1 + 0, 1 }, { W_CH1 + 1, 1 }, { W_CH1 + 2, 6 }, { W_CH1 + 3, 1 }, { W_CH1 + 4, 6 }, { W_CH1 + 5, 6 },
  { W_CH1 + 6, 1 }, { W_CH1 + 10, 1 },
  { W_CH2 + 0, 1 }, { W_CH2 + 1, 1 }, { W_CH2 + 2, 6 }, { W_CH2 + 3, 1 }, { W_CH2 + 4, 6 }, { W_CH2 + 5, 6 },
  { W_CH2 + 6, 1 }, { W_CH2 + 10, 1 },

  //Time/div index, sample rate index, trigger mode (auto, single, normal), edge, source (CH1, CH2, EXT)
  { W_TRIG + 0, TB_LAST }, { W_TRIG + 1, RATE_LAST }, { W_TRIG + 2, 2 }, { W_TRIG + 3, 1 }, { W_TRIG + 4, 2 },

  //Move speed, right menu state (0..4), confirmation mode, screen and grid brightness (0..100 %), always 50 %, XY,
  //roll (long) mode, average, new auto setup
  { W_OTHER + 0, 1 }, { W_OTHER + 1, 4 }, { W_OTHER + 2, 1 }, { W_OTHER + 3, 100 }, { W_OTHER + 4, 100 },
  { W_OTHER + 5, 1 }, { W_OTHER + 6, 1 }, { W_OTHER + 7, 1 }, { W_OTHER + 8, 1 }, { W_OTHER + 10, 1 },
  //REF1..REF8, references on start-up, RTC, USB mode, developer mode, tag in BMP
  { W_OTHER + 12, 1 }, { W_OTHER + 13, 1 }, { W_OTHER + 14, 1 }, { W_OTHER + 15, 1 }, { W_OTHER + 16, 1 },
  { W_OTHER + 17, 1 }, { W_OTHER + 18, 1 }, { W_OTHER + 19, 1 }, { W_OTHER + 20, 1 }, { W_OTHER + 21, 1 },
  { W_OTHER + 22, 1 }, { W_OTHER + 23, 1 }, { W_OTHER + 24, 1 },

  //Time cursors on, volt cursors on, cursors locked
  { W_CURSOR + 0, 1 }, { W_CURSOR + 3, 1 }, { W_CURSOR + 6, 1 },

  //The measurement cards' hide switches per channel
  { W_MEAS + 2, 1 }, { W_MEAS + 3, 1 },

  //Generator on, duty 0..100 % (the generator menu's limits)
  { W_FPGA + 0, 1 }, { W_FPGA + 4, 100 },
};

int srec_check_header(const uint16_t *w, uint16_t ver_hi, uint16_t ver_lo)
{
  uint32_t sum = 0, i;

  if((w[2] != ver_hi) || (w[3] != ver_lo))
    return SREC_BAD_VERSION;

  for(i=2;i<SREC_WORDS;i++)
    sum += w[i];

  if((w[0] != (uint16_t)(sum >> 16)) || (w[1] != (uint16_t)sum))
    return SREC_BAD_SUM;

  return SREC_OK;
}

static int srec_bad(uint16_t *bad_word, uint32_t word)
{
  if(bad_word)
    *bad_word = (uint16_t)word;

  return SREC_BAD_FIELD;
}

int srec_check_fields(const uint16_t *w, uint16_t *bad_word)
{
  uint32_t i, freq, total, pre;
  int32_t  comp;

  if(bad_word)
    *bad_word = 0;

  //The limits table, then the rules that need more than one word; the word of the first failing check is reported
  for(i=0;i<sizeof(srec_limits)/sizeof(srec_limits[0]);i++)
    if(w[srec_limits[i].word] > srec_limits[i].max)
      return srec_bad(bad_word, srec_limits[i].word);

  //Short time base (not roll): time/div 11..34 and one of the 18 short-mode rates (scope_set_timebase keeps roll mode
  //for the indices below 11)
  if(!w[W_OTHER + 7] && ((w[W_TRIG + 0] < TB_FIRST) || (w[W_TRIG + 1] >= RATES_SHORT)))
    return srec_bad(bad_word, (w[W_TRIG + 0] < TB_FIRST) ? W_TRIG + 0 : W_TRIG + 1);

  //The PWM centres the calibration found per range: inside the range a calibration can store (a centre of 0 parks
  //the trace outside the ADC range and unbinds the calibration block in sector 708)
  for(i=0;i<12;i++)
    if((w[W_CAL + i] < SREC_PWM_MIN) || (w[W_CAL + i] > SREC_PWM_MAX))
      return srec_bad(bad_word, W_CAL + i);

  //1.00o5 ADC compensations: int16, whole codes (the calibration rounds into -127..127)
  for(i=0;i<4;i++)
  {
    comp = (int16_t)w[W_CAL_C + i];

    if((comp < -127) || (comp > 127))
      return srec_bad(bad_word, W_CAL_C + i);
  }

  //Generator frequency (int32, high word first): 0..25 MHz, what SET_GEN and the generator menu can set
  freq = ((uint32_t)w[W_FPGA + 2] << 16) | w[W_FPGA + 3];
  if(freq > SREC_GEN_MAX_HZ)
    return srec_bad(bad_word, W_FPGA + 2);

  //Memory depth and trigger point: SET_TRIGGER's geometry (a depth over the bitstream's is clamped on load)
  total = w[W_FPGA + 8];
  pre   = w[W_FPGA + 9];

  if((total < TOTAL_MIN) || (total & 1))
    return srec_bad(bad_word, W_FPGA + 8);

  if(pre + 2 > total)
    return srec_bad(bad_word, W_FPGA + 9);

  return SREC_OK;
}

int srec_check(const uint16_t *w, uint16_t ver_hi, uint16_t ver_lo, uint16_t *bad_word)
{
  int r = srec_check_header(w, ver_hi, ver_lo);

  if(bad_word)
    *bad_word = 0;

  return (r == SREC_OK) ? srec_check_fields(w, bad_word) : r;
}

const char *srec_text(int result)
{
  switch(result)
  {
    case SREC_OK:          return "valid";
    case SREC_NO_READ:     return "read failed";
    case SREC_BAD_VERSION: return "other version";
    case SREC_BAD_SUM:     return "checksum";
    case SREC_BAD_FIELD:   return "value out of range";
  }

  return "unknown";
}
