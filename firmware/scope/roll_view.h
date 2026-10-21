//----------------------------------------------------------------------------------------------------------------------------------
// roll_view.h - the Scope view in the long time bases (100 ms/div .. 50 s/div): the roll trace from the 0x153D logger stream
// (fw-roll; roll.h has the model, pc_hal_scope.c pc_glue_roll_* the stream). Replaces 1.00o5's
// scope_get_long_timebase_data / scope_check_long_trigger (test.c), which averaged live ADC pin reads 0x24 / 0x26.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ROLL_VIEW_H
#define ROLL_VIEW_H

//Main loop, long mode: stream, model, trace (the stream runs while the UI runs and stops when it stops)
void roll_view_pass(void);

//scope_preset_values in long mode: an empty record with the trigger state of the current settings
void roll_view_reset(void);

#endif /* ROLL_VIEW_H */
