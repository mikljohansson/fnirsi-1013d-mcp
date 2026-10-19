//----------------------------------------------------------------------------------------------------------------------------------
// ui_menu.c - the main menu's rows, hit areas and mode rule (see ui_menu.h). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_menu.h"

const ui_mm_label ui_mm_labels[UI_MM_ROWS] =
{
  { "Scope",       ""          },
  { "Spectrum",    "analyser"  },
  { "Saved",       "pictures"  },
  { "Saved",       "waveforms" },
  { "Settings",    ""          },
  { "Diagnostics", ""          },
  { "USB drive",   ""          },
};

uint32 ui_mm_row_top(int row)
{
  return UI_MM_TOP + (uint32)row * UI_MM_PITCH;
}

int ui_mm_row_at(uint32 x, uint32 y)
{
  if((x < UI_MM_X) || (x >= UI_MM_X + UI_MM_W) || (y < UI_MM_TOP) || (y >= UI_MM_TOP + UI_MM_H))
    return -1;

  return (int)((y - UI_MM_TOP) / UI_MM_PITCH);
}

int ui_mm_choose(int row, int spectrum_on, int roll_on, int phosphor_wanted, ui_mm_action *a)
{
  a->spectrum  = spectrum_on ? 1 : 0;
  a->phosphor  = (!spectrum_on && phosphor_wanted) ? 1 : 0;
  a->roll_off  = 0;
  a->warn_roll = 0;

  if(row == UI_MM_SCOPE)
  {
    a->spectrum = 0;
    a->phosphor = phosphor_wanted ? 1 : 0;
    return 1;
  }

  if(row == UI_MM_SPECTRUM)
  {
    a->spectrum  = 1;
    a->phosphor  = 0;
    a->roll_off  = roll_on ? 1 : 0;
    a->warn_roll = roll_on ? 1 : 0;
    return 1;
  }

  return 0;
}
