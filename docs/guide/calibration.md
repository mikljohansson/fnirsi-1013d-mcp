# Calibration

Each channel has two ADC cores that take turns, which gives 200 MSa/s per channel. The firmware measures a zero and a
gain for each core, and keeps separate values for the full rate (200 MSa/s) and the slower rates. While the scope runs
it also tracks the small drift between the two cores in the background.

Calibrate on first use. Calibrate again after you flash a different FPGA bitstream, and whenever Diagnostics asks for
it.

## Before you start

- Let the scope run for 5 minutes. A calibration taken earlier is marked "recalibrate after 5 minutes of warm-up".
- Have both probes at hand. The gain steps need each probe on the calibration post, set to 1X.

## Start the wizard

Use any of these:

- **Calibrate now** on the First start screen.
- Menu > Diagnostics > **Calibrate** (on the Status page or the Calibration page).

## The four screens

The wizard is four dialogs over the dimmed scope screen. "Step M of N" at the top right says where you are. Nothing is
written before the last screen. **Cancel** on any screen writes nothing, also while a measurement runs; the
measurement stops within about a second.

| Screen | What you do |
|---|---|
| 1. Zero level | Disconnect everything from the device: both probes, anything on the calibration post, and the USB cable. Tap **Calibrate**. |
| 2. CH1 gain | Plug the CH1 probe back in and connect it to the calibration post. Set it to 1X on the handle. Tap **Calibrate**. |
| 3. CH2 gain | Plug the CH2 probe in and connect it to the calibration post. Set it to 1X on the handle. Tap **Calibrate**. |
| 4. Done | Read the result and tap **Save calibration**. |

![Calibration wizard, zero level](screenshot:calibration-zero)

**Screen 1** measures both channels on all six ranges. The progress line reads "CH1: measuring range 3 of 6", then
"CH1: zero level done". The step takes about 8 seconds. Each channel first trains its capture
stages ("CH1: training capture stages..."), which adds about half a second. While a USB cable is connected, the screen
says in amber "USB is connected: unplug it before Calibrate". **Continue** appears when both channels passed.

**Screens 2 and 3** first check the signal on the post ("CH1: checking the signal on the post"), then measure the gain
("CH1: measuring the gain, about 1 s"). **Continue** appears after "CH1 gain done".

**Screen 4** shows one line per channel, for example "CH1 calibrated: cores matched to 0.1 code, gain mismatch 1.3 %".
"Cores matched" is how close the two cores' zero levels came. "Gain mismatch" is the gain difference between the cores
that the correction now removes. Save calibration writes the result to the card and closes the wizard.

## When a step fails

A failed step shows the reason after the channel name, for example "CH1: the input is not at 0 V: unplug the
probe". **Calibrate** stays, so you can try again. There is no Skip.

| Message | What to do |
|---|---|
| the input is not at 0 V: unplug the probe | Remove the probe from the input, then retry |
| too much noise, range 3 at 200 MSa/s: ... (or "too much noise: unplug the probes, move chargers and USB away") | Remove everything from the inputs, unplug USB and move chargers away. The second line names the limit: 1.0 code RMS at 2 MSa/s, 3.0 codes at 200 MSa/s |
| zero still moving, range 3 at 2 MSa/s: ... | Let the scope warm up for a few minutes, then retry |
| No signal on CH1: tip on the post? | Put the probe tip on the calibration post |
| the post signal clips: set the probe to 1X | Switch the probe to 1X |
| the post signal moved: hold the tip still | Retry without moving the probe |
| ADC readout fault near code 128: power-cycle and retry; report it if it persists | The ADC data near mid-scale came back garbled at 200 MSa/s: no capture timing read cleanly for a core, or samples hit 0 / 255 only at full rate with the input at 0 V. Switch the scope off and on, let it warm up and retry. If it fails again, open an issue with the step's second line (the measurement point and raw values) and a Diagnostics screenshot |

A failed zero step also shows a small second line with the measurement point and the raw values. It is meant for a bug
report.

## The volts calibration

The volts calibration sets the scale of each of the six input ranges with an external DC supply. It is the
calibration of the original 1.00o5 firmware. You need it only when the card holds no volts factors. The First start screen then says "Volts calibration missing too: it needs a DC supply up to
15 V.", and the wizard goes on to it after Save calibration. You can also start it by itself: Diagnostics >
Calibration > **Volts calibration**.

1. Connect CH1 and CH2, with the probes at 1X, to one DC supply.
2. Set the supply to the voltage the dialog names and tap **Calibrate**. The steps are 300 mV, 600 mV, 1.5 V, 3 V, 7.5 V
   and 15 V.
3. "No reading near 1.5 V: check the supply and the probes" means the scope did not see the voltage it expects. Check
   the supply and retry.
4. After "All six ranges calibrated." tap **Save calibration**.

Cancel puts the previous factors back.

## Checking the result

Diagnostics > Status shows one calibration verdict with its reason:

| Verdict | Reason shown | What to do |
|---|---|---|
| OK | Calibrated, every value within its limit. | Nothing |
| Check | FPGA changed since calibration (0x1535 to 0x153A): recalibrate. | Run Calibrate. The core order can differ between bitstreams |
| Check | Calibrated 2 min after power-on: recalibrate after 5 minutes of warm-up. | Run Calibrate after warm-up |
| Check | ADC capture flagged 3 times within a minute: run Calibrate. | Run Calibrate; it trains the capture stages again |
| Check | Gains not measured: run Calibrate with the probes on the post. | Run the wizard with the gain steps |
| Check | Gains carried over from an earlier calibration: run Calibrate. | Run the wizard with the gain steps |
| Check | A value is over its limit (below): move chargers and the USB cable away and calibrate again. If it stays, the input stage is noisy. | Run Calibrate with nothing on the inputs, chargers and the USB cable away. If the noise stays over its limit, the input stage is noisy |
| Check | Saved calibration does not match the current offsets: run Calibrate. | Run Calibrate |
| Fault | Uncalibrated: readings may be several % off. Run Calibrate. | Run Calibrate |

Below the verdict, the Status page shows per channel the gain difference between the two ADC cores ("Core gain split,
A / B", limit 8.0 %) and the worst noise RMS with the inputs open (limit 3.0 code), each with OK or Check. Both
limits are the wizard's own pass criteria, so a calibration that passed shows OK. The provenance line says when the
calibration was taken, for example "Calibrated on fw 0.2.59, FPGA 0x1535, 12 min after power-on, 1 power-on ago."

Diagnostics > Calibration shows the values per range: the offset centre, the volts factor, the zeros and gains of both
cores, the noise and the background tracking. A value outside its limit is amber.

**Clear calibration** on that page asks first, then clears the zeros and gains. The volts factors stay. Readings use
defaults until you calibrate again.

Details and limits: [firmware README](../../firmware/README.md) sections 6.4 and 6.5, and the
[glossary](../glossary.md) for the terms.
