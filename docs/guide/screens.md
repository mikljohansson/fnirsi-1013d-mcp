# The screens

The Menu button (top left) opens the main menu. It always shows the same eight rows, top to bottom, with no marker for
the current mode. This page follows that order. Rules the screens share: [UI guidelines](../ui-guidelines.md).

!!! note "To be written after fw 0.2.70"
    The text per screen is a first draft from the firmware notes; the screens are being polished and the screenshots
    are captured on the unit once the final firmware is on it (`tools/screenshots.py`, see the
    [site notes](#about-the-screenshots) below).

## Scope

The oscilloscope view: two channels at up to 200 MS/s each, with the right-hand button bar (RUN/STOP, AUTO SET,
MEASURES, CTRL ...), the top bar (channel, time base, trigger and generator buttons, the battery, and a chip when a
host is connected) and the bottom rows of measurement cards. Up to twelve measurements per channel are available
through MEASURES (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width, +Duty, -Duty); a new unit shows
Freq, Vpp and Vavg per enabled channel. The CTRL page holds the Display choices (including the digital phosphor view
with persistence 0.2 s, 1 s, 5 s or infinite, mono or heat palette).

![Scope view](screenshot:scope)

Touch on the plot, time/div and sample rate coupling, and the record: see the "Screens" sections of the
[firmware README](../../firmware/README.md).

## Spectrum analyser

An FFT of the channel on the scope: up to 4096 points, Hann, flat-top or rectangular window, averaging and max hold,
waterfall and density sub-views, tap to zoom, drag to pan, and Auto. Rolling mode is switched off when you enter it
(the screen says so). Cards show the spectrum measurements.

![Spectrum analyser](screenshot:spectrum)

## Frequency response

Measures a passive two-port (a filter, a divider, a tank) with the scope alone. Connect the calibration post to the
device's input, CH1 to the input, CH2 to the output and the device's ground to the probe ground clips, then press RUN.
The view plots the ratio CH2 / CH1 as magnitude (dB) and phase (degrees) against frequency, with -3 dB and peak
markers and two cursors. Passive or buffered devices only: the post is a 3.1 Vpp square through 1 kOhm and nothing
else protects it, so never connect a coil, a MOSFET drain or anything that can drive the post. The row is grey when the
bitstream lacks the filter and DDS (needs FPGA 0x1534 or later). Leaving the view gives back the generator, channels,
time base and RUN/STOP as they were. While no sweep runs, the post carries your generator setting (the one the
generator chip shows); a sweep takes it over and gives it back when it ends.

AUTO SET runs an overview from 100 Hz to 10 MHz and, for a sharp resonance, up to three finer passes, then sets the
span and starts RUN. If the result is not consistent it stops instead, keeps your settings and says why on the top
line. "Auto set: no consistent response. Check wiring" usually means a loose probe or ground clip, but the circuit
itself can cause it: a device that oscillates, a notch deeper than 20 dB that falls on one point, or a response that
changes between passes. "Auto set: peak not confirmed: sweep by hand" means a resonance too sharp or too isolated for
the overview's points; set Start and Stop around it in CTRL > Sweep and use Single sweep. "Auto set: CH1 level low:
CH1 tip on the DUT input?" and "Auto set: no response: CH2 tip on the DUT output?" point at the probe that reads too
little. Single sweep keeps the Sweep panel open: its button turns red ("Stop sweep", with the points done) while the
sweep runs, and a tap stops it.

![Frequency response](screenshot:frequency-response)

The same measurement from a host: `scope_bode_sweep` and `fnirsi1013d-bode` ([mcp README](../../mcp/README.md), "Bode
sweep"). Design and bench: [engineering articles](../articles/index.md).

## Saved pictures

The viewer for screenshots saved to the SD card (BMP files). Files are stamped with the time when the clock module is
on, else with the file name (Settings > Files).

![Saved pictures](screenshot:saved-pictures)

## Saved waveforms

The viewer for waveforms saved to the SD card. Reference traces can be loaded at power-on (Settings > Files).

![Saved waveforms](screenshot:saved-waveforms)

## Settings

The fold-out has the screen and grid brightness sliders, Time and date (only with a DS3231 clock module) and Other
settings, which opens the Settings screen: a full-screen page with Display, Controls, Files, Clock and Start-up groups
and a Restore default settings button. A tap confirms nothing destructive without asking first.

| Group | Rows |
|---|---|
| Display | Screen brightness, Grid brightness |
| Controls | File message timeout, Cursors (move together / freely), Auto setup (improved / stock) |
| Files | Stamp screenshots with (time / file name / nothing), Reference traces at power-on |
| Clock | Clock, Time and date |
| Start-up | At power-on: Open firmware / Boot menu then open firmware / Boot menu then stock firmware |

![Settings](screenshot:settings)

## Diagnostics

Four pages, updated once a second, with a Back button; PC control keeps working while it is open.

- **Status**: identity (firmware, git hash, protocol, USB serial), the FPGA line with the bitstream's features, tiles
  for Firmware, Restarts, USB and Battery, and the calibration verdict with Calibrate and Save report to card.
- **Calibration**: per range the PWM centre, the volts factor and the zeros, the gains per class and core, the noise
  and the background tracking; Calibrate, DC shift trim, Volts calibration, Clear calibration.
- **Log**: the event log decoded in words, newest first, with the counters, the last restart and the card.
- **Developer**: for firmware work (Enter FEL now).

A state is shown as a word and a colour: OK green, Check amber, Fault red, not available grey.

![Diagnostics, Status page](screenshot:diagnostics)

## USB drive

Makes the SD card a USB drive on the PC. PC control (MCP) is disconnected while the screen shows. Eject the drive on
the PC before tapping Exit; Exit with the drive still mounted shows "Not allowed!".

![USB drive](screenshot:usb-drive)

## About the screenshots

The pictures on this site are captured from the scope's own screen over USB by `tools/screenshots.py`, so they can be
retaken whenever the firmware changes. A page that shows "Screenshot pending" is waiting for its picture.
