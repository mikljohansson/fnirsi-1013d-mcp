# Engineering articles

Longer write-ups of the hard parts of this project, for people who want to know why the scope behaves as it does, or
who want to build on it. Each article tells one story in the order it happened, with the numbers from the bench log
and the bitstream and firmware versions involved. Every measurement is from the owner's unit, the only one tested.

- [The calibration story](calibration.md): two ADC cores per channel, each with its own zero and gain. From Atlan4's
  1.00o5 compensations through per-core zeros in two clock classes, per-core gains from the calibration post and the
  four-screen wizard, to the background tracker for the full-rate drift.
- [The ADC encode-phase and capture-window faults](adc-capture-fault.md): why the first calibration wizard failed on
  clean data, and the four bitstreams it took: 0x1537 and 0x1538 (the encode phase and its check), 0x1539 (the MCU
  readout) and 0x153A (programmable capture stages, trained on the unit).
- [Decimation filters and the FIR](decimation.md): filtering before decimating in the FPGA (boxcar, CIC2, CIC3), the
  compensation FIR of 0x1535, the alias bench (-17 dBV with a boxcar against -76 dBV with CIC2 + FIR) and what 16-bit
  samples changed in the firmware and the protocol.
- [The MMU and the FFT](mmu-fft.md): a fixed-point FFT on a CPU without an FPU, and how turning on the MMU and the data
  cache took the 4096-point FFT from 10.96 ms to 2.72 ms and halved the scope view's pass time.
- [Frequency response: design and bench](frequency-response.md): magnitude and phase from a square-wave generator and
  two 8-bit channels; the frequency plan, the per-point steps and flags, the host tool and the on-device engine, both
  benched on an RC low-pass.
- [The UX makeover](ux-makeover.md): the 1.00o5 interface rebuilt as one system through mockups, adversarial reviews
  and the rule of consistency over muscle memory, and the UI guidelines that came out of it.
