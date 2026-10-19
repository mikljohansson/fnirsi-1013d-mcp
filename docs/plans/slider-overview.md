# Slider-track overview: the whole record, spectrum or response under the window

Owner request (2026-10-19 20:00): draw a mini overview inside the slider track under the plot, where the data exists, so
the window shows where the view sits in the whole. In Scope it is the record's min / max envelope over the full record,
and the window is the part on screen. In Spectrum it is the spectrum over 0..fs/2, and the window is the shown span. In
Frequency response it is AUTO SET's overview sweep over 100 Hz .. 10 MHz, and the window is Start..Stop.

This is the design (ux-overview, 2026-10-19); fw 0.2.73 (fw-over) built it with the owner's decisions of section 10 F,
which override the drafts below where they differ (sections 2 and 9 are updated to the build; handoff and bench
walk-through: `firmware/AGENTS.md` section 49). The mockups are the acceptance reference (section 9). They
follow `docs/ui-guidelines.md`, the slider paragraphs of `docs/plans/ux-makeover.md` and `docs/plans/bode-ui.md`
section 2. Facts about the firmware are cited to the source files at fw 0.2.69. Estimates are marked as such.

## 1. Summary and recommendation

| Mode | What the track shows | Data source | Extra CPU per pass | Extra memory | Recommendation |
|---|---|---|---|---|---|
| Scope | min / max envelope of every enabled channel over the whole record, on the plot's own vertical scale | the record already in the trace buffers | one min / max pass over the record: about 0.06 ms at 3000 samples, about 0.33 ms at 16382 (estimate) | 2880 B | build it |
| Spectrum | the spectrum over the whole axis 0..fs/2 (capped at 25 MHz), on the plot's ref level and dB/div | the bins the FFT already computes: every pass transforms the whole axis, and the plot shows a slice of it | about 0.01 ms, a max over at most 2048 bins (estimate); **no extra FFT** | 720 B | build it with the Spectrum slider |
| Frequency response | AUTO SET's 20-point overview: magnitude (phase in Phase only), fitted to the track's height | a copy of the overview table taken when AUTO SET ends; today the first real sweep overwrites it | none | about 400 B | build it; nothing is drawn before the first AUTO SET |

In all three modes the overview is drawn dimmed outside the window and in full colour inside it. The window is drawn
over it exactly as today, and touch does not change.

## 2. Geometry and colours (all three modes)

**As built in fw 0.2.73** (owner decisions 10 F items 5 and 6, 2026-10-19 21:20 and 21:36; firmware `slider_ov.h`). The
row keeps only what parts it from the plot; everything else is track:

| Part | Pixels | Source |
|---|---|---|
| Line | y 452, x 2..727 (white): parts the track from the plot above, which has no bottom line of its own when Grid is off | `SOV_LINE_Y`, `sov_draw_row` |
| Track | x 2..727 (726 columns) and y 453..471 (19 rows): the old frame's 18 inside rows plus its bottom row; the frame's left, right and bottom lines are gone | `SOV_X` / `SOV_W` / `SOV_Y` / `SOV_H`; `TBV_SLIDER_X` / `_W` and `BU_SL_X` / `_W` follow |
| Window | two vertical lines, 1 px each (white), at its left edge x and its right edge x + w - 1, over y 453..471; no top or bottom line | `sov_draw_window` |
| Overview | every track pixel: one column per track pixel, all 19 rows | `sov_draw_run` |
| Hit area | unchanged: the Scope y 436..479 over x 2..727, the Spectrum and Frequency response y 450 down | `TBV_SLIDER_HIT_Y`, `BU_SL_HIT_Y` |

The count: fw 0.2.71's frame was rect x 2..727, y 452..471 (`display_draw_rect(2, 452, 726, 20)`), the window a 14 px
rect at y 455 on a track x 4..723. Without the frame's sides and bottom the free columns are x 2..727 (726) and the free
rows y 453..471 (19). The track the window maps grew from 720 to 726 px with it, so the window, the drags and
`bu_slider_x` scale by 726 / 720 (a 10 px drag at 25 MSa/s, 1 ms/div, 3000 samples moves 82 samples, was 83;
`bu_slider_x(6)`, 100 Hz, is x 209, was 210).

- **Rows.** The overview uses all 19 rows. Only the window's two edge lines cross it, one column each.
- **Columns.** Column x stands for the part of the whole that maps to track pixel x. The window uses the same mapping,
  so the window lies exactly over the part the plot shows. A frequency at the track's right end (10 MHz in Frequency
  response, x 728 by the formula) is drawn on the last column, 727.
- **Colours.** Outside the window the overview is drawn in the channel colour at half intensity: CH1 0x808000, CH2
  0x008080. In Frequency response it uses the magnitude colour (CH2 cyan, `bode_view.c`) or the phase colour
  (`PHASE_COLOR` 0xFF80FF -> 0x804080) in the same way. Between the window's edge lines (edges included) it is drawn in
  the full colour. A flagged Frequency response point is `FLAG_COLOR` grey, as on the chart. All three dim colours are
  exact in RGB565.
  - **Why not grey.** Grey means "not available" (ui-guidelines section 3), so a grey overview would read as missing
    data. It would also lose which channel is which.
  - **Why not full colour everywhere.** The whole track would then compete with the plot and with the window. The
    bright part is the window's content, the same part the plot shows above it, so the window reads even before the eye
    finds its two lines.
  - **Why dimmed and not hidden** outside the window. The part outside the window is the reason for the feature.
- **Drawing order.** The row black and the line, then the overview (CH2 first and CH1 over it, the traces' order), then
  the window's two edge lines last, in white (the mockups draw the line and the edges grey, LG1 / LG2, as they always
  drew the frame and the window).
- **Fonts.** Nothing in the track is text: no labels, no scale and no "no data" line (section 7).

**Shared code (`slider_ov.c/.h`, pure, host_tests/test_slider_ov.c; drawing in `slider_ov_draw.c`):**

- the item range of each column of a whole of n items (samples or bins), each with one item of overlap for joining
  (`sov_items`, and `sov_cols` stepping through the columns without a division per column);
- the row of a value on a linear scale or fitted (`sov_row`, `sov_row_fit`), clipped to 0..18;
- the vertical run per column of a line through points (`sov_lerp`, `sov_join`);
- the inside-window test (`sov_inside`) and tap to jump (`sov_jump`, section 6).

Each view gathers its data with it and draws runs with `display_draw_vert_line`, a run in the dim or the full colour by
the column's place against the window.

## 3. Scope

**What.** For every enabled channel, the lowest and highest sample of each column, over the whole record:

- the record is `channel1tracebuffer` / `channel2tracebuffer` with `total` = `fpgasettings.totalsamples`, the same
  `total` that `tbv_slider` maps;
- column c covers samples c x total / 720 up to (c + 1) x total / 720, plus the previous column's last sample, so a step
  between two columns shows as a vertical line, as the trace joins its points;
- a column run is drawn from its min row to its max row.

**Vertical scale.** The plot's own scale, squeezed: screen y 49..448 (what `scope_get_y_sample` returns, clipped as it
clips) maps to the rows by row = 453 + (y - 49) x 19 / 400. That is 2.4 rows per division (fw 0.2.73; 1.5 in the 12-row
draft). The track is
therefore the plot zoomed out to the whole record. A signal clipped on the plot is clipped in the track, and a signal
under a division peak to peak is a flat line, as it is a thin trace on the plot.

**Stored values.** The min and max code per column per channel, 720 x 2 x 2 = 2880 bytes in .bss. Codes, not rows: a
change of V/div, position or offset while stopped redraws the track from the same codes, as the plot redraws from the
same record. The batch factors the code-to-y part out of `scope_get_y_sample` into `scope_code_to_y(settings, code)`,
which `scope_get_y_sample` then calls. The overview uses it for its 2 x 720 values per channel.

**When.** The overview is computed once per record, where the trace drawing starts on a new record: a capture, a host
capture shown on screen, or a record loaded in the waveform viewer. It is not computed during a drag: the record does
not change, and only the bright part moves with the window. The overview is cleared whenever the plot's trace is
cleared. An example is a time base change, after which the buffers still hold the old rate's record.

**Variants and cases.**

| Case | Track |
|---|---|
| Normal, both channels | both envelopes, CH2 then CH1 |
| One channel off | only the other |
| Phosphor | the latest record's envelope (the phosphor layer is the plot's business) |
| Average on | the record the plot draws (the averaged one) |
| Waveform viewer | the loaded record's envelope |
| Roll mode (`long_mode`) and the legacy branch of `scope_draw_grid` (sample rate index 18 or more, time base 35 or more) | no overview. The window there is 1.00o5's fixed 181 px box, which does not map the record, and in roll mode the record moves. The frame and box stay as today |
| X-Y | no overview: the plot has no time axis to place the window on. The row stays as today |
| References, math trace | not drawn: the track is about the record |

**Cost.**

- **CPU (estimate).** About six instructions per sample: a byte load, two compares, two conditional moves and the loop.
  That is about 200,000 cycles for 2 x 16382 samples, 0.33 ms at 600 MHz with the D-cache on (fw 0.2.48+). At 3000
  samples it is 0.06 ms. The scope view's median pass is 15 ms (`firmware/README.md`, "Costs"), so this is under 2.5 %
  at the deepest memory and under 0.5 % at 3000.
- **Drawing.** At most 1440 vertical runs of up to 12 px, under 18,000 pixels: well under 0.1 ms.
- **Measurement.** The batch measures it with GET_STATUS `pass_ms`, on and off, at 3000 and 16382 samples.

**Recommendation: build it.** The cost is small, the data is on hand, and it is the one place on screen that shows what
the record holds outside the window. That is the owner's case for a deep record zoomed into one decay (board
ScopeOverview).

## 4. Spectrum

**The fact that decides it.** Every pass already transforms the whole axis. `spectrum.c` computes all N/2 + 1 bins of
the record (`fx_fft_cdb(&spec_plan, 0, half + 1, ...)`, line 442). The plot then shows the slice from `first` to
`first + span` of those bins (`spec_view`, `spec_columns`). The axis ends at fs/2 or at `PC_SPECTRUM_FMAX_HZ` (25 MHz),
whichever is lower (`v->half`). So the spectrum over 0..fs/2 is in `spec_db` (and `spec_raw`) every pass. The overview
is `spec_columns` run over the whole axis instead of the slice: first 0, span `half`, one column per track pixel, each
taking the highest bin it covers, which is the plot's own rule.

**The two alternatives, and why not.**

1. **A second FFT at full span of the same record.** It would give the same bins. It costs 0.68 ms at 2048 points and
   2.72 ms at 4096 (FFT_BENCH with the MMU on, `firmware/README.md`), plus the dB conversion, and draws nothing new.
2. **A spectrum at a faster rate** than the view's. After a zoom-in has stepped to a slower rate (span fs/16 or less,
   fw 0.2.41), 0..25 MHz could show the wider picture. It needs a second capture at another sample rate on every pass.
   The FPGA samples at one rate, so the view would alternate time bases, wait for each (the `spec.wait` logic), and
   halve the update rate. The window would also no longer be the shown span over the same axis, which breaks the
   slider's meaning (ux-makeover.md: the window's left edge at start / (fs/2)). Rejected.

**What is drawn.**

- **Data.** `spec_db` in the Spectrum variant (as shown: averaged or max-hold). `spec_raw` (the latest record) in
  Waterfall and Density, because those views take the unaveraged records.
- **Channels.** Analyse picks them: CH1, CH2, or both (CH2 first, CH1 over it).
- **Vertical scale.** The plot's ref level and dB per division, its 8 divisions squeezed into 12 rows: row = 456 +
  (top - level) x 12 / (8 x dB per division), clipped. A noise floor below the plot sits on the bottom row, and lines
  rise from it.
- **Shape.** A line, each column joined to the one before it, as the trace is drawn.

**When.**

- **Each new spectrum.** The overview is recomputed whenever the plot gets one.
- **During a pan drag.** The overview stays and the window moves. The plot's new slice comes from the same bins, so
  after the release the plot and the bright part agree at once.
- **Waiting at a new sample rate.** While the view says "Waiting for a capture at the new sample rate" (`spec.have` is
  0), nothing is drawn: the old overview belongs to the old axis.

**Cost.** A max over at most 2048 bins plus 720 columns: about 10 us (estimate). No buffer is kept: the rows are
computed into a 720-byte array at draw time.

**Dependency.** The Spectrum slider itself is not in the firmware yet (`firmware/AGENTS.md` section 45, "Not done" item
2). The overview is drawn by the same code, so it lands with that batch or after it.

**Recommendation: build it.** It is free, the data is already there, and it answers the question a zoomed spectrum
leaves open: what is outside the span.

## 5. Frequency response

**What.** AUTO SET's overview sweep: 20 points, 100 Hz .. 10 MHz, Settle Fast (bode-ui.md 3.2; `bode_view.c`
`AUTO_START_I` 6, `AUTO_STOP_I` 21, `AUTO_POINTS` 20).

- **Kept by copy.** Today the engine keeps no copy: the first real sweep after AUTO SET overwrites the table
  (`firmware/AGENTS.md` section 45, "Not done" item 3). The batch copies the overview when it completes, in
  `bv_auto_finish` right after `gather()`. Per point it copies `p_f` (uint64 mHz), `p_m`, `p_p` (int32), `p_flags`
  (uint16), `p_have` and `p_ok`: 20 x 20 bytes = 400 bytes in `bv`.
- **Separate from the full-restore copy.** This copy is not the "previous table comes back on cancel" copy, which needs
  about 25 KB and stays queued separately.

**Lifetime.**

| Event | Overview |
|---|---|
| Power-on, Restore default settings | none |
| Before the first AUTO SET | none; the track is empty, as in fw 0.2.69 |
| AUTO SET running | the points measured so far, drawn as they come; the window spans 100 Hz .. 10 MHz (bode-ui.md section 2) |
| AUTO SET completed (any result, also "No corner" and "No response") | the new overview replaces the old one |
| AUTO SET cancelled | the previous overview comes back, as the settings do |
| Start / Stop / Points / Settle changed, slider drags, sweeps, RUN / STOP, leaving and re-entering the mode | kept |

It is not saved to the settings sector. 400 bytes do not fit the three free words, 216..218, and an overview of a
circuit that may since have been unplugged should not survive a power cycle. Whether a later sweep that contradicts it
should drop it is owner question 3.

**What is drawn.**

- **Curve.** The magnitude in Overlaid and Magnitude only; the phase in Phase only, which follows the Display variant
  so the track shows what the plot shows. Never both: two curves in 12 rows cannot be told apart.
- **Columns.** A point's column is its log position on the track. The track is 1 Hz .. 10 MHz over x 4..723, about 103
  px per decade, so the overview covers x 210..723. The new function `bu_track_x(mhz)` uses the same log mapping as
  `bu_slider_x`, for any frequency rather than a 1-2-5 step index. The left 206 px (1 Hz .. 100 Hz) stay empty because
  the overview never looks there.
- **Vertical scale: fitted.** The highest valid point goes on the top row and the lowest on the bottom row. The track
  has no labels, so only the shape counts. The chart's own scale for the overview (20 dB per division, two rows per
  division) would be 10 dB per row, and it squashes the board's Q 8 tank into a hump six rows high. Fitted, the hump uses
  all twelve.
- **Points.** Valid points are joined by a line, as on the chart. A flagged point is a single grey pixel with no
  joining line, the chart's rule: a flagged value never looks like a good one.

**Cost.** 20 points, 400 bytes, and drawing a 20-point line. None of it is per pass.

**Recommendation: build it.** The overview is already measured; the batch only stops throwing it away. On a tank it
shows where the resonance sits against the span chosen by hand, and that is the question a narrow span leaves open.

## 6. Interaction

- **Touch does not change.** The hit areas stay as they are: the Scope's TBV_SLIDER_HIT_Y 436..479 over the frame's
  width, the Frequency response mode's BU_SL_HIT_Y 450 down, and the Spectrum the Scope's. The drags stay the same too:
  the window follows the finger (fw 0.2.64; `bode_view.c` `slider_drag`).
- **During a drag.**
  - The overview does not move: it is the whole, and the whole does not change while the finger moves.
  - The window and its bright part follow the finger, column by column: the column under the window switches from dim
    to full.
  - The plot follows as today: the Scope's trace, the Spectrum's span, the Frequency response axis with the old
    points inside the new span.
  - Nothing is recomputed; only the row is redrawn.
- **A tap on the overview** outside the window: in fw 0.2.73 (owner, 10 F item 2) a touch-down on the row (y 450
  down) outside the window centres the window on it, kept on the track (`sov_jump`), and the drag goes on from there;
  the release ends as a drag release (Scope scrolled, Spectrum panned, Frequency response re-sweeps or runs one sweep).
  The Scope's extended drag area above the row (y 436..449, beside the cards) never jumps.
- **Frequency response during AUTO SET.** A drag is ignored, as today, while the overview fills in.

## 7. Empty states

The guideline that every unavailable state has defined text (section 4) applies to values. The overview is not a
value: it is the track's background, and the window and its drag work without it. In each mode the plot above already
says why there is no data, so the track does not repeat it, and a 13 px line does not fit its 12 rows.

| Mode | Empty when | The plot says |
|---|---|---|
| Scope | no record since entering the view, or the trace was cleared | the empty grid, the run state chip WAIT |
| Spectrum | waiting at a new sample rate, or no spectrum yet | "Waiting for a capture at the new sample rate" |
| Frequency response | no AUTO SET since power-on | the two-line instruction (bode-ui.md section 6) |

## 8. Firmware batch (after approval)

1. **`slider_ov.c/.h`** (pure, section 2) with host tests: column ranges for n < 720 and n > 720, the one-item
   overlap, row clipping, line runs, and the inside-window test at the window's drawn x and w. That includes the 8 px
   minimum width of `tbv_slider`: the bright part is the drawn window, even where the true share is narrower.
2. **Scope.**
   - `scope_code_to_y` factored out of `scope_get_y_sample`, with no change to the trace (host test: the same y for
     every code, V/div and position);
   - the envelope pass where a new record reaches the trace drawing;
   - the drawing in `scope_draw_grid`, between the frame and the window;
   - the cases of section 3 (roll, legacy branch, X-Y: none).
3. **Spectrum.** With the slider batch: `spec_columns` over the whole axis into the 720-byte row array, drawn in the
   slider row.
4. **Frequency response.**
   - the overview copy in `bv_auto_finish` and the live overview while `bv.autoset` is set;
   - `bu_track_x`;
   - the fitted rows;
   - the drawing in `draw_slider` before the window;
   - clearing at power-on and by Restore defaults (`bode_view_defaults`).
5. **Colours.** `CH1_DIM_COLOR` 0x808000, `CH2_DIM_COLOR` 0x008080, `PHASE_DIM_COLOR` 0x804080 beside the stock
   colour defines.
6. **Bench.**
   - pass_ms on and off at 3000 and 16382 samples;
   - a PI-like burst at 10 us/div and 16382 samples: three pulses in the track, the window over one;
   - the Spectrum zoomed one step: the bright half over the left;
   - Frequency response: no overview before AUTO SET, the overview filling during it, kept after it, kept through a
     cancelled AUTO SET, gone after a power cycle.

## 9. Mockups

Generated by `docs/mockups/gen_ux` (`common.py`: `slider_row`, `ov_svg`, `ov_line_runs`, `scope_square_overview`;
`boards.py`: `spectrum_overview`, `pi_board`; `bode_final.py`: `track_x`, `fr_overview`, `fr_slider`), regenerated for
the geometry of section 2 (fw 0.2.73: the line at y 452, the 19-row track x 2..727, the window as two edge lines):

| Board | Shows |
|---|---|
| makeover/Scope (and every board drawn on the Scope view) | the 1 kHz square's record (3000 samples at 500 kSa/s) in the track. The window is fw 0.2.64's to-scale window on the 726 px track (x 189, 350 px) instead of 1.00o5's fixed 181 px box. Six periods, three of them bright between the window's edges |
| makeover/Phosphor, ScopeCtrlDisplay | the same record, CH1 only |
| **makeover/ScopeOverview** | a pulse-induction coil (CH1) and its TX gate (CH2, the trigger), 15000-sample record at 5 MSa/s and 10 us/div: the window is 35 px over the middle pulse's decay, and the other two pulses show dimmed in the track |
| makeover/Spectrum, SpectrumCtrl*, frequency-response/SpectrumSlider | 0..100 kHz in the track (the square's odd harmonics), the window over 0..50 kHz in full colour |
| frequency-response/Main, BodeCursors, BodeSingle, BodeCtrl*, BodeMeasures | the tank's AUTO SET overview (20 points; the five below -58 dB flagged and grey) with the window over the hand-set 5 kHz .. 500 kHz. These boards assume that AUTO SET ran before the span was set by hand |
| frequency-response/BodeFirst | no overview: AUTO SET has not run |
| frequency-response/BodeAuto | the overview at 12 of 20 points, filling in under the 100 Hz .. 10 MHz window |

The frequency-response and Spectrum tracks start at x 2 as `BU_SL_X` does (host test `bu_slider_x(6) == 209`).

## 10. Adversarial self-review

### A. Against the guidelines

1. **Colour carries state; words carry meaning.** Dim against full is a state (outside or inside the window) with no
   word next to it. The window's frame already says the same thing, so the colour adds nothing a colour-blind reader
   misses. Dim against full is a difference in luminance, not hue. Pass, stated so it is not mistaken for drift.
2. **Channel colours.** The dimmed colours are new colours. They keep the hue, so they never collide with the
   guideline's green (verified), amber (attention) or grey (unavailable). Pass.
3. **Only values the firmware has.** Scope and Spectrum draw data the view already holds every pass. Frequency response
   draws a measurement the engine made and today discards; the batch keeps it. Nothing is interpolated: the FR line joins
   measured points as the chart does, and flagged points stay grey. Pass.
4. **Every unavailable state has defined text.** The empty track has no text. Section 7 argues that the overview is not
   a value, the plot states the reason, and 12 rows cannot hold a 13 px line. Accepted, for the owner to veto.
5. **No placeholder for a feature not built.** Before AUTO SET the Frequency response track is empty, with no outline
   or hint where the overview will come. Pass.
6. **Touch.** Unchanged. The overview adds no target that looks tappable but is not: it lies under the existing 44 px
   hit area. Pass.
7. **Nothing blinks.** Nothing does, and the live AUTO SET fill grows point by point. Pass.
8. **Process.** Mockups first, at 800 x 480, generated from the same code as the boards they extend. Pass.

### B. What 13 px bitmap fonts and a 20 px track cannot render

1. **No text at all in the track.** The inside of the frame is 18 rows and the overview 12. A 13 px glyph with its
   descent does not fit between the window's lines, so there are no labels, scale, units or "no data" line.
2. **Twelve rows.** That is 1.5 rows per division in Scope and Spectrum.
   - A signal under about two thirds of a division peak to peak is a flat line.
   - The Scope boards' two channels share rows 462..464: CH1's low level and CH2's high level are a row apart.
   - On the PI board the coil's -0.6 V during TX and its 0 V rest fall in the same row, so the TX phase is visible only
     through CH2's gate.

   This is the honest picture at the plot's scale. Option: split the rows per channel (owner question 4).
3. **One column is a lot of record.** At 16382 samples a column is about 23 samples, 4.6 us at 5 MSa/s. A spike shorter
   than that still shows, because the envelope keeps the min and max. Its shape does not.
4. **The window's vertical edges** cover one overview column each. At the 8 px minimum width that is a quarter of the
   bright part.
5. **Frequency response.** Twenty points over 514 px are about 27 px apart, so the line is drawn at the overview's
   resolution and a narrow resonance between two points is missed. That limit is AUTO SET's own: `firmware/AGENTS.md`
   section 45, "Not done" item 6, says that a tank's Q from the overview is a lower bound.

### C. Contradictions with the current firmware

1. **The Scope boards' window.** It was drawn as 1.00o5's fixed 181 px box at x 275. fw 0.2.64 draws it to scale
   (`tbv_slider`), so the boards now show x 190, 347 px for 3000 samples at 200 us/div. That fixes the mockups, not
   the firmware.
2. **The track's first pixel.** The Frequency response and Spectrum generators started the track at x 5; the firmware
   starts it at 4 (`BU_SL_X`, `TBV_SLIDER_X`). Fixed in the generators.
3. **The row's height.** bode-ui.md section 2 says y 452..472; the firmware frame is y 452..471
   (`display_draw_rect(2, 452, 726, 20)`). This design uses the firmware's numbers. bode-ui.md is left as written.
4. **The frame and window colour.** The firmware draws white; the mockups draw grey (LG1 / LG2) and always have. The
   design keeps white in the firmware: the window is drawn over the overview and must be its brightest line.
5. **AUTO SET's overview is discarded today** (section 5). The design needs a 400-byte copy. It does not depend on the
   25 KB copy for a full restore on cancel, which stays queued.
6. **The Spectrum slider is not in the firmware** (AGENTS.md section 45, "Not done" item 2). The Spectrum overview
   depends on it. `spectrum.c`'s header still describes the layout before the slider (plot y 66..410, cards y 446) and
   says the D-cache is off; both are stale and go with the slider batch.
7. **`scope_get_y_sample` reads by index and through the global `math_sample`.** Factoring out `scope_code_to_y` must
   leave the trace and math traces exactly as they are. The host test in section 8 pins it.
8. **Roll mode and the legacy branch** keep 1.00o5's fixed box. The overview is not drawn there; no mapping exists to
   draw it on.

### D. Values and feasibility

1. All CPU figures are estimates from instruction counts. The FFT times are measured (FFT_BENCH, fw 0.2.48) and are not
   needed, because no FFT is added. The batch measures pass_ms (section 8). An overview that costs more than 1 ms at
   16382 samples should be revisited: computing it only when the record changed, not per redraw, is already the design.
2. Memory is 2880 + 720 + 400 bytes of .bss, nothing in on-chip SRAM, and no settings words.
3. On the 0x1539 bitstream the capture fault flags every Frequency response point (hardware-status, 20:51). The
   overview is then a row of grey pixels: honest, and it matches the chart.

### E. Questions for the owner

1. **Colours.** Dimmed channel colour outside the window and full colour inside it, or dimmed everywhere, or grey
   everywhere? **Recommendation: dimmed outside, full inside.** The bright part repeats the window. Grey means
   unavailable and loses the channel.
2. **Tap to jump.** Should a tap on the overview outside the window move the window there (centre it on the tap)?
   Today a tap does nothing and this design keeps touch unchanged. **Recommendation: yes, in a later touch batch.** The
   overview makes the target visible for the first time, and the drag stays the precise way.
3. **When the Frequency response overview turns stale.** Keep it until the next AUTO SET and drop it at power-off, or
   also drop it when the first complete sweep disagrees with it by more than 6 dB at the overview's frequencies inside
   Start..Stop (the DUT was changed)? **Recommendation: the simple rule now, and the disagreement check only if the
   bench shows stale overviews misleading.**
4. **Two channels in Scope.** Both overlaid on the plot's scale (as drawn), or six rows per channel, or only the
   trigger source? **Recommendation: overlaid.** It is the plot zoomed out and needs no rule to explain; the split
   halves the resolution of both.
5. **Window height.** Keep the 14 px window with a 12-row overview inside its lines (as drawn), or make the window as
   tall as the frame's inside (18 px) for a 14-row overview? **Recommendation: keep 14 px.** The Scope's construction
   stays as the owner has used it since fw 0.2.64, and the window's lines never cross the data.

### F. Owner decisions (2026-10-19 21:20, walked through one by one)

1. Colours: dimmed channel colour outside the window, full colour inside **(owner)**. The Scope's existing full-yellow
   thumbnail changes to this so the three modes look the same.
2. Tap to jump: yes, as a fast follow right after the overview batch (a tap outside the window centres it on the tap)
   **(owner)**.
3. Frequency response overview lifetime: the simple rule, kept until the next AUTO SET or power-off; no disagreement check
   **(owner)**.
4. Scope channels: overlaid on the plot's own scale, enabled channels only; the owner will look at it on the bench and may
   change it **(owner)**.
5. Window height: not 14 px. Use the whole inside of the frame (18 px window, 14 overview rows): "backwards compat doesn't
   matter, maximize the available space; it'll be difficult enough to cram both traces in there" **(owner)**. Sections 2 and
   9 and the boards are to be regenerated to 18 px by the firmware batch's designer pass (the geometry in section 2 is now
   superseded on this point). Superseded in turn by item 6.
6. Track geometry (2026-10-19 21:28): give the traces every pixel row of the track. The row loses the frame's bottom,
   left and right lines and keeps only the line at y 452 that parts it from the plot; the window is no longer a box but
   its left and right edges, 1 px each in full colour over the track's full height, with no top or bottom line and no
   margin between the row and the window; the overview is full colour between the edges and dimmed outside them; the
   drag hit areas stay as they were **(owner)**. Built in fw 0.2.73: the track x 2..727 (726 columns) by y 453..471 (19
   rows), section 2.

