//----------------------------------------------------------------------------------------------------------------------------------

#include "fpga_control.h"
#include "touchpanel.h"
#include "timer.h"
#include "variables.h"
#include "pc_hooks.h"
#include "acal_live.h"
#include "pc_transport.h"
#include "enc_heal.h"
#include "fpga_bus.h"

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_init(void)
{
  //First set pin high in data register to avoid spikes when changing from input to output
  FPGA_CLK_INIT();
  
  //Initialize the three control lines for output
  FPGA_CTRL_INIT();
}

//----------------------------------------------------------------------------------------------------------------------------------

//fw 0.2.61: trace of the writes that set the ADC capture state (0x0D rate, 0x53 decimation mode, 0x56 FIR, 0x01 = 1
//sampling reset; fw 0.2.87: 0x54 shift, 0x55 16-bit level) into the event log, type 22, with the caller (fpga_trace_who): the bench correlates a garbled full-rate
//capture with the writes before it (firmware/AGENTS.md section 37). fw 0.2.72: counted always (fpga_bus_note_write,
//GET_LINK_STATS 64-66), logged as events only at trace level 1 (GET_EVENT_LOG flags bits 2:1): with the UI running every
//conversion's 0x01 entry flooded the 128-entry ring within seconds
uint8        fpga_trace_who = FPGA_WHO_UI;
uint8        fpga_trace_level;
static uint8 fpga_trace_cmd;

static void fpga_trace(uint32 value)
{
  uint8 c = fpga_trace_cmd;

  fpga_trace_cmd = 0;

  if(fpga_bus_note_write(c, value) && fpga_trace_level)
    pc_usb_log_fpga_write(c, (uint16)(((uint32)fpga_trace_who << 12) | ((value > 0xFFF) ? 0xFFF : value)));
}

void fpga_write_cmd(uint8 command)
{
  fpga_trace_cmd = command;

  //Set the control lines for writing a command
  FPGA_CMD_WRITE();

  //Set the bus for writing
  FPGA_BUS_DIR_OUT();

  //Write the data to the bus  
  FPGA_SET_DATA(command);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
}

//----------------------------------------------------------------------------------------------------------------------------------

uint8 fpga_read_cmd(void)
{
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_CMD_READ();

  //Clock the data to the output of the FPGA
  FPGA_PULSE_CLK();
 
  //Read the data
  return(FPGA_GET_DATA());
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_write_byte(uint8 data)
{
  if(fpga_trace_cmd)
    fpga_trace(data);

  //Set the control lines for writing a command
  FPGA_DATA_WRITE();

  //Set the bus for writing
  FPGA_BUS_DIR_OUT();
  
  //Write the data to the bus  
  FPGA_SET_DATA(data);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
}

//----------------------------------------------------------------------------------------------------------------------------------

uint8 fpga_read_byte(void)
{
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_DATA_READ();

  //Clock the data to the output of the FPGA
  FPGA_PULSE_CLK();
 
  //Read the data
  return(FPGA_GET_DATA());
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_write_short(uint16 data)
{
  if(fpga_trace_cmd)
    fpga_trace(data);

  //Set the control lines for writing a command
  FPGA_DATA_WRITE();

  //Set the bus for writing
  FPGA_BUS_DIR_OUT();
  
  //Write the msb to the bus  
  FPGA_SET_DATA(data >> 8);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
  
  //Write the lsb to the bus  
  FPGA_SET_DATA(data);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
}

//----------------------------------------------------------------------------------------------------------------------------------

uint16 fpga_read_short(void)
{
  uint16 data;
  
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_DATA_READ();

  //Clock the data to the output of the FPGA
  FPGA_PULSE_CLK();

  //Get the msb
  data = FPGA_GET_DATA() << 8;

  //Clock the data to the output of the FPGA
  FPGA_PULSE_CLK();

  //Get the lsb
  data |= FPGA_GET_DATA();
  
  //Read the data
  return(data);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_write_int(uint32 data)
{
  if(fpga_trace_cmd)
    fpga_trace(data);

  //Set the control lines for writing a command
  FPGA_DATA_WRITE();

  //Set the bus for writing
  FPGA_BUS_DIR_OUT();
  
  //Write the msb to the bus  
  FPGA_SET_DATA(data >> 24);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();

  //Write the mlsb to the bus  
  FPGA_SET_DATA(data >> 16);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
  
  //Write the lmsb to the bus  
  FPGA_SET_DATA(data >> 8);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
  
  //Write the lsb to the bus  
  FPGA_SET_DATA(data);
  
  //Clock the data into the FPGA
  FPGA_PULSE_CLK();
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_backlight_brightness(uint16 brightness)
{
  fpga_write_cmd(0x38);
  fpga_write_short(brightness);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_translated_brightness(void)
{
  uint16 data;
  
  //Translate the 0 - 100 brightness value into data for the FPGA
  data = (scopesettings.screenbrightness * 560) + 4000;
  
  //Write it to the FPGA
  fpga_set_backlight_brightness(data);
}

//----------------------------------------------------------------------------------------------------------------------------------

uint16 fpga_get_version(void)
{
  uint16  fw_version;

  fpga_write_cmd(0x06);
  fw_version = fpga_read_short();

  return(fw_version);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_enable_system(void)
{
  //This command is only ever set with 1 as data and this function is only called at setup and check hardware
  fpga_write_cmd(0x04);
  fpga_write_byte(0x01);
}

//----------------------------------------------------------------------------------------------------------------------------------

//FPGA 0x153D (fw-d5): the generator is the pattern generator's two-step continuous pattern (gen_pat.h, fpga_bus.h "D5
//pattern"); the DDS (0x51 / 0x52) is gone. The UI's whole Hz and whole percent go through the contract's P16 / H16
//formulas; a running square changes at the next step boundary (glitch-free), so the order of the frequency and the duty
//writes no longer matters. Frequency 0 or duty 0 % holds the pin low, 100 % high (the DDS's behaviour at the extremes).
static void fpga_gen_program(uint32 freq_hz, uint8 duty_pct)
{
  uint64    p16, h16;
  uint8_t   level;
  gp_square sq;

  if(gp_ui_square(freq_hz, duty_pct, &p16, &h16, &level))
    fpga_pat_level(level);
  else
  {
    (void)gp_split(p16, h16, &sq);
    fpga_pat_square(&sq);
  }
}

void fpga_on_off_generator(void)
{
  if (fpgasettings.gen_enable == 1)
  {
    fpga_gen_program(fpgasettings.gen_freq, fpgasettings.gen_duty);
    fpga_pat_drive(1);
  }
  else
  {
    //Off: the pattern stopped at the low idle level and the pin hi-Z (the EXT trigger input)
    fpga_pat_drive(0);
    fpga_pat_level(0);
  }
}

//------------------------------------------------------------------------------

//While the generator is off nothing is written: fpga_on_off_generator programs the values when it is switched on
void fpga_set_generator_freq(uint32 freq_hz)
{
  if(fpgasettings.gen_enable == 1)
    fpga_gen_program(freq_hz, fpgasettings.gen_duty);
}

//------------------------------------------------------------------------------

void fpga_set_generator_duty(uint8 duty_pct) //50 is 50%
{
  if(fpgasettings.gen_enable == 1)
    fpga_gen_program(fpgasettings.gen_freq, duty_pct);
}
  
//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_channel_enable(PCHANNELSETTINGS settings)
{
  //Send the command for channel enable to the FPGA
  fpga_write_cmd(settings->enablecommand);
  
  //Write the needed data based on the setting
  if(settings->enable == 0)
  {
    //Disable the channel
    fpga_write_byte(0x00);
  }
  else
  {
    //Enable the channel
    fpga_write_byte(0x01);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_channel_coupling(PCHANNELSETTINGS settings)
{
  //Send the command for channel 1 coupling to the FPGA
  fpga_write_cmd(settings->couplingcommand);
  
  //Write the needed data based on the setting
  if(settings->coupling == 0)
  {
    //Set the DC coupling for the channel
    fpga_write_byte(0x01);
  }
  else
  {
    //Set the AC coupling for the channel
    fpga_write_byte(0x00);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_channel_voltperdiv(PCHANNELSETTINGS settings)
{
  register uint32 setting = settings->samplevoltperdiv;
  
  //Send the command for channel 1 volts per div to the FPGA
  fpga_write_cmd(settings->voltperdivcommand);
  
  //Check if setting in range of what is allowed
  if(setting > 5)
    setting = 5;
  
  //Write it to the FPGA
  fpga_write_byte(setting);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_channel_offset(PCHANNELSETTINGS settings)
{
  //uint32 offset;
    
  //Send the command for channel DC offset to the FPGA scopesettings.channel1.traceposition
  fpga_write_cmd(settings->offsetcommand);

  //Write the center offset data for this channel and volt per div setting
  //fpga_write_short(settings->dc_calibration_offset[settings->samplevoltperdiv];
  fpga_write_short(settings->dc_calibration_offset[settings->samplevoltperdiv]+(settings->dcoffset));
  //fpga_write_short(settings->dc_calibration_offset[settings->samplevoltperdiv]-(settings->dcoffset));
  //offset = (signal_adjusters[0] * (settings->dcoffset)/2) / signal_adjusters[settings->samplevoltperdiv];
  
  //offset = (multiply * (settings->traceposition)) / signal_adjusters[settings->displayvoltperdiv];
  
  //fpga_write_short(settings->dc_calibration_offset[settings->samplevoltperdiv]-(offset));
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_sample_rate(uint32 samplerate)
{
  //Translate the time per div setting to what the FPGA needs
  //22-11-2021 Since the long time base settings have been removed and the system does things differently a translation is needed here
  //Old time base was 0 for 50S/div to 29 for 10nS/div
  //This is no longer the case. 0 means 200mS/div but needs the setting for 50mS/div. Same for 100mS/div. Simplest solution is a translation table
  
  //This most likely needs to be based on the acquisition speed and no longer the time per div setting, but that needs further investigation
  
  //Make sure the setting is in range of the table
  if(samplerate < (sizeof(sample_rate_settings) / sizeof(uint32)))
  {
    //Write the command to the FPGA
    fpga_write_cmd(0x0D);

    //Write the time base clock setting to the FPGA
    fpga_write_int(sample_rate_settings[samplerate]);
    fpga_divider = sample_rate_settings[samplerate];
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_trigger_channel(void)
{
  //Send the command for selecting the trigger channel to the FPGA
  fpga_write_cmd(0x15);

  //EXT trigger (bitstream 0x1533+): source 2. Only selectable when the bitstream has it (pc_glue_ui_has). D5 pattern
  //(0x153D): source 3, the pattern's marker, set by the host on top of the EXT channel (fpga_trig_marker)
  if(scopesettings.triggerchannel == 2)
  {
    fpga_write_byte(fpga_trig_marker ? 0x03 : 0x02);
    return;
  }

  fpga_trig_marker = 0;
  
  //Write the needed data based on the setting
  if(scopesettings.triggerchannel == 0)
  {
    //Set channel 1 as trigger input
    fpga_write_byte(0x00);
  }
  else
  {
    //Set channel 2 as trigger input
    fpga_write_byte(0x01);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_trigger_edge(void)
{
  //Send the command for selecting the trigger channel to the FPGA
  fpga_write_cmd(0x16);
  
  //Write the needed data based on the setting
  if(scopesettings.triggeredge == 0)
  {
    //Set trigger edge to rising
    fpga_write_byte(0x00);
  }
  else
  {
    //Set trigger edge to falling
    fpga_write_byte(0x01);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_swap_trigger_channel(void)
{
  //EXT (source 2, 0x1533+) does not depend on which channels are on: keep it (fw 0.2.72; 1.00o5 overwrote it with CH1 /
  //CH2 whenever one channel was toggled with the other off, fpga/al3/README.md section 7)
  if(scopesettings.triggerchannel == 2)
    return;

  //Check if channel 1 is disabled or channel 2 is enabled
  if((scopesettings.channel1.enable == 0) || (scopesettings.channel2.enable))
  {
    //Check if both channels are disabled
    if(scopesettings.channel2.enable == 0)
    {
      //If so do nothing
      return;
    }
    
    //Else check if channel 1 is enabled
    if(scopesettings.channel1.enable)
    {
      //Assume the correct setting is already in the FPGA and do nothing
      return;
    }
    
    //Only channel 2 enabled so set it as source
    scopesettings.triggerchannel = 1;
  }
  else
  {
    //Only channel 1 enabled so set that as source
    scopesettings.triggerchannel = 0;
  }
  
  //Write it the new setting to the FPGA
  fpga_set_trigger_channel();
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_trigger_level(void)
{
  uint32 voltperdiv;
  uint32 traceposition;
  int32 dcoffset;
  int32  level;
  
  //The trace position on screen equals to ADC reading 128
  //The trigger vertical position is relative to the trace position.
  //The screen position values are volt/div related, so conversion to ADC level is needed
  
  //Check which channel is used for triggering
  if(scopesettings.triggerchannel == 0)
  {
    //Channel 1, so use its data
    voltperdiv    = scopesettings.channel1.samplevoltperdiv;
    traceposition = scopesettings.channel1.traceposition;
    //dcoffset = scopesettings.channel1.dcoffset/2;
    //dcoffset = (scopesettings.channel1.dcoffset*100)/220;//195
    dcoffset = (scopesettings.channel1.dcoffset*100)/scopesettings.channel1.dc_shift_size;
    scopesettings.channel1.triggerverticalposition=scopesettings.triggerverticalposition;
  }
  else
  {
    //Channel 2, so use its data
    voltperdiv    = scopesettings.channel2.samplevoltperdiv;
    traceposition = scopesettings.channel2.traceposition;
    //dcoffset = scopesettings.channel2.dcoffset/2;
    //dcoffset = (scopesettings.channel2.dcoffset*100)/220;//230
    dcoffset = (scopesettings.channel2.dcoffset*100)/scopesettings.channel2.dc_shift_size;
    scopesettings.channel2.triggerverticalposition=scopesettings.triggerverticalposition;
  }
  
  //The difference between the two positions determines the level offset on 128, but it needs to be scaled back first
  //level = ((((int32)scopesettings.triggerverticalposition - (int32)traceposition) * 4194304) / signal_adjusters[voltperdiv]) + 128;
  //level = ((((int32)scopesettings.triggerverticalposition - (int32)traceposition) * 2100000) / signal_adjusters[voltperdiv]) + 130;
  //level = ((((int32)scopesettings.triggerverticalposition - (int32)traceposition) * 2097152) / signal_adjusters[voltperdiv]) + 128;
  
    level = ((((int32)(scopesettings.triggerverticalposition - dcoffset) - (int32)traceposition) * 2097152) / signal_adjusters[voltperdiv]) + 128;
  //level = scopesettings.channel1.center-255+traceposition+scopesettings.triggerverticalposition;

  //Set the new level in the settings
  scopesettings.triggerlevel = level;

  //Limit on extremes
  if(level < 0)
  {
    level = 0;
  }
  else if(level > 255)
  {
    level = 255;
  }
  
  //Send the command for setting the trigger level to the FPGA
  fpga_write_cmd(0x17);
  
  //Write the actual level to the FPGA
  fpga_write_byte(level);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_trigger_mode(void)
{
  //Send the command for selecting the trigger mode to the FPGA
  fpga_write_cmd(0x1A);
  
  //Write the needed data based on the setting
  if(scopesettings.triggermode == 0)
  {
    //Set trigger mode to auto
    fpga_write_byte(0x00);
  }
  else
  {
    //Set trigger mode to single or normal
    fpga_write_byte(0x01);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//short time base
void fpga_set_time_base(uint32 timebase) //len pre rezim auto, urcuje max cas po ktory sa odoberaju vzorky
{
  //Make sure setting is in range
  if(timebase < (sizeof(timebase_settings) / sizeof(uint32)))
  {
    //Write the command to set the short time base data to the FPGA: the sample count (Atlan4 bitstreams count samples)
    fpga_write_cmd(0x0E);
    fpga_write_int(scopesettings.samplecount);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_set_long_timebase(uint32 timebase)
{
  //Send the data, which is always 2000
  //fpga_write_int(2000);//0x07d0 is 2000
  
  //Make sure setting is in range
  if(timebase < (sizeof(timebase_settings) / sizeof(uint32)))
  {
    //Send the command for long time base to the FPGA
    //fpga_write_cmd(0x0D);
  
    //Table settings ranges from setting 0 (200mS/div) to 23 (5nS/div)
    //fpga_write_int(timebase_settings[timebase]);
  }
 
  //The command 0x0D sets the rate with which the ADC is clocked. The 0x07D0 means 100KSa/s.
  //With the command 0x28 the mode is selected. Needs to be set to 0x01 for the roll mode sampling. Not sure if it is needed for every read of the sampl
  
  //Disable trigger system???
  fpga_write_cmd(0x0F);
  fpga_write_byte(0x01);
  
  //Some mode select command for the FPGA (0x01 for long time base)
  //fpga_write_cmd(0x28); //0X28 not support in FPGA???
  //fpga_write_byte(0x01);
}

//----------------------------------------------------------------------------------------------------------------------------------

static void fpga_arm_idle(void)
{
  //Same unbounded ready wait as 1.00o5 (UI behaviour unchanged), but keep the USB link answering if the FPGA hangs.
  //The BUSY poll never touches the FPGA bus, so it is safe between these reads.
  PC_BUSY_POLL();
}

void fpga_do_conversion(void)
{
  fpga_arm_cfg arm;

  scopesettings.conversion_done = 0;    //flag for conversion, 0-start

  //fw 0.2.62: a new capture for the full-rate check (unless this is its re-arm)
  fpga_encode_new_conversion();

  //Decimation filter (0x1534+): the UI acquisition's setting, legacy (K = 0) for every other caller (calibration,
  //auto setup), written and settled before the sampling reset
  pc_glue_decim_conversion();
  
  //Check if sampling with trigger system enabled
  if(scopesettings.samplemode == 1)
  {
    //Enable trigger system???
    fpga_write_cmd(0x0F);
    fpga_write_byte(0x00);
  }
  else
  {
    //Disable the trigger circuit??
    fpga_write_cmd(0x0F);
    fpga_write_byte(0x01);
  }

  //Set the FPGA for short time base mode
  //fpga_write_cmd(0x28);
  //fpga_write_byte(0x00);
  

  
  //0x1536: sequencer registers before the reset (UI averaging run or 0x57 = 0)
  PC_SEQ_PRE_RESET();

  //Sampling reset (fpga_bus.h): pretrigger and total words (16-bit layout: one sample per word) before the reset and
  //again inside it, the time-out for auto mode (the sample count, fpga_set_time_base's value), unbounded ready wait as
  //1.00o5
  arm.pre_words    = (uint16_t)pc_glue_decim_pre_words(fpgasettings.settriggerpoint);
  arm.total_words  = (uint16_t)pc_glue_decim_words(fpgasettings.totalsamples);
  arm.write_0e     = (uint8_t)(scopesettings.timeperdiv < (sizeof(timebase_settings) / sizeof(uint32)));
  arm.auto_timeout = scopesettings.samplecount;
  arm.max_polls    = 0;
  arm.idle         = fpga_arm_idle;
  fpga_arm_reset(&arm);

  //PC protocol: a UI conversion started (the ring no longer holds the last capture)
  PC_UI_CAPTURE_START();
}
//----------------------------------------------------------------------------------------------------------------------------------

uint8 fpga_done_conversion(void)
{
  //Send check on triggered or buffer full command to the FPGA
  fpga_write_cmd(0x0A);
  
  //Check if sampling with trigger system enabled
  if(scopesettings.samplemode == 1)
  {
    if (((fpga_read_byte() & 1) == 1))
    {
        //Disable trigger system???
        fpga_write_cmd(0x0F);
        fpga_write_byte(0x01);
        scopesettings.conversion_done = 1;
    }
  }
  else
  {
    //Check for the FPGA to signal triggered or buffer full
    if (((fpga_read_byte() & 1) == 1)) scopesettings.conversion_done = 1;
  }
  
  //fw 0.2.62: a full-rate capture is checked before anyone reads it; a bad one is discarded and the conversion re-armed
  //(enc_heal.h, bounded), so every caller's wait loop simply keeps waiting
  if(scopesettings.conversion_done && fpga_encode_after_capture())
    scopesettings.conversion_done = 0;

  //Scan the touch panel
  tp_i2c_read_status();
  if (havetouch == 1) scopesettings.display_data_done = 1;
      
  return (scopesettings.conversion_done);
}

//----------------------------------------------------------------------------------------------------------------------------------

uint16 fpga_prepare_for_transfer(void)
{
  uint32 data;
  
  //Send the command for getting some data from the FPGA
  fpga_write_cmd(0x14);
  
  //Just read a short, and with 0x0FFF and add 2
  //Have to see if the add 2 is needed
  //data = (fpga_read_short() & 0x0FFF) + 2;    
  data = (fpga_read_short() & 0x1FFF);  //change 11.12.2025
 
  return(data & 0x1FFF);
}

//----------------------------------------------------------------------------------------------------------------------------------

//See https://en.wikipedia.org/wiki/Methods_of_computing_square_roots#Binary_numeral_system_.28base_2.29
uint32 isqrt(uint32 n) {
  uint32 x = n;
  uint32 c = 0;
  uint32 d = 1 << 30;

  while (d > n) d >>= 2;

  while (d != 0) {
    if (x >= c + d) {
      x -= c + d;
      c = (c >> 1) + d;
    } else {
      c >>= 1;
    }
    d >>= 2;
  }
  return c;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32 fpga_divider;
uint8  fpga_read_settle;
uint8  fpga_read_prestrobe;
static uint32 fpga_dither = 1;

void fpga_channel_correction(PCHANNELSETTINGS settings, acal_corr *c)
{
  const acal_channel *cal = 0;

  if(adc_cal_loaded)
  {
    if(settings == &scopesettings.channel1)
      cal = &adc_cal[0];
    else if(settings == &scopesettings.channel2)
      cal = &adc_cal[1];
  }

  acal_correction(cal, settings->dc_calibration_offset, settings->samplevoltperdiv, fpga_divider, settings->dcoffset,
                  settings->dc_shift_value, settings->adc1compensation, settings->adc2compensation, c);

  //Calibration v4 (CH1 / CH2, v3 bound): the full-class common mode and the tracked A - B split (acal_live.c)
  acal_live_apply(settings, c);
}

//C6 peak detect: the record's two raw bytes come from the full-rate ADC stream (dec_on runs the encodes at 200 MSa/s as
//the filter does), so the per-core correction of the full class applies, not the gated class the divider would select
void fpga_channel_correction_full(PCHANNELSETTINGS settings, acal_corr *c)
{
  const acal_channel *cal = 0;

  if(adc_cal_loaded)
  {
    if(settings == &scopesettings.channel1)
      cal = &adc_cal[0];
    else if(settings == &scopesettings.channel2)
      cal = &adc_cal[1];
  }

  acal_correction(cal, settings->dc_calibration_offset, settings->samplevoltperdiv, 0, settings->dcoffset,
                  settings->dc_shift_value, settings->adc1compensation, settings->adc2compensation, c);
  acal_live_apply(settings, c);
}

void fpga_prepare_correction(PCHANNELSETTINGS settings)
{
  acal_corr c;

  fpga_channel_correction(settings, &c);
  acal_live_prepared(settings, &c);

  settings->gain_a  = (int32)c.gain_a;
  settings->gain_b  = (int32)c.gain_b;
  settings->off_a   = c.off_a;
  settings->off_b   = c.off_b;
  settings->clip_lo = c.clip_lo;
  settings->clip_hi = c.clip_hi;
  settings->cal_ver = c.version;
  settings->dcshift = c.shift;
  settings->corr16  = c.corr16;
  settings->gain16  = c.gain16;
}

void fpga_read_sample_data(PCHANNELSETTINGS settings, uint32 triggerpoint)
{
  uint32 threshold;
  uint32 wide;
  
  //Clear min and max and average for new calculations
  settings->min     = 0x7FFFFFFF;
  settings->max     = 0;
  settings->average = 0;
  settings->rms     = 0;
  
  settings->minDC     = 0x7FFFFFFF;
  settings->maxDC     = 0;
  
  //Zero correction for this readout (per core, for the capture's clock class)
  fpga_prepare_correction(settings);

  //Decimation filter on: read the 16-bit samples now; both passes below then take them from memory (odd, even). The
  //converted samples already carry the full correction (gain16, corr16), so unity gain, no offset, no dither (the
  //dither's top 16 bits never carry into a whole code) and no rail rule on top
  pc_glue_decim_read(settings, triggerpoint);
  wide = (pc_decim_src != 0);

  if(pc_decim_src)
  {
    settings->gain_a  = settings->gain_b = 65536;
    settings->off_a   = settings->off_b  = 0;
    settings->clip_lo = 0;
    settings->clip_hi = 255;
  }

  //Pointer, ADC1 (core A) command and the dummy strobe (fpga_bus.h; 1.00o5 read one word late without it)
  fpga_read_start(settings->adc1command, (uint16_t)triggerpoint);

  //Signal no checking on the first ADC when compensating
  settings->checkfirstadc = 0;
  
  //Setup for reading the first ADC
  settings->gain = settings->gain_a;
  settings->off  = settings->off_a;
  settings->buffer = &settings->tracebuffer[1];
  
  //Read the data for the first ADC. Samples start on the second location, skipping every other sample
  fpga_read_adc_data(settings);

  //Save the calculated measurements
  settings->adc1rawaverage = settings->rawaverage;
  

  //Calculate the signal center for frequency determination
  //Calculate the signal center for autosetup dc shift  
  settings->centerDC = (settings->maxDC + settings->minDC) / 2;
  
  //Calculate the peak to peak value for autosetup dc shift
  settings->peakpeakDC = settings->maxDC - settings->minDC;
  
    //Calculate the signal center for frequency determination
  //settings->center = (settings->max + settings->min) / 2;
  
  //Calculate a zero crossing threshold value
  threshold = ((settings->maxDC - settings->minDC) / 10) + 2;
  
  //Set levels for detecting zero crossings
  settings->highlevel = settings->centerDC + threshold;
  settings->lowlevel  = settings->centerDC - threshold;
  
  
  /*
  //Calculate the signal center for frequency determination
  settings->center = (settings->max + settings->min) / 2;
  
  //Calculate a zero crossing threshold value
  threshold = ((settings->max - settings->min) / 10) + 2;
  
  //Set levels for detecting zero crossings
  settings->highlevel = settings->center + threshold;
  settings->lowlevel  = settings->center - threshold;
  */
  
  //Initialize the frequency determination
  settings->zerocrossings   = 0;
  settings->lowsamplecount  = 0;
  settings->lowdivider      = 0;
  settings->highsamplecount = 0;
  settings->highdivider     = 0;
  
  settings->previousindex = 0;
  
  //See in which state the signal starts
  if(settings->tracebuffer[1] > settings->highlevel)
  {
    //Above high it is a one
    settings->state = 1;
  }
  else
  {
    //Below start with a zero
    settings->state = 0;
  }
  
  //Pointer, ADC2 (core B) command and the dummy strobe
  fpga_read_start(settings->adc2command, (uint16_t)triggerpoint);

  //Signal to check the readings of the first ADC on being zero when compensating
  settings->checkfirstadc = 1;

  pc_glue_decim_even();

  //Setup for reading the second ADC
  settings->gain = settings->gain_b;
  settings->off  = settings->off_b;
  settings->buffer = &settings->tracebuffer[0];
  
  //Read the data for the second ADC. Samples start on the first location, skipping every other sample
  fpga_read_adc_data(settings);

  pc_decim_src = 0;

  //Save the calculated measurements
  settings->adc2rawaverage = settings->rawaverage;
  
  //Calculate the overall average
  settings->average /= scopesettings.samplecount;

  //Calculate the RMS
  settings->rms = isqrt(settings->rms / scopesettings.samplecount);

  //Calculate the peak to peak value
  settings->peakpeak = settings->max - settings->min;
  
  //Calculate the signal center
  settings->center = (settings->max + settings->min) / 2;

  //A peak record: Vmax / Vmin / Vpp from its min / max bytes (the center above stays the midpoint's)
  pc_glue_peak_extremes(settings);

  //Calibration v4: an 8-bit full-class record of CH1 / CH2 to the A - B tracker
  acal_live_record(settings, wide);
  
  //===================================
  /*
  //Calculate the frequency if possible
#define FP_SHIFT 20

if(settings->zerocrossings > 2 &&
   settings->highdivider &&
   settings->lowdivider)
{
  settings->frequencyvalid = 1;

  // Average HIGH time (Q16.16 samples)
  settings->hightime =
    ((uint64)settings->highsamplecount << FP_SHIFT)
    / settings->highdivider;

  // Average LOW time (Q16.16 samples)
  settings->lowtime =
    ((uint64)settings->lowsamplecount << FP_SHIFT)
    / settings->lowdivider;

  // FULL PERIOD = 2 × (HIGH + LOW)
  settings->periodtime =
    (settings->hightime + settings->lowtime) << 1;

  // Frequency
  settings->frequency =
    ((uint64)freq_calc_data[scopesettings.samplerate].sample_rate << FP_SHIFT)
    / settings->periodtime;
}
else
{
  settings->frequencyvalid = 0;
}

*/
  
  //=============================
/*
    // --------------------------------------------------------------
     // FREQUENCY + TIME CALCULATION
     // --------------------------------------------------------------
    if ((settings->zerocrossings >= 4) && (settings->highdivider > 0) && (settings->lowdivider > 0))
    {
        // Average HIGH and LOW time in SAMPLES 
        settings->hightime = settings->highsamplecount / settings->highdivider;

        settings->lowtime = settings->lowsamplecount / settings->lowdivider;

        settings->periodtime = settings->hightime + settings->lowtime;

        if (settings->periodtime > 0)
        {
            settings->frequency = (freq_calc_data[scopesettings.samplerate].sample_rate / settings->periodtime) / 2;

            settings->frequencyvalid = 1;
        }
        else
        {
            settings->frequencyvalid = 0;
        }
    }
    else
    {
        settings->frequencyvalid = 0;
    }
  
uint32 sr = freq_calc_data[scopesettings.samplerate].sample_rate;

settings->hightime =
    (uint64)settings->hightime * 1000000ULL / sr;

settings->lowtime =
    (uint64)settings->lowtime * 1000000ULL / sr;

        settings->periodtime = settings->hightime + settings->lowtime;
*/

//}

  
  
  
//=========================================
  

   //Calculate the frequency if possible
  if(settings->zerocrossings > 2)
  {
    //Signal valid frequency determination possible
    settings->frequencyvalid = 1;
    

                     //Calculate the average high time of the signal expressed in samples
    settings->hightime = (((uint64)settings->highsamplecount << 16) / settings->highdivider) * 2;
    
    //Calculate the average low time of the signal expressed in samples
    settings->lowtime = (((uint64)settings->lowsamplecount << 16) / settings->lowdivider) * 2;
    
    //Calculate the period time expressed in samples
    settings->periodtime = settings->hightime + settings->lowtime;
 
      //Calculate the frequency
    settings->frequency = ((uint64)freq_calc_data[scopesettings.samplerate].sample_rate << 16) / settings->periodtime;
 
 
/*
    settings->hightime = (settings->highsamplecount * 2) / settings->highdivider;

    settings->lowtime = (settings->lowsamplecount * 2) / settings->lowdivider;

    settings->periodtime = settings->hightime + settings->lowtime;

    settings->frequency = (uint64)freq_calc_data[scopesettings.samplerate].sample_rate / settings->periodtime;

*/
    
    //Calculate the frequency
   // settings->frequency = ((uint64)freq_calc_data[scopesettings.samplerate].sample_rate << 20) / settings->periodtime;
/*
     
            settings->hightime = settings->highsamplecount / settings->highdivider;

        settings->lowtime = settings->lowsamplecount / settings->lowdivider;

        settings->periodtime = settings->hightime + settings->lowtime;


            settings->frequency = (freq_calc_data[scopesettings.samplerate].sample_rate / settings->periodtime) / 2;
 */
            
/*
 
                 //Calculate the average high time of the signal expressed in samples
    settings->hightime = (((uint64)settings->highsamplecount << 20) / settings->highdivider) * 2;
    
    //Calculate the average low time of the signal expressed in samples
    settings->lowtime = (((uint64)settings->lowsamplecount << 20) / settings->lowdivider) * 2;
    
    //Calculate the period time expressed in samples
    settings->periodtime = settings->hightime + settings->lowtime;
 */
            
  }
  else
  {
    //Signal no valid frequency determination possible
    settings->frequencyvalid = 0;
  }
  
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_read_adc_data(PCHANNELSETTINGS settings)
{
  register int32  sample;
  register uint32 count;
  register uint32 sum = 0;
  //Correction of this pass (fpga_prepare_correction): code = (raw x gain + off + 16-bit dither) >> 16, and the rail
  //limits of the two cores
  register int32  gain    = settings->gain;
  register int32  off     = settings->off;
  register uint32 dither  = fpga_dither;
  register int32  dcshift = settings->dcshift;
  register int32  clip_lo = settings->clip_lo;
  register int32  clip_hi = settings->clip_hi;
  const int16 *src = pc_decim_src;      //decimation filter: samples already read and converted (pc_glue_decim_read)
  register uint32 settle = fpga_read_settle;
  register uint32 pre    = fpga_read_prestrobe;
  
  //static uint64 global_sample_counter = 0;
  
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_DATA_READ();
  
  //Set the number of samples to read
  count = scopesettings.nofsamples;
  //count = fpgasettings.totalsamples/2;
  
  //Read the data as long as there is count
  while(count)
  {
    if(src)
    {
      sample = *src;
      src += 2;
    }
    else
    {
      //CAL_PROBE A/B tests (fw 0.2.67): a write strobe first (no read, no address step), and the port read again
      //after the strobe, the last read taken
      if(pre)
      {
        FPGA_DATA_WRITE();
        FPGA_PULSE_CLK();
        FPGA_DATA_READ();
      }

      FPGA_PULSE_CLK();

      if(settle)
      {
        register uint32 i;

        for(i=0;i<settle;i++)
          (void)FPGA_GET_DATA();
      }

      sample = FPGA_GET_DATA();
    }
    
    //Sum the raw data for ADC difference calibration
    sum += sample;

    //Gain and zero correction of this core (adc_cal.h): the mean is exactly (raw x gain + off) / 65536
    ACAL_DITHER_STEP(dither);
    sample = (sample * gain + off + (int32)(dither >> 16)) >> 16;
     
    //Check if sample became negative
    if(sample < 0)
    {
      //Keep it on zero if so
      sample = 0;
    }
    
    //Check if sample over its max
    if(sample > 255)
    {
      //Keep it on max if so
      sample = 255;
    }
      
 //*****************************************************************************   
    //Check if sample overload ADC
    if (sample == 0)    settings->ADoverload |= 1;
    if (sample == 255)  settings->ADoverload |= 254;    //=255
 //*****************************************************************************   
    
    //Second ADC: near a rail the core that cannot reach the other one's code is stuck at its own limit, so both
    //codes of the pair take the more extreme one (1.00o5 rule, here on the corrected limits of both cores:
    //clip_lo = the higher of their codes for raw 0, clip_hi = the lower of their codes for raw 255)
    if(settings->checkfirstadc)
    {
      register int32 first = settings->buffer[1];

      if((sample < clip_lo) || (first < clip_lo))
      {
        if(first < sample)
          sample = first;
        else
          settings->buffer[1] = sample;
      }
      else if((sample > clip_hi) || (first > clip_hi))
      {
        if(first > sample)
          sample = first;
        else
          settings->buffer[1] = sample;
      }
    }

    if (settings->invert) sample = 255-sample;  //Invert channel settings 1-signal inverted 
    
    //Store the data
    *settings->buffer = sample;
    
//-*****************************************************************************    
    
        //Get the minimum value of the samples
    if(sample < settings->minDC)
    {
      //Keep the lowest
      settings->minDC = sample;
    }
    
    //Get the maximum value of the samples
    if(sample > settings->maxDC)
    {
      //Keep the highest
      settings->maxDC = sample;
    }
  //************** 
    /* ------------------------------------------------------------------
 * Robust zero-crossing + frequency + time+ / time- measurement
 * Time is measured ONLY in samples using a monotonic counter
 * ------------------------------------------------------------------ */


    /* --------------------------------------------------------------
     * GLOBAL MONOTONIC SAMPLE COUNTER
     * -------------------------------------------------------------- */
    
    //global_sample_counter++;

    /* --------------------------------------------------------------
     * ZERO-CROSSING / TIME MEASUREMENT
     * -------------------------------------------------------------- */
    /*
    if (settings->checkfirstadc)
    {
        uint64 idx = global_sample_counter;   // TRUE TIME IN SAMPLES

        // Rising edge: LOW -> HIGH 
        if (sample > settings->highlevel)
        {
            if (settings->state == 0)
            {
                settings->state = 1;

                if (settings->zerocrossings > 0)
                {
                    uint64 dt = idx - settings->previousindex;
                    settings->lowsamplecount += dt;
                    settings->lowdivider++;
                }

                settings->previousindex = idx;
                settings->zerocrossings++;
            }
        }
        // Falling edge: HIGH -> LOW 
        else if (sample < settings->lowlevel)
        {
            if (settings->state == 1)
            {
                settings->state = 0;

                if (settings->zerocrossings > 0)
                {
                    uint64 dt = idx - settings->previousindex;
                    settings->highsamplecount += dt;
                    settings->highdivider++;
                }

                settings->previousindex = idx;
                settings->zerocrossings++;
            }
        }
    }

*/

    //======================
        // ---- zero-crossing / frequency detection (robust pre veľké nofsamples) ---- 
    
    if (settings->checkfirstadc)
    {
        // compute a stable up-counting sample index (0 .. nofsamples-1) 
        uint64 idx = (scopesettings.nofsamples - count); // up-counter 

        // Use current sample (already compensated/inverted) 
        if (sample > settings->highlevel)
        {
            // Rising edge: Low -> High 
            if (settings->state == 0)
            {
                settings->state = 1;

                if (settings->zerocrossings == 0)
                {
                    // first crossing: just record the index 
                    settings->previousindex = idx;
                }
                else
                {
                    // interval that was LOW 
                    uint64 dt = idx - settings->previousindex;
                    settings->lowsamplecount += dt;
                    settings->lowdivider++;
                    settings->previousindex = idx;
                }

                settings->zerocrossings++;
            }
        }
        else if (sample < settings->lowlevel)
        {
            // Falling edge: High -> Low 
            if (settings->state == 1)
            {
                settings->state = 0;

                if (settings->zerocrossings == 0)
                {
                    settings->previousindex = idx;
                }
                else
                {
                    // interval that was HIGH 
                    uint64 dt = idx - settings->previousindex;
                    settings->highsamplecount += dt;
                    settings->highdivider++;
                    settings->previousindex = idx;
                }

                settings->zerocrossings++;
            }
        }
    }

    
    //=====================
    /*
    //Check if busy with second ADC data to see if frequency determination can be done
    if(settings->checkfirstadc)
    {
      //Check against the high level
      if(sample > settings->highlevel)
      {
        //If above, check if in low state
        if(settings->state == 0)
        {
          //If so flip the state
          settings->state = 1;
          
          //Check if first zero crossing detected
          if(settings->zerocrossings == 0)
          {
            //Set the previous index if so
            settings->previousindex = count;
          }
          else
          {
            //Calculate the total number of samples in the low parts of the signal
            settings->lowsamplecount += settings->previousindex - count;
            
            //Add one to the low divider for average calculation
            settings->lowdivider++;
            
            //Set the new previous index
            settings->previousindex = count;
          }

          //Found a zero crossing
          settings->zerocrossings++;
        }
      }
      //Check against the low level
      else if(sample < settings->lowlevel)
      {
        //If below check if in high state
        if(settings->state == 1)
        {
          //If so flip the state
          settings->state = 0;

          //Check if first zero crossing detected
          if(settings->zerocrossings == 0)
          {
            //Set the previous index if so
            settings->previousindex = count;
          }
          else
          {
            //Calculate the total number of samples in the high parts of the signal
            settings->highsamplecount += settings->previousindex - count;
            
            //Add one to the low divider for average calculation
            settings->highdivider++;
            
            //Set the new previous index
            settings->previousindex = count;
          }
          
          //Found a zero crossing
          settings->zerocrossings++;
        }
      }
    }  
    */
  //***************  
    //For dc shift in DC mode  
    sample += dcshift;              //dcoffset x 100 / dc_shift_value, computed once per readout
    
    //Get the minimum value of the samples
    if(sample < settings->min)
    {
      //Keep the lowest
      settings->min = sample;
    }
    
    //Get the maximum value of the samples
    if(sample > settings->max)
    {
      //Keep the highest
      settings->max = sample;
    }
    
    //Add the samples for average calculation
    settings->average += sample;

	//Add squares of the samples for RMS calculation
	settings->rms += (sample - 128) * (sample - 128);
     
    //Skip a sample to allow for ADC2 data to be placed into
    settings->buffer += 2;
    //fpga fifo RAM    
    //settings->buffer += 1;
         
    //One read done
    count--;
  }
 
  fpga_dither = dither;

  //Calculate the raw average
  settings->rawaverage = sum / scopesettings.nofsamples;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint16 fpga_average_trace_data(PCHANNELSETTINGS settings)
{
  //Read ten bytes of trace data and average them
  uint16 data = 0;
  int16  datatmp = 0;
  uint32 count = 10;
  
  //register uint32 setting = settings->samplevoltperdiv;
  
  //Send a command for getting trace data from the FPGA
  fpga_write_cmd(settings->averagecommand);
  
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_DATA_READ();

  //Read the data as long as there is count
  while(count)
  {
    //Clock the data to the output of the FPGA
    FPGA_PULSE_CLK();
 
    //Read the data CH1 or CH2
    datatmp = FPGA_GET_DATA() + settings->adc1compensation; 
    
    //Check limits
    if (datatmp > 255)  datatmp = 255;
    if (datatmp < 0)    datatmp = 0;
        
        
    data += (uint16)datatmp;
    
    //One read done
    count--;
  }
  
  //Calculate the average and return it
  //data = data / 10;
  //data = (data * multiply) >> VOLTAGE_SHIFTER;
  return(data / 10);
}

uint16 fpga_average_trace_data_long(PCHANNELSETTINGS settings)
{
  //Read ten bytes of trace data and average them
  uint16 data = 0;
  int16 datatmp = 0;
  uint32 count = 100;
  
  //register uint32 setting = settings->samplevoltperdiv;
  
  //Send a command for getting trace data from the FPGA
  fpga_write_cmd(settings->averagecommand);
  
  
  //Set the bus for reading
  FPGA_BUS_DIR_IN();
  
  //Set the control lines for reading a command
  FPGA_DATA_READ();

  //Read the data as long as there is count
  while(count)
  {
    //Clock the data to the output of the FPGA
    FPGA_PULSE_CLK();
 
    //Read the data CH1 or CH2
    datatmp = FPGA_GET_DATA() + settings->adc1compensation; 
    
    //Check limits
    if (datatmp > 255)  datatmp = 255;
    if (datatmp < 0)    datatmp = 0;
        
        
    data += (uint16)datatmp;
    
    //One read done
    count--;
  }
  
  //Calculate the average and return it
  //data = data / 10;
  //data = (data * multiply) >> VOLTAGE_SHIFTER;
    
  return(data);
}



//----------------------------------------------------------------------------------------------------------------------------------
//Decimation filter, bitstream 0x1534+ only (pc_decim.h). Callers check the bitstream: older ones do not decode 0x29,
//0x53-0x55. 0x53, 0x54 and 0x56: fpga_decim_write (fpga_bus.c, fw 0.2.87).

void fpga_set_decim_level(uint16 level)
{
  //16-bit trigger level, used instead of 0x17 while K >= 1
  fpga_write_cmd(0x55);
  fpga_write_short(level);
}

uint16 fpga_get_decim_state(void)
{
  //High byte {bit 7 16-bit layout active, bit 6 FIR enable, bit 5 FIR active, bit 4 neg B, bit 3 neg A, bit 2 swap,
  //bits 1:0 K}, low byte S
  fpga_write_cmd(0x29);
  return(fpga_read_short());
}

//----------------------------------------------------------------------------------------------------------------------------------

void fpga_delay(uint32 usec)
{
  uint32 loops = usec * 54;

  __asm__ __volatile__ ("1:\n" "subs %0, %1, #1\n"  "bne 1b":"=r"(loops):"0"(loops));
}

//----------------------------------------------------------------------------------------------------------------------------------
