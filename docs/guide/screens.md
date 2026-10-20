# The screens

The **Menu** button (top left) opens the main menu. It always shows the same eight rows, top to bottom: Scope,
Spectrum analyser, Frequency response, Saved pictures, Saved waveforms, Settings, Diagnostics, USB drive. The first
three are modes: each one changes what the scope measures. The rows below them are utilities. There is no marker for
the current mode; the screen shows it. This page follows the menu from top to bottom. The rules all screens share are
in the [UI guidelines](../ui-guidelines.md).

## What every mode shares

**Top bar.** The same in every mode: Menu, the CH1 and CH2 chips, the brightness sun, the ACQ chip, Move fast / Move
slow, the trigger chip with the run state, the generator button and the battery. A green **MCP** chip shows while a
program on the PC has the scope's serial port open or has sent it a request in the last moments.

- Tap a channel chip for the input: V/div, coupling, probe factor, invert.
- Tap ACQ for the acquisition: Sample Rate, Time per Division, Memory, roll mode (Long mode / Short mode), Average
  mode and Filter. The menu also shows the pass time, how long one screen update takes.
- Tap the trigger chip for the trigger: Auto, Single or Normal, the edge, the source (CH1, CH2, or EXT on bitstream
  0x1533 and later), the position and Always 50 %.
- The run-state chip beside the trigger reads STOP, RUN or WAIT (a single shot waiting for its trigger).
- The sun switches the screen to full brightness and back.

**Right bar, first page.** The same slots in every mode: CTRL, RUN/STOP, AUTO SET, T CURSOR, V CURSOR, MEASURES,
SAVE PIC, SAVE WAVE. The RUN/STOP button shows what a tap does: green RUN while stopped, red STOP while running. A
mode that has no use for a slot leaves it empty.

**Right bar, CTRL page.** CTRL switches the bar to the page of controls for how the plot is shown. The order is the
same in every mode: Display first, then the mode's own analysis controls, then Grid. A button that holds a value shows
it and opens a fold-out beside the bar. A tap outside a fold-out closes it and does nothing else.

**Bottom row.** Measurement cards, chosen with MEASURES. Each card is a name in the colour of its source and a value.
With more cards chosen than fit, the last place reads "+N more". Under the cards is the slider row: a small overview
of everything the plot shows a part of, with a window for the part on screen. Drag the window to move it, or tap the
track outside the window to jump there.

**Warnings.** One orange line at the top middle of the plot. It states the consequence, then what to do, for example
"CH1 overrange: increase V/div".

## Scope

The oscilloscope: two channels at up to 200 MSa/s each.

![Scope view](screenshot:scope)

**Measurement cards.** MEASURES opens "Scope measurements". It offers twelve measurements per channel: Vmax, Vmin,
Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width, +Duty, -Duty. Choose the source (CH1 or a reference trace) at the
top of the menu. A new unit shows Freq, Vpp and Vavg on each enabled channel. The cards appear in the order you chose
them; the number at the top left of each item in the menu is its place. Freq, Period, the widths and the duties read
"no period" while the record holds fewer than three crossings. The values are taken over the whole record, and Vrms
includes the DC level.

**Touch on the plot.**

- Drag a pointer at the left edge to move that channel or reference trace up or down.
- Drag a trace, a cursor or the trigger level pointer to move it.
- Drag an empty part of the plot sideways to move the trigger point.
- Tap the empty plot to zoom. The middle of the left half shows a slower time/div, the middle of the right half a
  faster one. The top band makes the channel more sensitive (CH1 on the left half, CH2 on the right), the bottom band
  less sensitive.

**Time/div and sample rate.** While the scope is running, the time/div picks the sample rate. Each zoom step takes
the fastest rate whose record covers the ten divisions with at least ten samples per division. Zooming in ends at
50 ns/div at 200 MSa/s. Zooming out ends at 200 ms/div, at the border of roll mode. A rate you pick by hand in the ACQ
menu holds until the next zoom step. When a rate makes the time/div impossible, the time/div snaps to the nearest one
that works, and "Time/div set to ... for this rate" shows for a few seconds. While stopped, the record on screen keeps
its rate: the zoom goes only as far as that record allows, then says "Stopped: press RUN to zoom further".

**CTRL page.**

| Button | What it does |
|---|---|
| Display | Normal or X-Y, and the Phosphor switch (below) |
| Trace style | Linear, Dots or Peak. Grey in X-Y |
| Grid | Grid lines on (green) or off (grey) |
| V+ / CH1 / V- | One V/div step for CH1 |
| V+ / CH2 / V- | One V/div step for CH2 |
| 50% TRIG | Sets the trigger level to the middle of the signal |

**Phosphor.** The Phosphor switch in the Display fold-out draws the traces as a graded picture: bright where many
records hit, dim where few did. Jitter and rare glitches show with their relative frequency. Its second line shows
the records per second ("55 wfm/s"). While it is on, the fold-out adds:

- Persistence: 0.2 s, 1 s (default), 5 s or Infinite. The time a pixel that is no longer hit takes to fade.
- Palette: Mono (the channel colour) or Heat.
- Clear: clears the picture.

The picture clears by itself when what a pixel means changes, for example on a V/div or time base change. It is not
available in roll mode ("Not in roll mode"). The scope draws about 30 records per second, so a rare event needs time to
show.

![Phosphor view](screenshot:phosphor)

**Warnings.** "No channel enabled: switch on CH1 or CH2", "X-Y needs both channels: switch on CH1 and CH2", "CH1
overrange: increase V/div" (or CH2), "No trigger: check the trigger level".

## Spectrum analyser

An FFT of the channels on the scope. The Scope row of the main menu brings the oscilloscope back. Roll mode is switched
off when you enter, and the warning line says "Roll off for the spectrum".

![Spectrum analyser](screenshot:spectrum)

**Levels.** The vertical axis is in dBV, the RMS level of a sine. In current mode (a probe set to A) it is dBA. The
plot has eight divisions. A white triangle marks the highest peak.

**Span and centre.** The view shows a span around a centre frequency. The span steps through 1, 2, 5 Hz up to 20 MHz,
then the whole axis 0 to 25 MHz. The front end rolls off near 30 MHz, so the view stops at 25 MHz.

- Tap the right half of the plot to narrow the span one step, the left half to widen it. The centre stays.
- Drag the plot or the slider window to move the centre. The slider track shows the whole axis the current sample
  rate covers.
- Each zoom step also sets the sample rate: the slowest rate that covers the span and takes at most about 2 s per
  record. A move never changes the rate. To look above the end of the track, zoom out, move, and zoom in again.
- After a rate change the view says "Waiting for a capture at the new rate" until a new record arrives.
- While stopped, the record keeps its rate. Where only a new record could go further, the view says "Stopped: press
  RUN to zoom further".

**AUTO SET** finds the signal and sets the FFT length, window, scale, reference level and span from it. It takes one
to two seconds, and the button reads "Auto..." meanwhile. A second tap, or any touch on the plot, cancels it. It needs
RUN and the trigger mode Auto or Normal; otherwise the view says "Auto needs RUN: press RUN" or "Auto needs trigger
mode Auto or Normal".

**CTRL page.**

| Button | Fold-out rows |
|---|---|
| Display | Spectrum, Waterfall or Density. In Waterfall and Density also Channel (CH1 or CH2) |
| Analyse | Analyse (CH1, CH2 or CH1 + CH2), FFT length (1024 to 4096; "(record)" when the record holds fewer samples), Window (Hann, Flat top, Rect), Average (Off, 4, 16, 64, Max hold), Scale (5, 10 or 20 dB/div), Ref level (- / +, the level of the top line), Reset |
| Grid | The plot's grid on or off. The level labels stay |
| V+ / CH1 / V-, V+ / CH2 / V- | One V/div step for the channel |

Reset puts back 2048 points, Hann, 10 dB/div, 0 dBV at the top and the whole axis of the current rate. Average 4 is
the default. It averages the dB levels over the last captures, which steadies the display; on noise it reads about
2.5 dB low.

**Waterfall and Density.** Both show one channel, chosen in the Display fold-out. Waterfall adds one row per capture
at the top, coloured from the bottom line (black) to the reference level (white). Density counts how often each point
of the plot is hit, with a slow fade, from "rare or old" to "frequent". Both clear when the axis changes. While stopped
they wait for RUN.

![Spectrum waterfall](screenshot:spectrum-waterfall)

**Measurement cards.** MEASURES opens "Spectrum measurements": per channel Peak, Level, THD and Noise, and for the view
RBW, Span, Center, Time and Capture. Time is the history the waterfall shows. Capture is the time one record takes
(memory depth / sample rate). THD reads "f0 too low" or "f0 too high" when the fundamental is too close to an end of
the axis. The 8-bit ADC limits THD to about 0.3 %.

**Warnings.** "CH1 overrange: increase V/div", "Aliasing possible: set Acquisition > Filter" (below 200 MSa/s with
the filter off; "use a faster time base" on a bitstream without the filter), "Stopped: press RUN to fill the view",
"No trigger: check the trigger level".

## Frequency response

Measures the frequency response of a passive two-port, such as a filter, a divider or an LC tank, with the scope
alone. The calibration post drives the device under test (DUT), CH1 reads its input, CH2 its output. The view plots
CH2 / CH1 as magnitude in dB (cyan, left axis) and phase in degrees (pink, right axis) against frequency. The row is
grey on a bitstream without the decimation filter and the DDS generator; tapping it then says "Frequency response
needs FPGA 0x1534 or later".

!!! warning "Passive or buffered devices only"
    The post is a 3.1 Vpp square wave through 1 kOhm, and that resistor is its only protection. Never connect a coil
    that is driven by something else, a MOSFET drain, or anything else that can push current into the post.

**Wiring.**

1. Connect the calibration post to the DUT's input.
2. Put the CH1 probe tip on the DUT's input and the CH2 probe tip on its output.
3. Clip both probe grounds to the DUT's ground. Do not ground the DUT on the USB connector's shell: that ground sits
   about 1.2 V below the probe ground.
4. The bench sweeps use both probes at 1X.

**Entering and leaving.** The view takes over the generator, both channels, the time base and RUN/STOP. Leaving
through the Scope or Spectrum analyser row gives every one of them back as it was. Nothing is driven until a sweep
starts. Until the first point the top line says: "Connect the calibration post and CH1 to the input, CH2 to the
output, grounds on the probe clips. Press RUN." An EXT trigger is set to CH1 on entry, because the EXT input is the
generator's pin.

![Frequency response, before the first sweep](screenshot:frequency-response)

**Right bar, first page.**

| Button | What it does |
|---|---|
| RUN/STOP | RUN repeats sweeps; each new sweep overwrites the chart point by point. STOP ends the sweep after the capture in progress. The measured points stay |
| AUTO SET | Finds the span, Settle and CH2 range, runs one sweep and stops (below) |
| T CURSOR | Cursors C1 and C2. They snap to measured points. Drag a flag, or tap the plot to move the nearer cursor. The box at the bottom left shows f, level and phase for C1, C2 and C2-C1 |
| MEASURES | "Frequency response measurements": Level, -3dB, BW, Peak, Q, Ph45, Ph90, Ph180, Sweep, Thru. Defaults: -3dB, Peak, Q, Level |
| SAVE PIC | A screenshot |
| SAVE WAVE | The sweep table as a CSV file, `bode/FR00001.CSV` and up. Read it on a PC through USB drive |

V CURSOR is empty in this mode. Before the first sweep the cards read "no sweep"; while a new table fills they read
"sweeping"; a marker that is not in the data reads "not found". White triangles on the chart mark the -3 dB points,
a resonance peak and the phase crossings.

**AUTO SET** runs an overview from 100 Hz to 10 MHz (20 points). For a sharp resonance it measures up to three finer
passes around the peak. Then it sets Start, Stop, Settle and the CH2 range, runs one sweep and stops. The chart and the
cards show that sweep. It never starts RUN; press RUN yourself to repeat. The button reads "Auto..." until the last
sweep ends. A second tap or STOP ends that sweep early and keeps the points measured so far. A tap during the overview
cancels AUTO SET and says "Auto set cancelled: settings kept".

AUTO SET chooses CH2 range Hold when the response inside its span stays within 20 dB, so RUN then switches no relay.
Otherwise it chooses Auto, and the relays click where CH2 needs a different range. A flat response gives "No corner
from 100 Hz to 10 MHz: span kept" and one sweep at the span you had.

When the result does not add up, AUTO SET stops, keeps your settings and says why on the top line:

| Message | Likely cause |
|---|---|
| Auto set: no consistent response. Check wiring | A loose probe or ground clip. Also a DUT that oscillates, a notch deeper than 20 dB that falls on one point, or a response that changes between passes |
| Auto set: peak not confirmed: sweep by hand | A resonance too sharp for the overview's points. Set Start and Stop around it in CTRL > Sweep and use Single sweep |
| Auto set: CH1 level low: CH1 tip on the DUT input? | CH1 sees too little |
| Auto set: no response: CH2 tip on the DUT output? | CH2 sees nothing |

**CTRL page.**

| Button | Fold-out rows |
|---|---|
| Display ("Overlay", "Mag", "Phase") | Overlaid, Magnitude only, Phase only; Frequency axis Log (default) or Linear. Linear changes only the drawing: the points stay log spaced |
| Sweep ("50 pt") | Start (1 Hz to 5 MHz, default 100 Hz), Stop (10 Hz to 10 MHz, default 1 MHz), Points (10, 20, 50, 100), Settle (Fast, Normal, Resonant, High Q), CH2 range (Auto, or the held V/div), Single sweep |
| Thru ("None", "On", "Off", "Stale", "Meas.") | Measure thru, Apply, Clear |
| Trace style | Lines (default) or Dots |
| Grid | The grid on or off |

- Start and Stop step in 1-2-5 steps; tap the left half of the row to go down, the right half to go up.
- Settle is how many periods each point waits before it is recorded: Fast 3, Normal 10, Resonant 50, High Q 200. Use
  a longer Settle for a sharp resonance.
- CH2 range: a tap switches between Auto and Hold. Hold keeps the range CH2 has at the tap; set it first in the CH2
  menu.
- Single sweep keeps the panel open and shows what a tap does. Stopped, green "Single sweep" with the estimated time
  ("about 3 s") runs one sweep. While RUN repeats, green "STOP after this sweep" makes the current sweep the last.
  During a single sweep, red "Stop sweep" with the progress ("12 / 50") stops it. It is grey during AUTO SET.
- Thru removes the difference between the two channels. Put both probe tips on the DUT input and tap Measure thru;
  that runs one sweep. Apply divides it out of later sweeps. Its second line says whether it "matches this sweep".
  The thru is kept until power-off, and it turns stale when Start, Stop or Points change.

![Frequency response after a sweep](screenshot:frequency-response-sweep)

**Points shown grey** did not pass a check, for example "unsettled", "near floor" or "CH2 clipped". The cursor box
names the reason. They are never joined to the curve.

**Slider row.** The track spans 1 Hz to 10 MHz on a log scale, and the window is Start to Stop. After AUTO SET the track
shows its overview. Drag the window to move Start and Stop together. On release a running sweep restarts at the new
span; while stopped, one sweep runs there.

**Top bar in this mode.** The ACQ chip reads "Set by the sweep": the sweep picks the sample rate for each point. While
a sweep runs, the generator button shows the tone being measured, and a tap on it says "Driven by the frequency response
sweep". When no sweep runs, the post carries your own generator setting again. The trigger menu says "Not used by the
sweep".

**Warnings while sweeping.** Among others: "CH2 overrange: set CH2 range Auto or raise V/div", "N points not settled:
choose a longer Settle", "CH1 level low: the DUT loads the 1 kOhm source", "CH1 level low above 2 MHz: the x1 probe
rolls off", "Thru does not match this sweep: measure again", "DC level beyond the offset range: AC-couple DUT",
"Sweep stopped: the PC took over", "ADC readout fault: see Diagnostics".

The same measurement from a PC: `scope_bode_sweep` and `fnirsi1013d-bode` ([mcp README](../../mcp/README.md), "Bode
sweep").

## Saved pictures

The file browser for the screenshots that SAVE PIC writes to the card (BMP files in `pictures`). Tap a thumbnail to
open the picture. A delete asks "Delete from the SD card?" with Cancel and Delete. Files carry the time when a clock
module is fitted and switched on, else the file name (Settings > Files > Stamp screenshots with).

![Saved pictures](screenshot:saved-pictures)

## Saved waveforms

The file browser for the waveforms that SAVE WAVE writes (files in `waveforms`). Open one to see it on the scope
screen, with its measurement cards. The Frequency response CSV files are not listed here. Reference
traces can be loaded at power-on (Settings > Files > Reference traces at power-on).

![Saved waveforms](screenshot:saved-waveforms)

## Settings

The Settings row opens a fold-out:

- **Screen brightness** and **Grid brightness**: sliders beside the fold-out.
- **Time and date**: only with a DS3231 clock module fitted.
- **Other settings**: opens the Settings screen.

The Settings screen is a full page with Back at the top right. Each row shows its value on the right. A green value is
in use, grey is off or not available, amber needs attention.

| Group | Row | Values |
|---|---|---|
| Display | Screen brightness | 0 to 100 %, - / + in steps of 5 |
| Display | Grid brightness | 0 to 100 %, - / + in steps of 5 |
| Controls | File message timeout | wait for a tap, or 0.7 s |
| Controls | Cursors | move together, or move freely |
| Controls | Auto setup | improved, or stock (the Scope's AUTO SET) |
| Files | Stamp screenshots with | time, file name or nothing |
| Files | Reference traces at power-on | loaded, or not loaded |
| Clock | Clock | shown, not used, or no clock module |
| Clock | Time and date | opens a page with Hour, Minute, Day, Month, Year; each change is written to the clock module at once |
| Start-up | At power-on | Open firmware; Boot menu, then open firmware; Boot menu, then stock firmware |

At power-on is written to the card when you tap Back, and only when it changed.

**Restore default settings** (bottom left) asks first: "Channels, trigger, time base, display and these settings go
back to their defaults. Calibration and start-up are kept."

![Settings](screenshot:settings)

## Diagnostics

Four pages, chosen with the tabs at the top: Status, Calibration, Log and Developer. They update every second. A PC
can keep working with the scope while Diagnostics shows. Back returns to the scope.

A state is a word and a colour: OK green, Check amber, Fault red, grey not available.

- **Status**: the firmware identity (version, git hash, protocol, USB serial), the FPGA line with the bitstream and its
  features, tiles for Firmware, Restarts, USB and Battery, and the calibration verdict with its reason. Buttons:
  Calibrate (the [wizard](calibration.md)) and Save report to card (a text file in `reports`).
- **Calibration**: per channel and range the offset centre, the volts factor, the zeros and gains of both ADC cores,
  the noise and the background tracking. Buttons: Calibrate, DC shift trim, Volts calibration, Clear calibration.
- **Log**: the event log in words, newest first, with Older events and Newest events to page through it. Also the
  counters since power-on, the last restart and the SD card state.
- **Developer**: "For firmware work. Normal use needs none of this." Enter FEL now restarts the scope into the USB
  boot loader, after a confirmation.

![Diagnostics, Status page](screenshot:diagnostics)

![Diagnostics, Calibration page](screenshot:diagnostics-calibration)

Clear calibration is red and asks first. It clears the zeros and gains and keeps the volts factors; readings use
defaults until you calibrate again.

## USB drive

Makes the SD card a USB drive on the PC. The screen says "The SD card is now a USB drive on the PC." PC control (MCP)
is disconnected while it shows. Eject the drive on the PC before you tap Exit. Exit with the drive still mounted shows
"Not allowed!".

![USB drive](screenshot:usb-drive)

## About the screenshots

The pictures on this site are read from the scope's own screen over USB by `tools/screenshots.py`, so they can be
taken again whenever the firmware changes. A page that shows "Screenshot pending" is waiting for its picture.
