# First run

## The boot menu

At power-on the bootloader on the card shows its menu with four buttons: PECO (the open firmware), FEL (the USB boot
mode for loading firmware from a PC), FNIRSI (the stock firmware) and Update. With the card written as the
[install page](install.md) says, PECO is preselected. Leave the menu alone for about 2 seconds and the open firmware
starts; a touch keeps the menu open.

You can change this later in Menu > Settings > Other settings > **At power-on**:

| Value | At power-on |
|---|---|
| Open firmware | The open firmware starts at once, without the menu |
| Boot menu, then open firmware | The menu, with PECO preselected |
| Boot menu, then stock firmware | The menu, with FNIRSI preselected |

## The First start screen

The open firmware checks its calibration at every start. While either channel is not calibrated, a full screen
"First start" appears before the scope. A calibrated unit starts with no screen at all, also after a settings reset.

The screen lists what the firmware found, one line per check:

| Line | What it means |
|---|---|
| FPGA recognised: AL3A10 bitstream 0x153A: ... | The FPGA bitstream and the features it has |
| FPGA stock bitstream 0x1432: no generator, no filter; gain calibration not possible (amber) | The stock bitstream. The scope works, but without the generator, the filters and the gain steps of the calibration |
| FPGA bitstream ...: not recognised; the scope runs with the stock limits (amber) | An unknown bitstream. The firmware uses only what the stock bitstream has |
| FPGA AL3A10 bitstream 0x1535: ADC capture timing race, fixed in 0x153B (amber) | An older bitstream with a known fault. The firmware works around it; flash the recommended bitstream |
| Settings loaded from the card. | The saved settings were read |
| No saved settings on the card: defaults used. | A new card, or the settings sector could not be read |
| Calibration missing: readings may be several % off. (amber) | No calibration on the card |
| Saved calibration does not match the current offsets: calibrate again. (amber) | A calibration exists, but it was made under other offsets |
| Volts calibration missing too: it needs a DC supply up to 15 V. (amber) | The card also lacks the volts factors (see [Calibration](calibration.md#the-volts-calibration)) |

Below the checks the screen says what calibrating needs:

- Calibrate after the scope has been on for 5 minutes.
- The zero step takes 8 seconds and needs no probes.
- The gain step needs a probe on the calibration post, one channel at a time. On the stock bitstream it says instead
  that this bitstream keeps the gains.

Two buttons:

- **Later** (left) opens the scope. The screen comes back at the next power-on. Calibrate is in Menu > Diagnostics.
- **Calibrate now** (right) starts the [calibration wizard](calibration.md).

!!! note "Warm up first"
    A calibration taken in the first 5 minutes is accepted, but Diagnostics marks it "recalibrate after 5 minutes of
    warm-up". If the scope was just switched on, tap Later, wait, then calibrate from Menu > Diagnostics.

## What to do next

1. Calibrate: [Calibration](calibration.md).
2. Learn the screens: [The screens](screens.md).
3. Optional: connect an AI agent over USB ([Agent setup](../agent-setup.md)). A green MCP chip in the top bar shows
   while a program on the PC talks to the scope. You and the agent share the same settings: either side can change
   them.
