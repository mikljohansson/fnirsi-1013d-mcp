# Calibration

Each channel has two interleaved ADC cores. The firmware measures a zero and a gain per core and clock class, and
tracks the full-rate drift in the background. Calibrate on first use, and again after changing the FPGA bitstream.

## Start the wizard

Menu > Diagnostics > Calibrate (on the Status and Calibration pages), or Calibrate now on the First start screen.

## The four screens

The wizard is four step dialogs over the dimmed, frozen scope screen. Nothing is written before the last one; Cancel on
any screen, also while a measurement runs, writes nothing.

| Screen | What you do |
|---|---|
| 1. Zero level | Disconnect both probes and the USB cable from the device, then tap Calibrate. While a host is attached the screen says so in amber. Both channels are measured; Continue appears when both pass. |
| 2. CH1 gain | Plug the CH1 probe in, connect it to the calibration post, set the probe to 1X, tap Calibrate. |
| 3. CH2 gain | The same for CH2. |
| 4. Done | The result per channel (for example "cores matched to 0.1 code, gain mismatch 1.3 %"). Save calibration writes it to the card. |

A failure keeps Calibrate available for another try; there is no Skip. On the stock bitstream (0x1432) there is no
generator, so the gain steps are not possible: Continue keeps the gains and saves the zero level.

![Calibration wizard, zero level](screenshot:calibration-zero)

## The volts calibration

When the volts factors are missing (the card sector did not load), the wizard goes on to the volts calibration: both
inputs on one DC supply, stepping through 300 mV, 600 mV, 1.5 V, 3 V, 7.5 V and 15 V. It is also reachable by itself
from Diagnostics > Calibration > Volts calibration.

## Checking the result

Diagnostics > Status shows one calibration verdict (OK, Check or Fault) with the reasons, Calibration shows the values
per range and the background tracking. Details and limits: [firmware README](../../firmware/README.md) section 6.4, and
the [glossary](../glossary.md) for the terms.

!!! note "To be written after fw 0.2.70"
    The wizard on the final firmware with screenshots, what a typical amber result means, and when to calibrate
    again. The story of how the calibration was built is an engineering article (see [Articles](../articles/index.md)).
