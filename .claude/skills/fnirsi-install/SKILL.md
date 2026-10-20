---
name: fnirsi-install
description: Guide a person through installing, hacking, upgrading or flashing their FNIRSI 1013D oscilloscope with this repository's open firmware, SD card and optional FPGA bitstream. Use when asked to install / hack / upgrade / flash my FNIRSI 1013D, put the firmware on the card, write the SD card, flash the FPGA or the bitstream, back up the scope, identify the FPGA variant, or recover a scope after a failed install.
---

# Installing the open firmware on a FNIRSI 1013D

Read `docs/guide/agent-runbook.md` in full and follow it in order. It has the questions to ask first (operating
system, FPGA variant, programmer, spare card), the safety gates, every command with what a good result looks like, the
first boot, and the recovery paths.

Rules that the runbook states and you never bend:

1. Identify the variant first. Only the Anlogic AL3A10 board is verified; no bitstream exists for the others.
2. Back up before writing: the card before the card tool runs, the FPGA flash twice (the two reads must agree) before
   any write to it.
3. Verify `SHA256SUMS` before building or writing anything.
4. Never `dd` the whole SD package to the card.
5. When a check fails, stop and tell the person what you saw. Do not skip a gate, do not improvise a workaround.

The person does the physical steps (cards, soldering, the programmer). You prepare each command, read its output
against the runbook's "good" result, and only then go on.
