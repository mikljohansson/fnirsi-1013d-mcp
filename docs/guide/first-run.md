# First run

## The boot menu

On power-on the bootloader shows its menu: PECO (the open firmware, preselected when the boot mode is 4), FEL, FNIRSI
(the stock firmware) and Update. Leave it alone and the open firmware starts. The start-up choice can be changed later
in Menu > Settings > Other settings > At power-on.

## The First start screen

A unit whose calibration is not valid on both channels shows a full screen "First start" at every boot until it is
calibrated. It lists the checks:

- the FPGA is recognised, with its features (or the stock bitstream is named in amber);
- the settings were loaded, or defaults are used;
- the calibration is missing or does not match the offsets;
- when the volts calibration is missing too, that is listed.

It says what calibrating needs and offers **Later** (left, goes to the scope) and **Calibrate now** (right, starts the
wizard). A calibrated unit starts with no screen at all, also after the settings were reset.

![First start screen](screenshot:first-start)

## What to do next

1. Calibrate: [Calibration](calibration.md).
2. Look at the screens: [The screens](screens.md).
3. Optional: connect an agent over USB ([Agent setup](../agent-setup.md)). The scope shows a chip in the top bar
   while a host is connected.

!!! note "To be written after fw 0.2.70"
    A walk-through with the final screens, and what each line of the First start screen means when it is amber.
