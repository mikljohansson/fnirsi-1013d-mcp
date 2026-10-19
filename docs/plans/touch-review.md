# Touch review: Scope and Spectrum plot interactions (2026-10-19)

An adversarial code review of the touch gestures on the plot, done because of the owner's report: dragging a channel
up or down is unreliable, CH1 could not be moved while CH2 could, and the CH1 / CH2 pointers at the left edge of the
grid cannot be grabbed. Base: `main` at 84fa444 (fw 0.2.50). Review only; no firmware was changed. Line numbers refer
to that commit. Rules applied: `docs/ui-guidelines.md` (44 px targets, hit area = drawn area, thin controls extend
their hit area, no parameter with two homes). "Inference" marks what was reasoned from the code and not seen on the
device.

Owner confirmation (2026-10-19, relayed by the coordinator): with CH2 dragged to the bottom, CH1 can be picked up
again, but only when the drag starts on its zero line; a miss does nothing. This matches defects 1, 2 and 3 below.
Owner decision: **the pointers drawn at the left edge become real drag handles; anything drawn as a handle must be
draggable.** Section (c) makes that the primary design and section (c1) gives the fix batch ready to dispatch.

A correction to the briefing: the 30 px grab zone is not centred 7 px below the drawn line. `display_left_pointer`
draws a 14 px tall icon whose top row is at `442 - traceposition` (`scope_functions.c:334`, `display_lib.c:1339`); the
icon's tip is its widest two rows, rows 6 and 7 (`icons.c:500-501`, `0xF8` = 21 px), so the tip lies at
`448 - traceposition` and `449 - traceposition`. The trace's zero line is drawn at `448 - traceposition`
(`scope_get_y_sample`, `scope_functions.c:2656`), and the hit test centres on `449 - traceposition`
(`statemachine.c:721`). Draw and hit agree to 1 px. The real geometry faults are elsewhere: the pointer's own pixels
(x 2..22) are outside every channel hit area, the hit test ignores the clamp the drawing applies, and it targets the
zero line instead of the visible trace.

## (a) How touch works today (reference)

### Entry and loop

- Main loop (`fnirsi_1013d_scope.c:254-410`): PC poll, battery, acquisition (`scope_acquire_trace_data` or
  `scope_get_long_timebase_data`), then `touch_handler()` once per pass (`:364`). The waveform viewer runs its own
  loop calling `touch_handler()` (`statemachine.c:4883-4893`) without the PC MAIN poll.
- `touch_handler()` (`statemachine.c:34-253`): with `touchstate == 0` it calls `scan_for_touch()` and snapshots the
  grabbed object's value into a `previous_*` variable; with a grab active it reads the panel once per pass and calls
  the object's `change_*` / `move_*` function, or ends the grab when the finger is gone.
- Touch reading: `tp_i2c_read_status()` (`touchpanel.c:182-247`) polls the GT911 status register over bit-banged
  I2C. Status bit 7 set and 1 point: `xtouch`, `ytouch`, `havetouch = 1`. Bit 7 set and 0 or 2+ points:
  `xtouch = 800`, `ytouch = 480`, `havetouch = 0`. Bit 7 clear: nothing changes (the last values stay). It also runs
  `PC_BUSY_POLL()`. `fpga_done_conversion()` reads the panel too (`fpga_control.c:719-720`).

### `scan_for_touch()` zones (`statemachine.c:259-1031`), in order

| Zone | Condition | Action |
|---|---|---|
| Top bar | x <= 730 and y <= 44 | buttons by x: Menu x <= 80, brightness 75..118, CH1 174..277, CH2 281..384, ACQ 388..491, Move fast 496..540, Trigger 546..626, Generator 630..692; each waits for release, then opens its modal menu |
| Right bar | x > 730 | CTRL y 3..57, otherwise the page's handler |
| Plot | everything else (x <= 730, y >= 45) | `PC_SPECTRUM_TOUCH()` first (spectrum, then phosphor strip); then the blocking tap / drag decision below |

### Plot: tap or drag (`statemachine.c:626-699`)

The touch-down point goes to `previousxtouch` / `previousytouch` and the time to `previoustimerticks`. The handler
then **blocks** in `while(havetouch)` reading the panel until the finger moves more than 2 px in x or y (drag) or
lifts (tap). A lift within 200 ms is a tap and calls `scope_adjust_timebase_and_voltdiv()`; a longer still touch does
nothing.

Tap zones (`scope_functions.c:36-172`), taken at the touch-down point, only for 2 < x < 720 and 50 < y < 470:

| | x < 358 | x > 362 |
|---|---|---|
| y < 100 | CH1 V/div one step more sensitive | CH2 V/div one step more sensitive |
| 100 < y < 370 | time/div one step slower | time/div one step faster |
| y > 370 | CH1 V/div one step less sensitive | CH2 V/div one step less sensitive |

The y > 370 band includes the bottom 80 px of the grid, the measurement cards and the record slider row (y 452-471,
`menu.c:8963-8973`).

### Plot: drag pick (`statemachine.c:702-1029`), taken at the position **after** the 2 px move

Normal display, first match wins:

1. **CH1 / CH2 zero line**: only if 60 < x < 660 (volt cursors off) or 60 < x < 310 / 410 < x < 660 (volt cursors
   on). Distance to `449 - traceposition`. CH1 if d1 < 30 and d1 < d2 and CH1 enabled; else CH2 if d2 < 30 and CH2
   enabled. Sets `MOVE_CHANNEL_n | MOVE_TRIGGER_POINT`.
2. **Reference pointers**: only if x < 60. First of ref1..ref8 that is on and within 30 px of `449 - traceposition`.
3. **Trigger level**: x > 660 and within 30 px of `448 - triggerverticalposition`.
4. **Time cursors** (if on): within 30 px in x, left one if strictly nearer.
5. **Volt cursors** (if on and x > 60): within 30 px in y, top one if strictly nearer.
6. **Fallback**: `MOVE_TRIGGER_POINT` (horizontal drag moves the trigger point; vertical does nothing).

X-Y display: any drag is `TOUCH_STATE_X_Y_MODE`: x moves CH1, y moves CH2, no pick.

### Drag maths (`statemachine.c:5580-6267`)

Every handler computes `diff` from the touch-down point (`xtouch - previousxtouch` or the y equivalent), divides it by
5 when `movespeed` is 1 (the top bar shows "slow"), adds it to the snapshot and clamps: channels, trigger level and
refs 6..396 (screen units above the grid bottom), time cursors 3..726 and each other, volt cursors 49..449 and each
other. A channel drag also moves the trigger level when that channel is the trigger source, and the trigger point by
the x component. In long mode every step calls `scope_preset_values()`. While `touchstate != 0` the acquisition is
not re-armed (`scope_functions.c:736, 760`).

### Drawn geometry vs hit geometry

| Object | Drawn | Hit today |
|---|---|---|
| CH1 / CH2 pointer | icon 21 x 14 at x 2, top `442 - tp` clamped 49..442, tip rows `448/449 - tp` (`scope_functions.c:333-375`) | none: x < 60 belongs to the refs |
| CH1 / CH2 trace | y = `448 - (tp + signal + dcoffset*100/dc_shift_size)` clamped (`scope_functions.c:2637-2656`) | only the zero line `449 - tp`, ±29 px, 60 < x < 660, unclamped |
| Ref pointer n | icon 38 x 14 at x 2, top `442 - tp` clamped (`scope_functions.c:390-625`), drawn after CH1, CH2 | x < 60, ±29 px around `449 - tp`, first in ref order |
| Trigger level pointer | 21 x 14 at x 707, top `442 - tvp` clamped, tip `448/449 - tvp`; hidden in X-Y and in long mode with Auto trigger (`scope_functions.c:631-680`) | x > 660, ±29 px around `448 - tvp`, also when hidden |
| Trigger point "H" | 14 x 21 at y 48, x = position (`scope_functions.c:644-655`) | none of its own: the fallback anywhere on the plot |
| Time cursors | dashed vertical lines x = position, y 51..446 (`menu.c:9195-9207`) | ±29 px in x, any y, after channels and level |
| Volt cursors | dashed horizontal lines y = position, x 6..725 (`menu.c:9211-9223`) | ±29 px in y, x > 60, after channels, level, time cursors |
| X-Y CH1 pointer | top pointer at x `157 + tp`, y 47 | whole plot (drag x) |
| Record slider | box y 452-471, window 181 px (`menu.c:8963-8973`) | none (a tap there changes V/div) |

### Spectrum and phosphor

- `spectrum_touch()` (`spectrum.c:1983-2045`, via `pc_glue_spectrum_touch`, `pc_hal_scope.c:1587`) takes every
  plot touch while the spectrum is active: strip (y >= 436) one field per 72 px, acted on at touch-down; elsewhere a
  blocking loop, tap (< 2 px, < 200 ms) left of x 366 zooms the span out, right zooms in; a drag pans when zoomed. No
  channel, cursor or level gestures exist in Spectrum.
- `phosphor_touch()` (`phosphor.c:526-560`) takes only the strip (y >= 452), acted on at touch-down; the rest of the
  plot keeps the scope gestures.

## (b) Defects

Severity: **B** blocks use, **A** annoying, **C** cosmetic.

1. **The channel pointers are not handles.** `statemachine.c:716-717` (channels need x > 60) and `:772` (x < 60 is
   refs only). The 21 x 14 pointer at x 2..22 is outside every channel hit area; a drag on it moves nothing (only its x
   jitter moves the trigger point through the fallback). Repro: drag the yellow "1" pointer up. Nothing moves.
   **B.** Fix: the left column is a handle column for CH1, CH2 and the refs (section c1, step 1).
2. **The grab targets the invisible zero line, not the trace.** `statemachine.c:721, 736` use `449 - traceposition`;
   the trace is drawn at `448 - (traceposition + signal + DC-shift term)` (`scope_functions.c:2637-2656`). A channel
   whose signal sits away from its zero line (any DC level, the calibration square, a large amplitude) can only be
   grabbed at the zero line's height. Repro: CH1 on the calibration pin, DC coupling, 1 V/div; touch the top of the
   square and drag: nothing; touch at the pointer's height in the middle of the grid and drag: CH1 moves. This is the
   owner's confirmed symptom. **B.** Fix: handles first (defect 1), trace body as a secondary candidate with the
   nearest-object rule (section c1, step 2).
3. **The CH1 / CH2 choice is not "nearest eligible".** `statemachine.c:751-768`: CH1 needs `d1 < d2` even when CH2
   is disabled, and a tie goes to CH2. With CH2 disabled and its hidden zero line nearer than CH1's, neither moves;
   with both lines at the same height CH1 can never be grabbed. Repro: drag CH2 to 10 px below CH1, switch CH2 off in
   its menu, touch 5-10 px below CH1's line and drag: nothing moves. **B** in that state. Fix: compare only enabled
   candidates; ties to the one drawn on top (CH2).
4. **Out-of-range `traceposition` traps a channel.** The new auto setup with Invert on: `scope_functions.c:1977` and
   `:2036` subtract from a `uint16` (wraps, then clamped to 399, the wrong edge) and `:1996`, `:2055` add `offset`
   with no upper clamp, so `traceposition` can exceed 449. Settings load does not range-check it
   (`scope_functions.c:4212, 4238, 6768, 6784`). Then `offset = 449 - traceposition` (`uint32`,
   `statemachine.c:721`) wraps and the channel is never grabbable, while its pointer is drawn clamped at the top
   (y 49) where no hit area is. Values 400..449 are reachable only near the top. Repro (inference, needs a DC input):
   Settings -> new auto setup, CH1 Invert on, DC coupling, a positive DC input, AUTO SET; the CH1 pointer sits at the
   top and cannot be dragged. **B** when hit. Fix: clamp `traceposition` to the drag range 6..396 at every writer
   (auto setup, load) and do the hit maths in `int32` on the clamped drawn position.
5. **The pick uses the point after the move, not the touch-down point.** The loop breaks at > 2 px
   (`statemachine.c:675-681`) and the pick reads `xtouch` / `ytouch` (`:716-1008`). A fast stroke is several reports
   past the touch-down by then, so the pick can miss the object or take its neighbour. Repro: flick quickly upward
   starting on a channel's zero line: often the trigger point moves instead. **A.** Fix: pick at
   `previousxtouch` / `previousytouch`.
6. **2 px dead band and 200 ms tap.** `statemachine.c:675`, `:690`. Finger jitter beyond 2 px turns a tap into a drag
   (a trace or the trigger point moves a few pixels); a quick touch aimed at a trace changes the time base. Repro: tap
   a trace briskly: the time/div changes; rest a finger and lift it: sometimes the trace shifts. **A.** Fix: 8 px dead
   band (inference: typical capacitive jitter; tune on the bench), and no tap action when the touch-down lands on a
   handle or object.
7. **Hidden tap zones overlap visible things.** `scope_functions.c:41-166`: the V/div bands (y < 100, y > 370) cover
   the trigger point marker, the cursor readouts, the measurement cards and the record slider row (y 452-469); the
   time base bands cover the trigger level pointer column (x 660-720). Repro: tap the record slider: CH1 or CH2 V/div
   changes. **A.** Fix: no tap action below y 449 or on any handle; the owner decides whether the V/div bands stay at
   all now that the CTRL steppers exist (they are a second, invisible home of `displayvoltperdiv`).
8. **A channel drag also moves the trigger point.** `statemachine.c:754, 763` add `MOVE_TRIGGER_POINT`; `:160-164`
   applies the x component. Repro: drag CH1 up with a slightly diagonal stroke: the H marker shifts. **A.** Fix: drop
   the flag from channel grabs; a channel drag is vertical only.
9. **The trigger level drifts against its channel at the edges.** `statemachine.c:174-192` move channel and level by
   the same `diff` but clamp them separately (`:5720-5729`, `:5804-5813`). Repro: CH1 trigger, level 1 div above the
   trace, drag CH1 to the top and back: the level now sits on the trace (its voltage changed). **A.** Fix: clamp the
   shared delta so both stay in range.
10. **EXT trigger follows CH2.** `statemachine.c:74, 188` test `if(scopesettings.triggerchannel)`, true for EXT (2).
    Repro (bitstream 0x1533+): trigger source EXT, drag CH2: the trigger level moves. **A.** Fix: test `== 1`.
11. **Hidden objects stay live.** The trigger level hit (`statemachine.c:869-895`) works while its pointer is hidden
    (long mode with Auto trigger, `scope_functions.c:631`); in X-Y a drag moves disabled channels too
    (`statemachine.c:169-172`, the `change_*` functions do not test `enable`). **C/A.** Fix: a hit only for what is
    drawn.
12. **Priority instead of proximity.** `statemachine.c:716-1008`: channels beat refs, level, time and volt cursors
    whenever within 30 px; with volt cursors on, channels are dead in x 310..410; refs pick the first in order, not the
    nearest (`:855-862`); time cursors beat volt cursors regardless of distance. Repro: volt cursors on, a cursor 15 px
    from CH1's line, touch on the cursor at x 200: CH1 moves. **A.** Fix: one nearest-object pick (section c).
13. **Locked cursors.** `statemachine.c:198-218`, `:5846-5855`, `:5885-5894`: the pair moves in two separately
    clamped steps, so the spacing collapses at an edge and lags a frame on fast strokes; only the left time cursor and
    the bottom volt cursor drive the pair. Dead code: `:906, 913, 958, 965` compute distances that are overwritten.
    Repro: lock cursors, drag the left time cursor to the right edge and back: the gap is 1 px. **C.** Fix: move the
    pair by one clamped delta from either cursor.
14. **The blocking loop stalls the scope and the PC link.** `statemachine.c:634-684`: a resting finger holds the main
    loop (no acquisition; the PC link answers only the BUSY set, E_BUSY after 250 ms, per `firmware/AGENTS.md`
    contexts). During a grab the loop runs but the acquisition is not re-armed (`scope_functions.c:736, 760`), so a
    host capture waits as long as a finger stays down. The viewer loop never polls MAIN. **A** for MCP sessions. Fix:
    a non-blocking touch state machine (section c).
15. **Touch reports are consumed outside the handler.** `fpga_control.c:719-720` reads the GT911 inside
    `fpga_done_conversion`, so a press can be seen before `scan_for_touch` starts its 200 ms clock, and a whole short
    tap can be consumed in a multi-call wait (inference). **A.** Fix: one reader; others only test a "touch pending"
    flag.
16. **Two fingers end a gesture.** `touchpanel.c:236-245` turns 2+ points into "no touch". A second finger ends a
    drag; in the blocking loop it counts as a release and, under 200 ms, as a tap (zoom). Lifting one finger starts a
    new gesture. **A.** Fix: keep tracking the first point (GT911 track id) and ignore extra points.
17. **Long mode wipes the record while dragging.** Every `change_*` / `move_*` step calls `scope_preset_values()` in
    long mode (`statemachine.c:5634, 5738, 5780, 5821, 5861, 5900, 5939, 5978`), so the roll view restarts on every
    pixel of a drag (inference: visible as a blank plot while dragging). **A.** Fix: preset once on release.
18. **Host race.** The PC poll runs between drag steps; if the host switches the trigger channel to the dragged
    channel mid-drag, `change_trigger_level_offset` uses a `previous_trigger_level_offset` never snapshotted for this
    drag (`statemachine.c:64-67, 178-181`) and the level jumps. **C.** Fix: snapshot all followers at grab time.
19. **Off-by-one gaps and jumps.** x == 60 and x == 660 belong to no object; taps at x 358..362, y == 100, y == 370,
    x 720..730 do nothing; `traceposition` 0..5 or 397..399 (auto setup writes 0 and 399) jumps to 6 / 396 at the first
    drag step. **C.** Fix: inclusive ranges from shared constants; one clamp range for all writers.
20. **Spectrum and phosphor.** Pan ignores Move fast (`spectrum.c:1509-1515`), though the plan makes Move fast the drag
    modifier in every mode; strip fields act at touch-down (`spectrum.c:1990-1999`, `phosphor.c:532-559`) where
    every other button acts on release; plot taps also fire on the header and the frequency labels (no y bound) with
    no centre dead band; a drag at full span gives no feedback. **C.** Fix: apply `movespeed` to `dx`; act on release;
    bound the tap area to the plot. The strips go away in UX step 2, so only the pan and tap fixes outlive it.

## (c) Proposed hit-test design (primary: the pointers are handles)

Principles: everything drawn as a handle is draggable and its hit box is at least 44 x 44 around what is drawn (the
stock icons stay their size; like the thin stepper bars, their hit area extends, `docs/ui-guidelines.md` "Touch");
only drawn, enabled objects are candidates; the nearest candidate wins, ties go to the one drawn on top; the pick is
made once, at the touch-down point.

1. **Left handle column (CH1, CH2, ref1..ref8).** Box x 0..44, y `tip - 22 .. tip + 21` (44 rows), with
   `tip = clamp(442 - tp, 49, 442) + 6` (the icon's first widest row, `icons.c:500`; equals the trace's zero row
   `448 - tp` when unclamped). Using the clamped drawn position means a pointer pinned at an edge is grabbable where
   it is drawn. Candidates in draw order CH1, CH2, ref1 .. ref8 (`scope_draw_pointers`, `scope_functions.c:316-625`);
   pick the smallest `|y0 - tip|` with `<=`, so a tie goes to the later-drawn (top) one. A tap in the column does
   nothing.
2. **Reference pointers** share the column: they are drawn 38 px wide from x 2 in the same column after the channels,
   so they are candidates of rule 1 with the same box, replacing the x < 60 band. A ref parked exactly on a channel
   hides the channel's pointer on screen and wins the tie, which is what the user sees; moving it away uncovers the
   channel. Later (not this batch): draw coincident pointers side by side so each keeps a visible handle.
3. **Trace bodies (secondary path).** For x0 in 45..685: for each enabled channel the distance from y0 to the nearer
   of its zero row (`448 - tp`) and its drawn trace at column x0 (from `settings->tracepoints`, taking the min..max
   span of points with |x - x0| <= 1, which covers the Peak and Pro styles' paired points). Radius 40 px, nearest
   wins, tie to CH2. In phosphor mode `tracepoints` is not filled by the renderer (inference), so only the zero row
   counts there.
4. **Trigger level handle** on the right: box x 686..730, 44 rows around `clamp(442 - tvp, 49, 442) + 6`; only while
   the pointer is drawn.
5. **Trigger point handle**: box 44 wide around the H marker, y 45..92; a background horizontal drag still pans it.
6. **Cursors** by proximity to their lines (radius 22), competing with rule 3 on distance, only while shown.
7. **One pick, then axis lock**: channel, level, ref, volt cursor drags use y only; time cursor and trigger point use x
   only. Followers (the trigger level for its source channel, the locked cursor pair) move by one shared clamped delta.
8. **Tap vs drag**: 8 px dead band before a drag starts; a tap is a lift inside the band within 250 ms; taps act only
   on the empty plot (no candidate under the touch-down point, y < 449).
9. **No blocking loop** (later step): a touch module owns the GT911 (the only reader) and keeps IDLE / PRESSED
   (x0, y0, t0, picked object) / DRAGGING / RELEASED; `touch_handler()` advances it once per main-loop pass, so the
   acquisition and the PC MAIN poll keep running; the re-arm gate becomes "not while DRAGGING" or goes.
10. **Move fast** divides `diff` for every drag including the spectrum pan.

### (c1) Fix batch 1: minimal diff, ready to dispatch

All in `firmware/scope`. No protocol change. Host-testable if the pick helper is a pure function (add
`host_tests/test_touch_pick.c` with the geometry cases below).

- `scope_functions.c` / `.h`: add `int32 scope_pointer_top(int32 position)` returning `position` clamped to 49..442;
  use it for the CH1, CH2, ref and trigger level pointers in `scope_draw_pointers` (`:333-345, 374-386`, the eight ref
  blocks, `:660-670`) so draw and hit share one formula. Add `#define POINTER_TIP_DY 6`, `#define HANDLE_W 44`,
  `#define HANDLE_HALF 22`.
- `statemachine.c`, new static helper above `scan_for_touch`:

  ```c
  //Nearest enabled left-edge pointer within its 44 px box at touch-down y; ties to the later-drawn one. 0 = none.
  static uint8 pick_left_handle(int32 y)
  {
    struct { uint8 on; uint16 tp; uint8 state; } c[10] = {
      { scopesettings.channel1.enable, scopesettings.channel1.traceposition, TOUCH_STATE_MOVE_CHANNEL_1 },
      { scopesettings.channel2.enable, scopesettings.channel2.traceposition, TOUCH_STATE_MOVE_CHANNEL_2 },
      { scopesettings.ref1, scopesettings.ch_ref1.traceposition, TOUCH_STATE_MOVE_REF1 },
      /* ... ref2 .. ref8 ... */
    };
    int32 i, d, best = HANDLE_HALF + 1;
    uint8 pick = 0;

    for(i = 0; i < 10; i++)
    {
      if(!c[i].on) continue;
      d = y - (scope_pointer_top(442 - (int32)c[i].tp) + POINTER_TIP_DY);
      if(d < 0) d = -d;
      if(d <= best) { best = d; pick = c[i].state; }
    }
    return (best <= HANDLE_HALF) ? pick : 0;
  }
  ```

  In X-Y display mode the CH1 pointer is a top pointer, so the helper is used only when `xymodedisplay == 0`.
- `scan_for_touch()`, plot branch (`:626-699`): after the touch-down point is stored (`:630-631`) and before the
  blocking loop, if `xymodedisplay == 0 && previousxtouch < HANDLE_W`, call `pick_left_handle(previousytouch)` and keep
  the result. On a lift without displacement and a picked handle, return without calling
  `scope_adjust_timebase_and_voltdiv()` (`:687-699`). After displacement, a picked handle sets `touchstate` to it
  (without `TOUCH_STATE_MOVE_TRIGGER_POINT`) and returns.
- Same function, the channel block (`:716-769`): use `previousxtouch` / `previousytouch` instead of `xtouch` /
  `ytouch` (defect 5); compute distances as `int32` from `448 - traceposition` (defect 4); build the choice as
  "nearest enabled within radius, tie to CH2" (defect 3) with radius 40 (rule 3; the zero row only in this batch, the
  trace body follows in batch 2); drop `| TOUCH_STATE_MOVE_TRIGGER_POINT` at `:754, 763` (defect 8). Replace the x > 60
  lower bound with `x >= HANDLE_W` and delete the x < 60 ref block (`:772-863`), now covered by the helper.
- `touch_handler()`: `:74` and `:188` `if(scopesettings.triggerchannel)` -> `== 1` (defect 10); in the
  `MOVE_CHANNEL_n` cases compute one `diff`, clamp it so both the channel and (when it follows) the trigger level stay
  in 6..396, and apply it to both (defect 9). Simplest shape: change `change_channel_n_offset` /
  `change_trigger_level_offset` to take the clamped delta as a parameter.
- `scope_functions.c:1977, 1996, 2036, 2055` and the loaders `:4212, 4238, 6768, 6784`: compute in `int32` and clamp
  `traceposition` to 6..396 (defect 4, and the 0 / 399 jumps of defect 19).

Bench check for the owner after the batch: each pointer (CH1, CH2, a ref) drags from its icon; with CH2 parked on CH1
the CH2 pointer moves first, then CH1 is reachable; with CH2 off, CH1 drags from anywhere within 22 px of its pointer;
a brisk tap on a pointer changes nothing; a diagonal CH1 drag leaves the H marker in place.

## (d) Implementation order and overlap with the UX makeover

| Batch | Defects | Files and functions | Overlap with `docs/plans/ux-makeover.md` |
|---|---|---|---|
| 1. Handles and pick fixes (c1) | 1, 3, 4, 5, 8, 9, 10, part of 19 | `statemachine.c` `scan_for_touch` plot branch, `touch_handler`, `change_*`; `scope_functions.c` `scope_draw_pointers`, auto setup, loaders | Same files as UX step 1 (menu rows), different functions (inference: step 1 edits `handle_main_menu_touch` and menu drawing). Merge after step 1 lands; expect textual conflicts only. |
| 2. Trace body and proximity pick, dead band, tap zones | 2, 6, 7, 11, 12, 13 | `scan_for_touch` pick rewritten as one `touch_pick()` (rules 3-8), `scope_adjust_timebase_and_voltdiv` | UX step 2 puts fold-outs over the plot's right edge and cards in the bottom row: the pick must ask the open fold-out first and must not fire taps on cards. Do after step 2, or coordinate one owner of the plot's touch order. Owner decision needed on the V/div tap bands. |
| 3. Non-blocking touch and one GT911 reader | 14, 15, 16, 17, 18 | `touchpanel.c`, `fpga_control.c:719`, `statemachine.c` `touch_handler`/`scan_for_touch`, `scope_functions.c:736, 760`, the viewer loop | Touches the main loop and every modal loop's use of `havetouch`; needs a bench soak with an MCP session (PC link while dragging). |
| 4. Spectrum | 20 | `spectrum.c` `spectrum_touch`, `spec_pan` | UX step 2 removes the spectrum and phosphor strips: do this after step 2 and only for the pan and tap parts. |
