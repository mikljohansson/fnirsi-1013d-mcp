//----------------------------------------------------------------------------------------------------------------------------------

//Scan the touch panel

//Main state is displaying the scope data

//On touch the first action is to see where the touch is, so an array of boxes might be needed
//Difficulty lies in when a trace or cursor is selected.

//----------------------------------------------------------------------------------------------------------------------------------

#include "statemachine.h"
#include "pc_hooks.h"
#include "touchpanel.h"
#include "timer.h"
#include "fpga_control.h"
#include "sd_card_interface.h"
#include "scope_functions.h"
#include "power_and_battery.h"
#include "display_lib.h"
#include "DS3231.h"
#include "menu.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_ctrl.h"
#include "ui_meas.h"
#include "bode_view.h"
#include "ui_fmt.h"
#include "ctrl_page.h"
#include "touch_pick.h"
#include "tb_view.h"
#include "acq_view.h"
#include "ui_diag_view.h"
#include "test.h"

#include  "ref_and_math.h"
#include "generator.h"
#include "PC_interface.h"

#include "variables.h"
#include "trig_ui.h"

//----------------------------------------------------------------------------------------------------------------------------------
//fw 0.2.54: a drag distance in screen units, divided by 5 when Move is slow

static int32 touch_drag_delta(int32 distance)
{
  if(scopesettings.movespeed)
  {
    distance /= 5;
  }

  return distance;
}

//----------------------------------------------------------------------------------------------------------------------------------
//touch handler

void touch_handler(void)
{
  int32 delta;

  //Check on touch state
  if(touchstate == TOUCH_STATE_INACTIVE)
  {
    //No touch yet so scan for it
    scan_for_touch();

    //When movement of object initiated previous settings need to be saved for updating
    if(touchstate)
    {
      //Check if the trigger point position can be moved (fw 0.2.64: also by the record slider)
      if((touchstate & TOUCH_STATE_MOVE_TRIGGER_POINT) || ((touchstate & TOUCH_STATE_MASK) == TOUCH_STATE_MOVE_RECORD))
      {
        //Save the current position for it (fw 0.2.76: after the slider's tap to jump, done at touch-down in
        //scan_for_touch)
        previous_trigger_point_position = scopesettings.triggerhorizontalposition;
      }

      //Save the data for the selected object
      switch(touchstate & TOUCH_STATE_MASK)
      {
        case TOUCH_STATE_X_Y_MODE:
          previous_channel_1_offset = scopesettings.channel1.traceposition;
          previous_channel_2_offset = scopesettings.channel2.traceposition;
          break;

        case TOUCH_STATE_MOVE_CHANNEL_1:
          previous_channel_1_offset = scopesettings.channel1.traceposition;

          //Check if trigger on this channel and if so save it's current offset
          if(scopesettings.triggerchannel == 0)
          {
            previous_trigger_level_offset = scopesettings.triggerverticalposition;
          }
          break;

        case TOUCH_STATE_MOVE_CHANNEL_2:
          previous_channel_2_offset = scopesettings.channel2.traceposition;

          //Check if trigger on this channel and if so save it's current offset (fw 0.2.54: 1 only, 2 is EXT)
          if(scopesettings.triggerchannel == 1)
          {
            previous_trigger_level_offset = scopesettings.triggerverticalposition;
          }
          break;

        case TOUCH_STATE_MOVE_TRIGGER_LEVEL:
          previous_trigger_level_offset = scopesettings.triggerverticalposition;
          break;

        case TOUCH_STATE_MOVE_TIME_CURSOR_LEFT:
          previous_left_time_cursor_position = scopesettings.timecursor1position;
          //for lock move cursors
          if(scopesettings.lockcursors) previous_right_time_cursor_position = scopesettings.timecursor2position;
          break;

        case TOUCH_STATE_MOVE_TIME_CURSOR_RIGHT:
          previous_right_time_cursor_position = scopesettings.timecursor2position;
          break;

        case TOUCH_STATE_MOVE_VOLT_CURSOR_TOP:
          previous_top_volt_cursor_position = scopesettings.voltcursor1position;
          //for lock move cursors
          //if(scopesettings.lockcursors) previous_bottom_volt_cursor_position = scopesettings.voltcursor2position; 
          break;

        case TOUCH_STATE_MOVE_VOLT_CURSOR_BOTTOM:
          previous_bottom_volt_cursor_position = scopesettings.voltcursor2position;
          //for lock move cursors
          if(scopesettings.lockcursors) previous_top_volt_cursor_position = scopesettings.voltcursor1position;
          break;
          
        case TOUCH_STATE_MOVE_REF1:
          previous_ch_ref1_position = scopesettings.ch_ref1.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF2:
          previous_ch_ref2_position = scopesettings.ch_ref2.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF3:
          previous_ch_ref3_position = scopesettings.ch_ref3.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF4:
          previous_ch_ref4_position = scopesettings.ch_ref4.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF5:
          previous_ch_ref5_position = scopesettings.ch_ref5.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF6:
          previous_ch_ref6_position = scopesettings.ch_ref6.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF7:
          previous_ch_ref7_position = scopesettings.ch_ref7.traceposition;
          break;
          
        case TOUCH_STATE_MOVE_REF8:
          previous_ch_ref8_position = scopesettings.ch_ref8.traceposition;
          break;
      }
    }
  }
  else
  {
    //At this point the movement of the traces, cursors and pointers is handled
    //Read the touch panel status to get a new position or be done
    tp_i2c_read_status();
    
    //When no longer touched reset and quit
    if(havetouch == 0)
    {
      //Signal done with moving
      touchstate = TOUCH_STATE_INACTIVE;

      //That's it for now
      return;
    }

    //Slow things down a bit. This is not a proper solution but will do for now
    //timer0_delay(50);  //before 0.26v4

    //Check if the trigger point position can be moved
    if(touchstate & TOUCH_STATE_MOVE_TRIGGER_POINT)
    {
      //Move the trigger point position
      move_trigger_point_position();
    }

    //Handle the selected object
    switch(touchstate & TOUCH_STATE_MASK)
    {
      case TOUCH_STATE_X_Y_MODE:
        //x moves channel 1, y moves channel 2
        change_channel_1_offset(touch_drag_delta((int32)xtouch - (int32)previousxtouch));
        change_channel_2_offset(touch_drag_delta((int32)previousytouch - (int32)ytouch));
        break;

      //fw 0.2.54: a channel and the trigger level on it (triggerchannel 1 for CH2; 2 is EXT) move by one delta, limited so
      //both stay in the drag range: the level keeps its distance to the trace (its voltage) at the edges
      case TOUCH_STATE_MOVE_CHANNEL_1:
        if(scopesettings.channel1.enable)
        {
          delta = touch_drag_delta((int32)previousytouch - (int32)ytouch);

          if(scopesettings.triggerchannel == 0)
          {
            delta = touch_shared_delta(delta, previous_channel_1_offset, previous_trigger_level_offset, TRACE_POS_MIN, TRACE_POS_MAX);
            change_channel_1_offset(delta);
            change_trigger_level_offset(delta);
          }
          else
          {
            change_channel_1_offset(delta);
          }
        }
        break;

      case TOUCH_STATE_MOVE_CHANNEL_2:
        if(scopesettings.channel2.enable)
        {
          delta = touch_drag_delta((int32)previousytouch - (int32)ytouch);

          if(scopesettings.triggerchannel == 1)
          {
            delta = touch_shared_delta(delta, previous_channel_2_offset, previous_trigger_level_offset, TRACE_POS_MIN, TRACE_POS_MAX);
            change_channel_2_offset(delta);
            change_trigger_level_offset(delta);
          }
          else
          {
            change_channel_2_offset(delta);
          }
        }
        break;

      case TOUCH_STATE_MOVE_TRIGGER_LEVEL:
        change_trigger_level_offset(touch_drag_delta((int32)previousytouch - (int32)ytouch));
        break;

      //fw 0.2.64: the record slider's window follows the finger (dragging right shows later samples, the trace moves
      //left), limited to the record
      case TOUCH_STATE_MOVE_RECORD:
        scopesettings.triggerhorizontalposition = acq_view_slider_pos(previous_trigger_point_position,
                                                                      touch_drag_delta((int32)xtouch - (int32)previousxtouch));
        break;

      case TOUCH_STATE_MOVE_TIME_CURSOR_LEFT:
        move_left_time_cursor_position();
        //for lock cursors
        if(scopesettings.lockcursors) move_right_time_cursor_position();
        break;

      case TOUCH_STATE_MOVE_TIME_CURSOR_RIGHT:
        move_right_time_cursor_position();
        break;

      case TOUCH_STATE_MOVE_VOLT_CURSOR_TOP:
        move_top_volt_cursor_position();
        //for lock cursors
        //if(scopesettings.lockcursors) move_bottom_volt_cursor_position();
        break;

      case TOUCH_STATE_MOVE_VOLT_CURSOR_BOTTOM:
        move_bottom_volt_cursor_position();
        //for lock cursors
        if(scopesettings.lockcursors) move_top_volt_cursor_position();
        break;
        
      case TOUCH_STATE_MOVE_REF1:
        if(scopesettings.ref1) move_ref1_position();
        break;  
        
      case TOUCH_STATE_MOVE_REF2:
        if(scopesettings.ref2) move_ref2_position();
        break;
        
      case TOUCH_STATE_MOVE_REF3:
        if(scopesettings.ref3) move_ref3_position();
        break;
        
      case TOUCH_STATE_MOVE_REF4:
        if(scopesettings.ref4) move_ref4_position();
        break;
        
      case TOUCH_STATE_MOVE_REF5:
        if(scopesettings.ref5) move_ref5_position();
        break;
        
      case TOUCH_STATE_MOVE_REF6:
        if(scopesettings.ref6) move_ref6_position();
        break;
        
      case TOUCH_STATE_MOVE_REF7:
        if(scopesettings.ref7) move_ref7_position();
        break;
        
      case TOUCH_STATE_MOVE_REF8:
        if(scopesettings.ref8) move_ref8_position();
        break;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//fw 0.2.54: the left handle column. The pointers drawn at the left edge (scope_draw_pointers, draw order CH1, CH2, ref1..ref8)
//are drag handles: the switched-on pointer whose 44 px box holds the touch-down point, nearest first, a tie to the one
//drawn on top. Returns its TOUCH_STATE_MOVE_* or 0. Normal display only (in X-Y the CH1 pointer is a top pointer).

static uint8 scan_left_handle(int32 x, int32 y)
{
  static const uint8 state[10] =
  {
    TOUCH_STATE_MOVE_CHANNEL_1, TOUCH_STATE_MOVE_CHANNEL_2,
    TOUCH_STATE_MOVE_REF1, TOUCH_STATE_MOVE_REF2, TOUCH_STATE_MOVE_REF3, TOUCH_STATE_MOVE_REF4,
    TOUCH_STATE_MOVE_REF5, TOUCH_STATE_MOVE_REF6, TOUCH_STATE_MOVE_REF7, TOUCH_STATE_MOVE_REF8
  };
  TOUCH_POINTER p[10] =
  {
    { scopesettings.channel1.enable, scopesettings.channel1.traceposition },
    { scopesettings.channel2.enable, scopesettings.channel2.traceposition },
    { scopesettings.ref1, scopesettings.ch_ref1.traceposition },
    { scopesettings.ref2, scopesettings.ch_ref2.traceposition },
    { scopesettings.ref3, scopesettings.ch_ref3.traceposition },
    { scopesettings.ref4, scopesettings.ch_ref4.traceposition },
    { scopesettings.ref5, scopesettings.ch_ref5.traceposition },
    { scopesettings.ref6, scopesettings.ch_ref6.traceposition },
    { scopesettings.ref7, scopesettings.ch_ref7.traceposition },
    { scopesettings.ref8, scopesettings.ch_ref8.traceposition }
  };
  int32 i = pick_left_handle(p, 10, x, y);

  return (i < 0) ? 0 : state[i];
}

//----------------------------------------------------------------------------------------------------------------------------------
//fw 0.2.64 (touch batch 2): the distance from (x, y) to a channel's drawn trace, -1 when it has none there (phosphor view:
//the map replaces the polyline, so only the zero line counts)

static int32 scan_trace_distance(PCHANNELSETTINGS c, int32 x, int32 y)
{
  if(!c->enable || pc_glue_phosphor_on() || !c->tracepoints || (c->noftracepoints == 0))
    return -1;

  return trace_distance((const TOUCH_XY *)c->tracepoints, (c->noftracepoints <= TRACE_POINTS_MAX) ? c->noftracepoints : TRACE_POINTS_MAX, x, y);
}

//The object under the touch-down point in the normal display, as a TOUCH_STATE_MOVE_* (0 = none: a drag pans the trigger
//point). *handle is set for the handles, which never take a tap: the left pointers, the record slider, the trigger level.
//Order: the left handle column; the slider row; the trigger level pointer; then the nearest of the channels (drawn trace
//or zero line, TRACE_GRAB_RADIUS) and the cursors (30 px; a tie to the cursor, drawn on top); last the slider's
//extended hit area (y TBV_SLIDER_HIT_Y..449) when nothing else is near.

static uint8 scan_plot_pick(int32 x, int32 y, uint8 *handle)
{
  uint8 pick = 0;
  int32 best = 0x7FFFFFFF, d, dl, dr;
  int   slider_ok = !scopesettings.long_mode && (x >= TBV_SLIDER_FRAME_X) && (x < TBV_SLIDER_FRAME_X + TBV_SLIDER_FRAME_W);

  *handle = 1;

  if((pick = scan_left_handle(x, y)) != 0)
    return pick;

  if(slider_ok && (y >= TBV_SLIDER_FRAME_Y - 2))
    return TOUCH_STATE_MOVE_RECORD;

  //Trigger level pointer at the right edge, only while it is drawn (scope_draw_pointers)
  if((x > 660) && (!scopesettings.long_mode || scopesettings.triggermode))
  {
    d = y - (448 - (int32)scopesettings.triggerverticalposition);

    if((d > -30) && (d < 30))
      return TOUCH_STATE_MOVE_TRIGGER_LEVEL;
  }

  *handle = 0;

  //Channels: drawn trace or zero line
  if((x >= HANDLE_W) && (x < 686))
  {
    uint32 ch = pick_channel_near(scopesettings.channel1.enable, scopesettings.channel1.traceposition,
                                  scan_trace_distance(&scopesettings.channel1, x, y),
                                  scopesettings.channel2.enable, scopesettings.channel2.traceposition,
                                  scan_trace_distance(&scopesettings.channel2, x, y), y);

    if(ch)
    {
      PCHANNELSETTINGS c = (ch == 1) ? &scopesettings.channel1 : &scopesettings.channel2;
      int32 t = scan_trace_distance(c, x, y);

      d = y - (448 - (int32)c->traceposition);
      if(d < 0)
        d = -d;
      if((t >= 0) && (t < d))
        d = t;

      best = d;
      pick = (ch == 1) ? TOUCH_STATE_MOVE_CHANNEL_1 : TOUCH_STATE_MOVE_CHANNEL_2;
    }
  }

  //Time cursors: the left one when strictly nearer (1.00o5)
  if(scopesettings.timecursorsenable)
  {
    dl = x - (int32)scopesettings.timecursor1position;
    dr = x - (int32)scopesettings.timecursor2position;
    if(dl < 0) dl = -dl;
    if(dr < 0) dr = -dr;

    if((dl < 30) && (dl < dr) && (dl <= best))
    {
      best = dl;
      pick = TOUCH_STATE_MOVE_TIME_CURSOR_LEFT;
    }
    else if((dr < 30) && (dr <= best))
    {
      best = dr;
      pick = TOUCH_STATE_MOVE_TIME_CURSOR_RIGHT;
    }
  }

  //Volt cursors: the top one when strictly nearer (1.00o5)
  if(scopesettings.voltcursorsenable && (x > 60))
  {
    dl = y - (int32)scopesettings.voltcursor1position;
    dr = y - (int32)scopesettings.voltcursor2position;
    if(dl < 0) dl = -dl;
    if(dr < 0) dr = -dr;

    if((dl < 30) && (dl < dr) && (dl <= best))
    {
      best = dl;
      pick = TOUCH_STATE_MOVE_VOLT_CURSOR_TOP;
    }
    else if((dr < 30) && (dr <= best))
    {
      best = dr;
      pick = TOUCH_STATE_MOVE_VOLT_CURSOR_BOTTOM;
    }
  }

  if(pick)
    return pick;

  //The slider's hit area is 44 px tall: its upper part, when nothing else is near
  if(slider_ok && (y >= TBV_SLIDER_HIT_Y))
  {
    *handle = 1;
    return TOUCH_STATE_MOVE_RECORD;
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Top menu bar ranges from 0,0 to 730,46        //44
//Right menu bar ranges from 730,0 to 800,480

void scan_for_touch(void)
{
  uint8  handle = 0, pick;
  uint32 moved;

  //Read the touch panel status
  tp_i2c_read_status();
  
  //Check if the panel is not touched
  if(havetouch == 0)
  {
    //Quit if so
    return;
  }
  
  //scope_test_TP();
  //xtouch_tmp=xtouch;
  //ytouch_tmp=ytouch;

  //Draw directly to screen
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
 
  //Scan for where the touch is applied
  if((xtouch <= 730) && (ytouch <= 44))//46
  {
    //Top menu bar, so handle that
    //Check if in main menu button range
    if(xtouch <= 80)
    {
      //Check if in normal state
      if(!scopesettings.waveviewmode)
      {
        //Set the button active
        scope_menu_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Set the button inactive
        scope_menu_button(0);

        //Save the screen rectangle where the menu will be displayed
        display_set_destination_buffer(displaybuffer1);//2 0.026p z,menene
        display_copy_rect_from_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);//0  149 233

        //Go and setup the channel 1 menu
        scope_open_main_menu();

        //Go and handle the menu touch
        handle_main_menu_touch();

        //Restore the screen when done
        display_set_source_buffer(displaybuffer1);//2
        display_copy_rect_to_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);//0  149 233
        
        //set other values and clear view area
        if(scopesettings.long_mode) scope_preset_values();
            //scope_update_cursor_possition(); else scope_preset_values();
      }
      else
      {
        //Set the button active
        scope_main_return_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Switch back to the main menu button and set it inactive
        scope_menu_button(0);

        //Reset the wave view mode back to normal state
        scopesettings.waveviewmode = 0;
      }
    }
    //===========================================================================================================
    //Check if in maximum lignt button range
    else if((xtouch >= MAXLIGHT_BUTTON_XPOS) && (xtouch <= MAXLIGHT_BUTTON_XPOS+MAXLIGHT_BUTTON_WIDTH)) //535 545
    {
      //Set the button active
      scope_maxlight_item(2);
    
      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //fw 0.2.68: 100 % screen brightness and back to the value it came from (ui_settings.h); the sun redraws
      scope_brightness_sun();
    }
    //===========================================================================================================  
    //Check if in channel 1 settings button range
    else if((xtouch >= CH1_BUTTON_XPOS) && (xtouch <= CH1_BUTTON_XPOS + CH_BUTTON_BG_WIDTH))
    {
      //Set the button active
      scope_channel_settings(&scopesettings.channel1, 1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Set the button inactive
      scope_channel_settings(&scopesettings.channel1, 0);

      //Save the screen rectangle where the menu will be displayed
      display_set_destination_buffer(displaybuffer1);//2
      display_copy_rect_from_screen(CH1_MENU_XPOS, CH_MENU_YPOS, CH_MENU_WIDTH, CH_MENU_HEIGHT);

      //Go and setup the channel 1 menu
      scope_open_channel_menu(&scopesettings.channel1);
      
      //Signal the channel menu is opened
      channelmenuopen = 1;

      //Go and handle the menu touch
      handle_channel_menu_touch(&scopesettings.channel1);
      
      //Signal the channel menu is close
      //channelmenuopen = 0;

      //Restore the screen when done //netreba je v close_open_channel_menus
      //display_set_source_buffer(displaybuffer1);//2
      //display_copy_rect_to_screen(CH1_MENU_XPOS, CH_MENU_YPOS, CH_MENU_WIDTH, CH_MENU_HEIGHT);
    }
    //===========================================================================================================
    //Check if in channel 2 settings button range
    else if((xtouch >= CH2_BUTTON_XPOS) && (xtouch <= CH2_BUTTON_XPOS + CH_BUTTON_BG_WIDTH))
    {
      //Set the button active
      scope_channel_settings(&scopesettings.channel2, 1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Set the button inactive
      scope_channel_settings(&scopesettings.channel2, 0);

      //Save the screen rectangle where the menu will be displayed
      display_set_destination_buffer(displaybuffer1);
      display_copy_rect_from_screen(CH2_MENU_XPOS, CH_MENU_YPOS, CH_MENU_WIDTH, CH_MENU_HEIGHT);

      //Go and setup the channel 1 menu
      scope_open_channel_menu(&scopesettings.channel2);
      
      //Signal the channel menu is opened
      channelmenuopen = 1;
      
      //Go and handle the menu touch
      handle_channel_menu_touch(&scopesettings.channel2);
      
      //Signal the channel menu is close
      //channelmenuopen = 0;

      //Restore the screen when done //netreba je v close_open_channel_menus
      //display_set_source_buffer(displaybuffer1);
      //display_copy_rect_to_screen(CH2_MENU_XPOS, CH_MENU_YPOS, CH_MENU_WIDTH, CH_MENU_HEIGHT);
    }
    //===========================================================================================================
    //Check if in acquisition button range & set short time base (long_mode = 0)
    else if(((xtouch >= ACQ_BUTTON_XPOS) && (xtouch <= ACQ_BUTTON_XPOS + ACQ_BUTTON_BG_WIDTH)))// && (!long_mode) )
    {
      //Set the button active
      scope_acqusition_settings(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Set the button inactive
      scope_acqusition_settings(0);

      //fw 0.2.71 (bode-ui.md 7): in Frequency response the dropdown is one line and no controls
      if(bode_view_active())
      {
        bode_view_top_note(0);
        return;
      }

      //Save the screen rectangle where the menu will be displayed
      display_set_destination_buffer(displaybuffer1);
      display_copy_rect_from_screen(ACQ_MENU_XPOS, ACQ_MENU_YPOS, ACQ_MENU_WIDTH, ACQ_MENU_HEIGHT);

      //Go and setup the channel 1 menu
      scope_open_acquisition_menu();

      //Go and handle the menu touch
      handle_acquisition_menu_touch();

      //Restore the screen when done
      display_set_source_buffer(displaybuffer1);
      display_copy_rect_to_screen(ACQ_MENU_XPOS, ACQ_MENU_YPOS, ACQ_MENU_WIDTH, ACQ_MENU_HEIGHT);
    }
    //===========================================================================================================
    //Check if in move speed button range
    else if((xtouch >= MOVE_FAST_BUTTON_XPOS) && (xtouch <= MOVE_FAST_BUTTON_XPOS+MOVE_FAST_BUTTON_WIDTH))//487 527
    {
      //Set the button active
      scope_move_speed(1);
      
      //if (dev_mode) watchdog_reset_now();


      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Toggle the speed
      scopesettings.movespeed ^= 1;

      //Set the button inactive and show the new state
      scope_move_speed(0);
    }  
    //===========================================================================================================
    //Check if in trigger settings button range
    else if((xtouch >= TRIGGER_BUTTON_XPOS) && (xtouch <= TRIGGER_BUTTON_XPOS+TRIGGER_BUTTON_BG_WIDTH))    
    {
      //Set the button active
      scope_trigger_settings(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Set the button inactive
      scope_trigger_settings(0);

      //Save the screen rectangle where the menu will be displayed (ui-type: at its largest height, the type changes
      //the height inside the menu)
      display_set_destination_buffer(displaybuffer1);
      display_copy_rect_from_screen(TRIGGER_MENU_XPOS, TRIGGER_MENU_YPOS, TRIGGER_MENU_WIDTH, TRIGGER_MENU_SAVE_H);

      //Go and setup the trigger menu
      scope_open_trigger_menu();
      
      //Signal the trigger menu is opened
      triggermenuopen = 1;

      //Go and handle the menu touch
      handle_trigger_menu_touch();
      
      //****************
      //Go and setup the channel 1 menu
      //scope_open_channel_menu(&scopesettings.channel1);
      
      //Signal the channel menu is opened
     // channelmenuopen = 1;

      //Go and handle the menu touch
      //handle_channel_menu_touch(&scopesettings.channel1);

      //***************

      //Restore the screen when done
      //display_set_source_buffer(displaybuffer1);
      //display_copy_rect_to_screen(560, 46, 172, 246);//186
      //display_copy_rect_to_screen(TRIGGER_MENU_XPOS, TRIGGER_MENU_YPOS, TRIGGER_MENU_WIDTH, TRIGGER_MENU_HEIGHT);
      
      //Preset screen and values to long time mod
      if(scopesettings.long_mode) scope_preset_values();  
    }
    //===========================================================================================================
    //******************
    //Check if in generator settings button range
    else if((xtouch >= GEN_BUTTON_XPOS) && (xtouch <= GEN_BUTTON_XPOS+GEN_BUTTON_BG_WIDTH) PC_UI_HAS(PC_FEAT_GEN_DDS))    
    {
      //Set the button active
      scope_generator_settings(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Set the button inactive
      scope_generator_settings(0);

      //fw 0.2.71 (bode-ui.md 7): while a frequency response sweep drives the generator, its dropdown says so
      if(bode_view_gen_tone(0))
      {
        bode_view_top_note(1);
        return;
      }

      //Save the screen rectangle where the menu will be displayed
      display_set_destination_buffer(displaybuffer1);
      //display_copy_rect_from_screen(560, 46, 172, 246);//186
      display_copy_rect_from_screen(GEN_MENU_XPOS, GEN_MENU_YPOS, GEN_MENU_WIDTH, GEN_MENU_HEIGHT);

      //Go and setup the generator menu
      scope_open_generator_menu();
      
      //Signal the generator menu is opened
      genmenuopen = 1;

      //Go and handle the menu touch
      handle_generator_menu_touch();

      //Restore the screen when done
      //display_set_source_buffer(displaybuffer1);
      //display_copy_rect_to_screen(560, 46, 172, 246);//186
      //display_copy_rect_to_screen(GEN_MENU_XPOS, GEN_MENU_YPOS, GEN_MENU_WIDTH, GEN_MENU_HEIGHT);
      
      //Preset screen and values to long time mod
      if(scopesettings.long_mode) scope_preset_values();  
    }
    //******************
    //===========================================================================================================
  }
  else if(xtouch > 730)
  {
    //Check if control button is touched
    if((ytouch >= 3) && (ytouch <= 57))
    {
      //Highlight the button if touched
      scope_control_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Button back to inactive state
      scope_control_button(0);

      //Toggle the right menu state
      //scopesettings.rightmenustate ^= 1;
      
      if ((dc_shift_cal)&&(scopesettings.rightmenustate < 2))
      {   scopesettings.rightmenustate = 2; 
      
          //frst run,reset
          if ((scopesettings.channel1.dc_shift_center<1)||(scopesettings.channel1.dc_shift_center>250))
          {
            scopesettings.channel1.dc_shift_center = 174;
            scopesettings.channel1.dc_shift_size   = 220;
            scopesettings.channel1.dc_shift_value  = 385;
    
            scopesettings.channel2.dc_shift_center = 174;
            scopesettings.channel2.dc_shift_size   = 220;
            scopesettings.channel2.dc_shift_value  = 385;
          }
      
      }    
      else {if (scopesettings.rightmenustate < 2) scopesettings.rightmenustate ^= 1; 
                else scopesettings.rightmenustate = 0;}
      
      //if dcshift menu and pres ctrl switch to normal scope menu
      //if (scopesettings.rightmenustate > 1) scopesettings.rightmenustate = 0;
      
      
      //if (scopesettings.rightmenustate > 0) scopesettings.rightmenustate = 0;
        //else scopesettings.rightmenustate = 1;

      //Display the changed state
      scope_setup_right_control_menu();
    }
    //Not then handle the menus based on the selected menu state
    else
    {
      //Right menu bar, so handle it according to the menu state
      if(scopesettings.rightmenustate == 0)
      {
        //Basic control state
        handle_right_basic_menu_touch();
      }
      else
      {
        //Volts per div adjust state
        handle_right_volts_div_menu_touch();
      }
    }
  }
  else
  {
    //Main screen part, so handle that
    //PC protocol build: the spectrum view takes touches in the trace area
    PC_SPECTRUM_TOUCH();

    //Get timer ticks to detect touch duration
    previoustimerticks = timer0_get_ticks();

    //Set first touch position for movement detection
    previousxtouch = xtouch;
    previousytouch = ytouch;

    //fw 0.2.64: the object under the touch-down point, picked before the finger moves (handles, slider, trace, level,
    //cursors; scan_plot_pick)
    pick = (scopesettings.xymodedisplay == 0) ? scan_plot_pick(previousxtouch, previousytouch, &handle) : 0;
    moved = 0;

    //fw 0.2.76, tap to jump at touch-down (the owner's 0.2.73 tap in the track did nothing): 0.2.73 jumped in
    //touch_handler, which runs only once the finger has moved past the dead band, so a still tap ended in the tap
    //branch below, where the slider (a handle) takes no tap. The Spectrum jumps at touch-down; so does the Scope now,
    //and a drag that follows goes on from the new place (touch_handler snapshots the position after this).
    if(pick == TOUCH_STATE_MOVE_RECORD)
      scopesettings.triggerhorizontalposition = acq_view_slider_jump(scopesettings.triggerhorizontalposition,
                                                                     (int32)previousxtouch, (int32)previousytouch);

    //Wait for touch to end and detect movement within the trace display section (still a blocking loop: touch-review
    //batch 3 replaces it)
    while(havetouch)
    {
      //Scan the touch panel for touch
      tp_i2c_read_status();

      //Check if there still is touch
      if(havetouch)
      {
        xtouchdisplacement = (xtouch < previousxtouch) ? previousxtouch - xtouch : xtouch - previousxtouch;
        ytouchdisplacement = (ytouch < previousytouch) ? previousytouch - ytouch : ytouch - previousytouch;
        maxdisplacement    = (xtouchdisplacement > ytouchdisplacement) ? xtouchdisplacement : ytouchdisplacement;

        if(maxdisplacement > moved)
          moved = maxdisplacement;

        //fw 0.2.64: a dead band of TOUCH_DEAD_BAND px (was 2) so finger jitter on a tap does not become a drag
        if(maxdisplacement > TOUCH_DEAD_BAND)
        {
          //Signal a movement of touch
          touchstate = TOUCH_STATE_HAVE_DISPLACEMENT;

          //Go and handle it
          break;
        }
      }
    }

    //Check if no big displacement detected. If there was, touchstate will not be zero
    if(touchstate == 0)
    {
      //A short touch is a tap on the plot's bands. Never on a handle (pointers, slider, trigger level); on a trace or a
      //cursor only when the finger did not move (fw 0.2.64: a brisk stroke on a trace changed the time base)
      if(((timer0_get_ticks() - previoustimerticks) < TOUCH_TAP_MS) && !handle && (!pick || (moved <= TOUCH_MOVED)))
      {
        scope_adjust_timebase_and_voltdiv();
      }

      //Done for now.
      return;
    }

    //A drag moves what was picked at touch-down; X-Y moves both channels; nothing picked pans the trigger point
    if(scopesettings.xymodedisplay)
      touchstate = TOUCH_STATE_X_Y_MODE;
    else if(pick)
      touchstate = pick;
    else
      touchstate = TOUCH_STATE_MOVE_TRIGGER_POINT;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//**********************************************************************************************************************************
//----------------------------------------------------------------------------------------------------------------------------------

void handle_main_menu_touch(void)
{
  int   row;                            //fw 0.2.51: main menu row under the touch (ui_menu.h), -1 outside
  int   drag = 0;                       //fw 0.2.63: the brightness slider a touch started on (1 screen, 2 grid), 0 none
  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu and its fold-outs as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      close_open_menus(1);
      return;
    }

    //fw 0.2.63 (owner): a drag that starts on a brightness slider stays with it until the finger lifts. Before, every
    //touch sample was hit-tested afresh, so a finger drifting off the slider's box mid-drag counted as a tap outside
    //(the fold-out closed) or on a main-menu or fold-out row
    if(!havetouch)
      drag = 0;
    else if(drag == 1)
    {
      if(scope_move_slider(395, 46, &scopesettings.screenbrightness))
      {
        scope_system_settings_screen_brightness_value();
        scope_set_brightness(scopesettings.screenbrightness, 1);
      }
      continue;
    }
    else if(drag == 2)
    {
      if(scope_move_slider(395, 104, &scopesettings.gridbrightness))
        scope_system_settings_grid_brightness_value();
      continue;
    }

    //Check if there is touch
    if(havetouch)
    {
      //Check if touch within the menu field: the rows' hit areas are contiguous, one 52 px pitch each (ui_menu.h)
      if((row = ui_mm_row_at(xtouch, ytouch)) >= 0)
      {
        //Check if on system settings
        if(row == UI_MM_SETTINGS)
        {
          //Check if already open
          if(systemsettingsmenuopen == 0)
          {
            //Set the button active
            scope_main_menu_item(UI_MM_SETTINGS, 1);
            //Set the button inactive
            //scope_main_menu_diagnostic_view(0);
            

            //Wait until touch is released
            tp_i2c_wait_for_touch_release();

            //Save the screen under the menu
            display_set_destination_buffer(displaybuffer2);
            display_copy_rect_from_screen(SYSTEM_MENU_XPOS, SYSTEM_MENU_YPOS, SYSTEM_MENU_WIDTH, SYSTEM_MENU_HEIGHT);//353

            //Show the system settings menu
            scope_open_system_settings_menu();

            //Signal the system settings menu is opened
            systemsettingsmenuopen = 1;
          }
        }
        //Check if on picture view
        else if(row == UI_MM_PICTURES)
        {
          //Close any other open menu
          close_open_menus(1);

          //Set the button active
          scope_main_menu_item(UI_MM_PICTURES, 1);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          //Signal viewing of pictures
          viewtype = VIEW_TYPE_PICTURE;

          //Switch to picture view screen and stay there until return is pressed
          scope_setup_view_screen();

          //Need to exit the loop here
          return;
        }
        //Check if on waveform view
        else if(row == UI_MM_WAVEFORMS)
        {
          //Close any other open menu
          close_open_menus(1);

          //Set the button active
          scope_main_menu_item(UI_MM_WAVEFORMS, 1);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          //Signal viewing of pictures
          viewtype = VIEW_TYPE_WAVEFORM;

          //Switch to waveform view screen and stay there until return is pressed
          scope_setup_view_screen();

          //Need to exit the loop here
          return;
        }
        //Check if on diagnostic_screen
        else if(row == UI_MM_DIAGNOSTICS)
        {
          //Close any other open menu
          close_open_menus(1);
            
          //Signal the system settings menu is closed
          //systemsettingsmenuopen = 0;
          
          //Set the button active
          scope_main_menu_item(UI_MM_DIAGNOSTICS, 1);
          //Set the button inactive
          //scope_main_menu_system_settings(0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
 
          //Restore the main screen before entering the diagnostics
          display_set_source_buffer(displaybuffer1);//2 0.26p
          display_copy_rect_to_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);//149 233

          //fw 0.2.59: Diagnostics is a display mode of the main loop (ui_diag_view.c), not a modal screen: it keeps the
          //scope screen to return to and takes over the main loop's passes until Back
          ui_diag_open();

          //Need to exit the loop here
          return;
          
        }
        //Check if on USB connection
        else if(row == UI_MM_USB)
        {
          //Close any other open menu
          close_open_menus(1);

          //Set the button active
          scope_main_menu_item(UI_MM_USB, 1);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          //Restore the main screen before entering the USB screen
          display_set_source_buffer(displaybuffer1);//2 0.026p
          //display_copy_rect_to_screen(2, 46, 149, 292);//149 233
          display_copy_rect_to_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);//149 233


          //And make a copy of it to return to
          display_set_destination_buffer(displaybuffer2);
          display_copy_rect_from_screen(0, 0, 800, 480);

          //Show and handle the usb connection here
          scope_setup_usb_screen();

          //Copy back the original screen to continue were left of
          display_set_source_buffer(displaybuffer2);
          display_copy_rect_to_screen(0, 0, 800, 480);

          //Need to exit the loop here
          return;
        }
        //fw 0.2.68: Frequency response where the bitstream lacks the filter and the DDS (or in the waveform viewer): grey;
        //fw 0.2.69: a tap says why at the top middle of the plot (the menu stays open)
        else if((row == UI_MM_BODE) && !pc_glue_bode_available())
        {
          const char *t = bode_view_unavailable_text();

          display_set_screen_buffer((uint16 *)maindisplaybuffer);
          display_set_font(&font_2);
          display_set_fg_color(ORANGE_COLOR);
          display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
          tp_i2c_wait_for_touch_release();
        }
        //Scope, Spectrum analyser or Bode plot: the mode rows (pc_glue_main_menu_mode carries out ui_mm_choose)
        else
        {
          //Close any other open menu
          close_open_menus(1);

          //Set the button active
          scope_main_menu_item(row, 1);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          PC_MAIN_MENU_MODE(row);

          //Need to exit the loop here
          return;
        }
      }
      
      //**--------------------------------------------------------
      
      //Check on system settings menu opened and being touched (fw 0.2.55: the rows of ui_settings.h, contiguous 59 px)
      else if(systemsettingsmenuopen && ((row = ui_fo_row_at(pc_glue_ui_has(PC_FEAT_RTC), xtouch, ytouch)) >= 0))
      {
        //Check if on screen brightness
        if(row == UI_FO_SCREEN)
        {
          //Check if already open
          if(screenbrightnessopen == 0)
          {
            //Close any of the sub menus if open
            close_open_menus(0);

            //Set this item active
            scope_system_settings_screen_brightness_item(1);

            //Wait until touch is released
            tp_i2c_wait_for_touch_release();

            //Show the screen brightness slider
            scope_open_slider(395, 46, scopesettings.screenbrightness);

            //Signal the screen brightness slider is opened
            screenbrightnessopen = 1;
          }
        }
        //Check if on grid brightness
        else if(row == UI_FO_GRID)
        {
          //Check if already open
          if(gridbrightnessopen == 0)
          {
            //Close any of the sub menus if open
            close_open_menus(0);

            //Set this item active
            scope_system_settings_grid_brightness_item(1);

            //Wait until touch is released
            tp_i2c_wait_for_touch_release();

            //Show the screen brightness slider
            scope_open_slider(395, 104, scopesettings.gridbrightness);

            //Signal the screen brightness slider is opened
            gridbrightnessopen = 1;
          }
        }
        //Time and date: 1.00o5's editor beside the fold-out
        else if(row == UI_FO_TIME)
        {
          //Check if already open
          if(RTCsettingsopen == 0)
          {
            //Close any of the sub menus if open
            close_open_menus(0);

            //Set this item active
            scope_system_settings_RTC_settings_item(1);

            //Wait until touch is released
            tp_i2c_wait_for_touch_release();

            //Save the screen under the menu
            display_set_destination_buffer(displaybuffer3);
            display_copy_rect_from_screen(RTC_MENU_XPOS, RTC_MENU_YPOS, RTC_MENU_WIDTH, RTC_MENU_HEIGHT);

            //Show the RTC settings menu
            scope_open_RTC_settings_menu();

            //Signal the menu RTC_settings is opened
            RTCsettingsopen = 1;
          }
        }
        //Other settings: the Settings screen (ui_settings_ui.c), modal until Back, then back to the scope
        else
        {
          //Close any of the sub menus if open
          close_open_menus(0);

          //Set this item active
          scope_system_settings_other_settings_item(1);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          //Close the fold-out and put back what was under the main menu, so the screen keeps the clean scope
          close_open_menus(1);
          display_set_source_buffer(displaybuffer1);
          display_copy_rect_to_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);

          ui_settings_run();

          //Need to exit the loop here
          return;
        }
      }
        //------------------------------------------------------------------------------------------------
      else if(RTCsettingsopen && (xtouch >= 395) && (xtouch <= 595) && (ytouch >= 106) && (ytouch <= 470))
      {
        //Check if on RTC is pushed
        if((ytouch >= 405) && (ytouch <= 470))
        {
          //Toggle the ON/OFF RTC state
          onoffRTC ^= 1;
              
          if (onoffRTC)
          { //Read time for RTC
            get_fattime();    
            //Show the state
            scope_system_settings_hour_item_value(); 
            scope_system_settings_minute_item_value(); 
            scope_system_settings_day_item_value(); 
            scope_system_settings_month_item_value(); 
            scope_system_settings_year_item_value(); 
          } else       
              {//Clear TIME on display
                display_set_fg_color(BLACK_COLOR);
                //display_fill_rect(647, 5, 50, 13);
                display_fill_rect(TIME_TEXT_XPOS, TIME_TEXT_YPOS, TIME_TEXT_WIDTH, TIME_TEXT_HEIGHT);
              }

          //Show the state
          scope_display_slide_button(530, 419, onoffRTC, GREEN_COLOR); 
        }
        
        //Check if year (<) being touched
        if((xtouch >= 420) && (xtouch <= 450) && (ytouch >= 354) && (ytouch <= 378))
        {       
          //Decrement year and sent to RTC
          yearDown();

          //Show the state
          scope_system_settings_year_item_value(); 
        }
        
        //Check if year (>) being touched
        if((xtouch >= 540) && (xtouch <= 572) && (ytouch >= 354) && (ytouch <= 378))
        {
          //Increment year and sent to RTC
          yearUp();
                    
          //Show the state
          scope_system_settings_year_item_value();
        }
        
        //Check if month (<) being touched
        if((xtouch >= 420) && (xtouch <= 450) && (ytouch >= 295) && (ytouch <= 319))
        {
          //Decrement month and sent to RTC
          monthDown();

          //Show the state
          scope_system_settings_month_item_value(); 
        }
        
        //Check if month (>) being touched
        if((xtouch >= 540) && (xtouch <= 572) && (ytouch >= 295) && (ytouch <= 319))
        {
          //Increment month and sent to RTC
          monthUp();
                    
          //Show the state
          scope_system_settings_month_item_value();
        }
        //Check if day (<) being touched
        if((xtouch >= 420) && (xtouch <= 450) && (ytouch >= 237) && (ytouch <= 261))
        {
          //Decrement day and sent to RTC
          dayDown();

          //Show the state
          scope_system_settings_day_item_value(); 
        }
        
        //Check if day (>) being touched
        if((xtouch >= 540) && (xtouch <= 572) && (ytouch >= 237) && (ytouch <= 261))
        {
          //Increment day and sent to RTC
          dayUp();
                    
          //Show the state
          scope_system_settings_day_item_value();
        }
        
        //Check if minutes (<) being touched
        if((xtouch >= 420) && (xtouch <= 450) && (ytouch >= 178) && (ytouch <= 202))
        {
          //Decrement minutes and sent to RTC
          minuteDown();

          //Show the state
          scope_system_settings_minute_item_value(); 
        }
        
        //Check if minutes (>) being touched
        if((xtouch >= 540) && (xtouch <= 572) && (ytouch >= 178) && (ytouch <= 202))
        {
          //Increment minutes and sent to RTC
          minuteUp();
                    
          //Show the state
          scope_system_settings_minute_item_value();
        }
        
        //Check if hours (<) being touched
        if((xtouch >= 420) && (xtouch <= 450) && (ytouch >= 119) && (ytouch <= 143))
        {
          //Decrement hours and sent to RTC
          hourDown();

          //Show the state
          scope_system_settings_hour_item_value(); 
        }
        
        //Check if hours (>) being touched
        if((xtouch >= 540) && (xtouch <= 572) && (ytouch >= 119) && (ytouch <= 143))
        {
          //Increment hours and sent to RTC
          hourUp();
                    
          //Show the state
          scope_system_settings_hour_item_value();
        }
        
        //Wait until touch is released
        tp_i2c_wait_for_touch_release();
      }
        
       //*********************************************************************************************************
      //--------------------------------------------------------------------------------------------------------
      //Check on screen brightness slider opened and being touched
      else if(screenbrightnessopen && (xtouch >= 395) && (xtouch <= 726) && (ytouch >= 46) && (ytouch <= 104))
      {
        drag = 1;

        //Move the slider to a new position and check if there was a change in position
        if(scope_move_slider(395, 46, &scopesettings.screenbrightness))
        {
          //Update the setting in the system settings menu
          scope_system_settings_screen_brightness_value();

          //Update the actual screen brightness and the top bar sun (fw 0.2.68: one setting)
          scope_set_brightness(scopesettings.screenbrightness, 1);
        }
      }
      //Check on grid brightness slider opened and being touched
      else if(gridbrightnessopen && (xtouch >= 395) && (xtouch <= 726) && (ytouch >= 105) && (ytouch <= 163))
      {
        drag = 2;

        //Move the slider to a new position and check if there was a change in position
        if(scope_move_slider(395, 104, &scopesettings.gridbrightness))
        {
          //Update the setting in the system settings menu
          scope_system_settings_grid_brightness_value();

          //Update the actual grid brightness if needed. There is no direct update of the background!
        }
      }
      else
      {
        //Close any of the menus if open
        close_open_menus(1);

        //Touch outside the menu so wait until touch is released and then quit
        tp_i2c_wait_for_touch_release();
        
        return;
      }
    } 
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_channel_menu_touch(PCHANNELSETTINGS settings)
{
  uint32 i,j,x,y;
  
  //If ref function is enable automatic open ref menu
  //if (settings->ref1||settings->ref2||settings->ref3||settings->ref4)  
  //if((scopesettings.ch_ref1.enable)||(scopesettings.ch_ref2.enable)||(scopesettings.ch_ref3.enable)||(scopesettings.ch_ref4.enable))
  if(((settings->color == CHANNEL1_COLOR)&&((scopesettings.ref1)||(scopesettings.ref2)||(scopesettings.ref3)||(scopesettings.ref4)))||
    ((settings->color == CHANNEL2_COLOR)&&((scopesettings.ref5)||(scopesettings.ref6)||(scopesettings.ref7)||(scopesettings.ref8))))
    { 
      refmenuopen = 1; 
      //Set this item active
      scope_channel_ref_menu_item(settings, 1);
      //Show the screen ref menu
      scope_channel_open_ref_menu(settings);
    } 
    //If math function is enable automatic open math menu
    else if (mathmode) 
    { 
      mathmenuopen = 1; 
      //Set this item active
      scope_channel_math_menu_item(settings, 1);
      //Show the screen math menu
      scope_channel_open_math_menu(settings);
    } 
    
  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu and its sub menus as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      close_open_channel_menus(settings, 1);
      return;
    }

    //Check if there is touch
    if(havetouch)
    {
 /*          
  xtouch_tmp=xtouch-settings->menuxpos;
  ytouch_tmp=ytouch-CH_MENU_YPOS;   
  scope_test_TP();
   */     
      //Check if touch within the menu field
      if((xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + CH_MENU_WIDTH) && (ytouch >= CH_MENU_YPOS) && (ytouch <= CH_MENU_YPOS + CH_MENU_HEIGHT))
      {
        //Check on channel enable or disable
        if((ytouch >= CH_MENU_YPOS + 15) && (ytouch <= CH_MENU_YPOS + 50))  //15 45 
        {
          //Check on enable
          if((xtouch >= settings->menuxpos + 80) && (xtouch <= settings->menuxpos + 155))//78 162
          {
            //Toggle the channel enable
            settings->enable ^= 1;

            //Update the trigger channel selection in the FPGA as needed
            fpga_swap_trigger_channel();

            //Set the trigger vertical position to match the trigger channel position
            scope_calculate_trigger_vertical_position();
            
            //Display this
            scope_channel_enable_select(settings);
            scope_channel_settings(settings, 0);
            scope_trigger_settings(0);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }
        } 
        //Check on invert channel
        else if((ytouch >= CH_MENU_YPOS + 69) && (ytouch <= CH_MENU_YPOS + 109))// && (scopesettings.waveviewmode == 0)) //140 170
        {
          //Check on invert channel
          if((xtouch >= settings->menuxpos + 80) && (xtouch <= settings->menuxpos + 155))//78 162
          {
            //Toggle the channel enable
            settings->invert ^= 1;
            
            //Display this
            //scope_channel_enable_select(settings);
            scope_channel_invert_select(settings);
            scope_channel_settings(settings, 0);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }
        }

        //Check on coupling DC or AD, and in normal view mode
        else if((ytouch >= CH_MENU_YPOS + 124) && (ytouch <= CH_MENU_YPOS + 164))// && (scopesettings.waveviewmode == 0)) //140 170
        {
          //Check on DC coupling
          if((xtouch >= settings->menuxpos + 78) && (xtouch <= settings->menuxpos + 110) && (!scopesettings.waveviewmode))
          {
            //Set the channel to DC coupling
            settings->coupling = 0;

            //Update the FPGA
            fpga_set_channel_coupling(settings);
            fpga_set_channel_offset(settings);

            //Display this
            scope_channel_coupling_select(settings);
            scope_channel_settings(settings, 0);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }
          //Check on AC coupling
          else if((xtouch >= settings->menuxpos + 130) && (xtouch <= settings->menuxpos + 162) && (!scopesettings.waveviewmode))
          {
            //Set the channel to AC coupling
            settings->coupling = 1;
            
            //Set the channel to dcoffset = 0
            settings->dcoffset = 0;

            //Update the FPGA
            fpga_set_channel_coupling(settings);
            fpga_set_channel_offset(settings);

            //Display this
            scope_channel_coupling_select(settings);
            scope_channel_settings(settings, 0);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }
        //}
        
         /*          
        //Check on 50%
         if((xtouch >= settings->menuxpos + 270 ) && (xtouch <= settings->menuxpos + 320 ))//6232-282//4
            {
            //Check if channel is 1 (0) or 2 (1) and trigger channel 1 or channel 2 and long mode with trigger or short mode
            //if(((scopesettings.triggerchannel == 0) && (settings->color == YELLOW_COLOR )&&((!scopesettings.long_mode)||(scopesettings.triggermode)))
             //  ||((scopesettings.triggerchannel == 1) && (settings->color == BLUE_COLOR )&&((!scopesettings.long_mode)||(scopesettings.triggermode))))
              if((((!scopesettings.long_mode)||(scopesettings.triggermode))))
                {
              
                  if(settings->color == CHANNEL1_COLOR) scopesettings.triggerchannel = 0; else scopesettings.triggerchannel = 1;


                  //Use the channel center value as trigger level
                  scope_do_50_percent_trigger_setup();

                  //flag - button 50% is touched
                  trigger50 = 1;

                  //Display this
                  scope_channel_sensitivity_select(settings);

                  //Display this on trigger button
                  scope_trigger_settings(0);

                  //Wait until touch is released
                  tp_i2c_wait_for_touch_release();
                }
            }
          */
        }//end else if coupling DC or AD, and in normal view mode
           
          //------------------
        
        //Check on V_A mode setting, and in normal view mode
        else if((ytouch >= CH_MENU_YPOS + 184) && (ytouch <= CH_MENU_YPOS + 219))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //Check on V
          if((xtouch >= settings->menuxpos + 75) && (xtouch <= settings->menuxpos + 105) && (!scopesettings.waveviewmode))
          {
            //Switch to V
            settings->V_A = 0;

            //Display this
            //scope_channel_probe_magnification_select(settings);
            scope_channel_VA_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }
          //Check on A
          else if((xtouch >= settings->menuxpos + 130) && (xtouch <= settings->menuxpos + 150) && (!scopesettings.waveviewmode))
          {
            //Switch to A
            settings->V_A = 1;

            //Display this
            //scope_channel_probe_magnification_select(settings);
            scope_channel_VA_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
          }       
        }
        //Check on probe magnification setting, and in normal view mode
        else if((ytouch >= CH_MENU_YPOS + 300) && (ytouch <= CH_MENU_YPOS + 335))// && (scopesettings.waveviewmode == 0))//260-290
        {
          //Check on 0.5x
          if((xtouch >= settings->menuxpos + 70) && (xtouch <= settings->menuxpos + 90) && (!scopesettings.waveviewmode))
          {
            //Enable the channel
            settings->magnification = 0;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 1x
          else if((xtouch >= settings->menuxpos + 100) && (xtouch <= settings->menuxpos + 120) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 1;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 10x
          else if((xtouch >= settings->menuxpos + 130) && (xtouch <= settings->menuxpos + 150) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 2;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 20x
          else if((xtouch >= settings->menuxpos + 160) && (xtouch <= settings->menuxpos + 180) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 3;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 50x
          else if((xtouch >= settings->menuxpos + 190) && (xtouch <= settings->menuxpos + 210) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 4;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 100x
          else if((xtouch >= settings->menuxpos + 220) && (xtouch <= settings->menuxpos + 240) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 5;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          //Check on 1000x
          else if((xtouch >= settings->menuxpos + 250) && (xtouch <= settings->menuxpos + 270) && (!scopesettings.waveviewmode))
          {
            //Disable the channel
            settings->magnification = 6;

            //Display this
            scope_channel_probe_magnification_select(settings);
            scope_channel_settings(settings, 0);
            scope_channel_sensitivity_select(settings);
          }
          
         //Check on 50%
          else if((xtouch >= settings->menuxpos + 280 ) && (xtouch <= settings->menuxpos + 330 ))//270-320//6232-282//4
          {
          //Check if channel is 1 (0) or 2 (1) and trigger channel 1 or channel 2 and long mode with trigger or short mode
          //if(((scopesettings.triggerchannel == 0) && (settings->color == YELLOW_COLOR )&&((!scopesettings.long_mode)||(scopesettings.triggermode)))
          //  ||((scopesettings.triggerchannel == 1) && (settings->color == BLUE_COLOR )&&((!scopesettings.long_mode)||(scopesettings.triggermode))))
            if((((!scopesettings.long_mode)||(scopesettings.triggermode))))
              {    
                //switch (select) trigger according to current channel
                if(settings->color == CHANNEL1_COLOR) scopesettings.triggerchannel = 0; else scopesettings.triggerchannel = 1;

                //Use the channel center value as trigger level
                scope_do_50_percent_trigger_setup();

                //flag - button 50% is touched
                trigger50 = 1;

                //Display this
                scope_channel_sensitivity_select(settings);

                //Display this on trigger button
                scope_trigger_settings(0);

                //Wait until touch is released
                //tp_i2c_wait_for_touch_release();
              }
          }
          
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
        
        }
              //************************************************************************************************
          
        //Check on sensitivity set
        if((ytouch >= CH_MENU_YPOS) && (ytouch <= CH_MENU_YPOS + 280) && (xtouch >= settings->menuxpos + 190) && (xtouch <= settings->menuxpos + 330))//skontrolovat 190 az 330
        {
            //------------------ acquisition_speed_texts start ------------------
            //Check on the different bounding boxes for the separate settings
            //Clear the boxes for the not selected items
            for(i=0;i<7;i++)//12 //7
            {
                if (i<6) {x = 178;j=i;} else {x = 261;j=i-6;}//178 260 -6//<4 177*258
                y = (j * 46) + 6;    //3 *56+8
    
                //Calculate the coordinates for this setting
                x = x + settings->menuxpos;
                y = y + CH_MENU_YPOS;

                //Check if touch within this bounding box
                if((xtouch >= x) && (xtouch <= x + 76) && (ytouch >= y) && (ytouch <= y + 40))
                {                  
                  //Set voltperdiv 
                  settings->displayvoltperdiv = 6-i;

                  //Set voltperdiv in FPGA Only update the FPGA in run mode
                  //For waveform view mode the stop state is forced and can't be changed
                  if(scopesettings.runstate) match_volt_per_div_settings(settings);//ok

                  //fw 0.2.94: in Frequency response a CH2 V/div by hand sets CH2 range Constant
                  if(settings == &scopesettings.channel2)
                    bode_view_ch2_vdiv_by_hand();

                  //Display this
                  scope_channel_settings(settings, 0);
                  scope_channel_sensitivity_select(settings);
                  break;
                }
            }
            //Wait until touch is released
            tp_i2c_wait_for_touch_release();
        }
                
        //************************************************************************************************
     
        //Check on 
        if(channelmenuopen && (xtouch >= settings->menuxpos + 350) && (xtouch <= settings->menuxpos + CH_MENU_WIDTH) && (!scopesettings.waveviewmode))
        { 
          //Check on REF menu button, and in normal view mode
          if((ytouch >= CH_MENU_YPOS + 6) && (ytouch <= CH_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
          {
            //Check if already open
            if(refmenuopen == 0)
            {
              //Close any of the sub menus if open
              close_open_channel_menus(settings, 0);
              
              //Set this item active
              scope_channel_ref_menu_item(settings, 1);
              
              //Wait until touch is released
              tp_i2c_wait_for_touch_release();
              
              //Save the screen under the menu
              display_set_destination_buffer(displaybuffer2);
              display_copy_rect_from_screen(settings->menuxpos + CH_REF_MENU_XPOS, CH_REF_MENU_YPOS, CH_REF_MENU_WIDTH, CH_REF_MENU_HEIGHT);
            
              //Show the screen ref menu
              scope_channel_open_ref_menu(settings);
              
              //Signal the REF menu is opened
              refmenuopen = 1;         
            }
          }
          //Check on MATH menu button, and in normal view mode
          else if((ytouch >= CH_MENU_YPOS + 52) && (ytouch <= CH_MENU_YPOS + 92))// && (scopesettings.waveviewmode == 0)) //200 230
          {
            //Check if already open
            if(mathmenuopen == 0)
            {
              //Close any of the sub menus if open
              close_open_channel_menus(settings, 0);
              
              //Set this item active
              scope_channel_math_menu_item(settings, 1);
              
              //Wait until touch is released
              tp_i2c_wait_for_touch_release();
              
              //Save the screen under the menu
              display_set_destination_buffer(displaybuffer2);
              display_copy_rect_from_screen(settings->menuxpos + CH_MATH_MENU_XPOS, CH_MATH_MENU_YPOS, CH_MATH_MENU_WIDTH, CH_MATH_MENU_HEIGHT);
            
              //Show the screen math menu
              scope_channel_open_math_menu(settings);
              
              //Signal the REF menu is opened
              mathmenuopen = 1;    
            }   
          }
        //Wait until touch is released
        tp_i2c_wait_for_touch_release();
          //---------------------------
        } //else close_open_channel_menus(settings, 0); //Close any of the sub menus if open (ref menu or math menu)
      } //tu konci X a Y plocha menu kanalov uvedene v prvom IF
         //************************************************************************************************
       
      //Check on  menu
      
      else if(refmenuopen && (xtouch >= CH_REF_MENU_XPOS) && (xtouch <= (CH_REF_MENU_XPOS + 70))
          &&(ytouch >= CH_REF_MENU_YPOS) && (ytouch <= CH_REF_MENU_YPOS + CH_REF_MENU_HEIGHT))
          //if(mathmenuopen && (xtouch >= settings->menuxpos+380) && (xtouch <= (settings->menuxpos + 500)))// && (!scopesettings.waveviewmode))
        //if(mathmenuopen && (xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + 550))// && (!scopesettings.waveviewmode))
      { 
        //Check on REF1 button, and in normal view mode
        if((ytouch >= CH_REF_MENU_YPOS + 6) && (ytouch <= CH_REF_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //Toggle the ref1

          //scopesettings.ch_ref1.enable ^= 1;
          if(settings->color == CHANNEL1_COLOR)  scopesettings.ref1 ^= 1;
            else scopesettings.ref5 ^= 1;
                
          //Display this
          scope_channel_ref_select(settings);
          //scope_channel_ref_select(settings, &scopesettings.ch1_ref1);//&scopesettings.channel1
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
          //Check on REF2 button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 52) && (ytouch <= CH_REF_MENU_YPOS + 92))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //Toggle the ref2
          if(settings->color == CHANNEL1_COLOR)  scopesettings.ref2 ^= 1;
            else scopesettings.ref6 ^= 1;
                          
          //Display this
          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on REF3 button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 98) && (ytouch <= CH_REF_MENU_YPOS + 138))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //Toggle the ref3
          if(settings->color == CHANNEL1_COLOR)  scopesettings.ref3 ^= 1;
            else scopesettings.ref7 ^= 1;
       
          //Display this
          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on REF4 button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 144) && (ytouch <= CH_REF_MENU_YPOS + 184))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //Toggle the ref4
          if(settings->color == CHANNEL1_COLOR)  scopesettings.ref4 ^= 1;
            else scopesettings.ref8 ^= 1;
                         
          //Display this
          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on OFF ALL button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 190) && (ytouch <= CH_REF_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //set All REF to OFF
          if(settings->color == CHANNEL1_COLOR) {scopesettings.ref1 = 0; scopesettings.ref2 = 0; scopesettings.ref3 = 0; scopesettings.ref4 = 0;}
          else { scopesettings.ref5 = 0; scopesettings.ref6 = 0; scopesettings.ref7 = 0; scopesettings.ref8 = 0; }

          //Display this
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_COPYtoREF1_item(settings, 0);
          scope_channel_COPYtoREF2_item(settings, 0);
          scope_channel_COPYtoREF3_item(settings, 0);
          scope_channel_COPYtoREF4_item(settings, 0);
          
          scope_channel_LOADtoREF1_item(settings, 0);
          scope_channel_LOADtoREF2_item(settings, 0);
          scope_channel_LOADtoREF3_item(settings, 0);
          scope_channel_LOADtoREF4_item(settings, 0);

          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
          
          //---------------------------
      }
  
      else if(refmenuopen && (xtouch >= CH_REF_MENU_XPOS + 70) && (xtouch <= (CH_REF_MENU_XPOS + 140))
          &&(ytouch >= CH_REF_MENU_YPOS) && (ytouch <= CH_REF_MENU_YPOS + CH_REF_MENU_HEIGHT))
          //if(mathmenuopen && (xtouch >= settings->menuxpos+380) && (xtouch <= (settings->menuxpos + 500)))// && (!scopesettings.waveviewmode))
        //if(mathmenuopen && (xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + 550))// && (!scopesettings.waveviewmode))
      { 
        //Check on CHx copy to REF1, and in normal view mode
        if((ytouch >= CH_REF_MENU_YPOS + 6) && (ytouch <= CH_REF_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //For now just copy the settings to the given struct
          if(settings->color == CHANNEL1_COLOR) { copy_ch_to_ref(&scopesettings.ch_ref1, settings, ref1_tracebuffer); scopesettings.ref1 = 1; }
            else { copy_ch_to_ref(&scopesettings.ch_ref5, settings, ref5_tracebuffer); scopesettings.ref5 = 1; }
                
          //Display this
          scope_channel_COPYtoREF1_item(settings, 1);
          
          //Display this
          scope_channel_ref_select(settings);
                   
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_LOADtoREF1_item(settings, 0);
          scope_channel_COPYtoREF1_item(settings, 2);
        }
          //Check on CHx copy to REF2, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 52) && (ytouch <= CH_REF_MENU_YPOS + 92))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //For now just copy the settings to the given struct
          if(settings->color == CHANNEL1_COLOR) { copy_ch_to_ref(&scopesettings.ch_ref2, settings, ref2_tracebuffer); scopesettings.ref2 = 1; }
            else { copy_ch_to_ref(&scopesettings.ch_ref6, settings, ref6_tracebuffer); scopesettings.ref6 = 1; }
                
          //Display this
          scope_channel_COPYtoREF2_item(settings, 1);
          
          //Display this
          scope_channel_ref_select(settings);
         
          //Wait until touch is released
          tp_i2c_wait_for_touch_release(); 
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_LOADtoREF2_item(settings, 0);
          scope_channel_COPYtoREF2_item(settings, 2);
        }
        //Check on CHx copy to REF3, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 98) && (ytouch <= CH_REF_MENU_YPOS + 138))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //For now just copy the settings to the given struct
          if(settings->color == CHANNEL1_COLOR) { copy_ch_to_ref(&scopesettings.ch_ref3, settings, ref3_tracebuffer); scopesettings.ref3 = 1; }
            else { copy_ch_to_ref(&scopesettings.ch_ref7, settings, ref7_tracebuffer); scopesettings.ref7 = 1; }
                
          //Display this
          scope_channel_COPYtoREF3_item(settings, 1);
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_LOADtoREF3_item(settings, 0);
          scope_channel_COPYtoREF3_item(settings, 2);
        }
        //Check on CHx copy to REF4, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 144) && (ytouch <= CH_REF_MENU_YPOS + 184))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //For now just copy the settings to the given struct
          if(settings->color == CHANNEL1_COLOR) { copy_ch_to_ref(&scopesettings.ch_ref4, settings, ref4_tracebuffer); scopesettings.ref4 = 1; }
            else { copy_ch_to_ref(&scopesettings.ch_ref8, settings, ref8_tracebuffer); scopesettings.ref8 = 1; }
                         
          //Display this
          scope_channel_COPYtoREF4_item(settings, 1);
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_LOADtoREF4_item(settings, 0);
          scope_channel_COPYtoREF4_item(settings, 2);
          
        }
        //Check on CLEAR ALL ref button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 190) && (ytouch <= CH_REF_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          if(settings->color == CHANNEL1_COLOR) 
          {
            //For now clear buffers REF1 to REF4
            //memset(ref1_tracebuffer, 128, sizeof(ref1_tracebuffer));
            memset(scopesettings.ch_ref1.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref2.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref3.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref4.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            
            scopesettings.ch_ref1.traceposition = 225;
            scopesettings.ch_ref2.traceposition = 265;
            scopesettings.ch_ref3.traceposition = 305;
            scopesettings.ch_ref4.traceposition = 345;
            
            scopesettings.ref1 = 0; scopesettings.ref2 = 0; scopesettings.ref3 = 0; scopesettings.ref4 = 0; 
          }
          else
          {
            //For now clear buffers REF5 to REF8
            memset(scopesettings.ch_ref5.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref6.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref7.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE);
            memset(scopesettings.ch_ref8.tracebuffer, 128, MAX_SAMPLE_BUFFER_SIZE); 
            
            scopesettings.ch_ref5.traceposition =  25;//145;//25;
            scopesettings.ch_ref6.traceposition =  65;//105;//50;
            scopesettings.ch_ref7.traceposition = 105;//65;//100;
            scopesettings.ch_ref8.traceposition = 145;//25;//150;

            scopesettings.ref5 = 0; scopesettings.ref6 = 0; scopesettings.ref7 = 0; scopesettings.ref8 = 0;       
          }
                
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 1);
          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
                    
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF1_item(settings, 0);
          scope_channel_COPYtoREF2_item(settings, 0);
          scope_channel_COPYtoREF3_item(settings, 0);
          scope_channel_COPYtoREF4_item(settings, 0);
          
          scope_channel_LOADtoREF1_item(settings, 0);
          scope_channel_LOADtoREF2_item(settings, 0);
          scope_channel_LOADtoREF3_item(settings, 0);
          scope_channel_LOADtoREF4_item(settings, 0);
          
          scope_channel_CLEAR_REF_ALL_item(settings, 2);
        } 
          
          //---------------------------
      }
      
      else if(refmenuopen && (xtouch >= CH_REF_MENU_XPOS + 140) && (xtouch <= (CH_REF_MENU_XPOS + 210))
          &&(ytouch >= CH_REF_MENU_YPOS) && (ytouch <= CH_REF_MENU_YPOS + CH_REF_MENU_HEIGHT))
          //if(mathmenuopen && (xtouch >= settings->menuxpos+380) && (xtouch <= (settings->menuxpos + 500)))// && (!scopesettings.waveviewmode))
        //if(mathmenuopen && (xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + 550))// && (!scopesettings.waveviewmode))
      { 
        //Check on CHx copy to REF1, and in normal view mode
        if((ytouch >= CH_REF_MENU_YPOS + 6) && (ytouch <= CH_REF_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {                
          //Display this
          scope_channel_LOADtoREF1_item(settings, 1);
          
          //Load data from file 1.ref to buffer ref1
          if(settings->color == CHANNEL1_COLOR) { scope_load_REFx_item_file(1); scopesettings.ref1 = 1; }
            else { scope_load_REFx_item_file(5); scopesettings.ref5 = 1; }
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF1_item(settings, 0);
          scope_channel_LOADtoREF1_item(settings, 2);
        }
          //Check on CHx copy to REF2, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 52) && (ytouch <= CH_REF_MENU_YPOS + 92))// && (scopesettings.waveviewmode == 0)) //200 230
        {            
          //Display this
          scope_channel_LOADtoREF2_item(settings, 1);
          
          //Load data from file 2.ref to buffer ref2
          if(settings->color == CHANNEL1_COLOR) { scope_load_REFx_item_file(2); scopesettings.ref2 = 1; }
            else { scope_load_REFx_item_file(6); scopesettings.ref6 = 1; }
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release(); 
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF2_item(settings, 0);
          scope_channel_LOADtoREF2_item(settings, 2);
        }
        //Check on CHx copy to REF3, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 98) && (ytouch <= CH_REF_MENU_YPOS + 138))// && (scopesettings.waveviewmode == 0)) //200 230
        {                
          //Display this
          scope_channel_LOADtoREF3_item(settings, 1);
          
          //Load data from file 3.ref to buffer ref3
          if(settings->color == CHANNEL1_COLOR) { scope_load_REFx_item_file(3); scopesettings.ref3 = 1; }
            else { scope_load_REFx_item_file(7); scopesettings.ref7 = 1; }
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF3_item(settings, 0);
          scope_channel_LOADtoREF3_item(settings, 2);
        }
        //Check on CHx copy to REF4, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 144) && (ytouch <= CH_REF_MENU_YPOS + 184))// && (scopesettings.waveviewmode == 0)) //200 230
        {           
          //Display this
          scope_channel_LOADtoREF4_item(settings, 1);
          //scope_channel_ref_select(settings);
          
          //Load data from file 4.ref to buffer ref4
          if(settings->color == CHANNEL1_COLOR) { scope_load_REFx_item_file(4); scopesettings.ref4 = 1; }
            else { scope_load_REFx_item_file(8); scopesettings.ref8 = 1; }
          
          //Display this
          scope_channel_ref_select(settings);
          scope_channel_SAVE_REF_ALL_item(settings, 0);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
          //Display this
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF4_item(settings, 0);
          scope_channel_LOADtoREF4_item(settings, 2);
        }
        //Check on Load all ref button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 190) && (ytouch <= CH_REF_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          if(settings->color == CHANNEL1_COLOR)
          {
            //Load data from file x.ref to buffer refx (1-4)
            scope_load_REFx_item_file(1);
            scope_load_REFx_item_file(2);
            scope_load_REFx_item_file(3);
            scope_load_REFx_item_file(4);
            scopesettings.ref1 = 1;
            scopesettings.ref2 = 1;
            scopesettings.ref3 = 1;
            scopesettings.ref4 = 1;
          }
          else
          {
            //Load data from file x.ref to buffer refx (5-8)
            scope_load_REFx_item_file(5);
            scope_load_REFx_item_file(6);
            scope_load_REFx_item_file(7);
            scope_load_REFx_item_file(8);
            scopesettings.ref5 = 1;
            scopesettings.ref6 = 1;
            scopesettings.ref7 = 1;
            scopesettings.ref8 = 1;
          }
            
          //settings->ref1 = 1; settings->ref2 = 1; settings->ref3 = 1; settings->ref4 = 1;
          //scopesettings.ref1 = 1; scopesettings.ref2 = 1; scopesettings.ref3 = 1; scopesettings.ref4 = 1;
          //scopesettings.ch_ref1.enable = 1; scopesettings.ch_ref2.enable = 1; scopesettings.ch_ref3.enable = 1; scopesettings.ch_ref4.enable = 1;
                
          //Display this
          scope_channel_LOAD_REF_ALL_item(settings, 1);
          
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_SAVE_REF_ALL_item(settings, 0);
          
          scope_channel_COPYtoREF1_item(settings, 0);
          scope_channel_COPYtoREF2_item(settings, 0);
          scope_channel_COPYtoREF3_item(settings, 0);
          scope_channel_COPYtoREF4_item(settings, 0);
                    
          scope_channel_LOADtoREF1_item(settings, 0);
          scope_channel_LOADtoREF2_item(settings, 0);
          scope_channel_LOADtoREF3_item(settings, 0);
          scope_channel_LOADtoREF4_item(settings, 0);

          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
          //Display this
          scope_channel_LOAD_REF_ALL_item(settings, 2);
        } 
          
          //---------------------------
      }//end else if
      
      else if(refmenuopen && (xtouch >= CH_REF_MENU_XPOS + 210) && (xtouch <= (CH_REF_MENU_XPOS + CH_REF_MENU_WIDTH))
          &&(ytouch >= CH_REF_MENU_YPOS) && (ytouch <= CH_REF_MENU_YPOS + CH_REF_MENU_HEIGHT))
          //if(mathmenuopen && (xtouch >= settings->menuxpos+380) && (xtouch <= (settings->menuxpos + 500)))// && (!scopesettings.waveviewmode))
        //if(mathmenuopen && (xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + 550))// && (!scopesettings.waveviewmode))
      { 
        //Check on CHx copy to REF1, and in normal view mode
        if((ytouch >= CH_REF_MENU_YPOS + 6) && (ytouch <= CH_REF_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //For now just copy the settings to the given struct
          //memcpy(settings->ref1_tracebuffer, settings->tracebuffer, MAX_SAMPLE_BUFFER_SIZE);
                
          //Display this
          scope_channel_SAVE_REF1_item(settings, 1);
          
          //Save to file ref1
          if(settings->color == CHANNEL1_COLOR) scope_save_REFx_item_file(1); else scope_save_REFx_item_file(5);
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_SAVE_REF1_item(settings, 0);
        }
          //Check on CHx copy to REF2, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 52) && (ytouch <= CH_REF_MENU_YPOS + 92))
        {
          //For now just copy the settings to the given struct
          //memcpy(settings->ref2_tracebuffer, settings->tracebuffer, MAX_SAMPLE_BUFFER_SIZE);
                
          //Display this
          scope_channel_SAVE_REF2_item(settings, 1);
          
          //Save to file ref2
          if(settings->color == CHANNEL1_COLOR) scope_save_REFx_item_file(2); else scope_save_REFx_item_file(6);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release(); 
          
          //Display this
          scope_channel_SAVE_REF2_item(settings, 0);
        }
        //Check on CHx copy to REF3, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 98) && (ytouch <= CH_REF_MENU_YPOS + 138))
        {
          //For now just copy the settings to the given struct
          //memcpy(settings->ref3_tracebuffer, settings->tracebuffer, MAX_SAMPLE_BUFFER_SIZE);
                
          //Display this
          scope_channel_SAVE_REF3_item(settings, 1);
          
          //Save to file ref3
          if(settings->color == CHANNEL1_COLOR) scope_save_REFx_item_file(3); else scope_save_REFx_item_file(7);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
          
          //Display this
          scope_channel_SAVE_REF3_item(settings, 0);
        }
        //Check on CHx copy to REF4, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 144) && (ytouch <= CH_REF_MENU_YPOS + 184))
        {
          //For now just copy the settings to the given struct
          //memcpy(settings->ref4_tracebuffer, settings->tracebuffer, MAX_SAMPLE_BUFFER_SIZE);
                
          //Display this
          scope_channel_SAVE_REF4_item(settings, 1);
          
          //Save to file ref4
          if(settings->color == CHANNEL1_COLOR) scope_save_REFx_item_file(4); else scope_save_REFx_item_file(8);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
          //Display this
          scope_channel_SAVE_REF4_item(settings, 0);
        }
        //Check on OFF button, and in normal view mode
        else if((ytouch >= CH_REF_MENU_YPOS + 190) && (ytouch <= CH_REF_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          if(settings->color == CHANNEL1_COLOR)
          {
            //Load data from file x.ref to buffer refx
            scope_save_REFx_item_file(1);
            scope_save_REFx_item_file(2);
            scope_save_REFx_item_file(3);
            scope_save_REFx_item_file(4);
          }
          else
          {
            //Load data from file x.ref to buffer refx
            scope_save_REFx_item_file(5);
            scope_save_REFx_item_file(6);
            scope_save_REFx_item_file(7);
            scope_save_REFx_item_file(8);
          }
          //settings->ref1 = 1; settings->ref2 = 1; settings->ref3 = 1; settings->ref4 = 1;
                
          //Display this
          scope_channel_SAVE_REF_ALL_item(settings, 1);
          
          scope_channel_ref_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();  
          
          //Show the screen math menu
          scope_channel_CLEAR_REF_ALL_item(settings, 0);
          scope_channel_LOAD_REF_ALL_item(settings, 0);
         
          //scope_channel_COPYtoREF1_item(settings, 0);
          //scope_channel_COPYtoREF2_item(settings, 0);
          //scope_channel_COPYtoREF3_item(settings, 0);
          //scope_channel_COPYtoREF4_item(settings, 0);
          
          //scope_channel_LOADtoREF1_item(settings, 0);
          //scope_channel_LOADtoREF2_item(settings, 0);
          //scope_channel_LOADtoREF3_item(settings, 0);
          //scope_channel_LOADtoREF4_item(settings, 0);
          
          scope_channel_SAVE_REF1_item(settings, 0);
          scope_channel_SAVE_REF2_item(settings, 0);
          scope_channel_SAVE_REF3_item(settings, 0);
          scope_channel_SAVE_REF4_item(settings, 0);
          
          scope_channel_SAVE_REF_ALL_item(settings, 2);
          
          
        } 
          
          //---------------------------
      }//end else if SAVE_REFx
      
      //Check on MATH menu
      //else if(mathmenuopen && (xtouch >= settings->menuxpos + CH_MENU_WIDTH) && (xtouch <= settings->menuxpos + CH_MENU_WIDTH + CH_MATH_MENU_WIDTH) && (!scopesettings.waveviewmode))
      else if(mathmenuopen && (xtouch >= CH_MATH_MENU_XPOS) && (xtouch <= (CH_MATH_MENU_XPOS + 70))
          &&(ytouch >= CH_MATH_MENU_YPOS) && (ytouch <= CH_MATH_MENU_YPOS + CH_MATH_MENU_HEIGHT))
          //if(mathmenuopen && (xtouch >= settings->menuxpos+380) && (xtouch <= (settings->menuxpos + 500)))// && (!scopesettings.waveviewmode))
        //if(mathmenuopen && (xtouch >= settings->menuxpos) && (xtouch <= settings->menuxpos + 550))// && (!scopesettings.waveviewmode))
      { 
        //Check on A+B button, and in normal view mode
        if((ytouch >= CH_MATH_MENU_YPOS + 6) && (ytouch <= CH_MATH_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //set math mode A + B
          mathmode = 1;
            
          //Show the screen math menu
          scope_channel_math_select(settings);
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
          //Check on A-B button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 52) && (ytouch <= CH_MATH_MENU_YPOS + 92))
        {
          //set math mode A - B
          mathmode = 2;

          //Show the screen math menu
          scope_channel_math_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on B-A button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 98) && (ytouch <= CH_MATH_MENU_YPOS + 138))
        {
          //set math mode A * B
          mathmode = 3;

          //Show the screen math menu
          scope_channel_math_select(settings);
          //scope_channel_open_math_menu(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on A*B button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 144) && (ytouch <= CH_MATH_MENU_YPOS + 184))
        {
          //set math mode A / B
          mathmode = 4;

          //Show the screen math menu
          scope_channel_math_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on OFF button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 190) && (ytouch <= CH_MATH_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //set OFF
          mathmode = 0;

          //Show the screen math menu
          scope_channel_math_select(settings);

          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
          
          //---------------------------
      }//end else if //else if(mathmenuopen) close_open_channel_menus(settings, 0); //Close any of the sub menus if open (ref menu or math menu)
      
      else if(mathmenuopen && (xtouch >= CH_MATH_MENU_XPOS + 70) && (xtouch <= (CH_MATH_MENU_XPOS + CH_MATH_MENU_WIDTH))
          &&(ytouch >= CH_MATH_MENU_YPOS) && (ytouch <= CH_MATH_MENU_YPOS + CH_MATH_MENU_HEIGHT))
      { 
        //Check on adding button channal A
        if((ytouch >= CH_MATH_MENU_YPOS + 6) && (ytouch <= CH_MATH_MENU_YPOS + 46))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //set Adding channel A
          if (channelA<3)  channelA++; else channelA = 0;
          
          //Show the screen math menu
          scope_channel_math_select(settings);
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        //Check on adding button channal B
        else if((ytouch >= CH_MATH_MENU_YPOS + 52) && (ytouch <= CH_MATH_MENU_YPOS + 92))// && (scopesettings.waveviewmode == 0)) //200 230
        {
          //set Adding channel B
          if (channelB<3)  channelB++; else channelB = 0;
            
          //Show the screen math menu
          scope_channel_math_select(settings);
              
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();   
        }
        /*
        //Check on B-A button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 98) && (ytouch <= CH_MATH_MENU_YPOS + 138))// && (scopesettings.waveviewmode == 0)) //200 230
        {
        //set math mode B-A
        mathmode = 3;
            
        //Show the screen math menu
        scope_channel_math_select(settings);
        //scope_channel_open_math_menu(settings);
              
        //Wait until touch is released
        tp_i2c_wait_for_touch_release();   
        }
        //Check on A*B button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 144) && (ytouch <= CH_MATH_MENU_YPOS + 184))// && (scopesettings.waveviewmode == 0)) //200 230
        {
        //set math mode A*B
        mathmode = 4;
            
        //Show the screen math menu
        scope_channel_math_select(settings);
              
        //Wait until touch is released
        tp_i2c_wait_for_touch_release();   
        }
        //Check on OFF button, and in normal view mode
        else if((ytouch >= CH_MATH_MENU_YPOS + 190) && (ytouch <= CH_MATH_MENU_YPOS + 230))// && (scopesettings.waveviewmode == 0)) //200 230
        {
        //set OFF
        mathmode = 0;
            
        //Show the screen math menu
        scope_channel_math_select(settings);
              
        //Wait until touch is released
        tp_i2c_wait_for_touch_release();   
        }
         */ 
          //---------------------------
      } //end else if //else if(mathmenuopen) close_open_channel_menus(settings, 0); //Close any of the sub menus if open (ref menu or math menu)
              
        //*******************************************************************************************
    
        //*******************************************************************************************

        //Wait until touch is released before checking on a new position
        //tp_i2c_wait_for_touch_release();
        
        //Nothing to do here so wait until touch is released
        //tp_i2c_wait_for_touch_release();
    
      //}
        //}
      //}// tu je have touch
      else
      { 
        //Touch outside the menu so wait until touch is released and then quit
        tp_i2c_wait_for_touch_release();
        
        //Close any of the channel menus if open (close all menu -channel menu, ref menu, math menu)
        close_open_channel_menus(settings, 1);

        return;
      }//tu ma  byt have touch
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_acquisition_menu_touch(void)
{
  uint32 i,x,y;

  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {   
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
      return;
    
    //Check if there is touch
    if(havetouch)
    {
    /*    
    xtouch_tmp=xtouch-ACQ_MENU_XPOS;
    ytouch_tmp=ytouch-ACQ_MENU_YPOS; 
    scope_test_TP();
    */        
      //Check if touch within the menu field
      if((xtouch >= ACQ_MENU_XPOS) && (xtouch <= ACQ_MENU_XPOS + ACQ_MENU_WIDTH) && (ytouch >= ACQ_MENU_YPOS) && (ytouch <= ACQ_MENU_YPOS + ACQ_MENU_HEIGHT))
      {
        //Check on acquisition frequency being set
        if((ytouch >= ACQ_MENU_YPOS + 32) && (ytouch <= ACQ_MENU_YPOS + 149) && (xtouch >= ACQ_MENU_XPOS) && (xtouch <= ACQ_MENU_XPOS + 304))
        {
          //Check if in run state. Changing this setting is only allowed when running or single mode trigger
          if((scopesettings.runstate)||(scopesettings.triggermode == 1))
          {//------------------ acquisition_speed_texts start ------------------
            //Check on the different bounding boxes for the separate settings
            for(i=0;i<((sizeof(acquisition_speed_texts) / sizeof(int8 *))-11);i++)//7
            {
              //Calculate the coordinates for this setting
              x = ((i & 3) * 72) + 10 + ACQ_MENU_XPOS;
              y = ((i >> 2) * 23) + 33 + ACQ_MENU_YPOS;

              //Check if touch within this bounding box
              if((xtouch >= x) && (xtouch <= x + 68) && (ytouch >= y) && (ytouch <= y + 20))
              {
                //Set the new speed
                scopesettings.samplerate = i;

                //Set the FPGA sample rate setting without changing the current time per div setting
                //This way the sampling is actually done with the set speed, but the display stays at the set time per div
                fpga_set_sample_rate(i);

                //fw 0.2.64: unless the time/div no longer fits the record at this rate: then the nearest one that does,
                //with a notice at the top of the plot (acq_view.c)
                acq_view_fit(1);

                //Display the new setting
                scope_acquisition_speed_select();

                //Update the viable time per div settings
                scope_acquisition_timeperdiv_select();

                //Update the top menu bar display
                scope_acqusition_settings(0);
                
                //Flag conversion in progres
                //scopesettings.display_data_done = 0;
                break;
              }
            }
          }
        }
        //======================================================================
        //Check on middle button line set
        //ui-sample: the row of four boxes (Roll, Average, Filter, Sample; ui_ctrl.h), each box's drawn area down to the
        //rule at y 211; a grey box (Average in roll or with Sample Peak, Filter with Peak, Sample in roll) takes no tap
        else if(ui_acq_box_at(xtouch - ACQ_MENU_XPOS, ytouch - ACQ_MENU_YPOS) >= 0)
        {
          int box = ui_acq_box_at(xtouch - ACQ_MENU_XPOS, ytouch - ACQ_MENU_YPOS);

          //Check ACQ mode (short or long) button
          if(box == UI_ACQ_ROLL)
          {
            //Toggle the long_mode state
            scope_acquisition_ACQ_mode_select();
          }

          //Average button
          if((box == UI_ACQ_AVERAGE) && pc_glue_acq_box_live(UI_ACQ_AVERAGE))
          {
            //Toggle the average_mode state
            scopesettings.average_mode ^= 1;
            scope_acquisition_Average_mode_select();
            scope_acqusition_settings(0);
          }

          //Filter selector in 1.00o5's Long memory slot: Off, Boxcar, CIC2, CIC3, each with the FIR
          //fw 0.2.64: a filter shortens the record to 8190 samples (16-bit layout): the time/div follows
          if((box == UI_ACQ_FILTER) && pc_glue_ui_has(PC_FEAT_DECIM) && pc_glue_acq_box_live(UI_ACQ_FILTER))
          {
            pc_glue_decim_tap();
            acq_view_fit(1);
            scope_acquisition_timeperdiv_select();
            scope_acqusition_settings(0);
          }

          //Sample Normal / Peak (Scope only, not in roll): Peak runs the 16-bit path, the record and time/div follow
          if((box == UI_ACQ_SAMPLE) && pc_glue_acq_box_live(UI_ACQ_SAMPLE))
          {
            pc_glue_sample_tap();
            acq_view_fit(1);
            scope_acquisition_timeperdiv_select();
            scope_acqusition_settings(0);
          }
        }
        //======================================================================
        //Check on time per div being set
        else if((ytouch >= ACQ_MENU_YPOS + 240) && (ytouch <= ACQ_MENU_YPOS + 380) && (xtouch >= ACQ_MENU_XPOS) && (xtouch <= ACQ_MENU_XPOS + 304))
        {
          //Check on the different bounding boxes for the separate settings
          for(i=0;i<((sizeof(time_div_texts) / sizeof(int8 *))-11);i++)//9   
          {
            //Calculate the coordinates for this setting
            x = ((i & 3) * 72) + 10 + ACQ_MENU_XPOS;
            y = ((i >> 2) * 23) + 241 + ACQ_MENU_YPOS;  //191

            //Check if touch within this bounding box (fw 0.2.64: a grey, not viable, time/div cannot be chosen)
            if((xtouch >= x) && (xtouch <= x + 68) && (ytouch >= y) && (ytouch <= y + 20))
            {
              //Set the new time per div, the trigger point kept inside the record (fw 0.2.68: acq_view_set_tb, the
              //path the snap after a rate change takes too). fw 0.2.76: running or in Single, the ladder's rate for it
              //comes with it (docs/ui-guidelines.md section 2, "Time/div drives the sample rate")
              int r = acq_view_set_tb(((sizeof(time_div_texts) / sizeof(int8 *)) - 1) - i);

              if(!r)
                break;

              if(r == ACQ_SET_RATE)
              {
                fpga_set_sample_rate(scopesettings.samplerate);
                scope_acquisition_speed_select();
              }

              //Display the new setting
              scope_acquisition_timeperdiv_select();
              scope_acqusition_settings(0);
              break;
            }
          }
        }
        //====================================================================== 
        //Check on memory deep being set
        else if((ytouch >= ACQ_MENU_YPOS + 32) && (ytouch <= ACQ_MENU_YPOS + 210) && (xtouch >= ACQ_MENU_XPOS + UI_ACQ_COL_X) && (xtouch <= ACQ_MENU_XPOS + ACQ_MENU_WIDTH))
        {
          //Check on the different bounding boxes for the separate settings
          for(i=0;i<(sizeof(memory_deep_texts) / sizeof(int8 *));i++)
          {
            //Calculate the coordinates for this setting            
            x = UI_ACQ_CHIP_X + ACQ_MENU_XPOS;
            y = (i * 36) + 33 + ACQ_MENU_YPOS;

            //Check if touch within this bounding box
            if((xtouch >= x) && (xtouch <= x + 54) && (ytouch >= y) && (ytouch <= y + 25) PC_UI_HAS(PC_FEAT_MEM_BASE + i))
            {
              //Set the new memory deep

              //extern uint32 a,b,c,d,e,f;
              scopesettings.samplecount = (i*3000)+3000;
              fpgasettings.totalsamples = scopesettings.samplecount;
              //fpgasettings.totalsamples = (((sizeof(memory_deep_texts) / sizeof(int8 *))*3000));
              //scopesettings.samplecount = (((sizeof(memory_deep_texts) / sizeof(int8 *))*3000));

              //Display the new memory deep
              //scope_acquisition_timeperdiv_select();
              //scope_acqusition_settings(0);
              
              scope_acquisition_memory_deep_select();

              //fw 0.2.64: a shorter record may no longer fill the trace area at this time/div
              acq_view_fit(1);
              scope_acquisition_timeperdiv_select();
              scope_acqusition_settings(0);
              break;
            }
          }
        }
        
        //======================================================================
        //fw 0.2.56: the acquisition menu holds acquisition only; Trace style and the Display variants (the 0.2.51 Display
        //row) are on the CTRL page (ctrl_page.c)
        //======================================================================     
        
      //------------------------------------------------------------------------

        //Wait until touch is released before checking on a new position
        tp_i2c_wait_for_touch_release();
      }
      else
      {
        //Touch outside the menu so wait until touch is released and then quit
        tp_i2c_wait_for_touch_release();

        return;
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

//ui-type: the keypad for the Nth edge's N (1 .. 65535), nested under the trigger menu as the generator's keypad is under
//its menu: the screen under it saved, the number typed applied on OK, the current value on a touch outside or a host
//request (handle_keyboard_menu_touch puts it back in the buffer), the keypad closed on every way out
static void handle_trigger_n_keypad(void)
{
  if(trignmenuopen)
    return;

  tp_i2c_wait_for_touch_release();

  display_set_destination_buffer(displaybuffer2);
  display_copy_rect_from_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT);

  trignmenuopen = 1;
  buffer_set_number(trig_ui.n);
  scope_open_keyboard_menu();
  handle_keyboard_menu_touch();

  tui_set_n(&trig_ui, buffer_to_int32());
  close_open_trigger_menus(0);
  tp_i2c_wait_for_touch_release();
}

void handle_trigger_menu_touch(void)
{
  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      close_open_trigger_menus(1);
      return;
    }

    //Check if there is touch
    if(havetouch)
    {
        
  //xtouch_tmp=xtouch-settings->menuxpos;
  //ytouch_tmp=ytouch-CH_MENU_YPOS;   
  //scope_test_TP();
  
  
      //Check if touch within the menu field
      if((xtouch >= TRIGGER_MENU_XPOS) && (xtouch <= TRIGGER_MENU_XPOS+TRIGGER_MENU_WIDTH) && (ytouch >= TRIGGER_MENU_YPOS) && (ytouch <= TRIGGER_MENU_YPOS+TRIGGER_MENU_HEIGHT)) //232
      {
        //Check on trigger mode, and not in waveform view
        //if((ytouch >= TRIGGER_MENU_YPOS+5) && (ytouch <= TRIGGER_MENU_YPOS+29) && (!scopesettings.waveviewmode))
        if((ytouch >= TRIGGER_MENU_YPOS+5) && (ytouch <= TRIGGER_MENU_YPOS+52) && (!scopesettings.waveviewmode))
        {
          //Check on auto
          if((xtouch >= TRIGGER_MENU_XPOS+116) && (xtouch <= TRIGGER_MENU_XPOS+152))//112-148
          { 
            //Set to auto mode
            set_trigger_mode(0);
            
            tp_i2c_wait_for_touch_release();
          }
        //}
        //Check on trigger mode, and not in waveform view
        //if((ytouch >= TRIGGER_MENU_YPOS+28) && (ytouch <= TRIGGER_MENU_YPOS+52) && (!scopesettings.waveviewmode))
        //{
          //Check on single
          else if((xtouch >= TRIGGER_MENU_XPOS+65) && (xtouch <= TRIGGER_MENU_XPOS+112))//67-114
          {
            //Set to single mode
            set_trigger_mode(1);
            
            tp_i2c_wait_for_touch_release();
          }
          //Check on normal
          else if((xtouch >= TRIGGER_MENU_XPOS+156) && (xtouch <= TRIGGER_MENU_XPOS+210))//146-200
          {
            //Set to normal mode
            set_trigger_mode(2);
            
            tp_i2c_wait_for_touch_release();
          }
        }
        //Check on trigger edge, and not in waveform view
        else if((ytouch >= TRIGGER_MENU_YPOS+61) && (ytouch <= TRIGGER_MENU_YPOS+107) && (!scopesettings.waveviewmode)) //120 155 ,125-147
        {
          //Check on rising (ui-type: Positive under Type Pulse, the wider box)
          if((xtouch >= TRIGGER_MENU_XPOS+66) && (xtouch <= TRIGGER_MENU_XPOS+128))    //626-666
          {
            //Set the trigger edge to rising
            scopesettings.triggeredge = 0;

            //Update the FPGA
            fpga_set_trigger_edge();

            //Display this
            scope_trigger_edge_select();
            scope_trigger_settings(0);
            
            tp_i2c_wait_for_touch_release();
          }
          //Check on falling (ui-type: Negative under Type Pulse)
          else if((xtouch >= TRIGGER_MENU_XPOS+147) && (xtouch <= TRIGGER_MENU_XPOS+209))   //671-716
          {
            //Set the trigger edge to falling
            scopesettings.triggeredge = 1;

            //Update the FPGA
            fpga_set_trigger_edge();

            //Display this
            scope_trigger_edge_select();
            scope_trigger_settings(0);
            
            tp_i2c_wait_for_touch_release();
          }
        }
        //Check on trigger channel, and not in waveform view
        else if((ytouch >= TRIGGER_MENU_YPOS+117) && (ytouch <= TRIGGER_MENU_YPOS+163) && (!scopesettings.waveviewmode)) //185 220 ,188-210
        {
          //Check on channel 1
          if((xtouch >= TRIGGER_MENU_XPOS+70) && (xtouch <= TRIGGER_MENU_XPOS+103))
          {
            //Only when the channel is enabled
            if(scopesettings.channel1.enable)
            {
              //Set the channel 1 as trigger source
              scopesettings.triggerchannel = 0;

              //Update the FPGA
              fpga_set_trigger_channel();
              
              //Set the trigger vertical position to match channel 1 trace position
              scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_channel_select();
              scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();
            }
          }
          //Check on channel 2
          else if((xtouch >= TRIGGER_MENU_XPOS+121) && (xtouch <= TRIGGER_MENU_XPOS+154))
          {
            //Only when the channel is enabled
            if(scopesettings.channel2.enable)
            {
              //Set channel 2 as trigger source
              scopesettings.triggerchannel = 1;

              //Update the FPGA
              fpga_set_trigger_channel();

              //Set the trigger vertical position to match channel 2 trace position
              scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_channel_select();
              scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();
            }
          }
          else if((xtouch >= TRIGGER_MENU_XPOS+172) && (xtouch <= TRIGGER_MENU_XPOS+205) PC_UI_HAS(PC_FEAT_EXT_TRIGGER))
          {
            //Only when the channel is enabled
            //if(scopesettings.channel2.enable)
            //{
              //Set channel 2 as trigger source
              scopesettings.triggerchannel = 2;

              //PC protocol: write the EXT source to the FPGA, switch the generator off (it drives the same pin)
              PC_EXT_SELECTED();

              //Update the FPGA
              //fpga_set_trigger_channel();

              //Set the trigger vertical position to match channel 2 trace position
              //scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_channel_select();
              scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();
            //}
          }
        }
        //Check on tigger position (Always50%), and not in waveform view
        else if((ytouch >= TRIGGER_MENU_YPOS+173) && (ytouch <= TRIGGER_MENU_YPOS+219) && (!scopesettings.waveviewmode)) //125-147
        {
          //Check on tigger position 25%
          if((xtouch >= TRIGGER_MENU_XPOS+70) && (xtouch <= TRIGGER_MENU_XPOS+103))
          {

              //Set the channel 1 as trigger source
              //scopesettings.triggerchannel = 0;
            
            //fpgasettings.settriggerpoint = 750;
            fpgasettings.settriggerpoint = scopesettings.samplecount / 4;

              //Update the FPGA
              //fpga_set_trigger_channel();
              
              //Set the trigger vertical position to match channel 1 trace position
              //scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_position_select();
              //scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();
            
          }
          //Check on tigger position 50%
          else if((xtouch >= TRIGGER_MENU_XPOS+121) && (xtouch <= TRIGGER_MENU_XPOS+154))
          {

              //Set channel 2 as trigger source
              //scopesettings.triggerchannel = 1;
            
            //fpgasettings.settriggerpoint = 1500;
            fpgasettings.settriggerpoint = scopesettings.samplecount / 2;

              //Update the FPGA
              //fpga_set_trigger_channel();

              //Set the trigger vertical position to match channel 2 trace position
              //scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_position_select();
              //scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();

          }
          //Check on tigger position 75%
          else if((xtouch >= TRIGGER_MENU_XPOS+172) && (xtouch <= TRIGGER_MENU_XPOS+205))
          {

              //Set channel 2 as trigger source
              //scopesettings.triggerchannel = 2;
              
              //fpgasettings.settriggerpoint = 2250;
              fpgasettings.settriggerpoint = scopesettings.samplecount-1000;

              //Update the FPGA
              //fpga_set_trigger_channel();

              //Set the trigger vertical position to match channel 2 trace position
              //scope_calculate_trigger_vertical_position();

              //Display this
              scope_trigger_position_select();
              //scope_trigger_settings(0);
              
              tp_i2c_wait_for_touch_release();

          }
        }
        //Check on 50% button and not in waveform view
        else if((ytouch >= TRIGGER_MENU_YPOS+228) && (ytouch <= TRIGGER_MENU_YPOS+276) && (!scopesettings.waveviewmode)) //125-147
        {
          //Toggle the always trigger 50% state
          scopesettings.alwaystrigger50 ^= 1;
          
          //Show the state 
          scope_display_slide_button(TRIGGER_MENU_XPOS + 140, TRIGGER_MENU_YPOS + 240, scopesettings.alwaystrigger50, GREEN_COLOR);//298
        }
        //ui-type: the Type row (sixth, docs/plans/ui-rows.md 2.2), not in the waveform viewer
        else if((ytouch >= TRIGGER_MENU_YPOS+283) && (ytouch <= TRIGGER_MENU_YPOS+332) && (!scopesettings.waveviewmode))
        {
          int t = -1;

          if((xtouch >= TRIGGER_MENU_XPOS+65) && (xtouch <= TRIGGER_MENU_XPOS+109))
            t = TUI_EDGE;
          else if((xtouch >= TRIGGER_MENU_XPOS+113) && (xtouch <= TRIGGER_MENU_XPOS+163))
            t = TUI_PULSE;
          else if((xtouch >= TRIGGER_MENU_XPOS+167) && (xtouch <= TRIGGER_MENU_XPOS+207))
            t = TUI_NTH;

          if((t >= 0) && (t != trig_ui.type))
          {
            //The type and its parameter (snapped into the rate's range) armed through the SET_TRIGGER2 path
            tui_set_type(&trig_ui, (uint8)t, pc_glue_trig_tw5());
            pc_glue_trig_ui_apply();

            //The menu's height follows the type: the screen under it back, the menu drawn again in place; the chip's glyph
            display_set_source_buffer(displaybuffer1);
            display_copy_rect_to_screen(TRIGGER_MENU_XPOS, TRIGGER_MENU_YPOS, TRIGGER_MENU_WIDTH, TRIGGER_MENU_SAVE_H);
            scope_draw_trigger_menu();
            scope_trigger_settings(0);
          }

          tp_i2c_wait_for_touch_release();
        }
        //ui-type: the type's parameter rows (2.3): Pulse has Width (- / +) and Shorter / Longer (a tap toggles), Nth has
        //N (- / + by 1, the keypad from its value box) and Idle (- / + with Off below the ladder); not in the viewer
        else if(pc_glue_trig_params() && (ytouch >= TRIGGER_MENU_YPOS+334) && (ytouch <= TRIGGER_MENU_YPOS+424) &&
                (!scopesettings.waveviewmode))
        {
          int changed = 0;

          if(ytouch <= TRIGGER_MENU_YPOS+378)
          {
            if(trig_ui.type == TUI_PULSE)
              changed = tui_step_width(&trig_ui, (xtouch < TRIGGER_MENU_XPOS+110) ? -1 : 1, pc_glue_trig_tw5());
            else if((xtouch >= TRIGGER_MENU_XPOS+100) && (xtouch <= TRIGGER_MENU_XPOS+172))
            {
              handle_trigger_n_keypad();
              changed = 1;
            }
            else
              changed = tui_step_n(&trig_ui, (xtouch < TRIGGER_MENU_XPOS+100) ? -1 : 1);
          }
          else if(ytouch >= TRIGGER_MENU_YPOS+380)
          {
            if(trig_ui.type == TUI_PULSE)
            {
              trig_ui.longer ^= 1;
              changed = 1;
            }
            else
              changed = tui_step_idle(&trig_ui, (xtouch < TRIGGER_MENU_XPOS+110) ? -1 : 1, pc_glue_trig_tw5());
          }

          if(changed)
          {
            pc_glue_trig_ui_apply();
            scope_trigger_param_select();
          }

          tp_i2c_wait_for_touch_release();
        }
        //Wait until touch is released before checking on a new position

       // tp_i2c_wait_for_touch_release(); // doplnit do ostatnych tlacidiel aby nebliakli
      }
      else
      {         
        //Touch outside the menu so wait until touch is released and then quit
        tp_i2c_wait_for_touch_release();
        
        //Close the trigger menu
        close_open_trigger_menus(1);

        return;
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_generator_menu_touch(void)
{
  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {
    int32 tmp;
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      close_open_gen_menus(1);
      return;
    }

    //Check if there is touch
    if(havetouch)
    {
     
      if(dev_mode)
      {
        xtouch_tmp=xtouch-GEN_MENU_XPOS;
        ytouch_tmp=ytouch-GEN_MENU_YPOS;   
        scope_test_TP();
      }
  
  
      //Check if touch within the menu field
      if((xtouch >= GEN_MENU_XPOS) && (xtouch <= GEN_MENU_XPOS+GEN_MENU_WIDTH) && (ytouch >= GEN_MENU_YPOS) && (ytouch <= GEN_MENU_YPOS+GEN_MENU_HEIGHT)) //232
      {
        //====================================================================================================
        //Check on buttons on power flor 1
        if((ytouch >= GEN_MENU_YPOS+4) && (ytouch <= GEN_MENU_YPOS+52) && (!scopesettings.waveviewmode))
        {
          //Toggle the on/off state
          fpgasettings.gen_enable ^= 1;
          
          //Update the FPGA
          fpga_on_off_generator();
          
          //Show the state (generator on or off)
          scope_display_slide_button(TRIGGER_MENU_XPOS + 162, TRIGGER_MENU_YPOS + 17, fpgasettings.gen_enable, GREEN_COLOR);
          
          //Wait until touch is released before checking on a new position
          tp_i2c_wait_for_touch_release();
          
          //Set the button inactive
          scope_generator_settings(0);
        
          //********************************************************************
          //Check on DSO gen
          if((xtouch >= GEN_MENU_XPOS+65) && (xtouch <= GEN_MENU_XPOS+112))//67-114
          { 

          }
          //********************************************************************
          //Check on Extern gen
          else if((xtouch >= GEN_MENU_XPOS+116) && (xtouch <= GEN_MENU_XPOS+152))//112-148 
          {

          }
          //********************************************************************
          //Check on Configuration extern gen
          else if((xtouch >= GEN_MENU_XPOS+156) && (xtouch <= GEN_MENU_XPOS+210))//146-200
          {

          }
        }
        //Check on buttons on freq flor 3
        else if((ytouch >= GEN_MENU_YPOS+60) && (ytouch <= GEN_MENU_YPOS+108) && (!scopesettings.waveviewmode)) //185 220 ,188-210
        {
          //Check on box with frequency value
          if((xtouch >= GEN_MENU_XPOS+80) && (xtouch <= GEN_MENU_XPOS+150))
          {
            //Check if already open
            if(freqmenuopen == 0)//refnenu
            {
              //Close any of the sub menus if open
              close_open_gen_menus(0); //close_open_channel_menus(settings, 0);
              
              //Set this item active
              //scope_channel_ref_menu_item(settings, 1);
              
              //Wait until touch is released
              tp_i2c_wait_for_touch_release();
              
              //Save the screen under the menu
              display_set_destination_buffer(displaybuffer2);
              display_copy_rect_from_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT); //key (hold off) menu
            
              //Signal the frequency menu is opened
              freqmenuopen = 1;
              
              buffer_set_number(fpgasettings.gen_freq);
              
              //Show the screen key (frequency) menu
              scope_open_keyboard_menu();//scope_channel_open_ref_menu(settings);
              
                       
            } 
            
            handle_keyboard_menu_touch(); 
 
             tmp = buffer_to_int32();
            if (tmp > GEN_f_MAX)fpgasettings.gen_freq = GEN_f_MAX;
              else if (tmp < 0)fpgasettings.gen_freq = 0; 
                else fpgasettings.gen_freq = tmp;
            
            fpga_set_generator_freq(fpgasettings.gen_freq);
            
            gen_freq_select();
            
            //Set the button inactive
            scope_generator_settings(0);
          

          }
          //********************************************************************
          //Check on 1 kHz button
          else if((xtouch >= GEN_MENU_XPOS+175) && (xtouch <= GEN_MENU_XPOS+205)) //else if((xtouch >= GEN_MENU_XPOS+172) && (xtouch <= GEN_MENU_XPOS+205))
          {   
            //Select the font for the texts
            display_set_font(&font_3);
            //Display the green text and frame
            display_set_fg_color(GREEN_COLOR);
            display_draw_rounded_rect(GEN_MENU_XPOS+165, GEN_MENU_YPOS+73, 42, 22, 2);
            display_text(GEN_MENU_XPOS+169, GEN_MENU_YPOS+76,  "1 kHz");//695 155 33
    
            fpgasettings.gen_freq = 1000; //1000Hz freq
            fpga_set_generator_freq(fpgasettings.gen_freq);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release(); 
            
            gen_freq_select();
            
            //Set the button inactive
            scope_generator_settings(0);
          }
        }
        //====================================================================================================
        //Check on buttons on duty flor 4
        else if((ytouch >= GEN_MENU_YPOS+116) && (ytouch <= GEN_MENU_YPOS+164) && (!scopesettings.waveviewmode)) //125-147
        {
          //Check on box with duty value 
          if((xtouch >= GEN_MENU_XPOS+90) && (xtouch <= GEN_MENU_XPOS+140))
          {
            //Check if already open
            if(dutymenuopen == 0)//refnenu
            {
              //Close any of the sub menus if open
              close_open_gen_menus(0); //close_open_channel_menus(settings, 0);
              
              //Set this item active
              //scope_channel_ref_menu_item(settings, 1);
              
              //Wait until touch is released
              tp_i2c_wait_for_touch_release();
              
              //Save the screen under the menu
              display_set_destination_buffer(displaybuffer2);
              display_copy_rect_from_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT); //key (hold off) menu
            
              //Signal the hold off menu is opened
              dutymenuopen = 1;
              
              buffer_set_number(fpgasettings.gen_duty);
              
              //Show the screen key (duty) menu
              scope_open_keyboard_menu();//scope_channel_open_ref_menu(settings);
                       
            } 
            handle_keyboard_menu_touch(); 
            
            tmp = buffer_to_int32(); 
            if (tmp > 100)fpgasettings.gen_duty = 100; 
              else if (tmp < 0)fpgasettings.gen_duty = 0; 
                else fpgasettings.gen_duty = tmp;
            fpga_set_generator_duty(fpgasettings.gen_duty);
            
            gen_duty_select();
            
            //Set the button inactive
            scope_generator_settings(0);
          }
          //********************************************************************
          //Check on 50% duty button
          else if((xtouch >= GEN_MENU_XPOS+175) && (xtouch <= GEN_MENU_XPOS+205)) //else if((xtouch >= GEN_MENU_XPOS+172) && (xtouch <= GEN_MENU_XPOS+205))
          {
            //Select the font for the texts
            display_set_font(&font_3);
            //Display the yellow text and frame
            display_set_fg_color(YELLOW_COLOR);
            display_draw_rounded_rect(GEN_MENU_XPOS+165, GEN_MENU_YPOS+129, 42, 22, 2);
            display_text(GEN_MENU_XPOS+175, GEN_MENU_YPOS+132, "50%");//175

            fpgasettings.gen_duty = 50; //50% duty
            fpga_set_generator_duty(fpgasettings.gen_duty);
            
            //Wait until touch is released
            tp_i2c_wait_for_touch_release(); 
            
            gen_duty_select();
            
            //Set the button inactive
            scope_generator_settings(0);
          }
        }
        //====================================================================================================
        //Wait until touch is released before checking on a new position
        //tp_i2c_wait_for_touch_release(); // doplnik pre kr=azdy prikaz zvlast
      }
      else
      {  
        //Touch outside the menu so wait until touch is released and then quit
        tp_i2c_wait_for_touch_release();
        
        //Close any of the generator menus if open (close all menu -freq menu, duty menu, ampl menu, offset menu, phase menu)
        close_open_gen_menus(1);

        return;
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

void handle_keyboard_menu_touch(void)
{
  uint32 x,y,z;
  uint8 number_len = 0;
  extern char numbuffer[NUMBUF_MAX];
  extern uint8 numbuffer_len;
  extern uint8 numchange;
  
  numchange = 1;

  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {   
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the keypad as a touch outside does (protocol 1.23), without the number typed:
    //the caller applies the buffer, so it gets the current value back
    if(PC_UI_YIELD())
    {
      if(freqmenuopen)
        buffer_set_number(fpgasettings.gen_freq);
      else if(dutymenuopen)
        buffer_set_number(fpgasettings.gen_duty);
      else if(trignmenuopen)
        buffer_set_number(trig_ui.n);

      if(freqmenuopen || dutymenuopen) close_open_gen_menus(0);
      if(trignmenuopen) close_open_trigger_menus(0);
      return;
    }
    
    //Check if there is touch
    if(havetouch)
    {
         if(dev_mode)
      {
      xtouch_tmp=xtouch-KEY_MENU_XPOS;
      ytouch_tmp=ytouch-KEY_MENU_YPOS; 
      scope_test_TP();
         }
            
      //Check if touch within the menu field
      if((xtouch >= KEY_MENU_XPOS) && (xtouch <= KEY_MENU_XPOS + KEY_MENU_WIDTH) && (ytouch >= KEY_MENU_YPOS) && (ytouch <= KEY_MENU_YPOS + KEY_MENU_HEIGHT))
      {
        //Check on time per div being set
        if((ytouch >= KEY_MENU_YPOS + 80) && (ytouch <= KEY_MENU_YPOS + 280))//80-350
        {
          x = KEY_MENU_XPOS;
          y = KEY_MENU_YPOS;
          z='0';
          
          //Check if touch within this bounding box
          if((ytouch >= y + 80) && (ytouch <= y + 120))//riadok 1 100
          {     
            if((xtouch >= x + 15) &&  (xtouch <= x + 55))   buffer_add_digit('7');//z='7';    //1 cislo z lava 40 35
            if((xtouch >= x + 70) &&  (xtouch <= x + 110))  buffer_add_digit('8');//z='8';    //2 cislo z lava 90  90
            if((xtouch >= x + 130) && (xtouch <= x + 170))  buffer_add_digit('9');//z='9';    //3 cislo z lava 150
            if((xtouch >= x + 190) && (xtouch <= x + 230))  z='m';    //4 cislo z lava 210
            if((xtouch >= x + 245) && (xtouch <= x + 285))  {buffer_add_digit('0');buffer_add_digit('0');buffer_add_digit('0');}//z='k';    //4 cislo z lava 260 265
          } 
                    //Check if touch within this bounding box
          else if((ytouch >= y + 130) && (ytouch <= y + 170))//riadok 2 150
          {                
            if((xtouch >= x + 15) &&  (xtouch <= x + 55))   buffer_add_digit('4');//z='4';    //1 cislo z lava 40
            if((xtouch >= x + 70) &&  (xtouch <= x + 110))  buffer_add_digit('5');//z='5';    //2 cislo z lava 90
            if((xtouch >= x + 130) && (xtouch <= x + 170))  buffer_add_digit('6');//z='6';    //3 cislo z lava 150
            if((xtouch >= x + 190) && (xtouch <= x + 230))  z='u';    //4 cislo z lava 210
            if((xtouch >= x + 245) && (xtouch <= x + 285))  {buffer_add_digit('0');buffer_add_digit('0');buffer_add_digit('0');
                                                             buffer_add_digit('0');buffer_add_digit('0');buffer_add_digit('0');}//z='M';    //4 cislo z lava 260
          }
          else if((ytouch >= y + 180) && (ytouch <= y + 220))//riadok 3 200
          {
            if((xtouch >= x + 15) &&  (xtouch <= x + 55))   buffer_add_digit('1');//z='1';    //1 cislo z lava 40
            if((xtouch >= x + 70) &&  (xtouch <= x + 110))  buffer_add_digit('2');//z='2';    //2 cislo z lava 90
            if((xtouch >= x + 130) && (xtouch <= x + 170))  buffer_add_digit('3');//z='3';    //3 cislo z lava 150
            if((xtouch >= x + 190) && (xtouch <= x + 230))  z='n';    //4 cislo z lava 210
            if((xtouch >= x + 245) && (xtouch <= x + 285))  z='G';    //4 cislo z lava 260
          }
          else if((ytouch >= y + 235) && (ytouch <= y + 275))//riadok 4 255
          {
            if((xtouch >= x + 15) &&  (xtouch <= x + 55))   buffer_add_digit('0');//z='0';    //1 cislo z lava 40
            if((xtouch >= x + 70) &&  (xtouch <= x + 110))  buffer_set_sign('+');//z=',';    //2 cislo z lava 90
            if((xtouch >= x + 130) && (xtouch <= x + 170))  buffer_set_sign('-');//z='-';    //3 cislo z lava 150
            if((xtouch >= x + 190) && (xtouch <= x + 230))  z='p';    //4 cislo z lava 210
            if((xtouch >= x + 245) && (xtouch <= x + 285))  z='T';    //4 cislo z lava 260
          }
        }
        //Check on time per div being set
        else if((ytouch >= KEY_MENU_YPOS + 280) && (ytouch <= KEY_MENU_YPOS + KEY_MENU_HEIGHT))//80-350  
        {
          //Check if touch within this bounding box
          if((ytouch >= y + 290) && (ytouch <= y + 330))//riadok 5  310
          { 
            if(freqmenuopen)
            {
              if((xtouch >= x + 15)  && (xtouch <= x + 85))   buffer_set_number(1);//display_text(KEY_MENU_XPOS + 220, KEY_MENU_YPOS + 34, "1 Hz");    // MIN 50
              if((xtouch >= x + 115) && (xtouch <= x + 185))  buffer_set_number(1000);//display_text(KEY_MENU_XPOS + 230, KEY_MENU_YPOS + 34, "1 kHz");   // Default 150
              if((xtouch >= x + 215) && (xtouch <= x + 285))  buffer_set_number(5000000);//display_text(KEY_MENU_XPOS + 210, KEY_MENU_YPOS + 34, "5 MHz");   // MAX 250
            }
            if(dutymenuopen)
            {
              if((xtouch >= x + 15)  && (xtouch <= x + 85))   buffer_set_number(0);//display_text(KEY_MENU_XPOS + 220, KEY_MENU_YPOS + 34, "1 Hz");    // MIN 50
              if((xtouch >= x + 115) && (xtouch <= x + 185))  buffer_set_number(50);//display_text(KEY_MENU_XPOS + 230, KEY_MENU_YPOS + 34, "1 kHz");   // Default 150
              if((xtouch >= x + 215) && (xtouch <= x + 285))  buffer_set_number(100);//display_text(KEY_MENU_XPOS + 210, KEY_MENU_YPOS + 34, "5 MHz");   // MAX 250
            }
            if(trignmenuopen)
            {
              //ui-type: N 1 (MIN), 2 (Default), 65535 (MAX)
              if((xtouch >= x + 15)  && (xtouch <= x + 85))   buffer_set_number(1);
              if((xtouch >= x + 115) && (xtouch <= x + 185))  buffer_set_number(2);
              if((xtouch >= x + 215) && (xtouch <= x + 285))  buffer_set_number(65535);
            }
          } 
          //Check if touch within this bounding box
          else if((ytouch >= y + 345) && (ytouch <= y + 385))//riadok 6  365
          {
           
            if((xtouch >= x + 15)  && (xtouch <= x + 85))  buffer_backspace();  // zmaže posledný znak    // BACK
            if((xtouch >= x + 115) && (xtouch <= x + 185)) {buffer_clear(); }   // Vymaže buffer          // Clear
            if((xtouch >= x + 215) && (xtouch <= x + 285)) { break;}//int32_t val = buffer_to_int32();   // OK

          }

        }
        extern uint32 a,b,c,d,e,f,g,h,i,j,k;
        
        //Select the font for the texts
        display_set_font(&font_5);//3

        //Set background color 
        display_set_fg_color(GREY_COLOR); //DARKGREY_COLOR
                
        //display_set_fg_color(DARKGREY_COLOR);
        
        //Fill the background
        display_fill_rect(KEY_MENU_XPOS + 142, KEY_MENU_YPOS + 32, 145, 27);//100 35 145 21
        
        //Set color for the text
        display_set_fg_color(WHITE_COLOR);
            
        if (numbuffer_len>0) number_len = numbuffer_len - 1; else number_len = 0;
                    
        display_text(KEY_MENU_XPOS + 269-(number_len*12), KEY_MENU_YPOS + 34, numbuffer); //200
      //------------------------------------------------------------------------

        //Wait until touch is released before checking on a new position
        tp_i2c_wait_for_touch_release();
      }
      else
      {
        //Touch outside the menu so wait until touch is released and then quit
        //tp_i2c_wait_for_touch_release();
        
        //fpgasettings.gen_freq = buffer_to_int32();
        
        
        if(freqmenuopen || dutymenuopen) close_open_gen_menus(0);
        if(trignmenuopen) close_open_trigger_menus(0);

        return;
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_right_basic_menu_touch(void)
{
  //fw 0.2.56: the spectrum's empty slots (T CURSOR, V CURSOR; ui_p1_shown) do nothing; fw 0.2.65: MEASURES opens the
  //spectrum's own list
  if((ytouch >= 183) && (ytouch <= 237) && !ui_p1_shown(UI_P1_TCURSOR, ctrl_page_spectrum()))
    return;

  //fw 0.2.69: Frequency response has T CURSOR (C1 / C2) but no V CURSOR
  if((ytouch >= 243) && (ytouch <= 297) && !ui_p1_shown(UI_P1_VCURSOR, ctrl_page_spectrum()))
    return;

  //fw 0.2.66: the Bode plot's empty MEASURES slot does nothing either
  if((ytouch >= 303) && (ytouch <= 357) && !ui_p1_shown(UI_P1_MEASURES, ctrl_page_spectrum()))
    return;

  //Check if run/stop or page up button is touched
  if((ytouch >= 63) && (ytouch <= 117))
  {
    //Check on wave view state(Scope mode) for which button is shown
    if(!scopesettings.waveviewmode)
    {
      //Run/stop so highlight that button if touched
      scope_run_stop_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //fw 0.2.68: the button does what it reads (scope_run_view): RUN while stopped, STOP while running, waiting or
      //while a host acquisition runs (that one is aborted and the UI stays stopped)
      if(scope_run_view() == UI_RUN_STOP)
      {
        scopesettings.runstate = 1;
      }
      else
      {
        scopesettings.runstate = 0;
        pc_glue_host_stop();
      }

      //Button back to inactive state
      scope_run_stop_button(0);

      //When the scope is switched back to running state, change the sample rate to the current time per div setting
      if(scopesettings.runstate)//ok
      {
        //Long time base - set other values and clear view area
        if(scopesettings.long_mode) scope_preset_values(); 
   
        //Show this on the screen
        scope_acqusition_settings(0);
        
        //Match the volts per division settings for both channels
        //Is needed when vertical zoom has been used
        match_volt_per_div_settings(&scopesettings.channel1);
        match_volt_per_div_settings(&scopesettings.channel2);
      }

      //Display the changed state
      scope_run_stop_text();
    }
    else
    {
      //Previous waveform so highlight that button if touched
      scope_next_wave_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Button back to inactive state
      scope_next_wave_button(0);

      //Check if not on first item
      if(viewcurrentindex > 0)
      {
        //Select the previous one
        viewcurrentindex--;

        //Load the waveform data for the newly selected item
        if(scope_load_trace_data() == VIEW_TRACE_LOAD_ERROR)
        {
          //On error signal need to return to main item view mode
          scopesettings.waveviewmode = 0;
        }
      }
    }
  }
  //Check if auto set or page down button is touched
  else if((ytouch >= 123) && (ytouch <= 177))
  {
    //Check on wave view state (Skope mode) for which button is shown
    if(!scopesettings.waveviewmode)
    {
      //Auto set so highlight that button if touched
      scope_auto_set_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //fw 0.2.51: in the spectrum view the view's own Auto (start, or cancel while it runs); it redraws the button
      if(!PC_AUTOSET())
      {
        //Button back to inactive state
        scope_auto_set_button(0);

        //Short time base run auto setup
        if(!scopesettings.long_mode) scope_do_auto_setup();
      }
    }
    else
    {
      //Next waveform so highlight that button if touched
      scope_previous_wave_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Button back to inactive state
      scope_previous_wave_button(0);

      //Check if not on last item
      //Index starts on zero, available items on 1
      if(viewcurrentindex < (viewavailableitems - 1))
      {
        //Select the next one
        viewcurrentindex++;
        
        //Load the waveform data for the newly selected item
        if(scope_load_trace_data() == VIEW_TRACE_LOAD_ERROR)
        {
          //On error signal need to return to main item view mode
          scopesettings.waveviewmode = 0;
        }
      }
    }
  }
  //Check if time cursor button is touched
  else if((ytouch >= 183) && (ytouch <= 237))
  {
    //Highlight the button if touched
    scope_t_cursor_button(1);

    //Go and setup the t_cursor menu
    //scope_open_t_cursor_menu();
    
    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_t_cursor_button(0);

    //fw 0.2.69: in Frequency response the frequency cursors C1 / C2
    if(bode_view_active())
    {
      bode_view_tcursor();
      return;
    }

    //Toggle the enable setting
    scopesettings.timecursorsenable ^= 1;
    
    //Draw the cursor lines
    //scope_update_cursor_possition();
    if(scopesettings.long_mode && (!scopesettings.waveviewmode)) scope_preset_values();
    
  }
  //Check if volt cursor button is touched
  else if((ytouch >= 243) && (ytouch <= 297))
  {
    //Highlight the button if touched
    scope_v_cursor_button(1);
    
    //Go and setup the v_cursor menu
    //scope_open_v_cursor_menu();

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_v_cursor_button(0);

    //Toggle the enable setting
    scopesettings.voltcursorsenable ^= 1;
        
    //Draw the cursor lines
    //scope_update_cursor_possition();
    if(scopesettings.long_mode && (!scopesettings.waveviewmode)) scope_preset_values();
  }
  //Check if measures button is touched
  else if((ytouch >= 303) && (ytouch <= 357))
  {
    //Highlight the button if touched
    scope_measures_button(1);

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_measures_button(0);

    //Save the screen rectangle where the menu will be displayed (fw 0.2.65: its height follows the mode, ui_meas.h)
    display_set_destination_buffer(displaybuffer2);
    display_copy_rect_from_screen(UI_MSM_X, ui_msm_top(scope_measures_mode()), UI_MSM_W, ui_msm_height(scope_measures_mode()));

    //Go and setup the measurements menu
    scope_open_measures_menu();

    //Go and handle the menu touch
    handle_measures_menu_touch();

    //Restore the screen when done
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(UI_MSM_X, ui_msm_top(scope_measures_mode()), UI_MSM_W, ui_msm_height(scope_measures_mode()));

    //fw 0.2.71: Frequency response draws its cards with the plot: the new choice shows at once, not at the next redraw
    if(bode_view_active())
      bode_view_display();
  }
  //Check if save picture button is touched
  else if((ytouch >= 363) && (ytouch <= 417))
  {
    //Highlight the button if touched
    scope_save_picture_button(1);
    
    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_save_picture_button(0);
    
    //Save the picture on the SD card; fw 0.2.78: a host request meanwhile gets E_BUSY "file save in progress"
    {
      int hold = pc_glue_ui_hold(PC_HOLD_SAVE);

      scope_save_view_item_file(VIEW_TYPE_PICTURE);
      pc_glue_ui_hold(hold);
    }
  }
  //Check if save or delete wave button is touched
  else if((ytouch >= 423) && (ytouch <= 477))
  {
    //Check on wave view state (Scope mode) for which button is shown
    if(!scopesettings.waveviewmode)
    {
      //Save wave so highlight that button if touched
      scope_save_wave_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Button back to inactive state
      scope_save_wave_button(0);

      //Save the waveform on the SD card; fw 0.2.69: in Frequency response the sweep table as CSV. fw 0.2.78: a host
      //request meanwhile gets E_BUSY "file save in progress"
      int hold = pc_glue_ui_hold(PC_HOLD_SAVE);

      if(bode_view_active())
        bode_view_save();
      else
        scope_save_view_item_file(VIEW_TYPE_WAVEFORM);

      pc_glue_ui_hold(hold);
    }
    else
    {
      //Delete wave so highlight that button if touched
      scope_delete_wave_button(1);

      //Wait until touch is released
      tp_i2c_wait_for_touch_release();

      //Button back to inactive state
      scope_delete_wave_button(0);

      //Ask user for confirmation of the delete
      if(handle_confirm_delete() == VIEW_CONFIRM_DELETE_YES)
      {
        //Need to remove this item from the thumbnails
        scope_remove_item_from_thumbnails(1);

        //save the thumbnail file
        scope_save_thumbnail_file();

        //No more items available then return
        if(viewavailableitems == 0)
        {
          //Signal need to return to main item view mode
          scopesettings.waveviewmode = 0;
        }
        else
        {
          //Otherwise need to see if there is a next image to display or the previous one needs to be used
          if(viewcurrentindex >= viewavailableitems)
          {
            //Use the last available one
            //Index starts on zero, available items on 1
            viewcurrentindex = viewavailableitems - 1;
          }

          //Load the waveform data for the newly selected item
          if(scope_load_trace_data() == VIEW_TRACE_LOAD_ERROR)
          {
            //On error signal need to return to main item view mode
            scopesettings.waveviewmode = 0;
          }
        }
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_right_volts_div_menu_touch(void)
{
  //fw 0.2.56: page two is the CTRL page (ctrl_page.c); below, the waveform viewer's page and the DC shift pages
  if((scopesettings.rightmenustate == 1) && !scopesettings.waveviewmode)
  {
    ctrl_page_touch();
    return;
  }

  //Check if channel 1 V+ button is touched
  if((ytouch >= 93) && (ytouch <= 155))//78 - 140
  {
    //Highlight the button if touched
    scope_channel_sensitivity_control(&scopesettings.channel1, 0, 1);

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_channel_sensitivity_control(&scopesettings.channel1, 0, 0);

    //Only if channel is enabled
    if(scopesettings.channel1.enable)
    {
    //--------------------------------------------------------------------------    
      if(scopesettings.rightmenustate == 1)
      {
        //Check if setting not already on max
        if(scopesettings.channel1.displayvoltperdiv < 6)
        {
            //Step up to the next setting. (Lowering the setting)
            scopesettings.channel1.displayvoltperdiv++;

            //Show the change on the screen
            scope_channel_settings(&scopesettings.channel1, 0);

            //Only update the FPGA in run mode
            //For waveform view mode the stop state is forced and can't be changed
            if(scopesettings.runstate) match_volt_per_div_settings(&scopesettings.channel1);
        }
      }
    //--------------------------------------------------------------------------
      if(scopesettings.rightmenustate == 2)
      {//change value for - center position on signal
          if (scopesettings.channel1.dc_shift_center < 200) scopesettings.channel1.dc_shift_center++;
          scope_channel_dcshift_value();
          //scope_do_auto_setup();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 3)
      {//change value for - size of signal
          if (scopesettings.channel1.dc_shift_size < 250) scopesettings.channel1.dc_shift_size++;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 4)
      {//change value for - measurement value of signal
          if (scopesettings.channel1.dc_shift_value < 450) scopesettings.channel1.dc_shift_value++;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
    }
  }
  //Check if channel 1 V- button is touched
  else if((ytouch >= 178) && (ytouch <= 238))//163-223
  {
    //Highlight the button if touched
    scope_channel_sensitivity_control(&scopesettings.channel1, 1, 1);

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_channel_sensitivity_control(&scopesettings.channel1, 1, 0);

    //Only if channel is enabled
    if(scopesettings.channel1.enable)
    {
      if(scopesettings.rightmenustate == 1)
      {
        //Check if setting not already on min
        if(scopesettings.channel1.displayvoltperdiv > 0)
        {
            //Original code has some limit on updating when the scope is not running on the time base setting or a previous stored volts per div setting
            //The latter maybe has to do with a stored wave that is being looked at
            //For now just allowing constant change within the max limit

            //Step down to the next setting. (Increasing the setting)
            scopesettings.channel1.displayvoltperdiv--;

            //Show the change on the screen
            scope_channel_settings(&scopesettings.channel1, 0);

            //Only update the FPGA in run mode
            //For waveform view mode the stop state is forced and can't be changed
            if(scopesettings.runstate) match_volt_per_div_settings(&scopesettings.channel1);
        }
      }
    //--------------------------------------------------------------------------
      if(scopesettings.rightmenustate == 2)
      {//change value for - center position on signal
          if (scopesettings.channel1.dc_shift_center > 100) scopesettings.channel1.dc_shift_center--;
          scope_channel_dcshift_value();
          //scope_do_auto_setup();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 3)
      {//change value for - size of signal
          if (scopesettings.channel1.dc_shift_size > 150) scopesettings.channel1.dc_shift_size--;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 4)
      {//change value for - measurement value of signal
          if (scopesettings.channel1.dc_shift_value > 300) scopesettings.channel1.dc_shift_value--;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
    }
  }
 
  //Check if channel 2 V+ button is touched
  else if((ytouch >= 258) && (ytouch <= 320))
  {
    //Highlight the button if touched
    scope_channel_sensitivity_control(&scopesettings.channel2, 0, 1);

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_channel_sensitivity_control(&scopesettings.channel2, 0, 0);

    //Only if channel is enabled
    if(scopesettings.channel2.enable)
    {
    //--------------------------------------------------------------------------    
      if(scopesettings.rightmenustate == 1)
      {
        //Check if setting not already on max
        if(scopesettings.channel2.displayvoltperdiv < 6)
        {
            //Step up to the next setting. (Lowering the setting)
            scopesettings.channel2.displayvoltperdiv++;

            //Show the change on the screen
            scope_channel_settings(&scopesettings.channel2, 0);

            //Only update the FPGA in run mode
            //For waveform view mode the stop state is forced and can't be changed
            if(scopesettings.runstate) match_volt_per_div_settings(&scopesettings.channel2);
        }
      }
    //--------------------------------------------------------------------------
      if(scopesettings.rightmenustate == 2)
      {//change value for - center position on signal
          if (scopesettings.channel2.dc_shift_center < 200) scopesettings.channel2.dc_shift_center++;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 3)
      {//change value for - size of signal
          if (scopesettings.channel2.dc_shift_size < 250) scopesettings.channel2.dc_shift_size++;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 4)
      {//change value for - measurement value of signal
          if (scopesettings.channel2.dc_shift_value < 450) scopesettings.channel2.dc_shift_value++;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
    }
  }
  //Check if channel 2 V- button is touched
  else if((ytouch >= 343) && (ytouch <= 403))
  {
    //Highlight the button if touched
    scope_channel_sensitivity_control(&scopesettings.channel2, 1, 1);

    //Wait until touch is released
    tp_i2c_wait_for_touch_release();

    //Button back to inactive state
    scope_channel_sensitivity_control(&scopesettings.channel2, 1, 0);

    //Only if channel is enabled
    if(scopesettings.channel2.enable)
    {
      if(scopesettings.rightmenustate == 1)
      {
        //Check if setting not already on min
        if(scopesettings.channel2.displayvoltperdiv > 0)
        {
            //Original code has some limit on updating when the scope is not running on the time base setting or a previous stored volts per div setting
            //The latter maybe has to do with a stored wave that is being looked at
            //For now just allowing constant change within the max limit

            //Step down to the next setting. (Increasing the setting)
            scopesettings.channel2.displayvoltperdiv--;

            //Show the change on the screen
            scope_channel_settings(&scopesettings.channel2, 0);

            //Only update the FPGA in run mode
            //For waveform view mode the stop state is forced and can't be changed
            if(scopesettings.runstate) match_volt_per_div_settings(&scopesettings.channel2);
        }
      }
    //--------------------------------------------------------------------------
      if(scopesettings.rightmenustate == 2)
      {//change value for - center position on signal
          if (scopesettings.channel2.dc_shift_center > 100) scopesettings.channel2.dc_shift_center--;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 3)
      {//change value for - size of signal
          if (scopesettings.channel2.dc_shift_size > 150) scopesettings.channel2.dc_shift_size--;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
      if(scopesettings.rightmenustate == 4)
      {//change value for - measurement value of signal
          if (scopesettings.channel2.dc_shift_value > 300) scopesettings.channel2.dc_shift_value--;
          scope_channel_dcshift_value();
      }
    //-------------------------------------------------------------------------- 
    }
  }
  
  //-**********************************************************************************************************
  /*
  //Check if channel 2 V+ button is touched
  else if((ytouch >= 258) && (ytouch <= 320))
  {

    if (scopesettings.channel1.dcoffset<1000) scopesettings.channel1.dcoffset+=5; 
    //Wait until touch is released
    tp_i2c_wait_for_touch_release();
    fpga_set_channel_offset(&scopesettings.channel1);
    
    //scopesettings.channel1.traceposition -= scopesettings.channel1.dcoffset/2;


  }
  //Check if channel 2 V- button is touched
  else if((ytouch >= 343) && (ytouch <= 403))
  {
    if (scopesettings.channel1.dcoffset>-1000) scopesettings.channel1.dcoffset-=5; 
    //Wait until touch is released
    tp_i2c_wait_for_touch_release();
    fpga_set_channel_offset(&scopesettings.channel1);
    
    //scopesettings.channel1.traceposition += scopesettings.channel1.dcoffset/2;

   
  }
   */
  
  //-**********************************************************************************************************
  //Check if 50% trigger or show grid button is touched or calibration value menu
  else if((ytouch >= 423) && (ytouch <= 477))
  {
    //Check on wave view state (Scope mode) for which button is shown
    if(!scopesettings.waveviewmode)
    {   
      if(scopesettings.rightmenustate == 1)
      {
        //50% trigger so highlight that button if touched
        scope_50_percent_trigger_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Button back to inactive state
        scope_50_percent_trigger_button(0);

        //Only when scope is running
        if(scopesettings.runstate)//ok
            {
            //Set the trigger level for the active trigger channel
            scope_do_50_percent_trigger_setup();
            }
      }else
      //-------------------------------------------------------
      if(scopesettings.rightmenustate == 2)
      {
        //Show next so highlight that button if touched
        scope_next_cal_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Button back to inactive state
        scope_next_cal_button(0);
      
        //Switch to next page
        scopesettings.rightmenustate = 3;
        scope_channel_dcshift_value();
      }else    
      //-------------------------------------------------------
      if(scopesettings.rightmenustate == 3)
      {
        //Show next so highlight that button if touched
        scope_next_cal_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Button back to inactive state
        //scope_next_cal_button(0);
        //Button back to inactive state
        scope_end_cal_button(0);
      
        //Switch to next page
        scopesettings.rightmenustate = 4;
        scope_channel_dcshift_value();
      }else 
      //-------------------------------------------------------
      if(scopesettings.rightmenustate == 4)
      {
        //Show end so highlight that button if touched
        scope_end_cal_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Button back to inactive state
        scope_end_cal_button(0);
        
        //save calibration data
        scope_save_input_calibration_data();
        
        //end of dc shift calibration, flag to zero
        //dc_shift_cal = 0;
      
        //Switch to normal right scope menu
        scopesettings.rightmenustate = 0;
        
        //Display the changed state
        scope_setup_right_control_menu();
      }
      //------------------------------------------------------------------
      
      
      
    }
    else
    { 
      if(scopesettings.rightmenustate == 1)
      {
        //Show grid so highlight that button if touched
        scope_show_grid_button(1);

        //Wait until touch is released
        tp_i2c_wait_for_touch_release();

        //Button back to inactive state
        scope_show_grid_button(0);
      
        //Toggle the gird enable status
        scopesettings.gridenable ^= 1;
      }
      
    }   
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

//fw 0.2.65 (Measurements v2): the MEASURES menu's touch loop (ui_msm_hit). A cell acts on touch and the menu stays; a
//touch outside the box closes it (consumed). An item toggles (turned on it goes last in the row, ui_ms_toggle); Select
//All keeps the chosen order and appends the rest in table order; Clear empties the block; a source cell picks the
//block's source (Scope: the channel or a reference trace); Hide toggles the block. After a change every cell is drawn
//again (the places shown on the other items move), over their own backgrounds so nothing flashes.
void handle_measures_menu_touch(void)
{
  ui_msm_cell cells[UI_MSM_CELLS_MAX], c;
  int    mode = scope_measures_mode(), len, n, i, r;
  uint8 *rank;

  n = ui_msm_cells(mode, cells);

  //Stay in the menu as long as there is no touch outside the menu
  while(1)
  {
    //Scan the touch panel for touch
    tp_i2c_read_status();

    //fw 0.2.78: a host request closes the menu as a touch outside does (protocol 1.23)
    if(PC_UI_YIELD())
      return;

    if(!havetouch)
      continue;

    r = ui_msm_hit(mode, xtouch, ytouch, &c);

    if(r < 0)
    {
      //Touch outside the menu so wait until touch is released and then quit
      tp_i2c_wait_for_touch_release();
      return;
    }

    if(r > 0)
    {
      rank = scope_measures_ranks(mode, c.group);
      len  = scope_measures_list_len(mode, c.group);

      switch(c.what)
      {
        case UI_MSC_ITEM:   ui_ms_toggle(rank, len, c.index); break;
        case UI_MSC_ALL:    ui_ms_select_all(rank, len);      break;
        case UI_MSC_CLEAR:  ui_ms_clear(rank, len);           break;

        case UI_MSC_HIDE:
          if(c.group)
            scopesettings.hide_values_CH2 ^= 1;
          else
            scopesettings.hide_values_CH1 ^= 1;
          break;

        case UI_MSC_SOURCE:
          if(c.group)
            scopesettings.source2_measures = c.index;
          else
            scopesettings.source1_measures = c.index;
          break;

        default:
          break;
      }

      for(i = 0; i < n; i++)
        scope_measures_menu_cell(mode, &cells[i]);

      //fw 0.2.71: Frequency response's cards left of the menu follow at once
      if(mode == UI_MS_BODE)
        bode_view_cards_refresh();
    }

    //Wait until touch is released before checking on a new position
    tp_i2c_wait_for_touch_release();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

const TOUCHCOORDS view_item_touch_coords[16] =
{
  {  4, 178,   4, 116}, {186, 360,   4, 116}, {368, 542,   4, 116}, {550, 726,   4, 116},
  {  4, 178, 124, 236}, {186, 360, 124, 236}, {368, 542, 124, 236}, {550, 726, 124, 236},
  {  4, 178, 244, 356}, {186, 360, 244, 356}, {368, 542, 244, 356}, {550, 726, 244, 356},
  {  4, 178, 364, 476}, {186, 360, 364, 476}, {368, 542, 364, 476}, {550, 726, 364, 476},
};

//----------------------------------------------------------------------------------------------------------------------------------

void handle_view_mode_touch(void)
{
  int32 index;
  int32 found;

  //Stay in this mode as long as the return is not touched
  while(1)
  {
    //Read the touch panel status
    tp_i2c_read_status();

    //fw 0.2.78: a host request leaves the file browser as Return does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      scope_preset_values();
      return;
    }

    //Check if the panel is touched
    if(havetouch)
    {
      //Check if touch is in right menu area.
      if((xtouch >= 730))
      {
        //Check if the return button is touched
        if((ytouch >= 4) && (ytouch <= 76))
        {
          //Highlight the button to show it is touched
          scope_return_button(1);
          
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();

          //Back to inactive state
          scope_return_button(0);
          
          //Set other values and clear view area
          scope_preset_values();
          
          //DOne with item view so return
          return;
        }
        //Else check if the select all button is touched
        else if((ytouch >= 84) && (ytouch <= 156))
        {
          //Highlight the button
          scope_select_all_button(1);
          
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
          
          //Deselect the possibly enabled select button
          scope_select_button(0);

          //Depending on the state take action
          if((viewselectmode == VIEW_SELECT_NONE) || (viewselectmode == VIEW_SELECT_INDIVIDUAL))
          {
            //Enable the button
            scope_select_all_button(2);

            //Switch to all items selected mode
            viewselectmode = VIEW_SELECT_ALL;

            //Make sure all selected signs are displayed
            for(index=0;index<viewitemsonpage;index++)
            {
              //Check if the selected sign is not selected
              if(viewitemselected[index] == VIEW_ITEM_NOT_SELECTED)
              {
                //If so make sure it gets displayed
                viewitemselected[index] = VIEW_ITEM_SELECTED_NOT_DISPLAYED;
              }
            }
          }
          else
          {
            //Make sure the button is no longer highlighted
            scope_select_all_button(0);

            //Switch to all items deselected mode
            viewselectmode = VIEW_SELECT_NONE;

            //Not in a selected mode any more so clear the active selected signs
            for(index=0;index<viewitemsonpage;index++)
            {
              //Check if the selected sign is actually displayed
              if(viewitemselected[index] == VIEW_ITEM_SELECTED_DISPLAYED)
              {
                //If so make sure it gets cleared
                viewitemselected[index] = VIEW_ITEM_NOT_SELECTED_DISPLAYED;
              }
            }
          }

          //Update the selected signs
          scope_display_selected_signs();
        }
        //Else check if the select button is touched
        else if((ytouch >= 164) && (ytouch <= 236))
        {
          //Highlight the button to show it is touched
          scope_select_button(1);
            
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
          
          //Deselect the possibly highlighted select all button
          scope_select_all_button(0);

          //Check if all items are selected or select mode is on
          if(viewselectmode)
          {
            //Make sure the select button is not highlighted
            scope_select_button(0);

            //Switch to all items deselected mode
            viewselectmode = VIEW_SELECT_NONE;

            //Not in a selected mode any more so clear the active selected signs
            for(index=0;index<viewitemsonpage;index++)
            {
              //Check if the selected sign is actually displayed
              if(viewitemselected[index] == VIEW_ITEM_SELECTED_DISPLAYED)
              {
                //If so make sure it gets cleared
                viewitemselected[index] = VIEW_ITEM_NOT_SELECTED_DISPLAYED;
              }
            }
          }
          else
          {
            //Nothing selected so switch to individual select mode
            viewselectmode = VIEW_SELECT_INDIVIDUAL;

            //Highlight the select button
            scope_select_button(2);
          }

          //Update the selected signs
          scope_display_selected_signs();
        }
        //Else check if the delete button is touched
        else if((ytouch >= 244) && (ytouch <= 316))
        {
          //Highlight the button to show it is touched
          scope_delete_button(1);
            
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
          
          //Back to inactive button
          scope_delete_button(0);
          
          //Need to see if there are items selected for delete
          if(viewselectmode)
          {
            //A select mode is active so check the list to see if there are items selected
            //This is not done in the original code. Activate the single select mode and without selecting an item press delete. The confirm menu is shown
            for(index=0,found=0;index<viewitemsonpage;index++)
            {
              //Check if the current item is selected
              if(viewitemselected[index] == VIEW_ITEM_SELECTED_DISPLAYED)
              {
                //Signal there is at least one item to delete
                found = 1;
                break;
              }
            }

            //Check if there is an item to delete
            if(found)
            {
              //Ask the user if the items should be deleted
              if(handle_confirm_delete() == VIEW_CONFIRM_DELETE_YES)
              {
                //User opted for delete so do this for the selected items
                //Start with the last item on the page to avoid problems with the file number list being modified
                for(index=viewitemsonpage-1;index>=0;index--)
                {
                  //Check if the current item is selected
                  if(viewitemselected[index] == VIEW_ITEM_SELECTED_DISPLAYED)
                  {
                    //Set the current index for this file
                    viewcurrentindex = index + (viewpage * VIEW_ITEMS_PER_PAGE);

                    //Remove the current item from the thumbnails and delete the item from disk
                    scope_remove_item_from_thumbnails(1);
                  }
                }

                //Save the thumbnail file
                scope_save_thumbnail_file();

                //Clear the select flags
                memset(viewitemselected, VIEW_ITEM_NOT_SELECTED, VIEW_ITEMS_PER_PAGE);

                //Clear the select state and the button highlights
                viewselectmode = VIEW_SELECT_NONE;
                scope_select_all_button(0);
                scope_select_button(0);

                //Redisplay the thumbnails
                scope_initialize_and_display_thumbnails();
              }
            }
          }
        }
        //Else check if the page up button is touched
        else if((ytouch >= 324) && (ytouch <= 396))
        {
          //Highlight the button to show it is touched
          scope_page_up_button(1);
            
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
          
          //Back to inactive button
          scope_page_up_button(0);
          
          //Check if there is a previous page
          if(viewpage > 0)
          {
            //Clear the select mode
            viewselectmode = 0;

            //Deselect the possibly highlighted buttons
            scope_select_all_button(0);
            scope_select_button(0);

            //Clear the item selected flags
            memset(viewitemselected, 0, VIEW_ITEMS_PER_PAGE);

            //Select the previous page
            viewpage--;

            //Display the previous page of thumbnails
            scope_display_thumbnails();
          }
        }
        //Else check if the page down button is touched
        else if((ytouch >= 404) && (ytouch <= 476))
        {
          //Highlight the button to show it is touched
          scope_page_down_button(1);
            
          //Wait until touch is released
          tp_i2c_wait_for_touch_release();
          
          //Back to inactive button
          scope_page_down_button(0);
          
          //Check if there is a next page
          if(viewpage < viewpages)
          {
            //Clear the select mode
            viewselectmode = 0;

            //Deselect the possibly highlighted buttons
            scope_select_all_button(0);
            scope_select_button(0);

            //Clear the item selected flags
            memset(viewitemselected, 0, VIEW_ITEMS_PER_PAGE);

            //Select the next page
            viewpage++;

            //Display the next page of thumbnails
            scope_display_thumbnails();
          }
        }
      }
      else
      {
        //Start with no item touched yet and check on first item
        found = 0;
        index = 0;

        //Do until found or last available item checked
        while(index < viewitemsonpage)
        {
          //Check if touch is on this item
          if((xtouch >= view_item_touch_coords[index].x1) && (xtouch <= view_item_touch_coords[index].x2) &&
             (ytouch >= view_item_touch_coords[index].y1) && (ytouch <= view_item_touch_coords[index].y2))
          {
            //Signal item found
            found = 1;

            //Quit the loop when found
            break;
          }

          //Next item
          index++;
        }

        //Need to wait for touch to release before selecting or opening the item
        tp_i2c_wait_for_touch_release();
        
        //Check if touch was on one of the available items
        if(found)
        {
          //Check if in a select mode
          if(viewselectmode)
          {
            //Yes, so toggle the select on this item
            if(viewitemselected[index] == VIEW_ITEM_SELECTED_DISPLAYED)
            {
              //If displayed make sure it gets cleared
              viewitemselected[index] = VIEW_ITEM_NOT_SELECTED_DISPLAYED;
            }
            else
            {
              //If not make sure it gets displayed
              viewitemselected[index] = VIEW_ITEM_SELECTED_NOT_DISPLAYED;
            }

            //Update the selected signs
            //Could improve on this with a single sign update function
            scope_display_selected_signs();
          }
          else
          {
            //Set the current index for this file
            viewcurrentindex = index + (viewpage * VIEW_ITEMS_PER_PAGE);

            //Loaded ok, so take action based on the type of the opened item
            if(viewtype == VIEW_TYPE_PICTURE)
            {
              //Load the bitmap in the screen memory
              if(scope_load_bitmap_data() == VIEW_BITMAP_LOAD_OK)
              {
                //On initialization draw the bottom menu bar with a save of the background
                scope_setup_bottom_file_menu(VIEW_BOTTON_MENU_INIT);

                //Handle the touch
                handle_picture_view_touch();
              }
            }
            else
            {
              //Try to load the trace data for the file indicated by the current index and view type
              if(scope_load_trace_data() == VIEW_TRACE_LOAD_OK)
              {
                //waveform view needs to be handled here as long as the flag is set
                while(scopesettings.waveviewmode)
                {
                  //fw 0.2.78: a host request leaves the waveform as the return button does (protocol 1.23); the
                  //file browser around it then leaves too
                  if(PC_UI_YIELD())
                  {
                    scope_menu_button(0);
                    scopesettings.waveviewmode = 0;
                    break;
                  }

                  //Use the main touch handler for waveform view
                  touch_handler();

                  //Update the display as needed
                  scope_display_trace_data();

                  //Monitor the battery status
                  battery_check_status();
                }
              }
            }

            //Display the file actions menu on the right side of the screen
            scope_setup_right_file_menu();

            //Display the available thumbnails for the current view type
            scope_initialize_and_display_thumbnails();
          }
        }
      }
    }

    //Need to wait for touch to release before checking again
    tp_i2c_wait_for_touch_release();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void handle_picture_view_touch(void)
{
  //Stay in this mode as long as the return is not touched
  while(1)
  {
    //Read the touch panel status
    tp_i2c_read_status();

    //fw 0.2.78: a host request leaves the picture as Return does (protocol 1.23)
    if(PC_UI_YIELD())
    {
      viewbottommenustate = VIEW_BOTTON_MENU_HIDE;
      return;
    }

    //Check if the panel is touched
    if(havetouch)
    {
      //Check if bottom view menu is shown
      if(viewbottommenustate & VIEW_BOTTON_MENU_SHOW)
      {
        //Check if touch outside of the menu bar
        if(ytouch < 420)
        {
          //Wait until touch is released before taking action
          tp_i2c_wait_for_touch_release();
          
          //Toggle the menu visibility
          viewbottommenustate ^= VIEW_BOTTON_MENU_SHOW;

          //Update accordingly
          scope_setup_bottom_file_menu(viewbottommenustate);
        }
        else
        {
          //Check if in return button region
          if((xtouch > 40) && (xtouch < 160))
          {
            //Highlight the button
            scope_bmp_return_button(1);
            
            //Wait until touch is released before taking action
            tp_i2c_wait_for_touch_release();

            //Back to inactive state
            scope_bmp_return_button(0);
            
            //Clear the menu state. This is needed for the emulator
            viewbottommenustate = VIEW_BOTTON_MENU_HIDE;
            
            //Just return
            return;
          }
          //Check if in delete button region
          else if((xtouch > 240) && (xtouch < 360))
          {
            //Highlight the button
            scope_bmp_delete_button(1);
            
            //Wait until touch is released before taking action
            tp_i2c_wait_for_touch_release();

            //Back to inactive state
            scope_bmp_delete_button(0);
            
            //Ask user for confirmation of the delete
            if(handle_confirm_delete() == VIEW_CONFIRM_DELETE_YES)
            {
              //Need to remove this item from the thumbnails
              scope_remove_item_from_thumbnails(1);

              //save the thumbnails file
              scope_save_thumbnail_file();

              //No more items available then return
              if(viewavailableitems == 0)
              {
                return;
              }

              //Then need to see if there is a next image to display or the previous one needs to be used
              if(viewcurrentindex >= viewavailableitems)
              {
                //Use the last available one
                //Index starts on zero, available items on 1
                viewcurrentindex = viewavailableitems - 1;
              }

              //Display the new picture item
              if(scope_display_picture_item() == VIEW_BITMAP_LOAD_ERROR)
              {
                //On error return to main view
                return;
              }
            }
          }
          //Check if in previous item button region
          else if((xtouch > 440) && (xtouch < 560))
          {
            //Highlight the button
            scope_bmp_previous_button(1);
            
            //Wait until touch is released before taking action
            tp_i2c_wait_for_touch_release();

            //Back to inactive state
            scope_bmp_previous_button(0);
            
            //Check if not on first item
            if(viewcurrentindex > 0)
            {
              //Select the previous one
              viewcurrentindex--;

              //Display the new picture item
              if(scope_display_picture_item() == VIEW_BITMAP_LOAD_ERROR)
              {
                //On error return to main view
                return;
              }
            }
          }
          //Check if in next item button region
          else if((xtouch > 640) && (xtouch < 760))
          {
            //Highlight the button
            scope_bmp_next_button(1);
            
            //Wait until touch is released before taking action
            tp_i2c_wait_for_touch_release();

            //Back to inactive state
            scope_bmp_next_button(0);
            
            //Check if not on last item
            //Index starts on zero, available items on 1
            if(viewcurrentindex < (viewavailableitems - 1))
            {
              //Select the next one
              viewcurrentindex++;

              //Display the new picture item
              if(scope_display_picture_item() == VIEW_BITMAP_LOAD_ERROR)
              {
                //On error return to main view
                return;
              }
            }
          }
        }
      }
      else
      {
        //If not shown, show it again
        scope_setup_bottom_file_menu(VIEW_BOTTON_MENU_SHOW);
      }
    }

    //Need to wait for touch to release before checking again
    tp_i2c_wait_for_touch_release();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

int32 handle_confirm_delete(void)
{
  //fw 0.2.55: the confirmation dialog (ui_settings_ui.c); was 74 x 50 px NO / YES 11 px apart
  return ui_confirm(&ui_dlg_delete, 1) ? VIEW_CONFIRM_DELETE_YES : VIEW_CONFIRM_DELETE_NO;
}

//----------------------------------------------------------------------------------------------------------------------------------

void close_open_menus(uint32 closemain)
{
  //uint8 tmp;                            //for diagnostic menu
  //boot_menu_start = ptr[0] & 0x07;      //choice 1 for fnirsi firmware, 0 for peco firmware, 2 for FEL mode, 4 view choice menu
  
  //Only one of these menu's will be opened at any time

  //Check if the screen brightness slider is open
  if(screenbrightnessopen)
  {
    //Set the button inactive
    scope_system_settings_screen_brightness_item(0);

    //Restore the screen under the screen brightness slider
    display_set_source_buffer(displaybuffer3);
    display_copy_rect_to_screen(S_BRIGHTNESS_MENU_XPOS, S_BRIGHTNESS_MENU_YPOS, S_BRIGHTNESS_MENU_WIDTH, S_BRIGHTNESS_MENU_HEIGHT);

    //Signal it is closed
    screenbrightnessopen = 0;
  }
  //Check if the grid brightness slider is open
  else if(gridbrightnessopen)
  {
    //Set the button inactive
    scope_system_settings_grid_brightness_item(0);

    //Restore the screen under the grid brightness slider
    display_set_source_buffer(displaybuffer3);
    display_copy_rect_to_screen(G_BRIGHTNESS_MENU_XPOS, G_BRIGHTNESS_MENU_YPOS, G_BRIGHTNESS_MENU_WIDTH, G_BRIGHTNESS_MENU_HEIGHT);

    //Signal it is closed
    gridbrightnessopen = 0;
  }
  //Check if the RTC settings menu is open
  else if(RTCsettingsopen)
  {
    //Set the button inactive
    scope_system_settings_RTC_settings_item(0);

    //Restore the screen under the RTC_settings_menu
    display_set_source_buffer(displaybuffer3);
    display_copy_rect_to_screen(RTC_MENU_XPOS, RTC_MENU_YPOS, RTC_MENU_WIDTH, RTC_MENU_HEIGHT);    //? display_draw_rect(395, 106, 200, 353)
   
    //Signal it is closed
    RTCsettingsopen = 0;
  }

  //Check if system settings menu has been opened and needs to be closed
  if(closemain && systemsettingsmenuopen)
  {
    //Restore the screen under the system settings menu when done
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(SYSTEM_MENU_XPOS, SYSTEM_MENU_YPOS, SYSTEM_MENU_WIDTH, SYSTEM_MENU_HEIGHT);//353

    //Clear the flag so it will be opened next time
    systemsettingsmenuopen = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void close_open_channel_menus(PCHANNELSETTINGS settings, uint32 closemain)
{
  //Only one of these menu's will be opened at any time

  //Check if the ref menu is open
  if(refmenuopen)
  {
    //Set the button inactive
    scope_channel_ref_menu_item(settings, 0);

    //Restore the screen under the ref menu
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(settings->menuxpos + CH_REF_MENU_XPOS, CH_REF_MENU_YPOS, CH_REF_MENU_WIDTH, CH_REF_MENU_HEIGHT);

    //Signal it is closed
    refmenuopen = 0;
  }
  //Check if the math menu is open
  else if(mathmenuopen)
  {
    //Set the button inactive
    scope_channel_math_menu_item(settings, 0);

    //Restore the screen under the math menu
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(settings->menuxpos + CH_MATH_MENU_XPOS, CH_MATH_MENU_YPOS, CH_MATH_MENU_WIDTH, CH_MATH_MENU_HEIGHT);

    //Signal it is closed
    mathmenuopen = 0;
  }

  //Check if channel settings menu has been opened and needs to be closed
  if(closemain && channelmenuopen)
  {
    //Restore the screen under the channel settings menu when done
    display_set_source_buffer(displaybuffer1);
    display_copy_rect_to_screen(settings->menuxpos, CH_MENU_YPOS, CH_MENU_WIDTH, CH_MENU_HEIGHT);//353

    //Clear the flag so it will be opened next time
    channelmenuopen = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void close_open_trigger_menus( uint32 closemain)
{
  //ui-type: the keypad for N under the menu first
  if(trignmenuopen)
  {
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT);
    trignmenuopen = 0;
  }

  //Check if trigger menu has been opened and needs to be closed
  if(closemain && triggermenuopen)
  {
    //Restore the screen under the trigger menu when done (ui-type: the largest height, as saved)
    display_set_source_buffer(displaybuffer1);
    display_copy_rect_to_screen(TRIGGER_MENU_XPOS, TRIGGER_MENU_YPOS, TRIGGER_MENU_WIDTH, TRIGGER_MENU_SAVE_H);

    //Clear the flag so it will be opened next time
    triggermenuopen = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
void close_open_gen_menus( uint32 closemain)
{
  //Check if the keyboard menu is open
  if(freqmenuopen)
  {
    //Set the button inactive
    //scope_channel_ref_menu_item(settings, 0);

    //Restore the screen under the keyboard menu
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT);

    //Signal it is closed
    freqmenuopen = 0;
  }
    //Check if the keyboard menu is open
    else if(dutymenuopen)
  {
    //Set the button inactive
    //scope_channel_ref_menu_item(settings, 0);

    //Restore the screen under the keyboard menu
    display_set_source_buffer(displaybuffer2);
    display_copy_rect_to_screen(KEY_MENU_XPOS, KEY_MENU_YPOS, KEY_MENU_WIDTH, KEY_MENU_HEIGHT);

    //Signal it is closed
    dutymenuopen = 0;
  }

  //Check if generator menu has been opened and needs to be closed
  if(closemain && genmenuopen)
  {
    //Restore the screen under the generator menu when done
    display_set_source_buffer(displaybuffer1);
    display_copy_rect_to_screen(GEN_MENU_XPOS, GEN_MENU_YPOS, GEN_MENU_WIDTH, GEN_MENU_HEIGHT);//353

    //Clear the flag so it will be opened next time
    genmenuopen = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_trigger_point_position(void)
{
  int32 diff;
  int32 position;
  
  int32 positionlimitL;
  int32 positionlimitH;

  //Calculate the distance to move the setting with
  diff = xtouch - previousxtouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_trigger_point_position + diff;
  
  //positionlimitH = (((1500        ) * 727.0) / (727.0 / disp_xpos_per_sample))+15;
  //positionlimitL = (((1500 - 4000 ) * 727.0) / (727.0 / disp_xpos_per_sample));
  
 //   positionlimitH = (((1500        ) * 724.0) / (724.0 / disp_xpos_per_sample))+15;
  //positionlimitL = (((1500 - 4000 ) * 724.0) / (724.0 / disp_xpos_per_sample));
  
      //positionlimitH = (((1500       ) ) *( disp_xpos_per_sample))+15;
  //positionlimitL = ((-(scopesettings.totalsamples/2)+1000  ) * (disp_xpos_per_sample));
  //positionlimitL = ((2900-(scopesettings.totalsamples) ) * (disp_xpos_per_sample));
  
  positionlimitH = (((fpgasettings.settriggerpoint) )*( disp_xpos_per_sample));//+15;
  positionlimitL = (((-(fpgasettings.totalsamples-fpgasettings.settriggerpoint-1400)) ) * (disp_xpos_per_sample));//1450

  //Limit it on the trace portion of the screen (fw 0.2.64: long mode only; short mode uses the record's limits below,
  //1.00o5's 1400-sample margin stopped the window short of the record's end)
  if(scopesettings.long_mode)
  {
    if(position > positionlimitH)//712
    {
      //And not above 712;
      position = positionlimitH;
    }
        
    if(position < positionlimitL)//2
    {
      //So not below 2
      position = positionlimitL;
    }
  }
  
  //Update the current position
  scopesettings.triggerhorizontalposition = position;

  //fw 0.2.64: in short mode the limits come from the record (the window cannot scroll past either end of it)
  acq_view_clamp_pos();
  
  //if(!scopesettings.long_mode || scopesettings.triggermode) scopesettings.triggerhorizontalposition = position;
  
  //Scope mode & long time base & no auto mode - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode && scopesettings.triggermode) scope_preset_values();  
 
}

//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------
//******************************************************************************
/*
void change_channel_1_HW_offset(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with based on the display mode
  if(scopesettings.xymodedisplay == 0)
  {
    //For normal display use y displacement
    diff = ytouch - previousytouch;
  }
  else
  {
    //For x-y mode display use x displacement
    diff = previousxtouch - xtouch;
  }

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_channel_1_offset - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.channel1.traceposition = position;

  //Write it to the FPGA
  fpga_set_channel_offset(&scopesettings.channel1);
}
*/
//**************************************************************************************
//-----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

void change_channel_1_offset(int32 delta)
{
  //fw 0.2.54: the drag distance comes from touch_handler (y in normal display, x in X-Y)
  scopesettings.channel1.traceposition = scope_trace_clamp((int32)previous_channel_1_offset + delta);

  //Write it to the FPGA
  fpga_set_channel_offset(&scopesettings.channel1);

  //Skope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values();
}

//----------------------------------------------------------------------------------------------------------------------------------

void change_channel_2_offset(int32 delta)
{
  scopesettings.channel2.traceposition = scope_trace_clamp((int32)previous_channel_2_offset + delta);

  //Write it to the FPGA
  fpga_set_channel_offset(&scopesettings.channel2);

  //Scope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values();
}

//----------------------------------------------------------------------------------------------------------------------------------

void change_trigger_level_offset(int32 delta)
{
  scopesettings.triggerverticalposition = scope_trace_clamp((int32)previous_trigger_level_offset + delta);

  //Skope mode & long time base & no auto mode - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode && scopesettings.triggermode) scope_preset_values();

}

//----------------------------------------------------------------------------------------------------------------------------------

void move_left_time_cursor_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = xtouch - previousxtouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_left_time_cursor_position + diff;

  //Limit it on the trace portion of the screen and the right time cursor
  if(position < 3)
  {
    //So not below 3
    position = 3;
  }
  else if(position >= scopesettings.timecursor2position)
  {
    //And not right of the right cursor;
    position = scopesettings.timecursor2position - 1;
  }

  //Update the current position
  scopesettings.timecursor1position = position;
  
  //Skope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values(); 
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_right_time_cursor_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = xtouch - previousxtouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_right_time_cursor_position + diff;

  //Limit it on the trace portion of the screen and the left cursor
  if(position <= scopesettings.timecursor1position)
  {
    //So not left of the left cursor
    position = scopesettings.timecursor1position + 1;
  }
  else if(position > 726)//722
  {
    //And not beyond end of the screen;
    position = 726;//722
  }

  //Update the current position
  scopesettings.timecursor2position = position;
  
  //Skope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values();  
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_top_volt_cursor_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_top_volt_cursor_position + diff;//32

  //Limit it on the trace portion of the screen and not below the bottom cursor
  if(position < 49)//47  46
  {
    //So not below 47
    position = 49;//47
  }
  else if(position >= scopesettings.voltcursor2position)
  {
    //And not below the bottom cursor;
    position = scopesettings.voltcursor2position - 1;
  }

  //Update the current position
  scopesettings.voltcursor1position = position;
  
  //Skope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values(); 
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_bottom_volt_cursor_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_bottom_volt_cursor_position + diff;

  //Limit it on the trace portion of the screen and not above the top cursor
  if(position <= scopesettings.voltcursor1position)
  {
    //So not above the top cursor
    position = scopesettings.voltcursor1position + 1;
  }
  else if(position > 449)//447
  {
    //And not above 447;
    position = 449;
  }

  //Update the current position
  scopesettings.voltcursor2position = position;
  
  //Scope mode & long time base - set other values and clear view area
  if((!scopesettings.waveviewmode) && scopesettings.long_mode) scope_preset_values(); 
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref1_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref1_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref1.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref2_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref2_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref2.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref3_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref3_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref3.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref4_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref4_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref4.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref5_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref5_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref5.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref6_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref6_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref6.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref7_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref7_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref7.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void move_ref8_position(void)
{
  int32 diff;
  int32 position;

  //Calculate the distance to move the setting with
  diff = ytouch - previousytouch;

  //Make it based on move speed
  if(scopesettings.movespeed)
  {
    //For slow divide by 5
    diff /= 5;
  }

  //Calculate the new position
  position = (int32)previous_ch_ref8_position - diff;

  //Limit it on the trace portion of the screen
  if(position < 6)
  {
    //So not below 6
    position = 6;
  }
  else if(position > 396)//394
  {
    //And not above 394;
    position = 396;//394
  }

  //Update the current position
  scopesettings.ch_ref8.traceposition = position;
}

//----------------------------------------------------------------------------------------------------------------------------------

void match_volt_per_div_settings(PCHANNELSETTINGS settings)
{
  //Check if the sample volts per division differs from the display volts per division setting
  if(settings->samplevoltperdiv != settings->displayvoltperdiv)
  {
    //Match them if so and change them in the FPGA
    settings->samplevoltperdiv = settings->displayvoltperdiv;

    //Set the volts per div for this channel
    fpga_set_channel_voltperdiv(settings);

    //Since the DC offset is influenced set that too
    fpga_set_channel_offset(settings);

    //Wait 50ms to allow the circuit to settle
    timer0_delay(50);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void set_trigger_mode(uint32 mode)
{
  //Set the new mode
  scopesettings.triggermode = mode;

  //Set the new mode in the hardware
  fpga_set_trigger_mode();

  //Make sure the scope is running
  scopesettings.runstate = 1;//ok

  //Show this on the screen
  scope_run_stop_text();
  
  //Check in which state the right menu is in
  //Button back to inactive state
  if(scopesettings.rightmenustate == 0) scope_run_stop_button(0);

  //Match the volts per division settings for both channels
  //Is needed when vertical zoom has been used
  match_volt_per_div_settings(&scopesettings.channel1);
  match_volt_per_div_settings(&scopesettings.channel2);
  
  //Display this
  scope_trigger_mode_select();
  scope_trigger_settings(0);

}

//----------------------------------------------------------------------------------------------------------------------------------
