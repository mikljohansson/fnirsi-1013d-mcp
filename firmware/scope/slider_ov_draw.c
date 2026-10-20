//----------------------------------------------------------------------------------------------------------------------------------
// slider_ov_draw.c - drawing the slider row and its overview (see slider_ov_draw.h).
//----------------------------------------------------------------------------------------------------------------------------------

#include "slider_ov_draw.h"
#include "display_lib.h"
#include "fnirsi_1013d_scope.h"

void sov_draw_row(void)
{
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(SOV_X, SOV_Y, SOV_W, SOV_H);
}

void sov_draw_run(int32 x, int32 lo, int32 hi, uint32 full, uint32 dim, int32 wx, int32 ww)
{
  if((x < SOV_X) || (x > SOV_X + SOV_W - 1))
    return;
  if(lo < 0) lo = 0;
  if(hi > SOV_H - 1) hi = SOV_H - 1;
  if(lo > hi)
    return;

  display_set_fg_color(sov_inside(x, wx, ww) ? full : dim);
  display_draw_vert_line((uint32)x, (uint32)(SOV_Y + lo), (uint32)(SOV_Y + hi));
}

void sov_draw_curve(const sov_curve *cv, uint32 flag, int32 wx, int32 ww)
{
  int32 vmax, vmin, i, pi = -1, lo, hi;

  if(!sov_curve_fit(cv, &vmax, &vmin))
    return;

  for(i = 0; i < cv->n; i++)
  {
    int32 c = cv->col[i], r;

    if(!cv->have[i])
      continue;

    r = sov_curve_row(cv, i, vmax, vmin);

    if(!cv->ok[i])
    {
      sov_draw_run(c, r, r, flag, flag, wx, ww);
      pi = -1;
      continue;
    }

    if((pi >= 0) && sov_curve_joins(cv, pi, i))
    {
      int32 pc = cv->col[pi], pr = sov_curve_row(cv, pi, vmax, vmin), x, prev = pr;

      for(x = pc + 1; x <= c; x++)
      {
        int32 rr = sov_lerp(pc, pr, c, r, x);

        sov_join(prev, rr, &lo, &hi);
        sov_draw_run(x, lo, hi, cv->full, cv->dim, wx, ww);
        prev = rr;
      }
    }
    else
      sov_draw_run(c, r, r, cv->full, cv->dim, wx, ww);

    pi = i;
  }
}

void sov_draw_curves(const sov_curve *mag, const sov_curve *phase, uint32 flag, int32 wx, int32 ww)
{
  if(phase)
    sov_draw_curve(phase, flag, wx, ww);
  if(mag)
    sov_draw_curve(mag, flag, wx, ww);
}

void sov_draw_window(int32 wx, int32 ww)
{
  int32 l = sov_col(wx), r = sov_col(wx + ww - 1);
  int32 ml = (l > SOV_X) ? l - 1 : l, mr = (r < SOV_X + SOV_W - 1) ? r + 1 : r;

  //The margin: the track's top and bottom rows over the box and the columns beside it (on the track)
  display_set_fg_color(BLACK_COLOR);
  display_draw_horz_line(SOV_Y, (uint32)ml, (uint32)mr);
  display_draw_horz_line(SOV_Y + SOV_H - 1, (uint32)ml, (uint32)mr);
  if(ml < l)
    display_draw_vert_line((uint32)ml, SOV_Y, SOV_Y + SOV_H - 1);
  if(mr > r)
    display_draw_vert_line((uint32)mr, SOV_Y, SOV_Y + SOV_H - 1);

  //The box
  display_set_fg_color(WHITE_COLOR);
  display_draw_horz_line(SOV_WIN_Y, (uint32)l, (uint32)r);
  display_draw_horz_line(SOV_WIN_Y + SOV_WIN_H - 1, (uint32)l, (uint32)r);
  display_draw_vert_line((uint32)l, SOV_WIN_Y, SOV_WIN_Y + SOV_WIN_H - 1);
  display_draw_vert_line((uint32)r, SOV_WIN_Y, SOV_WIN_Y + SOV_WIN_H - 1);
}
