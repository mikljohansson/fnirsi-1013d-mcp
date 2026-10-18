# fnirsi-1013d-ai

Open firmware, FPGA design and MCP tooling for the FNIRSI 1013D tablet oscilloscope, so that AI agents can
drive the scope as a measurement instrument.

## Why

The 1013D is a cheap 2-channel tablet scope built on an Allwinner F1C100s and a small FPGA. Thanks to
pecostm32's reverse engineering and Atlan4's long-running open firmware, the whole stack is hackable: the
MCU firmware, the FPGA bitstream, the boot chain. This project adds the missing piece, a PC control
protocol and an MCP server, and continues the performance work. The motivating use case is iterating on
transmit and receive waveforms for a VLF / pulse-induction metal detector, where a Claude agent runs the
measurement loop.

## Scope

In scope:

- USB CDC command protocol in the scope firmware: acquisition setup, trigger, raw and processed waveform
  readout, generator control, calibration and status.
- Python MCP server exposing those as tools, plus derived tools (averaged capture, FFT, decay fits).
- Firmware tree based on Atlan4 v1.00o5 with a plain Makefile build in WSL, cherry-picked fixes from the
  Donwulff tree, and FEL-based development loading.
- FPGA work on the Anlogic AL3A10 variant: Atlan4's 16k-sample bitstream as the base, then external
  trigger, deeper memory, timing closure and constraints.
- Documentation of the hardware preparation: FPGA-flash header, backups, programming, recovery.

Out of scope:

- Anything beyond the hardware's real limits (200 MS/s per channel, 8-bit, ~30 MHz bandwidth).
- Arbitrary-waveform output (the board has no DAC; only a square-wave generator on the calibration pin).
- Other FPGA variants (EF2L45, EP4CE6) beyond keeping Atlan4's version-word switching intact.
- Porting DPOS350P code (different SoC); its features are only inspiration for rewrites.

## Status

Research phase complete (2026-10-18): see `research/notes/SYNTHESIS.md`. Firmware bootstrap, hardware
preparation guide and protocol design in progress. Hardware state is tracked in `docs/hardware-status.md`.

## Layout

See `CLAUDE.md` for the repository layout, build instructions, reference checkouts and working rules.

## Credits

- pecostm32: reverse engineering, original open firmware, schematics, emulator, FPGA bus traces.
- Atlan4: years of firmware development, calibration, and the custom FPGA bitstreams for all three variants.
- Donwulff: audited fixes, test harness, headless Anlogic TD flow.
- The EEVblog "FNIRSI-1013D 100MHz tablet oscilloscope" thread community.

Licensing follows the upstream projects (GPL-3.0 for pecostm32/Atlan4 derived code); see `LICENSE` once
added.
