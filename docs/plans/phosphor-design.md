# Digital phosphor view: design (fw-phosphor, fw 0.2.46, protocol 1.13)

Owner priority 2 after the spectrum analyser. Study: `docs/plans/dpos350p-features.md` section 4. This file is the
design as built; user-facing description in `firmware/README.md` "Phosphor view", handoff in `firmware/AGENTS.md`
section 24. [F] = fact from source or a bench note, [I] = inference, [E] = estimate.

## 1. What it is

A display mode of the main loop like the spectrum view (main menu row 7 "Phosphor display"; the row reads "Scope view"
while it is on). The scope view stays as it is: grid, cursors, cursor readouts, measurements, the trace / trigger
pointers, REF and MATH traces, the top bar and the right column are 1.00o5's. Only the CH1 / CH2 traces change: instead
of the latest record's polyline, each channel shows a hit-count map of all recent records, coloured by how often a
pixel was hit. A one-line strip replaces the record-position slider under the grid (y 452..479): Persistence,
Palette, Clear, Scope, and a status field. GET_STATUS byte 43 `display_mode` = 4.

Not available (row hidden, the view ends, as the spectrum view does): roll mode (long time base) and the picture /
waveform viewers. XY mode is supported (one map, section 6).

## 2. Map

- Area: x 2..729 (728 columns), y 49..448 (400 rows), the range `scope_get_y_sample` returns [F: scope_functions.c].
- One map per channel, **uint8 per pixel, column-major** (`map[ch][x][y]`, 400 bytes per column): 291,200 B per channel,
  582,400 B in .bss (26 MB free [F: README memory budget]). Column-major because both hot operations walk a column:
  a record adds a vertical run per column, and a column's non-zero cells are a contiguous byte range.
- Per column an extent `[lo, hi]` of rows that may be non-zero (one u32 per column and channel, DDR). Decay and render
  touch only the extent, so the cost follows the lit area, not the map size.
- Full resolution, not half: a half-resolution map saves map reads but not pixel writes (each cell paints 4 pixels),
  and it blurs the 1-px traces the grading is for. The per-column histogram alternative (bins per column) is the same
  structure at a coarser y; not needed with the extents.
- Small tables in on-chip SRAM A at 0x0400 (code-to-row LUT per channel, 2 x 512 B; three palettes, 3 x 512 B): free
  at run time, only the vectors (0x00-0x3F) live below 0x1000 and the spectrum's FFT uses 0x1000-0x7008 [F: spectrum.c
  SRAM comment]. Every DDR access is uncached, ~90 cycles [F: firmware/AGENTS.md section 20]; a table lookup per
  sample or per pixel from DDR would cost as much as the map access itself.

## 3. Adding a record (once per new record, UI or host capture)

- Rows: a 256-entry LUT code -> row built per record with 1.00o5's own `scope_get_y_sample` (V/div, calibration, trace
  position, DC offset), so the map, the cursors and the measurements agree by construction.
- Columns: sample i is at x = disp_xstart + (i - disp_first_sample) x disp_xpos_per_sample (the linear trace's mapping,
  in Q16). Every sample of the window is used (min/max per column, like peak detect), not one sample per column as
  1.00o5's linear trace does.
- Line drawing: each column gets one run from its lowest to its highest point, extended towards the next column's
  first point (exclusive), so consecutive samples are joined like a line and every pixel of the line is counted once
  per record. Zoomed in (several columns per sample) the intermediate columns are interpolated.
- Increment: saturating add of `inc` to 4 cells per word (SWAR, no ARMv6 UQADD8 on the ARM926): 2 accesses per 4 cells.
- Untriggered records (firmware auto time-out) are dropped while a triggered one came within the last second, as in
  the spectrum view: a periodic signal's occasional miss would otherwise paint a ghost that looks like a glitch.

Cost per record and channel at 3000 samples [E, 90 cycles per uncached access, 600 MHz]:

| item | accesses | cycles |
|---|---|---|
| samples read as words | 750 | 68 k |
| LUT in SRAM + per-sample ALU | | ~60 k |
| columns: extent read + write, run words read + write (flat trace: run 1-3 cells = 1-2 words) | 728 x 6 | 390 k |
| **total, flat or slow trace** | | **~0.5 M = 0.85 ms** |
| worst: every column spans all 400 rows (signal far faster than the time base) | 728 x 202 | 13 M = 22 ms |

Touches (cells incremented) per record = sum over columns of the run lengths: about 728 for a flat trace, 728 + the
edge heights for a square wave, 728 x 400 at worst. The host test checks the count for these shapes (`test_phosphor`).
1.00o5's linear trace has the same worst case (a 400-px line per column, 291 k pixel writes).

## 4. Decay (persistence)

- Every pass, for the time since the previous pass (dt, at most 200 ms, so a stall or a modal menu does not wipe the
  picture; nothing decays while stopped: persistence counts acquisition time). A fixed decay clock (e.g. 20 Hz) was
  the first design and was dropped: with 30 ms passes a decay step lands on some passes and not others, and the
  brightness shimmers by the step size (50 % per step at 0.2 s).
- Factor k = k1^dt, k1 = exp(-ln 16 / (1000 T)) per millisecond in Q24 (square and multiply), applied in Q16 per cell:
  c = (c x k + d) >> 16 with a dither d = (seed + 25033 x column) mod 65536, seed stepped every pass. The floor alone
  would fade small counts linearly (a count of 4 at 5 s would vanish in a few passes instead of ~2 s); with the dither
  the expected value is exactly c x k, and every cell still reaches 0. Deterministic for a given sequence of records
  and dt (host-tested).
- Persistence T = the time a cell that is no longer hit takes to fade to 1/16 (from full to dim). 0.2 s, 1 s, 5 s,
  Infinite (no decay). Default 1 s. Host test: 255 falls to 15.7-16.1 at T for all three, at 33 and 50 ms per pass.
- Increment normalised to the record rate, so the brightness means hit probability whatever the time base:
  inc = round(1.25 x 255 x (1 - k1^interval)), 1..255, interval = ms between records (exponential average over 8
  records, at most 2 s). A pixel hit by every record settles at 1.25 x 255 (saturated); one hit by 10 % averages ~29
  (host test). At 33 ms per record: inc 117 (0.2 s), 28 (1 s), 6 (5 s); records 2 s apart get 255 each. Infinite:
  inc = 1 (the count is the number of hits, saturating at 255).
- Fused with the render: the render paints each lit cell with its count, then decays it and writes the word back
  when it changed; extents shrink to the words still lit. No separate pass over the map.

## 5. Render (every pass)

1.00o5 clears and repaints the trace area every pass [F: scope_display_trace_data], so the map is painted every pass,
into `displaybuffertmp` after the grid and before REF / MATH, cursors, pointers and measurements, exactly where the
traces were drawn. Zero words are skipped (the grid shows through), non-zero cells write one RGB565 pixel each.
CH2 is painted only where its count exceeds CH1's (the brighter channel wins, CH1 on ties). The palette is indexed by
the count before this pass's decay, so a saturated cell shows full brightness.

Cost per pass and channel [E]: extent words read (~90 cycles each) + one pixel write per lit cell (posted writes; the
1.00o5 clear writes 314 k pixels per pass inside a 30 ms pass, so a write costs under ~55 cycles [I]).

| picture | words read | pixels | per channel |
|---|---|---|---|
| one sharp trace (2-3 px per column) | 728 x 1-2 | ~2 k | ~0.3 ms |
| trace with jitter / noise, 10-20 px per column | 728 x 4-6 | 7-15 k | ~1-2 ms |
| worst: whole area lit | 72.8 k | 291 k | ~25 ms [E] |

The decay adds one write per lit word per pass. With both channels at the worst case the pass grows from ~30 ms to
~90 ms (11 passes/s): still interactive. A typical picture adds 1-4 ms
to the 30 ms pass. Bench check: GET_STATUS `pass_ms` in the phosphor view vs the scope view (section 8).

How the UI stays responsive: one record per pass at most (1.00o5's loop), decay by elapsed time (not per record),
everything bounded by the lit area, nothing allocates, nothing waits. Settings changes clear by extents (memset of
the lit runs only).

## 6. Palettes, channels, XY

- **Mono** (default): the channel colour (CH1 yellow, CH2 cyan) scaled by brightness 24 % (one hit) .. 100 %
  (saturated), on a square-root curve so rare paths stay visible.
- **Heat**: dark blue, blue, cyan, green, yellow, red, white (most frequent), on the same square-root index. Shared by
  both channels (the pointers and measurement tiles identify them).
- **XY mode** (stretch, built): one map (CH1 x, CH2 y, 1.00o5's 730 points from the trigger index), each segment drawn
  with Bresenham into the map (start pixel skipped for chained segments), XY colour (magenta) in Mono. Cost ~730
  segments x a few pixels x 2 accesses per pixel ~ 0.5-3 ms per record [E].

## 7. Clearing, controls, settings

- Clears when what a pixel means changes, per channel: enable, V/div (display and sample), trace position, DC
  offset, coupling; for both: time base, sample rate, trigger position, memory depth, decimation filter, XY mode,
  persistence. Not on trigger level (always-50 % moves it every record) or palette (display only).
- Strip fields (tap; one line each, grey label, white value; the value is the label, no on/off words):
  Persistence 0.2 s -> 1 s -> 5 s -> Infinite -> 0.2 s; Palette Mono <-> Heat; Clear; Scope (leave). Status field at
  the right: "NN wfm/s" (records added per second), or a warning in orange with consequence and action:
  "CHn overrange: increase V/div", "No trigger: check the trigger level", "No channel enabled: switch on CH1 or CH2".
  Stopped: the field is empty and the picture frozen (STOP is the user's own choice and the RUN/STOP chip shows it).
- Settings word 210 (next after the spectrum's 208/209, unused by 1.00o5): 0x9A00 marker | palette << 2 | persistence;
  restored only when the marker matches and the fields are in range (the spectrum's rule). The mode itself is not
  restored (boot into the scope view).

## 8. Bench acceptance (owner)

- Generator 1 kHz square into CH1, 200 us/div: sharp steady trace, edges drawn; Persistence steps fade as stated.
- Duty toggled 30 / 70 % from the host every 0.25 s: both falling edges at about equal intensity.
- `pass_ms` (GET_STATUS) in the phosphor view within ~5 ms of the scope view for that signal.
- Grid, cursors, measurements, pointers unchanged; READ_SCREEN shows the graded picture.
