# Generator

The scope has no DAC. Its only output is a logic-level square wave on the calibration post, the metal pin beside the
inputs. A 32-bit DDS in the FPGA sets the frequency and the duty cycle. The post drives the signal through a 1 kOhm
resistor, about 3.1 Vpp at the post with nothing connected.

The open bitstreams (0x1532 and later) have the generator. On the stock bitstream (0x1432) the post carries a fixed
1 kHz square wave and the generator button does nothing.

!!! warning "Check the wiring first"
    The 1 kOhm resistor is the post's only protection. Connect the post only to passive circuits or buffered inputs.
    Never connect it to a node that something else drives, such as a coil in a running circuit or a MOSFET drain.

## On the scope

The generator button in the top bar shows the duty cycle and the frequency. Tap it to open the generator menu. It has
three rows:

| Row | Controls |
|---|---|
| Power | A switch: the post outputs the square wave (green) or not |
| Freq | The frequency box, and a **1 kHz** preset |
| Duty | The duty box, and a **50%** preset |

![Generator menu](screenshot:generator)

Tap the frequency box or the duty box to open a keypad ("Set frequency" or "Set duty"):

- Type the value with the digit keys. For the frequency, **k** adds three zeros and **M** adds six, so "10k" is
  10000 Hz. The other prefix keys do nothing here.
- **MIN**, **Default** and **MAX** set 1 Hz, 1 kHz and 5 MHz (frequency) or 0 %, 50 % and 100 % (duty).
- **Back** deletes the last digit, **Clear** empties the field.
- **OK** applies the value.

A typed frequency above 10 MHz is set to 10 MHz. A duty above 100 % is set to 100 %. The frequency is a whole number of
hertz.

The wave shape, amplitude, offset and phase rows of the original firmware are gone, because the hardware cannot do
them.

## Example: a 10 kHz test signal

1. Tap the generator button.
2. Tap **Power** so the switch turns green.
3. Tap the frequency box, type 1, 0, **k**, and tap **OK**.
4. Tap outside the menu to close it.
5. Put the CH1 probe tip on the calibration post. Set the probe to 1X.
6. Tap **AUTO SET** to fit the trace to the screen. CH1 shows a square wave of about 3 Vpp, and the Freq card (on by
   default) reads 10 kHz.

## Over USB

The MCP tool `scope_generator` sets the power, frequency and duty. It drives the calibration pin, so it is a tier 3
tool: it needs a confirmation token ([MCP tools](../mcp-tools.md)). Check the wiring before you enable it.
The server also caps the frequency at 1 MHz and the duty at 50 % unless you raise its limits
(`FNIRSI_MCP_GEN_MAX_HZ`, `FNIRSI_MCP_GEN_MAX_DUTY`).

## Where else the post is used

- **Calibration**: the gain steps of the [wizard](calibration.md) measure the post's signal with each probe.
- **Frequency response**: the sweep drives the device under test from the post
  ([The screens](screens.md#frequency-response)). While a sweep runs, the generator button shows the tone being
  measured, and a tap on it says "Driven by the frequency response sweep". When no sweep runs, the post carries your
  own setting again.
- **External trigger**: on bitstream 0x1533 and later the post can also be a trigger input. Choose EXT in the trigger
  menu. That switches the generator off, because the input and the output share the pin.
