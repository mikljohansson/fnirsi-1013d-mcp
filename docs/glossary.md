# Glossary: the 1013D signal chain as this project names it

Written for the owner on 2026-10-19 after a bench day; facts from `research/notes/SYNTHESIS.md`, `firmware/README.md`,
`fpga/al3/README.md` and `docs/plans/bode-design.md`. One line per term, grouped from the probe tip to the screen.

## Analog front end

- **Front end**: the analog circuitry between the BNC and the ADC: relay-switched attenuators, the coupling switch, the
  offset injection and the buffer amplifier.
- **Range, hardware range**: one of six attenuator settings chosen by relays (5, 2.5, 1, 0.5, 0.2, 0.1 V/div). 50 mV/div
  and below are **software zoom**: the 0.1 V/div range is used and the trace is scaled on screen.
- **Coupling**: DC passes everything; AC puts a capacitor in series so the DC level is removed before the amplifier.
- **Probe reference, VRF1**: the node the probe ground clips and the BNC shells sit on. It is tied to the scope's internal
  ground through a 100 ohm resistor (R71), so it is not the same node as **USB ground**: a circuit grounded on the USB shell
  sits about 1.2 V below the probe reference (bench 2026-10-19).
- **Offset, offset PWM, dcoffset, trace position**: a DC voltage added at the ADC input so a signal can be placed
  vertically. The FPGA generates it as a 25 kHz PWM; `dcoffset` is the host protocol's name for the same knob; the trace
  position is the screen-side result.
- **Window**: the span of input voltage the ADC can represent at a range, 8 divisions high on screen. Outside it the code
  clips at 0 or 255. A stage ahead of the offset injection can also **saturate**; then the offset cannot bring the signal
  back and the record is a noise-free flat line.

## Conversion

- **ADC**: one AD9288 per channel, each holding two 8-bit converters, the **cores A and B**, each at 100 MS/s. The FPGA
  clocks them half a period apart (**interleaving**) for 200 MS/s together.
- **Encode, encode phase**: the clock edge that tells a core to sample. When a core's encode lands where the data lines
  are changing, the stored byte is garbage: a **metastable** core reads 20-35 codes RMS on a quiet input.
- **Core order**: which of the two bytes in a stored word is the earlier sample. The firmware assumes an order; it can
  differ between bitstreams (0x1535 vs 0x1537 on chip 2, bench 2026-10-19).
- **Code, LSB**: one step of the 8-bit converter, 0..255; 0 V sits near 128 after the offset calibration. One division is
  32 codes, so one code is about 3 mV at 0.1 V/div and 156 mV at 5 V/div.
- **Zero level, gain, calibration**: per range and per core, the code at 0 V (zero) and the codes per volt (gain). The
  calibration wizard measures them from the internal square wave. **Volts calibration** is the older 1.00o5 procedure for
  the attenuator scale factors using an external DC source.
- **Clock classes, full-rate and gated**: at 200 MS/s both cores run continuously (**full-rate**); at slower legacy rates the
  FPGA stores a sample only every N clocks (**gated**). The cores behave slightly differently in the two cases, so the
  calibration keeps values per class.
- **Tracker, class probe**: background code (fw 0.2.50+) that keeps measuring the small A-B and class differences while
  the scope runs and corrects the readout; it switches the sample rate briefly every 30 s to do so.

## Sampling and filtering

- **Sample rate, divider**: how often a sample is stored. The FPGA divides its 200 MHz clock by (divider + 1): 0 is
  200 MS/s, 99 is 2 MS/s, 3999 is 50 kS/s.
- **Decimation, boxcar, CIC, FIR**: at low rates the FPGA can average the fast samples instead of discarding them.
  Boxcar is a plain average; CIC a cascaded averaging filter with a soft roll-off; the FIR a 15-tap filter after the CIC
  that flattens its droop with a sharp cut-off (bitstream 0x1535+). Filtered samples are **16-bit** words.
- **Legacy mode**: the plain 8-bit path without a filter, what the stock firmware always uses.
- **Aliasing**: a signal above half the sample rate folding back to a lower frequency. The filters reject it; discarding
  samples does not.
- **Gibbs ringing**: the overshoot a sharp-cut-off filter adds to a fast edge; with the FIR 0.5-7 % depending on where the
  edge falls on the sample grid.

## Capture

- **Record, memory depth**: the block of samples stored for one acquisition, 3000..16384 per channel in FPGA RAM.
- **Arm, trigger, pretrigger**: arming starts the FPGA filling memory; the trigger event fixes where the record ends;
  the pretrigger is the part of the record before it.
- **Capture**: one armed-and-read record. **Within-capture** noise is the spread inside one record; **between-capture**
  differences are drift or pickup.
- **Time/div window, record slider**: the part of the record shown on screen and the bar under the grid that moves it.
- **Segment, segmented capture**: with 0x1536+ the record memory holds many short records, each with its own trigger and
  timestamp, filled in one run.
- **Hardware averaging**: the FPGA sums several records itself (0x1536+) instead of the firmware averaging afterwards.
- **Phosphor**: a display variant that accumulates many records into a graded picture.

## Instrument level

- **Bitstream, version word**: the FPGA configuration, stored in the 1 MB flash beside the FPGA (W25Q80); the firmware
  reads a 16-bit version word (0x1432 stock, 0x1532..0x1538 ours) to know the features.
- **Firmware, FEL, SD card**: the scope program boots from the SD card. **FEL** is the SoC's USB boot mode that lets a
  firmware be loaded into RAM without touching the card; a power cycle returns to the card's version.
- **Calibration post, generator, DDS**: the metal post outputs a logic-level square wave from the FPGA's DDS (a phase
  accumulator that sets the frequency), through a 1 kohm resistor; the scope's only signal source.
- **Protocol, caps**: the USB command set between PC and firmware (versioned 1.x, `docs/protocol.md`); caps are the
  capability bits that say which features this firmware and bitstream have.
- **Settings sector, calibration sector**: SD sectors 709 and 708 hold the saved settings and the calibration.

## Bode terms

- **Thru**: both probes on the same node; records the channel-to-channel gain and phase to subtract from later sweeps.
- **Floor**: CH2 on ground while CH1 is driven; the crosstalk and pickup level, the smallest response the tool trusts.
- **Splice**: when CH2 changes range during a sweep, the ratio measured at the switch point stitches the ranges.
- **Wiring A, wiring B**: the filter's ground on the probe clips (A, recommended) or on USB ground (B).
