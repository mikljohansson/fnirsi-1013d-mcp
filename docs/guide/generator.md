# Generator

The scope has no DAC. The only output is a logic-level square wave on the calibration pin (the "calibration post"):
a 32-bit DDS in the FPGA sets frequency and duty. The stock bitstream (0x1432) has a fixed 1 kHz square wave and no
generator menu; the open bitstreams (0x1532 and later) have the generator.

## On the scope

The generator button in the top bar opens a compact menu with three rows: Power, Frequency and Duty. The wave shape,
amplitude, offset and phase rows of the original firmware were removed because the hardware cannot do them.

![Generator menu](screenshot:generator)

## Over USB

`scope_generator` (MCP) sets enable, frequency and duty. Driving the calibration pin is a tier 3 tool that needs a
confirmation token ([MCP tools](../mcp-tools.md)), because the pin can switch real current if it is wired into a
circuit. Check the wiring before you enable it.

## Where the generator is used

- **Calibration**: the gain steps put a probe on the post and use the generator as the reference level.
- **Frequency response**: the sweep drives the DUT from the post (see
  [The screens](screens.md#frequency-response)). Only passive or buffered devices: the post is a 3.1 Vpp square
  through 1 kOhm and that resistor is its only protection.
- **External trigger**: bitstream 0x1533 and later can trigger on the calibration pin as an input; selecting it
  switches the generator off.

!!! note "To be written after fw 0.2.70"
    The frequency and duty ranges as the final menu shows them, and a worked example.
