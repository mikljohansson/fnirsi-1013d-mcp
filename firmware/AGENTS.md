# firmware/ handoff for agents

**Current state (bench session merge, 2026-10-19 14:35): latest build fw 0.2.58, protocol 1.17** = main with fw-ui2 (UX makeover
step 2: CTRL page with fold-outs, strips and spectrum header out, spectrum cards, warnings in every variant, top bar chips;
section 33) and fw-seq (FPGA 0x1536 segmented capture and hardware averaging, SET_SEQ / GET_SEQ / READ_SEQ, protocol row 56;
section 35, with the 0x1536 flash session plan). Still running: fw-ui4 (step 4, section 34), fpga-gen6.
**Branch fw-capmetric (2026-10-22): fw 0.2.105, protocol 1.28 (row 68): the capture-stage training parks at the carries 63.5..191.5 and keeps only the quiet stages (0 outliers and the 3-sample-median residual within 25 % + 4 of the core's best); GET_CAL 432 bytes with the last training; section 92.**
**Branch fw-guardfix (2026-10-22): fw 0.2.105, protocol 1.27 unchanged: the capture-stage guard, the repeat watch and REPEAT act only on captures taken at the 0x63 value the firmware wrote (0x36's first word; a DEBUG_FPGA sweep no longer gets a core moved and stored); section 90.**
**Branch fw-relay (2026-10-22): fw 0.2.105, protocol 1.27 unchanged: host acquisition requests and SET_ACQ_STATE RUN close the Frequency response view first (no Bode sweep from RUN; the soak's relay clicks), the relay registers written only on a change; section 89.**
**Branch fw-fullrate (2026-10-21): fw 0.2.105, protocol 1.27 (row 67): one calibration clock class for FPGA 0x153D, whose ADC encodes run at 200 MSa/s at every rate (the gated class, its wizard steps, the class probe gone; old cards load), the host-test model of the full-rate encodes, the encode check over every capture; section 83 has the owner's bench list.**
**Branch fw-105 (2026-10-21): fw 0.2.105, protocol 1.27 (row 67): the merged fw-d4 / fw-d5 / fw-nopat / fw-roll / fw-small / fw-cruft numbered and built for FPGA 0x153D (`PC_FW_FPGA_VERSION`), the dead checks for a bitstream without the 0x153D features removed (LOG_START E_FPGA detail 1, `fpga_log_start` -2, roll's "needs 0x153D" stop, the wizard's CW_GAIN_NA, the Frequency response's caps test); section 80 has the owner's bench list.**
**Branch fw-d4 (2026-10-21): fw 0.2.105 (was 0.2.104 + D4), protocol 1.27 (row 67): the FPGA 0x153D streaming logger: LOG_START / LOG_READ / LOG_STOP 0x0214-0x0216, the log loop (drain + MAIN poll, the notice slot's "Logging to the host" line), the 8 MiB NOLOAD `.logring` DDR ring, gaps on a lap or a full ring, acq_state 5, E_STATE 9 while logging, caps bit 23 / features bit 11; built for 0x153D since fw 0.2.105; section 74.**
**Branch fw-d5 (2026-10-21): fw 0.2.105 (was 0.2.104 + D5), protocol 1.27 (row 67): FPGA 0x153D, the generator as the pattern generator's two-step square (P16 / H16, glitch-free changes, the DDS writes gone), SET_PATTERN 0x0217, SET_PATTERN_TRIGGER 0x021A, capture trigger source 3 (the marker), GEN 40 bytes, Bode on P = 4 M (the patterns cut by fw-nopat); fw 0.2.105 for 0x153D; section 75.**
**Branch fw-cruft (2026-10-21): main 703e9c9, what 0x153C dropped (fw 0.2.105, protocol 1.27 row 67): no 0x0F writes, READ_LIVE / caps bit 9 retired, 0x53 = K alone (SET_DECIM flags bits 0..2 dropped); the roll mode's 0x24 / 0x26 reads it found are fw-roll's (section 77).**
**Branch fw-roll (2026-10-21): main 2611de2, roll mode on the 0x153D logger stream (fw 0.2.105, protocol unchanged): the long time bases draw corrected column means of both channels from fpga_log (roll.c, roll_view.c, pc_glue_roll_*), the 1.00o5 roll trigger kept on the columns, every arm releases the stream, host ARM / LOG_START take precedence over a rolling UI, averagecommand / fpga_average_trace_data(_long) gone, check_retired_regs covers 0x24..0x27; section 78.**
**Branch fw-nopat (2026-10-21): main 4fd9f2f, the pattern feature cut (owner 15:55; fw 0.2.105, protocol 1.27 row 67): SET_PATTERN / SET_PATTERN_TRIGGER, trigger source 3, the engine and 0x6E / 0x6F / 0x76-0x7D / read 0x42 gone, caps 24 / 27 and features 12 / 13 reserved, the square on fpga-d3's RTL (commit taken at once), fpga_pat_* renamed fpga_gen_*; section 79.**
**Branch fw-small (2026-10-21): main 803d215 + three small fixes (fw 0.2.105), protocol unchanged: the forced stop writes 0x0E = 0 before 0x1A = 0 (`fpga_force_stop`), Vmax / Vmin / Vpp / Vp from the peak bytes with Sample = Peak (`pb_extremes`), the "ADC readout fault near code 128" message without the bitstream advice; section 76.**
**Branch fw-c6 (2026-10-21): fw 0.2.103 + C5 + C6 (fw 0.2.104), protocol 1.26 (row 66 "Peak detect"): peak detect on FPGA 0x153C (0x56 bit 1), SET_DECIM filter 4, READ_RAW layout 3, GET_CAPTURE_INFO 79 bits 1 / 0, the shift byte's peak bits (0xC0) with mode 0x81, the mean-core full-class correction and the midpoint trace, averaging refused (E_STATE 7), caps bit 21 / features bit 9; section 70.**
**Branch fw-c7 (2026-10-21): fw 0.2.103 + C7 (fw 0.2.104), protocol 1.26 (row 66 addition): the 0x153C hardware counter: boot 0x62 = 128 / 6 and 0x61 = gate 100 ms on the trigger source, one 0x35 read per main-loop pass (`fpga_counter.c`, 17 strobes), the rate-change rule (0x61 = 0 before the read, the result dropped, 0x61 = value after), the level following the UI trigger level, GET_COUNTER 0x0213 (24 bytes, the mcp's layout), caps bit 22 COUNTER / features bit 10, the six timing cards fed by the counter for the trigger channel (`ui_meas.c`); section 71.**
**Branch ui-sample (2026-10-21): fw 0.2.103 + C6 + ui-sample (fw 0.2.104), protocol 1.26 unchanged: the ACQ menu's Sample row (478 px menu, Normal / Peak, the greyed Filter / Average with the Memory column's note, word 211 bit 3), peak records drawn as bars per column (`peak_bars.c`), phosphor bars, Trace style "Bars"; section 73.**
**Branch ui-type (2026-10-21): fw 0.2.103 + C5 + ui-type (fw 0.2.104), protocol 1.26 unchanged: the trigger menu's Type row (Edge / Pulse / Nth with Width, Shorter / Longer, N, Idle), the chip glyph, the notice queue, the hints, settings words 202 / 203; section 72.**
**Branch fw-c5 (2026-10-21): fw 0.2.103 + C5 (fw 0.2.104), protocol 1.26 (row 66): built for FPGA 0x153C (`PC_FW_FPGA_VERSION`; a scope on 0x153B shows the mismatch notice until C4's bitstream is flashed), caps bits 2 HOLDOFF / 20 TRIG_TYPES, SET_TRIGGER2 0x0219, GET_TRIGGER 52 bytes, GET_STATUS 52, GET_CAPTURE_INFO 112, the trigger extension registers 0x5E / 0x5F / 0x60 / 0x1B / 0x66 in fpga_bus.c with the arm-window writes; section 69.**
**Branch fw-one (2026-10-20): fw 0.2.103, protocol 1.25 (row 65): the firmware runs on one FPGA bitstream, `PC_FW_FPGA_VERSION` 0x153B (fixed caps, a mismatch notice screen, E_UNSUPPORTED detail 1, GET_INFO 80 / GET_HEALTH 128 bytes), the stock / Atlan / EF2 / EP4 arms, the encode-heal and core-order workarounds and the 0x1532..0x153A stop geometry removed; Average is one behaviour (the mean of N records on both paths); section 68.**
**Branch fw-diag (2026-10-20): fw 0.2.100, protocol 1.24 unchanged: Diagnostics Restarts tile counts crashes only (type 0 = deliberate), the noise rows use the zero step's limits (gated 1.0, full 3.0 codes, one pair of constants in adc_cal.h; the wizard's gated limit tightened from 3.0); section 66.**
**Branch fw-floor (2026-10-20): fw 0.2.99, protocol 1.24 unchanged: Frequency response ch2_small points grey (BODE_F_BAD, mcp BAD_FLAGS) and the two-line "CH2 too small above ... kHz" note with its mcp caveat; section 65.**
**Branch fw-arm2 (2026-10-20): fw 0.2.98, protocol 1.24 unchanged: the Frequency response engine reads its records as 0.2.96 did (0.2.97's 0x14 / cut rules counted half its captures as time-outs); section 64 addendum.**
**Branch fw-arm (2026-10-20): fw 0.2.97, protocol 1.24 unchanged (GET_CAPTURE_INFO flags bit 6): auto mode on FPGA 0x1532..0x153A arms the FPGA's time-out with the stop geometry (no spliced records), the record flagged when the firmware still forces; section 64.**
**Branch fw-yield3 (2026-10-20): fw 0.2.96, protocol 1.24 unchanged: a state-changing host request leaves Diagnostics (Back) before it runs, so the view no longer draws into it; section 63.**
**Branch fw-cap2 (2026-10-20): fw 0.2.81 provisional (version not bumped), protocol 1.24 + GET_HEALTH 106 bit 5: capture-stage training over codes 120..140, widest-run pick, repeat watch; section 54 addendum.**
**Branch fw-cap (2026-10-20): fw 0.2.79, protocol 1.24 (row 64): FPGA 0x153A capture stages (training in the wizard's zero step, 0x36 self-check, stored with the calibration), EXT offsets, the wizard's zero back at code 128, section 54.**
**Branch fw-yield (2026-10-19): fw 0.2.78, protocol 1.23 (row 63), both provisional: host commands close the modal screens (every modal loop leaves by its cancel path; E_BUSY only for a calibration measurement, a file save, USB drive mode or a 1.5 s time-out), section 53.**
**Branch fw-screen (2026-10-19): fw 0.2.70, protocol 1.21 unchanged: READ_SCREEN built (cap SCREENSHOT), section 46.**
**Branch fw-bus (2026-10-19): fw 0.2.72, protocol 1.22 (row 62): FPGA bus contract (dummy strobe in every readout, geometry before the reset, legacy EXT shift, encode check after async writes, type 22 trace level, core order from 0x33), section 48.**
**Branch fw-bode (2026-10-19): fw 0.2.66, protocol 1.20 (row 60): the Bode plot on the device (bode_dsp / bode_sweep / bode_view / pc_bode, BODE_CTRL / READ_BODE), section 42.**
**Branch fw-polish1 (2026-10-19): fw 0.2.63 provisional, protocol unchanged: UI polish batch 1 (maintainer's walk-through of 0.2.59, items a-q), section 39.**
**Branch fw-ui4 (2026-10-19): fw 0.2.59 provisional, protocol unchanged: UX makeover step 4 (Diagnostics as a main-loop
display mode with Status, Calibration, Log, Developer; the four-screen calibration wizard; the first-run screen), section 34.**
**Branch fw-ui3 (2026-10-19): fw 0.2.55 provisional, protocol 1.16 unchanged: UX makeover step 3 (Settings fold-out
trimmed, full Settings screen, confirmation dialog), section 31.**
**Current state (bench session merge, 2026-10-19 13:50): latest build fw 0.2.52, protocol 1.16** = main with fw-ui1 (UX makeover
step 1: main menu, USB drive screen, top bar, Display row, strips, measurement cards; section 29) and fw-readers (diagnostics
readers: GET_HEALTH, card image identity, calibration provenance, unit life record, SAVE_REPORT; section 30) merged. Running in
parallel: fw-ui2 (step 2), fw-ui3 (step 3), fw-touch1, fw-seg (0x1536, protocol 1.17). 
**Previous state (bench session merge, 2026-10-19): latest build fw 0.2.50, protocol 1.15** (fw-cal3's calibration wizard, interleave
tracking and class probe, **section 28**, merged on fw-usb2's 0.2.49 USB suspend watch, **section 27**; the merge renumbered fw-cal3's
protocol change from 1.14 / row 53 to 1.15 / row 54 because fw-usb2 took 1.14 / row 53) on fw-mmu's 0.2.48 (MMU + D-cache, section 26) on fw-gen7's 0.2.47 (dead-code purge,
reproducible images, 16-bit trigger level fix; protocol 1.13 unchanged) on 0.2.46 (protocol 1.13: digital phosphor view,
section 24) on fw-cal2's 0.2.45 (protocol 1.12: calibration data v3, a gain per ADC core and clock class from a gain step at the
calibration post, **section 23**) on fw-gen6's 0.2.44 (protocol 1.11: FPGA 0x1535 FIR support, spectrum strip repaint fix,
Filter default CIC2, USB suspend as a state, section 22); see the ladder (section 8); section 20 (spectrum analyser, auto
trigger mode) and 21 (calibration v2; its zero model and zero step still hold) still apply. The USB drop story: 0.2.25 PHYCTL fix closed the cold-boot drops; the device lost after a long host suspend (2026-10-19) is handled by 0.2.49's suspend watch, bench pending (section 27). Sections 9-17 below are
**Before any UI change** read `docs/ui-guidelines.md` (the rules for modes, bars, CTRL, dialogs, chips, fonts and touch)
and `docs/plans/ux-makeover.md` (the agreed changes and their mockups). older generations' notes, kept for history. Read the root `AGENTS.md` first, then this file. Details live in `firmware/README.md` (build, SD layout, FEL, memory budget, boot, bring-up
checklist), `docs/protocol.md` (the spec; section 14 lists every deviation and addition, rows 28-42 are this
generation's) and `docs/hardware-status.md` (bench log, newest at the bottom). This file does not repeat them.

**First job for fw-gen3:** the open crash in section 9. Everything else in section 17 waits for it.

## 1. Tree

```
firmware/scope/            Atlan4 v1.00o5 scope app + the USB control protocol (PC_PROTOCOL)
  pc_protocol.c/.h         frame layer: 16-byte header, CRC-16/CCITT-FALSE + CRC-32, parser with 100 ms
                           timeout and resync replay queue, ASCII probe lines. Pure C, no globals, host-buildable
  pc_cmds.c/.h             session: dispatch table, all handlers, shared control with the UI (settings_seq,
                           capture records, READ_RAW hold, MCP indicator), host single capture state machine,
                           duplicate cache, deferred WAIT, link counters. Hardware only via pc_hal.h. Host-buildable
  pc_hal.h                 the HAL interface (front end, timebase, trigger, generator, FPGA acquisition, trace,
                           shared UI state + fingerprint + indicator, transport, FEL)
  pc_hal_scope.c           HAL on the scope: writes go through the UI's own apply functions and redraw what changed
                           (drawing state saved/restored), the static pc_session, the glue called from 1.00o5
                           (pc_glue_*), ENTER_FEL jump
  pc_transport.c/.h        USB CDC transport: 2 KiB RX ring + NAK back-pressure, 66,580 B TX staging + pump,
                           ZLP, link events, counters, FIFO layout constants (PC_FIFO_MAP)
  pc_rx_ring.h             RX ring copies (two-segment in/out) and the one-FIFO-read-per-packet store. Header only,
                           host-buildable; host_tests/test_rx_ring.c runs it at every ring phase
  pc_hooks.h               the hook macros used in 1.00o5 files (always built since 0.2.47)
  build_id.c               the build identity as data (commit hash, commit time), linked last (section 25)
  pc_decim.c/.h            decimation filter maths for FPGA 0x1534 (N, K fallback, S, L16, Q24 scale, settle). Pure C,
                           host-buildable (section 19)
  gen_pat.c/.h             the generator as the FPGA 0x153D pattern: P16 / H16 maths, the split into two steps. Pure C,
                           host-buildable (section 75)
  adc_cal.c/.h             calibration data v3: per-core zero and gain model, sector 708 block, correction maths,
                           zero and gain step checks. Pure C, host-buildable (sections 21, 23)
  cal_wiz.c/.h             calibration wizard steps: state machine, actions, summary texts. Pure C, host-buildable
                           (host_tests/test_cal_wizard.c; section 27)
  cal_wiz_ui.c             the wizard's step dialog, touch loop and the zero / gain measurements (sections 28, 34)
  ui_diag.c/.h             Diagnostics pages and the first-run screen as data from the protocol bodies. Pure C,
                           host-buildable (host_tests/test_ui_diag.c; section 34)
  ui_dlg.c/.h              drawing of those pages, the dimmed-background step dialog and the confirmation (section 34)
  ui_diag_view.c/.h        Diagnostics as a main-loop display mode, its actions, the first-run screen (section 34)
  phos_map.c/.h            phosphor hit-count map: rasteriser, decay, render, palettes. Pure C, host-buildable (section 24)
  phosphor.c/.h            phosphor view (the Display fold-out's switch since 0.2.56; hooks in scope_display_trace_data;
                           section 24, 33)
  ui_ctrl.c/.h             CTRL page geometry, fold-outs, value names, warning line, spectrum cards. Pure C (test_ui; 33)
  ctrl_page.c/.h           the CTRL page and its fold-outs: drawing and the modal touch loops (section 33)
  pc_mmu.c/.h              flat page table (builder + walk host-buildable), MMU enable; pc_mmu_off in start.s (section 26)
  host_tests/              test_protocol.c, test_rx_ring.c, test_session.c (+ mock_hal.c FPGA/transport model), vector_check.c,
                           check_vectors.py, gen_vectors.py, bench_link.py (pyserial, bench only)
firmware/tools/            mksunxi.py, make_sd_image.py, fetch_gcc10.sh, spiflash tools (not this agent's)
docs/protocol-vectors.json shared frame vectors; the MCP agent merges entries with "source": "host" into it
```

1.00o5 files touched (all under `#if PC_PROTOCOL` or hook macros that are empty without it; 0.2.6 adds `menu.c` and
`statemachine.c`: PC_UI_MASK after the generator, trigger, acquisition and system-settings item draws, PC_UI_HAS in
their touch conditions): `fnirsi_1013d_scope.c`
(init + main loop: MAIN poll, indicator, then the UI pass), `scope_functions.c` (12 spin-loop BUSY polls; in
`scope_acquire_trace_data` the arm gate PC_UI_ARM_ALLOWED, PC_UI_CAPTURE_ADDR after the 0x14 read, PC_UI_CAPTURE_DONE),
`touchpanel.c` (BUSY poll in `tp_i2c_read_status`), `fpga_control.c` (BUSY poll in the 0x05 wait,
PC_UI_CAPTURE_START at the end of `fpga_do_conversion`), `power_and_battery.c` (PC_POWER_OFF, empty since 0.2.0),
`timer.c` (1 ms ISR poll), `usb_interface.c`, `cdc_class.c/.h` (transport hooks, FIFO map, USB identity),
`variables.h` (trace buffers rounded up to 4096 words), and since 0.2.19 `start.s` (restart record, section 10; the Makefile
now passes $(PC_DEFS) to the assembler). Later UI work also touched `menu.c`, `statemachine.c`, `power_and_battery.c`
(battery filter) and `fpga_control.c` (AL3 family version rule). List with reasons: README section 5.

## 2. Build and test

All builds run in WSL Ubuntu (`wsl -d <distro>`); from PowerShell use
`wsl -d <distro> -- bash -lc "cd /mnt/d/path/to/fnirsi-1013d-mcp/firmware/scope && make -j8"`. Git Bash mangles quoting
in long WSL command lines and Windows Python heredocs halve backslashes: put multi-step commands in a script file.

| command | result |
|---|---|
| `make` | GCC 13.2.1, protocol on: `build/fnirsi_1013d.bin` |
| `make gcc10` | Atlan's GCC 10.3.1 (`~/opt/gcc10`, from `tools/fetch_gcc10.sh`): `build-gcc10-pc/`. The bench uses this one |
| `make PC_DEBUG=1 BUILD=build-dbg` | adds DEBUG_FPGA + FORCE (caps 0x120) for bench tests T3/T7 |
| `make PC_FIFO_MAP=1 ...` | alternative USB FIFO layout (version string `-m1`) |
| `make -C host_tests test` | host gcc: test_protocol, test_rx_ring (all 2048 ring phases), test_session + mock_hal (release + PC_DEBUG; 762 / 760 checks on 0.2.19), check_vectors.py (79 vectors). None of them models usb_interface.c, the IRQ dispatcher or the timer-tick USB path |
| `python host_tests/bench_link.py <port> [--census 2000] [--fel]` | on the bench PC, needs pyserial |

Since 0.2.47 there is no PC_PROTOCOL=0 build and no `make check-reference` (maintainer dropped the byte-identical rule on
2026-10-18; the proof up to then is in the CI logs). Images are reproducible: the identity comes from the commit, so a
change that only removes dead code can be checked by building both commits with a fixed `PC_FW_BUILD=11111111
SOURCE_DATE_EPOCH=<any>` and comparing sha256 (section 25). Warning policy: no new warning under GCC 13 or GCC 10
(compare sorted, line-number-free warning lists against the parent commit; new files are also clean with -Wextra).
0.2.47: GCC 10 36, GCC 13 31, PC_DEBUG 31, all upstream.

## 3. Dev loop (no SD writes)

1. Scope runs a protocol build: host sends ENTER_FEL (cmd 0x0F01, u32 0x4C45465F). Fallback: boot menu, "FEL".
2. `usbipd attach --wsl --busid <busid>` (`usbipd list` shows it; FEL id 1f3a:efe8) or `--hardware-id 1f3a:efe8`.
3. `sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000` (~2 s, run as root in WSL).
4. The scope enumerates as a USB serial port, `<port>` below (Windows: a COMn port, VID:PID 1209:0001, serial
   1013D-0001; the maintainer's unit is COM10), or attach it to WSL as /dev/ttyACM0 for Linux cdc_acm.

The SD card boots our 0.2.11 from sector 750 (persisted with SD_STAGE + SD_WRITE_SECTORS, see section 13). One serial user at a time; ask the maintainer to keep hands off
the screen during tests (menus are modal and defer commands). The power-off ISR still writes settings to sector 709
(1.00o5 behaviour); with PC_FORCE_CDC=1 that save stores "USB CH340 mode" = on.

## 4. USB facts learned the hard way

- FIFO RAM is 2048 bytes (F1C100s datasheet); the EP0 FIFO is fixed at bytes 0-63 (MUSBMHDRC guide, dynamic FIFO
  note). FIFOADD is in 8-byte units; FIFOSZ = 2^(n+3), DPB doubles it. 1.00o5's CDC map (OUT at 0 on EP0, EP1 at
  2048, IN at 4096 aliasing onto 0) produced: setup bytes inside IN data, IN data overwriting waiting requests,
  stalls. MSC's map (OUT 0, IN 1024) is unchanged upstream code.
- Map 0 (default since 0.1.6, size-aligned): EP0 0-63, EP1 64-127, EP2 OUT 512-1023 single, EP2 IN 1024-2047 DPB.
  Map 1: the 0.1.3-0.1.5 layout (OUT 64-575, IN 576-1599, EP1 1600-1663). Compile-time disjointness check. The
  guide requires only 8-byte granularity for FIFO addresses.
- One FIFO access width per packet ("all the transfers associated with one packet must be of the same width"):
  OUT packets are read in one `usb_read_from_fifo` call into an aligned bounce buffer, then copied into the ring
  (`pc_rx_ring.h`). Splitting the read at the ring end corrupted one request per 2 KiB received (0.1.2-0.1.5): the
  0.1.5 GET_BAD_FRAME diffs show the tail from the 2 KiB stream boundary on replaced by the last 8-bit value
  replicated (constant bytes) or the last 16-bit value replicated (2-byte alternation), i.e. a wider read after a
  narrower one inside a packet returns the previous value again. test_rx_ring.c models exactly that.
- Double-buffered IN: every TxPktRdy clear raises the TX interrupt, including the immediate clear after the first
  load. The pump loads at most 2 packets per call; it runs from the EP2 TX ISR and from every poll (missed-IRQ
  fallback, counter tx_kicked). ZLP after frames that are an exact multiple of 512.
- RX: single-buffered OUT; the ISR copies packets into the ring; when the ring lacks 512 B free it leaves RxPktRdy
  set and masks EP2 in INTRXE (controller NAKs). Polls sweep the FIFO and take a packet still waiting 2 ms after it
  was first seen (counter rx_swept). The sweep was added for interrupts thought lost by 1.00o5's write of 0 to INTC
  pending; the 0.2.20 bench found no such loss (section 11), so it is a safety net only.
- INDEX (EPIND) discipline: every non-USB-ISR path that touches indexed endpoint registers masks IRQs (CPSR I) and
  saves/restores EPIND (`pc_irq_save`). USB ISR paths set EPIND explicitly; IRQs do not nest.
- 1.00o5's bus-reset handler flushes CSRs with EPIND = 0 (not EP2); `pc_usb_isr_link` flushes EP2 itself.
- SOF interrupt is not enabled in protocol builds, but the SOF flag (INTUSB bit 3) still latches every frame. Any code
  that reads INTUSB outside the real ISR must ignore it (0.2.12-0.2.15 did not, see section 11).
- DTR: pyserial asserts DTR on open. DTR 1->0 flushes TX (1.2 also released REMOTE); DTR 0->1 flushes
  a leftover frame. Windows usbser sends GET_LINE_CODING when the app calls SetCommState (pyserial reconfigure),
  also mid-transfer. Windows-side reads of 20-33 KiB stalled in the host stack (fixed in mcp/); Linux cdc_acm
  reads every size up to 64 KiB.
- A response with no progress for 2 s is dropped; `#RESET` aborts one at any time.

## 5. Service contexts

| context | where | may touch | answers |
|---|---|---|---|
| ISR (`PC_CTX_ISR`) | timer0 IRQ, every 1 ms | transport, session reads | PING, GET_INFO, GET_STATUS, SET_REMOTE (no-op), GET_BATTERY, GET_LINK_STATS, GET_BAD_FRAME, GET_CAPTURE_INFO, `#VER`, `#RESET`, error replies; never E_BUSY |
| BUSY (`PC_CTX_BUSY`) | touch-panel scans, UI spin loops, 0x05 wait | transport, session | same set; others E_BUSY once no MAIN poll for 250 ms (UI modal) |
| MAIN (`PC_CTX_MAIN`) | top of the main loop, between UI passes | everything (FPGA, UI, display) | all commands, deferred WAIT completion, link events, settings tracking, host capture polling, ENTER_FEL |

Guard: `s->in_poll`. A poll returns at once if another is running (ISR skips are counted); `pc_ui_service` and the
UI capture hooks hold it too. Everything not in the safe set (`F_BUSY` flag in the dispatch table) stays queued as `pending_ev`
for MAIN. One response in flight at a time (TX staging buffer); one further event is parsed and held, so `#RESET`
still works, except behind a held READ_RAW (section 6). Every non-ISR command waits for the next UI pass (35-75 ms).

## 6. Shared control (protocol 1.3, fw 0.2.0)

Plan: `docs/plans/collaborative-mode.md`; wire format agreed with the MCP agent (mcp-gen2), spec in protocol section
14 rows 30-39. No LOCAL/REMOTE, no lock, no banner, no snapshot, no watchdog. The UI loop always runs.
- Host SET_* call the UI apply code in `pc_hal_scope.c` (`scope_set_timebase`, fpga_set_*, the trigger level as a
  marker position found by search, rounded UI copy of SET_GEN) and redraw the touched top-bar item. Drawing saves
  and restores `displaydata` (the UI's current buffer/colour/font).
- `settings_seq`: fingerprint (CRC of the capture-relevant fields, `pc_hal_settings_fingerprint`) compared at every
  poll, UI arm and UI DONE. Captures latch their settings at arm (`pc_capture`); a change before DONE makes
  capture_settings_seq 0xFFFFFFFF.
- ARM = host single: `pc_hal_ui_set_run(0)` (screen shows STOP), then the 1.2 ARM sequence; DONE builds the trace
  buffers for the enabled channels, which the UI redraws. Aborted when the UI arms (start hook or runstate seen
  set). SET_ACQ_STATE 0x0208 = RUN/STOP button. GET_CAPTURE_INFO 0x0209 = latest capture's settings.
- READ_RAW needs `ring_valid` (no arm since the capture's DONE). In RUN it returns PC_HOLD (request stays queued,
  re-run every MAIN poll), the UI arm gate is closed, at most PC_HOLD_MS = 1000 ms, then E_BUSY detail 1. While held,
  the parser takes no new event (so `#RESET` comes after the hold); link reset / DTR up drop the hold. The done hook
  counts a conversion once (`ui_started`), because 1.00o5 re-processes a finished conversion every pass while the
  gate holds (conversion_done stays set).
- MCP chip (`pc_ui_service`, `pc_hal_indicator`): rendered once into a pixel array (via the top-bar corner of
  displaybuffertmp), then drawn as one blit of final pixel values, so the 100 ms refresh and the PC_TOPBAR_DRAWN hooks
  cannot flicker (0.2.3-0.2.9 cleared the slot and redrew it every 100 ms on the screen; maintainer saw flicker while the
  main loop ran). The slot is cleared only on on/off transitions. 0.2.3 steady green, 38 x 17 at x 127 y 3, directly
  above the RUN/STOP chip, in the RTC clock's slot (the main-loop RTC draw is gated by PC_RTC_ALLOWED while the chip shows);
  on while DTR or a frame in the last 2 s, redrawn every 100 ms. Maintainer rejected the 0.2.1 spot (x 697 y 22 under the
  battery) and the pulse.
- SET_TIMEBASE mode 0 (0.2.3): sets index + rate + short mode, FPGA, then one scope_preset_values +
  scope_acqusition_settings. 0.2.1 called scope_set_timebase and the maintainer saw flicker through time/div values; the
  exact mechanism was not identified (host requests never use the UI's one-index step routine). Bench: confirm.
- Unverified on the bench (acceptance list in README section 5, step 6): screen redraws after host SET_*, marker
  search for the trigger level on every range, the UI showing a host capture, the chip position and colours.

## 7. FPGA layer and capture geometry

**Since fw-one (section 68) there is no variant table**: `pc_variant_init` (pc_cmds.c) sets the one geometry (ring 8192,
max 16382) and the one capability word of the bitstream the firmware is built for (`PC_FW_FPGA_VERSION`, pc_cmds.h); the
version word read at boot only sets `pc_variant.mismatch`. The paragraph below is the history up to fw 0.2.102.
Variant table from the 0x06 word in `pc_variant_from_version` (0x1432 stock: ring 4096, fixed 1500/3000;
0x1532 AL3: ring 8192, max 16382; 0x1533..0x153F AL3 family, same geometry, 0x1533+ adds EXT_TRIGGER (cmd 0x15 = 2,
status cmd 0x28; `fpga_get_version` maps the family to fw_FPGA 2 under PC_PROTOCOL); 0x1632 EF2 / 0x1732 EP4: ring 6144, max 12286; unknown: caps 0). The maintainer's
unit runs AL3 0x1532. Host ARM uses its own sequence (`pc_hal_arm`: 0x0F=0, 0x1A=mode, 0x01=1, bounded 0x05
wait, 0x0B/0x0C/0x0E in reset, 0x01=0); DONE latches 0x14 and writes 0x0F=1; capture start =
(trigger_addr - pretrigger_words) mod ring (the UI keeps 1.00o5's `T + 7441` math). READ_RAW reads per core with
one dummy strobe after each 0x1F write, splits at the ring end, `read_lag` = 0. READ_TRACE runs the 1.00o5
readout (`fpga_read_sample_data`, B before A, no dummy read).

0x1534 (fw 0.2.30) adds caps bit 15 DECIM: the decimation filter with 16-bit samples, one per ring word. Then
pretrigger / total / start are words = samples (`pc_cap_words`, `s->cap_wide`, `pc_glue_decim_words`), the UI start
is (0x14 - pretrigger) mod 8192, and every conversion start writes the filter mode. Section 19.

Unverified (caps UNVERIFIED bits stay set until they pass; `BENCH: Tn` comments mark the code):
T1 ZLP (passed on Windows and Linux), T2 throughput (1.0 MB/s LOCAL, 2.35 MB/s REMOTE on Windows, >1 MB/s Linux),
T3 read_lag / stale first byte (DEBUG_FPGA image), T4 A/B interleave order (generator loopback, 200 MS/s),
T5 trigger_addr alignment, T6 0x18 after auto timeout, T7 FORCE via 0x1A=0 (PC_DEBUG), T8 volts per code with a DC
reference on every range, T9 RGB565 framebuffer (READ_SCREEN built in 0.2.70, section 46), T10 stock trigger core (needs the stock
bitstream restored from backups/). Procedures: protocol section 12, README section 5 checklist.

## 8. Version ladder (bench-relevant images)

Every bench image is pinned as `backups/fw-<version>-<sha256 prefix>.bin` (gitignored). Older images (0.1.4-0.2.10)
are pinned the same way; their history is in `git log -- firmware` and docs/hardware-status.md.

| fw | commit | sha256 | change | bench result |
|---|---|---|---|---|
| 0.2.11 | 1594918 | af4b7f30 | a request sent right after the port opens is kept (DTR-up no longer resets a pending request) | accepted: census clean, 20/20 open-then-request; **on the SD card**; one USB drop during 10 Hz polling |
| 0.2.12 | e797f25 | c9270dd3 | timer tick runs the whole CDC IRQ handler (lost-IRQ fallback), counter 47, GET_EVENT_LOG, protocol 1.5 | counter 47 = 46k in 3 min (race with the real IRQ), 1/100 64 KiB lost, soak: 2 drops; the "restart" reported earlier was retracted by the lead (timeline fits no restart) |
| 0.2.13 | 7ebda04 | 691d0d28 | System settings compacted without a DS3231; battery level averaged with hysteresis while charging | not benched alone; maintainer verdicts pending |
| 0.2.14 | d6a6b37 | aa4a142c | fallback only after two quiet ticks; EXT ground note | counter 47 = 11.8k in 2 min (SOF defeats the gate), 1/100 64 KiB lost |
| 0.2.15 | c4989e5 | b0cec01f | SOF ignored for counter 47 / type 14, event log 128, frozen pre-reset snapshot | superseded unbenched, not pinned |
| 0.2.16 | 079f648 | 5f25b9e7 | CDC handler split into entry + body; the tick reads the flags and runs the body only for non-SOF flags | 300 x 64 KiB clean, counter 47 = 16 in 7 min (real lost RX IRQs), **self-restarts** under bench traffic |
| 0.2.17 | 0c13045 | 02aa445a | quiet boot: no "Failed to open file" box without scope.bmp, counter 48 | built, host-tested; maintainer verdict pending |
| 0.2.18 | 50c9802 | ed117dc5 | event-log snapshot re-armed only with request flag bit 0 | built, host-tested, not benched |
| 0.2.19 | 55d5c5b | 9b94c9a8 | restart record at 0x81A00000, exception vectors record and restart, counters 49-56, event type 15 | built, host-tested, disassembly checked; **ELF kept at backups/fw-0.2.19-9b94c9a8.elf** |

| 0.2.19 A/B pair | 02288b8 | c30ff516 / 3d1f3c88 | same code as 0.2.19; `make gcc10-nofb` (PC_USB_TICK_FALLBACK=0) builds `0.2.19-nofb` without the tick fallback (#VER shows the suffix) | for the restart A/B; both ELFs kept as backups/fw-0.2.19-c30ff516.elf and backups/fw-0.2.19-nofb-3d1f3c88.elf |
| 0.2.20 | 7f1c0c0 | e1517803 | tick fallback checks endpoint state (served only when seen on two consecutive ticks, no USB ISR between), skips while the USB IRQ is pending, counter 57 = races (n = 58), type 14 b condition bits | built, host-tested; ELF backups/fw-0.2.20-e1517803.elf |
| 0.2.20-nofb | 7f1c0c0 | c7210b74 | same without the tick fallback (`make gcc10-nofb`, target removed in 0.2.21) | built; ELF backups/fw-0.2.20-nofb-c7210b74.elf |
| 0.2.21 | 1a893a9 | 6be6f9e8 | USB tick fallback removed (single 1.00o5 CDC handler again, no INTC pending capture); counters 47 and 57 retired (always 0, n stays 58); type 14 no longer emitted | built, host-tested; ELF backups/fw-0.2.21-6be6f9e8.elf; on 0x1533: FEL load 4 passes clean, cold boot from the card lost EP2 IN in pass 3 |
| 0.2.22 | 7b6d855 | 6615bab6 | usb_device_init logs the controller state it finds (event type 16, protocol row 43), then asserts the USB OTG bus reset and PHY reset for 1 ms before 1.00o5 releases them | built, host-tested; ELF backups/fw-0.2.22-6615bab6.elf |
| 0.2.23 | 25e90fc | 545126ff | 0.2.22 plus EP2 IN state after every TX abort: type 17 (POWER, TXCSR), type 18 (EP2 TX int enabled / ZLP pending, frame number) | FEL: replug test clean (400 x 64 KiB); FEL init state 0x4000 / 0x070f / 0x2000 / 0x0000 / 0x2fc0; persisted to the card |
| 0.2.24 | 7e5a2e3 | 9de2caad | 0.2.23 plus GET_EVENT_LOG bus-level ring tail (16, types 1-4, 7, 8, 12, 13, 15-18), identical EP0 setups logged at most once per second | built, host-tested; ELF backups/fw-0.2.24-9de2caad.elf |
| 0.2.25 | 85a113d | c71935f9 | 0.2.24 plus PHY tuning writes to PHYCTL 0x01C13404 (Linux suniv) instead of 1.00o5's 0x01C13410; type 19 post-init state incl. PHY tuning read-back | built, host-tested, disassembly checked; ELF backups/fw-0.2.25-c71935f9.elf |
| 0.2.26 | bf0651b | 6a7049bf | channel-menu FFT switch hidden, Invert/Coupling/V/A one row up (`PC_CH_DY`) | built, host-tested; maintainer verdict pending |
| 0.2.27 | efc5170 | 29db9cd4 | fx_fft.c FFT core (6edb719) + FFT_BENCH 0x0F02 (protocol 1.6, row 44) | bench clean; FFT_BENCH 4096 points 24.3 ms (4x the estimate, section 20); channel menus accepted |
| 0.2.28 | e6a7c4c | b7109d34 | spectrum view (main menu row 6), READ_SPECTRUM 0x020A, GET_STATUS byte 43 (protocol 1.7, row 45); spec_fmt.c (bed2dd4) | maintainer: works; harmonics within 0.4 dB of theory |
| 0.2.29-0.2.30 | (fw-decim) | | decimation filter for FPGA 0x1534 (protocol 1.9, row 47, section 19) | merged, not benched alone |
| 0.2.31 | 0930c72 | b5685268 | FFT hot loops in ARM assembly (LDRD/STRD, no spills, twiddle table), qemu-arm host test; includes 0.2.30 | bench clean on 0x1533; FFT_BENCH 2.2-2.5x faster (4096: 10.9 ms; SRAM 2048: 2.3 ms); spectrum as before |
| 0.2.32 | 83aa5b5 | ed9fb0f8 | spectrum view: Waterfall and Density sub-views (View field), GET_STATUS byte 43 = 2 / 3; trace area cleared with memset | built, host-tested; maintainer verdict pending |
| 0.2.33 | d125a41 | e002ab21 | 0.2.32 plus the view's FFT from SRAM A for N <= 2048 | built, host-tested; bench with 0.2.32's items |
| 0.2.34 | 1a9a773 | 00847286 | spectrum view on the 0x1534 16-bit records (fx_fft_run_u16, filter rate 200 MHz / N, scale_q24 levels) | built, host-tested; on 0x1533 identical in behaviour to 0.2.33 |
| 0.2.35 | 36149d1 | b1be459d | maintainer's UX: tap left/right on the plot = zoom out/in, drag = pan, Auto and Reset replace Span/Start; FFT length capped at 4096, default 2048; density with per-cell age (no band) and intensity ramp | built, host-tested |
| 0.2.36 | 8e1a946 | 6060514b | view axis capped at 25 MHz (PC_SPECTRUM_FMAX_HZ) | built |
| 0.2.37 | 0f3e743 | 9fde228a | dB conversion in ARM assembly (3 accesses per bin, byte-identical to C) | built, host-tested incl. qemu |
| 0.2.38 | 6ce82d9 | a6f50f6c | pass time under the acquisition menu's Memory header | built, host-tested |
| 0.2.39 | 11b1729 | 267e80fe | span drives the time base (zoom out past full span = faster rate up to 50 MS/s, zoom in past 64 bins = slower), Auto picks the rate in up to 3 captures, spectrum settings persist in settings words 208/209, filter tag "CIC3" instead of the N text | built, host-tested |
| 0.2.40 | e595a0f | 3907e7ac | spectrum Average field (Off/4/16/64/Max hold, default 4), one header line + warnings over the plot, Roll mode label without on/off | built, host-tested |
| 0.2.41 | 0fd6a9c | 08aeaa63 | firmware auto trigger mode (auto captures arm normal; time-out max(100 ms, 2 records) forces an untriggered record, reported truthfully, UI and host); spectrum drops occasional untriggered records; zoom-in steps the rate down at span <= fs/16 | built, host-tested (test_fw_auto_timeout) |
| 0.2.42 | cebb982 | b23f5fc8 | deterministic Auto (probe 50 MSa/s, period search, derive from f0 / f_hi, confirm, refine once), warnings with consequence + action, "MSa/s" units, no Avg tag in the header | built, host-tested |
| 0.2.43 | 80ab45d | 7d2ea871 | calibration data v2: per-core zeros (full / gated clock), dithered correction, GET_CAL 248 bytes (protocol 1.10, row 48, section 21) | built, host-tested; bench list README 6.2 |
| 0.2.44 | e0ee6f3 | 08f9a5ed | FPGA 0x1535 FIR (protocol 1.11, row 49), spectrum view not repainted by scope_preset_values, Filter default CIC2 (PC_DECIM_DEFAULT), USB suspend as a state + resume logging (row 50), generator Freq row no longer hits the removed Wave row; includes main's fw-cal 64 full-rate conversions | built, host-tested (section 22) |
| 0.2.45 | df61557 | 32f259b4 | calibration data v3: gain per core and clock class from a gain step at the calibration post, per-class offset slopes, block v3 (304 bytes), GET_CAL 256 bytes (protocol 1.12, row 51, section 23) | built, host-tested; bench list README 6.3 |
| 0.2.47 | 463b489 | b5098dc4 | dead-code purge (PC_PROTOCOL=0, check-reference / package, FFT switch, generator Wave/Ampl/Offset/Phase, Long memory, hold-off), reproducible image (identity = commit; same sha256 from any directory), 16-bit trigger level through gain16 / corr16 (fw-gen7, section 25) | built, host-tested; bench list in section 25 |
| 0.2.46 | 1b6359f | cdba5b51 | digital phosphor view (main menu row 7; Persistence / Palette / Clear strip; XY), GET_STATUS byte 43 = 4 (protocol 1.13, row 52), settings word 210 (fw-phosphor, section 24) | built, host-tested (test_phosphor 95 checks); bench steps in section 24 |
| 0.2.48 | b1922c3 | 2f768fd9 | MMU + D-cache on (flat map, DDR / SRAM write-back, framebuffer write-through, restart record and peripherals strongly ordered), clean + off at _reset and ENTER_FEL, GET_LINK_STATS 58-59 fault status / address (fw-mmu, section 26) | built, host-tested (test_mmu); A/B image `make gcc10-nommu` 0.2.48-nommu sha256 45e03f7f; bench sequence in section 26 |
| 0.2.49 | c477faa | 90f460d6 | USB suspend watch in the 1 ms tick (pc_usbw.c): inferred resume (SOFs, EP0 setup, EP2 OUT while suspended) acts, soft reconnect (SoftConn off 1 s) after a retry storm or 15 min suspended, GET_LINK_STATS 60-61, event type 20, SUSPEND before RESUME in the ISR (protocol 1.14, row 53; fw-usb2, section 27) | built, host-tested (test_usbw 90 checks); A/B `make gcc10-nommu` sha256 f5f377ea; bench plan in section 27 |
| 0.2.56 | 5255516 | 7729e4fe | UX makeover step 2 (fw-ui2, section 33; provisional number): CTRL page in both modes (Display fold-out with icons and the Phosphor switch with Persistence / Palette / Clear / wfm/s, Trace style or Analyse with Scale and Ref level, Grid switch, compact V+ / CH / V- steppers, 50 % TRIG in Scope), ACQ menu acquisition only, phosphor and spectrum strips and spectrum header gone (spectrum cards Peak / Level / RBW / Time), empty cursor and measures slots in Spectrum, warning line in every scope variant, top bar chips one construction, settings word 211 | built, host-tested (test_ui 6672 checks); bench walk-through in section 33 |
| 0.2.58 | 3934ea3 | 6bbd7598 | FPGA 0x1536 sequencer (fw-seq, section 35; provisional number): caps bit 17 SEQ, 0x0B / 0x0C / 0x57-0x5C before the reset, UI Average as a hardware run of 2-64 records for legacy records that fit, SET_SEQ / GET_SEQ / READ_SEQ, READ_RAW addr_mode 3, GET_STATUS 48 (features, UI Average), GET_DECIM 36, GET_CAPTURE_INFO 96 (protocol 1.17, row 56) | built, host-tested (test_seq 732, test_session 69705); nommu fc8fc081; flash plan in section 35 |
| 0.2.55 | e01f775 | 6f100098 | UX makeover step 3 (fw-ui3, section 31; provisional number): fold-out Screen / Grid brightness, File message timeout, Time and date, Baseline calibration (interim), Other settings; full Settings screen (`ui_settings.c` / `ui_settings_ui.c`) with At power-on 0 / 4 / 5 written on Back, Restore default settings behind a confirmation (boot byte kept, CDC kept); Other settings menu, USB-Serial and Dev mode rows removed; delete confirmation as the dialog pattern | built, host-tested (test_ui 1,431,758 checks); bench walk-through in section 31 |
| 0.2.54 | f9fcac6 | a4f0c832 | touch fix batch 1 (fw-touch1, section 32; provisional number): left pointers are drag handles (`touch_pick.c`, 44 px boxes, nearest / on-top pick), pick at touch-down, nearest enabled zero line, no trigger-point move in a channel drag, channel + trigger level share one clamped delta, EXT no longer follows CH2, traceposition 6..396 in the new auto setup and at load |
| 0.2.51 | 8f5bf16 | 38580def | UX makeover step 1 (fw-ui1, section 29): main menu Scope / Spectrum analyser / Saved pictures / Saved waveforms / Settings / Diagnostics / USB drive with one 59 px hit area per row, Spectrum in roll mode switches Roll off; USB drive screen text + Exit; no top-bar version; Display row Normal / Phosphor / X-Y in the acquisition menu (X-Y out of System settings); strips without Scope / Auto, AUTO SET = spectrum Auto; phosphor warnings top-middle; cards with chip, scope-maker names, 4 significant digits, defaults Freq / Vpp / Vavg | built, host-tested (test_ui 1513 checks); bench walk-through in section 29 |
| 0.2.50 | e871c50 | 1b55efc2 | calibration wizard (one modal three-step flow, 48e5737) and calibration v4: A - B interleave tracker applied in the full class, full-class common-mode class probe, GET_CAL 288 bytes, diagnostics lines (protocol 1.14, row 53; fw-cal3, section 28) | built, host-tested; bench lists README 6.4 and 6.5 |
| 0.2.52 | c8f874b | e0968392 | diagnostics readers (fw-readers, section 30): GET_HEALTH 0x000A (USB state, CP15, power-on count, operating minutes, settings save state, stored restart record), unit life record in settings words 224..255 with one sector-709 write per boot, image identity record + SD_INFO IDENT (112 bytes), calibration provenance in sector 708 432..487 + GET_CAL 356 bytes, SAVE_REPORT 0x0504 (protocol 1.16, row 55) | built, host-tested (test_diag 462 checks); bench list in section 30 |
| 0.2.65 | 37714c3 | 1de13bd1 | Measurements v2 (fw-meas, section 41; provisional number): MEASURES list per mode (Scope twelve per channel, Spectrum Peak / Level / THD / Noise per channel + RBW / Span / Center / Time in words 221..223), chosen display order (defaults Freq, Vpp, Vavg), one card row with "+N more", MEASURES menu with title and 44 px rows, card source hook for an FPGA counter | built, host-tested (test_ui 1,420,237 checks); bench list in section 41 |
| 0.2.105 | e3f3baf | 3f4eb195 | built for FPGA 0x153D (fw-105, section 80): the streaming logger (fw-d4), the generator as a fractional square (fw-d5, patterns cut by fw-nopat), roll mode on the logger stream (fw-roll), the 0x0E force, the peak cards, the readout message (fw-small), the 0x153C cruft (fw-cruft), the checks for a bitstream without them removed; protocol 1.27 row 67 | built, host-tested; bench list section 80 |

All of 0.2.12-0.2.20 contain the tick fallback, except the -nofb builds; 0.2.11 and 0.2.21+ do not. Full 0.2.19 sha256:
9b94c9a8881db847b148eeed9d6011ddd84eaf94fb28d0a78b46817d43ad7fc4. The A/B pair differs from it only in the embedded
build hash and build time; use the pair together. Full sha256: 0.2.19 (02288b8)
c30ff5168bf227e0bf80038d037845e7a5d1e883fe00473f5f9f99a7f9b780fc, 0.2.19-nofb
3d1f3c889a206621bc6546c0a7df227365e76589d5b41c05341183e4cb161cd9. 0.2.20 (7f1c0c0)
e1517803e6bc9a8b595a3a2797924db4269e256b7ff34207136b6b4a12fbdc8e, 0.2.20-nofb
c7210b749cc55482155a2211fec422f674efa3a22faa3d378bf5a60d33bbc318. 0.2.21 (1a893a9)
6be6f9e81673c19bf88a812fe598b844426167cb14c3f73ca4d0b9c7bdbcdd04. 0.2.22 (7b6d855)
6615bab63dc37918b654d00ce6e133b7c25a4b0d43d0845892e3a786cb96d1cb. 0.2.23 (25e90fc)
545126ff304b6ce87802b51cc05e95d41f0084308758c4e116b62d536770955e. 0.2.24 (7e5a2e3)
9de2caad698bcd96e5c7195f9f5aa7b6bb8aecad1ba2b2c74655ad9963c92d5c. 0.2.25 (85a113d)
c71935f9df1652b7d597875fb5eadcb3b49fb370770c5cc950e1bce3f781167e.

## 9. Open crash: self-restart from RAM (one confirmed case, 0.2.16)

Symptom (lead, 0.2.16): under repeated full bench_link runs the device restarts itself. GET_STATUS uptime_ms read
175 s after 7+ minutes of running, `#VER` still 0.2.16, three bench runs died right after "#VER OK". Lead's
timeline: one FEL load at ~17:21:30, no reload or ENTER_FEL after it, runs died 17:24-17:27, uptime says the restart
was at ~17:25:35, i.e. within 4 minutes of boot under bench traffic. The 0.2.12 case was retracted (its `now` 326 s
fits the load time). Bench since then: 0.2.11 30-minute soak (selective suspend off) clean, 0 restarts; 0.2.19 8
bench passes (800 x 64 KiB) clean, restarts 0, usb_irq_polled 54 in 90 s. A watchdog or power reset would boot the card's
0.2.11, so this is a jump back into the loaded image: to address 0 (low vector 0 is `ldr pc, _jump_reset`) or to
0x80000000 (eGON branch to `_reset`). Up to 0.2.18 the exception vectors spun (`b .`), so an exception would have
hung, not restarted: the cause is a NULL function call, a corrupt return address or a stack overwrite (inference).
fw-gen3's first suspect (inference, not seen): a NULL-pointer write into SRAM 0, where start.s copies the low vectors
and their literals (0x00-0x3F); a 0 in the IRQ literal at 0x38 sends the next IRQ to 0 and so to `_reset`. Signature
in the 0.2.19 record: type 0, reset CPSR mode IRQ (0x12), reset SP near the IRQ stack top 0x81FDA800, LR - 4 = the
interrupted code, not the writer. Next step then: compare the 16 vector words every tick and record the first change.
Code review by fw-gen3 found nothing in the tick path the real ISR does not also do (same body, same flags, IRQ mode,
no function pointers); EPIND is touched only by ISR bodies and IRQ-masked transport code.

Ruled out by code reading (fw-gen2; verify if in doubt):
- The 1.00o5 dispatcher `irq_handler` (interrupt.c) checks for a NULL handler, so a stray pending IRQ does not call 0.
- IRQs never nest: nothing re-enables IRQs inside a handler (`pc_irq_save/restore` keep the I bit), and the IRQ entry
  in start.s reloads SP at the top of the 100 KB IRQ stack.
- 1.00o5's own CDC send paths (`usb_CDC_in_ep_callback`, PC_interface.c) are no-ops under PC_PROTOCOL.
- Requests are zero-extended into `s->req` before dispatch, so short payloads cannot read stale bytes.

Suspects, in the lead's order, with where to look:
1. **Tick path runs the USB handler outside the USB ISR.** `pc_usb_poll_irq` (pc_transport.c, about line 535) is
   called from the timer glue in pc_hal_scope.c (about line 1257, right before `pc_poll(PC_CTX_ISR)`). After two
   quiet ticks it reads INTUSB/INTTX/INTRX and calls `usb_device_CDC_irq_body` (usb_interface.c, split from
   `usb_device_CDC_irq_handler` under PC_PROTOCOL) in timer-IRQ context with `pc_usb_in_poll` = 1. Check what the body
   can do there that the real ISR never does: EP0 setup handling with a stale `usb_setup_packet`, the bus reset and
   SET_CONFIGURATION paths, EP1/EP2 callbacks, every function pointer it calls. Only 0.2.12+ have this path, so the
   30-minute 0.2.11 baseline soak the lead is running is the discriminator. Quick experiment: 0.2.19 with
   `pc_usb_poll_irq` returning at once, under the same bench_link loop.
2. **MUSB Index register (EPIND).** The body writes EPIND without restoring it (usb_interface.c, the
   `*USBC_REG_EPIND = ...` lines around 413, 481, 649, 697, 708, 723; the real ISR does the same). Main-loop and
   BUSY-context transport code masks IRQs and saves/restores EPIND (pc_transport.c, the `epind` blocks around lines
   200-438), so by reading no unprotected user exists. Re-check with a grep for `USBC_REG_EPIND`, the indexed CSR
   registers and RXCOUNT over every file, including 1.00o5's menu.c debug helpers and mass_storage_class.c.
3. **Event ring and snapshot indexing.** `pc_evlog_add` (pc_transport.c, about line 78) indexes with
   `& (PC_EVLOG_SIZE - 1)` (128) and freezes up to `PC_EVSNAP_SIZE` (16) entries; `pc_hal_event_log` (about line
   592) and `pc_hal_event_snapshot` copy them; `h_get_event_log` (pc_cmds.c, about line 729) writes
   12 + 8 x 128 + 8 + 8 x 16 = 1172 bytes into the TX staging buffer. Sizes must stay powers of two. `pc_evlog_add`
   runs from the USB ISR, the timer ISR and main-loop code: every main-loop caller must hold IRQs off
   (`pc_usb_log_restart` does; check the others, e.g. the DTR, TX flush/abort and RX release paths).

Other places worth a look if those come up empty: the TX pump called from both the tick and the EP2 TX ISR on the
same staging state, the deferred and held request paths (READ_RAW hold), `pc_hal_indicator`'s blit (`pc_chip_px`
bounds), and IRQ stack depth (USB body + `pc_poll(PC_CTX_ISR)` + a handler in one tick).

Build variants for experiments: `make gcc10` (bench), `make`, `make PC_DEBUG=1 BUILD=build-dbg`,
`make PC_FIFO_MAP=1`, `make check-reference`. fw-gen2's script built all five plus sha256 in one go; recreate it as
a script file (section 2 on quoting).

## 10. Reboot-reason record (0.2.19)

As built, in `start.s` under `#if PC_PROTOCOL`:
- At `_reset` it stores LR, SP and CPSR as found to 0x81A00000 + 8 / 12 / 16. After a jump to 0 or 0x80000000, LR
  points just past the culprit call (or holds the stale LR of a corrupt `bx lr` / `pop {pc}`). The CPSR mode bits
  give the context: 0x12 IRQ (tick or USB ISR), 0x13 SVC (main loop).
- Every exception vector sets r11 = type (1 undefined instruction, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved
  vector, 6 FIQ) and branches to `pc_exception_common`. That stores type, LR, SPSR and SP of the exception mode plus
  the magic 0x45584321 at +20..+36, then branches to `_reset`. Fault PC = LR - 8 for a data abort, LR - 4 for an
  undefined instruction or prefetch abort.

Record layout, u32 words at 0x81A00000 (free DDR: .bss ends at 0x805A5030, the bootloader's sector-710 copy sits at
0x81BFFC00): [0] alive magic 0x4C495645, [1] restart count, [2] reset LR, [3] reset SP, [4] reset CPSR,
[5] exception type, [6] exception LR, [7] SPSR, [8] exception SP, [9] exception magic; since 0.2.48 [10] DFSR,
[11] IFSR, [12] FAR (GET_LINK_STATS 58-59, section 26). Since 0.2.48 the record's 1 MB section is mapped strongly ordered.

`pc_restart_check` (pc_hal_scope.c, about line 1147, first call in `pc_glue_init`) counts a restart when the alive
magic of the previous run is present, copies the fields (exception fields only with the exception magic), clears [9]
and sets the alive magic. `pc_glue_init` then logs event type 15 (a = exception type, 0 = plain jump to the entry;
b = low 16 bits of the exception LR, or of the reset LR). ENTER_FEL (`pc_restart_clear`) zeroes [0] and [1], so a
deliberate FEL reload does not count. Host side: GET_LINK_STATS 49-56 (`restarts`, `restart_exc_type`,
`restart_exc_lr`, `restart_exc_spsr`, `restart_exc_sp`, `restart_reset_lr`, `restart_reset_sp`,
`restart_reset_cpsr`), named in host_tests/bench_link.py and in mcp/.

Caveat (inference): DDR keeps its contents through a short power cycle and through a boot-menu FEL load, so a
`restarts` of 1 right after either may be false; the reset LR and CPSR then point into the bootloader or FEL path,
not into the image. Mapping an address to code:

```
wsl -d <distro> -- arm-none-eabi-addr2line -f -e /mnt/d/path/to/fnirsi-1013d-mcp/backups/fw-0.2.19-9b94c9a8.elf 0x800xxxxx
wsl -d <distro> -- bash -lc "arm-none-eabi-objdump -d /mnt/d/path/to/fnirsi-1013d-mcp/backups/fw-0.2.19-9b94c9a8.elf > /tmp/fw.dis"
```

`firmware/tools/map_addr.py` (fw-gen3) does both and decodes the record: `python3 firmware/tools/map_addr.py ELF
--restart v49 v50 ... v56` prints the mode at `_reset`, the fault PC with the LR offset applied, function + offset,
source line, the disassembly around it and the stack each SP lies in; `map_addr.py ELF ADDR...` maps plain hex
addresses (`--disasm N` adds N instructions either side). Run it in WSL (toolchain from ~/opt/gcc10, PATH or
CROSS=prefix-); from Git Bash put the call in a script file.

Keep the ELF of every later bench image next to its .bin: a rebuild moves addresses.

## 11. USB: lost interrupts, the tick fallback, SOF, port resets

**Closed: the tick fallback was removed in 0.2.21; the dispatcher does not lose interrupts, verified 2026-10-18**
(0.2.20 bench and 30-minute soak: counter 47 = 0, counter 57 = 2176). Counters 47 and 57 are retired slots (always 0),
event type 14 is retired, `make gcc10-nofb` is gone. The 2 ms RX sweep and the TX pump kick stay. The bullets below
are the history of 0.2.12-0.2.20, kept for reading old logs.

- **Lost interrupts.** 1.00o5's `irq_handler` reads INTC pending and then writes 0 to it, so an interrupt raised
  between the read and the write could be lost. **Unverified as a loss mechanism**: the F1C200s manual (3.5.6.4)
  calls the bits "Pending/Clear" without saying whether 0 or 1 clears or whether they follow the source level; the
  Linux sun4i/suniv irqchip (from memory) never writes pending for level sources, which would make the write harmless.
  Stock FNIRSI firmware writes 0 the same way. It also dispatches in IRQ-number order, so the timer handler runs before
  the USB handler when both are pending. The MUSB keeps its INTUSB/INTTX/INTRX flags latched until they are read, so
  a lost USB interrupt leaves its flags waiting. EP2 OUT had the 2 ms RX sweep and EP2 IN the pump kick as fallbacks;
  EP0 had none, so a lost EP0 interrupt leaves a control transfer unanswered until Windows fails the device (prime
  suspect for the error-22 drops, not proven).
- **The fallback** (`pc_usb_poll_irq`, 0.2.12+). 0.2.12 ran the whole handler on every tick and won the dispatch race
  against the real IRQ during traffic (counter 47 = 46,033 in 3 min). 0.2.14 added the **quiet-tick gate**: serve
  only after two consecutive ticks without a real USB ISR run (`pc_usb_isr_flags` counts runs). A flag the real
  interrupt is about to serve makes it run before the second quiet tick.
- **SOF.** The SOF flag (INTUSB bit 3) latches every frame although its interrupt is off, so the gate passed and the
  handler ran 300-500 times per second (0.2.14: counter 47 = 11.8k in 2 min; event log flooded with type 14, all
  INTUSB = 0x08). 0.2.12 and 0.2.14 each lost one 64 KiB census response, attributed to the tick-run handler. 0.2.15
  ignored SOF for the counter and the log; 0.2.16 split the 1.00o5 handler into entry (`usb_device_CDC_irq_handler`,
  reads the flags, calls `pc_usb_isr_flags`) and body (`usb_device_CDC_irq_body(usb, tx, rx)`), and the tick reads
  the flags itself and runs the body only for a flag other than SOF. 0.2.16 bench: 300 x 64 KiB clean, counter 47 =
  16 in about 7 min, every hit INTRX EP2 after 2-3 ms without any USB interrupt: real lost interrupts, so EP0 can lose
  them the same way.
- **Races, not losses (measured on 0.2.20).** Mechanism: the old gate opened after every gap of 2 ms or more
  without a USB interrupt. When the next USB interrupt pended in the same dispatch as a timer tick (or both pended
  inside an IRQ-masked section such as the PIO fill in pc_tx_fill), the timer handler (IRQ 13) ran before USB (26)
  and the old tick took the event the real ISR was about to serve. Which endpoint shows up depends on the traffic:
  0.2.16 status polling gave only EP2 OUT; the 0.2.19 capture soak (4 KiB TEST_PATTERN every 50 ms, 30 min) gave
  +2011 hits (1.1/s), of the 139 logged 128 EP2 IN (INTTX 0x04), 10 EP2 OUT, 1 EP0, after a 50 ms gap the first event
  is often the TX interrupt of a freshly submitted response. fw-gen3's earlier "only EP2 OUT, so not random losses"
  argument was specific to that traffic; the conclusion rests on the 0.2.20 measurement below, not on it.
  0.2.20, 8 bench passes, about 90 s: counter 47 = 0, counter 57 = 59, rx_swept 0, i.e. every former "lost interrupt"
  in that sample was a tick that coincided with a pending real USB IRQ. Conclusion: no evidence that 1.00o5's
  dispatcher loses interrupts; the 0.2.12-0.2.19 fallback only ever served events the real ISR was about to serve.
  Still to confirm: counter 47 over the 30-minute 0.2.20 capture soak and longer idle/open-close sessions (EP0).
- **0.2.20 fallback** (`pc_usb_poll_irq`). Never reads the flag registers to decide. It skips any tick at which the
  USB IRQ is pending (`pc_irq_dispatch_pend0`, stored by `irq_handler` under PC_PROTOCOL, or INTC_PEND_REG0 bit 26),
  and counts those ticks where the old gate would have served as counter 57 (races). Otherwise it checks endpoint
  state with EPIND saved: EP0 CSR0 RxPktRdy/SetupEnd/SentStall, an IN data phase with TxPktRdy clear, a SET_ADDRESS
  whose status stage completed (usb_set_faddr with DataEnd clear); EP2 RXCSR RxPktRdy while EP2 OUT is unmasked and
  not stalled. A condition seen on two consecutive ticks with no USB ISR run in between is a lost interrupt: the flags
  are read (clearing them lets a stuck line drop), the body runs with them plus the synthesized EP0 / EP2 OUT flags,
  counter 47 counts it, type 14 logs the flags as read plus bit 15 (EP0 condition) / bit 7 (EP2 OUT condition). A
  lost bus reset or suspend is still invisible (only in INTUSB). EP2 IN keeps the pump kick; the 2 ms RX sweep stays.
  Bench: counter 47 = 0 and counter 57 = 59 in ~90 s, as predicted.
- **Keep or remove the fallback (decision for fw-gen4 and the lead).** If counter 47 stays 0 over the 0.2.20 soak and
  some long sessions, the fallback has no observed purpose. fw-gen3 recommends keeping 0.2.20's version as a safety
  net: it acts only on endpoint state stuck for 1 ms or more with no USB IRQ pending, so it cannot race the ISR, and
  it costs a few register reads per tick. Keep counter 57 as a diagnostic either way (it is computed before any
  service decision). Removing it (PC_USB_TICK_FALLBACK=0 by default) is equally defensible and simplest; then also
  drop the race counter or keep it computed in the tick alone. The RX sweep and the TX pump kick are separate safety
  nets and are not affected by this decision.
- **Open: about two suspend + bus-reset pairs per session.** 0.2.16 counted usb_bus_resets 12 and suspends 10 over 6
  sessions with no drop seen by the host (link_resets 22 = 12 + 10). A host port reset shows as suspend, then bus
  reset. Unknown whether Windows silently recovers a failed control transfer, resets on port open, or something
  else. The 0.2.18 frozen snapshot (re-armed only on request) is the tool: read GET_EVENT_LOG without flag bit 0
  right after a session, look at the 16 events before the first reset. Host EP0 traffic per mcp-gen2 (from pyserial
  and usbser behaviour, not a USB trace): open = SET_LINE_CODING 0x20 + SET_CONTROL_LINE_STATE 0x22 (DTR 0, RTS 1),
  20 ms later 0x22 with DTR 1; close = 0x22 with DTR 0; no reconfiguration while open. In the snapshot, a setup
  (type 5) with bRequest 0x20/0x22 or a bad setup (type 6) right before the suspend + reset pair points at an
  unfinished open/close request; a suspend with no EP0 traffic before it, or a resume then a reset, points at
  selective suspend. On the maintainer's PC selective suspend is off on AC, on on battery (turning it off on battery is the
  lead's call: `powercfg /setdcvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3
  48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0`, then `powercfg /setactive SCHEME_CURRENT`). The mcp soak saves the
  frozen snapshot before re-arming it (mcp commits 2c6a4c5, 56bf8ad); no session-open path reads the log.
- **Drops seen** (Windows WriteFile error 22, device re-enumerates): 0.2.2 idle with the port closed, 0.2.3 during an
  SD write under back-pressure, 0.2.11 during 10 Hz GET_STATUS polling, 0.2.12 soak (two drops 7 s apart, the first
  with three suspend+reset pairs). The maintainer turned Windows USB selective suspend off before the 0.2.12 soak.
- **"Error state" incident** (0.2.2, port closed ~10 min, device present but CreateFile failed, cable replug
  recovered): not reproduced. 0.2.5 fixed EP2 OUT staying masked in INTRXE after suspend/resume. Other candidates
  (selective suspend + failed resume; 1.00o5's bus-reset handler writing CSRs with EPIND 0) unverified.

## 12. Diagnostics wire reference

**GET_LINK_STATS 0x0007** answers u16 n, u16 0, then n u32 counters since boot. Decode by n (word 0 is the count).
Authoritative text: protocol section 14 rows 28 and 38.

| n | firmware | indexes added |
|---|---|---|
| 27 | 0.1.4-0.1.7 | 0-26: rx_packets, rx_bytes, rx_stalls, rx_swept, tx_packets, tx_zlps, tx_kicked, tx_flushes, link_resets, dtr_up, dtr_down, frames, hdr_crc, payload_crc, oversize, version, resp_dropped, rx_timeouts, junk_bytes, responses, duplicates, tx_aborts, disp_main, disp_busy, disp_isr, held, isr_skipped |
| 29 | 0.2.0-0.2.1 | 27 init to first main poll (ms), 28 first pass (ms) |
| 31 | 0.2.2-0.2.3 | 29 first conversion, 30 first capture (ms + 1 since 0.2.4) |
| 35 | 0.2.4 | 31-34 first-pass marks: after battery, acquisition, touch, RTC |
| 41 | 0.2.5-0.2.6 | 35-40 bus resets, suspends, resumes, disconnects, EP0 setups, EP0 bad setups |
| 47 | 0.2.7-0.2.11 | 41-46 RX ISR stalls, releases, re-stalls, empty releases, RXCOUNT max, RXCOUNT > 512 |
| 48 | 0.2.12-0.2.16 | 47 usb_irq_polled (meaningful only from 0.2.16) |
| 49 | 0.2.17-0.2.18 | 48 boot splash ms + 1 |
| 57 | 0.2.19 | 49-56 restart record (section 10) |
| 58 | 0.2.20 | 57 USB IRQ races (section 11) |
| 58 | 0.2.21+ | same layout; 47 and 57 retired, always 0 |
| 60 | 0.2.48 | 58 fault status (DFSR / IFSR of the restart's abort), 59 fault address (FAR, data abort only) |
| 62 | 0.2.49+ | 60 USB resumes inferred without a RESUME interrupt, 61 USB soft reconnects (row 53) |

**GET_EVENT_LOG 0x0009** (protocol 1.5, any poll context). Request: empty, or since 0.2.18 u8 flags (bit 0 = re-arm
the snapshot after this read; other bits E_BAD_ARG detail 0). Response: u16 n (<= 128; 64 before 0.2.15), u16 0,
u32 total events since boot, u32 now (ms, same clock as uptime_ms), n entries oldest first (u32 ms, u8 type, u8 a,
u16 b). Since 0.2.15 a snapshot tail follows: u16 m (<= 16), u16 frozen (1 = a bus reset froze it), u32 reset ms, m
entries (the events before the first bus reset since the snapshot was armed). 0.2.15-0.2.17 re-armed at every read;
0.2.18+ only with flag bit 0. 0.2.24+ appends the bus-level ring: u16 k (<= 16), u16 0, u32 total, k entries (types 1-4, 7, 8, 12, 13, 15-18 only); identical EP0 setups are logged at most once per second. Types (pc_transport.h): 1 bus reset, 2 suspend, 3 resume, 4 disconnect, 5 EP0 setup
(a = bRequest, b = bmRequestType << 8 | wValue low), 6 EP0 packet not 8 bytes (b = COUNT0), 7 SET_CONFIGURATION,
8 CLEAR_FEATURE(HALT) (a = endpoint), 9 DTR (a = new DTR), 10 RX ring full, 11 RX release, 12 TX flushed, 13 TX
aborted, 14 retired since 0.2.21 (0.2.12-0.2.20: flags found by the tick, a = INTUSB, b = INTTX << 8 | INTRX; at most one per 100 ms; 0.2.20: low 7 bits each as read, bit 15 EP0 condition, bit 7 EP2 OUT condition), 15 restart
(0.2.19, section 10), 20 USB soft disconnect (0.2.49, section 27; a = reason | level << 2 | suspends << 4, b = s suspended). Type 3 since 0.2.49: a bit 7 = inferred, bits 6-5 = cause (0 SOFs, 1 EP0 setup, 2 EP2 OUT; 3 = the 0.2.44-0.2.48 encoding), bits 4-0 = POWER bits 4-0.

## 13. Boot, splash and SD package

- Boot gap: about 1.5 s from session init to the first main-loop poll (counter 27 = 1515-1517 ms up to 0.2.16), first
  pass 31 ms. Almost all of it is 1.00o5's splash step `load_picture("scope.bmp")`. Without the file (the normal
  case) it showed "Failed to open file" for 700 ms: the brief error the maintainer saw at every boot.
- 0.2.17 (quiet boot): `pc_glue_boot_splash` probes the file silently with `f_open` and skips the splash without a
  message; with the file present it shows as in 1.00o5 (3 s). Counter 48 = ms spent + 1. Maintainer verdict pending.
- `scope.bmp` must be the scope's own screenshot format: 800x480 RGB565 BMP, 768,070 bytes, 1.00o5's 70-byte header.
  It adds about 4 s per boot, so the release package leaves it out. The FAT partition needs nothing at boot; picture
  and waveform folders are created on the first save. README section 4 ("Boot", "SD card FAT partition").
- Image placement: 740 sectors from 750, ends at 1489, below the FAT partition at 2048+. Persist path
  (SD_STAGE + SD_WRITE_SECTORS, 32 sectors per write, SD_STAGE served from the timer poll since 0.2.7): 0.2.7 5.3 s,
  0.2.11 5.7 s with read-back identical and the header verified. SD command design: protocol section 14 row 40;
  writes need a valid MBR, window [16, min(first partition, 2048)) (16384 from fw 0.2.101: section 67), flag BOOT for
  16-79 and CONFIG for 708-710, power-key EINT masked during writes.

## 14. Maintainer's UI verdicts

Accepted: MCP chip (38 x 17 at x 127 y 3, above RUN/STOP, steady, one idempotent blit, stable while free-running on
0.2.11), compacted generator and trigger menus (0.2.8), acquisition "Roll mode on/off" box (0.2.10), menus hiding
what the hardware lacks (0.2.6), battery indicator stable on 0.2.11
(battery likely full). Pending on 0.2.13+: System settings without the empty row (compacted when no DS3231 answers),
battery hysteresis while charging from a lower level. Pending on 0.2.17+: quiet boot. Pending with fpga-gen2's 0x1533
bitstream: EXT trigger (caps bit 3, trigger menu EXT, GET_STATUS byte 42). Maintainer policy: hide unsupported options;
compact menus rather than painting over gaps.

## 15. Other known issues and closed items

- Request corruption (closed, 0.1.6): split-width FIFO read at the RX ring wrap, section 4; test_rx_ring covers it.
- RX corruption under back-pressure (parked): 0.2.3 SD persist had rx_stalls 307,837, hdr_crc 660, duplicates 658,
  then an error-22 drop. Not explained by code reading. No shipped host flow reaches back-pressure since 0.2.7
  (counters 41-46 never fired). Repro idea: flood 492-byte PINGs without reading responses, then read 41-46.
- First request after a port open (closed, 0.2.11): link events are taken in every poll context and DTR-up keeps a
  pending request. Test: test_dtr_up_keeps_request.
- Startup gap (closed): section 13.
- Latency: PING about 1 ms (timer poll); non-safe commands wait one UI pass (35-75 ms).
- Upstream: the 1.00o5 warnings (32-37 depending on the compiler) stay; menu debug helpers print nothing in protocol builds; 1.00o5
  UI pointer math off by one in the wrapped case (left as is).
- ENTER_FEL works hands-free on the bench. It does not save settings, and it clears the restart record.

## 16. Conventions and bench rules

- No libc (`-nostdlib`): memcpy/memset/memcmp/memmove come from the .s files. No `double` or float in acquisition or
  protocol paths. 64-bit integer math is fine (libgcc).
- (Until 0.2.46: PC_PROTOCOL=0 byte-identical to Atlan's image. Dropped by the maintainer; the switch is gone in 0.2.47.)
  Hidden features are removed in code, not masked (maintainer, 2026-10-18); keep settings sector / file slots stable.
- No new warnings under GCC 13 or GCC 10 (compare sorted, line-number-free warning lists against the parent commit).
- Small commits, one topic each, touching only firmware/ (plus docs/protocol.md section 14 and
  docs/protocol-vectors.json for wire changes), message ending with the Co-Authored-By line.
- Edits from Windows: put Python edit scripts in a file and run them with absolute paths; open files with
  `newline=''` and `encoding='latin-1'` for the 1.00o5 sources. Their blank lines carry trailing spaces, so anchor
  edits on non-blank lines (the Write tool strips them).
- Bump `PC_FW_VERSION` (pc_cmds.h) for every image handed to the bench; protocol additions bump `PC_PROTO_MINOR`
  (pc_protocol.h), get a row in protocol section 14 and a message to mcp-gen2 (owns mcp/ and the simulator).
- Report every bench image with path, commit hash copied from git output, and sha256 of
  `build-gcc10-pc/fnirsi_1013d.bin`; pin the .bin and the ELF in backups/.
- Bench: the agent never touches hardware. The bench session (team lead) owns the serial port `<port>`, FEL loads and SD writes. Never
  flash anything without a verified backup; the stock firmware must stay bootable. Only the bench session persists an
  image to the card; 0.2.11 stays there until the crash is fixed.

## 17. TODO, in priority order

1. Self-restart (section 9), state at fw-gen3's retirement: one confirmed case (0.2.16), none since. Clean: 0.2.11
   30-minute soak; 0.2.19 8 bench passes + 30-minute capture soak (35,579 requests); 0.2.20 8 bench passes. The 0.2.20
   capture soak was running. Interpretation (fw-gen3): the 0.2.12-0.2.19 tick body ran in timer context on live
   traffic about 1/s, always while the real ISR was about to run; 0.2.20 no longer does that. That correlates with
   the only restart, but **no mechanism is proven**: by code reading the tick-served body does what the real ISR does
   (section 9). So 0.2.20 removes the most likely trigger without a proven root cause. Keep the restart record in every
   image; on any restart run `map_addr.py <that image's ELF> --restart v49..v56` (section 10) and check first for the
   IRQ-mode signature of the vector-overwrite suspect (section 9). The 0.2.20 soak was clean (counter 47 = 0,
   57 = 2176), so 0.2.21 (fw-gen4) removed the fallback outright: the race that correlates with the only restart can
   no longer happen. 0.2.21 needs its own bench passes and soak before it replaces 0.2.20 as the persist candidate.
1b. USB drops on bitstream 0x1533 (bench 2026-10-18): 0.2.20 from the card had four host port resets within ~60 s
   (CLEAR_FEATURE 0x82, suspend, bus reset); 0.2.11 on the same bitstream ran 300 x 64 KiB clean. fw-gen4 audit: no
   ISR- or BUSY-context path touches the FPGA (GET_STATUS sends `ext_flags` cached at the MAIN poll since 0.2.9; the
   other 0x28 read is in capture finish, main loop / UI hook), and every EXT-gated line is the same in 0.2.11 and
   0.2.21, so EXT code alone cannot explain 0.2.11 clean vs 0.2.20 failing. `test_fast_path_no_fpga` (host test,
   mock `fpga_ops`) fails if a fast-path request ever drives the FPGA bus. Discriminator: 0.2.21 on 0x1533. If it
   drops too, the suspects are the non-EXT changes since 0.2.11 (event log, restart record in start.s, splash probe,
   battery filter, system menu) and boot conditions (card boot after a power cycle, generator on).
   Result: 0.2.21 FEL-loaded on 0x1533 was clean (4 passes); 0.2.21 cold-booted from the card lost EP2 IN in pass 3
   (tx_abort + tx_flush every ~3 s, no restart). So it is the cold-boot path, not EXT and not the fallback. 0.2.22
   resets the USB controller and PHY at init and logs what it found (type 16). Caveats (fw-gen4, inference): a stale
   data toggle cannot be it, since SET_CONFIGURATION already does ClrDataTog + FlushFIFO on EP2 IN/OUT and the
   failure came after 200 clean 64 KiB transfers. Also the FEL path runs the SD bootloader too (boot menu -> FEL).
   The cold boot differs further in that USB is plugged in after boot (six suspends in 1 ms = contact bounce with
   soft connect on and VBUS forced valid) and charging starts then. Discriminators: FEL load, then unplug and
   replug the cable; cold boot with the cable already plugged in. Later fact (lead): the episode was ~20 s of
   aborted 64 KiB responses, then service resumed with no bus reset (tx_aborts 23, tx_flushes 49). 0.2.23 logs
   EP2 TXCSR / POWER / frame number at each abort (types 17, 18; protocol row 43) to tell a host that stopped
   taking IN data (TxPktRdy stays set) from a device that stopped loading (TxPktRdy clear, UnderRun set).
   0.2.23 cold boot with the cable in: 1 abort in 500 x 64 KiB, type 17 TXCSR 0x0003 (TxPktRdy + FIFONotEmpty,
   no UnderRun), POWER 0x70 (HS), SOFs arriving: the host stopped taking IN data, device side fine. Type 16 differs
   by path: FEL 0x4000/0x070f/0x2000/0x0000/0x2fc0 (boot ROM left the block live, full speed), cold boot all 0.
   **PHY register (fact from source, effect inferred):** 1.00o5's usb_phy_setup writes 0x01C13410; Linux
   phy-sun4i-usb.c suniv_f1c100s_cfg uses REG_PHYCTL_A10 = 0x04 of "phy_ctrl" (dtsi: 0x01C13400, length 0x10),
   i.e. 0x01C13404 (0x10 is the A33+ offset). So the RES45 / TX amplitude / disconnect-threshold tuning
   presumably never reached the PHY in any 1.00o5-derived image. 0.2.25 writes 0x404 and logs a read-back
   (type 19; the read protocol, data at PHYCTL bit 16, is unverified). Inference, unproven: an untuned PHY
   gives a marginal HS eye. Counter-evidence to weigh: since 0.2.22 the PHY reset at init wipes whatever the
   boot ROM's FEL tuned, yet the 0.2.23 FEL runs (500 x 64 KiB) were clean; the cold-boot failure rate is
   about 1 per 500 transfers, so small samples cannot separate the paths. Judge 0.2.25 on long runs.
1c. Host-capture trigger position varies between runs at decimated rates (lead, 0x1533 / 0.2.21, lower priority; maintainer
   of the fix TBD, firmware or host geometry). Capture.pretrigger_samples vs the measured CH1 edge is stable within a
   run but differs between runs: 1 MS/s (divider 199) EXT +11/+12 us in the first 10 captures after selecting EXT,
   then -1.0 us in 17 captures over five setting orders; CH1 source +2/+3 us in one run, -1.0 us in another; 20 MS/s
   both -50 ns consistently. Suspects: FPGA decimation phase or trigger_addr-to-sample mapping at divider > 0 (words
   vs samples), 1.00o5 trace position state inherited by host-origin captures, a stale divider in the host geometry
   when UI and host disagree. Test: alternate CH1/EXT and timebases, log GET_CAPTURE_INFO (trigger_addr,
   capture_start_word, divider) next to the measured edge index (method in docs/hardware-status.md).
2. Read the 0.2.18+ frozen snapshot after sessions to explain the two suspend+reset pairs per session (section 11).
3. Maintainer verdicts on 0.2.13+ System settings and battery, 0.2.17+ quiet boot; EXT trigger bench with 0x1533.
4. Bench tests T3-T8 and T10 (section 7), then clear the UNVERIFIED caps and set FORCE_TRIGGER in release builds.
5. Phase 2/3 commands: READ_LIVE, READ_SCREEN (T9), ACQUIRE_AVG (128 KiB accumulators), full-speed fallback.
6. Per-unit USB serial (SID/eFuse) and a real pid.codes PID before anyone else flashes this.
7. Optional: composite CDC + MSC (needs a TinyUSB-style rework; plan section 9).

## 18. Handoff from fw-gen4 (retired at about 280k tokens, 2026-10-18)

State: 0.2.21-0.2.25 built, host-tested, pinned in backups/ with ELFs (ladder, section 8). **0.2.25 (c71935f9) is
on the card.** Nothing is uncommitted. The bench session is running the deciding test: cold boot with the cable in,
then the 30-minute capture soak (~35,000 requests).

What fw-gen4 did:
- 0.2.21: USB tick fallback removed. The 0.2.20 bench and soak showed counter 47 = 0 and 57 = 2176: the 1.00o5
  dispatcher never lost a USB interrupt and the fallback only raced the real ISR (section 11, closed). Counters
  47/57 are retired slots (always 0, n stays 58), event type 14 is retired, `make gcc10-nofb` is gone.
- Host test `test_fast_path_no_fpga` (mock `fpga_ops`): no FPGA bus access from ISR/BUSY polls. EXT audit clean.
- 0.2.22: USB OTG + PHY reset asserted at init; pre-init state logged (type 16).
- 0.2.23: EP2 IN state after each TX abort (types 17, 18).
- 0.2.24: GET_EVENT_LOG bus-level ring tail; identical EP0 setups logged at most once per second.
- 0.2.25: PHY tuning written to PHYCTL 0x01C13404; post-init state with PHY read-back (type 19).

**The PHY finding (verified on the bench 2026-10-18).** 1.00o5's `usb_phy_setup` writes 0x01C13410. Linux
phy-sun4i-usb.c `suniv_f1c100s_cfg` uses REG_PHYCTL_A10 = 0x04 of "phy_ctrl" (suniv-f1c100s.dtsi: 0x01C13400,
length 0x10), so PHYCTL is 0x01C13404 (0x10 is the A33+ offset). Evidence from 0.2.25 type 19 (FEL load):
before the writes the packed tuning reads 0x0068 (RES45 calibration off, TX amplitude 0x14, disconnect threshold
1), after them 0x00E9 (RES45 on, amplitude 0x14, threshold 3), i.e. exactly what was written; PHYCTL 0x2B80.
So the read-back method (address in PHYCTL bits 8-15, data at bit 16) works, and every image before 0.2.25 ran
the PHY at its defaults: no 45-ohm calibration, disconnect threshold 1. Since 0.2.22 that held on the FEL path
too (the init reset wiped whatever the boot ROM tuned). Before 0.2.22, FEL loads inherited the boot ROM's PHY
state, which may explain why only cold boots failed (inference). Other 0.2.25 post-init fields (FEL): POWER 0x70
(HS), INTUSBE 0x27, CCU flags 0xF, ISCR bits 4-19 0x3F00, USBPHY_CFG 0x0003.

What the soak decides: cold boot of 0.2.25 with the 30-minute soak clean (0.2.23 failed ~1 per 500 transfers,
within 5 minutes) = the untuned PHY was the cause; note it in section 11 and docs/hardware-status.md. If it still
fails, see the next candidates below.

Open, for fw-gen5 (in this order):
1. Soak verdict (above). If cold boot still fails: compare other state the boot ROM's FEL leaves (CCU PLL / AHB /
   USB clock registers between paths with a one-off dump event); consider host-side recovery timing. The failure
   signature is in TODO 1b (device holds TxPktRdy, SOFs arrive, recovers after the 2 s abort). Judge on long runs.
2. Spectrum analyser work starts next, per docs/plans/dpos350p-features.md (recommended order there): fixed-point
   FFT core plus an on-target benchmark, then the on-scope spectrum display mode. No float (no FPU, AGENTS.md);
   keep it out of the acquisition hot path. fpga-gen3 will publish an FPGA decimation interface (commands and
   semantics) that the spectrum path and the capture geometry must use; wait for its document, then wire it in
   pc_hal_scope.c / fpga_control.c and add the caps bit, a protocol section 14 row and host tests.
3. TODO 1c: host-capture trigger position varies between runs at decimated rates; likely tied to the decimation
   interface in item 2, so look at it together.
4. mcp/ (mcp-gen2) still needs decoders for event types 16-19, the bus ring tail and the retired counters 47/57.
5. The older TODOs in section 17 (2-7).

Gotchas from this session: Python heredocs via Git Bash turn `\0`-style C escapes into raw bytes; check with
`cat -A` or build escapes with `bytes([92])`. Bash variables inside `wsl ... bash -lc "..."` from Git Bash get
eaten; put WSL steps in a script and run it through PowerShell `wsl -d <distro> -- bash /mnt/c/...`. Every rebuild
changes the sha256 (build time is embedded): pin from the same build you report. Of the init entries, type 16 is
copied into the bus ring and type 19 is not (only the main ring and the snapshot hold it).

## 19. Decimation filter (fw-decim, fw 0.2.30, protocol 1.9)

Built and host-tested against fpga-gen3's interface for bitstream 0x1534 (not flashed yet, nothing benched). Spec:
docs/protocol.md section 14 row 47; design and file map: README section 5 "Decimation filter". Points a successor
needs:
- One rule for when the FPGA mode changes: at every conversion start (`pc_glue_decim_conversion` at the top of
  `fpga_do_conversion`, `pc_dec_apply` in `pc_hal_arm`). The UI acquisition sets `pc_dec_ui_arm` just before its
  `fpga_do_conversion`; every other caller (calibration, auto setup) gets K = 0, because they read per-core
  averages. A new acquisition path that should be filtered must set the flag (`pc_glue_ui_decim_arm`).
- `pc_dec_cur` (what the FPGA runs) decides the readout and the word geometry, never the setting: the setting only
  takes effect at the next arm, so a capture in the ring is always read the way it was taken.
- The display path never sees 16-bit data: `pc_glue_decim_read` converts into 8-bit codes and feeds
  `fpga_read_adc_data` through `pc_decim_src` (odd pass, then even pass, compensation pre-subtracted per core).
  READ_TRACE WIDE serves the raw u16 buffer (`pc_hal_trace16`).
- Memory depth with the filter on is at most 8190 samples (UI boxes 9000+ hidden, SET_TRIGGER / SET_DECIM refuse,
  `pc_decim_config` falls back to legacy as a last guard).
- EXT latency: `pc_decim_ext_shift` samples fewer pretrigger words (UI `pc_glue_decim_pre_words`, host `h_arm`),
  start computed from the words actually written; `ext_shift` per capture (GET_CAPTURE_INFO byte 70).
- 0x53 bits 2-4 (swap, falling-edge A / B) are `pc_decim_cfg.flags`, written with every mode; neg A / B are saved in
  word 207 bits 3-4, swap is not.
- Settle waits use timer0 ms (`fpga_delay` is not calibrated: 54 loops per "us").
- The spectrum work (fw-gen5) can take the 16-bit buffer and N / S / gain from `pc_hal_decim_current`.
Open: the bench list in README "Decimation filter"; TODO 1c (trigger position at decimated rates) should be
re-measured with the filter on, since its geometry is exact (no `T + 7441`); mcp/ needs GET_DECIM / SET_DECIM, the
READ_TRACE WIDE flag and the 76-byte GET_CAPTURE_INFO (message to mcp-gen2).

## 20. Spectrum analyser and auto trigger mode (fw-gen5, fw 0.2.26-0.2.41; retired at about 290k tokens)

Spec and design: docs/plans/dpos350p-features.md section 2; README section 5 "FFT core" and "Spectrum view"; protocol
section 14 rows 44 (FFT_BENCH) and 45 (READ_SPECTRUM, GET_STATUS byte 43).
- Files: `fx_fft.c/.h` (FFT, windows, dB; host-buildable, ARM assembly kernels with C twins), `fx_fft_tables.c`
  (generated by `firmware/tools/gen_fft_tables.py`, committed), `spec_fmt.c/.h` (number formatting, host-tested),
  `spectrum.c/.h` (FFT_BENCH, the view, READ_SPECTRUM). Hooks: `PC_SPECTRUM_DISPLAY` at the top of
  `scope_display_trace_data`, `PC_SPECTRUM_TOUCH` in the trace-area branch of `scan_for_touch`, `PC_MAIN_MENU_*` for
  the sixth main-menu row (`MAIN_MENU_HEIGHT` is runtime in protocol builds), `spectrum_new_data` in the UI capture
  hook and in `pc_hal_trace_build`, `spectrum_service` in `pc_glue_ui_service`.
- Bench facts: CP15 control 0x0005107C (I-cache on, MMU off, so the D-cache bit is ineffective); about 90 cycles per
  uncached data access, the stack included. With the stack spills gone SRAM A is ~2x faster than DDR, so the view
  runs N <= 2048 from SRAM A at 0x1000 (FFT_BENCH borrows the same space). That is why the hot loops are assembly
  (load+stage 1, stages, split, dB conversion). Measured 0.2.37: 4096 points 10.96 ms run + 1.44 ms dB in DDR, 2048
  points 2.30 ms in SRAM.
  Every change to them: `make -C host_tests test-arm` (qemu-arm, byte-compares with the C twin) and objdump the loop
  for stack traffic. Inline asm with 64-bit operands forces even register pairs and made GCC spill; write whole loops.
- Settings sector words 208 / 209 belong to the spectrum view (pc_hooks.h PC_SETTING_WORD_SPEC0/1); 207 is the Filter.
- Constraints: u8 input offset 0..255 (clamped) for the Q22 window trick; i32 input is C only (64-bit products).
  Buffers 8-byte aligned (`fx_fft_init` refuses otherwise).
- The view (README "Spectrum view" is the user-facing spec): N 1024..4096 limited by the record; fs from the UI time
  base, or 200 MHz / N with the 0x1534 filter (16-bit records via `pc_hal_trace16`, `fx_fft_run_u16`); axis capped at
  `PC_SPECTRUM_FMAX_HZ` (spectrum.h); gestures as the scope view (2 px / 200 ms: tap left/right = span out/in, drag =
  pan); span drives the time base past the ends (`spec_zoom`, `spec_tb_set` via pc_hal_timebase_write, then
  `spec.wait` until a capture at the new rate; fw 0.2.77 replaced this with the span ladder, section 52); Auto is a 3-stage state machine (`spec_auto_start` / `spec_auto_step`);
  Average field (`spec_acc`, keyed restart); waterfall rows RGB565, density count + last-hit capture index (no fading
  pass). Persisted at power-off: FFT length, window, scale (word 208), ref + averaging (word 209); not the mode.
  A host capture with a raw divider is labelled with the UI rate (row 45).
- Maintainer verdicts so far: spectrum view works, harmonics within 0.4 dB; waterfall "very nice"; density band fixed in
  0.2.35 (verdict pending); 0.2.35-0.2.40 UX items pending on the screen (header layout, gestures, Auto, averaging).
- Firmware auto trigger mode (0.2.41): README section 5 "Firmware auto trigger mode", protocol section 7.5. The
  FPGA's 0x0E time-out counts samples and 0x18 is not gated by the stop, so auto records at fast rates were untriggered
  but could read triggered. Auto now arms normal on Atlan bitstreams; `pc_ui_auto_service` (main poll) and the host
  poll write 0x1A = 0 at the deadline and clear the triggered flag. Calibration and auto setup keep the FPGA's auto
  (`pc_glue_decim_conversion` writes 0x1A per conversion). Bench: the 10 MS/s / 1 kHz square case from the lead's
  host captures (2 of 6 flat "triggered" records on 0.2.40) should give 6 of 6 single-edge records in auto mode, and
  the spectrum view's comb must not flicker.
- Maintainer verdicts open: 0.2.41 auto mode and zoom-in rate stepping; 0.2.42 Auto procedure (README "Auto"), warnings.
  0.2.40 header and averaging accepted (Avg tag removed from the header in 0.2.42). Accepted so far: spectrum view, waterfall, the CIC3 floor; the "wagging tail" was
  window-leakage variation, fixed by averaging (default 4).
- Next for fw-gen6: FPGA 0x1535 FIR support, once the lead confirms fpga-gen4's testbench numbers. Interface delta
  (lead, fixed): write 0x56 bit 0 = FIR enable; the FIR is active only when 0x56 = 1, K >= 1, N >= 16 and N % 4 == 0
  (D >= 14, D mod 4 in {2, 3}); 0x29 byte 0 bit 6 = FIR enable as written, bit 5 = FIR active now. Chain: CIC of
  order K decimating by N/2 (gain (N/2)^K), shift S, saturate to 16 bit, 15-tap FIR decimating by 2 with DC gain 1.
  Firmware S = max(0, bitlen(255 (N/2)^K) - 16) + 1; ADC mean = out 2^S / (N/2)^K; 16-bit trigger level =
  round(v (N/2)^K / 2^S); K = 3 exact while N/2 <= 10334. Latency 25 write clocks (250 ns) + EXT data lag 250 ns +
  17.5 N ns + 1.25 K (N - 2) ns; settle (K + 10) N 5 ns + 1 us after changing 0x0D / 0x53 / 0x54 / 0x56. Plan:
  extend pc_decim.c/h (FIR flag in pc_decim_cfg, gain/shift/scale/level/ext-shift/settle per the formulas, host
  vectors in decim_check + test_session), write 0x56 in pc_dec_apply, read back bits 5/6, a "FIR" toggle in the
  Filter box hidden when unsupported, SET_DECIM flags bit 3, GET_DECIM reports it, caps bit 16 FIR, protocol minor
  1.11 (fw-cal has 1.10), a protocol section 14 row, and settings word 207 bit for FIR. The spectrum view needs no
  change (it takes scale_q24 and the rate from pc_hal_decim_current); check that the FIR's halving of the output rate
  shows in `spec_fs_mhz` (rate = 200 MHz / N stays the output rate if N counts the whole chain; confirm with fpga-gen4).
- Other open items: an assembly u16 load if the filtered path's FFT speed matters (C now, about 2x the u8 path);
  `fpga/al3/src/zaklad.sdc` showed as modified in the tree all session (not firmware's, never committed by fw-gen5).
- TODO (lead, later, not fw-gen5's): enable the MMU with a flat 1:1 section map, DDR cacheable, the framebuffers and
  any DMA regions uncached, peripherals strongly ordered. The D-cache bit is already set (CP15 0x0005107C) but does
  nothing without the MMU. Numbers that motivate it: 0.2.27 FFT_BENCH 4096 points 24.3 ms in DDR (~90 cycles per
  data access), 18.1 ms with buffers in SRAM; 0.2.31 after the assembly rewrite 10.9 ms DDR, SRAM 2048 points 2.3 ms
  vs 4.9 ms DDR. 1.00o5's per-pixel display fills pay the same price on every pass.
- Plan cache: `fx_fft_init` runs only when N, window or the plan's memory (SRAM for N <= 2048, DDR above) changes, or
  after FFT_BENCH borrowed SRAM (init 0.84 ms at 2048 in SRAM, 2.17 ms at 4096 in DDR on 0.2.31).


## 21. Calibration data v2 (fw-cal, fw 0.2.43, protocol 1.10) and the v3 gain handoff (for fw-cal2)

fw-cal retired at about 500k tokens, 2026-10-18. Branch `fw-cal` = main 0.2.42 + the calibration work; the maintainer ran
0.2.43 (80ab45d, sha256 7d2ea871c6f9bca8cc2442eb9122205d9903399abb07dcfecf1e1580bed8be73). 3bfc892 (64 full-rate
conversions) is committed after it and in no image yet. Detail: README section 6 (6.1 what 1.00o5 does, 6.2 v2 design,
formulas, bench list), protocol section 14 row 48, `adc_cal` section of docs/protocol-vectors.json.

**v2 as built**
- Files: `adc_cal.c/.h` (pure C: model, block pack/unpack + CRC-32, `acal_correction`, `acal_range_store` checks,
  `acal_acc` statistics; `host_tests/test_adc_cal.c`, 596 checks, `--pack` feeds check_vectors.py), `scope_functions.c`
  (`scope_do_baseline_calibration` replaced 1.00o5's procedure and its globals; 708 load/save of the block),
  `fpga_control.c` (`fpga_channel_correction`, `fpga_prepare_correction` at the top of `fpga_read_sample_data`;
  `fpga_divider` = last 0x0D value; the loop holds comp / frac / dcshift / xorshift dither in registers and no longer
  divides per sample), `pc_hal_scope.c` (16-bit feed uses `corr16`; raw 0x0D writes update `fpga_divider`;
  `pc_hal_channel_read` copies the block), `pc_cmds.c` (GET_CAL 248 bytes; GET_CHANNEL / GET_CAPTURE_INFO / READ_TRACE
  meta zero = 256 T via `pc_channel_corr`), `spectrum.c` (16-bit zero from corr16), `menu.c` (diagnostics right column,
  failure text), `spec_fmt.c` (`fmt_x16`). `adc_cal.c` and `spec_fmt.c` are in the base SRCS (PC_PROTOCOL=0 links).
- Model per core k (A = 0x20/0x22 odd trace positions, B = 0x21/0x23 even), hardware range r (6 uses 5), clock class
  c: raw_k(0 V) = zero[c][k][r] / 16 + slope[k][r] / 65536 x dcoffset. FULL = legacy divider 0 and every filtered
  capture (ADCs at 100 MHz), GATED = legacy divider >= 1 (zaklad.v gates the encode clocks with the divider; the zero
  steps when gated and is flat from 20 MSa/s to 1 kSa/s).
- Calibration per channel and range: coarse centre (PWM 500 / 1200, gated), slopes per core at centre +- 150 (gated
  only), refined centre P where the mean of both gated cores reads 128, zeros at P: 8 conversions gated (2 MSa/s), 64
  at full rate (3bfc892; 0.2.43 has 8). Checks and reasons in README 6.2; nothing changes unless both channels pass;
  writes the 1.00o5 fields too (PWM centres, whole-code split of the gated zeros) and saves 708 + 709 at once.
- Correction: T = 128 - trunc(dcoffset x 100 / dc_shift_value); d16_k = 16 T - zero[c][k][r] - round(slope x dcoffset
  / 4096); comp_k = floor(d16 / 16), frac_k = rest; per sample code = clamp(raw + comp_k + ((xorshift >> 28) < frac_k)).
  16-bit path: corr16 = round(16 T - (zf16_A + zf16_B) / 2) added after `pc_decim_to_code` (now in 1/16 code).
- Sector 708 block v2: bytes 128..455, 328 bytes: u32 'ACAL', u16 version 2, u16 length 328, u16 classes 2, u16 0;
  per channel u16 pwm[6], i16 zero[class][core][range], i32 slope[core][range] (Q16), u16 noise[class][core][range];
  u32 zlib CRC of the 324 bytes before. Used per channel only while pwm[0..5] == dc_calibration_offset[0..5] (709).

**Bench results (bench session, 2026-10-18, 0.2.43)**
- Base calibration ran clean; GET_CAL version 2, flags 0x07 both channels. CH1 PWM centres 867 863 870 866 871 865;
  full zeros A 2105 2102 2105 2099 2102 2109, B 2069 2050 2049 2051 2044 2050; gated A 2096 2096 2098 2096 2096 2098,
  B 2006 2001 2007 2001 2002 2005. CH2 centres 854 849 857 852 857 853; full A 2077 2076 2078 2073 2077 2069, B 2084
  2073 2074 2093 2073 2088; gated A 2048 x6, B 2048 2049 2050 2050 2049 2047. Slopes (i32, Q16; the dump's int16
  pairs are low/high words): CH1 A -16998 -17067 -16916 -16971 -16971 -16957, B -17190 -17149 -17080 -17108 -17135
  -17094; CH2 A -17067 -17121 -16985 -17026 -16998 -17053, B -17381 -17449 -17312 -17353 -17312 -17381. Full GET_CAL
  hex: backups/gain_bench_1V.json.
- Shorted inputs, DC, dcoffset 0, READ_TRACE flags 0, 4 captures: CH1 (range 2) corrected A/B 127.94/127.87 at
  1 MSa/s, 127.97/127.95 at 10 kSa/s, CIC2 mean 127.94: **zero model verified**, legacy and filtered agree. 200 MSa/s
  128.14/127.72 (+0.42): offset-PWM ripple (a 1000-sample readout spans 5 us of the 40 us period), fixed in 3bfc892,
  unbenched. CH2 (range 6): 128.57/128.93 gated, raw equal (correction about 0 because the centre was set so the
  gated cores average 128.0 and they agree); the range-5 zero moved about +0.7 code since calibration. Hypothesis
  (unverified): warm-up / thermal drift at the most sensitive range, or relay settling. Checks: (a) CH2 raw at ranges
  5 and 4 now vs the stored zeros; (b) base calibration after 10 minutes on, measure range 6 at once and 10 minutes
  later.
- Pass time 30 ms at every rate (was 44-58 ms).
- 1.0045 V (DMM) on CH1 range 2, dcoffset 0, raw minus the shorted raw: full rate A +28.84, B +27.96 codes (A 3.1 %
  higher); gated A +28.15, B +28.64 (B 1.7 % higher). The sign of the gain mismatch flips between the classes, and
  the class means differ too, so the filtered (full-rate) and legacy (gated) volts disagree away from 0 V. At the
  calibration post with the generator off (about -1.24 V, -34 codes) the gated A/B ratio was about 0.994 vs 0.983 at
  +1 V: possibly non-linear (core INL); the maintainer offers a second point (-1 V or 2 V): ask for -1 V on CH1 range 2
  (200 MSa/s, 1 MSa/s legacy, CIC2) before choosing one gain or two (above / below zero). The PWM slope ratio (CH1
  gated A/B about 0.992) has the right sign but half the size: a weak gain estimator, do not use it.

**v3 gain design (to build as 0.2.45 on fw-gen6's 0.2.44)**
- Measure per class (full, gated) and core: slope tables per class (add the full-rate slope points; keep them for the
  offset term) and a signal gain from the voltage-calibration steps (`scope_input_calibration`, 300 mV..15 V on ranges
  5..0): record per-core means at each step in both classes (one extra conversion at 200 MSa/s per step). gain_k[c][r]
  = ref / (mean_k - zero_k), ref = the gated mean of both cores (what input_calibration is fitted on), so legacy volts
  keep their calibration and the full-rate class is pulled onto it. Alternative if simpler: a dedicated step with the
  generator square on the calibration post (both cores see the same node; plateaus give a gain above and below zero;
  the amplitude need not be known for relative gains, but the full/gated class ratio and the one-range-for-all
  assumption need a bench check). Inference to verify: the core gain mismatch is ADC-internal, i.e. the same on every
  range.
- Apply: G_k = round(g_k x 65536), O_k = 65536 T - G_k z_k (z_k with the offset term, 1/16 code scaled), per sample
  code = clamp((raw x G_k + O_k + (xorshift & 0xFFFF)) >> 16): mla + add + shift, about the cost of today's comp +
  dither; keep a whole-code comp_k for the rail clip-matching rule. 16-bit path: one gain per class folded into the
  scale (scale_eff = scale_q24 x g_full / 65536) and corr16 recomputed with it; spectrum.c uses scale_eff for the zero
  and the dB gain term.
- Block v3: noise as u8 (1/16 code) and slope[class][core][range] -> per channel pwm 12 + zero 48 + slope 96 + noise
  24 = 180 bytes, block 12 + 360 + 4 = 376 bytes at 708 byte 128 (ends at 504), plus the gain table if stored
  separately (re-check the 512-byte budget: gains per class/core/range as u16 Q15 add 48 per channel, too much: store
  gains per class and core only if they prove range-independent, 8 bytes). Version 3; v2 blocks are then not used
  (the maintainer re-runs base calibration, and the voltage calibration if the gain comes from it). GET_CAL bytes 92+
  change with it: new row (protocol 1.11), vectors (`adc_cal` section), mcp-gen3 told.

**Remaining bench (README 6.2 list)**: offset sweep -600..600 on ranges 0/3/5; AC vs DC on all ranges; Nyquist bin
with the 1 kHz square (now -34.7 dBV vs -61 floor: gain-limited); 1.000 V / 0.010 V vs host model; 0.2.42 with the
new card (block cleared after its 708 save); 3bfc892 at 200 MSa/s (target |A - B| <= 0.1).

**Host side**: mcp-gen3 implements GET_CAL v2 once fw-cal is merged (248-byte parse, 72-byte fallback; byte 72 = 2
means READ_TRACE 8-bit and zero_q8 are already corrected; READ_RAW / WIDE get zero / slope per class or corr16 / 16
after the 2^S / N^K conversion). The "zero_q8 = 0" seen on the bench was a host script attribute, not firmware.

**Gotchas**
- Worktree agents: git must run from the worktree directory, never `git -C` or `cd` into the main checkout; combined
  shell commands that mention git plus heredocs get refused, so write scripts into the scratchpad and run them. The
  scratchpad is shared between agents: use unique script names (fw-cal's `build.sh` was overwritten by another agent
  mid-session and built the other worktree).
- Files written with the Write tool from Windows may be CRLF (test_adc_cal.c was; Git Bash grep does not show the
  CR); check with Python `rb` and convert.
- Version numbers collide with parallel firmware agents: rebase onto origin/main right before building and take the
  next free PC_FW_VERSION (fw-cal went 0.2.35 -> 0.2.42 -> 0.2.43); commit messages keep the old number.
- `fpga_read_sample_data` with `calibrationsettings` gets no correction (not channel1/2), which the calibration needs
  (raw codes); `calibrationsettings.dc_shift_value` is 0, so the shift is guarded.
- Types: firmware `int32` is not `int32_t` (pointer mismatch warnings); no libc (`strncpy` did not link: use
  `fmt_str`).

## 22. fw-gen6: 0x1535 FIR, spectrum repaint, Filter default, USB suspend (fw 0.2.44, protocol 1.11)

Commits 2e0cd10 (FIR), 9591d83 (repaint), e09b25a (default), 45b9b9a (USB suspend), f9b8e17 (merge of main: fw-cal's
64 full-rate conversions), e0ee6f3 (generator menu hit areas). Image sha256
08f9a5ed0b4e7097367bd209004eec27d6d729514b2f3be846df93e8f0420b22 (from e0ee6f3). Nothing benched. Host tests: test_session 68908 / 68906 (debug) checks, 163 decim vector cases, test-arm byte-identical.
- FIR (README "Decimation filter", protocol row 49): `pc_decim_config` takes a capability level (`PC_DECIM_HAS_NONE /
  CIC / FIR`) and the setting bit `PC_DECIM_SET_FIR` (0x20); `fir_en` is what 0x56 gets, `fir` whether it engages
  (K >= 1, N >= 16, N % 4 == 0). With `fir` the configuration's gain is (N/2)^K and S has + 1, so every consumer
  (scale_q24, L16, corr16 feed, spectrum 16-bit path, GET_CAPTURE_INFO) is right without knowing about the FIR. Keep
  it that way: never recompute N^K outside pc_decim.c. Output rate 200 MHz / N confirmed by fpga-gen4's commands file.
  Settings word 207 bit 5 = FIR (valid mask 0xFFC4); SET_DECIM flags bit 3; caps bit 16 from version 0x1535.
- Filter box on 0x1535+: off, boxcar, boxcar+FIR, CIC2, CIC2+FIR, CIC3, CIC3+FIR (`pc_glue_decim_tap`). The box shows
  the setting; the spectrum header tag shows the effective state (FIR only where active).
- Default: `PC_DECIM_DEFAULT` (pc_decim.h) = CIC2, for sectors without a valid word 207 and the DEFAULT button
  (`PC_SETTINGS_DEFAULTS` in scope_reset_config_data). Maintainer: CIC2 | PC_DECIM_SET_FIR once 0x1535 is benched.
- Spectrum repaint (README "Redraws in the spectrum view"): `scope_preset_values` starts with `PC_SPECTRUM_DISPLAY()`;
  `spectrum_display` has a re-entry guard (spec_tb_set -> pc_hal_timebase_write -> preset -> display). Audit: no other
  host-driven redraw reaches the trace area. If the maintainer still sees the strip flicker on SET_TRIGGER / SET_GEN alone,
  look at what the host sent around it (MCP configure calls SET_TIMEBASE too) before searching the drawing code again.
- USB suspend (protocol row 50): suspend no longer runs `pc_usb_isr_link` (no flush, no DTR clear, no session reset);
  RESUME ends it, reset / disconnect reset the link. `pc_hal_dtr` reads 0 while suspended. `pc_usb_main_service`
  (main poll, 20 ms) logs a resume inferred from a moving frame number (type 3, a bit 7). Types 1 / 4 carry
  a = suspended before, b = ms since the suspend, which tells a port reset (a few ms after the SE0) from a reset after
  a real host suspend. 1.00o5 audit: RESUME was always enabled, EnableSuspendM never set (now cleared explicitly), no
  PHY / CCU handling on suspend, same as Linux musb. My reading of the 0.2.43 case (inference, unverified): "suspend ->
  bus reset" at reopen with nothing before is most likely the reset preamble, i.e. Windows did not suspend and reset
  the port for another reason; the new fields decide it.
  Bench: soak tool for 10 s (`fnirsi1013d-soak --duration 10`), close, wait 2-5 min, repeat 10 times; after every
  reopen read GET_EVENT_LOG and GET_LINK_STATS 36/37. Look at type 1 b (few ms vs minutes), whether type 2 appears at
  the close (host suspend) or only at the reopen, and type 3 with or without a bit 7. Also: Windows powercfg USB
  selective suspend on vs off (soak.py prints it). A `--mode reopen --idle S` in soak.py would automate this (mcp).
- Generator menu (e0ee6f3): `pc_glue_ui_has` returned the EXT cap for PC_FEAT_GEN_WAVE / GEN_ANALOG, so on 0x1533+
  the removed Wave row's touch area (y 60..108) overlapped the moved-up Freq row; they return 0 now (no DAC). Audit of
  the touch rows of the generator, trigger (Hold off gated, Always 50% shifted with PC_TRIG_DY), acquisition (Roll,
  Average, Filter / Long memory slot) and channel (FFT row gated by PC_CH_COMPACT) menus: nothing else stale.
- mcp/ needs: GET_DECIM byte 1 bit 3 / 0x29 bits 6-5 / caps bit 16 decoding, SET_DECIM fir flag, and the new event
  field meanings of types 1-4.

## 23. Calibration data v3: per-core, per-class gain (fw-cal2, fw 0.2.45, protocol 1.12)

Branch `fw-cal2` from main 854ed2a: 3aa3431 (protocol row 51, spec first), c8f17bf (adc_cal v3, readout, 16-bit path,
GET_CAL, diagnostics, vectors, host tests), f5fdf72 (gain step prompt), df61557 (0.2.45, CHANGELOG), 510f54c (README
6.3), then this handoff. Image from df61557: `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the fw-cal2 worktree,
sha256 32f259b4f4ea9ad435b64af41a2d90516b96461b1c9d2b126ea1957228dce5fa (not pinned in backups/ yet; built 2026-10-19 00:06; WSL cannot
resolve the worktree's Windows gitdir, so the identity line carries an empty hash: a rebuild on main after the merge differs
in hash and build time only). Nothing benched. Design, assumptions, cost and the bench
procedure: README 6.3; wire: protocol row 51.

**What was decided and why**
- Gain source: the calibration post, not the voltage calibration. The post needs a probe, so it cannot run inside the
  zero step (probes unplugged); it is a second step right after it, with its own prompt (calibrationopen 10). No known
  voltage is needed: only relative gains are measured, the gated mean of both cores stays the reference that
  `input_calibration` is fitted on. The voltage-calibration fallback of section 21 was not built (it needs a
  variable source and six steps; the post needs only the probes).
- The PWM-slope ratio is no gain estimator (gated slopes A/B 0.989 vs signal 1.004 on the bench): the offset slopes
  are measured per class for the offset term only.
- Range: the gains are measured on one range (2 for a x1 probe, 5 for x10, picked from a probe of both levels) and
  used on all. Assumption, bench step 7 of README 6.3 checks it. If it fails, store gains per range (the block has
  80 bytes left: 512 - 432).
- Centring: the post's levels are asymmetric (-35 / +53 codes at range 2); the PWM moves them symmetric about the 0 V
  code so even-order INL (core A's gated bow) cancels in the span. Host test: 0.07 % vs 0.53 % gain error.
- Readout: (raw x gain + off + top 16 dither bits) >> 16; the 1.00o5 v1 arithmetic is the special case gain 65536,
  off = comp << 16 (dither never carries). Rail rule rewritten on both cores' corrected limits (clip_lo / clip_hi),
  identical to 1.00o5 with unity gains. 16-bit readouts run the loop at unity (the feed carries gain16 and corr16).
- v2 blocks are not read (no conversion): the maintainer re-runs the base calibration.

**Host tests**: test_adc_cal 8590 checks: block, correction identities (v1 exact, unity-gain v3 = v2), every gain
check, and a model of the maintainer's CH1 from the gain bench (v2 reproduces A - B 0.91 code at +-1 V full rate and the
1.1 % class gap; v3 leaves A - B under 0.03 and both classes plus CIC2 within 0.05 code of the gated codes per volt).
test_session 68933 / 68931 (GET_CAL 256 bytes, flags, gains, offsets, block offsets), vectors `adc_cal_v3`
(check_vectors.py against `acal_pack`); the v2 `adc_cal` section stays because mcp/tests/test_cal_v2.py reads it.
Warnings: GCC 13 32, GCC 10 37, PC_DEBUG 32, PC_PROTOCOL=0 33, identical lists to 854ed2a.

**Not done / open**
- mcp/ (mcp-gen3): GET_CAL 1.12 parse (256 bytes; 72 = 3; bytes 92..111 gain_a, gain_b, off_a, off_b, gain16; block
  at 112 with i16 slope[class][core][range], u8 noise, gains Q15, gain_range / flags / lever), the volts model with
  the gains (READ_RAW: code = T + G (raw - z); WIDE: x gain16 / 65536), the simulator, then retire the v2 vectors
  section and test_cal_v2.py's use of it.
- The 16-bit trigger level ignores corr16 and gain16 (as in v2): a 1 % level error at full rate, cosmetic.
- No gain step on the stock bitstream (fixed 1 kHz on the post); the gains stay unity there.
- Bench of everything: README 6.3 steps 1-10.
## 24. fw-phosphor: digital phosphor view (fw 0.2.46, protocol 1.13)

Branch `fw-phosphor` from main 854ed2a (0.2.44). Commits f46e1c5 (design doc), faca915 + 04c60bc (map core + host
test), 15f15e0 (view, hooks, menu row, settings word), 8be5f3a (0.2.46 / protocol 1.13, row 52), 0570f6b (strip clear),
1b6359f (README, CHANGELOG). Image from 1b6359f: sha256 cdba5b514217f62dacceb5dcba8a36c4bb438c4c169988bb4702f588cbc8067e
(481,280 B, 940 SD sectors), pinned as backups/fw-0.2.46-cdba5b51.bin + .elf. Nothing benched. fw-cal2 works in
parallel on 0.2.45 / 1.12 / row 51: expect trivial merge conflicts in pc_protocol.h (minor + history comment),
test_session.c (the three proto-minor checks, take 13), CHANGELOG and the ladder above.

Design and arithmetic: docs/plans/phosphor-design.md; user-facing: README section 5 "Phosphor view"; protocol section 14
row 52. Points a successor needs:
- Files: `phos_map.c/.h` (pure C, host-buildable: rasteriser, Bresenham, render + decay, palettes, parameter maths;
  `host_tests/test_phosphor.c`, 95 checks: cost model = cells touched per record for flat / square / zoomed / worst
  shapes, decay to 1/16 at the persistence time at 33 and 50 ms passes, dither unbiased for small counts, steady states
  of the rate-normalised increment, brighter-channel rule, determinism) and `phosphor.c/.h` (target only: the view).
- Map: uint8 per pixel, **column-major** (400 bytes per column), 728 x 400, per-column extents `lo | (hi + 1) << 16` (0 =
  empty, so .bss zero is a valid empty map). Everything works on 32-bit words (uncached DDR): SWAR saturating add,
  two-lane decay products. Tables (2 code-to-row LUTs, 4 palettes) in SRAM A 0x0400-0x0FFF (`phos_sram_base` hides the
  literal from GCC's -Warray-bounds); the spectrum's FFT owns 0x1000-0x7008, the vectors 0x00-0x3F.
- Decay: every pass by k1^dt (Q24 per ms, `phos_persist_k1`, dt capped at 200 ms, none while stopped), applied in Q16
  per cell with a per-column dither (`(seed + 25033 x)` mod 65536). The first version used a 50 ms tick and Q8: rejected
  because ticks land on some 30 ms passes and not others (brightness shimmer). The render paints the count **before**
  that pass's decay, so saturation shows full brightness; CH2 compares its decayed count with CH1's decayed count.
- Increment: `phos_inc(k1, interval)` = 319 x (1 - k1^interval), interval = EMA of ms between added records; Infinite =
  1. Brightness therefore means hit probability at any record rate.
- Hooks (all under PC_PROTOCOL, `pc_hooks.h`): `PC_PHOSPHOR_SKIP()` before both `switch(scopesettings.ACQ_trace)` in
  `scope_display_trace_data` (skips the polylines), `PC_PHOSPHOR_DRAW()` before `display_REFx_data()` (new record,
  render, strip), `PC_PHOSPHOR_XY(index, last)` before the XY loop (skips it), `PC_PHOSPHOR_STRIP()` after the pointers in
  `scope_preset_values` (its grid redraw would otherwise paint the slider over the strip until the next pass), settings
  word 210 in `scope_save_config_data`. Glue in pc_hal_scope.c: main menu height 410 and row 7 (y 402..459, rect 413),
  `pc_glue_spectrum_touch` also asks `phosphor_touch` (strip y >= 452 only; the rest of the trace area keeps 1.00o5's
  gestures), `phosphor_new_data` in the UI capture hook and in `pc_hal_trace_build` only while the host capture is ARMED /
  TRIGGERED (so READ_TRACE re-reads do not add), `phosphor_capture_info` beside the spectrum's, `phosphor_service`,
  `phosphor_settings_restore` in `pc_ui_fix_settings`, `pc_hal_display_mode` = 4. PC_PROTOCOL=0 build verified
  byte-identical to 854ed2a's PC_PROTOCOL=0 build (GCC 10, fixed epoch); warnings 32 = 32 on GCC 10 and 13; the new files
  are clean with -Wextra. (`make check-reference` against Atlan's binary no longer matches on main since the maintainer
  dropped that rule; compare against the base commit instead, script idea: git archive the base into the scratchpad.)
- Clearing: per channel when `phos_inputs` changes (enable, coupling, V/div display + sample, input_calibration of the
  sample range, trace position, dcoffset, dc_shift_size): the LUT is rebuilt from `scope_get_y_sample` with the trace
  buffer pointer swapped for a 0..255 ramp, so rows match 1.00o5's traces, cursors and measurements by construction.
  Both maps on `phos_xkey` (dx Q16, disp_xstart / xend, time base, rate, trigger position, depth, settriggerpoint, filter
  K / FIR, XY, persistence). Not on trigger level (always-50 % moves it every record).
- Known limits / open: one record per pass (no fast loop; design-study steps P3-P5 are open: window readout, several
  records per frame, wfm/s counter is already there); the waveform-save thumbnail keeps the last scope-view
  tracepoints; the Trace selector does not apply; per-pass costs are estimates (the bench must measure `pass_ms`);
  `mcp/` `DisplayMode` lacks 2 / 3 / 4 (server falls back to the number; tell mcp-gen3).

**Bench steps (maintainer, with the bench session on <port>)**
1. FEL-load the image (`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), check #VER
   fw=0.2.46 proto=1.13. CH1 probe (x1) on the calibration pin, generator on, 1 kHz, duty 50 %; CH1 1 V/div DC, 200
   us/div, trigger CH1 rising, Auto.
2. Main menu: a seventh row "Phosphor display" under "Spectrum analyzer" (icon: three grey edges). Tap it. Expect: the
   scope screen unchanged except the bottom line: `Persistence 1 s | Palette Mono | Clear | Scope` boxes and "NN wfm/s"
   in grey at the right (about 25-33). The trace is yellow, sharp, both edges drawn, grid visible around it; pointers,
   measurement tiles and cursors (switch them on) where they always are. The main menu row now reads "Scope view".
3. Generator duty: toggle 30 % / 70 % (generator menu, or the bench session loops SET_GEN every 0.25 s): both falling
   edges visible at about equal brightness; stop toggling and the old edge fades within about 1 s.
4. Persistence: tap through 5 s (slow fade), Infinite (no fade; rare paths stay), 0.2 s (almost the normal trace),
   back to 1 s; each tap clears. Palette: Heat (frequent white / red, rare blue), Mono. Clear: the picture empties and
   refills at once.
5. Change V/div, drag the trace position, change the time base: the picture clears and rebuilds at the new place,
   no ghost. STOP: the picture freezes (no fade); RUN: continues.
6. CH2 on the same pin (cyan): where both overlap the brighter shows; turning CH2 off removes only its picture.
7. Optional: XY display mode: a Lissajous graded in magenta. Roll mode (slow time base): the view ends, rows 6-7
   hidden in the main menu.
8. Main menu row 6 while the phosphor view is on: switches to the spectrum view (its "Scope" field returns to the
   scope view). Row 7 from the spectrum view: switches to phosphor.
9. Host: GET_STATUS byte 43 = 4 in the view, 0 after "Scope"; `pass_ms` in the view vs the scope view for the 1 kHz
   square (expected within ~5 ms; report both); READ_SCREEN shows the graded picture. Power off / on: persistence and
   palette kept, boots into the scope view.
Bug indicators: trace not on the pointer / cursor positions (LUT mismatch); grid missing under or around the trace;
brightness shimmer at a constant signal; a picture that never fades at 1 s or never clears after a V/div change; the
strip briefly replaced by the slider on a time base change; wfm/s far below the scope view's record rate; `pass_ms`
over 50 ms for a single clean trace; any hang or restart (SRAM 0x0400-0x0FFF use; check GET_LINK_STATS 49-56).

## 25. fw-gen7: dead-code purge, reproducible images, 16-bit trigger level (fw 0.2.47, protocol 1.13)

Branch `fw-gen7` from main ca5aeb8. Commits, one topic each (bisectable): 0f84761 (check-reference, make package,
reference download, CI), 8770209 (PC_PROTOCOL=0 removed with unifdef; image byte-identical to ca5aeb8 with a fixed
identity), 473bd0b + 57d0d26 (reproducible image), 2731e52 (FFT switch), 55960cf (generator Wave / Ampl / Offset /
Phase), 3a5ffc6 (Long memory), 5906fbe (hold-off), f09e381 (v2 `adc_cal` vectors), 04619ca (16-bit trigger level),
463b489 (0.2.47, CHANGELOG, README, docs), then this handoff. Image from 463b489: `build-gcc10-pc/fnirsi_1013d.bin`,
481,792 B, sha256 b5098dc48d2605f38a967585ea6d7a1671f0c98568176884e1450c7ab6f5b2b0, ELF
5f7f463cb8709a67afa5b88712812023371e042757406a26a9218a5b7ed53a32 (identical from the worktree and two fresh clones at
other paths). A build of a later commit differs only in the 10 identity bytes. Not pinned in backups/, nothing benched.

**Reproducible images.** Cause of 0.2.44's main d4c0b716 vs worktree 08f9a5ed: WSL's git cannot follow a worktree's
`gitdir: D:/...`, so the worktree build had PC_FW_BUILD 00000000 and the wall-clock time; both were compile-time
constants in the code (-DPC_FW_BUILD in every file, COMPILE_* in pc_version_init), and GCC folds the hex loop /
literal pool differently for 0 (80 bytes less .text), shifting everything after pc_hal_scope.o. No __FILE__ /
__DATE__ / __TIME__ / assert is used. Now: build_time.h (generated, rewritten only when it changes) carries COMPILE_*
from the commit time (UTC) and PC_FW_BUILD; only variables.c (RTC defaults, data) and build_id.c (pc_fw_build,
pc_build_time[6], linked last) include it; pc_cmds.c / pc_hal_scope.c read the data. The Makefile reads hash and
commit time from git, translating a worktree's Windows gitdir to /mnt/x/...; without git it stops and asks for
`PC_FW_BUILD=... SOURCE_DATE_EPOCH=...`. `-ffile-prefix-map=$(CURDIR)=.` (C) and `-Wa,--debug-prefix-map` (gas does not
get the GCC option under GCC 10) make the ELF path-independent. CI prints the image sha256 (log line + notice). The
identity line now reads `git <hash> of 20yy-mm-dd hh:mm` (commit time, UTC). Host tests define pc_fw_build = 0 in
mock_hal.c. Useful side effect: for a pure dead-code commit build parent and child with a fixed identity and compare.

**What was removed** (settings sector 709 and waveform file slots kept, written 0, skipped on load; wire unchanged):
- PC_PROTOCOL=0 paths (12 files, unifdef), the empty hook variants, the Makefile switch; `make check-reference`,
  `make package` (PACKAGE_BASE came from the reference download; `tools/make_release_sd_package.py build` makes SD
  packages), the `reference` subcommand of that script, firmware/reference/README.md, the CI reference cache/fetch.
- Channel menu FFT switch (scope_channel_fft_show, draw/touch, PC_CH_COMPACT; PC_CH_DY folded into coordinates).
  `fftenable` stays as a slot. kiss_fft was never in this tree.
- Generator Wave / Ampl / Offset / Phase: select functions, keyboard sub-menus + amplmenuopen / offsetmenuopen /
  phasemenuopen, touch branches, fpgasettings fields, defaults, PC_FEAT_GEN_WAVE / GEN_ANALOG, PC_GEN_COMPACT /
  PC_GEN_DY. The top-bar frequency text is always orange (green / yellow were only for sine / triangle).
- Long memory: the menu box function, touch branch, empty main-loop branch, scopesettings.long_memory,
  PC_FEAT_LONG_MEMORY; the slot is PC_MASK_ACQ_FILTER (Filter box on 0x1534+, empty background otherwise).
- Hold-off: menu text, touch branch + keyboard sub-menu, holdoffmenuopen, fpgasettings fields, PC_FEAT_HOLDOFF,
  PC_TRIG_COMPACT / PC_TRIG_DY. SET_TRIGGER still answers E_UNSUPPORTED detail 6 for a hold-off.
- Unused PC_MASK_GEN_MENU / TRIG_MENU / SYS_RTC (the RTC mask call was a no-op).
- docs/protocol-vectors.json `adc_cal` (v2) section and its generator (the regenerated file differs only by it).
Warnings: GCC 10 37 -> 36, GCC 13 32 -> 31 (menu.c `modetext` set but not used is gone), PC_DEBUG 31.

Sizes (GCC 10 release image, text / data / bss, image bytes): ca5aeb8 366,009 / 118,416 / 7,756,136, 484,864 B;
463b489 362,985 / 118,448 / 7,756,120, 481,792 B (-3,024 text, +32 data: build_id.o, -16 bss). GCC 13: 370,025 ->
366,905 text.

**Decided to keep (and why)**: the hook macros in pc_hooks.h (they mark every place the protocol reaches into a
1.00o5 file, README section 5 lists them; inlining them would spread pc_glue calls without a marker); settings and
file slots of removed features (format stability); `usb_irq_polled` / `usb_irq_races` slots 47 / 57 (wire, already
retired as always 0; no dead code left behind them was found in the time I had: re-check pc_transport.c for code that
only feeds them); the RTC and EXT masks (hardware-dependent, live).

**Trigger level fix (04619ca).** pc_dec_apply wrote round(level x N^K / 2^S), the inverse of the uncorrected
transform; the trace shows out x acal_scale(scale_q24, gain16) / 2^24 + corr16 / 16. New pc_decim_level16_corr
(pc_decim.c) inverts pc_decim_to_code with the trigger channel's gain16 and corr16 (fpga_channel_correction at arm
time, so a V/div or offset change since the last readout is included); EXT keeps the plain conversion. Host test
test_adc_cal `test_trigger_level16` (59,717 checks in all): exact round trip for every level, the old conversion at
unity, the 1 % case. Inference to bench: triggerlevel is in the corrected trace domain (it is computed from the
screen position by fpga_set_trigger_level); the 8-bit legacy path still compares raw per-core codes (unchanged).

**Not done (for the next agent)**: a --gc-sections diagnostic (`EXTRA_CFLAGS="-ffunction-sections -fdata-sections"`
and `-Wl,-e,_start -Wl,--gc-sections -Wl,--print-gc-sections`, the linker script only names `.text`, so diagnosis
only) lists about 150 unreferenced functions; candidates after a grep check: test.c (filter experiments,
scope_test_*, scope_get_long_timebase_data1, convert_waveforms_to_csv_excel, display_text_message), PC_interface.c
(OEM-app helpers my_str*, usb_CDC_process_rx, send_hex_byte, usb_send_uint), spi_control.c sys_spi_flash_*,
usb_interface.c usb_CDC_send_*/usb_write_ep2_data, cdc_class.c circular_buffer_* / usb_CDC_in_ep_callback,
scope_functions.c sinx / thumbnail / fir variants, display_lib.c unused draws, ff.c unused API (FatFs: keep, it is a
library), fpga_control.c fpga_average_trace_data_long / fpga_read_short. Many "removed" entries are only inlined
static copies, so grep every name before deleting. Files never built: font_6.c, font_7.c, font_8.c, malloc.c/.h.
1014D remnants are comments only (display_control.c 187-188, spi_control.c 27). Remove in topic commits and check
with a fixed identity that only the expected functions change.

**Bench checks (bench session)**
1. FEL-load the image, `#VER` fw=0.2.47 proto=1.13; GET_INFO byte 52 = 0x463b4895 (or the hash of the built
   commit); diagnostics screen identity line "git 463b4895 of 2026-10-18 23:04" (UTC).
2. Census (bench_link.py --census 2000) and the mcp soak for 10 minutes: clean as 0.2.46.
3. Maintainer taps: channel menus CH1 / CH2 (Open CH, Invert, Coupling, V/A, Probe, V/div; no FFT row); generator
   (Power, Freq, Duty, the keyboard for Freq and Duty, nothing below Duty reacts); trigger menu (mode, edge,
   channel incl. EXT on 0x1533+, position, Always 50 %; no hold-off); acquisition menu (Roll, Average, Filter box on
   0x1534+ / empty slot on older, memory depths, time/div); system settings and RTC row; DEFAULT button; power off
   and on (settings restored, generator frequency / duty kept).
4. Trigger level in CIC mode (0x1534+): CIC2 at 1 MSa/s, generator 1 kHz square on CH1, normal trigger, level line
   at mid-edge and near both plateaus: the trigger point (GET_CAPTURE_INFO pretrigger vs the edge crossing the level
   in READ_TRACE) should sit on the level within one code; compare with 0.2.46 (about 1 % / 1 code off). Also with a
   dcoffset and on CH2.
5. CI on the merge: the firmware job's "image sha256" line equals a local `make gcc10` of the same commit.

## 26. fw-mmu: MMU + D-cache (fw 0.2.48, protocol stays 1.13)

Branch `fw-mmu` from main 1950e58 (0.2.46 + mcp 1.13): 6e36a89 (design, docs/plans/mmu-dcache.md), d486c21 (table
builder + test_mmu), 8cdefda (enable / exit paths, fault registers in the record), 52fe65a (GET_LINK_STATS 58-59),
e877c75 (mcp names), b1922c3 (0.2.48, CHANGELOG, README, protocol row 38). Image from b1922c3 (GCC 10, PC_FW_BUILD
passed by hand): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin`, 485,888 B, sha256
2f768fd9e9a02741e4c1b6c587bdaa7eebf01edb8cc51dc4e907cb92a725ad93; A/B `build-gcc10-nommu/fnirsi_1013d.bin`
(`make gcc10-nommu`, #VER `fw=0.2.48-nommu`) sha256 45e03f7f86669e1f10be7b7ce8edda6995839fe4755340574315e58ba8342956.
Not pinned in backups/ yet (the worktree agent cannot write there); the ELFs sit next to the .bins. **Nothing
benched.** fw-gen7's 0.2.47 purge runs in parallel: my changes outside the new files are small (start.s, a block at
the end of pc_hooks.h, one line in main, Makefile, linker ASSERT, pc_fel_jump, the restart record in pc_hal_scope.c,
pc_cmds.c link stats, pc_cmds.h version); expect conflicts only in pc_cmds.h (version), CHANGELOG and the ladder.

**What it does** (reasoning and the audit: docs/plans/mmu-dcache.md)
- `pc_mmu.c/.h`: `pc_mmu_build` (pure C) fills 4096 section descriptors, domain 0 client, AP 11, no fault entries:
  section 0 (SRAM A) write-back (`PC_MMU_SRAM=0`: strongly ordered), DDR 0x80000000-0x81FFFFFF write-back except the
  restart record's section 0x81A00000 (strongly ordered, so the record reaches DDR without a clean), everything else
  strongly ordered (= the MMU-off behaviour). The pages holding `maindisplaybuffer` (the only buffer another master
  reads: DEBE layer 0) are write-through via two coarse tables. `pc_mmu_enable` (target): build, drain, invalidate
  I/D/TLB, TTBR, DACR 1, control |= M C I with A S R V RR cleared; returns the control word. Called by
  `PC_MMU_ENABLE()` right after 1.00o5's `arm32_dcache_enable()` in main (after the BSS clear, before IRQs).
- `start.s`: `pc_mmu_off` (no stack): if the MMU is on, test-clean-invalidate the whole D-cache; drain; M C I off;
  invalidate I/D/TLB. Called at `_reset` right after the record stores, with IRQ/FIQ masked, so every restart path
  (NULL call, exception, eGON entry) starts from the cold-boot state with DDR coherent; and from `pc_fel_jump` before
  its own cache-off and the jump to 0xFFFF0020. With the MMU off at entry it only invalidates (it never cleans lines
  of unknown content over a freshly FEL-loaded image).
- Exceptions also store DFSR / IFSR / FAR at record words 10-12; GET_LINK_STATS 58 = fault status (DFSR for type 4,
  IFSR for type 3), 59 = FAR (type 4), n = 60. The fault PC is still counter 51 - 8 (data abort) / - 4.
- Framebuffer write-through, not write-back: the ARM926 D-cache allocates on read misses only, so 1.00o5's
  store-mostly drawing gains nothing from write-back, while write-back would need cleaning (no page flip; modal
  menus idle with dirty lines on screen). Write-through is coherent by construction and still removes the per-store
  stall (the write buffer takes it). Follow-up if pass_ms says the stores drain too slowly: write-back plus a clean of
  the whole D-cache in the 1 ms tick (at most 512 lines).
- Not needed: an alignment audit (A stays clear, as with the MMU off), volatile changes (one core, no DMA: SD and USB
  are PIO, the FPGA bus is GPIO), delay-loop changes (tp_delay, fpga_delay, sd_card_delay are register-only asm).

**Risks I see** (inference; the bench decides)
1. Something reads DDR that I missed (another bus master, or a buffer handed to the display engine elsewhere):
   symptom stale or torn screen content. Only `sys_init_display` sets a scan address (DEBE_LAY0_FB_ADDR).
2. Code that only worked because it was slow: USB FIFO / CSR sequences, the touch I2C bit-bang between tp_delay
   calls, FPGA pulse spacing where a trace-buffer store used to sit between two GPIO accesses (pulse widths are
   device-access bound and unchanged). Symptoms: tx_aborts / drops in the soak, dead or erratic touch, wrong samples
   (TEST_PATTERN content check, spikes in captures).
3. A hand-over with dirty lines or the MMU on: the bench's CP15 0x0005107C (MMU off at run time, nothing in the image
   turned it off) rules it out for the bootloader; the FEL `exe` after ENTER_FEL is new: the old image cleans before
   jumping, so the new one must come up exactly like a cold FEL load.
4. D-cache size unknown (16 KB per the F1C100s datasheet, unverified): performance only.
5. Record words 10-12 are written only by 0.2.48+ vectors; they are reported only for exception types 3 / 4 with
   the exception magic, which the same image wrote.

**Bench sequence** (bench session + maintainer; FEL loads only, nothing written to the card unless the bench session decides)
1. FEL-load `build-gcc10-pc/fnirsi_1013d.bin` (`sunxi-fel -p write 0x7FFFFFE0 ... exe 0x80000000`). Expect the
   normal boot within the usual time, USB enumerates, `#VER` `fw=0.2.48,proto=1.13`. Black screen / no enumeration =
   the enable path (a power cycle boots the card's image; then try -nommu to separate the MMU from the rest).
2. `fnirsi1013d-bench --fft --reps 3`: CP15 control must read **0x0005107D** (0x0005107C on -nommu). Reference
   (0.2.37, MMU off): 4096 DDR 10.96 ms run, 2048 DDR 4.9 ms, 2048 SRAM 2.3 ms. Expected: DDR rows 2-4x faster
   (4096 about 3-5 ms, 2048 about 1.2-2.5 ms), SRAM rows 1-1.5x, DDR and SRAM rows close together. -nommu numbers on
   0.2.48 = MMU not effective (check the CP15 word). Optional third image `make gcc10 PC_MMU_SRAM=0` (SRAM strongly
   ordered) shows whether SRAM caching matters; if DDR ~ SRAM, the spectrum view's SRAM plan can go later.
3. pass_ms (GET_STATUS, 20 samples each, both images): scope view with the 1 kHz calibration square at 200 us/div
   and at 1 MSa/s; spectrum view N 2048 and 4096 (also waterfall and density); phosphor view (Persistence 1 s).
   Baseline: scope view about 30 ms. Expect a clear drop wherever the display or the FFT dominates (scope view
   perhaps 15-25 ms; the FPGA readout at ~0.3 us per byte does not change), the largest in spectrum and phosphor.
4. Display correctness: open and close every menu over a running trace, spectrum waterfall and density, phosphor
   with fading, the MCP chip, text fields, the FEL screen. Leftover pixels, text that appears late, torn rows, or a
   screenshot (READ_SCREEN / picture save) that differs from the panel = coherence.
5. `fnirsi1013d-bench` (link checks + census 2000) and readback rates: equal or better than -nommu; TEST_PATTERN
   content errors or lower rates = regression.
6. `fnirsi1013d-soak --mode capture --duration 1800`: 0 restarts (GET_LINK_STATS 49), tx_aborts as on 0.2.46, no drops.
7. SD: save a picture and a waveform, view them back on the scope; power off / on keeps the settings (sector 709
   written by the power-off ISR); `fnirsi1013d-sd info --verify` and `fnirsi1013d-sd read 708 3 -o ...` equal to a
   -nommu read. The persist path (SD_STAGE + SD_WRITE_SECTORS) only if the bench session wants this image on the card;
   read-back identical is the check.
8. ENTER_FEL round trip x3 from 0.2.48: FEL device appears, reload, it boots, counter 49 (restarts) = 0; once load
   -nommu after 0.2.48 and once 0.2.48 after -nommu. FEL device missing = the jump; a reloaded image misbehaving or
   restarting = stale cache state (pc_mmu_off not effective).
9. Touch: taps and drags in the trace area, menus, spectrum gestures. Dead or erratic = I2C timing.
On any restart: GET_LINK_STATS 49-59 and `map_addr.py <0.2.48 ELF> --restart v49..v56`; 58 / 59 give the fault
status and address of a data abort (FS 0x5 / 0x7 translation, 0x9 / 0xB domain, 0xD / 0xF permission: none can come
from this table; 0x1 / 0x3 alignment; 0x8 / 0xA external abort).

## 27. fw-usb2: USB suspend watch, inferred resume, soft reconnect (fw 0.2.49, protocol 1.14)

Branch `fw-usb2` from main 603761d. Commits: 3b50641 (protocol row 53, written first), c20b657 (pc_usbw.c/.h state
machine + host_tests/test_usbw.c), 46d9817 (wiring in pc_transport.c, 1 ms tick, counters 60-61, type 20, ISR order,
0.2.49 / 1.14), f3763cc (mcp names and decoding), 46f3db2 (cause codes 0-2), 46e473f (powercfg GUID fix in the guide
and the skill), e77785c (long-rule back-off), a1d5e8f / c477faa (CHANGELOG). Image from c477faa (GCC 10):
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin`, 483,840 B, sha256
90f460d6f8e419d04de499547c0292b0a3c96a3c7b35c2b43a36691d3d1f8588; A/B `build-gcc10-nommu` (`#VER fw=0.2.49-nommu`)
sha256 f5f377ea5befe01794ca85114734da06cf172de7c92d691306574b0d93055f79. Not pinned in backups/ (worktree agent).
**Nothing benched.** Host tests all green (test_usbw 90 checks, test_session 68937 / 68935, test-arm identical); mcp
pytest 360 passed, ruff (CI selection) clean.

**The case** (docs/hardware-status.md 2026-10-19, bench session's brief): fw 0.2.48-nommu, port closed 08:40-10:20, at
10:20 no 1209:0001, no re-enumeration, no FEL device, screen alive; replug fixed it; afterwards counters 36 / 37 / 49 =
17 / 0 / 0. Overnight on 0.2.44 (backups/night_log_20261018.jsonl, decoded): 53 reopens after 10 min idle, counter 36
stayed 1 (the boot suspend: type 2 a = 0x60 b = 0 at 712 ms, bus reset a = 1 b = 127 ms, SET_CONFIGURATION), so this
host does not suspend an idle closed port within 10 min; fw-gen6's "Windows never suspends the device" (section 22)
holds only for short idles. **Counter 37 has been 0 on every build and bench since 0.2.5** (docs/hardware-status.md
0.2.5, 0.2.11, 0.2.43 entries): a RESUME interrupt has never been observed on this controller.

**Audit** (facts from the code and the logs, inferences marked)
- INTUSBE is written once, in `usb_device_enable`: SUSPEND | RESUME | RESET | DISCONNECT = 0x27 (type 19 field 1 at
  boot reads b = 0x270F: INTUSBE 0x27, CCU flags 0x0F). Only `usb_device_disable` clears it (MSC switch, ENTER_FEL).
  No other code reads INTUSB since the tick fallback went in 0.2.21, so nothing else can consume the RESUME flag; the
  Allwinner INTUSB is write-1-to-clear (the ISR writes the bit back, like Linux `sunxi_musb_interrupt`; the Mentor guide
  says read-to-clear). Neither the DTR state nor the 0.2.44 "suspend is a state" change masks anything: EP0 is served
  entirely in the ISR, the main loop kept running; a missed RESUME only left `pc_usb_suspended` set, i.e. DTR read 0 to
  the session until the main-loop frame check (every 20 ms, stopped while a menu is modal) logged the inferred resume.
- POWER: EnableSuspendM (bit 0) cleared at enable, so SUSPENDM is never driven and the PHY is never put in suspend
  (MUSB guide musbmhdrc_pspgUSB.pdf 8.4.4 / 10.1: SUSPENDM low only if enabled; POWERDWN "may be used to stop CLK",
  nothing here does). No CCU or PHY register is touched on suspend. PHYCTL low 16 bits after init 0x2B80 (type 19 field
  6; = the last tuning write's address 0x2B, data bit 7): the SIDDQ positions Linux phy-sun4i-usb uses on newer SoCs
  (bit 3 A83T, bit 1 H3; from memory, not in research/notes) are 0 and nothing writes PHYCTL later. Same as Linux
  musb + sunxi glue, which handle suspend / resume in software only (section 22).
- MUSB guide, Power D1 SuspendMode: "In Peripheral mode, this bit is set on entry into Suspend mode. It is cleared when
  the CPU reads the interrupt register"; IntrUSB D1 Resume: "Set when Resume signaling is detected on the bus while the
  MUSBMHDRC is in Suspend mode". Type 2 logs POWER read after the ISR read INTUSB: a = 0x60 (bit 1 clear), consistent
  with the first sentence. **Inference, unverified:** if on this core that read also ends the core's suspend state,
  resume signalling never arrives "while in Suspend mode" and RESUME can never fire, which matches 37 = 0 everywhere.
  Linux reads IntrUSB the same way on every MUSB platform and gets RESUME there, so it is not generic MUSB behaviour; an
  Allwinner difference is possible. Only a bus trace or the log of a real resume (bench plan B / C below) decides.
- The frame number (FRNUM, 11 bits) only changes with received SOFs: frozen while suspended, moving again once the host
  resumes (or after a reset + enumeration). That makes it a safe resume detector.
- What "17 suspends, 0 resumes" can mean: every suspend after the first needs the bus to have become active again, so
  between them the host either (a) resumed and the core resumed silently (0.2.44+ logs type 3 with a bit 7: count them
  in the 10:20 log), (b) resumed, the device failed to come back and Windows reset the port (type 1 a = 1, b = suspend
  length, often 65535; counter 35 grows with 36), or (c) reset the port for another reason (type 1 a = 1 with b of a few
  ms: the HS reset's SE0 is first seen as 3 ms idle). The end state (device not listed, not even unknown) means the host
  stopped talking to the port; the only way back without a replug is a connect change, hence the soft reconnect. **The
  decoded 10:20 GET_EVENT_LOG was not available when this was written**; read it against (a)-(c) and check whether
  the watch's rules would have fired (below).

**What 0.2.49 does** (pc_usbw.h has the full rationale; protocol row 53)
- `pc_usbw` (pure C, no registers) holds the suspend state; `pc_transport.c` feeds it SUSPEND / RESUME / RESET /
  DISCONNECT / SET_CONFIGURATION / EP0 setup / EP2 OUT / DTR up, and `pc_usb_watch_tick` runs it from the 1 ms timer
  interrupt (`pc_glue_poll_isr`, before `pc_ready`, also during modal menus). IRQs do not nest, so the ISR-side state
  needs no locking; the tick only touches POWER (byte RMW), FRNUM and, via `pc_usb_isr_link`, EP2 (EPIND saved).
- Inferred resume: frame number != the one at the suspend, or an EP0 setup / EP2 OUT packet while suspended. Acts like
  RESUME (EP2 OUT unmasked if configured, `usb_connect` = 1, DTR reads as stored again), counter 60, type 3 a = 0x80 |
  cause << 5 | POWER bits 4-0. Cannot fire during a suspend: no SOF and no packet exists on a suspended bus.
- Soft reconnect: POWER SoftConn off (D+ pull-up released, link reset like a bus reset, type 20), 1000 ms, on. Windows
  then sees unplug + plug and enumerates afresh (same descriptors and serial, same COM port). Rules, both only while the
  frame number stayed frozen since the last suspend:
  - storm: >= 3 suspends since the last proof of life (SET_CONFIGURATION, RESUME, inferred resume), then 10 s quiet,
    doubling per storm reconnect without a SET_CONFIGURATION (cap 640 s). A plug-in bounce (six suspends in 1 ms, then
    the host enumerates) and the port-reset lead-in (one suspend, then RESET) never get there: SET_CONFIGURATION clears
    the strikes. A charger-only cable costs one reconnect (the reconnect clears the strikes).
  - long: suspended 15 min after a SET_CONFIGURATION; once per enumeration; 15 min x 2^n where n counts long
    reconnects since the last DTR 0 -> 1 (cap 8 h). Cannot be told apart from a legitimate long suspend (PC asleep, or
    Windows selective suspend of a closed idle port): then it costs one re-enumeration per period, with the port closed.
    It cannot fire on an open port while the PC is awake (the host polls bulk IN, the bus is never idle).
  - A SoftConn found clear without a watch disconnect (MSC switch, ENTER_FEL) stops the watch; `usb_device_init`
    (`pc_transport_reset`) re-initialises it, keeping counters 60-61.
- CDC ISR: SUSPEND is handled before RESUME when both are pending (1.00o5 did RESUME first and stayed suspended).

**Risks** (inference): (1) a reconnect while the PC sleeps could wake it if Windows arms wake-on-connect for the root
port (modern standby); signature: the PC wakes about 15 min after going to sleep with the port closed. Then raise
PC_USBW_LONG_MS or drop the long rule and rely on the storm rule. (2) Windows' "device connected" sound at each
reconnect while idle with selective suspend on (15, 30, 60 ... min). (3) The storm rule misses a host that gives up
after fewer than 3 suspends without having enumerated the device since the last reconnect (long rule disarmed);
lower PC_USBW_STORM_SUSPENDS if the log shows that.

**Also found:** docs/agent-measurement-guide.md section 8 and the fnirsi-scope skill gave the selective-suspend
setting as 48e6b7a6-50f5-4782-a5d4-53bb**50f7e206**; the right GUID is ...-53bb**8f07e226** (soak.py, section 11).
If the maintainer used the guide's command on 2026-10-18, the setting never changed; `fnirsi1013d-soak` prints the real state.

**Bench plan** (bench session + maintainer; FEL load only; record `powercfg /query SCHEME_CURRENT
2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226` before and restore it after)
0. Load: `sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000` (sha256 90f460d6...). Expect
   `#VER fw=0.2.49,proto=1.14`, GET_LINK_STATS n = 62 with 60 = 61 = 0, the boot events as on 0.2.44 (type 2, type 1
   a = 1 b ~ 130 ms, type 7) and no type 20. One `fnirsi1013d-bench` pass: as 0.2.48 (no new tx_aborts, no resets).
1. Selective suspend on, deliberately, PC kept awake (sleep timeout never; screen off allowed):
   `powercfg /setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 1`,
   the same with `/setdcvalueindex`, then `powercfg /setactive SCHEME_CURRENT`. Run
   `fnirsi1013d-soak --mode reopen --idle 1800 --rounds 3`. Per round, expect one of:
   A. no host suspend (36 unchanged, no type 2): selective suspend does not reach usbser's idle device; go to 2.
   B. suspend then a silent resume at the reopen: type 2, then type 3 with a bit 7 and bits 6-5 = 0 (SOFs) or 1 (EP0),
      counter 60 +1, 37 +0, 35 +0, reopen fine without a bus reset. Resume works electrically; only the interrupt is
      missing (that is all 0.2.44 lacked, apart from the modal-menu gap).
   B'. the same with a bit 7 clear and 37 +1: RESUME fires after all (contradicts every earlier bench; report it).
   C. resume fails, Windows resets: type 2, type 1 a = 1 b = 65535 (or the suspend's ms), type 7, 35 +1, reopen fine
      after 1-3 s. The core cannot come back from suspend; the watch then matters only when Windows gives up.
   D. still suspended 15 min after the suspend: type 20 with a bits 1-0 = 2, b ~ 900 s, 61 +1, then within ~2 s type 1
      a = 0 and type 7 (fresh enumeration), Windows' unplug / plug sound, <port> back; the reopen at the round's end is
      fine and resets the long back-off. At most one type 20 per 30-min round.
2. Two hours idle, port closed, selective suspend still on, PC awake: `fnirsi1013d-soak --mode reopen --idle 7200
   --rounds 1`. If the host suspends: type 20 (reason 2) about 15, 45 and 105 min after the first suspend (15 + 30 + 60),
   each followed by type 1 a = 0 and type 7; reopen fine; 61 <= 3. Then (maintainer's call) once with the PC put to sleep
   for >= 20 min with the port closed: on wake the device comes back by itself (type 1 / 7 after the wake; 61 +0 or +1
   during the sleep). The PC waking by itself ~15 min into the sleep = risk (1).
3. Restore the power setting recorded at the start.
**Failure signatures:** port gone and 61 unchanged = no rule fired: look for a type 3 bit 7 right before (frames moved,
so the host was alive and the loss is something else) or a reset with < 3 suspends and no SET_CONFIGURATION after it
(risk 3). Type 20 reason 1 repeating = the host keeps failing enumeration: read the type 5 / 6 setups between them. A
type 20 while a port was open, or counter 61 rising in a normal bench session = the frame check is wrong (report the
type 2 / 3 / 20 sequence). Counter 49 > 0 = restart: map with the 0.2.49 ELF (section 10). After any failure: replug,
then GET_EVENT_LOG (with the bus ring) and GET_LINK_STATS before anything else.

**Next** (for whoever picks this up): decode the 10:20 log against (a)-(c) above and against the two rules; if it is
(a) only, 0.2.49 already fixes the session side and the loss needs another explanation; if (b) / (c), the soft
reconnect is the recovery, and the resume path itself remains a candidate for a bus trace (a USB analyser, or Linux
usbmon on the host side with `echo auto > power/control` to force a selective suspend of the port).
## 28. fw-cal3: calibration wizard and interleave tracking (fw 0.2.50, protocol 1.15 after the merge; the branch said 1.14 / row 53)

User-facing spec, layout, coordinates and the bench list: README section 6.4. Points for a successor:
- `cal_wiz.c` is the whole step logic: states CW_*, button events, actions the glue runs (`cw_take`: ZERO, PREPARE,
  GAIN_LEVEL, GAIN_MEASURE, COMMIT, DISCARD) and results fed back (`cw_zero_result`, `cw_level_result`,
  `cw_gain_result`). Change behaviour there, with a test in `test_cal_wizard.c`; `cal_wiz_ui.c` only draws and runs.
- The zero / gain measurement helpers (`cal_channel`, `cal_gain_check` + `cal_gain_measure`, the split of the old
  `cal_gain_channel`) live static in `cal_wiz_ui.c`; `scope_do_baseline_calibration`, `scope_do_gain_calibration`,
  `calibration_fail_reason`, `calibrationopen` 1 and 10 and their menu.c boxes are gone. `calibrationopen` 2..9 (the
  1.00o5 input-divider calibration) is unchanged and is entered after Done while `calibrationfail` is set.
- Measurements batch: `cal_begin` before the first measurement action of a touch, `cal_end` after the last
  (channels, sample rate, trigger mode, generator after a gain action). The trigger mode is now restored (the old code
  left Auto) and the channels' dcoffset is not zeroed any more.
- `cal_wizard_committed()` (weak, empty, scope_functions.c) runs after Done wrote something: the calibration v4
  tracker reset belongs there.
- Skip in step 1 needs a block bound to both channels (`acal_bound`), so step 2 always has zeros; a channel skipped in
  step 2 keeps what the working copy has (carried gains with flag KEPT after new zeros, or the block's own gains).
- Screen: saved to `displaybuffer4` (was unused), the view drawn in `displaybuffertmp` and copied.

**Calibration v4 (interleave tracking), as built.** Commits on `fw-cal3` (from d867610): 5f6bc0d (protocol row 53 on the branch, row 54 / 1.15 on main,
0.2.50, spec first), 7cfc7f7 (tracker wiring, acal_cm pure parts, GET_CAL 288, test_session), 93087b2 (class probe
scheduler, test_acal_track (j)), 4cd60f7 (diagnostics lines), e871c50 (README 6.5, CHANGELOG, task queue), then this
handoff. Image from e871c50: `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 502,784 B, sha256
1b55efc2636f488a41e15a447a0ab99a79a287d4d2972657b7b55e2f263d601c; not pinned in backups/. **Nothing benched.** Host
tests: test_session 68981 / 68979 (debug), test_acal_track 577, test_adc_cal 59717, test_cal_wizard 1076, all suites
green, check_vectors OK (81 frames, 163 decim, adc_cal_v3). Warnings (clean builds, sorted lists identical to d867610):
GCC 10 36, GCC 13 31, PC_DEBUG 31; acal_track.c / acal_live.c clean with -Wextra.

- Files: `acal_track.c/.h` (pure: estimator, solve, `acal_track_apply`, and the class-probe parts `acal_cm_probe`,
  `acal_cm_apply`, `acal_cm_mean_q8`, `acal_v4_apply` = cm then split), `acal_live.c/.h` (target: `acal_trk[2]`,
  `acal_cms[2]`, the hooks, `cal_wizard_committed` now defined here, the probe scheduler behind `PC_CAL_CLASS_PROBE`,
  default 1). Hooks: `fpga_channel_correction` calls `acal_live_apply` (so the readout, the 16-bit trigger level in
  pc_dec_apply and the diagnostics all see v4), `fpga_prepare_correction` calls `acal_live_prepared` (remembers class and
  applied d / e), the end of `fpga_read_sample_data` calls `acal_live_record(settings, wide)` (wide = `pc_decim_src` was
  set after `pc_glue_decim_read`). Reset: `scope_load_input_calibration_data` after the 708 unpack, and Done.
  `pc_hw_channel` carries `trk` / `cm` snapshots; `pc_channel_corr` (pc_cmds.c) applies `acal_v4_apply`, so GET_CAL 84
  and 92..107 are "what the next readout applies" (zero_q8 users are unaffected: v4 keeps 0 V at T). mock_hal: set
  `mock.ch[i].trk` / `.cm`.
- Wired, host-tested: the estimator and fit (test_acal_track a-i: 1.7-code step under 0.1 code in 36.8 s, gain ramp,
  bow q within 2 %, noise floor d 0.0012-0.004 code, 40 MHz rejection, rate gate, inversion), the class-probe check / EMA
  / apply (j), the GET_CAL tail and the gated / full / 16-bit-arm / unbound cases (test_session).
- Wired, untested on hardware: the readout hooks (record counts, `last_pairs` on real traces, cost per record about
  50 us), the class probe as a whole (probe_run: raw scratch channel with its own 1000-byte buffer, 8 / 64 / 8
  conversions, then sample counts, trigger mode, `fpga_set_sample_rate`, a raw host divider written back, time base,
  trigger mode restored; the UI's next arm re-applies its filter and settles because pc_dec_apply sees the K change),
  the scheduler gate (`pc_glue_ui_service` passes display mode < 4 and no host ARMED / TRIGGERED / ui_hold;
  `acal_live_service` adds runstate, `display_data_done`, touchstate, roll / viewer, channel on + bound, last record RMS
  <= 2 codes). Verified by reading: `pc_glue_ui_service` is only called from the main loop (fnirsi_1013d_scope.c), never
  inside a modal menu; `display_data_done` = 1 means the UI's last conversion was read and the arm comes later in the
  same pass. Watch on the bench: pass_ms spikes every 30 s (about 100-170 ms, more with deep memory at 2 MSa/s),
  the ring no longer holds the last UI capture right after a probe (as after auto setup), no trace glitch.
- Missing / open: the bow correction (q only reported; design section 6: per-core 256-entry table, decide from the
  bench q); a gated-class A - B tracker for 03:48-type steps (that step is not tracked: the gated class is the
  reference; queued only if it recurs); mcp/ parse of GET_CAL 1.15 (tail 256..287, flags, the "never" ages) and its
  volts model (bytes 92..107 / 84 already include v4, so a host that uses them needs nothing else); README 6.5 bench.
  GET_CAL is now 288 bytes, above the 256-byte duplicate-cache limit (protocol row 5): a retried GET_CAL runs again
  (harmless, it only reads).
- Bench session's bench facts this rests on (docs/hardware-status.md 2026-10-19): full-class common mode +0.7-0.8 code in
  2 h while the gated class stays (design section 8); the FIR path benched at 78-83 dB alias rejection and CIC2+FIR
  becomes the default, so gain16 / corr16 must keep working with the FIR gain (N/2)^K: they do, cm enters corr16 as an
  additive 1/16-code term after `pc_decim_to_code` (code = out x scale / 2^24 + corr16 / 16), independent of N, K and
  the FIR; the 03:48-type gated step is not tracked (above).
- Design-doc change made while building (interleave-tracking.md section 2): q identifiable needs w >= 65536 codes^4
  **and** w >= v^2 / 4 (the first draft's 256 codes^4 let the noise inside the post square's two levels pull q to 0
  and bias e; test_acal_track (c)).
- Gotchas: the main menu reorder 7c62586 put Spectrum / Phosphor first; the system settings fold-out stays anchored at
  y 46 while its row moved to y 164 (maintainer to judge). Pre-existing, unfixed: in `fpga_read_adc_data` the core-B rail
  rule reads `settings->buffer[1]` (core A's code) after it was stored as 255 - code on an inverted channel, so on
  inverted channels the rule compares mirrored codes near the rails (fix: un-invert `first` or apply invert after the
  pair). The tracker un-inverts the buffer itself. Diagnostics CH2 block moved from y 205 to 225 for the two v4 lines.
  Edit scripts and builds: no git together with heredocs in one shell command; Python edits with latin-1 / newline='';
  clean warning lists need clean build dirs (the incremental `build.sh` count is only the recompiled files).


## 29. fw-ui1: UX makeover step 1 (fw 0.2.51, protocol 1.15 unchanged)

Branch `fw-ui1` from main d8d5a62. Plan: `docs/plans/ux-makeover.md` section 7 step 1 plus the card work; rules
`docs/ui-guidelines.md`; user-facing description README section 5 "Screens". Commits, one topic each: 33cc330
(`ui_fmt.c/.h`, `ui_menu.c/.h`, `host_tests/test_ui.c`; `measurement_names` moved from variables.c to ui_fmt.c),
04b6f2c (main menu), 1027f90 (USB drive screen), 3c66d85 (top-bar version), 4d74f82 (Display row, X-Y out of System
settings), 79b6b00 (phosphor strip, warnings), 7ff6c68 (spectrum strip, AUTO SET), 2f1ab97 (cards, Measures menu names,
defaults), 8f5bf16 (0.2.51, CHANGELOG, README), then this handoff. Image from 8f5bf16: `firmware/scope/build-gcc10-pc/
fnirsi_1013d.bin` in the worktree, 504,832 B, sha256 38580def064809e1c290a215f5720fd21e07086eb99420895ee3285424599e66,
ELF 1cf4e7a024b01a1210d77f2f7ef142ea3675ffb71f6a6c0872c150858fc0055f; not pinned in backups/. **Nothing benched.**
Host tests all green (test_ui 1513 checks, test_session 68983 / 68981, check_vectors OK, test-arm identical).
Warnings (clean builds, sorted line-number-free lists identical to d8d5a62): GCC 13 31, GCC 10 36; the new files are
clean with the host tests' -Wall -Wextra -Werror. No protocol change: GET_STATUS byte 43 keeps its meaning (4 =
phosphor view, 1-3 = spectrum views). No settings words used (211..223 stay free; fw-readers claims 224..255).

Points for a successor:
- **Main menu** (`ui_menu.h`): the row table (labels, `UI_MM_*` order), `ui_mm_row_at` (one contiguous 59 px hit area
  per row from y 46, x 2..148; the drawn 131 x 35 button sits inside, test_ui checks every y) and `ui_mm_choose` (the
  mode rule: Spectrum always offered, `roll_off` + `warn_roll` in roll mode, Scope restores the phosphor variant, Roll
  never switched back on). menu.c `scope_main_menu_item(row, mode)` draws a row (Settings highlights yellow while its
  fold-out is open, the others light grey while touched); the Scope icon (a square wave) and the spectrum bars are
  drawn with lines. statemachine.c `handle_main_menu_touch` dispatches on the row; the mode rows go to
  `PC_MAIN_MENU_MODE(row)` = `pc_glue_main_menu_mode` (pc_hal_scope.c), which carries out the action: roll off through
  `pc_hal_timebase_write` with time base 20 (what the acquisition menu's Roll box does), then the phosphor / spectrum
  switches, then `spectrum_note_roll_off` (spectrum.c warning "Roll off for the spectrum", 5 s). MAIN_MENU_HEIGHT is a
  constant 413 again; `MAIN_MENU_DY`, `pc_glue_main_menu_height/dy/row6/items/touch` are gone. Bode, when it ships,
  is a row after Spectrum analyser: add it to the table (the column then needs 472 px: shrink the pitch or the top).
- **Display variant** (`pc_hal_scope.c`, `pc_glue_acq_display_draw/tap`, hooks `PC_ACQ_DISPLAY_DRAW/TAP`): Normal /
  Phosphor / X-Y in a row under the acquisition menu (ACQ_MENU_HEIGHT 432, 1.00o5's part ACQ_MENU_BODY_H 386, boxes
  84 x 36 at ACQ_DISPLAY_Y 390; 36 px is under the guideline's 44, as the neighbouring ACQ boxes; temporary). X-Y =
  `xymodedisplay` (saved as before), Phosphor = `phosphor_set_active` (not saved, as before). `pc_phos_wanted` keeps the
  variant while the spectrum shows. Lost until step 2: phosphor drawing in X-Y (phosphor.c still supports it; the
  fold-out's Phosphor switch brings it back). Step 2 replaces the row by the CTRL page's Display fold-out: delete the
  row, restore ACQ_MENU_HEIGHT 386, keep `pc_phos_wanted` / `pc_display_variant` as the fold-out's model.
- **System settings fold-out**: X-Y row removed, Notification confirmation and RTC settings one row up (draw y and
  touch ranges in menu.c / statemachine.c), SYSTEM_MENU_HEIGHT_FULL 354; the fold-out's touch area now ends at its
  box (was y 470). The fold-out stays anchored at y 46 while its main-menu row is the fifth (maintainer to judge; step 3
  rebuilds it).
- **Strips**: phosphor F_COUNT 3 (no Scope), warnings via `phos_draw_warning` at (366 centre, y 69) in font_2 orange,
  wfm/s stays at the strip's right end until step 2 moves it. Spectrum F_COUNT 8 (no Auto, no Scope; FIELD_W 90).
  `spectrum_autoset` (AUTO SET in the view: start / cancel) and `spectrum_auto_running` behind `PC_AUTOSET()` /
  `PC_AUTOSET_RUNNING()` in statemachine.c / menu.c `scope_auto_set_button`, which reads "Auto..." (font_2) while it
  runs; `auto_shown` in spectrum.c redraws the button when Auto ends (end of `spectrum_display_body`) and
  `spectrum_set_active` ends a running Auto.
- **Cards** (`scope_display_channel_measurements`, scope_functions.c): `ui_card_chip_w` = name width + 2 x 3 px in
  font_2, the chip is a rounded rect squared on its right by a 2 px fill so only the outer left corners are round.
  Values: `ui_fmt_sig4(value_in_hundredths, prefix_index, unit)` rounds to four significant digits, never adds digits
  the input lacks, picks the prefix; `ui_fmt_sig4_fixed` for % (duty in hundredths of a percent from Q16 times).
  `scope_display_time` / `scope_display_duty` replace the four near-identical 1.00o5 bodies. `scope_print_value`
  (3 digits) stays for the cursor readouts. "no period" (`UI_MEAS_NO_PERIOD`) for the timing values without three
  crossings. Defaults: `scope_reset_config_data` sets Freq, Vpp, Vavg per channel (also on DEFAULT). The cards show
  in table order (Vavg, Vpp, Freq), not the plan's "Freq, Vpp, Vavg": reordering needs a display order separate from
  the saved index (later, with the per-mode lists of plan section 3a).
- Edit tooling that worked here: the worktree isolation refuses shell heredocs and compound commands, so edits go
  through a Python script in the scratchpad (`ed.py <file> <spec.py>`, latin-1, newline='', unique-match EDITS and
  RANGES) run from PowerShell, and builds through scripts run with `wsl -d <distro> -- bash <script>` (WSL /tmp did not
  survive between calls; keep logs and the base build under $HOME). The Write tool strips trailing blanks, so never
  anchor an edit on a 1.00o5 blank line.

**Bench walk-through (maintainer, bench session on <port>)**
1. FEL-load the image, `#VER` fw=0.2.51 proto=1.15. Top bar: no "v0.2.51" text at the right (the diagnostics view
   still shows the identity line).
2. Menu: seven rows Scope, Spectrum analyser, Saved pictures, Saved waveforms, Settings, Diagnostics, USB drive, each
   with an icon, no row marked as current. Tap every row near its top and bottom edge (just below / above the black
   separator lines): each opens its own function (Settings: the fold-out; Saved pictures / waveforms: the file
   browsers; Diagnostics: the diagnostics screen; USB drive: step 4); a tap on a separator line belongs to the row above.
3. Settings fold-out: no X-Y row; Notification confirmation toggles in the fourth slot, RTC settings (with a DS3231)
   in the fifth; the box ends under them.
4. USB drive: title "USB drive", the scope and PC picture, the three lines, an Exit button bottom right. With the PC
   showing the drive, Exit gives "Not allowed!"; eject on the PC, Exit returns to the scope and <port> comes back.
5. Spectrum: Menu > Spectrum analyser shows the spectrum (strip: Channel, FFT length, Window, Scale, Ref level,
   Average, Reset, View; no Auto, no Scope). AUTO SET: reads "Auto..." while running, the view settles; tap AUTO SET
   during a run: it stops and reads AUTO SET again. Menu > Scope returns to the traces.
6. Roll: ACQ menu > Roll mode on (slow time base), then Menu > Spectrum analyser: Roll goes off, the spectrum shows
   with "Roll off for the spectrum" orange at the top middle for about 5 s. Menu > Scope: the scope at time base 20
   (the short time base the Roll box uses), Roll still off.
7. ACQ menu: a Display row at the bottom (Normal orange). Phosphor: close the menu, the graded picture with the strip
   Persistence / Palette / Clear and wfm/s at the right (no Scope field). Unplug the probe or set a too-small V/div:
   "CH1 overrange: increase V/div" appears top middle, not in the strip. X-Y: the Lissajous (plain, no phosphor),
   Normal back. Menu > Spectrum analyser from Phosphor, then Menu > Scope: Phosphor again.
8. Cards: Measures, tick Vmax, Vmin, Vrms, Vp, Freq, Period, +Width, -Width, +Duty, -Duty on CH1 (1 kHz cal signal):
   names in the yellow chip with no notch at the join and square right corners, values like "1.000kHz", "1.000ms",
   "500.0us", "50.00%", nothing overflowing the 118 px card. The Measures menu shows the same names centred. Generator
   off (no edges): the timing cards read "no period". After loading the default settings (the DEFAULT button): CH1 and CH2 show Vavg,
   Vpp, Freq.
9. Host: GET_STATUS byte 43 = 0 in Normal and X-Y, 4 in Phosphor, 1-3 in the spectrum views (unchanged meanings).
Bug indicators: a main-menu tap opening the neighbouring row; the ACQ menu's Display row not reacting or the Trace
boxes reacting to it; AUTO SET stuck on "Auto..."; a card value running past the card edge; the System settings
fold-out leaving a stripe of the old X-Y row; roll mode back on after Scope.
## 30. fw-readers: diagnostics readers (fw 0.2.52, protocol 1.16, row 55)

Branch `fw-readers` from main d8d5a62 (0.2.50): 8614032 (protocol row 55, spec first), b830434 (code, host tests,
vectors), c8f874b (PC_DEBUG restart test, GET_HEALTH flag 0x80), then docs and this handoff. Image from c8f874b (GCC 10):
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 528,896 B, sha256
e09683929d643f850012f3dbd354fd9ab04aea7ab17796dc824a56c5e5382851 (identity c8f874b; a build of a later commit
differs only in the identity record). PC_DEBUG image (`build-dbg`, for bench check 3) sha256
fdc3b1b99ebe8ea16d0c82ca8cc812c9739f7ec3003444a3767835f46616d72b. Not pinned in backups/. **Nothing benched.** Host tests all
green (test_diag 462, test_session 69009 / 69007, check_vectors incl. `diag_layouts`); warnings GCC 10 36, GCC 13 31,
PC_DEBUG 31 (counts as on 0.2.50, none in the new files). Images: GCC 10 +24.5 KB over 0.2.50 (pc_report.c is built
`#pragma GCC optimize ("Os")`: at -O3 the inlined text helpers made it 120 KB).

**Files.** `pc_life.c/.h` (pure: settings words 224..255 pack/unpack, boot logic, 1.00o5 checksum), `pc_ident.c/.h`
(identity record type, card reader with cache, legacy tail, `#VER` text scanner; host-buildable through pc_hal_sd_read),
`pc_report.c/.h` (text builder, the report from the protocol bodies; host-buildable), `pc_diag.c` (target: life state,
boot write, dirty check, provenance stamping, CP15, FatFs report file, `pc_glue_*` entry points), `adc_cal.c/.h`
(`acal_prov` pack/unpack/what), `build_id.c` (one object `pc_image_ident`; `pc_fw_build` / `pc_build_time` are now
macros in pc_cmds.h), pc_transport.c (`pc_hal_usb_state`), pc_cmds.c (GET_HEALTH, SD_INFO 112, GET_CAL 356, SAVE_REPORT;
`pc_link_counters`, `pc_health_body`, `pc_cal_body`, `pc_sd_info_body` exported). Hooks (pc_hooks.h): PC_SETTINGS_STORE
and PC_SETTINGS_SAVED in scope_functions.c's settings save, PC_CAL_LOADED / PC_CAL_SAVE in the sector 708 load / save,
PC_POWER_OFF now `pc_glue_power_off`. pc_hal_scope.c: `pc_diag_boot` after `pc_restart_check` in `pc_glue_init`,
`pc_diag_service` in `pc_glue_ui_service`, accessors `pc_glue_session`, `pc_glue_rtc_present`.

**Decisions.**
- One new command for the live state (GET_HEALTH, 96 bytes, F_BUSY: works while Diagnostics is modal) instead of slots
  spread over GET_INFO / GET_STATUS / GET_LINK_STATS; GET_LINK_STATS stays n = 62.
- Provenance is a separate trailer after the v3 block, not a v3 -> v4 block bump: 0.2.45-0.2.51 keep reading the card
  (a bump would have made the maintainer's fallback images uncalibrated). Stamped by comparing the block on save with the
  one loaded / last saved (`acal_prov_what`), so cal_wiz_ui.c needed no change and a DC-shift trim or volts calibration
  keeps the provenance. Per block, not per channel; `what` says which parts the last calibration wrote.
- Power-on count = cold boots (power-on or FEL load); a warm restart is the same power-on. Operating minutes = stored
  base + uptime; minutes since the last save are lost when a power-on ends without one (crash, flat battery).
- The boot write of sector 709 (only when it loaded valid) makes the count, the session-open flag and the restart
  record survive a crash plus power cycle. One extra SD write per boot; the 1.00o5 words are written exactly as loaded.
- "Changed since the last save" rebuilds the buffer as the save would (`scope_save_config_data`, harmless: the
  power-off save does the same first) once per second in the main loop and compares a CRC of words 2..223 with the
  last load / save. Stale while a menu is modal (byte 40 gives the age).
- Card identity: fw 0.2.52+ ends with a 36-byte record (magic 'FWID' '1013'); 0.2.47-0.2.51 end with GCC 10's
  time[6], pad[2], build (verified on the 0.2.48, 0.2.49, 0.2.50 and 0.2.50-nommu backups with `test_diag --image`);
  older images only by the `#OK,FNIRSI-1013D-AI,fw=` text (0.2.11, 0.2.25, 0.2.46 checked). Full scan costs one read
  of the image (as VERIFY); cached until SD_WRITE_SECTORS reaches sector 750.
- USB "disconnected": VBUS is forced valid (ISCR), so a pulled cable is indistinguishable from a suspend once
  configured; state 2 "not configured" is the defined no-PC state.

**For the UI agent (Diagnostics pages, later generation).** `const pc_card_id *pc_glue_card_ident(void)` (pc_ident.h;
first call reads the card, MAIN context), `int pc_glue_save_report(char *name, uint32 name_max)` (0 or FRESULT, MAIN),
`void pc_glue_health(uint8 *body96)` (the GET_HEALTH body; decode per row 55), `pc_cal_body(ch, buf356)` for the
calibration page incl. provenance. Firmware tile: `pc_image_ident` (running) vs `pc_glue_card_ident()` (`same`).

**Not done / notes for the next agent.**
- Post-based calibration check (diagnostics-review section 4 item 7): not built. fw-cal3's class probe covers the
  full-vs-gated common mode only; a check at the post (both levels, both classes, A - B residual and gain agreement
  against the provisional limits of review finding 6) needs the generator and its own modal step.
- "Undo last calibration" storage: sector 711 is, by inference, unused by the Atlan boot chain (README section 3 lists
  708-710 and 750; the updater's update.h defines 16, 80, 708-710, 750); verify against the updater 0.02v binary and
  the v0.8 bootloader before writing there. The 708 provenance trailer leaves 488..511 free (24 bytes).
- FPGA build identity: the fpga agent's (0x2A+ taken by segmented capture on 0x1536).
- mcp: parse GET_HEALTH, SD_INFO 64..111 (+ flag 0x02), GET_CAL 288..355, SAVE_REPORT; MCP tool returning the report
  as JSON (review finding 8).

**Bench checks** (bench session + maintainer; FEL load, the card keeps 0.2.25)
1. Load, `#VER fw=0.2.52,proto=1.16`. GET_HEALTH: byte 4 = 3 (configured), byte 5 bit 0 = 1 (high speed), bit 3 = 1
   with the port open; 16 = 0x0005107D; 32 bit 3 (709 valid) and bit 1 (boot write ok); 20 power_on = 1 on the first
   boot with 0.2.52 (no life record yet), 33 last_end = 0. SD_INFO flags 0x02: 64 = 3, version "0.2.25", proto 1.5,
   build 0, 65 = 0, 100 = scan ms (expect well under 1 s). The card is untouched except sector 709 (boot write).
2. Settings: GET_HEALTH 34 bit 0 = 0; change V/div on the scope; within ~1 s bit 0 = 1. Power off with the button and
   on again (the card boots 0.2.25, which keeps words 224..255), FEL-load 0.2.52 again: power_on = 2, 33 = 1 (saved at
   power-off). Pull no battery for this. With 0.2.25 in between, 0.2.25's own power-off save also keeps the record.
3. Forced restart: load the PC_DEBUG image (`make PC_DEBUG=1 BUILD=build-dbg`, #VER 0.2.52, caps DEBUG_RAW) and send
   GET_HEALTH with flag 0x80: after the answer it jumps to the image entry (type 0 restart). After the reboot (about
   2 s, USB re-enumerates): GET_LINK_STATS 49 = 1, 50 = 0; GET_HEALTH 44 = 3 (stored, this power-on), 45 = 0, 46 = 1,
   64 = the LR at entry (in `pc_hal_restart_test`'s caller chain, map with the ELF), power_on unchanged, 33 = 2.
   Power off with the button, on, FEL-load 0.2.52 again: 44 = 1 (earlier power-on), 48 = the old power_on. Clear with
   GET_HEALTH flag 0x01, power off/on (or any save): 44 = 0. ENTER_FEL is not a restart (it clears the DDR record).
4. Calibration: run the wizard (Done). GET_CAL 288 = 1, 292..347: 'APRV', build b8304340 (or the loaded build), FPGA
   word, what = 0x0F after zeros + both gains, uptime_ms at Done, power_on; 348 = 0 and 352 = 0 right after. A
   DC-shift "End" keeps the same trailer; `fnirsi1013d-sd read 708 1` shows it at 432..487.
5. SAVE_REPORT: answer `reports/report-0000N-00.txt`; open the file on a PC (card in a reader, or USB drive mode):
   identity lines, "Card image (sector 750): 0.2.25 ...", counters, event log in words, calibration in words.
6. Regression: `fnirsi1013d-bench` and the soak as on 0.2.50; pass_ms unchanged (the 1 Hz dirty check rebuilds 256
   words).



## 31. fw-ui3: UX makeover step 3, Settings (fw 0.2.55 provisional, protocol 1.16 unchanged)

Branch from main 9cc599c (fw 0.2.51), merged with origin/main (0.2.52 readers, 0.2.54 touch) in e01f775. Plan:
`docs/plans/ux-makeover.md` section 5 (Settings), 6b (dialog buttons), 6c (full-screen look); rules
`docs/ui-guidelines.md`; user-facing description README section 5 "Settings: fold-out, Settings screen, confirmations".
Commits, one topic each: a4664ae (`ui_settings.c/.h` pure tables + `host_tests/test_ui.c` tests), 66a3a70 (fold-out
trim, `ui_settings_ui.c` Settings screen, 1.00o5 Other settings menu removed, reset without boot byte), 24743a7 (delete
confirmation), 3e2fec2 (max-light icon), 07bd90d (0.2.55, CHANGELOG, README), e01f775 (merge), then this handoff.
Image from e01f775: `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 536,576 B, sha256
6f1000986ea64d7a7b8ed13a22c13dc9995a8e9380aeb3c36dfd69eb968d68aa, ELF
28d8054ba964ead137bee380414347f19cf2592ec11a905874409942beb17c48 (nommu bin b7cd9404...). **Nothing benched.** Host
tests green (test_ui 1,431,758 checks: every pixel of the Settings, time-editor and dialog hit areas against the drawn
rectangles; every row value in the 16 px font against its row). Warnings: GCC 10 36, GCC 13 31, line-number-free lists
identical to 9cc599c; the new files are clean with -Wextra. No protocol change, no settings words.

Where things are:
- **Tables** (`ui_settings.h`): fold-out ids `UI_FO_*` and `ui_fo_rows/height/row_top/row_at` (contiguous 59 px rows
  from y 46, Time and date only with a DS3231); Settings rows `ui_set_rows[]` (label, group, kind STEP / TAP / OPEN,
  x, y), groups `ui_set_groups[]`, `ui_set_hit` (a STEP row answers only on its 80 x 44 - / + buttons), `ui_set_value`
  (text + tone: ON green 0x35C26A, OFF grey, WARN orange, NA grey and inert), `ui_boot_next` / `ui_boot_text`, the time
  editor (`ui_time_hit`), the dialog (`ui_dlg_layout`, `ui_dlg_hit`, texts `ui_dlg_restore`, `ui_dlg_delete`).
- **Fold-out** (menu.c `scope_open_system_settings_menu`, `sys_fo_row`; statemachine.c `handle_main_menu_touch`
  switches on `ui_fo_row_at`): `scope_system_settings_filemsg_item` replaces `..._confirmation_item`; the RTC, calibration
  and Other settings items take their y from the table. `SYSTEM_MENU_HEIGHT` = `pc_glue_sys_menu_height` =
  `ui_fo_height`. Other settings closes the fold-out, puts back what was under the main menu (displaybuffer1) and runs
  `ui_settings_run()`, then returns from the menu.
- **Settings screen** (`ui_settings_ui.c`): modal (menus are), saves the screen in displaybuffer4, `gather()` reads the
  variables into `ui_set_state`, actions write the variables directly. Back writes the boot byte (uint8 store at
  0x81BFFC1F, then `sd_card_write(710)`) only when it changed, restores the screen, clears the top-bar clock if it was
  switched off here, redraws the max-light icon if a brightness step left max light, and copies the main-menu rect
  into displaybuffer1 (the menu's caller restores that rect afterwards). After Restore default settings it calls
  `apply_all()` (the FPGA setup of `main`) and `scope_setup_main_screen()` instead of restoring the old picture.
- **Dialog** `ui_confirm(text, restore)`: saves/restores `displaydata`; with restore=1 the box area goes through
  displaybuffer4 (so never call it with restore=1 while the Settings screen or the wizard owns displaybuffer4).
  `handle_confirm_delete` is now one line on top of it.
- **Removed**: 1.00o5's Other settings menu (`scope_system_settings_open_other_settings_menu` and its nine item
  functions, `othersettingsopen`, `OTHER_MENU_*`, the boot-byte write in `close_open_menus` with its dev-mode rule),
  `SYSTEM_MENU_HEIGHT_FULL`, the boot-byte write in `scope_reset_config_data`. `pc_glue_settings_defaults` sets
  `USB_CH340 = 1` under PC_FORCE_CDC (the reset used to set MSC, and the USB drive screen's exit re-inits from it).

Every removed control and where it went (guidelines section 5):

| was | now |
|---|---|
| Fold-out: Notification confirmation (slide toggle) | Fold-out: File message timeout, chip "tap" / "0.7 s"; Settings > Controls |
| Fold-out: RTC settings | Fold-out: Time and date (same editor); Settings > Clock |
| Fold-out: Baseline calibration | **interim**, still in the fold-out above Other settings; fw-ui4 moves it to Diagnostics > Calibrate and deletes the row (`UI_FO_CAL`, `scope_system_settings_calibration_item`, the `calibrationopen` 2-9 volts boxes stay with it) |
| Other settings: Boot menu + Default start | Settings > Start-up > At power-on (0 / 4 / 5); FEL not offered |
| Other settings: New autosetup | Settings > Controls > Auto setup |
| Other settings: Lock move cursors | Settings > Controls > Cursors |
| Other settings: DC shift calibration | **interim** Settings > "Calibration (moves to Diagnostics)" > DC shift trim; fw-ui4 moves it to the Calibration page and deletes `UI_SET_DCSHIFT` and the group (the test's geometry checks follow the table) |
| Other settings: USB-Serial mode | removed (PC_FORCE_CDC) |
| Other settings: Dev mode | removed from the UI; `dev_mode` still loaded / saved and still drives the trigger-address overlay and `scope_test_TP`; fw-ui4's Developer page gets "Debug overlay" for it |
| Other settings: Add a tag to BMP | Settings > Files > Stamp screenshots with (time / file name / nothing) |
| Other settings: Load Ref on start | Settings > Files > Reference traces at power-on |
| "Confirm to delete?" NO / YES | the dialog: Cancel left, Delete right (red), 160 x 44 px, 16 px apart |
| DEFAULT button (diagnostics view, no confirmation) | unchanged (fw-ui4's territory); Settings > Restore default settings is the confirmed path. Both now keep the boot byte |

Not done / for a successor:
- The Settings screen is modal, not a main-loop mode (guidelines section 4 asks main-loop only for long-running screens).
- No dimming behind the dialog (the panel library has no blend); the box is drawn over the screen.
- Brightness - / + step 5 per tap, no auto-repeat while held.
- The fold-out stays anchored at y 46 while its main-menu row is the fifth (as in step 1).
- Restore default settings while the spectrum or phosphor view is active rebuilds the scope screen and leaves the view
  switches alone; not tried on the device.
- The full-screen colours (0x0D0D0D rows, 0x0F2447 / 0x1F3A66 buttons, 0x35C26A values) are local defines in
  `ui_settings_ui.c`; fw-ui4's Diagnostics pages should move them to a shared header when they reuse them.

**Bench walk-through (maintainer, bench session on <port>)**
1. FEL-load the image, `#VER` fw=0.2.55 proto=1.16.
2. Menu > Settings: the fold-out shows Screen brightness, Grid brightness, File message timeout (chip "0.7 s" or "tap"),
   Time and date (only with a DS3231), Baseline calibration, Other settings; the box ends under Other settings. Tap
   each row near its top and bottom edge: Screen / Grid open their sliders; File message timeout flips the chip; Time
   and date opens the hours..year editor; Baseline calibration opens the wizard (Cancel returns).
3. Other settings: the Settings screen, black, "Settings" top left, Back top right, Display / Controls / Calibration
   (moves to Diagnostics) on the left, Files / Clock / Start-up on the right, Restore default settings bottom left.
   Values on the right of each row in green (grey for off, amber for DC shift trim on).
4. Screen brightness - / +: the backlight steps by 5 %; Grid brightness - / +: the value steps (the grid shows it after
   Back). A tap on the row outside the buttons does nothing.
5. Tap File message timeout, Cursors, Auto setup, Stamp screenshots with, Reference traces at power-on, DC shift trim:
   each flips its value. Back; Menu > Settings > Other settings: the values are as left. Power-cycle: they persist
   (DC shift trim does not, as before).
6. Clock: "shown" / "not used" (without a module "no clock module", inert, and Time and date inert too). Clock on:
   Time and date shows "20yy-mm-dd hh:mm"; tap it: the editor page, - / + change and write at once; Back returns.
   Clock off: Back to the scope, the top-bar time disappears; clock on: it comes back within a second.
7. At power-on: cycles Open firmware -> Boot menu, then open firmware -> Boot menu, then stock firmware -> Open
   firmware. Leave it on "Boot menu, then open firmware", Back, power-cycle: the boot menu shows with PECO
   preselected. Set "Open firmware": the next power-on goes straight to the open firmware. With the byte at FEL (set
   by the loader tool) the row reads "Boot menu, then FEL" in amber.
8. Restore default settings: the dialog "Restore default settings?" with three lines, Cancel left, Restore right in red;
   taps outside the two buttons do nothing. Cancel: back to the page, nothing changed. Restore: the page shows the
   defaults (brightness 20 %, grid 25 %, clock not used); Back: the scope screen rebuilt (CH1 on, defaults), the boot
   menu choice unchanged at the next power-on, calibration unchanged (diagnostics lines), <port> still there (CDC), and
   USB drive > Exit still returns to CDC.
9. Saved pictures: select a file, Delete: "Delete from the SD card?", "This cannot be undone.", Cancel left, Delete
   right (red), both 44 px tall and 16 px apart; Cancel keeps the file and the browser redraws cleanly; Delete removes
   it. Same in Saved waveforms and in the waveform viewer's Delete wave.
Bug indicators: a fold-out tap opening the neighbouring row; the Settings screen leaving parts of itself on the scope
after Back; the main-menu area showing an old picture after Restore; a row's value overflowing into its label; the
boot menu appearing (or not) against the At power-on value; a tap between rows or outside the dialog buttons acting;
<port> disappearing after Restore default settings; the delete dialog leaving a box behind in the browser.

## 32. fw-touch1: touch fix batch 1 (fw 0.2.54 provisional, protocol 1.16 unchanged)

Branch from main 9cc599c (fw 0.2.51). Spec: `docs/plans/touch-review.md` sections (b) and (c1); rule
`docs/ui-guidelines.md` section 3 "Touch" (handles drawn on screen are draggable). Commits: 4a3753b (`touch_pick.c/.h`,
`host_tests/test_touch_pick.c`, both Makefiles), 4bec0ff (drawing through `scope_pointer_top`, `scan_for_touch`
handles and pick), ca7372a (`touch_handler` shared delta, `triggerchannel == 1`), d94f09a (traceposition clamps),
f9fcac6 (0.2.54, CHANGELOG, README section 5 "Touch on the scope plot"), then this handoff. Image from f9fcac6:
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 504,832 B, sha256
a4f0c832079364cc2d5f91f04dfd6546b816f0a8ff336138884b7c75e0f72347, ELF
52f9cf983405116adadb4500e55b228655a87f6267ff6f858acdf1c1c4a27a94 (nommu bin eefe9a4e...). **Nothing benched.** Host
tests green (test_touch_pick 538 checks, the rest unchanged). GCC 10 warnings 36, line-number-free list identical to
9cc599c; the new files are clean with the host tests' -Wall -Wextra -Werror. No settings words, no protocol change.

What changed, by defect number of the review:
- **1, 12 (refs)**: `scan_left_handle()` (statemachine.c, above `scan_for_touch`) builds the ten pointers (CH1, CH2,
  ref1..ref8, draw order of `scope_draw_pointers`) and calls the pure `pick_left_handle()`: x 0..43 (`HANDLE_W`), rows
  tip - 22 .. tip + 21 with tip = `scope_pointer_top(position) + POINTER_TIP_DY` (the clamped drawn top + 6), nearest
  switched-on pointer, `<=` so a tie goes to the later-drawn one. Normal display only (`xymodedisplay == 0`). It runs at
  the touch-down point, before the blocking loop; a lift without displacement on a handle returns without the tap
  action; a drag sets `touchstate` to the handle's state alone. The old x < 60 ref block is deleted; the
  `distance_ch_ref_*` globals in variables.c are now unused (left in place to keep the merge with fw-ui2/fw-ui3 small).
- **Drawing**: CH1 / CH2 / ref / trigger level pointers use `scope_pointer_top()` (touch_pick.c) instead of eleven copies
  of the 49..442 clamp. The X-Y CH1 top pointer and the H marker are unchanged.
- **3, 4 (hit maths)**: the zero-line grab calls `pick_channel_line()` (int32, zero row `448 - position`, radius
  `CHANNEL_GRAB_RADIUS` 30 as 1.00o5, only enabled channels, tie to CH2). Its x range starts at `HANDLE_W` (was > 60);
  with volt cursors on it still skips x 310..410 (batch 2).
- **5**: every pick in the plot branch (channels, trigger level, time and volt cursors) reads `previousxtouch` /
  `previousytouch`. The drag maths still measure from that point, so nothing else moved.
- **8**: channel grabs no longer carry `TOUCH_STATE_MOVE_TRIGGER_POINT`; the fallback (no object) still pans the
  trigger point.
- **9, 10**: `touch_handler` computes `touch_drag_delta()` once (Move slow /5, same truncation as before) and, when the
  level follows the channel (`triggerchannel == 0` for CH1, `== 1` for CH2; 2 = EXT never follows), limits it with
  `touch_shared_delta()` (both starts clamped first, so the limits never cross). `change_channel_1_offset`,
  `change_channel_2_offset`, `change_trigger_level_offset` now take the delta (statemachine.h); X-Y passes x for CH1 and
  y for CH2 as before. The snapshot at grab time also tests `== 1` for CH2.
- **4, 19 (writers)**: `scope_do_auto_setup_new` computes the DC adjustment in int32 (`position`) and stores
  `scope_trace_clamp(position)`; `dcoffset` follows from the clamped value as before. `scope_restore_config_data` and
  `scope_restore_setup_from_file` clamp CH1 / CH2 `traceposition`. Not done (not in c1): `scope_do_auto_setup_old`
  still writes 0 / 399, and the new auto setup's two-channel spread (+100 / -100 after the DC block) can leave 398 or
  1..5; both are grabbable now (the pick uses the clamped drawn position) and jump into range on the first drag step.
- Left for batch 2 / 3 as briefed: trace-body grab and radius 40, 8 px dead band, tap zones (the maintainer keeps the hidden
  V/div bands), hidden objects (the trigger level hit while its pointer is hidden, X-Y moving disabled channels),
  locked cursors, the blocking loop, the second GT911 reader, long-mode presets per step, spectrum pan.
- Merge note: fw-ui2 edits the right-bar handlers in statemachine.c and fw-ui3 the settings fold-out; this branch only
  touches `touch_handler`, `scan_for_touch`'s plot branch, the three `change_*` functions, `scope_draw_pointers`, the new
  auto setup's DC blocks and the two loaders. Both Makefiles gained one SRCS / test line each.

**Bench walk-through (maintainer, bench session on <port>)** - FEL-load the image, `#VER` fw=0.2.54 proto=1.16. Normal
display, both channels on, CH1 trigger, Move fast (not slow) unless noted.
1. Defect 1, pointer is not a handle. Old repro: drag the yellow "1" pointer up: nothing moved. Now: touch the "1"
   pointer and drag up and down: CH1 follows the finger; the same for "2". A touch up to about 20 px above or below the
   pointer, within the left 44 px, still grabs it.
2. Defect 3, nearest enabled. Old repro: drag CH2 to about 10 px below CH1, switch CH2 off in its menu, touch 5-10 px
   below CH1's line and drag: nothing moved. Now: CH1 moves (both from its pointer and from the line on the plot). With
   CH2 on and parked exactly on CH1: the drag moves CH2 (drawn on top); move it away and CH1 is reachable again.
3. Maintainer's report (CH2 at the bottom, CH1 only from its zero line): CH1 now drags from its pointer at any height; on the
   plot the zero-line grab is unchanged (the trace body follows in batch 2).
4. Defect 5, pick after the move. Old repro: a quick upward flick starting on a channel's zero line often moved the
   trigger point. Now: the flick moves the channel.
5. Defect 8, H marker shifts. Old repro: drag CH1 up with a diagonal stroke: the H marker moved. Now: the H marker
   stays; a drag on empty plot still moves it horizontally.
6. Defect 9, level drifts. Old repro: CH1 trigger, level about one division above the trace, drag CH1 to the top and
   back: the level ended on the trace. Now: the drag stops when the level reaches the top, and back down the level is
   still one division above the trace (same trigger voltage).
7. Defect 10, EXT follows CH2 (bitstream 0x1533+). Old repro: trigger source EXT, drag CH2: the level moved. Now: it
   stays. With source CH2 it follows as before.
8. Defect 4, auto setup with Invert (needs a DC input). Old repro: Settings > new auto setup, CH1 Invert on, DC
   coupling, a positive DC input, AUTO SET: the CH1 pointer sat at the top and could not be dragged. Now: the pointer
   is inside the grid (position 6..396) and drags from the pointer and from its line. Also: a settings sector or a
   saved waveform setup with an out-of-range position loads with the pointer on the grid.
9. Taps. A brisk tap on a pointer changes nothing (it used to change the time base). Taps elsewhere keep the 1.00o5
   zones: left / right half time/div, top / bottom bands V/div.
10. References: Ref menu, show two references at different heights; each drags from its own pointer (the nearer one when
   their boxes overlap, the later-numbered one when they coincide). Touches at x 44..60 no longer reach the refs.
11. X-Y: any drag moves CH1 with x and CH2 with y, as before; the left column is not a handle column there.
Bug indicators: a pointer drag moving a different pointer than the one under the finger; the trigger level moving
with EXT; a tap on a pointer changing time/div or V/div; a channel stuck after AUTO SET with Invert; the level jumping
at the first drag step.

## 33. fw-ui2: UX makeover step 2 (fw 0.2.56 provisional, protocol 1.16 unchanged)

Branch from main 9cc599c (fw 0.2.51), merged with main d31ee84 (0.2.52 readers, 0.2.54 touch) in 2ebef4d and with main
33cd1b5 (0.2.55 settings, fw-ui3) in 5255516. Plan:
`docs/plans/ux-makeover.md` section 7 step 2; rules `docs/ui-guidelines.md`; user-facing description README section 5
"Screens: CTRL page, fold-outs, chips, warnings, spectrum cards". Commits, one topic each: 93f1b68 (`ui_ctrl.c/.h`, the
card formatters in `ui_fmt.c`, test_ui), aaa0613 (top bar chips), a41ca39 (CTRL page, fold-outs, strips and header out,
warnings, ACQ menu, settings word 211), e1900ff (0.2.56, CHANGELOG, README), 2ebef4d (merge), f372581 (AUTO SET label
only on page one), cf2cbf6 (handoff), 5255516 (merge of main with fw-ui3's 0.2.55), then this note. Bench image from
5255516 (step 2 on top of 0.2.55): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 543,744 B, sha256
7729e4fe970f3fb634f5d600191c3cc873587edb9fd6ffb604b21f4e64cc69c8, ELF
a79ad696068a3d3d5641d78710b86b5556ec0d83661f07a1dfc2ed36ce08324d (nommu bin 16bb691e..., ELF d73204cc...). The
pre-merge image from f372581 was a182ba84 (536,576 B). **Nothing benched.** Host
tests green (test_ui 6672 checks on the branch, 1,436,917 with fw-ui3's settings tests after the merge). GCC 10
warnings 36, GCC 13 / PC_DEBUG 31, upstream only; the
new files are clean with -Wall -Wextra. No protocol change: GET_STATUS byte 43 keeps its meaning (4 = phosphor drawn,
now also in X-Y). Settings words: 210 bit 3 (the Phosphor switch), 211 (0xC700: bit 0 Grid, bits 1-2 the spectrum's
Display variant); 212..223 stay free (later claims: 212 fw-acq, 219..220 fw-bode section 42, 221..223 fw-meas section 41).

Points for a successor:
- **Files**: `ui_ctrl.c/.h` (pure: page-two items and hit areas `ui_ctrl_item_at`, page-one slots `ui_p1_shown`,
  fold-out layout and hits `ui_fold_*`, the value-name tables, `ui_display_value`, `ui_scope_warning`, the spectrum
  card set `ui_spec_cards`); `ctrl_page.c/.h` (target: drawing, the modal fold-out loop `cp_fold_run`, the steppers,
  Grid, 50 % TRIG). menu.c `scope_setup_right_control_menu` calls `ctrl_page_draw` for page two outside the viewer;
  statemachine.c `handle_right_volts_div_menu_touch` hands page two to `ctrl_page_touch`; the viewer's page two and the
  DC shift states 2-4 still run 1.00o5's code below it (old V+ / V- at `voltdivypos`, Show grid, Next page / END cal.).
- **Fold-outs** are modal loops like the top bar dropdowns: the trace area x 500..727 is saved in `displaybuffer2`
  and put back on close, then the whole page is redrawn (a value can show on two buttons: Reset sets the Display
  variant). Choice rows close, value rows step and stay, Ref level is the one two-way row (left half down), a tap
  outside closes and is consumed. The wfm/s line in the Phosphor row is a snapshot taken when the panel is drawn.
- **Phosphor switch** (phosphor.c `phos.wanted`, `phosphor_switch` / `phosphor_set_switch`): the picture is drawn while
  the switch is on, the scope view shows and `phosphor_available()`; `phosphor_service` turns it off in roll mode and
  the viewers and back on after. The spectrum keeps it off without touching the switch (`pc_glue_main_menu_mode`
  passes `phosphor_switch()` to `ui_mm_choose`). `pc_phos_wanted`, `pc_display_variant`, the 0.2.51 Display row
  (`pc_glue_acq_display_*`, `ACQ_MENU_BODY_H`, `ACQ_DISPLAY_*`), the strip (`phosphor_strip`, `phosphor_touch`,
  `PC_PHOSPHOR_STRIP`) and `phos.over` are gone; `phosphor_param_text/step`, `phosphor_wfm_text` feed the fold-out.
- **Warnings**: `PC_SCOPE_PASS()` at the start of `scope_display_trace_data` latches the ADoverload flags (the channel
  blocks clear them), `PC_SCOPE_WARNING()` before the copy to the screen draws `ui_scope_warning` at (366 centre, y 69)
  in font_2 orange, not in the viewer; the untriggered flag is `pc_scope_untrig` from `pc_glue_ui_capture_addr`. The
  spectrum keeps `spec_draw_warnings`.
- **Spectrum**: the strip (`F_*`, `spec_draw_strip`, `spec_field_value`) and the header (`spec_draw_header`,
  `spec_text_at`, `spec_glyph_marker`) are gone; `spec_draw_cards` draws `ui_spec_cards` with `scope_draw_card` (the
  scope cards' helper, factored out of `scope_display_channel_measurements`) at y 446; touches below y 436 do nothing.
  `spectrum_param_text/step` (`SPEC_P_*`, a step ends a running Auto) and `spectrum_view/set_view` feed the fold-outs.
  The grid dots follow `gridenable`, the level labels stay. `spec_autoset_button` draws only on page one.
- **Grid** is `scopesettings.gridenable` in every mode now (1.00o5 used it only in the viewer): `scope_draw_grid` always
  draws the record-position slider and the lines only when it is on; both callers in scope_functions.c call it
  unconditionally. Restored from word 211 by `pc_ui_fix_settings`; 1.00o5's load still sets 1 first.
- **Right bar follows the mode**: `pc_right_bar_follow` in `pc_glue_ui_service` redraws the bar when the spectrum view
  starts or ends (main menu, roll mode, viewer, Auto), in `maindisplaybuffer`.
- **Deviations from the brief / mockups, on purpose**: Scale and Ref level are below a separator in the Analyse
  fold-out (the plan's maintainer text and the SpectrumCtrlAnalysis board; there is no free slot for an Axis button, and the
  spectrum's 50 % slot stays empty). Grid shows a slide switch, not "on" (guidelines: a toggle never reads on / off).
  MEASURES is an empty slot in Spectrum (the spectrum's own list is Measurements v2; the cards are a fixed set). The
  Display button values are abbreviations that fit 47 px ("Phos", "XY Ph", "Spec", "Wfall"; test_ui checks the widths).
- **Lost or open until later steps**: the spectrum's MEASURES list and THD card (Measurements v2); one-tap phosphor
  Clear (now CTRL, Display, Clear, accepted by the maintainer); a live wfm/s while working (it shows only in the fold-out;
  a card is the review's suggestion A8); the viewer's and DC shift pages keep 1.00o5's look; the Settings fold-out and
  screen are step 3 (fw-ui3, 0.2.55, merged here); fold-outs pause the trace and the PC (E_BUSY after 250 ms)
  like every dropdown.

**Bench walk-through (maintainer, bench session on <port>)**
1. FEL-load the image, `#VER` fw=0.2.56 proto=1.16. Top bar: CH1, CH2, ACQ and the trigger chip each a grey box with
   the coloured block on the left, no dark notch at the join; touch each chip: white box, menu opens as before.
2. CTRL: page two reads Display (Normal), Trace style (Linear), Grid (switch, green), the CH1 stepper (grey V+, yellow
   CH1, grey V-), the CH2 stepper (cyan), 50 % TRIG. Tap V+ / V- near their outer edges and near the middle of the
   CH1 / CH2 block: the upper half of the block is V+, the lower V-; the top bar chip's V/div follows. CH2 off: its
   centre is grey and the stepper does nothing. 50 % TRIG sets the level as before.
3. Grid: the grid lines go, the record-position slider under the plot stays; power-cycle: Grid stays off.
4. Display: a panel left of the bar with Normal (green), X-Y, a line, the Phosphor switch row. X-Y: closes, the
   Lissajous shows. Display again, Phosphor on: Persistence, Palette, Clear appear, the second line of the Phosphor
   row reads "NN wfm/s" after reopening. Close by tapping the plot: the plot under the panel is back, the button reads
   "XY Ph", the X-Y figure is graded (phosphor in X-Y is back). Normal: "Phos", the graded traces; Clear empties the
   picture and closes. Persistence / Palette step in place.
5. Roll mode on (ACQ menu): Display shows the Phosphor row grey with "Not in roll mode"; Roll off: the picture comes
   back by itself.
6. Trace style: Linear / Dots / Peak / Pro, the chosen one green; a choice closes and applies. The ACQ menu has no
   Trace column and no Display row (386 px high).
7. Warnings in Normal: unplug the probe with a small V/div or overdrive CH1: "CH1 overrange: increase V/div" orange at
   the top middle; Auto trigger with no edge: "No trigger: check the trigger level"; X-Y with CH2 off: "X-Y needs both
   channels: switch on CH1 and CH2"; both off: "No channel enabled...".
8. Menu > Spectrum analyser: page one shows CTRL, RUN/STOP, AUTO SET, three empty slots, SAVE PIC, SAVE WAVE; no strip,
   no header line; cards at the bottom: [Peak] and [Level] per shown channel in its colour, [RBW] with a light grey
   chip. Page two: Display (Spec), Analyse (2048 pt), Grid, the steppers, an empty bottom slot.
9. Analyse: Analyse CH1 / CH2 / CH1 + CH2, FFT length, Window, Average, a line, Scale, Ref level with - and + (left
   half down, right half up), Reset; values change in place, the plot follows after closing; the Analyse button shows
   the new FFT length. Display: Waterfall: a fourth card [Time] counting up; Density: the legend top right.
10. AUTO SET in the spectrum, then CTRL during "Auto...": the Analyse button is not overwritten; back to page one: AUTO
   SET reads correctly. Menu > Scope: page one shows the cursor / measures buttons again, page two the Scope items.
11. Power-cycle with Phosphor on and the spectrum in Waterfall: the scope boots graded, Spectrum analyser opens in
   Waterfall. Settings > Restore default settings (0.2.55): Normal, Phosphor off, Grid on, Spectrum.
12. Waveform viewer: page two is 1.00o5's (V+ / V-, Show grid).
Bug indicators: a stepper half acting on the other channel or the wrong direction; a fold-out leaving a stripe of
the panel on the plot (stopped scope); AUTO SET painted over Analyse; the right bar showing Scope items in the spectrum
or empty slots in the scope after a mode change; the warning line in the waveform viewer; cards overlapping the
frequency labels; Phosphor switch on but no graded picture in the scope view outside roll mode.
## 34. fw-ui4: UX makeover step 4, Diagnostics, wizard, first run (fw 0.2.59 provisional, protocol unchanged)

Branch `fw-ui4` from main 72eacb3 (0.2.52), merged with main 508f0c5 (0.2.58, protocol 1.17) in 4c38068. Plan:
`docs/plans/ux-makeover.md` sections 6, 6a-6c, 7 step 4; review `docs/plans/diagnostics-review.md`; user-facing
description README section 5 "Screens: Diagnostics, calibration wizard, first run" and 6.4. Commits: f95dbee (`ui_diag.c/.h`,
`host_tests/test_ui_diag.c`, `pc_report_event`), 95f440e (four-screen wizard: `cal_wiz.c/.h` rewritten, `cal_wiz_ui.c`
dialog part rewritten, `ui_dlg.c/.h`, test_cal_wizard rewritten), 5e8a5db (display mode `ui_diag_view.c/.h`, hooks, old
screens removed), 6897cf5 (0.2.59, README, CHANGELOG), 4c38068 (merge + the fw-ui3 interim rows out), then this handoff.
Image from 4c38068 (GCC 10): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 602,112 B, sha256
7efaada392eb22c8a8b8531449d07d7054e4ea41e0593ae9cfdf31e56e06ec08, ELF a28c2705782f033656030cc3488486cacc7c21b215a661d0d7bf459c5d450fe6;
nommu 6787f60ab1ebb8ecbfdacfbfe9efe065e16b345065f8405dd5a76f1b084a7169 (`#VER` fw=0.2.59, proto=1.17). A build of a later
commit differs only in the identity. **Nothing benched.** Host tests all green (test_ui_diag 4862, test_cal_wizard 631,
test_ui 1418549, test_session 69705 / 69703, check_vectors OK). Warnings GCC 10 36, GCC 13 31, sorted lists identical to
main 508f0c5; the new files are clean with -Wall -Wextra -Werror (host). Version: kept 0.2.59 as briefed although main is
0.2.58: the bench session renumbers.

**Files.** `ui_diag.c/.h` (pure: each page and the first-run screen as boxes, texts and buttons built from the protocol
bodies GET_HEALTH, GET_CAL x 2, GET_LINK_STATS and the card identity; the verdict rules; `ud_hit`), `ui_dlg.c/.h` (target:
draws a `ud_page`, the 3/8 dim of the screen behind a dialog, the step dialog used by the wizard and the volts
calibration, `ui_dlg_confirm`), `ui_diag_view.c/.h` (target: the display mode, gathering the bodies, the actions, the
volts calibration flow, Clear calibration, the first-run screen). Hooks: `fnirsi_1013d_scope.c` (main loop: `if(ui_diag_pass())
continue;` after `pc_glue_ui_service`; `ui_first_run(restore)` before the loop, replacing `scope_setup_restore_screen`),
`statemachine.c` (main menu Diagnostics row -> `ui_diag_open`), `pc_hal_scope.c` (`pc_draw_begin` and
`pc_right_bar_follow` draw into `ui_diag_scope_buffer()`; the class probe and the RTC clock (`pc_glue_rtc_allowed`) are off
while Diagnostics shows), `pc_report.c/.h` (`pc_report_event`: one event in words).

**Decisions.**
- Diagnostics replaces the scope's pass, it does not run beside it: no acquisition while it shows (as STOP), so the PC's
  own captures work and nothing draws traces over the page. Full redraw once a second into `displaybuffertmp` and one
  copy (about 50 ms, estimated, not measured). The scope screen is kept in `displaybuffer2` (as the 1.00o5 screen did) and
  every background redraw goes there; Back copies it back.
- The page model is pure so the host test checks every string against the real fonts at its widest (15-character
  versions, 4-digit counters, a restart, the stock bitstream, no card): nothing is cut except the Log page's event lines
  (`trim`; the saved report has them whole).
- Status residuals: the firmware has no corrected A - B residual or corrected gain mismatch (they need the post check,
  diagnostics-review section 4 item 7, not built), so the Status box shows what exists against the review's provisional
  limits: class gain ratio full / gated (2 %) and the worst noise RMS over ranges, gated (0.3 code) and full (1.0 code),
  from the v3 block; the verdict adds bound / gains measured / carried / FPGA word changed / under 5 min warm-up from the
  flags and the provenance. "Limits are provisional" is printed under them.
- Wizard: no Skip (maintainer); Cancel is live during a measurement (polled between points, `CW_RC_CANCELLED`). The zero step
  re-measures both channels on every Calibrate. Save always writes zeros and gains (sectors 708 and 709); without the
  generator the earlier gains are carried (flag KEPT). The fold-out caller is gone, so after Save with `calibrationfail`
  the callers (Diagnostics, first run) run the new volts calibration flow.
- Volts calibration wraps 1.00o5's `scope_input_calibration` (`calibrationopen` 3..8 per step; its own drawing goes to
  `displaybuffer3` as a scratch screen) in a step dialog; it backs up and restores the channel, time base and trigger
  settings (1.00o5 left the scope at 5 V/div, 20 us/div, AUTO) and on Cancel the factors and `calibrationfail`.
- Clear calibration = 1.00o5 RESET without the volts reset: centres 860, compensations 0, `adc_cal_loaded = 0`, tracker
  reset; nothing written (709 at the next power-off).
- First run shows at every boot until both channels use a bound block (no settings word spent on "seen it").

**Removed** (README section 5 lists where each value went): `scope_open_diagnostic_view`, `scope_diagnostic_adc_cal`,
`scope_diagnostic_v4` and their fmt helpers, `handle_diagnostic_view_touch` (DEFAULT, RESTORE, RESET, EXIT, the hidden
option hit areas), `scope_setup_restore_screen`, the DEFAULT / RESTORE / RESET / EXIT / OK button drawers,
`pc_glue_diag_drawn`, `pc_glue_version` and `PC_UI_VERSION` / `PC_DIAG_DRAWN`; fw-ui3's interim fold-out row
`UI_FO_CAL` with `scope_system_settings_calibration_item`, the 1.00o5 volts boxes (`calibrationopen` 2..9 in
`handle_main_menu_touch`, `scope_show_Input_calibration`, `scope_show_calibrating_*_text`, `..._done`) and the Settings
screen's Calibration group (`UI_SET_DCSHIFT`; the DC shift trim is on the Diagnostics Calibration page).
`scope_show_Input_calibration_fail` stays (drawn by `scope_input_calibration` into the scratch screen).

**Not done / for the next agent.**
- Check on post and Undo last calibration (plan 6): need the post-based check and sector 711 (section 30 "Not done");
  no placeholders drawn.
- The DC shift trim still runs on the scope's CTRL page states 2-4 (1.00o5); the Calibration page only switches it.
- GET_STATUS byte 43 does not say "Diagnostics" (the host cannot tell the page is up; the protocol works regardless).
- The Firmware tile reads the card image once per Diagnostics session (cached by pc_ident); a pre-0.2.47 card image is a
  full image scan (under a second, not measured on the device).
- The pass time measured by GET_STATUS while Diagnostics shows is the page's pass.

**Bench walk-through (maintainer, bench session on <port>; FEL load, back up SD sectors 700-710 first: Save writes 708 and 709)**
1. FEL-load, `#VER fw=0.2.59,proto=1.17`. On the maintainer's calibrated unit no first-run screen. Menu > Settings: the
   fold-out has Screen brightness, Grid brightness, File message timeout, (Time and date), Other settings; no Baseline
   calibration row. Other settings: no Calibration group.
2. Menu > Diagnostics: the Status page; tile Firmware reads "Reading" for a moment, then Check "Running 0.2.59 from RAM.
   A power cycle starts 0.2.25 from the card." (or what the card holds). The uptime line advances every second. With the
   MCP connected: `scope_status` and a capture work while the page shows (no E_BUSY); the USB tile shows "PC connected,
   high speed." Calibration box: verdict and the two rows, the provenance sentence.
3. Tabs: Calibration (per-range table, gains, tracking lines, DC shift values), Log (events newest first; Older events
   pages, then Newest events), Developer (Debug overlay toggles: Back shows "DEV mode" on the scope; Enter FEL now asks
   first: Cancel returns). Back returns to the scope exactly as it was; a setting the PC changed meanwhile is shown.
4. Save report to card: "Saved to the card: reports/report-NNNNN-00.txt" (Status or Log page); without a card the reason.
5. Wizard, first time on the new screens (USB plugged in at first): Calibrate: screen 1 "Zero level" over the dimmed
   page, the amber "USB is connected: unplug it before Calibrate"; unplug USB (and both probes, or short the inputs with
   BNC caps): the line goes within a second. Calibrate: "Measuring..." greyed, "CH1: measuring range 1 of 6" .. "CH2:
   measuring range 6 of 6" (about 8 s), two ticks, Continue. Screen 2 without the probe on the post: "No signal on CH1: tip
   on the post?", Calibrate again with the CH1 probe at 1X on the post: "CH1 gain done", Continue; screen 3 the same for
   CH2. Done: two lines "CHn calibrated: cores matched to x code, gain mismatch y %" in the channel colours; Save
   calibration. Plug USB back in: GET_CAL flags bit 3 set, bit 4 clear, provenance of this power-on; the Status box OK or
   Check with the reason.
6. Cancel paths: a new run, Cancel during the zero step's measurement (stops within about a second), Cancel on screens 2-4:
   GET_CAL unchanged each time. Taps off the two buttons do nothing.
7. Clear calibration: the red button asks first ("Zeros and gains are cleared; the volts-per-range factors are kept."...);
   Cancel keeps everything; Clear: the Status verdict Fault "Uncalibrated", GET_CAL byte 72 = 1. Then Calibrate again
   (do not power off in between: the next power-off saves the 860 centres to 709, the old block then no longer binds).
8. First run (optional, with Clear calibration then a power cycle, or a card without 708): the "First start" screen with
   the checks; Later opens the scope; next boot Calibrate now runs the wizard.
9. Volts calibration (only with an adjustable DC supply up to 15 V): six steps; Cancel restores the factors and the
   scope's V/div and time base.
Bug indicators: anything drawn over the Diagnostics page that stays longer than a second (MCP chip, clock, battery, a
trace); Back showing a stale top bar; a wizard tap reacting outside its buttons; Cancel during a measurement taking more
than about 2 s; GET_CAL changing after a Cancel.

## 35. fw-seq: FPGA 0x1536 segmented capture and hardware averaging (fw 0.2.58 provisional, protocol 1.17, row 56 provisional)

Version 0.2.58 as assigned by the bench session (main: 0.2.56 fw-ui2, 0.2.59 fw-ui4); protocol 1.17 with the row kept at
**70** as briefed (main's last row is 55; renumber to 56 at the merge if preferred). Branch
`worktree-agent-adad2198a41835255` from main 9cc599c (0.2.51), merged with origin/main 57924e0 (0.2.55, protocol
1.16). Commits: e6bbc10 (pc_seq.c maths + test_seq + seq_check), 65abebf (HAL, UI averaging, protocol 1.17, mock,
test_session, vectors), 32a80ed (protocol.md row 56), f10ccf4 (CHANGELOG, READMEs, design-note status), 7bfd267
(docs/plans/phosphor-fast-capture.md), b4453bb (merge of main), 29c55ac (two GCC warnings), then this handoff. Then 246bc52 (merge of main), 3934ea3 (renumbered 0.2.58). Image from
3934ea3 (#VER fw=0.2.58 proto=1.17): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 548,352 B, sha256
6bbd7598cfb473b5226e34a9a18bf808f1795ac6f02158c0cd368316eb954dfe, ELF
ad1c1597c87441eb6c8bd26056229cf259227d75a13e910c477d0fb232b8c8dc; A/B `make gcc10-nommu` sha256
fc8fc081016d13d7320152845e684bc9c7bf01ea53ac6ab156c799b7ddedb191. Not pinned in backups/. **Nothing benched, nothing
flashed.** Host tests all green (test_seq 732, test_session 69705 / 69703, test_diag 462, check_vectors OK with 90 frames,
163 decim, 73 seq cases). Warnings: clean GCC 10 / nommu 36, GCC 13 / PC_DEBUG 31, sorted lists identical to a clean build
of origin/main 57924e0. No settings words or sector bytes claimed (none needed: the UI averaging picks N itself).

Spec: docs/protocol.md section 14 row 56; user-facing and file map: firmware/README.md section 5 "Segmented capture and
hardware averaging"; FPGA: fpga/al3/README.md section 6 (+ "Firmware support"), docs/plans/segmented-capture.md.

**What was decided and why**
- One cap bit, SEQ (17), for both modes (one bitstream feature); a features word in GET_STATUS 44-45 (and GET_DECIM
  32-33) so a host sees EXT / DECIM / FIR / SEGMENTS / HW_AVERAGE without decoding caps; GET_STATUS 46-47 = the UI Average
  state (0 off, 1 software, 2 hardware) and log2 N. All answered without FPGA access (ISR safe).
- Host interface follows design note section 7 with changes: S and raw are per read (READ_SEQ), not in SET_SEQ, because
  the FPGA applies them live; READ_SEQ has its own header; segments are read with READ_RAW addr_mode 3 so all three
  layouts work; STOP ends a run as DONE (a circular run has no other end) instead of ABORTED.
- Arm order on 0x1536: 0x0B / 0x0C and 0x57-0x5C before 0x01 = 1 (fpga-gen6 confirmed the race in simulation: a 0x0B in
  the last write clock before the release is ignored, and 0x57 / 0x59 written last in the window left the slot mask at
  1FFF). 0x57 = 0 is written whenever the tracked last value (`seq_57`, 0xFF after boot) is not 0; the READ_SEQ view write
  (0x57 = 0x02 | raw) updates the tracker, so the next plain arm writes 0 (fpga-gen6's caveat).
- UI averaging only for legacy records that fit the accumulator: the UI depths start at 3000 samples, so 16-bit records
  (1 word per sample) never fit 2046 words. With the default filter (CIC2+FIR) that means hardware averaging only at
  200 MSa/s or with the filter off. A UX follow-up could offer a 2000-sample depth (or cap the depth) while Average is on;
  `pc_seq_ui_conversion` would then need the 16-bit lane path (L0 / L2, u32 sums, a 64-bit mean).
- The averaged UI record is read with the dummy strobe (exact, as pc_hal_read_core). fpga-gen6 found that the 1.00o5
  readout and `fpga_read_decim_data` (no dummy strobe) return mem[S], mem[S], mem[S+1], ...: one word late. So an averaged
  UI record sits one word (2 samples) earlier than a single UI record at the same settings; the software trigger
  refinement re-centres both. Not changed here (it is the 1.00o5 readout, all records); queue the fix for the UI readout.

**Points a successor needs**
- Files: `pc_seq.c/.h` (pure), `pc_seq_hw.c/.h` (target: pc_hal_seq_* and the UI run), handlers in pc_cmds.c section
  "Sequencer". Edits in shared files are small: `fpga_control.c` (one hook before 0x01 = 1), `scope_functions.c` (`PC_SW_AVERAGE()`
  appended to the two software-average conditions), `pc_hal_scope.c` (glue calls), `pc_hooks.h` (two macros). UI agents
  editing scope_display_trace_data: keep the `PC_SW_AVERAGE()` term.
- `pc_hal_arm` gained `const pc_seq_cfg *sq` (NULL on bitstreams without SEQ; mode 0 = plain capture). Every new arm path
  must go through `pc_seq_hw_pre_reset` on 0x1536, or a stale mode turns a capture into a run.
- The firmware auto time-out must never run during a run (it writes 0x1A = 0 between records): host runs use the FPGA's
  0x0E in auto mode, UI runs have their own deadline (`pc_seq_ui_service`).

**Flash session plan (maintainer with the bench session; agents never flash)**
1. Backups: the stock FPGA flash dump exists (`backups/2026-10-18_fpga-25q80-stock.bin`, sha256 90f65f08...0d253).
   Before writing, dump the flash as it is now (0x1535): `python firmware/tools/spiflash_d2xx.py read -o
   backups/<date>_fpga-25q80-0x1535-before-1536.bin --twice`; its sha256 must equal the 0x1535 padded image
   `backups/al3_fir_0x1535_5040a2ae_padded.bin` (sha256 5ade9e755211fe65...), the rollback image. The tool also keeps its
   own double-read pre-write backup.
2. FEL-load this image on the running 0x1535 first (`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe
   0x80000000`). Check `#VER` fw=0.2.58 proto=1.17; GET_INFO caps without bit 17; GET_STATUS length 48, bytes 44-45 = 0x0007,
   46 = 0 (Average off) / 1 (Average on); SET_SEQ answers E_UNSUPPORTED; a normal ARM / WAIT / READ_RAW as on 0.2.55. This
   is the "older bitstream unchanged" check.
3. Flash 0x1536: `python firmware/tools/spiflash_d2xx.py probe` (expect `ef 40 14`), then `python
   firmware/tools/spiflash_d2xx.py write -i backups/al3_seg_0x1536_4eaf6811_padded.bin --yes` (padded sha256 66866c84...7408;
   the tool asks for the JEDEC ID and verifies by read-back). Scope on battery, no USB, as in fpga/al3/AGENTS.md
   "Flashing this unit". Power-cycle.
4. Verify: Diagnostics shows FPGA version 0x1536; FEL-load the image again (or boot it), `#VER` ...fpga=0x1536; GET_INFO
   caps has bit 17 (0x20000); GET_STATUS bytes 44-45 = 0x001F; GET_SEQ answers 48 bytes with byte 24 = 0x00 (no run yet).
   Log hashes and results in docs/hardware-status.md.
5. Modes off (fpga/al3/README.md section 6 check 1): the 0x1535 bench checks (legacy 200 MSa/s, CIC, CIC+FIR, EXT) give
   the same pictures and GET_DECIM read-back as on 0x1535; UI with Average off looks unchanged.
6. UI averaging: CH1 on the calibration pin, 1 kHz square, 200 MSa/s time base (filter irrelevant there), Average on:
   GET_STATUS 46 = 2, 47 = 6 (64 records); the trace is visibly less noisy than with Average off; the edges stay where they
   were (no shift beyond a sample); `pass_ms` within a few ms of Average off. Filter off at 1 MSa/s: 47 = 3 (8 records).
   CIC2 at 1 MSa/s: 46 = 1 (software, as before). Auto mode with the probe unplugged: the trace free-runs (single records)
   and comes back to 2 once the signal triggers. Normal mode, no signal: waits as before.
7. Segments (README check 2), host: SET_TRIGGER CH1 rising, normal, pretrigger 32, total 240 samples (legacy 200 MSa/s:
   16 / 120 words); SET_SEQ mode 1, m 7, count 32, hold-off 0; ARM; WAIT. GET_CAPTURE_INFO 76 = 1, 80 = 32, 84 = 0. READ_SEQ
   table: timestamps 100 us +/- 10 ns apart with the generator at 10 kHz (difference 20000 units); every segment via READ_RAW
   addr_mode 3 crosses the level at record index 32 (or the T5 convention). With the generator at 10 kHz: run time about
   3.2 ms.
8. Circular + STOP: SET_SEQ flags 1, count 4, m 7 on a 1 kHz square; ARM; after about 1 s STOP; WAIT returns DONE; GET_CAPTURE_INFO
   77 = 0x07 (circular, stopped, wrapped), 80 about 1000; READ_SEQ of 64 entries: valid entries with timestamps 1 ms apart, one
   invalid (the slot being refilled).
9. Averaging (README checks 5 and 6): input shorted, legacy 200 MSa/s, EXT or auto; SET_SEQ mode 2, count 1, N 1: READ_SEQ
   S 0 must equal READ_RAW of the same capture word for word (core A lane = 0x20 byte, B = 0x21) - the mirror check, the one
   hardware risk the testbench cannot cover. Then N 256, S 8: rms of the averaged record about 1/16 of N 1. 16-bit: CIC2+FIR at
   10 MSa/s, count 2, N 1000, READ_SEQ S 0xFF: sums / 1000 match the single-shot shape.
10. PI case (README check 7), when the detector is at hand.
Rollback: `spiflash_d2xx.py write -i backups/al3_fir_0x1535_5040a2ae_padded.bin --yes` (sha256 5ade9e75...; or the stock
dump); 0.2.58
runs on 0x1535 unchanged (step 2), so no firmware rollback is needed.

**For the mcp agent (do not edit mcp/ from this branch)**
- protocol.py: `Cmd.SET_SEQ = 0x020E`, `GET_SEQ = 0x020F`, `READ_SEQ = 0x0210` (cap SEQ), `Caps.SEQ = 1 << 17`;
  StatusResp fields `features` (u16 at 44), `ui_average` (u8 46: 0 off, 1 software, 2 hardware), `ui_average_log2n` (u8 47);
  DecimState the same at 32..35; CaptureInfo 76..95 (seq_mode, seq_flags, m, fmt16, count, untriggered, count requested, N,
  hold-off ns); SetSeqReq (16 B), SeqState (48 B), ReadSeqReq (12 B) + header (16 B); ReadRawReq addr_mode 3; table entry
  decode (ts 39..0 x 5 ns, offset 52..40, triggered 53, valid 63). Vectors to decode: `resp_get_seq_segments_done`,
  `resp_read_seq_table`, `resp_get_status_1_17`, requests `req_set_seq*`, `req_read_seq*`, `req_read_raw_segment_5`.
- client: `set_seq`, `get_seq`, `read_seq_table`, `read_seq_average(ch_mask, shift|raw)`, `read_segment(seg, layout)`; ARM / WAIT
  / STOP as today (STOP keeps a run's data).
- tools: `scope_capture_segments(count, length, pre, holdoff_us, circular, max_wait_s)` -> per segment timestamp (s), triggered,
  data per channel in volts (READ_RAW mode 3 + the usual conversion; 16-bit via GET_DECIM gain); `scope_capture_averaged(n,
  positions, length, pre)` -> mean per channel in volts (legacy: lane x 2^S / N per core then the per-core correction; 16-bit:
  sum / N then the row 47 / 49 conversion), both refusing with the GET_SEQ fit code; `scope_status` gains `hardware_averaging`
  (byte 46 == 2), the log2 N, and the features list. The simulator needs SET_SEQ / GET_SEQ / READ_SEQ and addr_mode 3.

**Open / for the bench session**
- fpga-gen6 findings that hold for 0x1532-0x1536 (fixed in their 0x1537, not built yet): (1) the A/B order inside a word can
  flip after a 0x0D or 0x53 write and stays flipped until the next such write, so PC_INTERLEAVE_ORDER and the swap bit are
  right by chance (bench T4); (2) the 1.00o5 readout and `fpga_read_decim_data` are one word late (see above); (3) on
  0x1532-0x1535 the 0x0B-in-reset race at slow legacy rates still exists (I kept their arm order byte-identical as the brief
  asked): moving 0x0B / 0x0C before 0x01 = 1 there too is a one-line change in `pc_hal_arm` / `fpga_do_conversion`; queue it.
- 0x1537 will need the version rule only (`pc_variant_from_version` already maps 0x1537..0x153F to SEQ).
- Phosphor fast capture: design in docs/plans/phosphor-fast-capture.md (hundreds of wfm/s at fast time bases), not built.
- UI 16-bit hardware averaging (see "decided" above) and an Average count setting are UX decisions for the maintainer.

## 36. fw-calnoise: the wizard's "too much noise" on 0.2.59 (fw 0.2.60, protocol unchanged)

Bench fact (maintainer, 2026-10-19 15:45, 0.2.59 in RAM, FPGA 0x1535): the Zero level step failed with "too much noise" on
both channels in most runs, with probes and USB unplugged. 0.2.48's base calibration had passed in the morning (stored
noise 0.6-1.3 codes).

**Correction (bench session, after 16:15).** The step also fails with nothing connected, and with USB connected, on
0.2.59 and on 0.2.60. The 0.2.60 line reads "CH1: range 4 at 200 MSa/s: core B, 21.1 codes, 21.0 per capture" and
"CH2: range 1 at 200 MSa/s: core A, 24.4 codes, 24.3 per capture". One core per channel returns about 21-24 codes RMS
inside a single full-rate capture, while host READ_RAW at 200 MSa/s legacy gives 0.5-2 codes on the same cores most of
the time. The earlier wire-on-the-post reading (CH1 B 19-30 codes with 50-60-code excursions, CH2 A 6-17, 200 MSa/s
only, 2 MSa/s clean) showed the same pattern and was not caused by the wire. With only USB connected and a good state:
0.5-2.0 codes per core, std of the capture means under 0.26, max excursion under 10 codes, generator on or off the same.

Likely cause, not confirmed on the bench: the A/B encode-phase race of 0x1532..0x1536 (fpga/al3/README.md "Findings"
item 3, fixed in 0x1537). On 0x1535 each ADC chip's encode flops toggle themselves on negedge clk_ADC
(e2aec2e:zaklad.v:685-695). The RAM write clock toggles 1.25 ns earlier on negedge clk_200MHz, and both sample the
combinational enable `divider < counter`. A 0x0D or 0x53 write that changes the compare inside that window leaves the
chip's encodes half a word out of phase with the write clock, until the next such accident; the two chips flip
independently. At 2 MSa/s the data is stable for microseconds, so either phase reads clean. At 200 MSa/s one core's
output transition can then fall on the capture edge, which returns metastable bytes on that core only. The odds per write
grow as the divider nears 0. 0x0D is written every 30 s by the v4 class probe (acal_live.c, since 0.2.50), twice per range
by the wizard, and many times by the Bode tool.
Ruled out for this failure: a stale decimation mode. Calibration conversions configure K = 0 with the FIR off
(pc_glue_decim_conversion), and pc_dec_apply writes 0x53 and 0x56 when its cache differs. Only pc_dec_apply calls
fpga_set_decim_mode. With dec_on = 0 the RAM takes the raw pins (zaklad.v:806, sample_dia), so a stale 0x54 shift,
0x55 level or swap/neg bits cannot reach the bytes. With dec_on set (the host study that showed fpga_mode 225), 0x20 /
0x21 read the low / high byte of dec_out1 and 0x22 / 0x23 those of dec_out2: garbage on every lane, at every rate.
Decisive bench test: alternate 0x0D between divider 0 and 1 (or a gated rate). After each change, take a 1000-sample
READ_RAW at divider 0 with fpga_mode 0. If the race is the cause, individual cores switch between about 1 and about 20
codes RMS, chip by chip. The cure is the 0x1537 bitstream. A firmware re-roll (toggle 0x0D until a full-rate check capture
is clean) is possible on 0x1535 but not built.

Ruled out (by reading and host tests):
- The merge. `cal_result` / `cal_measure` / `cal_channel` are byte-for-byte the path of 0.2.48's
  `scope_do_baseline_calibration`. fw-cal3 moved it; fw-ui4 added only the Cancel polls between ranges. `acal_rms16`
  over the merged halves is the per-core RMS about the merged mean: n is merged, the cores are never pooled, the value
  is in 1/16 code and compared with 48, and the noise check runs before the drift check. With the host std per core and
  a 0.3-code drift between the halves, the host test gives 9-35/16; the drift adds about 0.02 code
  (test_adc_cal `test_zero_noise_bench`).
- The acquisition state. Calibration conversions run legacy: K = 0, because `pc_dec_ui_arm` is set only by the UI
  acquisition. The 16-bit read returns early for `calibrationsettings`, and the v4 hooks skip it. The 0x1536 sequencer
  is off on 0x1535. The FPGA write address resets to 0 at every conversion, so the read at 100..600 is always fresh.
- The entry point. `cal_begin` differs from 0.2.48 only by a menu redraw and CH1/CH2 dcoffset = 0; the scratch channel
  uses neither. The Diagnostics and first-run entries reach the same `cal_wizard_run`.
- Limit unchanged: 3 codes of the same quantity 0.2.48 checked. The RMS pools 8 gated or 64 full-rate conversions, so
  it includes variation between captures as well as noise inside them. The new line shows both.

Built:
- 27556e8: `acal_acc.convsq` and `acal_rms16_within` (RMS about each conversion's own mean).
- `acal_range_check`: the store's checks with an `acal_fail` (class, core, value, within16). `acal_range_store` uses it.
- `cw_zero_detail`, and the noise and drift result lines, for example "CH1: too much noise, range 6 at 2 MSa/s:
  core B 3.3 codes, 1.1 per capture" and "CH2: zero still moving, range 1 at 200 MSa/s: core A 0.8 code". The range is
  1-based like the progress line (range 1 = 5 V/div).
- The Zero level instruction now reads "Disconnect everything from the device: both probes, anything on the /
  calibration post, and the USB cable. Tap Calibrate." It is still right but not the fix: see the correction above.
- test_cal_wizard checks the lines and their widths in the 16 px font. test_adc_cal checks the bench numbers, a
  +-3-code move between conversions (fails, 1.1 per capture) and a 64-code glitch per conversion (fails, within as
  large).

Bench check: the 0x0D toggle test above. If a core shows about 20 codes "per capture" on the wizard line, that is the
race. Pickup on an open input shows as a large total with a small "per capture" value instead.
Open: the zero step does not detect a load on the post by itself. The noise check caught it only because the wire
carried noise; a quiet wire would pass with a shifted zero.

## 37. fw-calnoise: full-rate capture check and FPGA write trace (fw 0.2.61, protocol 1.18, row 57)

Confirmed on the bench (bench session, 2026-10-19 16:20-16:45):
- **0x1535.** A 0x0D write re-rolls the encode phase at random. CH1 core B was garbled (15-29 codes RMS within a
  capture) after 15 of 30 changes 99 -> 0 and 12 of 30 changes 1 -> 0; otherwise 1.1. CH1 core A moved with it
  (0.50 -> 1.6). CH2 had flipped earlier the same day (24-code core A in the wizard).
- **0x1537** (flashed 16:31). Static 200 MSa/s captures are clean (20/20). After a 0x0D write to divider 0, CH2 core A
  read 32-34 codes in 35 of 40 trials from 99 or 1, and 11 of 40 from 3999. The bad state does not settle with time
  (0 ms 0/20, 5 ms 7/20, 20 ms 18/20, 300 ms 16/20). A second capture right after a bad one is also bad, so the state
  is set by some write between the rate write and the capture and persists until the next one; fpga-gen7 is analysing
  the bitstream side. At divider 1 static, CH2 A read 55 codes and both B lanes were constant: probably a READ_RAW
  layout difference of the 100 MSa/s mode, not chased.
- **0x1537 wizard on 0.2.60.** CH1 passes; CH2 fails "the input is not at 0 V". That text is ACAL_E_CLIP: any sample
  at code 0 or 255 in any measured point (the gated and full-rate slope points and the zero points; the coarse points
  are excluded). The 0x1537 garbage (32-34 codes RMS) reaches the rails, and the clip check runs before the noise check.
  The zero step never compares with the stored zeros: it reads raw codes through the scratch channel. A change of core
  order or lane mapping (CH2 A-B now 128.7 / 132.0 against about 130 / 130 on 0x1535) cannot fail it. It makes the
  stored per-lane zeros wrong until a new calibration, which the wizard then measures per lane.

Built (fw 0.2.61):
- `enc_heal.c/.h` (pure, test_enc_heal):
  - `eh_residual16`: per channel, the RMS of core B against the mean of its two core A neighbours. It takes the
    smaller of the two neighbour choices, so it does not depend on core order. Signal and offset cancel; noise leaves
    about 1.3 codes; a garbled core leaves tens.
  - `eh_run`: check, then retry up to `EH_MAX_TRIES` (16) above `EH_LIMIT16` (8 codes); `EH_WAIT_MS` (0) after each
    retry. All three constants are in enc_heal.h only.
  - `eh_affected` (0x1532..0x1536) only picks the amber Diagnostics line.
- `enc_heal_hw.c`:
  - The capture: one conversion, raw scratch channels for both channels, own buffers. Sample counts and trigger mode
    are restored.
  - The retry writes 0x0D = 1 and then 0.
  - Counters 62-63 and event type 21.
  - `fpga_encode_heal` runs only at divider 0, on every bitstream.
  - `fpga_encode_check_dirty` runs the check when a 0x0D write happened since the last check (`fpga_rate_dirty`, set by
    the write trace).
- Call sites:
  - the wizard before the full-rate zero points of each range and before each full-rate gain level (ACAL_E_TIMING 18);
  - the UI acquisition before its decimation arm and conversion (scope_functions.c);
  - `pc_hal_arm` first.
- FPGA write trace (fpga_control.c): `fpga_write_cmd` remembers the command, and the next data write logs event 22
  for 0x0D, 0x53, 0x56 and 0x01 = 1. The caller comes from `fpga_trace_who`: UI by default; host inside pc_poll
  (all three contexts); CAL between cal_begin and cal_end; PROBE in probe_run; CHECK in the heal.
- Protocol 1.18: GET_LINK_STATS has 64 counters; mcp protocol.py names them and event types 21/22.
- Diagnostics: the amber FPGA line on AL3 0x1532..0x1536 (test_ui_diag).

Open:
- Core order on 0x1537 (fpga-gen6/7). The firmware's fixed lane convention is: tracebuffer odd = 0x20/0x22 ("A"),
  even = 0x21/0x23 ("B").
  - Order-independent: the zero step, the check, and the per-lane zeros and gains (a recalibration fixes a mapping
    change).
  - Assumes "B is the earlier sample of a pair" (PC_INTERLEAVE_ORDER 0): acal_track's A-B timing model and the
    decimator's swap bit. Re-check both once fpga-gen7 reports the real order on 0x1538.
- The UI check costs one extra conversion after each rate write: the class probe every 30 s, time-base changes, the
  wizard's cal_end. It runs at divider 0 only.
- Bench: read GET_LINK_STATS 62-63 and GET_EVENT_LOG types 21/22 after a wizard run and after toggling the time base
  to 200 MSa/s a few times. Expect 21 entries with small `a` (recovered), and 22 entries showing who wrote what
  before each.

## 38. fw-enc: every full-rate capture checked, FPGA 0x1538 read 0x33 (fw 0.2.62, protocol 1.19, row 58)

Bench fact this answers (docs/hardware-status.md 2026-10-19 16:48): on 0x1537 one 0x0D write, then 1221 host captures in
60 s with no other write: CH2 core A garbled in about 40 % of them, randomly interleaved. Chip 2's phase is re-resolved at
every sampling reset (a marginal flop), so 0.2.61's check, which ran only after a 0x0D write, never fired. fpga-gen7's
0x1538 (fpga/al3/README.md "Changes" section 8) drives the encodes statelessly from the write clock and reports a
per-capture self-check, FPGA read 0x33.

Branch `worktree-agent-a54ba9470daa9c8cd` from main 84ce79f (0.2.61), merged with origin/main a179408 (fpga-gen7's 0x1538,
rel-gen1). Commits: 9e7e53d (firmware and host tests), 7cebed3 (protocol.md row 58, caps bit 18, vector), 8a5d6dd
(CHANGELOG, firmware/README.md), the merge, 1b4f763 (protocol 5.1 reconciled with fpga-gen7's paragraph), then this
handoff. **Nothing benched, nothing flashed.**

**What was built**
- `enc_heal.c/.h` (pure): `eh_mode` (DATA 0x1532..0x1537, FLAG 0x1538..0x153F, NONE otherwise), `eh_check` (FLAG: read
  0x33, bit 0 = bad; DATA: 250 words per channel from the record start, 0.2.61's B-against-A-neighbours residual, bad
  above 8 codes), `eh_cap_step` (re-arm while bad and fewer than EH_MAX_TRIES = 16 re-arms, then accept and flag), event
  field packers. 0.2.61's `eh_run`, `fpga_encode_heal`, `fpga_encode_check_dirty`, `fpga_rate_dirty` and their extra
  check conversion are gone.
- `enc_heal_hw.c` (target): `fpga_encode_init` (pc_glue_init: mode; on 0x1538 one 0x33 read at boot, bits 3:2 must be
  01, else GET_HEALTH 90 bit 1 and event 21 reason 3), `fpga_encode_after_capture` (called by `fpga_done_conversion` when
  a conversion is done: check, and on bad `pc_hal_enc_retry` + `pc_glue_decim_rearm` + `fpga_do_conversion`, returning
  "not done" so every caller's wait loop keeps waiting), `fpga_encode_new_conversion` (top of `fpga_do_conversion`: a new
  capture unless it is the re-arm), `pc_hal_enc_status` / `_retry` / `_note` / `_state`, `fpga_enc_garbled` (wizard).
- Host ARM (`pc_acq_service`, pc_cmds.c): `pc_enc_rearm` after `pc_hal_acq_finish`, before the trace build; re-arms with
  the setting `h_arm` stored (`arm_dc`, `arm_seq`, `arm_auto`, `arm_prog`, `arm_normal`; `pc_arm_hw`), state back to
  ARMED, `arm_ms` restarted (the firmware auto time-out counts again). A failed re-arm (0x05 never ready) ends ABORTED.
- Who is checked: every conversion at divider 0 (always K = 0): UI, wizard, v4 class probe, 1.00o5 calibration and
  auto setup (all through `fpga_done_conversion`), host ARM. Skipped: other dividers, NONE bitstreams, and on DATA
  bitstreams sequencer runs (UI averaging: `pc_seq_ui_running`; host: `cap_next.seq_mode`); a host run STOP ended is
  never restarted. FLAG covers a whole run (bit 0 is sticky until the next 0x01 = 1).
- Re-arm writes: 0x1532..0x1536 re-write 0x0D = 1, 0 (caller CHECK in event 22) before the re-arm, since only a rate
  write re-rolls their phase; 0x1537 and 0x1538 only re-arm (0x1537 re-resolves at every reset).
- Wizard (cal_wiz_ui.c): clears `fpga_enc_garbled` before the full-rate points of a range and before each gain level,
  fails ACAL_E_TIMING when it is set afterwards (a conversion accepted bad after 16 re-arms).
- Protocol 1.19 (row 58): caps bit 18 ENC_CHECK (0x1538+), features bit 5 (0x0020); GET_HEALTH 104 bytes (88 mode, 89
  last 0x33, 90 flags: 0 read, 1 convention bad, 2 last capture accepted bad, 3 core order differs (0x1537); 91 0x33 at
  boot; 92 bad, 96 recovered, 100 checked); GET_LINK_STATS 62-63 now per capture; event 21 a = reason << 5 | re-arms
  (bit 7 not recovered; reasons 1 data, 2 0x33, 3 boot convention), b = caller << 12 | first value. Vector
  `resp_get_health_1_19`.
- `PC_INTERLEAVE_ORDER` is fixed at 0 (no `#ifndef`); the two READ_RAW ternaries and the 0x53 swap default are
  constants now. acal_track's A - B model and the decimator's swap bit assume B first, which 0x1538 guarantees: nothing
  else changes. Caps bit 12 INTERLEAVE_UNVERIFIED stays set until T4 on 0x1538 (bench session's call).
- Diagnostics: 0x1532..0x1536 amber "ADC capture timing race, fixed in 0x1538"; 0x1537 amber "ADC core order differs:
  recalibrate after flashing 0x1538"; 0x1538 from GET_HEALTH: "encode check OK" (+ ", N captures re-taken"), amber
  "encode check failed on CH2 A, CH1 B" (pins with bit 0 in 0x33 bits 7:4), amber "unknown encode convention, 0x33 =
  0x..", or "encode check, no 200 MSa/s capture yet". Report file: "Full-rate capture check: ..." line, event 21 in words.
- Mock HAL (host_tests): `enc_bad_arms` (next arms garbled, -1 all), 0x33 per arm (`enc33_bad` 0x77 = CH2 A wrong), a
  garbled arm fills core 2 (CH2 A) with noise; counters as the target counts. The mock ring is now a triangle (one code
  per word) so the data check reads it clean; every existing test passed unchanged with it.
- Tests: test_enc_heal 105 checks, test_session 69800 / 69798 (`test_enc_check`), test_ui_diag 5485, test_diag 471.
  Warnings: GCC 10 and nommu 36 (sorted list identical to 0.2.61's), GCC 13 / PC_DEBUG 31.

**Decided and why**
- Data check from the ring, not from the trace buffers: the UI path checks before its readout, so a garbled capture
  never reaches the screen, the measurements or the 50 % trigger; the cost is 1000 bus bytes (about 0.3 ms) per
  full-rate capture on 0x1532..0x1537 only. Host READ_RAW reads later (and partially), so it needs the separate read
  anyway. 0x1538 costs one bus byte.
- Re-arm inside `fpga_done_conversion` rather than at each of the 12 call sites: the 1.00o5 calibration and auto setup
  loops, the wizard, the probe and the UI all go through it; the UI keeps its previous trace while re-arming.
- Normal / single trigger mode: a re-arm waits for the next trigger (a single-shot event caught garbled is lost; the
  data was useless anyway). Bounded at 16; the 17th bad capture is accepted and flagged (GET_HEALTH 90 bit 2, event 21
  bit 7, wizard ACAL_E_TIMING).
- Counters: 62 counts captures whose first check failed (at that moment), 63 those recovered; 62 - 63 = accepted bad or
  abandoned by a new conversion during the re-arms (no event for an abandoned one).

**Bench check** (maintainer with the bench session; FEL load only, nothing written):
1. On 0x1537 first (it is in the flash): FEL-load 0.2.62, `#VER` fw=0.2.62 proto=1.19 fpga=0x1537. Diagnostics FPGA
   line amber "ADC core order differs: recalibrate after flashing 0x1538". The 16:48 experiment again (divider 0 held,
   1221 host captures in 60 s): no READ_RAW with a garbled core (per-core std under 5 codes), GET_HEALTH 92 grows about
   0.4 per capture, 96 = 92 (all recovered), 100 = captures + re-arms; event 21 entries with a = 0x2N (reason 1, N
   re-arms), caller host.
2. Flash 0x1538 (fpga/al3/README.md section 8, the flash session plan in docs/hardware-status.md; padded image
   `backups/al3_seg_0x1538_0c24ecf6_padded.bin`, sha256 2fc7ea5b...1087), power-cycle, FEL-load 0.2.62: `#VER`
   fpga=0x1538; GET_INFO caps bit 18 (0x40000); GET_STATUS 44-45 = 0x003F; GET_HEALTH 88 = 2, 91 = 0xF6, 90 bit 1 clear.
   Diagnostics: "encode check, no 200 MSa/s capture yet", then after one 200 MSa/s capture "encode check OK".
3. Rate-toggle test on 0x1538: alternate SET_TIMEBASE raw divider 99 / 1 / 3999 <-> 0, 40 trials each, READ_RAW 1000
   at divider 0 after each: zero garbled captures; GET_HEALTH 92 stays 0 (or 62 = 63 with an occasional async-write
   miss), 89 = 0xF6.
4. 60 s back-to-back captures on 0x1538 (divider 0 held, host ARM / WAIT / READ_RAW as fast as possible): zero garbled,
   counters stable (92 unchanged, 100 = the capture count), no event 21.
5. UI at 200 MSa/s for a minute, then the time base toggled a few times: the trace clean, GET_HEALTH 92 stable.
6. Wizard zero step (probes and USB off): both channels pass ("CH1: zero level done", "CH2: zero level done"); then
   the full calibration (the 0x1537 calibration must be redone), Save; GET_CAL CH2 A-B near 0 as on 0x1535.
7. If 92 grows on 0x1538: the event 21 b values (0x33 bytes) say which pin; report to fpga-gen7.
Rollback: FEL-load 0.2.61 (pinned backups/fw-0.2.61_4593c43f.bin); 0.2.62 runs on 0x1535 / 0x1537 as well.

**For the mcp agent (do not edit mcp/ from this branch)**
- protocol.py: `PROTO_MINOR = 19`; `Caps.ENC_CHECK = 1 << 18`; features bit 5 ENC_CHECK (0x0020) in GET_STATUS 44 /
  GET_DECIM 32; HealthResp grows to 104 bytes: `enc_mode` (u8 88: 0 none, 1 data, 2 FPGA 0x33), `enc_last33` (u8 89),
  `enc_flags` (u8 90: 0 read, 1 convention bad, 2 last accepted bad, 3 core order differs), `enc_boot33` (u8 91),
  `enc_bad` (u32 92), `enc_recovered` (u32 96), `enc_checked` (u32 100); describe() with the 0x33 pins {CH2 A bit 7, CH2
  B 6, CH1 A 5, CH1 B 4}, 1 = right, 0xF6 good. Event 21 decode: reason (a >> 5) & 3 (1 data, 2 0x33, 3 boot
  convention), re-arms a & 0x1F, bit 7 not recovered, caller b >> 12 (as type 22), value b & 0xFFF (residual / 16 codes
  or the 0x33 byte). Link counters 62/63 names unchanged ("captures bad / recovered").
- Vector to decode: `resp_get_health_1_19`. The simulator should answer GET_HEALTH with 104 bytes.
- scope_status / diagnostics tool: show the check mode, last 0x33, the three counts; a measurement taken while
  `enc_flags` bit 2 is set deserves a caveat.

**Open**
- `docs/plans/task-queue.md` item "Full-rate encode check on every arm": done on this branch pending the bench above.
- `docs/release-notes/0.3.0.md` (rel-gen1) does not mention 0x1538 or the per-capture check; the bench session decides.
- Caps bit 12 INTERLEAVE_UNVERIFIED: clear it for 0x1538 once T4 confirms B first on hardware (one line in
  `pc_variant_from_version`).
- 0x1532..0x1536 still re-roll only on rate writes, so their re-arm re-writes 0x0D first (two extra writes per re-arm;
  0x1535 measured 50 % odds per write, so about one re-arm per bad capture on average).
- A large signal above about 15 MHz raises the DATA residual (a 100-code 10 MHz sine stays under the limit,
  test_enc_heal); on 0x1532..0x1537 such a signal could make every full-rate capture re-arm 16 times and be flagged.
  0x1538 has no such limit.
## 39. fw-polish1: UI polish batch 1 (fw 0.2.63 provisional, protocol unchanged)

Branch from main 8f8687a (fw 0.2.61), merged with origin/main a179408 in bc32b8b. Brief: `docs/plans/task-queue.md` "UI
polish batch 1" items a-p plus the bench session's item q; rules `docs/ui-guidelines.md`; user-facing description README
section 5 "Screens: UI polish batch 1 (fw 0.2.63)" (and the older Screens sections corrected where they changed). One
commit per item: f58ff29 (a), 70da62a (q), bcb3007 (b), f71ede6 (c), d513498 (d), 8d508cd (e), cf37f5d (f), 1fb05fd (g),
2830fc2 (h), 1ecaef3 (i), ba116cf (j), d4345e8 (k), eb51787 (l), 6c82e0c (m), 32d17a9 (n), b1fe1fc (p), then the version /
CHANGELOG / README commit, the merge, this handoff. Image from bc32b8b (GCC 10): `firmware/scope/build-gcc10-pc/
fnirsi_1013d.bin` in the worktree, 606,720 B, sha256 28b15dcdda21112caa49aee81b6b0c394977259784e44b7952ed47ce59d8f475, ELF
76852f3281c1a5e26fe5732e3eedbd58c880ff5207bc091649b30134e478f82e; nommu bin
21c80a58cf7851bef4b4d39943761613e98fa27559982cc743941ab1bec5a656 (`#VER` fw=0.2.63, proto=1.18). A build of the handoff
commit differs only in the identity. **Nothing benched.** Host tests all green (test_ui 1,418,459, test_ui_diag 4880,
test_cal_wizard 648, test_session 69707 / 69705, check_vectors OK). GCC 10 warnings 36 (gcc10 and gcc10-nommu), sorted
line-number-free list identical to 8f8687a. No settings words, no protocol change. Version: 0.2.63 as briefed (0.2.62 is
fw-enc's); the bench session renumbers if needed.

Decisions (maintainer may veto each):
- (e) **Pro removed.** `scope_display_channel_pro_trace` drew, per screen column, the minimum and the maximum of the
  samples in that column as two separate polylines (min to previous min, max to previous max), with no vertical fill
  between them. Zoomed in (fewer samples than pixels) min = max and it is Linear with held samples; on a clean signal the
  outlines lie within a pixel and it looks like Linear; on a noisy or fast signal it shows Peak's band as an outline. Not
  distinct enough to keep: three styles. `pc_ui_fix_settings` maps a saved 3 to Linear; the draw switch's default is now
  Linear (`_old`, the same function as case 0; the default called a different, unused-elsewhere linear variant).
- (n) **Debug overlay switch removed, flag kept.** `dev_mode` draws "DEV mode" at (670, 50), the raw 0x14 trigger
  address at (500, 10) over the top bar (cleared and redrawn every capture: the flicker), the trigger position
  (`scope_test_trigerposition_value`) and touch coordinates in the generator and keypad menus. Raw FPGA numbers that a
  user cannot act on, and the PC already gets the trigger address in every capture record. The flag stays in the
  settings sector; the Developer page shows a "Debug overlay / Switch off" row only while it is set, so units that turned
  it on with 0.2.59-0.2.62 can turn it off. (After switching it off the top-bar number at x 500 stays until the top bar
  is next redrawn, e.g. by opening a dropdown.)
- (p) **RUN/STOP wording: green "RUN" while running, amber (0xFFD262, the Diagnostics "Check" colour) "Stopped" while
  stopped**, black text both. "Stopped" reads as a state where "STOP" reads as a command. It is in the 13 px font
  (47 px wide): "STOPPED" is 62 px in the 16 px font and 50 px in the 13 px one, wider than the 51 px button allows.
  Touched: orange as every bar button. The top bar's RUN / WAIT / STOP chip (red STOP) is unchanged: say if it should
  follow (one line in `scope_run_stop_text`). `pc_hal_ui_set_run` now redraws the button too (page one, not in the
  viewer); the single-shot stop in scope_functions.c already did.
- (j) The Diagnostics header takes the Settings screen's: black band (`RGB_HEADER` 0) with the 0x262626 rule, Back at x
  692. The tabs keep their look (the unselected ones already were the Settings Back button's colours; the shown one is the
  lighter blue with the double edge). The first-run screen shares the header band.
- (k) DC shift trim: new button style `UD_B_ON` (green 0x145A2E / 0x35C26A) while on, `UD_B_GREY` while off; the line
  "On: CTRL on the scope opens the trim." (amber) only while on.
- (m) The history channel: `spec_hist_pick` (not saved, like Analyse). With Analyse = CH1 + CH2 both spectra stay
  computed and the pick chooses the history; with a single-channel Analyse the pick sets Analyse (`spec.recompute`), since
  only analysed channels have spectra. The row exists only in Waterfall and Density, below a separator; a tap toggles and
  keeps the fold-out open. Auto's channel (`spec_auto_ch`) still follows Analyse only. Two histories at once: later
  (needs a second `wf_rows` / `dens` history and a split-plot or overlay design).
- (f) Pass time moved under the Memory column's rule (x 312, y 218 of the menu), "Pass time" grey and "NN ms" white,
  geometry in `ui_ctrl.h` (`UI_ACQ_PASS_*`), test_ui checks it lies in the empty part of the column and the texts fit.
- (g) Root cause: `handle_main_menu_touch` hit-tests every touch sample afresh. A drag that left the slider's 58 px band
  (y 46..104 / 105..163) went to the final else (close everything), or into a fold-out row or a main-menu row (x < 150,
  which switches the mode). Now `drag` remembers the slider the touch started on and feeds every sample to
  `scope_move_slider` (which clamps x) until release.
- (h) The Restore dialog body is two lines broken where the next word does not fit; test_ui checks every dialog for that.
- (l) `ui_step.step`: drawn right-aligned on the title line in the 16 px font, soft grey. Only the volts calibration uses
  it (the calibration wizard's four screens have titles, no step count).
- (a, q, c) Icons drawn in code: cogwheel per pixel (ring r 3..7, four straight 5 px teeth to 10 px, four diagonal ones to
  r 9.9), sine as 25 column values joined by lines, X-Y as 24 points of a lemniscate turned 55 degrees. The
  `system_settings_icon` and `confirmation_icon` bitmaps are gone.
- (o) Checked: the zero step's first instruction line is 433 px in the 16 px font (592 available), the second shorter;
  test_cal_wizard checks both.

Not touched (other agents' areas): enc_heal / fpga_control arm paths, the ACQ dropdown handlers and
`handle_acquisition_menu_touch`, `scan_for_touch` and the drag handlers, `spectrum_touch`, the measurement cards and the
spectrum card set, the bode view.

**Bench walk-through (maintainer, bench session on <port>; FEL load, no SD writes needed)**
1. FEL-load, `#VER fw=0.2.63`. Menu: the Scope row shows one period of a sine wave, the Settings row a cogwheel; both
   in white, black while touched (Settings yellow while its fold-out is open).
2. CTRL page: Grid is a green button reading "Grid"; tap: grey, the grid lines go; tap: green, back. No slide switch.
3. Display: the X-Y row's icon is a figure 8 leaning right. Choose X-Y: the Trace style button turns grey (label and
   value), a tap on it does nothing. Normal: it is white / pale yellow again and opens.
4. Trace style: Linear, Dots, Peak only. (Optional: with Pro saved by 0.2.62, power-cycle into 0.2.63: Linear.)
5. ACQ menu: "Memory" header, then the depth chips with nothing between them; under the column's rule at the right:
   "Pass time" and "NN ms" on two lines, nothing overlapping.
6. Menu > Settings: Screen brightness, Grid brightness, (Time and date), Other settings; no File message timeout. Open
   Screen brightness, drag the slider and let the finger wander up and down well outside the slider's band and over the
   fold-out and the main menu: the slider follows x, the fold-out stays open, no row reacts. Release, then tap outside:
   it closes. Same for Grid brightness.
7. Other settings: the header reads "Settings" and Back only; nothing beside Restore default settings; Controls has File
   message timeout (tap flips "wait for a tap" / "0.7 s"). Restore default settings: the dialog body is two lines, the
   first nearly the box's width. Cancel.
8. Menu > Diagnostics: black header like Settings (grey rule under it), Back at the same place as on Settings; no
   battery icon over the header's right end for a minute (it is current again after Back). Calibration tab: DC shift
   trim button grey, no "DC shift trim off." line, no "Needs a DC supply up to 15 V."; tap: green with "On: CTRL on the
   scope opens the trim."; tap: grey. Developer tab: only Enter FEL now (unless the flag was on: then a "Debug overlay /
   Switch off" row above it; tap removes it).
9. Calibration tab > Volts calibration: the dialog title line reads "Volts calibration" at the left and "Step 1 of 6" at
   the right; the second line "Tap Calibrate.". Cancel.
10. Spectrum analyser > CTRL > Display: Spectrum, Waterfall, Density only. Choose Waterfall, open Display again: a line
   and "Channel CH1" (Analyse CH1 + CH2 or CH1). Tap: "CH2", the waterfall restarts with CH2's history; Analyse keeps
   CH1 + CH2 (or follows to CH2 when it was CH1). Switch CH2 off: the row is grey and inert, showing CH1.
11. RUN/STOP: running, the button is green "RUN"; tap: amber "Stopped"; tap: green "RUN". From the MCP:
   `scope_acquisition_control` action "stop": the button turns amber "Stopped" at once, and green on action "run". Same with Diagnostics open (Back shows the
   right state) and in the spectrum view.
Bug indicators: a slider drag that closes the fold-out or switches the mode; RUN/STOP and the top bar chip disagreeing on
run vs stop; the battery icon on the Diagnostics header; the Trace style fold-out opening in X-Y; a Pro row; the pass
time overlapping a chip; text past the edge of the RUN/STOP or Grid button.
## 40. fw-acqtouch: ACQ time/div coupling, Filter per mode, touch batch 2 (fw 0.2.64 provisional, protocol unchanged)

Branch from main 8f8687a (fw 0.2.61). Briefs: docs/plans/task-queue.md "ACQ: sample rate vs time/div coupling", "FIR
step overshoot pulsing" (maintainer decision: Filter per mode) and "Touch fixes batch 2"; docs/plans/touch-review.md (b), (c),
(d). Commits: 5718015 (tb_view / acq_view, ACQ menu, display window, slider drawing, host paths), 5818b5b (Filter per
mode, word 212), 29d61f1 (trace point buffers), b620113 (touch batch 2), f690e42 (spectrum pan, fold-outs on release),
d1f303f (trigger point drag limits), then docs and this handoff. Images from merge e626b1b (main a179408 merged, no firmware conflicts): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin`
in the worktree, 612,864 B, sha256 8730e6700777fc740803805e18fffef2991d6ae94bc3174566c99572f7493fd7; nommu
15e82d94dde5d057f1f9a7aa00f711f63612ecf8804f0a8b484551fb8da5545d (a build of a later commit differs only in the identity). **Nothing benched.**
Host tests green (test_tb_view 57750 checks, test_touch_pick 584, the rest unchanged). GCC 10 warnings 36, sorted
line-number-free list identical to 8f8687a. **Settings word 212 claimed** (0xD500 marker, the Spectrum's Filter; 207 is
the Scope's); 213..223 stay free. No protocol change (wire formats unchanged; docs/protocol.md SET_TIMEBASE notes the
snapped index in the response and the per-mode Filter).

Points for a successor:
- **tb_view.c/.h** (pure): the trace window maths. A time/div is viable when `724 x fs < total x 50 x fpd` (the record
  covers columns 3..727, the last pixel's interpolation included) and `fs >= fpd` (one sample per division; 200 MSa/s
  at 5 ns/div is exactly one). `tbv_window` keeps 1.00o5's first-sample formula (trig - 1 - ceil(pos / xpps)) and
  clamps it into the record; a record shorter than the screen draws from column 3 to `3 + ceil(total x xpps)`.
  `tbv_pos_limits` are the trigger point columns that keep the window inside the record for the nominal trigger sample
  (`fpgasettings.settriggerpoint`); a software trigger found off the nominal point is handled by the window clamp. The
  test compares with 1.00o5's table: 24 cells differ at 3000 samples (3 at 250 samples/div and 17 below one sample/div
  offered by the table; 4 at 200 samples/div withheld by it), so the table was removed.
- **acq_view.c/.h** (target glue): `acq_view_fit(notice)` snaps the time/div (short mode, indices 11..34) and clamps the
  trigger point; called from the ACQ menu (rate, memory, Filter), `scope_set_timebase` (after the tap zoom; re-writes
  0x0E when it snapped), `pc_hal_timebase_write` mode 0, `pc_hal_trigger_write` (pre / total), `pc_hal_decim_set`,
  `pc_dec_follow_mode` and `pc_ui_fix_settings`. The notice is a static string drawn by `pc_glue_scope_warning` when no
  warning shows, white, 4 s from its first draw (a long stay in the ACQ menu does not eat it). `acq_view_tb_ok` greys and
  blocks the ACQ time/div boxes (all unavailable in long mode, as 1.00o5's table rows 18-28 were).
- **Display**: `scope_display_trace_data` uses `tbv_window` in short mode (long mode keeps 1.00o5's xrange code); the
  last-pixel interpolation of the Linear and Dots styles runs only when the trace reaches the right edge.
  `scope_draw_grid` draws the slider window to scale (`tbv_slider`, min 8 px). `move_trigger_point_position` applies
  1.00o5's limits only in long mode (their 1400-sample margin stopped the window short of the record's end).
- **Filter per mode** (pc_hal_scope.c): `pc_dec_setting` is a macro for `pc_dec_set_mode[spectrum_mode() !=
  SPECTRUM_MODE_SCOPE]`; `pc_dec_follow_mode` (in `pc_glue_ui_service`) fits the memory to 8190 and refits the time/div
  when the new mode's filter is on. Defaults in pc_decim.h (`PC_DECIM_DEFAULT_SCOPE` CIC2, `PC_DECIM_DEFAULT_SPEC`
  CIC2+FIR, `PC_DECIM_DEFAULT_OLD` for the migration: an old sector holding exactly the old default gets the new
  per-mode defaults, any other choice is kept for both modes).
- **Touch** (statemachine.c `scan_plot_pick`; touch_pick.c `trace_distance`, `pick_channel_near`, `touch_tap_zone`,
  `touch_cards_top`): one pick at touch-down; TOUCH_STATE_MOVE_RECORD (0x12) for the slider, with
  `previous_trigger_point_position` snapshotted. The card geometry used by the tap bands copies
  `scope_display_channel_measurements` (rows of three from y 426, pitch 24): **if fw-meas changes the cards' layout,
  update `CARDS_*` in touch_pick.h and `scope_tap_bottom` in scope_functions.c.** The H marker has no handle box of its
  own (review rule 5, not in this batch). The `distance_*` globals in variables.c are unused.
- **Not done**: touch-review batch 3 (the blocking touch loop, one GT911 reader, long-mode presets per drag step, the host
  race) did not fall out of this work: the plot still blocks in `scan_for_touch` until the finger moves 6 px or lifts.
  Spectrum plot taps still have no y bound (defect 20, header / label taps). Locked cursors (defect 13) and X-Y moving
  switched-off channels (defect 11) unchanged.
- Peak / Pro overflow: those styles store two points per column (up to 1448) and the buffers had 730 entries;
  `TRACE_POINTS_MAX` 1460 now (inference: this corrupted the neighbouring point buffers whenever Peak or Pro was on).

**Bench walk-through (maintainer, bench session on <port>)** - FEL-load, `#VER fw=0.2.64,proto=1.18`. CH1 on the 1 kHz
calibration square, 1X, DC, 1 V/div, Auto trigger, memory 3000, Scope view.
1. ACQ menu, maintainer's repro: 500 us/div at 500 KSa/s, then tap 200 MSa/s. The time/div box jumps to 1 us (green); the
   boxes 2 us .. 200 ms read grey and do nothing when tapped; close the menu: "Time/div set to 1us for this rate" in
   white at the top middle for about 4 s; the trace fills the screen, no flat zero line at the right, the slider window
   spans almost the whole bar.
2. Back to 500 KSa/s: 1 us is below one sample per division there, so it snaps to 2 us with the notice.
   At 500 KSa/s the white range is 2 us .. 200 us; 500 us is grey now (250 samples/div did not fit 3000 samples: the old
   table offered it and drew zeros past the record). Memory 15000: the white range reaches 2 ms.
3. 1 MSa/s at 200 us/div (200 samples/div, newly allowed): the record fills the screen.
4. Slider: 200 MSa/s, 50 ns/div (window about 35 px). Drag the window right: it follows the finger, the trace moves left,
   the H marker leaves the screen and shows as the side pointer; at the end of the bar the window stops (no zeros, never
   past the record end); drag back: it stops where the record starts. Move slow: a fifth of the finger. A tap on the
   slider does nothing.
5. Drag on empty plot (no trace near): the H marker pans as before and stops at the record's ends.
6. Trace grab: 1 ms/div, the square about 1 division above its zero line. Touch the top of the square (not the zero line)
   and drag down: CH1 moves. 30 px above the square's top: still grabs (40 px); 50 px: the trigger point pans instead.
   Peak trace style: the same on the band.
7. Taps: a still, brisk tap left of centre mid-plot: time/div one step slower; a 4 px wobble still counts as a tap; a
   brisk stroke on the trace (moved, under 250 ms): nothing changes. Tap on a pointer, the slider or the trigger level
   pointer: nothing. Measures: four cards for CH1 (two rows): taps on the cards do nothing; the 80 px band just above
   them steps CH1 V/div less sensitive; nothing happens on the slider row.
8. Stopped zoom: STOP at 200 MSa/s 1 us/div, tap the left time band (slower): it stays at 1 us with the notice (2 us would
   not fit the record); tap right down to 5 ns: works; one more does nothing.
9. Filter per mode: ACQ menu in Scope reads "CIC2" on the first boot of 0.2.64 (a sector with the old default); the 1 kHz
   square shows no pulsing horns. Menu > Spectrum analyser, ACQ menu: "CIC2+FIR". Set Spectrum to CIC3, back to Scope:
   still CIC2. Power-cycle: both kept. GET_DECIM in each mode shows that mode's value.
10. Host: SET_TRIGGER total 1000, then SET_TIMEBASE index 19 (500 us): the response's timeperdiv_index is 20 (200 us) and
   the scope shows the notice.
11. Spectrum: zoom in (tap right), drag to pan with Move slow: a fifth of the speed. CTRL > Analyse: touch a row, slide off
   it and lift: nothing; lift on it: it acts. A tap outside the fold-out only closes it.
Bug indicators: a grey time/div box that can be chosen; zeros or a flat line at the right edge at a white time/div; the
trace jumping against the H marker when the rate changes; the slider window moving the wrong way or past the bar; a tap
on the cards changing V/div; the Spectrum's Filter changing the Scope's.

**fw-acq2 (fw 0.2.84 provisional), the Memory column after Filter -> Off** (e125cf2): `pc_glue_decim_tap` redraws the
column on every Filter change (it did only when a filter went on), the chip set from `acq_view_mem_chips` (acq_view.c,
test_acq_view `test_mem_chips`). Bench: AL3 bitstream with the Filter (0x1534+), ACQ menu, memory 15000, tap Filter
until it reads a filter: the memory jumps to 6000 and 9000 .. 15000 disappear at once; tap on through the filters to
"off": 9000 .. 15000 reappear at once (white, tappable) with the menu still open; pick 15000, close and reopen: kept.
Bug indicators: a depth chip missing with Filter off, or shown and tappable with a filter on.
## 41. fw-meas: Measurements v2 (fw 0.2.65 provisional, protocol unchanged)

Branch from main 8f8687a (fw 0.2.61), merged with main a179408 in 37714c3 (no firmware changes on main in between).
Plan `docs/plans/ux-makeover.md` section 3a, `docs/plans/ux-review.md` section 6 part B, task-queue "Measurements v2";
user-facing description firmware/README.md "Screens: Measurements v2 (fw 0.2.65)". Commits: 8749ffe (code and tests),
f0837c5 (0.2.65, CHANGELOG, README, ui-guidelines names), 37714c3 (merge), then this handoff. Bench images from 37714c3:
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 615,424 B, sha256
1de13bd1cc04f6d637bbd60bb250eaa8ea59caedaab7720fff2a10215bb4f7d2 (ELF 538ae1a7...); nommu
873377a33663d3b41769217f875c3445fef4063fc248ed037bb550ba02180605 (ELF 6e7a5681...). Not pinned in backups/.
**Nothing benched.** Host tests green (test_ui 1,420,237 checks). Warnings: GCC 10 36, GCC 13 / PC_DEBUG 31, the same
line-number-free lists as 8f8687a; ui_meas.c is clean with -Wall -Wextra -Werror.

**Settings words claimed: 221 (spectrum view cards), 222 (CH2 spectrum cards), 223 (CH1 spectrum cards)**, each 0xA000 in
bits 12-15 and four 3-bit ranks from bit 0 (`ui_meas.h` UI_MS_SPEC_WORD_*). fw-acq holds 212 (spectrum Filter, 0xD500).
213..220 stay free; 224..255 are the life record. The Scope's choice needs no new word: its ranks are 1.00o5's
`measuresstate` bytes (MEASUREMENT_SETTING_OFFSET), which 1.00o5 treats as on / off.

Points for a successor:
- **Model** (`ui_meas.c/.h`, pure): a list is an array of ranks (0 off, else the place in the row, ties by index).
  `ui_ms_order` gives the display order, `ui_ms_toggle` puts a new item last and closes gaps, `ui_ms_select_all` keeps
  the chosen order; every change renumbers 1..k first, so garbage ranks heal on the next tap. Wave files and reference
  files carry the ranks in the measuresstate bytes as before (harmless).
- **Fit**: one row only (the 1.00o5 stacking upward over the trace is gone). `ui_ms_scope_cap`: CH1 3, or 6 when the
  CH2 block is not shown; CH2 3 from x 366. `ui_ms_fit`: with more chosen than places the last place is "+N more" in the
  block's colour. Spectrum: 6 places from x 5 at y 446, the "+N" with a neutral chip.
- **Scope cards** (`scope_display_measurements`, `scope_display_channel_measurements(ch, cap)`, `scope_measures_source`
  in scope_functions.c): block rule as 1.00o5 (channel or one of its references on, Hide off, not in roll mode).
- **Spectrum cards** (spectrum.c `spec_draw_cards`, `spectrum_cards()` the selection; the card set left ui_ctrl.c):
  THD (`ui_ms_thd`, spec_db, whole axis `v.half`, harmonics 2..10, fundamental bin >= 5, "f0 too low" / "f0 too high"),
  Noise (`ui_ms_noise_cdb`, spec_raw = the latest record unaveraged, over the shown span, median + 1.59 dB, at the RBW),
  Span and Center from the view. The THD sums three bins per tone, so it is window-shape approximate (Hann scalloping
  cancels in the ratio); the 8-bit floor is about 0.3 %. Not done from the review's tier 2: sub-bin peak, H2 / H3, SFDR,
  SNR, band power, OBW; Scope rise / fall, top / base, overshoot, CH1-CH2 delay / phase / gain, AC RMS, gated statistics,
  PI tau. They fit the same lists: add an item to the table (Scope list is full at 12 per channel in the menu's three
  rows: a fourth row or a second page is needed, and `measuresstate[ch][12]` grows into a spare word block).
- **Card source hook** (`ui_ms_set_source(item, fn)` / `ui_ms_get_source`): only live-channel Scope cards ask it;
  documented in ui_meas.h and README. The FPGA counter driver should register in `pc_glue_init` (or wherever caps are
  read) when its caps bit is set, write with `ui_fmt_sig4` into the 20-byte text, and never touch the bus from it.
- **MEASURES menu** (menu.c `scope_measures_menu_draw` / `_cell`, statemachine.c `handle_measures_menu_touch`, geometry
  `ui_msm_*`): box x 232..730, ending y 477; Scope top y 223 (title + 5 rows), Spectrum top y 313 (title + 3 rows). Cells
  are 48-124 px wide (4-5 per half; the guideline's 80 px minimum is not met by the stock four-per-half item grid, kept
  on purpose), 44 px tall; hit areas include the separators, so every tap under the title is a cell (test_ui sweeps
  every pixel). Redraws cells in place after a change (no black flash). `ui_p1_shown` now shows MEASURES in the
  spectrum; `handle_right_basic_menu_touch` blocks only y 183..297 there.
- Names: "Center" (scope makers' spelling, as the guidelines ask for measurement names); the frequency labels under the
  plot keep their own wording.

**Bench walk-through (maintainer, bench session on <port>)**
1. FEL-load the image, `#VER` fw=0.2.65 proto unchanged (1.18). Scope with the 1 kHz calibration signal on CH1, CH2 on.
2. Settings > Restore default settings (or DEFAULT): the cards read Freq, Vpp, Vavg left to right on CH1 (x 5) and on
   CH2 (x 366), one row at y 426, nothing above it.
3. MEASURES: the box slides in, title "Scope measurements", a source row (CH1 filled yellow, Ref 1-4 grey), Select All /
   Clear / Hide in yellow, twelve names in three rows (Vmax ... -Duty), the same for CH2 on the right; Freq, Vpp, Vavg
   white with 1, 2, 3 at their top left. Tap Vmax on CH1: it turns white with 4. Tap Vpp: it goes grey, Vavg reads 2 and
   Vmax 3. Tap outside: the menu closes, the plot under it is back; the CH1 cards read Freq, Vavg, Vmax.
4. Overflow: MEASURES, Select All on CH1, close: CH1 shows two cards and a third "+10 more" (CH2 on). Switch CH2 off
   (and no Ref 5-8 shown): CH1 shows five cards and "+7 more" across the whole row. Hide on CH2 with CH2 on does the same.
5. Hide CH1: the Hide cell turns orange; close: no CH1 cards. Hide again: back. Ref 1 as source (with a saved
   reference shown): the source cell fills in the reference's colour, the cards follow its colour and values.
6. Power-cycle: the order from step 3 is kept (Freq, Vavg, Vmax).
7. Menu > Spectrum analyser: the right bar shows MEASURES (T CURSOR and V CURSOR slots empty). Cards: CH1 Peak, Level,
   THD, CH2 Peak, Level, RBW (both channels in the Spectrum view). THD on the 1 kHz square wave reads about 40-48 %
   (odd harmonics 1/3, 1/5 ... up to the 10th: sqrt(1/9 + 1/25 + 1/49 + 1/81) = 43 %); on a sine from a generator
   below 1 %. Peak below bin 5 (very slow span): "f0 too low"; a tone near the top of the axis: "f0 too high".
8. MEASURES in the spectrum: title "Spectrum measurements", per channel the label, Select All, Clear and Peak / Level /
   THD / Noise, below them RBW / Span / Center / Time across the whole width. Turn on Noise for CH1: it reads a level a
   few tens of dB below the peak; Span / Center match the frequency labels under the plot (start / centre / stop).
   With more than six chosen the sixth place reads "+N more".
9. Display > Waterfall: CH1 cards and RBW, then Time counting up. Power-cycle: the spectrum's choice is kept (words
   221..223). Restore default settings: back to the defaults of step 7.
10. Waveform viewer: MEASURES opens the Scope list.
Bug indicators: a menu tap acting on the neighbouring cell; a stripe of the menu left on the plot after closing; cards
stacking above y 426; a "+N" count that does not add up to the chosen count; THD changing wildly with the Window
choice on a clean tone; MEASURES doing nothing in the spectrum.

## 42. fw-bode: the Bode plot on the device (fw 0.2.66, protocol 1.20, row 60)

Branch from main 8f8687a (0.2.61), merged with main b5698c4 (0.2.65; fw-enc 1.19 / row 58, fw-polish, fw-acq, fw-meas) in
429a79e. Design: `docs/plans/bode-design.md` steps D1-D4 (status note under 6.1); user-facing: README section 5 "Bode
plot"; protocol: `docs/protocol.md` section 14 row 60 and the 8.1 index (0x0211 BODE_CTRL, 0x0212 READ_BODE). Commits:
626b3f6 (`bode_dsp.c/.h` + host test), 88456d5 (`bode_sweep.c/.h` + host test), 78a43c5 (view, hooks, protocol), 0bb73d4
(docs, vectors, CHANGELOG), 43c1c9c (viewer guard), 23651a4 (host settings kept), 429a79e (merge), then this handoff.
Image from 429a79e (GCC 10): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 679,936 B, sha256
5dab21cabe79135bcfddaa3196539f2e4a127a81f8e00442709daa856d4a3b14, ELF ebc8539f275ceb3b7fbb564e07c6b77774a7b588032225d63f23111d29b321b9;
nommu 695908d3616b77b37ff5fd826aea97f304dba199419d940857e215dbd6f9ef5d (`#VER fw=0.2.66,proto=1.20`). A build of a later
commit differs only in the identity. **Nothing benched.** Host tests all green (test_bode.py: the plan vectors, 3000
random points and 300 random sweeps equal to the host tool's planner, H within 0.002 dB / 0.002 deg of `measure_pair`;
test_bode_sweep 160 checks; test_session 69824; test_ui with the new slot rule; check_vectors 93 incl. `req_bode_ctrl`,
`req_read_bode`; the mcp vectors check passes). Warnings GCC 10 36, GCC 13 / PC_DEBUG 31, nommu 36, sorted line-number-free
lists identical to 8f8687a; the new files are clean with the host tests' -Wall -Wextra -Werror. Numbers claimed: protocol
1.20 (main was at 1.19, so it stays), row 60 (59 unused), commands 0x0211 / 0x0212 (0x020B was never assigned: git log -S
shows only the design doc; left free), features bit 6 (0x0040; bit 5 is fw-enc's ENC_CHECK), GET_STATUS display_mode 5,
settings words 219-220 (213..218 stay free; 212 fw-acq, 221..223 fw-meas).

**Files.**
- `bode_dsp.c/.h` (pure, host-tested by `host_tests/test_bode.py` through `bode_cli.c`, needs numpy and imports the host
  tool's `bode.py` and `simulator.py` from `mcp/src`): the planner (identical integers to `plan_point` / `plan_sweep`, incl.
  Python's round-half-even at the three places it matters; interior sweep points through a Q32 log2 / exp2 with 64-bit
  mantissas, so a tie within 1e-9 could differ, none in 3300 random cases), the DFT (2^-34 turn phase grid = phase_inc x N
  per sample exactly, fx_fft's Q30 quarter-wave table, rect or Hann as the host, mean-corrected sums, record halves, CH1
  +-1 bin leakage), levels in mdB (fx_log2_q16), angles by a 31-step CORDIC, the noise as the median of period differences
  (see "Deviations"), the half-record verdict (`bode_unsettled` = the host's rule), markers (-3 dB, resonant peak by the
  parabola vertex in log f with Q, phase crossings) by linear interpolation in log f.
- `bode_sweep.c/.h` (pure, `host_tests/test_bode_sweep.c` on a DUT model: RC within 0.004 dB / 0.03 deg, divider, wiring
  B saturation with Auto and Hold, STOP, abort, arm failure, thru): the host's `BodeRunner` as a continuation-style state
  machine. Each capture the host makes is a `request` with a phase (PRESCAN, VERIFY, BG, POINT, RECENTRE, RETRY,
  UP_PREV, UP_POINT, DOWN); `on_measure` is what the host does with that capture's result. `bode_step` does at most one
  arm or one readout per call. H is kept in polar form (mdB, mdeg): the splice and the thru are additions. The READ_BODE
  codec (`bode_proto_header / rows`) is here too.
- `bode_view.c/.h` (target): the mode (open / close with save and restore), the service (pending requests, RUN / STOP
  edges, the repeat, up to 20 ms of engine steps per main-loop pass), drawing (`draw_all` into `displaybuffertmp`, at most
  every 150 ms while sweeping), the cursor touch, the CTRL page (its own copy of ctrl_page.c's fold-out loop and row
  drawing: Sweep and Thru; Grid as ctrl_page.c), the generator menu row, settings words, BODE_CTRL actions.
- `pc_bode.c` (target): the scope's `bode_hw` (raw divider via `pc_hal_timebase_write(raw)`, `pc_decim_config` CIC3 + FIR
  with the Filter setting's swap / falling-edge bits, `pc_hal_arm(0, words, total, prog 1, normal 0)` = FPGA auto, the
  readout by `pc_hal_trace_build` (16-bit words from `pc_hal_trace16`, legacy codes x 256 copied), scales: `acal_scale(
  scale_q24, gain16)`, corr16, uv_q8 per range as GET_CHANNEL, the probe factor), the engine instance, the pc_hal glue.
- Hooks: `PC_BODE_PASS` (top of `scope_acquire_trace_data`: no UI acquisition while the view shows), `PC_GEN_BODE_DRAW` /
  `PC_GEN_BODE_HIT` (generator menu row 4; GEN_MENU_HEIGHT 225), `PC_VIEW_OPENING` (top of `scope_setup_view_screen`: the
  view closes so the viewer saves the user's settings), `pc_glue_spectrum_display / touch`, `pc_glue_autoset(_running)`
  ask the view first, `ctrl_page_draw / touch` hand page two over, `ctrl_page_spectrum()` returns 2 in the view
  (`ui_p1_shown(slot, 2)` also hides MEASURES; menu.c and statemachine.c follow it), `pc_glue_main_menu_mode` closes the
  view, `pc_right_bar_follow` tracks it, `pc_hal_display_mode` 5, `pc_ui_fix_settings` restores words 219-220,
  `scope_save_config_data` saves them.

**Decisions and deviations from the design.**
- The CTRL page replaces the design's strip (UX makeover): Sweep (Start / Stop two-way rows, Points, Settle, CH2 range,
  Run sweep) in the Display slot, Thru (Measure, Apply switch, Clear) in the analysis slot, Grid; the stepper and 50 %
  slots empty (the sweep ranges the channels; Hold takes CH2's range from the CH2 menu). The header became the cards
  (f, |H|, Phase at the cursor; -3dB or Peak + Q; Sweep n/m; Thru applied / stale). No Mode field (harmonic mode is D5).
- AUTO SET = one sweep (the mode's own auto; "Auto..." while it runs, a second tap ends it after the point). RUN repeats;
  STOP ends after the point. Entering the view never starts a sweep (the post may be wired to anything): STOP and the wiring.
- The noise estimate is the median of |x[n] - 2 x[n+p] + x[n+2p]| (time domain, cancels the periodic signal and a linear
  drift) instead of the host's median over FFT bins: no non-power-of-two FFT on the device. Equal for white noise (tested
  against the known noise within 1 dB; the host's own estimate reads the drift of a not-quite-coherent 50 Hz record as
  noise, 35 dB high there). A transient lasting most of the record raises it: such a point reads low_snr where the host
  says unsettled, and the device does not retry it. Only flags depend on it.
- READ_BODE rows are 20 bytes (design: 16): u32 M and u32 f in cHz, because 10 MHz does not fit a u32 in mHz. BODE_CTRL
  start / stop are cHz for the same reason.
- CH2 always DC coupled (no `ch2_coupling='ac'`); the background capture is on (BODE_CTRL flags bit 1 turns it off).
- The thru calibration lives in RAM (lost at power-off); stale = start, stop or points differ (ranges are flagged per
  point, cal_range).
- Diagnostics aborts a running sweep (no acquisition while it shows); a picture / waveform viewer closes the view first.

**Not done / for the next agent.**
- mcp: `Cmd.BODE_CTRL` 0x0211 / `Cmd.READ_BODE` 0x0212 (cap DECIM), request / response structs per row 60,
  `scope_bode_sweep(where="device")` (BODE_CTRL action 1 with flags bit 0, poll READ_BODE until state != 1, then the rows;
  recompute f_actual from M; the flags' bit order equals the host's flag names), `DisplayMode` 5, features bit 6. A
  device-vs-host comparison test on the simulator needs the simulator to implement both commands (it could run the C
  engine's logic in Python, or plan + measure_pair).
- D5: harmonic fast mode, impedance mode, thru calibration on the card.
- Restore default settings does not reset the Bode settings words (they keep their values; the marker rule ignores junk).
- Measured numbers the estimate assumes (15 ms per capture) and the 20 ms engine budget per pass: measure `pass_ms` and a
  sweep's duration (READ_BODE header bytes 28..31) on the bench.

**Bench walk-through (maintainer, bench session on <port>; FEL load only, no SD writes; parts on the bench: the RC 1 kOhm / 15 nF
ceramic and the 991 / 102 Ohm divider; wiring A: DUT ground on both probe ground clips; x1 probes)**
1. FEL-load (`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.66,proto=1.20`.
   GET_STATUS bytes 44-45 (features) = 0x7F on 0x1538.
2. Top bar generator chip: the menu has a fourth row "Bode plot" with Open (black line above it). Open: the menu closes,
   the plot shows the wiring text and empty panes with decade labels 100 .. 1M, RUN/STOP reads Stopped, the generator
   chip unchanged until a sweep. Right bar page one: CTRL, RUN/STOP, AUTO SET, SAVE PIC, SAVE WAVE (no cursors, no
   MEASURES). CTRL: Sweep "50 pt", Thru "None", Grid; nothing below. GET_STATUS byte 43 = 5.
3. Divider 991 / 102 (post -> 991 -> node -> 102 -> ground clips; CH1 on the post side of the 991, CH2 on the node): AUTO SET.
   Expect about 4 s, the Sweep card counting to 50/50, |H| flat at -20.6 dB (+-0.2 dB to 300 kHz; the host read -20.62 to
   -20.43), phase 0 +-1.5 deg, no grey points except possibly near 1 MHz; a tap on the plot shows f / |H| / Phase cards.
   Host: `fnirsi1013d-bode --port <port> --yes --start 100Hz --stop 1MHz --points 50` (close the view first: Menu > Scope)
   and compare point by point (expect within 0.05 dB / 0.3 deg where both are valid).
4. RC 1 kOhm / 15 nF (CH2 on the R-C junction): CTRL > Sweep: Start 1kHz, Stop 500kHz (+ / -), Points 20, Run sweep.
   Expect the low-pass shape, the -3dB card near 11 kHz (the host read 11.3 kHz on this capacitor; the ideal 10.6 kHz:
   the ceramic's loss bends it), at 300 kHz about -24.6 dB / -70 deg like the host's rc1_15n_clips_2.json, CH2 splices
   (the Sweep card stays; READ_BODE flags bit 9 on the splice points). Same plan on the host for the comparison:
   `--start 1kHz --stop 500kHz --points 20`.
5. Thru: both probe tips on the RC input node. CTRL > Thru > Measure thru: one sweep with "Thru sweep: both probe tips
   on the DUT input" at the top; then the Thru button reads "On" and the card "Thru applied". Run sweep again (still both
   tips on the input): |H| within +-0.03 dB of 0, phase +-0.2 deg (raw mismatch on the bench was +0.15..+0.30 dB). Apply
   switch off: the raw mismatch shows; Clear: "None". Change Points: the Thru card turns amber "stale" and the warning
   asks to measure again.
6. Hold: CH2 menu 0.5 V/div, Sweep > CH2 range Hold, divider sweep: no splices, the CH2 chip stays at 0.5 V/div. RC with
   Hold at 0.5 V/div: the stopband points under 3 codes are valid but flagged ch2_small (READ_BODE bit 4).
7. RUN: sweeps repeat, each overwriting point by point; STOP: the sweep ends after the point in progress, the points stay.
   AUTO SET while sweeping: "Auto..." then the sweep ends after the point.
8. Leaving: Menu > Scope: the generator is back as before (chip and output), CH1 / CH2 V/div, offsets and coupling as
   before, the time base as before, RUN/STOP as before; the scope traces return. Enter again: the last table shows; the
   Sweep settings were kept. Power cycle: the Sweep settings are kept (words 219-220), the thru is not.
9. Host commands (with the MCP closed; Python, `fnirsi1013d` installed):
   ```python
   import struct, time
   from fnirsi1013d.scope import Scope
   with Scope.connect("<port>") as s:
       r = s.t.request(0x0211, struct.pack("<BBBBIIHH", 1, 1, 1, 0, 100000, 50000000, 20, 0)).payload  # 1 kHz-500 kHz/20
       print("estimate ms", struct.unpack_from("<I", r, 32)[0])
       while True:
           h = s.t.request(0x0212, struct.pack("<HHI", 0, 0, 0)).payload
           if h[4] != 1: break
           time.sleep(0.2)
       print("state", h[4], "reason", h[5], "points", struct.unpack_from("<HH", h, 8), "ms", struct.unpack_from("<I", h, 28))
       for i in range(struct.unpack_from("<H", h, 8)[0]):
           m, f, db, ph, v1, v2, r1, r2, fl = struct.unpack_from("<IIhhhhBBH", h, 32 + 20 * i)
           print(f / 100, db / 100, ph / 100, v1 / 100, v2 / 100, r1, r2, hex(fl))
   ```
   Expect state 2, the same table as the screen, the elapsed time near the estimate. Action 4 aborts at once, action 10
   closes the view (everything restored).
10. Wiring B check of the overload rule (optional): DUT ground on the USB shell, clips open, CH2 0.1 V/div Hold: the sweep
   stops at once with "CH2 saturated by the DUT's DC level ..."; Auto: CH2 stays on a less sensitive range and the points
   are valid.
Bug indicators: any trace or grid of the scope view drawn over the Bode plot (UI acquisition not bypassed); a capture that
never ends (Sweep card stuck, READ_BODE state 1 for long; reason 2 afterwards); |H| of the divider off by a constant
(scale per channel wrong: gain16 / uv_q8 / probe); phase drifting with frequency on the divider (record start or core
order); the generator, V/div or time base not given back on leaving; a relay clicking at every point (bh_channel writes
only what changed); MEASURES or the cursors reacting in the view; the view drawing over a waveform viewer.

## 43. fw-cal5: the wizard's zero step on FPGA 0x1538, CAL_PROBE (fw 0.2.67, protocol 1.21 provisional, row 61)

Bench fact (bench session, docs/hardware-status.md 2026-10-19 17:55-18:40). On fw 0.2.64 / FPGA 0x1538 with nothing
connected, the Zero level step failed every time: "CH1: too much noise, range 1 at 200 MSa/s: core B 7.2 codes, 7.2 per
capture" and "CH2: the input is not at 0 V". At the same moment host READ_RAW on range 0 at 200 MSa/s was clean.

**Cause, confirmed with CAL_PROBE and a READ_RAW offset sweep.** At 200 MSa/s some FPGA readout lanes mix the bits of two
successive samples when they straddle 0x7F / 0x80. Outliers up to 255 appear only while a core's mean sits at 126..133
(CH2 both cores, CH1 core A once). Every other mean is clean, and so is the gated rate. The zero step parked the mean of
both cores exactly at 128. The host's READ_RAW sat a few codes off it, mostly outside the band.
- The data are bad in the bus readout, not in the ADC data. The 0x33 encode check cannot see it.
- The MCU-side early read is ruled out: CAL_PROBE settle 4 changed nothing. The prestrobe A/B made it worse.
- The fault depends on the lane and on the build: 0x1535 passed in the 0.2.48 flow.
- The likely cause is a hold race between BRAM dob and o_mcu_data on the unconstrained i_mcu_clk. fpga-gen8 owns the
  bitstream fix (registered output byte, constrained MCU clock).
- A Cancel-poll / touch interference theory was checked and ruled out. The touch is on Port A, the FPGA bus on Port E.
  wiz_poll_cancel runs only between ranges. The host probe passes through the same fpga_done_conversion touch scan.

Branch `worktree-agent-a8a5d87924697dbf0` from main a25987a (0.2.64), merged with origin/main 802c203 (fw-bode 0.2.66 /
1.20, fw-meas 0.2.65) in ae1187e.

Commits:
- 28158ea `acal_point`, extremes, `acal_fail_point`, `acal_probe_pack`.
- 789da66 `cal_range`, the failure note, cal_end's raw divider and time base.
- 448c08c CAL_PROBE, protocol 1.21, 0.2.67.
- a651c0c `firmware/tools/cal_probe.py`.
- cf3f9e2 settle A/B.
- b67219c prestrobe A/B, mode 1, the E_BUSY fix.
- e5ccbd3 docs.
- ae1187e the merge.
- **c22896c, the fix**.
- 8802b79 row 61 and the vector.
- 8329d63 CHANGELOG and README.
- Then this handoff.

**What was built**
- **Fix (c22896c).** Each class's zero is no longer measured at the centre PWM. It is the mean of two points d counts on
  either side of it:
  - Conversions: 8 per point gated, 64 per point at 200 MSa/s.
  - d comes from the range's gated slope points, so each point sits 12 codes off 128 (`acal_zero_split`: 8..120 counts;
    about 27 counts on range 0 of the maintainer's unit).
  - In the linear model the mean of the two points is exactly the zero at the centre (`acal_zero_pair`). So the stored
    zeros, slopes, PWM centres, `acal_correction` (v3), the v4 tracker and GET_CAL mean what they meant. Nothing in the
    block or the protocol changes for this.
  - Drift comes from the halves of both points: the level step cancels because both points take equal conversions.
  - Noise and within-capture RMS are the RMS of the two points' own values.
  - Cost: 8 gated and 64 full-rate conversions more per range, plus one 60 ms settle. That is about 0.15 s per range.
- **Readout fault reported as such.** ACAL_E_RESPONSE stays 4 and ACAL_E_TIMING stays 18; the new code is
  ACAL_E_READOUT = 19.
  - `cal_range` turns an ACAL_E_CLIP into ACAL_E_READOUT when `acal_readout_fault` holds. That means every clipping
    point is a 200 MSa/s point, and its gated twin at the same PWM is quiet and within 8 codes of 128. Any gated clip
    means the cause is the input.
  - Wizard text: "ADC readout fault near code 128 (FPGA 0x1538): flash the fixed bitstream". Short form: "ADC readout
    near 128".
  - With the zero pair the zero points no longer sit at 128, so this mainly catches a unit whose response is far off.
- **Failure note.** Under a failed channel's result line there is a 13 px line built by `cw_zero_note`, for example
  "Range 1, 200 MSa/s, zero point, PWM 812: A 130.1 (126..135), B 125.9 (0..255, 12 clip)".
  - Content: the point (`acal_fail_point`), its PWM, and per core the raw mean, lowest..highest code and samples at 0 or
    255.
  - `ui_step.note[2]`: with a note present, the results move to y 148 + 38 i and the note sits 18 px below its result.
  - Widths are tested at the worst case (PWM 1950, 32000 clipped).
- **CAL_PROBE 0x0402** (docs/protocol.md row 61, request vector `req_cal_probe`).
  - Request: u8 channel, u8 hardware range, u8 settle 0..15, u8 prestrobe 0/1, u8 mode 0/1.
  - Response: 252 bytes. Eight `acal_point`s and the header (rc, fail point, class, core, value, within, centre, zero
    PWM, encode-check deltas, flags), then the zero split d at 248.
  - Mode 0: one range through `cal_begin`, the scratch channel, the relay and `cal_range`, then `cal_end`.
  - Mode 1: the wizard's whole zero step in its screen context. It runs `cw_init`, the dimmed screen and dialog,
    CW_EV_CALIBRATE and `wiz_run_actions`. The response carries the given channel's last measured range.
  - Conditions: main loop only, UI stopped (E_STATE detail 1 otherwise), no host capture pending.
  - The first 1000 bytes of both trace buffers are put back afterwards.
  - The session's modal watch restarts after the probe (`last_main_ms`, `ui_busy`). Without that, the 0.2.67 diagnostic
    image answered the next request with E_BUSY. Host test in test_session `test_cal_probe`.
  - `fpga_read_settle` and `fpga_read_prestrobe` (fpga_control.c) are 0 except inside a probe.
- `cal_end` now also writes back a raw host divider and the time base, as the class probe does.
- `firmware/tools/cal_probe.py`: arguments port, CH1 / CH2 / both, ranges, repeats, settle, prestrobe, mode. It stops
  the UI for the run, prints every point and saves backups/calprobe_*.json.

**Tests**
- test_adc_cal 59859:
  - `test_points` covers extremes, the failure point and the 252-byte body.
  - `test_readout_fault` models the mixing readout (bit 7 new, bits 6..0 of the previous sample when they straddle
    127.5). The parked point clips, reaches 255 and fails the noise limit. The split pair through the same readout is
    clean, the zero is within 1/8 code, and drift is under 1/8 code. It also covers the classification rules.
- test_cal_wizard 664: the note texts and widths, and the READOUT line width.
- test_session 69865 / 69863.
- check_vectors OK (94).
- mcp pytest 398 passed.
- Warnings: GCC 10 and nommu 36 (sorted list identical to a25987a); GCC 13 / PC_DEBUG 31.

**Images.** From 8329d63, GCC 10, in this worktree's `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin`:
- 691,200 B, sha256 64159a52b1f8294daf26d1260a8fe74477d520a819ae2922e78a08deb5fc5a0b.
- nommu 3392db632f86ef3b39d1fc6db2024e17a2cedede139563b7ebfef4cd2531bffe.
- `#VER fw=0.2.67,proto=1.21`.
- WSL cannot resolve the worktree's gitdir, so the identity carries no hash; a build on main differs in the identity only.
- Diagnostic images loaded on the bench before the fix: f17e2d70 (cf3f9e2) and 7b881451 (b67219c), pinned by the
  bench session.

**Bench check** (FEL-load only, nothing written before Save; back up SD 700-710 first because Save writes 708 / 709):
1. `#VER fw=0.2.67,proto=1.21`. Run `python firmware/tools/cal_probe.py <port> both all` with probes, post and generator
   off.
   - Every range OK on both channels.
   - The "gated zero pair" and "full zero pair" rows print the two points' average as the mean (near 128), with "+- d"
     about 20-45 counts in the header line, and lowest..highest about 112..144 (both points together).
   - No clip at any point after the coarse ones.
2. Mode 1 on both channels: `python firmware/tools/cal_probe.py <port> CH2 0 1 0 0 1`. CH2 ends at range 5 (index 5) with
   OK.
3. The maintainer runs the wizard: Zero level gives two ticks, then the gain screens and Save. GET_CAL then shows CH2 A - B
   near the READ_RAW means' difference.
4. Optional regression check of the classification: none needed on hardware. If a unit still shows "ADC readout fault
   near code 128", the fault reaches 12 codes off 128: report the note's numbers to fpga-gen8.

**Open**
- The UI's own 200 MSa/s traces near code 128 still carry the outliers on 0x1538; that is the bitstream fix (fpga-gen8).
  Once it is flashed:
  - re-run CAL_PROBE mode 0 with the zero parked: needs a temporary `acal_zero_split` = 0 build, or READ_RAW at dcoffset
    0, to confirm;
  - keep the pair anyway (it costs 0.15 s per range and protects any build).
- mcp (do not edit from this branch): protocol.py `Cmd.CAL_PROBE = 0x0402`, `PROTO_MINOR = 21`, a decoder for the 252
  bytes (cal_probe.py has one), ACAL_E_READOUT = 19 in its reason names.
- The 0.2.60 per-capture DATA check (0x1532..0x1537) could also see such outliers as garbled; not relevant on 0x1538
  (the 0x33 check).
## 44. fw-polish2: UI polish batch 2 (fw 0.2.68 provisional, protocol 1.20 unchanged)

Branch from main b5698c4 (fw 0.2.65), merged with origin/main twice (f2a0ca2: fw-bode 0.2.66 / protocol 1.20; 15c5e56).
Brief: the maintainer's walk-through of 0.2.65 (2026-10-19 18:00), items 1-5, plus the bench session's item 6 (maintainer decision
18:45: Bode plot as a main-menu mode row). User-facing description: firmware/README.md "Screens: UI polish batch 2 (fw
0.2.68)". Commits: 95c1033 (1 RUN/STOP), ec9f319 (2 icons), a9daba6 (3 time/div snap), d51dcb7 + ee754c7 (4 brightness),
3dc0ed9 (5 wizard step, Calibration row), 67d5204 (version, CHANGELOG, README), 1972245 (6 Bode row), the merges, this
handoff. Host tests green (test_ui 1,420,883 checks, test_tb_view 530,790, test_ui_diag 5516, the rest unchanged,
check_vectors OK, CHANGELOG check OK). Warnings: GCC 10 36 (gcc10 and gcc10-nommu), GCC 13 / PC_DEBUG 31, all in the
pre-existing places (none new). **Nothing benched.** No protocol change (docs/protocol.md row 34 gains one sentence: the
screen's STOP during a host single aborts it). **Settings sector:** 1.00o5's max-light byte (OTHER_SETTING_OFFSET, after
new_autosetup) now holds `0x80 | brightback` (item 4); no new words.

Points for a successor:
- **1 RUN/STOP.** The maintainer's picture (chip red STOP, button amber "Stopped", trace updating) is what 0.2.65 showed while
  the PC captured with ARM: a host single stops the UI (`pc_hal_ui_set_run(0)`, row 34) and the screen shows each host
  capture, so the scope looked running with both controls saying stopped (`pc_hal_ui_set_run` already redrew both on
  SET_ACQ_STATE / STOP). Now `ui_run_view(runstate, single_wait, host_busy)` (ui_ctrl.c, pure) is the one state;
  host_busy (`pc_glue_host_busy`) = a host ARM armed / triggered, or DONE less than `UI_RUN_HOST_HOLD_MS` (1000) ago, so
  an averaging burst or the host's Bode sweep reads RUN without flicker. `scope_run_stop_text` / `scope_run_stop_button`
  draw from `scope_run_view()` and record what they drew (`scope_run_chip_shown`, `scope_run_button_shown`);
  `pc_run_follow` (pc_glue_ui_service, every pass) redraws a stale one (not in the file viewer; the button only on page
  one and not in the waveform viewer), so every writer is covered without a hook at each. The button shows the action:
  green "RUN" (black text) while stopped, red "STOP" (white text) otherwise, 16 px font; maintainer exception recorded in
  docs/ui-guidelines.md section 3. The tap does what it reads: STOP clears runstate and `pc_glue_host_stop` aborts a host
  single (`pc_ui_host_stop` -> `pc_host_abort`: ABORTED, a pending WAIT answers E_ABORTED); before, a tap during a host
  single started the UI's RUN. Chip text is centred (equals 1.00o5's offsets 1 / 5 / 2; test_ui checks it). Known: at a
  host ARM the chip shows STOP for one pass (pc_hal_ui_set_run(0) draws before acq_state is ARMED), then RUN.
- **2 Icons.** Settings: `Other_settings_icon` at (MAIN_MENU_XPOS + 15, y + 6); the drawn cogwheel is gone. Scope: sine
  at y + 9..28 of the button (18.5 - 9.5 sin), axes `UI_MM_AXIS_COLOR` 0x707070 at y + 19 and x + 12, drawn first.
- **3 Time/div snap.** I could not find, by reading, a state the 0.2.65 snap left different from a manual ACQ-menu set:
  the ACQ menu's time/div box only set `timeperdiv` and clamped the trigger point, which is what the snap did; 0x0E and
  the geometry are written at every arm. Done as briefed anyway: `tbv_set` / `tbv_fit` (tb_view.c, pure; tbv_fit calls
  tbv_set) and `acq_view_set` (index + trigger point, then `display_data_done = 1` while running so the next pass arms a
  fresh conversion instead of reading the one armed with the old geometry), used by both `acq_view_set_tb` (the ACQ
  box) and `acq_view_fit`. test_tb_view `test_snap_is_manual_set` sweeps every rate x depth x trigger sample x start
  time/div x trigger point: fit == set, the result viable and inside the limits, a second fit / set a no-op (nothing in
  this state can cycle). **Inference to check on the bench:** if the maintainer's "by hand" was a plot tap,
  `scope_set_timebase` in RUN also sets the sample rate from `time_per_div_sample_rate` (10 us/div -> 10 MSa/s), so the
  "fix" was a rate change, and the cycling belongs to the high rate with deep memory (candidates: the full-rate capture
  check's discard / re-arm, enc_heal; the firmware auto deadline with an untriggered window), not to the snap.
  Walk-through step 6 tells them apart.
- **4 Brightness.** 1.00o5 kept `maxlight` (flag) beside `screenbrightness`: the sun wrote 60000 (= 100 % in
  `fpga_set_translated_brightness`'s 560 v + 4000) or the translated value, and opening the slider switched max light
  off. Now `screenbrightness` is the only value and `brightback` (was the `maxlight` field) what the sun returns to. Pure
  model in ui_settings.c: `ui_bright_sun` (at 100: back, or 20 when back is 100; else remember and 100),
  `ui_bright_byte` / `ui_bright_load` (0x80 | back; an old byte 1 = max light on loads as 100 % with back = the old slider
  value; > 100 heals to 20). `scope_set_brightness(v, sun)` (menu.c) writes the backlight and redraws the sun (not from
  the full-screen Settings page, which redraws it on exit when the value changed). Sun yellow at 100 %, white below. The
  maintainer's "two fixed values" read as 1.00o5's pair: maximum and the slider's value.
- **5** `wiz_dlg.step` ("Step M of N": 4 with the generator, 3 without, the save screen then being step 3) is set next to
  the title in `wiz_build` (cal_wiz_ui.c title line only; fw-cal5's measurement code untouched). Calibration tab:
  `UD_CAL_BTN_X(i)` / `UD_CAL_BTN_W` 185, row at `UD_BTN_Y` 420, trim line and note at `UD_CAL_TEXT_Y` 374 / 394.
- **6 Bode row.** `UI_MM_BODE` 2, eight rows, `UI_MM_PITCH` 52, `UI_MM_BTN_DY` 9 (button 131 x 35 unchanged, 8 px to the
  separator), `MAIN_MENU_HEIGHT` 416 (y 46..461). `ui_mm_choose` row Bode: bode 1, spectrum 0, phosphor 0, roll_off in
  roll mode, no roll note (the note is the spectrum view's). `pc_glue_main_menu_mode` closes the view on any other mode
  row and opens it after the spectrum / phosphor / roll changes. Grey icon and label, tap ignored (the menu stays open)
  while `bode_view_available()` is 0. Removed: the generator menu's fourth row (`GEN_MENU_HEIGHT` back to 169,
  `bode_gen_row_*`, `PC_GEN_BODE_*`, `bode_view_request` and `bv.req`). Per the bench session the Bode view's right bar and
  CTRL page are untouched (ux-bode mockup, separate batch). Section 42's walk-through enters the view with Menu > Bode
  plot now.

Answers for the maintainer:
- **1.00o5's "Pro" trace style was not Linear.** `scope_display_channel_pro_trace` (removed in 8d508cd, polish batch 1
  item e) took, per screen column, the minimum and the maximum of all samples falling in that column and drew two
  polylines: max to the previous column's max, min to the previous column's min, with no vertical line between them.
  Peak takes the same min / max per column but fills it with a vertical line (a solid band); Linear draws one sample per
  column. So Pro = Peak's envelope as two outlines. It looks like Linear when a column holds one sample or less (zoomed
  in) or the signal is clean (both outlines within a pixel); on noise or a fast signal compressed into the screen it
  showed the band's top and bottom edges. (It stored two points per column, overflowing the 730-entry point buffers
  until fw-acq's 0.2.64 fix.)
- **Atlan's quick-settings fold-out and its Other settings (menu.c at 9cc599c^), and where each control is now:**

| was (fold-out) | now |
|---|---|
| Screen brightness (slider) | fold-out Screen brightness (same slider); Settings > Display > Screen brightness (- / +); one value with the top bar sun since 0.2.68 |
| Grid brightness (slider) | fold-out Grid brightness; Settings > Display > Grid brightness |
| Other settings (the sub-menu below) | fold-out Other settings opens the full-screen Settings page |
| Baseline calibration (then 1.00o5's input calibration boxes, 300 mV .. 15 V) | Diagnostics > Calibration > Calibrate (the wizard) and Volts calibration (the six DC levels); Clear calibration beside them |
| X-Y mode display | CTRL page > Display > X-Y |
| Notification confirmation | Settings > Controls > File message timeout ("wait for a tap" / "0.7 s") |
| RTC settings: Time & Date (hour, minute, day, month, year, RTC on / off) | fold-out Time and date (same editor, with the clock module); Settings > Clock > Clock and Time and date |
| (trigger 50 % row: commented out in Atlan, never shown) | CTRL page 50 % TRIG |

| was (Other settings) | now |
|---|---|
| Boot menu, Default start (PECO / FNIRSI / FEL) | Settings > Start-up > At power-on: Open firmware / Boot menu, then open firmware / Boot menu, then stock firmware. FEL: ENTER_FEL from the PC, the boot menu's FEL, Diagnostics > Developer > Enter FEL |
| New autosetup | Settings > Controls > Auto setup (improved / stock) |
| Lock move cursors | Settings > Controls > Cursors (move together / move freely) |
| DC shift calibration | Diagnostics > Calibration > DC shift trim (the trim itself on the CTRL page while on) |
| USB-Serial mode | removed: the build forces CDC (`PC_FORCE_CDC`); the setting did nothing visible |
| Dev mode | no switch; the flag stays in the sector, Diagnostics > Developer offers "Debug overlay / Switch off" only while it is set |
| Add a tag to BMP | Settings > Files > Stamp screenshots with (time / file name / nothing) |
| Load Ref on start | Settings > Files > Reference traces at power-on |

  Nothing of the two menus is lost except USB-Serial mode (no effect in this build) and the Dev mode switch (by the
  0.2.63 decision).

**Bench walk-through (maintainer, bench session on <port>; FEL load, no SD writes)** - `#VER fw=0.2.68,proto=1.20`. CH1 on the
1 kHz calibration square, 1X, DC, Auto trigger, Scope view.
1. RUN/STOP: running, the chip reads green RUN and the right-bar button red "STOP" (white text). Tap: chip red STOP,
   button green "RUN". Tap: back. Trigger mode Single: chip orange WAIT, button red "STOP"; after the capture both show
   stopped (chip STOP, button RUN).
2. From the MCP: `scope_acquisition_control` "stop": both change at once; "run": both change back. Then several
   `scope_capture` calls (or `scope_averaged_capture` n=16): while the PC captures, chip RUN and button STOP, steady;
   about 1 s after the last capture both show stopped. During an averaged capture tap STOP on the screen: the host call
   reports an aborted capture, the scope stays stopped. Repeat 1-2 with Diagnostics open (Back shows the right state) and
   in the spectrum view.
3. Menu: eight rows (Scope, Spectrum analyser, Bode plot, Saved pictures, Saved waveforms, Settings, Diagnostics, USB
   drive), nothing clipped at the bottom, every label inside its button. Scope icon: a sine over a grey cross, nothing
   cut at the top. Settings: the spanner and screwdriver of Other settings. Bode plot: two falling lines. Tap each row
   near its top and bottom edge: that row reacts.
4. Bode plot row (0x1538): opens the Bode view; the generator menu has three rows (Power, Frequency, Duty), no Bode row.
   Menu > Scope leaves the view (generator, channels, time base given back), Menu > Spectrum analyser too. From roll
   mode, Bode plot switches roll off. In the waveform viewer the row is grey and a tap does nothing (the menu stays open).
5. Brightness: tap the sun: full bright, the sun yellow. Menu > Settings > Screen brightness: slider and value read
   100 %. Drag to 40 %: the sun turns white as the value leaves 100. Close; sun: 100 %; sun again: 40 %. Other settings
   page: Screen brightness - / + changes the same value; Back: the sun matches (yellow only at 100). Power-cycle at 100 %
   set by the sun: comes up at 100 %, the next sun tap returns to 40 %.
6. Time/div snap (maintainer's repro): ACQ menu, a slow time/div at a low rate with deep memory, then a high rate: the
   notice "Time/div set to X for this rate"; close the menu, watch 10 s: expected stable. If it still cycles: note the
   ACQ menu's rate, then set time/div by hand in the **ACQ menu** (5 us, then 10 us). If that does not settle it but a
   **plot tap** does, read the rate after the tap (plot taps in RUN couple the rate to the time/div): the cause is then
   the rate / depth combination, not the snap (successor note, item 3).
7. Diagnostics > Calibration: four equal buttons on the bottom row: Calibrate (green), DC shift trim (grey / green),
   Volts calibration, Clear calibration (red) at the right. Tap DC shift trim: "On: CTRL on the scope opens the trim."
   above the row. Calibrate: the wizard's title line ends in "Step 1 of 4" (Continue: 2, 3, 4 of 4); Cancel.
Bug indicators: chip and button disagreeing; the button reading STOP with the scope idle for more than a second; a STOP
tap during a host capture that starts the UI's RUN; a main-menu tap selecting the neighbouring row; a Bode row in the
generator menu; the sun yellow below 100 % or the slider not at 100 % after the sun; the snapped time/div cycling.

## 45. fw-fr: Frequency response mode UI, ten samples per division (fw 0.2.69 provisional, protocol 1.21 unchanged)

Branch from main 71b026e (fw 0.2.68), merged with origin/main several times (bode-ui.md's final decisions e4452d9, 4b86769,
the icon de14622, the slider be0c93e / 6d849f6). Brief: implement docs/plans/bode-ui.md with the maintainer's decisions of
2026-10-19 evening (they override the document's earlier drafts; the document now records them), plus the tb_view
viability rule. User-facing description: firmware/README.md "Frequency response". Commits: 070e250 (ten samples per
division), 7772b64 (`bode_auto_span`), bbf12f1 (`bode_ui.c`, MEASURES list, ui_p1_shown, menu row name), 5d262b7 (the
view), 70d3dec (wiring, 0.2.69), then docs and this handoff. Host tests all green (test_ui 1,423,013 checks incl. the new
test_bode_scales / readout / cards / texts, test_bode_sweep 199 incl. test_auto_span, test_tb_view 580,345, test_bode.py,
check_vectors, CHANGELOG check). Warnings GCC 10 36 (gcc10 and gcc10-nommu), none in the new or changed code; bode_ui.c is
clean with the host tests' -Wall -Wextra -Werror, bode_view.c with arm-none-eabi-gcc -Wall -Wextra. **Nothing benched.**
**No protocol change** (the CSV and the cursors are screen-only; docs/protocol.md names display mode 5 "Frequency
response"). **Settings words claimed: 213..215** (the MEASURES list: 0xE000 marker and three 4-bit ranks in 213, four in
214, three in 215; anything else gives the defaults); word 220 gains bits 3..4 (Display variant), 5 (cursors), 6
(frequency axis), bit 7 must be clear (an erased word reads 0xFFFF). 216..218 stay free.

Points for a successor:
- **bode_ui.c/.h** (pure): the chart geometry (pane y 60..396, six divisions of 56 px; labels y 400; cards y 426; slider
  452..471), `bu_scale_pick` (the first division that holds the data with the free top division, else the first that
  holds it at all: bode-ui.md 9 C3, so 55 or 60 dB of data goes to 20 dB/div), log / linear x, `bu_lin_step`, the
  readout rows (`bu_readout`: C1, C2, C2-C1; a BODE_F_BAD point shows its reason instead of the phase and greys its row
  and the difference; spliced points are valid), the MEASURES list (`bu_ms_*`, ranks as ui_meas.c's), `bu_card_text`
  (no sweep / sweeping / not found / the value; Thru applied green, stale amber), the warning table `bu_warn[]` (all
  <= 50 characters, test_ui checks), the instruction, the slider maths (`bu_slider_x` of a step index, `bu_slider_shift`).
- **bode_view.c**: one `draw_all` into displaybuffertmp (pane, labels, curves, markers, the measured point's line,
  cursors, warning or instruction, cards, slider). `table_state`: FILLING only while running with unmeasured points
  (markers hidden; cards "sweeping"); a stopped partial table counts as complete. AUTO SET: `bv_auto_start` (aborts a
  running sweep, turns the thru's Apply off for the overview and gives it back), `bv_auto_finish` in the service when the
  overview ends (STOPPED = cancelled), a second tap aborts. Single sweep while RUN repeats sets `bv.single` (the sweep in
  progress becomes the last). Cursors: `bv.cur[2]` re-placed (a third / two thirds of the axis) whenever invalid or
  outside the span; reset when the plan changes. Slider: `slider_drag` (blocking while the finger is down, as the other
  plot drags), `bv.drag` makes the axis follow the settings; release restarts or runs one sweep. CSV: `bode_view_save`
  (24 KB static buffer, `bode/FRnnnnn.CSV` with FA_CREATE_NEW from 00001; its own file message box in the stock style).
- **Wiring**: menu.c (`scope_main_menu_bode_icon(y, fg)` from bode-ui.md 1a; V CURSOR drawn separately from T CURSOR;
  MEASURES `UI_MS_BODE`: neutral Select All / Clear, two `UI_MSC_EMPTY` places, no channel split), statemachine.c (T
  CURSOR and SAVE WAVE in the mode; the grey row's reason drawn at the top middle, it stays until the trace area redraws),
  pc_hal_scope.c (words 213..215 restore, `bode_view_defaults` from `pc_glue_settings_defaults`, the mode's roll note),
  scope_functions.c (words 213..215 saved).
- **Not done (queue them):** (1) the top bar in this mode, bode-ui.md section 7: ACQ chip "Set by / the sweep", the
  generator chip showing the tone being measured (the engine writes the FPGA generator directly; `fpgasettings.gen_freq`
  is untouched, so the chip shows the user's generator), the menus' notes. (2) The Spectrum view's slider (bode-ui.md /
  ux-makeover: window = shown span over 0..fs/2, drag pans the centre, plot 8 divisions of 44 px y 50..402, labels y 406,
  cards y 426): not started. (3) A cancelled AUTO SET leaves the partial overview on screen; the design says the previous
  table comes back (the engine has no copy; it needs one of `pts` + `plans` + `cfg` + `np`, about 25 KB). (4) Move fast
  does not change the cursor drag step (cursors snap to points). (5) The MEASURES menu's changes show at the view's next
  redraw (at most 1 s), not at once. (6) The overview's Q is a lower bound when the band is narrower than its point
  spacing; such tanks get half a decade and Settle Resonant, a Q 50 tank on the overview may need a manual narrower span.
- **Ten samples per division** (tb_view.h `TBV_MIN_SAMPLES_PER_DIV` 10): 200 MSa/s now reaches 50 ns/div; 20, 10, 5 ns/div
  are never viable (the ACQ menu greys them, a tap zoom or SET_TIMEBASE snaps to 50 ns/div with the notice). The spectrum's
  time bases 32..34 share 200 MSa/s with 31, so its rate never depended on them. test_tb_view: 74 cells now differ from
  1.00o5's table (71 below ten samples).

**Bench walk-through (maintainer, bench session on <port>; FEL load only, no SD writes; parts: the RC 1 kOhm / 15 nF ceramic and
the 991 / 102 Ohm divider; wiring A, DUT ground on both probe ground clips; x1 probes)** - FEL-load
(`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.69,proto=1.21`.
1. Ten samples per division: Scope, ACQ menu, 100 kSa/s: the time/div boxes below 100 us are grey; from 10 us/div the
   rate change snaps to 100 us/div with "Time/div set to 100us for this rate". 200 MSa/s: 50 ns/div is the fastest box.
2. Menu: the third row reads "Frequency" over "response", the icon a flat line, a knee and a 45 degree roll-off over a
   grey L. On a 0x1532 bitstream (or in the waveform viewer) the row is grey; a tap shows "Frequency response needs FPGA
   0x1534 or later" ("Leave the waveform viewer first") in orange at the top middle; the menu stays open.
3. Open it (0x1538): STOP, the empty pane 100 Hz .. 1 MHz with "+10 dB" .. "-40" in cyan on the left and "+135 deg" ..
   in pink on the right, the orange two-line instruction at the top middle, cards "-3dB no sweep", "Peak no sweep",
   "Q no sweep", "Level no sweep", the slider's window over 100 Hz .. 1 MHz. Right bar: CTRL, RUN (green), AUTO SET,
   T CURSOR, (empty), MEASURES, SAVE PIC, SAVE WAVE. CTRL: Display "Overlay", Sweep "50 pt", Thru "None", Grid at the
   fourth slot; nothing below.
4. RC (post -> 1 kOhm -> junction -> 15 nF -> clips; CH1 on the post side, CH2 on the junction): AUTO SET. "Auto..." and
   the Sweep card "auto n/20" over 100 Hz .. 10 MHz (about 2 s, more if the 15 ms per capture is optimistic); then the
   axis becomes 1 kHz .. 200 kHz, RUN (red STOP button, chip RUN), Settle Normal, CH2 Auto; the cards read "sweeping"
   until the table is complete, then "-3dB" near 11 kHz (the host read 11.3 kHz on this capacitor), "Level" near 0 dB,
   "Peak" / "Q" "not found". The magnitude stays below the top division (the top label one division above the data).
5. T CURSOR: C1 and C2 flags above the pane at a third and two thirds; the readout box at the bottom left lists C1, C2
   and C2-C1 (f white, dB cyan, deg pink). Drag a flag: its cursor follows, snapping to points; a tap in the plot moves
   the nearer cursor. Put C1 near 1 kHz and C2 near 100 kHz: about 0 / -19 dB, the C2-C1 row about -19 dB / -80 deg. A
   grey (flagged) point under a cursor shows its reason instead of the phase. T CURSOR again: both go.
6. Slider: drag the window right by about a decade: Start / Stop step together (CTRL > Sweep shows them), the axis follows
   with the old points inside the span; release: the sweep restarts at the new span (RUN repeats). STOP, drag back:
   release runs one sweep and returns to STOP. During AUTO SET's overview a drag does nothing.
7. CTRL > Display: Overlaid (green), Magnitude only (the phase curve and the right labels go), Phase only; Frequency
   axis: Log -> Linear (1-2-5 labels, the points bunch at the left over 1 kHz .. 200 kHz), back to Log. CTRL > Sweep:
   Single sweep reads "about N s"; while RUN repeats, tap it: "running, then STOP" and the sweep in progress is the last.
8. MEASURES: "Frequency response measurements", Select All / Clear in grey, the ten items in three rows, -3dB 1, Peak 2,
   Q 3, Level 4. Select BW and Ph45: their cards follow (Ph45 on a pink block). Select All: "+5 more" in the sixth place.
9. SAVE WAVE: "Sweep table saved" / "bode/FR00001.CSV"; a second save: FR00002. On the PC (USB drive) the file has the
   `#` lines and one row per point; compare with READ_BODE (section 42 step 9's script). Before any sweep: "Nothing to
   save: run a sweep first".
10. Divider (991 / 102): AUTO SET: "No corner from 100 Hz to 10 MHz: span kept" for 5 s, RUN at the kept span, flat
    -20.6 dB / 0 deg (5 dB per division, the data in the second division). CH2 tip off the DUT (on the ground clip):
    AUTO SET ends with "No response: CH2 tip on the DUT output?" and STOP. AUTO SET, then tap it again during the
    overview: "Auto set cancelled: settings kept", STOP.
11. Thru as in section 42 step 5 (Thru card applied green; change Points: stale amber and "Thru does not match this
    sweep: measure again"). During AUTO SET's overview the thru is not applied; afterwards its Apply is back.
12. Power cycle: Display variant, frequency axis, cursors on / off, Start / Stop / Points / Settle / CH2 range and the
    MEASURES choice are kept. Settings > Restore default settings: Overlaid, Log, cursors off, 100 Hz .. 1 MHz / 50,
    the default cards. Leave via Menu > Scope: everything given back as in section 42 step 8.
Bug indicators: data drawn in the top division while the scale had room; a curve in the wrong colour or a label column on
the wrong side; markers while "sweeping"; a cursor reading between points; the readout box beyond x 262; the slider window
off the track or a drag that changes the span's width; a sweep that does not restart on release; AUTO SET changing
settings after a cancel; a CSV row count different from the measured points; the T CURSOR / SAVE WAVE buttons acting on
the scope's cursors / waveform files in this mode; a time/div under ten samples per division offered anywhere.

## 46. READ_SCREEN (fw-screen, fw 0.2.70, protocol 1.21 unchanged)

Branch `fw-screen` from main 6c1e5bc. Commits: 350823e (handler, HAL, cap, host test, vectors on both sides,
mcp/tests/test_read_screen.py), 59d93d8 (scope_screenshot description), then docs and this section. PC_FW_VERSION not
bumped (the bench session numbers it). Host tests green (test_session 69,896 / 69,894 checks, check_vectors 98 vectors,
Python vector check ok, mcp pytest green apart from test_report, which needs matplotlib, absent from the local venv).
GCC 13, GCC 10 and PC_DEBUG builds: no warning outside the upstream list. **Nothing benched.**

- `h_read_screen` (pc_cmds.c): u16 row, u16 nrows; E_BAD_ARG detail 0 "rows outside 0..480" for row >= 480, nrows 0 or
  row + nrows > 480; E_OVERFLOW detail 2 above 41 rows (both as the simulator's `_cmd_read_screen`); then the 8-byte
  header (800, row, nrows, format 1) and the rows. Flags F_BASIC, not F_BUSY: main loop only, so a screenshot never
  catches a UI pass half drawn and the 1 ms tick never copies 64 KB; while a modal menu is open the request gets E_BUSY
  (the agent guide already says so). Cap SCREENSHOT is set with the shared-mode caps on every known bitstream (not on
  an unknown one, whose caps are 0 by the fail-closed rule).
- `pc_hal_screen_rows` (pc_hal_scope.c): one memcpy from `maindisplaybuffer` + row x 800, the buffer DEBE layer 0 scans
  (`sys_init_display`; `DEBE_LAY0_ATT_CTRL1_RGB565`, line width 800 x 16 bits, so no row padding). Views that draw into
  `displaybuffertmp` copy into it, so it holds what the panel shows. With the MMU on its pages are write-through
  (section 26): the cached copy equals DDR, no cache maintenance. A compile-time check ties PC_SCREEN_WIDTH / HEIGHT
  (pc_hal.h) to SCREEN_WIDTH / HEIGHT; GET_INFO bytes 72..75 use the same constants.
- Mock (host_tests/mock_hal.c): `mock_screen_pixel` puts the x position in every pixel (row mod 16); every byte is
  0x80..0xFF so the frame vector made from it (`resp_read_screen_last_row`) holds no "#" for the C request parser's
  ASCII path (a large response frame with "#" in it gives ascii_overflow events in C and none in Python: a known
  divergence for response frames the device never receives, left alone).

**Bench step (bench session + maintainer; FEL load only, no SD writes):** FEL-load `build-gcc10-pc/fnirsi_1013d.bin`, then
`scope_info`: caps include SCREENSHOT (bit 6). `scope_screenshot` with the scope view running (calibration square on
CH1): the PNG must match the panel (grid, trace colours: CH1 yellow, CH2 cyan, top bar text, the MCP chip) - compare
with a photo of the screen; that is **T9**. Wrong colours (red and blue swapped, or a byte-swapped mess) = the format
assumption; a shifted or torn picture = row stride. Repeat in the spectrum view, the phosphor view, Frequency response
and Diagnostics. With a menu open: E_BUSY (expected). Time a full screenshot (about 1 s expected on Windows).
## 47. fw-freq2: Frequency response follow-ups, Spectrum slider, the grid rule (fw 0.2.71, protocol 1.21 unchanged, row 60 reason 8)

Branch `fw-freq2` from main 6c1e5bc (fw 0.2.69). Brief: the fw-freq follow-ups (section 45 "Not done" 1-6), the maintainer's
10-19 20:00 single-sweep report, the queue item "Frequency response chart follow-ups" (a)-(e) and the Spectrum slider.
Commits: 92821ad (engine), 84a2d65 (bode_ui: grid rule, texts), 4d6c0e6 (the view and its wiring), 3d76e14 (Spectrum),
92ed2b9 (mcp reason 8), 02a9f1b (version, CHANGELOG, docs), then this handoff. Image of 02a9f1b (GCC 10):
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 722,432 B, sha256
211fd38cb2cab80b7a6f24e281fe32f45a128c6bbce39525173aad511b238627 (a later commit differs only in the identity).
Warnings GCC 10 36 (gcc10 and gcc10-nommu), GCC 13 31, as on 6c1e5bc; none in the new code. Host tests all green
(test_ui 1,432,070 incl. test_bode_grid, test_bode_sweep 272, test_tb_view 580,345, test_bode.py, check_vectors, the
CHANGELOG check); mcp: test_bode_device 7 passed, ruff clean; the full mcp run had one failure,
tests/test_report.py::test_reports_for_every_kind (a report's `png` is None in the main checkout's venv; it touches no
bode code: check it in CI). **Nothing benched.** **No protocol version change**: READ_BODE / BODE_CTRL gain reason 8 and
the header-before-the-first-sweep rule (docs/protocol.md row 60). **Settings word claimed: 216** (0xB200 | Trace style
bit 0; anything else reads Lines). 217..218 stay free.

What landed (by brief item):
1. **Single sweep visible and cancellable.** Reading the 0.2.69 code I found no path where STOP is ignored; what made
   it look ignored: (a) `scope_run_view` showed WAIT (orange) on the chip during any sweep while the Scope's trigger mode
   was Single; (b) a re-sweep of the same plan counted as a complete table, so cards and markers looked settled; (c)
   `bode_stop(e, 0)` let the point in progress run all its re-centring, settle retries and CH2 range steps (on a bad
   readout that is many captures, seconds at low frequencies). Now: the chip reads RUN in this mode (menu.c
   `scope_run_view` ignores the trigger there), a single sweep's cards read "sweeping" until its table is complete
   (`table_state`: `bv.single` and `bode_done_count < n`), and STOP ends the sweep after the capture in progress (the
   point keeps that capture's result; a STOP in the pre-scan, the verify loop or a background capture ends there). The
   root cause was not reproduced: if the maintainer still sees STOP ignored, note whether the chip read WAIT and whether
   the Sweep card kept counting.
2. **Spectrum slider** (spectrum.c): plot y 50..402 (8 x 44 px), labels y 406, cards y 426 (`UI_MS_SPEC_Y`), the slider
   at y 452..471 as the Scope's. The track is the displayed axis 0 .. min(fs/2, 25 MHz), not fs/2: the view caps its
   axis at 25 MHz, and with fs/2 the window could never reach past a quarter at 200 MSa/s (ux-makeover.md says so now).
   A drag moves `spec.start` by dx x START_STEPS / 720 (window follows the finger, no Move-fast scaling, as the Scope's
   slider). The colour legend of Waterfall / Density moved into the plot's top right (y 72). `HIST_H` follows `PLOT_H`
   (352 rows; about 29 KB more history).
3. **Top bar** (bode-ui.md 7, texts and boxes in `bode_ui.h`): ACQ chip "Set by / the sweep"; its tap opens a one-line
   box ("The sweep sets the sample rate for each point", x 250, y 48, 40 px) that any tap closes. The generator chip
   shows the point's tone (duty 50 %, green edge) from `bode_view_gen_tone`, refreshed by `bv_gen_chip` in `draw_all`
   (so at most every 150 ms while sweeping); while sweeping its tap opens "Driven by the frequency response sweep"
   (right-aligned at x 728) instead of the menu; stopped it is the user's generator and menu (as before the first
   sweep). Trigger menu: `TRIGGER_MENU_HEIGHT` is an expression now (variables.h declares `bode_view_active`), 24 px
   taller in the mode, the grey line at y +282.
4. **Cancelled AUTO SET restores the table**: `bode_snap` (bode_sweep.h, about 25 KB static `as_snap` in bode_view.c),
   saved in `bv_auto_start` after a running sweep is aborted; `bv_auto_cancel` (second tap, STOP, a host stop, leaving
   the mode) restores it. An error during a pass keeps the pass's points and its warning (not restored).
5. **Move fast** sets the cursor drag step (`bode_view_touch`: slow = a fifth of the finger's travel from the flag's own
   position, or from the touch point in the plot); **MEASURES** changes show at once (`bode_view_cards_refresh` redraws
   the cards left of the open menu after each tap, statemachine.c redraws the view when it closes); **sharp tank**: the
   design's rule f0 (1 -+ 4 / Q) needs a measured Q, so an unresolved band (narrower than two point spacings) now takes
   Q >= 1 / (2 ln r) and Settle one preset up, and AUTO SET runs up to three more passes around f0 (`bode_auto_zoom`:
   f0 / sqrt(10) .. f0 sqrt(10) Resonant, / 1.1 .. x 1.1 and / 1.03 .. x 1.03 High Q, 20 points) until resolved. Model:
   Q 50 at 15.3 kHz -> pass 3, Q 46, 10 kHz .. 20 kHz, High Q; Q 120 -> pass 4. Rule written in bode-ui.md 3.2 and
   README.
6. **Chart follow-ups**: (a) Trace style Lines / Dots at y 245 (Grid moved to y 305), the Scope's construction, choice
   rows without icons; flagged points always grey loose dots. (b) `bu_fgrid` (bode_ui.c) is the grid rule for both
   modes, written into docs/ui-guidelines.md section 3; horizontal lines were already at the labelled divisions. (c)
   `bv_header`: with sweep id 0 the header's settings fields are `cfg_now` (`bode_proto_settings`), the same settings
   the estimate and the Single sweep row use. (d) **Readout fault** (decided per the brief: fail fast): `on_verify`
   ends the sweep with BODE_ERR_READOUT (8) when a channel at range 0 (5 V/div) is clipped with its fundamental under
   CH1_LOW (20 codes; CH2 only when the record is not flat). Warning "ADC readout fault: see Diagnostics"; mcp
   BODE_REASONS[8] and a caveat in `bode.device_sweep`. The mcp simulator (SimBode) does not produce reason 8 and still
   reports its engine defaults before the first sweep (it never did otherwise in tests). (e) Single sweep's "about N s"
   was already `cfg_now`; now the header matches it.

Not done / for the next agent (queue them):
- The mini overview drawn in the slider tracks (maintainer 20:00, "design first"): not designed, not built.
- The mcp simulator's SimBode: reason 8 and the header-before-first-sweep rule (only if a test needs them).
- A Single sweep started from the CTRL page leaves the bar on the CTRL page, where RUN/STOP is not visible (the chip
  shows RUN); the maintainer may want the bar to return to page one. Ask before changing.
- The generator chip after a sweep shows the user's generator although the engine left the pin off (as 0.2.69); the
  view gives the generator back only on leaving.
- Spectrum: the slider has no Move-fast scaling (as the Scope's); touches at y 426..449 (the cards) do nothing (from
  y 436 before), y 450 down take the slider.

**Bench walk-through (maintainer, bench session on <port>; FEL load only, no SD writes; parts: the RC 1 kOhm / 15 nF ceramic,
the 991 / 102 Ohm divider, a tank if one is at hand; wiring A; x1 probes; on 0x1539 expect item 9's fault)** - FEL-load
(`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.71,proto=1.21`.
1. Spectrum analyser: the plot ends at y 402, frequency labels under vertical grid lines at round frequencies
   ("0Hz", "5MHz", ... at full span), the cards at the Scope's height, the slider frame under them with the window filling
   the track at full span. Tap the right half of the plot twice (zoom in): the window shrinks to a quarter; drag it: the
   spectrum pans with the finger, the labels move to the new round steps; at the track's ends it stops. Waterfall: the
   colour legend inside the plot's top right.
2. Scope: ACQ chip and trigger menu unchanged (no "Set by", no extra line).
3. Frequency response: ACQ chip reads "Set by" / "the sweep"; tap it: one line "The sweep sets the sample rate for each
   point", any tap closes it, nothing else changes. Trigger chip: its menu has "Not used by the sweep" at the bottom.
   Generator chip: the user's generator; its menu opens normally while stopped.
4. RC low-pass: CTRL > Sweep > Single sweep. The button reads STOP (red), the chip RUN (also with the Scope's trigger
   mode set to Single beforehand), the cards "sweeping", the generator chip steps through the tones (50 %); tap the
   generator chip during the sweep: "Driven by the frequency response sweep". At the end: STOP on the chip, RUN on the
   button, the cards show values, the generator chip back to the user's. Single sweep again (same plan): the cards read
   "sweeping" again until it ends. During a single sweep tap STOP: it ends within one capture (well under a second above
   1 kHz), the measured points stay, chip STOP.
5. CTRL page: Display, Sweep, Thru, Trace style ("Lines") at the fourth slot, Grid at the fifth. Trace style > Dots:
   the curves become loose dots; Lines joins them again; grey (flagged) points never joined. Power cycle: kept.
6. Grid: Log 100 Hz .. 1 MHz: decade lines only, labelled; Start 1 kHz Stop 200 kHz: the 2 and 5 lines appear (sparser
   dots), unlabelled; Start 20 kHz Stop 100 kHz: 20k, 50k, 100k lines, all labelled; Linear: lines at each label.
7. AUTO SET on the RC, then tap AUTO SET again during the overview: "Auto set cancelled: settings kept" and the table
   from step 4 is back (same curve, same cards). AUTO SET, then STOP during the overview: the same. A tank (Q 20..50):
   AUTO SET runs the overview and then one to three narrower passes (the slider window shrinks around the resonance,
   the Sweep card "auto n/20" each time), then RUN on a span close around f0 with Settle Resonant (Q up to 34) or High Q; Peak and Q cards near
   the tank's values.
8. T CURSOR, then Move fast off (slow): dragging a flag moves its cursor a fifth of the finger's travel, point by point;
   Move fast on: it follows the finger. MEASURES: select BW: its card appears at once on the left (if it lands left of
   the menu) and everything shows as soon as the menu closes.
9. On 0x1539 (A-core fault): RUN on the RC: the sweep stops within the pre-scan with "ADC readout fault: see
   Diagnostics" (READ_BODE state 4, reason 8), no flagged table. On a clean bitstream (0x153A or 0x1535) the RC sweeps
   normally (bench 20:51's host sweep: -3 dB near 10.3 kHz).
10. Host: right after boot, open the view (BODE_CTRL action 9) and send action 0: the header's settle / start / stop /
   points equal the screen's Sweep settings and the estimate matches the Single sweep row's "about N s".
Bug indicators: WAIT on the chip in this mode; STOP taking more than one capture; a cancelled AUTO SET leaving the
overview's points or axis; the generator chip stuck on a tone after a sweep; the ACQ chip reading "Set by" outside the
mode or the trigger menu's extra strip there; spectrum labels overlapping or a grid line without a label slot; the
slider window off the track or changing width while dragged; Trace style not saved; a readout fault on a clean
bitstream (the rule would be too tight: 5 V/div, clipped, fundamental under 20 codes).

**Addendum (fw-bode2, fw 0.2.83 provisional, 2026-10-20; branch fw-bode2, commit 51ab2a4, then the docs commit).** Not
benched, nothing flashed; PC_FW_VERSION not bumped. Bench 2026-10-20 10:52 (docs/hardware-status.md): on 0x153A / 0.2.82
AUTO SET and every host-started sweep ended in the pre-scan with reason 8 while the self-check counted 0 flagged
captures. Cause: `bh_read` set `cap->s[c].clipped` from `ADoverload`, which `fpga_read_adc_data` ORs per sample and only
the Scope view's passes clear (scope_functions.c); one railed sample in any earlier readout made every engine capture
"clipped", the pre-scan stepped down to 5 V/div and the rule above fired. What changed:
- `bode_scale.clipped` is gone: `bode_measure` judges clipping from the record's own min / max (codes < 0.5 or > 254.5).
- `pc_hal_trace_build` clears `ADoverload` of each channel it reads, so the host capture flags (`pc_capture_finish`),
  READ_TRACE meta bits and GET_STATUS bits 6-7 describe the latest readout; the Scope view's red label and warning keep
  their own clear-per-pass behaviour (they sample the flag at the start of a pass, as before).
- `bode_cap.check` (BODE_CHK_NONE / CLEAN / FAULT, bode_sweep.h): pc_bode.c `bh_check` reads 0x36
  (`pc_hal_cap_check`) after a legacy 200 MSa/s engine capture, as the host path does, and returns FAULT when that read
  flagged a core or PC_CAPF_REPEAT is set; decimated captures have the repeat bit only (the counters were validated at
  full rate). `bode_step` ends the sweep with reason 8 on FAULT in any phase. `readout_fault` (the 0.2.71 rule) runs
  only with BODE_CHK_NONE (bitstreams before 0x153A), now on the record's rails. A clean check never gives reason 8.
- test_bode_sweep (370 checks): clean check with rails in every capture (done, every point clipped, no reason 8); a clean
  sweep after a railed one on the same engine (no flags, CH1 at 0.5 V/div); one railed capture inside the sweep (no point
  clipped); FAULT on the first capture and on capture 12 (reason 8, generator off).
- Noted, not changed: a railed pre-scan capture on CH1 (one saturated record) leaves CH1 on 2.5 V/div for the sweep
  (ch1_low on the post), because the pre-scan ranges from that capture and the engine never ranges CH1 up (the host's
  `_prescan` does the same). The intermittent whole-record saturation after a rate change (10:52) was not chased.
- Not changed: mcp `BODE_REASONS[8]` still says "the pre-scan clipped at 5 V/div ..." (brief: keep the mcp text);
  docs/protocol.md row 60 reason 8 describes both rules.

Bench steps (maintainer present, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, 0x153A, RC 1 kOhm /
15 nF on the post, wiring A, x1 probes):
1. A deliberate clip first: Scope view, CH1 at 0.1 V/div on the post until the CH1 label turns red; then open Frequency
   response, AUTO SET: the overview runs (no "ADC readout fault"), the span lands around the RC's corner (about 10 kHz).
2. The same from the host after a clipped host capture (CH1 0.1 V/div, ARM + READ_TRACE showing the overload bit), then
   BODE_CTRL action 1 with 100 Hz .. 1 MHz / 20: READ_BODE state 2 (not 4 / reason 8). A reason 8 now must come with
   a flagged core or REPEAT in GET_HEALTH 106 / 107 (note the counts 108..115).
3. A device sweep after the wizard (1 kHz .. 500 kHz, 20 points): the rows show CH1 at range 3 (0.5 V/div) and no
   clipped flags (bits 0, 1) where the host sweep (`fnirsi1013d-bode --start 1kHz --stop 500kHz --points 20`) has none;
   |H| within 0.05 dB of the host where both are valid.
4. GET_STATUS bits 6-7 after a clean host capture read 0 even right after a clipped one (latest readout, not sticky).
5. Optional fault path (needs the repeat bit): with REPEAT set (GET_HEALTH 106 bit 5, e.g. after section 54 addendum
   step 5), any sweep ends at once with reason 8; after a saved training it sweeps.
## 48. fw-bus: FPGA bus contract fixes (fw 0.2.72, protocol 1.22, row 62)

Branch `fw-bus` from origin/main 6c1e5bc (fw 0.2.69). Brief: the fpga-gen6 contract findings (section 35 "Open", fpga/al3
README section 7 findings 2, 4, 5), the queue's "FPGA bus fixes (EXT shift in legacy, keep EXT in
fpga_swap_trigger_channel, dummy strobe in screen readouts)", the "Full-rate encode check" refinement and the core-order
convention. Commits: 5f38d23 (fpga_bus.c: readout, arm order, UI record start, test_fpga_bus + fpga_model), 7833312
(legacy EXT shift, EXT kept on channel toggles), d6de414 (encode check after async writes, write counters, type 22 level,
protocol 1.22, mcp + simulator + vectors), 267c1be (core order from 0x33), then docs and this handoff. **Nothing benched,
nothing flashed.** Host tests all green (test_fpga_bus 11259, test_session 69956 / 69954, test_enc_heal 109, test_diag
471, check_vectors OK with 96 frames); mcp pytest 417 passed (test_report's PNG test fails the same way on main in this
environment), CI ruff selection clean, `fnirsi1013d.vectors --check` ok. Warnings: GCC 13 31, GCC 10 36, sorted lists
identical to origin/main 6c1e5bc.

**What the bitstreams guarantee and what the firmware now does**
- Readout: every bitstream we know (stock measured, AL3 0x1532..0x1539 simulated; 0x1539 changed the output edge but
  kept the read pointer, the dummy strobe and the lag) returns mem[S], mem[S], mem[S + 1] after 0x1F = S without a dummy
  strobe. `fpga_bus_contract_for(version)` says one dummy strobe for all of them (one table, so a future bitstream that
  drops the lag gets its own entry), `fpga_bus_init` runs from `fpga_get_version` (the first version read at boot).
  `fpga_read_start` is now the only way into a readout: the UI trace (`fpga_read_sample_data`, both passes), the 16-bit
  trace (`fpga_read_decim_data`, moved into fpga_bus.c), READ_RAW / the encode data check (`pc_hal_read_core` ->
  `fpga_read_lane`) and the sequencer view (`pc_hal_seq_read`). Consequences: the scope trace, the spectrum (it reads the
  trace buffers), READ_TRACE and the 16-bit trace sit one word (two legacy samples, one 16-bit sample) earlier than on
  0.2.69, equal to READ_RAW and to the averaged trace; the stale first byte is gone. The software trigger refinement
  re-centres the screen trace, so the visible effect is small.
- UI record start: `pc_glue_ui_read_start` = (0x14 - the 0x0B words) mod the ring (`fpga_ring_start`). 1.00o5's branches
  wrapped with ring - 751 for any pretrigger: one word off on stock, (751 - pre) words off on AL3 / EF2 when 0x14 lay
  within the pretrigger of the ring start (auto mode with the write address wrapped). The encode data check uses it too.
- Arm: `fpga_arm_reset` (UI conversion and host ARM): 0x0B / 0x0C before 0x01 = 1 when the bitstream takes them, the 0x05
  wait (unbounded with the BUSY poll for the UI, PC_READY_POLLS for the host), 0x0B / 0x0C again, 0x0E, release.
  `pc_seq_hw_pre_reset` keeps only the sequencer registers. Harmless on 0x1537+ (they also load after the release).
- Legacy EXT: `pc_decim_ext_shift_legacy`: 8 samples (4 words), 4 at divider 0, from tb_trigpos (README section 7 table:
  EXT word +5..+6 at D 9 / 199, +3.5 at D 0, ADC trigger +1..+1.5). D 1..8 were not simulated. Applied through
  `pc_decim_ext_shift_at` everywhere the 16-bit shift was (host arm geometry, capture info 70, GET_DECIM 31, the record
  start, the UI pretrigger words). `fpga_swap_trigger_channel` returns at once for source 2.
- Encode check: 0x33 bit 0 clears only at 0x01 = 1 (README section 8), so "clear it on 0x0D" is not possible without a
  new bitstream. `fpga_bus_note_write` (from fpga_control.c's write trace) counts 0x0D, 0x53 / 0x56, 0x01 = 1 and
  remembers a rate / filter write since the last reset; `eh_capture_mode` then judges a 0x1538+ capture by its record
  data (order independent) instead of by bit 0. Nothing skipped: a garbled capture is still caught and re-armed.
  GET_LINK_STATS 64-67 (writes and those data-judged captures, `pc_hw_enc.replaced`). Type 22 is off after boot;
  GET_EVENT_LOG flags bits 2:1 = 2 on, 1 off (3 = E_BAD_ARG), response word 2 bit 0 = on (`fpga_trace_level`).
- Core order: `pc_variant_core_order` clears caps bit 12 only on 0x1538+ whose boot 0x33 had bits 3:2 = 01;
  `fpga_encode_init` now runs before `pc_session_init`. A 0x1538+ reporting another convention keeps bit 12, keeps the
  amber line / event, and is checked by its data (GET_HEALTH 88 = 1). The host ARM check takes the mode from
  `pc_hal_enc_state`, not from the version. 0x1532..0x1536: still unverifiable in general (a flipped order shows in the
  data only for large MHz signals at 200 MSa/s); bit 12 stays set and the Diagnostics amber line says why. The
  convention itself (PC_INTERLEAVE_ORDER 0, swap bit clear) did not change.

**Bench list** (maintainer with the bench session; FEL load only, nothing flashed; unit on 0x1539). Build from the branch head
(`make gcc10`, the image hash follows the commit). Python: `from fnirsi1013d.scope import Scope; from fnirsi1013d import
analysis as A; s = Scope.connect("<port>")`.
- B1 identity: `#VER` fw=0.2.72 proto=1.22 fpga=0x1539. `s.info.caps`: bit 12 (0x1000) clear, bit 11 (0x0800) set.
  `s.health()`: enc_mode 2, enc_boot33 0xF6, enc_flags bit 1 clear. `s.link_stats()` has 68 entries.
  Regression sign: bit 12 set on 0x1539 (boot 0x33 read or init order wrong).
- B2 readout lag (T3 in one step): CH1 on the calibration pin (generator 1 kHz), `s.set_timebase(divider=199)`,
  `s.set_trigger(source=0, edge=0, mode=1, pretrigger_samples=200, total_samples=1000)`, `cap = s.capture(["CH1"])`,
  `tr = s.read_trace_capture(["CH1"])`; rising crossing at mid level in `cap.channels["CH1"].volts` and in
  `tr.channels["CH1"].volts` (`A.find_edge_near(v, mid, "rising", 200, 20)`): equal within 0.5 sample. On 0.2.69 the
  READ_TRACE edge sat 2 samples later. Repeat at divider 0 and with Filter CIC2+FIR at 1 MSa/s (16-bit: within 0.5
  output sample; 0.2.69 one sample later). If READ_TRACE is now 2 samples *earlier* than READ_RAW, the bitstream has no
  lag and its `fpga_bus_contract_for` entry must say read_dummy 0.
- B3 screen: 200 MSa/s, cal pin, Average off / on (0x1536+ hardware averaging): the edge stays put when toggling
  Average (0.2.69: two samples apart, section 35). The first two samples of the trace are not a repeated or odd pair.
  Spectrum view of the 1 kHz square: unchanged.
- B4 arm order: 100 kSa/s (divider 1999), host captures alternating pretrigger 60 / 100 samples (20 each), CH1 rising on
  the generator: the edge at pretrigger + 1..3 samples in every capture. On 0x1539 the FPGA already loads after the
  release, so this is a no-regression check; the race itself shows only on 0x1532..0x1536 (test_fpga_bus reproduces it:
  the 1.00o5 order loses 425 of 600 changed pretriggers on the model of 0x1535 at D 199..1999).
- B5 legacy EXT (wiring: fpga/al3/README.md section 2; generator off; the same edge on CH1 and on the post):
  `s.set_trigger(source=2, edge=0, mode=1, pretrigger_samples=200, total_samples=1000)` with Filter off, at dividers
  199, 19, 9, 3, 1, 0: `s.capture_info().ext_shift` = 8, 8, 8, 8, 8, 4; the CH1 edge at the pretrigger within +-3
  samples (0.2.69: +10..+11 at 1 MSa/s). Note the offsets at D 1 and D 3 (not simulated). Then in the UI with EXT
  selected switch CH2 off and on, then CH1 off and on: the trigger chip stays EXT and `s.get_trigger(cached=False)`
  source = 2 (0.2.69: CH1 / CH2).
- B6 encode check after rate writes: UI running at 200 MSa/s, host `s.set_timebase(divider=99)` / `divider=0` 40 times
  with 0.3 s between: GET_HEALTH enc_bad stays 0 (0.2.64: 112 of 5798 re-taken), link stats `adc_fullrate_by_data`
  grows by about 40, `fpga_rate_writes` by 80+, no garbled trace. Then 60 s with no writes: `adc_fullrate_by_data`
  constant, enc_checked grows. Regression sign: enc_bad grows with the toggles, or by_data grows without writes.
- B7 trace level: `s.event_log()` has no type 22 entries and `fpga_trace` False; `s.event_log(fpga_trace=True)`, one host
  capture, `s.event_log()`: type 22 entries (0x01 = 1 by host, 0x0D when the time base changed); `fpga_trace=False`
  stops them; raw flags 0x06 answer E_BAD_ARG.
- B8 regression: the calibration wizard passes on 0x1539 as at 20:30 (it reads through the dummy strobe now); a host
  segments run (section 35 step 7, short) and UI averaging work (the sequencer pre-reset lost its own 0x0B / 0x0C
  writes to `fpga_arm_reset`); a host Bode sweep of the RC as at 20:51.

**Open / for the bench session**
- D 1..8 legacy EXT shift values are extrapolated (B5 measures them); 200 MSa/s uses 2 words from the 0x1538 model.
- Caps bit 11 READ_LAG_UNVERIFIED stays set until B2 is accepted (then clear it for AL3 in `pc_variant_from_version`).
- `pc_hal_seq_read` keeps its own strobe loop after `fpga_read_start` (same contract); fpga_read_adc_data's loop too
  (hot path with the correction).
- acal_track's A - B model drops sloped pairs, so a flipped order on 0x1532..0x1536 affects it little; not gated.
- mcp: `scope_event_log` gained `fpga_trace`; docs/mcp-tools.md was not regenerated.

## 49. fw-over: the slider track overview and tap to jump (fw 0.2.73, protocol 1.21 unchanged)

Branch `fw-over` from main 113ec80 (fw 0.2.71; not merged with main since: fw-bus took 0.2.72 / protocol 1.22 and
section 48 there, so this branch renumbered itself to 0.2.73 and this section is 49). Brief: docs/plans/slider-overview.md
with the maintainer's decisions of section 10 F (items 1-5, 21:20) and item 6 (21:36: the track takes the whole row, the
window is two edge lines), then tap to jump. Commits: a5c875e (slider_ov module, track geometry, tests), 5aa5093
(Scope), 2044a49 (Spectrum), 2b9dad9 (Frequency response), b3fa8d2 (tap to jump, batch 2), 9f51633 (version, CHANGELOG,
README), d12e414 (mockups regenerated), 2e51d53 (renumbered to 0.2.73), 07b9459 (plans), then this section. Image of
07b9459 (GCC 10): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 726,528 B, sha256
4e9899457b984b24f5efebfd8f4afa12368d50b29201a3a6a462d932cf65b0b8 (a later commit differs only in the identity).
Warnings GCC 10 36 (gcc10 and gcc10-nommu), GCC 13 31, as on 113ec80; none in the new or changed code. Host tests all
green (new test_slider_ov 70,809 checks; test_ui and test_tb_view with the new track numbers; CHANGELOG check).
**Nothing benched. No protocol change, no settings word.**

**Files touched (for the merge with fw-bus):** new slider_ov.c/.h, slider_ov_draw.c/.h, host_tests/test_slider_ov.c;
changed menu.c (scope_draw_grid split into scope_draw_slider_row + scope_draw_grid_lines, scope_slider_envelope; the
"~" row gone), menu.h, scope_functions.c (scope_code_to_y factored out of scope_get_y_sample; scope_display_trace_data
draws the grid lines first and the slider row after the traces), scope_functions.h, spectrum.c (spec_draw_slider,
spec_draw_overview, spec_current_view, the slider touch), bode_view.c (the overview copy, draw_overview / draw_slider,
slider_drag), acq_view.c/.h (acq_view_slider_jump), statemachine.c (the jump at touch-down), tb_view.h and bode_ui.h
(TBV_SLIDER_X/W, BU_SL_X/W = 2 / 726; BU_SL_FRAME_* and BU_SL_WIN_* removed), pc_cmds.h (version), both Makefiles,
host_tests/test_ui.c and test_tb_view.c (new numbers), CHANGELOG.md (a 0.2.73 heading above 0.2.71: put it above main's
0.2.72), firmware/README.md, docs/plans (slider-overview, ux-makeover, bode-ui), docs/mockups (generators and boards).
None of the FPGA readout paths fw-bus changed: the overview reads the trace buffers after readout.

What landed:
1. **The "stock thumbnail" was not data.** hardware-status 21:09 read the Scope's track as a record thumbnail. It is
   1.00o5's decoration: `scope_draw_grid` printed "~" in yellow every 5 px along y 453 (`display_text(i, 453, "~")`),
   identical with no signal. It is gone, replaced by the overview (no second drawing).
2. **Geometry** (slider_ov.h; maintainer item 6): a white line at y 452 over x 2..727, the track x 2..727 (726 columns) by
   y 453..471 (19 rows: the old frame's 18 inside rows plus its bottom row), the window two 1 px white lines at x and
   x + w - 1 over the track's height. The window maps on the 726 px track in all three modes (`TBV_SLIDER_X/W`,
   `BU_SL_X/W`), so a drag moves 726 / 720 of what it did (10 px at 25 MSa/s, 1 ms/div, 3000 samples: 82 samples, was
   83; `bu_slider_x(6)` 209, was 210). Hit areas unchanged (the Scope y 436..479 over x 2..727, the others y 450 down).
3. **Scope** (menu.c `scope_slider_envelope`): per enabled channel (CH2 then CH1) the min / max code of each column over
   `fpgasettings.totalsamples` (`sov_cols`: a column shares its first sample with the one before, so steps join), the
   codes through `scope_code_to_y` (a 256-entry row table for the codes that occur) into the 19 rows (y 49..448 ->
   row (y - 49) x 19 / 400). Drawn every pass, after the traces (the averaged record when Average is on), because the
   view redraws the trace every pass anyway (scope_acquire_trace_data ends in scope_display_trace_data): no cache, no
   stale overview. None in roll mode, X-Y, or the slow-rate branch (sample rate index >= 18 or time base >= 35, 1.00o5's
   181 px window, now drawn as two edge lines too).
4. **Spectrum** (spec_draw_overview): the highest bin per column over 0 .. v->half (the displayed axis, capped at
   25 MHz) of `spec_db` (Spectrum: CH2 then CH1 as shown) or `spec_raw[hch]` (Waterfall / Density), on ref level and
   dB/div (`sov_row` straight from centi-dB), joined as a line; nothing while `spec.wait`.
5. **Frequency response**: `ov_take` copies the overview pass's points (`gather`) when it completes (in `bv_auto_finish`
   with `bv.autoset == 1`) into `ov_new`; `ov_commit` makes it `ov` when AUTO SET ends with any result (span set, "No
   corner", "No response", or an error in a sharp-tank pass); `bv_auto_cancel` drops `ov_new` (the old `ov` stays);
   `bode_view_defaults` (Restore default settings) clears `ov`; power-on: empty. Track: the overview pass's live points
   while it runs, `ov_new` during the sharp-tank passes, else `ov`. Magnitude, or the phase in Phase only, fitted
   (`sov_row_fit` over the valid points; over the flagged ones when none is valid), columns `sov_col(bu_slider_x_mhz(f))`
   (10 MHz on column 727), valid points joined (`sov_lerp` + `sov_join`; not across a flagged point; the phase not
   across a wrap), flagged points one grey (FLAG_COLOR) pixel.
6. **Tap to jump** (batch 2, `sov_jump`): a touch-down at y >= 450 outside the window shifts the window so its centre is
   on the touch (kept on the track), then the ordinary drag continues from there, so the release does what a drag
   release does. Scope: `acq_view_slider_jump` at touch-down in touch_handler (through `tbv_slider_move` /
   `tbv_clamp_pos`), `previous_trigger_point_position` snapshotted after it. Spectrum: `spec.start` from the drawn window
   (`spec_current_view` + `spec_slider_window`), one redraw, then the drag loop. Frequency response: `slider_drag` adds
   the jump to the finger's travel (`bu_slider_shift`), so the release restarts the sweep or runs one, as a drag does;
   still ignored during AUTO SET. The Scope's extended area y 436..449 (beside the cards) never jumps: the cards and the
   band above them keep their taps (touch batch 2, section 40).

**Cost** (computed from the GCC 10 code at 600 MHz with the D-cache on; not measured): the Scope's sample loop is
unrolled by four (4 byte loads then 16 compare / conditional moves: about 7.5 cycles a sample plus about 1.5 for cache
line fills) = about 0.5 ms for 2 x 16,382 samples, 0.09 ms for 2 x 3000; per column (726 per channel: the iterator, the
row table, one `display_draw_vert_line` run with its colour) about 0.2-0.25 ms for both channels; total about 0.75 ms
per pass at 16,382 and 0.35 ms at 3000, against the design's 0.33 ms estimate for the sample loop alone (the design
counted 6 instructions a sample and no drawing). The removed "~" row cost about 0.1 ms. Spectrum under 0.1 ms;
Frequency response nothing measurable. All under the design's 1 ms revisit line, but **measure pass_ms**: if the Scope's
cost matters, cache the column codes per record (the record changes only on a capture, a host capture shown, a viewer
load or an averaging pass) or draw runs straight into the frame buffer.

Not done / for the next agent:
- No bench yet: pass_ms on / off is the first thing to measure (walk-through step 2).
- `scope_code_to_y` has no host test (scope_functions.c is target-only); the refactor is a mechanical move of
  `scope_get_y_sample`'s arithmetic (same expressions, the code passed in instead of read by index).
- The mockups draw the line and the window edges grey (LG1 / LG2), the firmware white, as before.

**Bench walk-through (maintainer, bench session on <port>; FEL load only, no SD writes; CH1 on the calibration post, CH2 free;
for step 6 the RC 1 kOhm / 15 nF or a tank)** - FEL-load (`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin
exe 0x80000000`), `#VER fw=0.2.73,...`.
1. Scope, 1 kHz square on CH1, 500 kSa/s, 200 us/div, 3000 samples: under the plot a white line at y 452 and the track
   below it down to y 471 with no frame sides or bottom; the square's envelope in the track, yellow between the window's
   two white edge lines and dark yellow outside; no "~" row. CH2 on (free input): a flat cyan line at its zero level
   under CH1's. CH2 off: gone. Move CH1's position: the envelope moves in the track with the trace; change V/div: it
   grows or shrinks as the trace does; clip the trace at the top of the plot: the envelope clips on the top row.
2. Cost: GET_STATUS `pass_ms` (median of 20) at 3000 samples and at 16,382 samples (Memory 16k), both channels on, and
   once with fw 0.2.71 (or 0.2.72) for the same settings: expected about +0.3 ms and +0.7 ms. More than 1 ms over:
   report it (cache per record).
3. Deep record, 16,382 samples, 5 MSa/s, a fast time base (window narrow, at least 8 px): the record's other periods
   dimmed outside the window; drag the window: the bright part follows the window column by column, the envelope itself
   does not move. Tap the track far left of the window: the window jumps to centre on the finger (at the track's end
   when the tap is near it), the trace shows that part; tap inside the window: nothing jumps (a drag works as before).
   Tap just above the line, beside the cards (y about 440): no jump; taps on cards do nothing, the band above them still
   steps V/div. Move slow: a drag after the jump moves a fifth of the finger.
4. Roll mode (time base 50 ms/div or slower), X-Y and a sample rate in 1.00o5's branch: no overview; the window is two
   edge lines (roll: as 1.00o5's box position). Waveform viewer: the loaded record's envelope. Average on: the averaged
   record's envelope (smoother noise).
5. Spectrum (square on CH1): the whole axis's harmonics in the track; zoom in one step (tap right of centre): the
   window halves, the left half bright; drag it: the spectrum pans, the track's content stays and only the bright part
   moves; tap the track to the right of the window: the window centres there and the spectrum pans to it. Change the
   time base: the track empties until the new spectrum arrives. Waterfall: the history channel's latest record.
6. Frequency response: on entry after power-on the track is empty (no overview before AUTO SET). AUTO SET on the RC: the
   track fills point by point under the 100 Hz .. 10 MHz window; afterwards the overview stays (magnitude, fitted to the
   track's height) with the window over the chosen span, bright inside. CTRL > Display > Phase only: the track shows the
   phase (pink). Drag the window and release: re-sweep as before; tap the track outside the window: the window centres
   there by whole steps and the sweep restarts (RUN) or one sweep runs (STOP). AUTO SET again and cancel it (second tap)
   during the overview: the previous overview comes back with the previous table. Settings > Restore default settings:
   the overview is gone. Power cycle: gone. On the 0x1539 bitstream (readout fault) a sweep ends at once and AUTO SET
   ends with "ADC readout fault": the previous overview stays.
Bug indicators: the overview crossing the window's edge lines in the wrong colour (bright outside or dim inside); a
dim / bright boundary one or more columns off a window edge; the Scope thumbnail drawn twice (a "~" row or an envelope
over a second one); the envelope not following V/div or position; an overview in roll mode or X-Y; anything drawn at
y 472..479 or left of x 2 / right of x 727 in the row; a frame line on the left, right or bottom; the window off the
track after a jump at the ends; a jump from the area beside the cards; a Spectrum overview from the old axis after a
time base change; a Frequency response overview before the first AUTO SET or after Restore defaults; pass_ms up by more
than about 1 ms at 16,382 samples.

**Addendum: decision 7, fw-over2 (fw 0.2.74, branch fw-over2).** The maintainer looked at 0.2.73 on the unit
(2026-10-19 22:15) and revised the geometry (slider-overview.md 10 F item 7, section 2 as built): the track's line at
y 452 is gone and the row is all track, x 2..727 by y 452..471 (`SOV_Y` 452, `SOV_H` 20; `SOV_LINE_Y` removed). The
window is a box again: `sov_draw_window` draws, last, a 1 px black margin (y 452 and y 471 over x - 1..x + w, columns
x - 1 and x + w over the track; clipped to x 2..727) and then 1 px white lines at y 453 / y 470 (`SOV_WIN_Y` 453,
`SOV_WIN_H` 18) and at x / x + w - 1. The views are unchanged: the overview is still mapped on all `SOV_H` rows with
`sov_draw_run` (full colour for `sov_inside` columns, dimmed outside), and the box and margin cover rows 0, 1, 18, 19
over the window's columns, so the full-colour part is x + 1..x + w - 2 by y 454..469 (16 rows) and the scale is the same
on both sides. The Scope's mapping is now row = (y - 49) x 20 / 400 (2.5 rows a division). Hit areas, tap to jump and
the horizontal mapping (726 px) unchanged; test_ui / test_tb_view needed no new numbers, test_slider_ov has the new
geometry and the 20-row values. Bench: as step 1 above but no line at y 452, the window a white box with a black ring
around it, the overview over y 452..471 outside the box; a trace at the plot's top or bottom edge is visible only
outside the window. Not benched.

**Addendum: decision 8, fw-ov3 (fw 0.2.93).** Overlaid draws the overview's magnitude and phase (`sov_curve`, each fitted on its own; `sov_draw_curves` phase first, magnitude over it, maintainer 2026-10-20 13:35 / 13:36), Magnitude only / Phase only one curve;
the curve walk moved from bode_view.c `draw_overview` to slider_ov.c / slider_ov_draw.c, and test_slider_ov checks the fit, joins and order on a stub screen. Not benched.

## 50. fw-zoom: Scope tap to jump at touch-down, the zoom ladder (fw 0.2.76, protocol 1.22 unchanged)

Branch `fw-zoom` from main 1173abd (fw 0.2.74). Brief: docs/plans/task-queue.md "Scope tap-to-jump and zoom stepping"
(maintainer 10-19 22:20 on 0.2.73) with the maintainer's additions relayed at 22:22 (zooming to either end and back must never
leave a weird state; the ends plain stops) and 22:24 (the invariant is on the pair: after any taps the rate is the
ladder's for the time/div, unless set by hand in the ACQ menu, until the next zoom step). Commits: 4eab9cc (the rule,
docs/ui-guidelines.md section 2 "Time/div drives the sample rate"), ccddf2e (tap to jump at touch-down), da24d91 (the
ladder: tb_view, acq_view, tap zoom, ACQ time/div boxes, host_tests/test_acq_view.c), 8327849 (0.2.76, CHANGELOG,
protocol.md note), 0411c49 (firmware/README), then this section. Image of 0411c49 (GCC 10):
`firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 730,112 B, sha256
db457d10da0bfa0783157d15eeed0c322018b1995d02e8cfa9a9d6d655e0806d; nommu
4895720277d90ac653424726e83ae6777ab11c03e31e2b38a18ba041eb91347b (a later commit differs only in the identity).
Warnings GCC 10 36 (gcc10 and gcc10-nommu), GCC 13 31, as on main; none in the changed code. Host tests all green (new
test_acq_view 1,895,407 checks). **Nothing benched. No protocol change, no settings word.**

1. **Why the Scope's tap in the track did nothing.** Not the hit-test order: (x, 460) picks TOUCH_STATE_MOVE_RECORD in
   `scan_plot_pick` (the slider row comes right after the left handle column). 0.2.73 called `acq_view_slider_jump` in
   `touch_handler`, which only runs once `scan_for_touch` has seen the finger move past `TOUCH_DEAD_BAND` (it sets
   touchstate to the pick then). A still tap left `scan_for_touch` through the tap branch, where the slider is a handle
   and takes no tap, so nothing happened; a drag that began outside the window did jump (from the position 6 px on).
   The Spectrum jumps inside `spectrum_touch` at touch-down, hence the difference. Fix: the jump runs in
   `scan_for_touch` right after the pick, from the touch-down point; `touch_handler` snapshots
   `previous_trigger_point_position` after it, so a drag continues from the new place. The screen shows the jump when
   the finger lifts or moves (the touch loop still blocks: touch-review batch 3).
2. **The ladder** (tb_view.c, pure): `tbv_ladder_rate(fpd, rates, n, total)` = the fastest of `sample_rate[0..17]` at
   which the time/div is viable (`tbv_viable`: record covers the 724 px, at least ten samples a division). It is
   monotonic (a faster time/div never takes a slower rate; tested). `tbv_zoom_step(t, dir, ...)` = the nearest index in
   that direction with a ladder rate, -1 at the end; from an index no rate can show (a state no writer leaves, e.g.
   5 ns/div) the end on the other side. With 3000 samples: 200 ms/div at 1 kSa/s .. 50 ns/div at 200 MSa/s; no holes
   from 1000 samples up (below about 1000 samples the 1-2-5 rates leave holes; the step skips them).
3. **acq_view.c**: `acq_view_rate_free()` = not in the viewer, and running or Single (the ACQ menu's rule for its rate
   boxes). `acq_view_zoom(dir, rate_free)`: free -> the next time/div and its ladder rate (`acq_view_set_pair`, through
   `acq_view_set` so the trigger point is clamped with the new rate); not free -> `tbv_zoom_step` over the record's one
   rate, and where the full ladder would go on, the notice "Stopped: press RUN to zoom further" (same notice machinery
   as the snap, `acq_notice_set`). At an end a plain stop, except that a pair not viable at all gets the ladder's rate
   (safety; the writers snap, so it should not occur). Returns 0 / ACQ_SET_TB / ACQ_SET_RATE. `acq_view_set_tb` (the
   ACQ box) takes the ladder's rate while free; `acq_view_tb_ok` greys by the ladder while free, by the record's rate
   while stopped. `acq_view_fit` (the snap after a rate box, memory, Filter, host) is unchanged.
4. **scope_functions.c** `scope_tap_zoom` replaces the tap's use of `scope_set_timebase` in short mode: the rate is free
   when running, or in Single with a tap in the plot's upper half (1.00o5's rule; such a tap re-arms the shot); it
   writes 0x0E, the rate when it changed, and calls `scope_preset_values` as before. 1.00o5's `time_per_div_sample_rate`
   table (100 samples a division) is no longer used by taps. Roll: taps step 0..8 (50 s .. 100 ms/div; 0..10 in the
   viewer) through `scope_set_timebase` as before but never cross into short mode, and short mode never zooms out into
   roll (the ACQ menu's mode button is the way). **The maintainer may want this differently** (0.2.74 stepped from
   200 ms/div into roll and back).
5. **statemachine.c** ACQ time/div box: on ACQ_SET_RATE writes `fpga_set_sample_rate` and redraws the rate boxes.
6. **SET_TIMEBASE mode 0 left alone** (docs/protocol.md note): it already picks a rate for every index (the documented
   table, 100 samples a division down to 500 ns, viable for records of 1450 samples and more) and the simulator mirrors
   it. Moving it to the ladder changes the rate a host gets for an index: it needs the simulator (`mcp` simulator.py
   `TIME_PER_DIV_SAMPLE_RATE`) and a protocol note; queue it if the maintainer wants it.
7. **test_acq_view.c** builds acq_view.c + tb_view.c + slider_ov.c on the host with the firmware's tables and stub
   globals (variables.h compiles on the host). It checks every (rate, time/div) cell at depths 250..16382 in both
   directions; five-leg walks (fast end, slow end, fast, slow, fast) from every cell, asserting the pair equals the
   ladder's after every step and at the ends, that an end is a plain stop and the opposite tap works at once; hand
   rates; the boxes; the stopped mode and its notice; the snap notice text; the maintainer's tap (y 460 left of the window,
   five Scope states: the window centres on the finger or stops at the track's start; inside the window, y 440 and
   y 449 never jump) and the Spectrum's (sov_jump on a Spectrum-shaped window). It tests the function the new
   touch-down call uses; the control-flow move in `scan_for_touch` itself is target code and needs the bench.

Not done / for the next agent: the jump is not drawn until the finger lifts or moves (the blocking touch loop); a left
handle pointer at the plot's bottom can still win a touch at x < 44 on the track (the handle column comes first, as in
0.2.73); X-Y and roll have no jump.

**Bench walk-through (maintainer, bench session on <port>; FEL load only)** - FEL-load (`sunxi-fel -p write 0x7FFFFFE0
build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.76,proto=1.22`. CH1 on the 1 kHz calibration post.
1. The maintainer's tap: Scope, 16k memory, 5 MSa/s at 2 us/div (window narrow), RUN or STOP. Tap the track at y about 460
   far left of the window, finger still: on lift the window centres there and the trace shows that part. Tap far
   right: the window goes to the track's end. Tap inside the window: nothing moves. Tap at y about 440 beside the
   cards: nothing jumps. Touch outside the window and drag: the window jumps, then follows the finger. Spectrum: the
   same tap still works as on 0.2.73.
2. Zoom, RUN, Auto trigger, 3000 samples, from 1 ms/div. Tap the right half repeatedly: one time/div faster per tap,
   the ACQ chip's rate rising with it (1 ms at 200 kSa/s, 2 us at 100 MSa/s, 1 us and faster at 200 MSa/s); at 50 ns/div
   one more tap does nothing. Tap the left half repeatedly: back to 200 ms/div at 1 kSa/s, every step a full screen of
   trace (no flat line at the right, no sparse dots); one more tap does nothing (no roll). Then right again: works at
   once. Repeat at 6k and 16k memory: same ends, faster rates in the middle (16k: 1 ms/div at 1 MSa/s). The slider
   window stays on the track and the trigger point on screen.
3. Hand rate: ACQ menu at 500 us/div, pick 50 kSa/s: the time/div stays (25 samples a division); pick 200 MSa/s: it
   snaps to 1 us with "Time/div set to 1us for this rate". Close the menu, tap left: 2 us/div at the ladder's rate
   (100 MSa/s), not 200 MSa/s.
4. ACQ time/div boxes while running: every box white except 20, 10, 5 ns; tap 200 ms: the rate becomes 1 kSa/s (the
   rate box highlight moves); tap 50 ns: 200 MSa/s.
5. STOP at 1 MSa/s, 200 us/div, 3000 samples: tap left: already at the record's limit, "Stopped: press RUN to zoom
   further" for 4 s, nothing changes; tap right down to 10 us/div (10 samples a division), then the notice again. The
   rate chip never changes while stopped; in the ACQ menu only the boxes the record can show are white. RUN: the next
   tap continues on the ladder.
6. Single: a tap in the upper half: the time/div and the ladder's rate, a new shot armed (WAIT); after the shot a tap
   in the lower half rescales the shot (record's rate kept).
7. Roll (ACQ mode button): taps step 50 s .. 100 ms/div and never leave roll.
Bug indicators: a fraction of a screen of trace or a flat line at the right while running; a rate that is not the
ladder's after a zoom step; a tap at an end that changes anything; an end the opposite tap cannot leave; the rate chip
changing while stopped; the slider window off the track or the trigger point off screen after a step; a still tap in
the Scope's track that does not jump; a jump from y 436..449.

## 51. fw-spec: legends removed (fw 0.2.75, protocol 1.22 unchanged)

Maintainer 2026-10-19 22:28: spectrum.c lost `spec_draw_legend` and `INFO_Y1` (Waterfall and Density colour legends); the
warning line at the plot's top middle and the rest of both variants are unchanged. No test checked the legend texts.

## 52. fw-spzoom: the Spectrum's zoom ladder, span drives the sample rate (fw 0.2.77, protocol 1.22 unchanged)

Branch `fw-spzoom` from main 25fa3e6 (fw 0.2.76). Brief: docs/plans/task-queue.md "Spectrum zoom ladder" (maintainer 10-19
23:09, "let's try your recommendation"); bench session 23:2x: option (a), the plain rule (the full axis runs at 50 MSa/s,
not 200 MSa/s as the queue item said; fixed there). Commits: 4a85e8a (the rule, docs/ui-guidelines.md section 2 "Span
drives the sample rate", queue wording), 3ec9e17 (spec_ladder.c/.h, acq_view_tb_for_rate, host_tests/test_spec_zoom.c,
test_acq_view.c test_tb_for_rate), 52c1c76 (spectrum.c), d49f9b6 (README "Spectrum view", CHANGELOG), then this
section. Image of d49f9b6 (GCC 10): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 741,376 B, sha256
91946f1598b9b0489606072588c6385b89a419ce34385883aeeca4af86ba316f (a later commit differs only in the identity).
Warnings GCC 10 36, GCC 13 31, as on main; none in the changed files; gcc10-nommu builds. Host tests all green (new
test_spec_zoom 685,416 checks; test_acq_view 1,900,576). **Nothing benched. No protocol change, no settings word,
PC_FW_VERSION not bumped** (the bench session bumps at merge).

1. **The rule** (spec_ladder.c, pure, no globals): a cell = span index 0..23 (1, 2, 5 Hz .. 10, 20 MHz; 23 = the whole
   axis 0 .. 25 MHz, `SPL_FULL`) + centre in mHz, kept inside 0 .. axis (`spl_fit`). `spl_rate(stop, log2n, rates, n)`
   = the slowest of `sample_rate[0..17]` with fs/2 >= stop and N/fs <= 2.048 s (`SPL_TMAX_MS`). `spl_ok` = the zoom-in
   test: stop inside the axis, the slowest *covering* rate's capture within 2.048 s, and the span >= 16 bins
   (`SPL_MIN_BINS`) at it. `spl_zoom` steps the span about the centre (in only to an ok cell; out to the whole axis, or
   with one rate to the widest span its axis holds), `spl_pan` moves the centre, `spl_from_axis` = the cell that shows
   a rate's whole axis (entry, Reset), `spl_auto` = Auto's cell, `spl_bins` = the bins that draw a cell on an axis (floor
   of the start, the last bin at or past the stop, >= 8, clamped to the axis when a hand rate does not cover it).
   A rates table of one entry is a stopped record: its axis, no capture-time limit.
2. **Ends in numbers** (test_end_numbers): zoom out ends at the whole axis, centre 12.5 MHz, 50 MSa/s. From there zoom
   in keeps 12.5 MHz and 50 MSa/s and ends at 1 MHz (N 1024), 500 kHz (2048), 200 kHz (4096): 16 bins of 50 MHz / N.
   At 0 Hz the narrowest cell is 10 Hz at 500 Sa/s (N 1024: the slowest rate already takes 2.048 s, then 16 bins end
   it), 500 Hz at 1 kSa/s (N 2048), 1 kHz at 2 kSa/s (N 4096): there the 2.048 s capture ends it, as the maintainer asked.
   A cell from 0 Hz holds 40-50 % of the N bins (1-2-5 spans on 1-2.5-5 Nyquist steps; tested, "a quarter to a half").
   A pan can reach a cell narrower than the zoom-in end (zoomed in at a high centre, then panned down): `spl_rate`
   holds the rate at the 2.048 s floor there, so no capture ever exceeds 2.048 s at N.
3. **spectrum.c**: `spec.zoom` / `spec.start` (power-of-two fractions of the current rate's axis) are gone; `spec.cell`
   + `spec.cell_set`. `spec_view` = `spec_view_full` (whole axis) + `spl_bins`; Auto's stages draw the whole axis.
   `spec_zoom`: `spl_zoom` over `spec_rates()` (all 18 rates when `acq_view_rate_free()`, else the record's one), then
   `spec_cell_apply` = `spec_rate_set(spl_rate(stop))` while free. `spec_rate_set(r)` writes the rate with
   `acq_view_tb_for_rate(r)` (the Scope ladder's time/div for r nearest the current one, else tbv_snap at r) through
   `spec_tb_write` -> `pc_hal_timebase_write` (its `acq_view_fit` then never snaps; the wait state as before). Plot drag:
   centre0 - dx x span / 720 (Move slow / 5); slider drag and tap to jump: dx x (axis at touch-down) / 726, so a pan
   that changes the rate (and the track's scale) mid-drag keeps the finger's frequency step. Stopped: a zoom the record
   ends but the ladder would continue sets "Stopped: press RUN to zoom further" (4 s, warning line, in the else-if chain
   after "press RUN to fill the view"). Entry keeps the cell when the rate is still the ladder's for it, else
   `spec_cell_from_rate` (the current rate's whole axis, no rate change; at 100 / 200 MSa/s the rule's rate is 50 MSa/s
   and the first step takes it). Span / Center cards show the cell (exact 1-2-5) while `spl_bins` holds all of it,
   else what is drawn. FFT length row: "2048 (record)" when the record limits the choice.
4. **Auto**: probes unchanged (50 MSa/s at TB 26 via `spec_tb_set`, the 10x period search by time base index); derive
   is `spl_auto` (narrowest span from 0 >= max(1.25 f_hi, 20 f0), narrower while RBW > f0 / 4, never past the zoom-in
   end) and the rate `spl_rate` of it; finish sets the cell and the rate (the time-out path may change the rate then).
   Its result is a ladder cell (test_auto). Expected on the 1 kHz square (derived, not benched): N 2048 100 kHz from 0
   at 200 kSa/s, N 4096 200 kHz at 500 kSa/s (0.2.76: about 80 kHz at 200 kSa/s).
5. **Hand rate** (rule 4): the Spectrum has no "Set by" chip (only Frequency response has "Set by / the sweep", fw
   0.2.71); its ACQ menu is the Scope's, rate boxes included. A rate set there or by the host holds; the view draws the
   cell on that axis as far as it reaches; the next zoom or pan step takes `spl_rate`. Kept as is.

Decisions to confirm with the maintainer / not done:
- **Option (b)**, the one-line alternative the bench session named: the whole axis (or any stop at 25 MHz) at 200 MSa/s
  instead of 50 MSa/s: in `spl_rate`, `if((n > 1) && (stop >= SPL_FMAX_MHZ)) return 0;` (and test_spec_zoom's 50 MSa/s
  expectations, test_rate / test_walks / test_end_numbers, move to index 0).
- **The Time card is the waterfall's history span**, not the capture time the brief mentioned; nothing shows N / fs.
  If the maintainer wants it, a card "Capture" = N / fs (ui_meas.h kinds) is the place.
- **Capture time is N / fs as the maintainer put it; the record actually takes depth / fs.** At 16k memory and 2 kSa/s
  (N 4096) a record takes 8 s although N / fs = 2.048 s. Using `fpgasettings.totalsamples` instead of N in
  `spl_time_ok` is the fix if the bench shows long waits.
- Changing the FFT length (or the memory depth) does not change the rate; the next step uses the new N.
- A drag across rate boundaries blanks the view at each crossing ("Waiting for a capture at the new rate") until a
  capture arrives; the touch loop blocks, so the new spectrum shows after the finger lifts (as before).
- Auto's probe and period search still go by 1.00o5 time base indices; the snap notice of `acq_view_fit` could arm
  there (as on 0.2.76).
- Section 20's "span drives the time base past the ends (`spec_zoom`, `spec_tb_set`)" is history; this section is
  current.

**Bench walk-through (maintainer, bench session on <port>; FEL load only)** - FEL-load (`sunxi-fel -p write 0x7FFFFFE0
build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.76,...` (version not bumped). CH1 on the 1 kHz
calibration post; Spectrum analyser, RUN, trigger Auto; MEASURES with Span, Center, RBW on the cards.
1. Whole range at N 2048 (memory 3000): Reset (whole axis of the current rate), tap left until nothing changes: Span
   "25.00MHz", Center "12.50MHz", ACQ chip 50 MSa/s; one more left tap: nothing. Tap right repeatedly: 20, 10, 5, 2, 1
   MHz, 500 kHz about 12.5 MHz, the rate stays 50 MSa/s; one more: nothing; left works at once. Drag the slider window
   to the left end: the rate falls as the stop falls (each crossing waits for a capture). Taps keep the centre, so to
   reach the 0 Hz end tap right and drag the window back to the left end after each tap: 200 kHz .. 500 Hz from 0
   with the rate following (500 Hz at 1 kSa/s, about 2 s per capture), then a tap right changes nothing. Without the
   drags the end comes at 16 bins of the rate the centre needs (centre 250 kHz: 5 kHz at 500 kSa/s). Tap left back to
   the whole axis: every step's rate is the slowest whose half covers the stop (chip vs Span + Center).
2. Repeat at N 1024 (FFT length) and N 4096 (memory 6k or 16k): from the whole axis the end is 1 MHz / 200 kHz; at 0 Hz
   10 Hz at 500 Sa/s / 1 kHz at 2 kSa/s. With N 4096 at 3000 memory the FFT row reads "2048 (record)".
3. Pan across a rate boundary: 1 kHz span centred at 2 kHz (5 kSa/s); drag the plot right-to-left a little (stop past
   2.5 kHz): the chip goes to 10 kSa/s and "Waiting for a capture at the new rate" shows until the next capture.
4. Hand rate: ACQ menu, pick 200 MSa/s while showing 10 kHz from 0 (20 kSa/s): the view shows the same span on the
   200 MSa/s axis (cards may read the drawn span), nothing else changes; close the menu, tap right: the ladder's rate
   (20 kSa/s for 5 kHz about 5 kHz), not 200 MSa/s.
5. STOP, then tap right: zooms on the record's rate down to 16 bins; tap left: up to the record's axis, then "Stopped:
   press RUN to zoom further" (4 s) where the running ladder would go on; the chip never changes while stopped. RUN:
   the next tap continues on the ladder.
6. AUTO SET on the 1 kHz square: "Auto...", then a span from 0 Hz in the 1-2-5 ladder (100 kHz at 200 kSa/s with N
   2048 expected); one tap left / right from there moves one ladder step with the rule's rate.
7. Cards and labels: at every step the Span and Center cards read 1-2-5 values and the frequency labels' first / last
   grid lines sit at the ends of the span; RBW = 1.5 (Hann) x fs / N. Waterfall and Density: the same taps and pans,
   the history clears at each rate change, no stale rows.
Bug indicators: a rate that is not the slowest covering the stop after a step; a capture over about 2 s at N while
running; a tap at an end that changes anything; an end the other direction cannot leave; the chip changing while
stopped; Span / Center off the 1-2-5 values while the rate covers the span; the slider window off the track; a time/div
snap notice in the Scope after using the Spectrum.

**Addendum, fw-spzoom2 (fw 0.2.81 provisional, protocol unchanged): pans never change the rate.** Maintainer 2026-10-20
10:24 on the 0.2.80 bench (task queue "Spectrum ladder: pans never change the rate"): the rate changing mid-scroll felt
janky. Branch `fw-spzoom2` from main 58c17a3, commit 9294f9e (code + test) and the docs commit after it. The rate is
derived (`spec_cell_apply` -> `spl_rate`) only in `spec_zoom` and in Auto's finish; `spec_pan_by` (plot drag, slider
drag, tap to jump) calls `spl_pan` with the *current* rate's axis (`spl_axis_mhz(&sample_rate[cur], 1)`, the slider
track) and only marks the cell set. New `spec.cell_rate` (rate when the cell was last set): entry keeps the cell when
the rate is that one or the ladder's for the cell, so a panned cell survives leaving and re-entering. This supersedes
item 3's "slider drag ... axis at touch-down" note, item 2's "a pan can reach a cell narrower than the zoom-in end"
(a pan keeps span and rate, so the bin count is unchanged) and bench step 3 above. Waterfall / Density still clear
when the drawn window moves (`spec_hist_key` holds first / span), a pan included: their rows are bins of that window;
not changed. test_spec_zoom: `test_pan_keeps_rate` (every span x 64 centres x N 1024..4096: random pans keep the rate
and stay inside the axis, +/-25 MHz pans clamp to stop = axis / start = 0 with a further 1 Hz doing nothing, a tap
either way afterwards is the rule's pair), `test_random` checks the rate after every pan, `test_hand_rate` (a pan on a
hand rate whose axis is narrower than the span centres it, the rate kept). GCC 10 36 / GCC 13 31 warnings as on main;
GCC 10 image 746,496 B. **Not benched.** Bench steps (CH1 on the 1 kHz post, RUN, MEASURES Span / Center / RBW):
1. Tap left until nothing changes: whole axis, 50 MSa/s. Tap right twice (10 MHz about 12.5 MHz, 50 MSa/s), drag the
   slider window (or the plot) to the left end: 0 .. 10 MHz, the ACQ chip stays 50 MSa/s and time/div does not move;
   no "Waiting for a capture" while dragging.
2. Tap right there: 5 MHz about 5 MHz, the rate drops to 20 MSa/s (the zoom re-derives from the new centre). Keep
   tapping right at 0 Hz: each step takes the ladder's rate, ending as before (500 Hz at 1 kSa/s for N 2048).
3. Pan up against the axis end (plot drag left, or slider window to the right end, or a tap at the track's right end):
   the window stops with its right edge on the track end (stop = fs / 2), the rate unchanged; further dragging does
   nothing. Tap left to widen (the rate rises), then pan further up.
4. AUTO SET on the 1 kHz square: a ladder cell from 0 Hz with the ladder's rate (as 0.2.80); a pan afterwards keeps it.
5. Leave to the Scope and come back without touching the rate: the panned cell is still shown.
Bug indicators: any rate or time/div change during a drag or tap-to-jump; the slider window past the track end; a zoom
step after a pan that is not the slowest rate covering the new stop.

**Addendum, fw-acq2 (fw 0.2.84 provisional, protocol unchanged, settings word 217): the follow-ups (a)-(c).** Branch
`fw-acq2` from main 83e3b32 (fw 0.2.83). Commits 5bfe097 (ladder, (b) and (c)), c449a5c (Capture card, (a)), then docs.
GCC 10 image of c449a5c 751,616 B, sha256 209176c35d12ae180c261af81a62eea65f68f74581366c26b25ebb9fc8ff50af; warnings GCC
10 36, GCC 13 31 as on main. Host tests green (test_spec_zoom 1,671,057 checks, test_ui 1,432,148). **Not benched.**
- **(b) The capture time is the record's, memory depth / fs** (`fpgasettings.totalsamples`, `spec_cap()`), chosen over
  capping the memory to N in the Spectrum: the pair stays a function of (cell, depth) with no per-mode meaning of the
  memory setting and no change to the record the Scope and the host see. `spl_rate(stop, cap, rates, n)` no longer
  takes N; `spl_ok` / `spl_zoom` / `spl_auto` take both (N for the 16-bin end, cap for the time). The 0 Hz end is now
  set by the depth: 1 kHz at 2 kSa/s (3000), 2 kHz at 5 kSa/s (6000, 8190, 9000), 5 kHz at 10 kSa/s (12000 .. 16382);
  item 2's "500 Hz at 1 kSa/s (N 2048)" and "10 Hz at 500 Sa/s (N 1024)" are history (they assumed a record of N).
- **(c)** `spec_follow_cap` at the start of `spectrum_display_body`: a depth change (ACQ menu depth or Filter, host
  SET_TRIGGER total / SET_DECIM) re-derives the rate at once when the rate is `spec.lad_rate` (set by a zoom step, Auto's
  finish, and at entry / Reset when the rule gives the current rate); a hand-picked rate holds, a pan keeps the ladder's
  (so the re-derivation uses the panned stop, as a zoom step would); stopped or during Auto it waits. The ACQ menu is
  modal, so the change lands when it closes. An FFT length change needs nothing: N is not in the rate. Entry still
  never changes the rate. Inference to watch: a host that sets the rate with SET_TIMEBASE to the value the ladder set
  and then changes SET_TRIGGER total in the Spectrum view gets the ladder's rate for the new depth (protocol.md does
  not promise otherwise; no host flow does this today).
- **(a)** Capture card (`UI_MS_SP_CAPTURE`, the view group's fifth item; `UI_MS_SP_PER_GROUP` 5, `UI_MS_SP_CH_ITEMS` 4,
  `ui_ms_sp_len`), value `ui_fmt_record_time(depth, fs)` at the record's rate (decimation included). Default on after
  RBW and Time; with both channels shown the default row overflows ("+2" outside the waterfall; it was exactly six).
  Its rank is word 217 (0xCA00 | rank); `ui_ms_spec_restore` of the view leaves the ranks raw and
  `ui_ms_spec_capture_restore` must follow it (scope_functions.c does). The Time card is unchanged.
- test_spec_zoom's "quarter to half of the bins" check never ran (its reference was asked for N 2^30); it runs now,
  excluding the slowest rate's floor.
Bench steps (CH1 on the 1 kHz post, Spectrum, RUN, MEASURES with Span, Center, RBW, Capture):
1. Capture card: memory 3000 at the whole axis (50 MSa/s): "60.00us"; memory 15000: "300.0us". ACQ rate 1 kSa/s by
   hand at 3000: "3.000s".
2. Zoom in to the end at 0 Hz (tap right, drag the window to the left end after each tap) at 16k memory (15000), N
   4096: it ends at 5 kHz from 0 at 10 kSa/s, Capture "1.500s"; no capture takes longer than about 2 s (0.2.83 went
   to 2 kSa/s, 8 s). At 3000: 1 kHz at 2 kSa/s, "1.500s".
3. Depth follows: at the 3000 end (1 kHz, 2 kSa/s), ACQ menu, memory 15000, close: the chip goes to 10 kSa/s at once
   ("Waiting for a capture at the new rate"), Span still 1 kHz; back to 3000: 2 kSa/s. Filter on at 15000: the depth
   drops to 6000 and the rate to 5 kSa/s at once. A rate picked by hand first, then a depth change: the rate stays.
4. N change: Analyse > FFT length 1024 / 2048 / 4096: the ACQ chip never changes; at the whole-axis end (500 kHz at N
   2048) going to N 1024 leaves the span, tap right does nothing, tap left works.
Bug indicators: a running capture over about 2.1 s (Capture card); a rate change on an FFT length change or on entering
the view; a hand-picked rate replaced by a depth change; Capture missing from the MEASURES view row (five cells).
## 53. fw-yield: host commands leave the modal screens (fw 0.2.78, protocol 1.23, row 63)

Branch `fw-yield` from main dd46518 (fw 0.2.76). Maintainer 2026-10-19 23:27 (task queue "Host commands leave the modal
menus"): "if the agent sends commands it just exits all modals, since that wouldn't be destructive; just exiting to
the base screen". Rule: `docs/ui-guidelines.md` section 4 (two bullets); wire: `docs/protocol.md` section 14 row 63,
the LOCAL_BUSY row of 6.1, the 8.1 E_BUSY line, section 9. Commits: 0493025 (session, glue, hooks, note string, host
tests), 888324c (the modal loops), 0f63317 (mcp, simulator, vectors), then the docs and this handoff. **Nothing
benched.** Host tests green (test_session 70093 / 70091 with the new `test_modal_yield`, test_ui, test_diag,
check_vectors); mcp ruff clean, pytest green except `test_report` (needs matplotlib, not in the local venv).

**Mechanism** (`pc_cmds.c` `pc_ui_yield`, `pc_ui_yield_pending`):
- A request without F_BUSY that reaches the dispatcher from the timer poll or a BUSY poll while `ui_busy` (no MAIN
  poll for PC_MODAL_MS 250 ms) no longer gets E_BUSY "UI busy". If `pc_hal_ui_hold()` is 0 it sets
  `pc_session.yield_req` (+ `yield_ms`, counter `yields` = GET_LINK_STATS 68) and stays queued (`pending_ev`, re-run
  every tick). The MAIN poll clears `yield_req` and answers the request. With a hold reason, or when no MAIN poll comes
  within PC_YIELD_MS 1500 ms of the flag, it answers E_BUSY detail 3/4/5 (the hold) or 2 ("UI did not return to the
  main screen"), counter `yield_refused` = 69. GET_LINK_STATS 70 counters (pc_report.c names `ui_yields`,
  `ui_yield_refused`). PROTO_MINOR 23. Detail 1 ("UI capture running", READ_RAW) and 0 ("deferred request pending")
  as before.
- Glue (`pc_hal_scope.c`, next to `pc_glue_poll_isr`): `pc_glue_ui_yield()` = `PC_UI_YIELD()` (1 while
  `pc_ui_yield_pending`: flag set and no hold; it also starts the note), `pc_glue_ui_hold(why)` returns the previous
  reason (nest-safe: `prev = pc_glue_ui_hold(PC_HOLD_SAVE); ...; pc_glue_ui_hold(prev)`), `pc_hal_ui_hold()`,
  `pc_glue_yield_note()` = `ui_yield_note` ("Host command: menu closed", ui_ctrl.c, test_ui checks <= 50 chars and the
  width) for UI_YIELD_NOTE_MS 4000 ms after the last yield. PC_HOLD_* in pc_hooks.h equal PC_BUSY_* in pc_cmds.h
  (compile-time check in pc_hal_scope.c).
- The note: Scope (`pc_glue_scope_warning`, white, ahead of the warnings and the snap notice; not in the waveform
  viewer) and Frequency response (`bode_view.c` `warning()`, first). **Not in the Spectrum view** (spectrum.c belonged
  to fw-spzoom while this ran): one line in `spec_draw_warnings`, `if(pc_glue_yield_note()) p = fmt_str(p, ...)`,
  is a follow-up (task queue). Diagnostics shows no note (it is a main-loop mode; its own dialogs yield).

**Modal loops changed** (each checks `PC_UI_YIELD()` right after its touch scan and takes its own cancel path):
- statemachine.c: main menu + Settings fold-out, brightness sliders, RTC fold-out (`close_open_menus(1)`), channel
  menu with ref / math sub menus (`close_open_channel_menus(settings, 1)`), ACQ menu (return), trigger menu
  (`close_open_trigger_menus(1)`), generator menu (`close_open_gen_menus(1)`), the generator keypad (puts the current
  frequency / duty back into the buffer first: its caller applies the buffer after any exit, 1.00o5 applied the typed
  number even on a tap outside), Measures (return), the file browser (`scope_preset_values`, as Return), the picture
  viewer (Return), the waveform viewer loop inside the browser (`waveviewmode = 0`, as the menu button's return).
- ctrl_page.c `cp_fold_run`, bode_view.c `fold_run` (CTRL fold-outs: closed as a tap outside), bode_view.c
  `bode_view_top_note` (closed) and `file_message` (dismissed, hold released while it waits).
- ui_settings_ui.c: `ui_confirm` (Cancel; covers the viewer's delete, Restore default settings), `ui_settings_run`
  (Back: the boot byte is written if it changed, as Back does: the person changed it), `run_time_page` (Back).
- ui_dlg.c `ui_dlg_confirm` (Cancel: Clear calibration, Enter FEL). ui_diag_view.c: `ud_volts_flow` between steps
  (Cancel: the factors go back), `ui_first_run` (Later). cal_wiz_ui.c `cal_wizard_run` between steps (CW_EV_CANCEL,
  CW_ACT_DISCARD: nothing written). scope_functions.c `scope_display_file_status_message` (dismissed; hold released
  while it waits, since the file operation is over).

**Holds (E_BUSY with the reason instead):** cal_wiz_ui.c `wiz_run_actions` (every measurement action and the Save
commit), ui_diag_view.c the volts calibration's `scope_input_calibration` step (3); statemachine.c SAVE PIC / SAVE
WAVE / the Frequency response CSV, ui_diag_view.c `ud_save_report` (4); menu.c `scope_setup_usb_screen` (5).

**Not changed, and why:** the USB drive screen (exception; the serial port is gone there anyway); the touch-drag loops
(`while(havetouch)` in scan_for_touch, bode_view.c, spectrum.c: the finger is on the glass, they end on release);
`tp_i2c_wait_for_touch_release` waits; test.c (timed waits); Diagnostics, Spectrum, Frequency response (main-loop
modes, the request is answered there without closing anything; fw 0.2.96 leaves Diagnostics first, section 63); auto set and slow UI captures (no modal loop: the
request waits for the main loop, E_BUSY detail 2 after 1.5 s). BODE_CTRL (every action, also status) and READ_BODE
have no F_BUSY flag, so they close a menu like any main-loop command.

**Simulator and host** (0f63317): `protocol.py` `YIELD_MINOR`, `BusyDetail`, `BUSY_TEXTS`, `busy_hint` (DeviceError's
E_BUSY hint), link names 68-69; `server.py` `ensure_control` proceeds with `MENU_CLOSED_NOTE` on 1.23 and raises the
old "close it on the scope" error below it; the simulator's `cfg.proto_minor` (below 23 = old behaviour), `ui_hold`,
`ui_yields`, `ui_yield_refused` (its GET_LINK_STATS stays the 31-counter 1.2 layout, as before).

**Bench list** (maintainer present, bench session on <port>; `fnirsi1013d-bench` or the MCP tools; `#VER` fw=0.2.78
proto=1.23):
1. Main menu open, idle 1 s: `scope_status` shows ui_busy 1 and the menu stays. `scope_set_timebase`: the menu closes,
   "Host command: menu closed" white at the top middle for about 4 s, the tool answers with the menu caveat, the
   time base changed.
2. The same with each: Settings fold-out; the brightness slider open; channel menu CH1 and CH2 (also with the ref menu
   open); ACQ; trigger; generator; the generator keypad with a half-typed frequency (closes, the generator keeps the
   old frequency); Measures; the CTRL page's Display and Trace style fold-outs; Settings screen (and its Time and date
   page); Saved pictures browser; a picture open; Saved waveforms with a waveform open (back to the scope, settings as
   before the viewer); the Clear calibration and Enter FEL dialogs (Cancel: nothing cleared, no FEL); the delete
   confirmation in a viewer (nothing deleted). Frequency response: its Display / Sweep / Thru fold-outs and the ACQ
   note (the note in its warning line).
3. Nested: main menu > Settings fold-out > Other settings (the Settings screen) > a host command: all three close,
   one note, the command answered. CH1 menu with its ref sub menu open: both close.
4. Wizard: Diagnostics > Calibrate, on screen 1 send a command: the wizard closes (nothing written, Diagnostics
   stays). Start the zero step, send a command while "Measuring...": E_BUSY "calibration measurement running"
   (detail 3) until the step ends; then the next command cancels the wizard between steps.
5. SAVE WAVE with the file message on (Settings > File message: asks first), send a command during the write: E_BUSY
   "file save in progress" or, once the message waits, the message is dismissed and the command answered.
6. First run (a card without calibration, or Clear calibration then power-cycle): the first-run screen; a command
   answers it with Later.
7. `GET_LINK_STATS` 68 counts the yields, 69 the E_BUSY answers; GET_STATUS ui_busy stays truthful (1 while modal).
8. Auto set running (a long one at a slow time base), a SET_* sent at once: answered after it or E_BUSY detail 2 "UI
   did not return to the main screen" after 1.5 s, never a hang.
Bug indicators: a dialog that took its action (a deletion, a cleared calibration, FEL) on a host command; the keypad's
typed number applied; a menu that stays open while the command times out; a screen left half drawn (a fold-out's
panel or a menu's background not restored); the note stuck on screen; a second menu opened later closing at once
without a host command.
Addendum fw 0.2.81 (fw-yield2, maintainer 2026-10-20 10:31): the note is removed (`ui_yield_note`, `UI_YIELD_NOTE_MS`,
`pc_glue_yield_note` and its Scope / Frequency response callers, the test_ui check); the yield unwinds silently, items 1-8 above minus the note.

## 54. fw-cap: FPGA 0x153A capture stages, the wizard's zero back at 128 (fw 0.2.79, protocol 1.24, row 64)

Branch `fw-cap` from origin/main 6125aee (fw 0.2.78, 0x153A merged). Brief: firmware for bitstream 0x153A (fpga/al3/AGENTS.md
"0x153A status" firmware notes, README section 10) and the maintainer's fail-fast decision of 2026-10-19 20:30 (task queue
"Wizard zero point back at code 128"). Commits: 381dd8a (firmware and host tests), then the docs / mcp / vectors commits
of the helper agent and this handoff. **Nothing benched, nothing flashed** (the scope was off). Host tests all green
(new test_cap_stage 85, test_session 70133 with `test_capture_stages`, test_ui_diag 6644, test_cal_wizard 672,
test_adc_cal 59843, check_vectors OK); GCC 13 31 warnings (as main), GCC 10 builds clean in the changed files.

**What was built**
- `cap_stage.c/.h` (pure): the 0x63 fields (`cs_stage`, `cs_with`, `cs_ref_bit`), `cs_has` (0x153A..0x153F), the training
  `cs_train_run` (all trained cores at stage s = 2, 3, 4, 5, one capture each, a core is clean at s when its 0x36 count is
  0; `cs_pick`: of {3, 4} the clean one whose outer neighbour 2 / 5 is clean too, the power-on stage when the clean run is
  symmetric, else the clean one of {3, 4}, else a clean reserve; a B core with no clean stage is measured again with its
  chip's reference bit, 0x63 bit 12 / 13; a core with no clean stage at all is CS_E_DIRTY and nothing changes), the
  self-check (`cs_excess`: a core's count minus its chip partner's, flagged above CS_EXCESS_LIMIT 8), texts ("CH1 A4 B4,
  CH2 A3 B4", "B4 A-clk" with the reference bit) and the sector 708 trailer 'ACAP' at bytes 488..503 (v1, 16 bytes, the
  0x63 value, the FPGA word it was trained on, CRC-32). The v3 block and its version are unchanged (0.2.45+ still read
  the zeros and gains); the brief said "calibration data version bump": the trailer has its own version instead, so an
  older image keeps the calibration (its own 708 save clears the trailer, as it does the provenance).
- `cap_hw.c` (target): `fpga_capture_init` (pc_glue_init, after fpga_encode_init) writes 0x64 = 8 and 0x63 = the stored
  value when it was trained on the running bitstream, else 0x08E4 (always written: a FEL load leaves the FPGA as the
  previous firmware left it; 0x63 is a plain register, zaklad.v `cap_sel_x`, not touched by the sampling reset, so boot
  and training are the only writes); `fpga_capture_loaded` / `_save` from pc_diag.c's calibration hooks;
  `fpga_capture_check` reads 0x36 after every accepted full-rate capture (enc_heal_hw.c for UI / wizard / probe
  conversions, `pc_hal_cap_check` from pc_cmds.c's host ARM path), flags, event 23 when the flagged set changes;
  `fpga_capture_train` (self-check off while training; event 23 a = 0x10 | bad cores, b = the value in effect).
- Detection: caps bit 19 CAPTURE_STAGES, features bit 7 (0x0080) on 0x153A..0x153F (`cs_has`; 0x153B is expected to be
  gen8-features rebased on this capture). `pc_decim_has_caps` adds PC_DECIM_HAS_CAPSTAGES: legacy EXT shift 12 samples at
  divider 0 and 14 above (0x1539: 4 / 8, from tb_trigpos: EXT +14..15 / +16..17 / +17..18 against CH1 +2..3), K >= 1 lag
  + 40 ns; 0x53 bits 3 / 4 masked to 0. fpga_bus.c's contract table needs no new entry (read_dummy 1 for every AL3; gen9's
  contract transcript for 0x153A equals 0x1539's apart from the version and data bytes).
- GET_HEALTH 124 bytes, layout byte 2: 104 0x63 in effect, 106 flags (PC_CAPF_HAS, TRAINED, READ, FLAGGED, OTHER), 107
  flagged cores, 108 the four counts, 116 reads, 120 flagged captures. GET_CAL 364 bytes: 356 trailer state, 357 flags,
  358 stored value, 360 its FPGA word, 362 value in effect. Event 23. Report file line "ADC capture stages (0x63): ...".
- Diagnostics, Calibration page, first line under the channel blocks (y 374; the trim line and the note moved 15 px
  down each): "ADC capture stages CH1 A4 B4, CH2 A3 B4 (trained): capture clean", amber "...: CH2 A: 37 outliers",
  "(not trained: calibrate)", amber "(trained on 0x153b: calibrate)", "no 200 MSa/s capture yet". Nothing without stages.
- Wizard (cal_wiz_ui.c): `cal_centre` factored out of `cal_range`; the zero of each class is measured at the refined
  centre again (8 gated, 64 full-rate conversions; `acal_zero_split`, `acal_zero_pair`, `acal_point_set_pair` and the
  ACAL_ZERO_SPLIT constants are gone; CAL_PROBE byte 248 is 0). On 0x153A `cal_channel` first runs `cal_train`: range 0,
  `cal_centre`, the gated slope points' within-capture RMS <= ACAL_MAX_NOISE16 (else ACAL_E_NOISE: a signal counts at
  every stage), the PWM at the centre, 200 MSa/s, `fpga_capture_train` with `cal_cap_measure` (0x63, an 8000-word
  conversion through fpga_do_conversion, 0x36). A failed training is ACAL_E_READOUT ("ADC readout fault near code 128:
  check the FPGA bitstream"); the note says "Capture stages: no clean stage for CH2 A" or, after a pass, "Capture stages:
  CH1 A4 B4" (cal_wiz.c `cw_cap_result`, test_cal_wizard). Save calls `fpga_capture_commit` before the sector write;
  Cancel (CW_ACT_DISCARD) and CAL_PROBE mode 1 write the value from before back.

**Known limits (inference, not benched)**
- The self-check compares the two cores of a chip: both cores of one chip garbled at once (the 0x1538 CH2 pattern) would
  not be flagged after normal captures. The wizard's training sees it (per-core counts at a quiet input).
- A large signal near 100 MHz could make the cores' counts differ by itself (amber line without a fault).
- The training runs at range 0 only; the stages do not depend on the range (the capture is digital), the input level does.
- cal_probe.py still prints "+- 0" for the zero split.

**Bench list** (maintainer present, bench session on <port>; fw 0.2.79 FEL-loaded on the unit with 0x153A flashed per
fpga/al3/README.md section 10 and docs/hardware-status.md 00:30; back up SD 700-710 before the wizard's Save):
1. Identity (README 10 item 1): `#VER fw=0.2.79,proto=1.24,fpga=0x153A`; GET_INFO caps bit 19 (0x80000) and bit 18;
   GET_STATUS 44 = 0x00FF; `s.health()`: layout 2, 104 = 0x08E4, 106 = 0x01 (HAS, not trained); FPGA 0x36 via
   READ_REG / DEBUG if available reads 0x08E4. After one 200 MSa/s capture with nothing connected: 106 bit 2 set, counts
   108..115 all 0, 107 = 0; the Calibration page line "(not trained: calibrate): capture clean".
2. dcoffset sweep at 200 / 100 / 50 / 20 MSa/s, range 0 (README 10 item 2; host): every core clean at means 126..146;
   GET_HEALTH 120 (flagged) stays 0 at the quiet points.
3. RC low-pass with the 1 kHz square (item 3): no rails, no jumps; Frequency response unflagged; GET_HEALTH flagged
   captures 0 (both cores of a chip see the edges alike).
4. The maintainer runs the wizard (item 4, on the screen): Zero level shows "Capture stages: CH1 A4 B4" / "CH2 A3 B4" (or a
   neighbour) under the two ticks, then the gain screens and Save. Then GET_CAL 356 = 1, 358 = the value, 360 = 0x153A,
   357 bits 0-1; Calibration page "(trained): capture clean"; power-cycle (FEL-load again): 104 = the trained value.
5. Training cross-check (item 5, bench session over USB): CAL_PROBE mode 1 on CH1 then CH2 runs the zero step with the
   training (about 15 s each, nothing stored, 0x63 back afterwards); GET_EVENT_LOG type 23 entries a = 0x10, b = the
   trained value. The reference-bit test (bit 12 / 13 with each B stage) needs raw 0x63 writes: a PC_DEBUG image or a
   short host script with DEBUG_FPGA; expected: every B stage counts about every word with the A-encode reference.
6. EXT offsets (item 6): legacy EXT at dividers 0, 1, 3, 9, 199 with the calibration pin loopback (fw-bus B5 recipe,
   section 48): `capture_info().ext_shift` 12 at divider 0, 14 above; the CH1 edge at the pretrigger within +-3
   samples. 16-bit (Filter CIC2+FIR, 1 MSa/s): the edge within 1 output sample.
7. Warm-up (item 7): hair dryer on the FPGA, repeat 2: counts stay 0; if a core flags, the Calibration line turns amber
   and event 23 names it; then re-run the wizard (the training should move that core's stage).
8. Fail-fast check (optional, needs 0x1539 back in the flash, so only if it is reflashed anyway): fw 0.2.79 on 0x1539
   has no stages; the wizard measures at 128 and is expected to fail CH1 / CH2 with "ADC readout fault near code 128"
   (the A cores at 128..145): that is the maintainer's intended behaviour.
Regression signs: the wizard passing on 0x153A with no "Capture stages" note (cs_has / init order), GET_HEALTH layout 1,
EXT edges 8 samples early (shift table not followed), the trained value lost after a power cycle (trailer not saved).

**Open**
- Bench session: protocol row 64 and the version numbers are provisional; the mcp, vector and doc commits are on this
  branch (see the commit list); docs/hardware-status.md gets the bench results.
- 0x153B (gen8-features on top of 0x153A): `cs_has` already covers it; add its own caps when it lands.
- A per-core absolute check (both cores of a chip) would need a quiet-input moment outside the wizard; not built.

**Addendum (bench session, fw 0.2.80, 2026-10-20 10:15).** Bench on 0x153A: the training's clean criterion failed on CH2 because the
0x64 threshold of 8 codes is under CH2's analog noise at range 0 (about 3 codes rms near code 128; the counters saturate at 65535
on noise); the firmware now writes 32 (`CS_THR_USE`), with which a clean stage set reads 0 on every core and a bad stage reads
hundreds (CH1 A4) or 65535 (CH2 B2). The stage map of this unit: CH1 A bad at 4 only, CH1 B bad at 2, CH2 A clean everywhere,
CH2 B bad at 2, reference bit without effect; expected training result 0x08E3. docs/hardware-status.md 10:14.

**Addendum (fw-cap2, fw 0.2.81 provisional, 2026-10-20; branch fw-cap2, commits 3e5a2a1 firmware + host tests, 3f47d78 mcp,
then docs).** Not benched, nothing flashed; PC_FW_VERSION not bumped. The 0.2.80 wizard still kept CH1 A4 (0x08E4): A4 garbles
only at codes 127..135 and not in every capture, which one capture at the centre missed.
- **Band training** (`cap_stage.c` `cs_train_run` / `cs_pass`, `cs_io` now has `points`, `reps`, `park`): per point
  (`cs_band_code` 120, 124, .. 140; `cal_wiz_ui.c` `cal_cap_park` sets `cal_band_pwm[p]` = `cs_band_pwm` from `cal_centre`'s
  gated slope points, mean of both cores, 60 ms settle) every stage 2..5 with `CS_BAND_REPS` = 2 captures; `cs_train.sum` /
  `sum_ref` hold the summed counts, clean = sum 0. A band PWM outside ACAL_PWM_MIN..MAX fails as ACAL_E_RESPONSE.
- **Pick** (`cs_pick`): score = run width, then 3 / 4 over 2 / 5, then the distance to the nearer end of the run, then the
  power-on stage, then its neighbour. {2,3,5} -> 3, {3,4,5} -> 4, {2,3,4} -> 3, {2..5} / {3,4} -> power-on, {2,5} -> next
  to power-on.
- **Time**: 6 x 60 ms settling + 48 captures (8000 words, about 80 us of acquisition plus about 0.3-0.5 ms of bus work each)
  is about 0.4 s per pass, a second pass only for a B core with no clean stage; plus the centre search as before. A
  channel's zero step grows by about 0.4 s (0.8 s with the reference pass). The wizard shows "CHn: training capture
  stages..." (`cw_progress(.., CW_PR_TRAIN)`).
- **Limit** (inference): a stage that garbles at only one band point in 30 % of the captures is missed with probability
  0.7^2 = 49 %; at two points (the A4 case) 0.7^4 = 24 %. If the bench shows the miss, raise `CS_BAND_REPS` (each step adds
  24 captures, about 15 ms) before touching the pick. The in-operation watch catches what the training misses.
- **Repeat watch** (`cs_watch`, `cap_hw.c` `fpga_capture_check`): a core flagged in 3 captures within 60 s sets
  PC_CAPF_REPEAT (GET_HEALTH 106 bit 5) and logs event 23 once per core (a = the cores that reached it); the old "event 23
  when the flagged set changes" is gone. `fpga_capture_commit` (the wizard's Save) clears it. ui_diag.c: the Status
  verdict "ADC capture flagged 3 times within a minute: run Calibrate." (after the FPGA-changed check), the Calibration
  page line "...: last capture clean, flagged 3 times in a minute: calibrate" or "...; calibrate"; pc_report.c says it.
  mcp: `CAPF_REPEAT`, `describe_capture()["flagged_repeatedly"]`. docs/protocol.md row 64 updated (bit 5, event 23 timing,
  the band training).
- Host tests: test_cap_stage 147 (the maintainer's unit as a mock: CH1 A4 bad at points 2, 3 in 30 % of captures,
  deterministic; the band picks 0x08E3, the 0.2.79 single-capture training on the same mock keeps A4; `cs_band_pwm`,
  `cs_watch` incl. tick wrap), test_cal_wizard 677, test_ui_diag 7213; all `make -s test` green, GCC 10 clean.

**Bench steps (fw 0.2.81, 0x153A, maintainer present, bench session on <port>; back up SD 700-710 before Save):**
1. `#VER fw=0.2.80...` (version not bumped) and GET_HEALTH 106 bit 5 = 0 after boot.
2. CAL_PROBE mode 1 on CH1, then on CH2 (`python firmware/tools/cal_probe.py <port> both all 1 0 0 1` or the MCP tool): the
   screen shows "CH1: training capture stages..." for about half a second before "measuring range 1 of 6"; GET_EVENT_LOG
   type 23 entries a = 0x10, b = 0x08E3 for both channels (CH2's power-on A3 B4 already equals its expected result);
   0x63 back to the value before afterwards (GET_HEALTH 104).
3. The wizard on the screen: Zero level notes "Capture stages: CH1 A3 B4" and "Capture stages: CH2 A3 B4"; Save; GET_CAL
   358 = 0x08E3, 360 = 0x153A; GET_HEALTH 104 = 0x08E3 with TRAINED.
4. The 200 MSa/s band sweep (dcoffset sweep, range 0, codes about 110..150 in steps of 2, 20 captures per point; README 10
   item 2): every core 0 outliers (GET_HEALTH 108..115) and no rail samples at any point, GET_HEALTH 120 stays 0, 106 bit 5
   stays 0. If CH1 A3 garbles at some code, record which; the next step would be CS_BAND_REPS or a wider band.
5. Repeat watch (optional, needs a PC_DEBUG image for a raw DEBUG_FPGA write of 0x63 = 0x08E4) with the input parked at
   code 128..132 by the offset, 200 MSa/s captures for a minute: the Status page Calibration verdict turns amber with
   "run Calibrate", one event 23 entry for CH1 A only.

## 55. fw-fr3: Frequency response small items, AUTO SET never loops (fw 0.2.84, protocol 1.24 unchanged)

Branch `fw-fr3` from main 83e3b32 (fw 0.2.83). Brief: task-queue "Frequency response small items" (a)-(c), the AUTO SET
absurd-point rule (d) and the maintainer's decision of 2026-10-20 11:23 (AUTO SET fails fast, never loops). Commits: 9079725
(`bode_auto_span`, the only change in bode_sweep.c; bode_sweep.h gains the BODE_AS_* failures and two `bode_autospan`
fields), 071c206 (bode_ui + bode_view), then docs. PC_FW_VERSION not bumped. Host tests all green (test_ui incl.
test_bode_ch1_low / test_bode_auto_verdict and the Single sweep states, test_bode_sweep 412 incl.
test_auto_span_absurd); GCC 10 build clean in the changed files (bode_view.c also with -Wall -Wextra). **Nothing benched,
nothing flashed.**

What landed:
- **(a) Single sweep stays in the fold-out** (maintainer 10-19 21:32). `fold_tap` returns 0 for the Single sweep row; the
  fold-out loop calls `fold_service` while no finger is down: `bode_view_service` (the engine steps, up to 20 ms), the
  plot redrawn around the panel at most every REDRAW_MS (`draw_all_rect` for the part left of the panel, then the
  strips above, below and right of it copied from displaybuffertmp), and the Single sweep row redrawn when its state
  or text changes. `bu_single_button` (bode_ui.c) gives the state: START green "Single sweep" / "about N s", LAST green
  "STOP after this sweep" (RUN repeats; a tap sets `bv.single`), STOP red "Stop sweep" / "n / N" (single or thru sweep;
  a tap sets RUN off, the service then stops after the capture in progress), GREY "during AUTO SET" (a tap does nothing).
  `bu_single_running` ("running, then STOP") is gone. Every fold-out (Display, Thru, Trace style too) now lets the
  sweep run; up to 0.2.83 the engine paused while one was open.
- **(b) Generator after a sweep**: decision "restore the user's generator on the pin" (matches what the chip and its
  menu show while stopped; written in bode-ui.md section 7). `bv.gen_taken` is set when `bv_start_cfg` starts a sweep;
  at the end of `bode_view_service`, with no sweep running, `bv_gen_restore` calls `fpga_on_off_generator()` with the
  untouched `fpgasettings`. RUN repeats and AUTO SET passes restart inside the same service call, so the pin does not
  change between them. Diagnostics' early return in the service skips the restore (the view gives it back on leaving).
- **(c) CH1 low by cause**: `bu_warn_ch1_low` over the shown points (`gather` now keeps r1 / r2 per point): a point
  flagged ch1_low on a range coarser than 0.5 V/div gives "CH1 level low at <5|2.5|1> V/div: sweep again" (the coarsest),
  else one at or below 2 MHz the DUT-loads text, else "CH1 level low above 2 MHz: the x1 probe rolls off".
- **(d) AUTO SET**: `bode_auto_span` drops an absurd point (more than 20 dB from both valid neighbours; an end point from
  both of the next two valid points), returns BODE_AS_INCONSISTENT for two of them or one among the three lowest valid
  points, takes the reference as the median of the three lowest valid points (written over the first point in a local
  copy before `bode_markers_find`; the low-end slope rule compares the median with the third point, the phase still the
  first two), BODE_AS_UNCONFIRMED when a resonance's peak point lacks a valid index neighbour on either side,
  BODE_AS_CH1_LOW when more than half the good points are ch1_low. `bv_auto_finish` asks `bu_auto_verdict` (bode_ui.c)
  after every pass: a bounce (`bu_auto_bounce`: CH2's range changes direction twice between the pass's points, or once
  with the engine's six switches used), the failures above, a finer pass that is FLAT / NO_RESPONSE or whose peak
  (`ref_mdb`) is more than 6 dB below the previous pass's, or the band still coarse after pass 4. A failure sets
  `bv.auto_fail` (the warning, shown until the next sweep; `no_response` folded into it), keeps the settings and the
  pass's points, STOP. FLAT is still a result ("No corner ...", RUN).

Notes for a successor:
- Inference: the 41-sweep "loop" the maintainer saw on 0.2.83 was AUTO SET ending in RUN on a span set from a bad overview,
  then RUN repeating (each sweep re-ranging CH1 / CH2). The engine itself caps CH2 switches at six per sweep. A
  *successful* AUTO SET still ends in RUN by design (bode-ui.md 3.2 step 4). Superseded by fw 0.2.86 (addendum below):
  one sweep at the found settings, then STOP.
- A real tank whose overview point lands exactly on f0 (Q above about 30) reads as absurd: the point is dropped, its
  neighbour becomes the peak without a valid index neighbour on that side and AUTO SET says "peak not confirmed: sweep
  by hand". Accepted per the maintainer's rule; the guide text (docs/guide/screens.md) names it.
- The first-record artefact itself (0x153A) is fpga-gen10's; with it fixed, the absurd-first-point case should not
  arise. `bode_auto_span` is pure; `bu_auto_verdict` / `bu_auto_bounce` / `bu_single_button` / `bu_warn_ch1_low` are in
  bode_ui.c, all host-tested; `fold_service` and the generator restore are not host-tested (drawing / FPGA writes).
- mcp: the simulator's warnings are not affected (READ_BODE unchanged); no mcp text names the changed warnings.

**Bench walk-through (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, no SD writes; 0x153A;
RC 1 kOhm / 15 nF on the post, wiring A, x1 probes)** - `#VER fw=0.2.83,...` (number not bumped).
1. Single sweep from the fold-out: CTRL > Sweep > Single sweep. The panel stays open; the button turns red "Stop sweep"
   with "1 / 50", "2 / 50", ... counting; the plot left of the panel fills; the generator chip steps through the tones.
   Change nothing; at the end the button reads green "Single sweep" / "about N s", the cards show values, chip STOP.
   Close the panel and go to page one: RUN (green). Again Single sweep, then tap "Stop sweep" mid-way: it stops within
   one capture (well under a second above 1 kHz), the measured points stay, the button green again. Change Points to
   20 and tap Single sweep without leaving the panel: a 20-point sweep runs. With RUN repeating (page one RUN, then CTRL >
   Sweep): the button reads green "Single sweep" / "STOP after this sweep"; a tap turns it red "Stop sweep" with the
   progress and the repeats end after this sweep. During AUTO SET the button is grey "during AUTO SET" and does nothing.
2. Generator chip after a sweep: set the generator to 1 kHz 50 % on before entering the mode (or from its menu while
   stopped); run a Single sweep; at the end the chip shows 1 kHz and the post carries 1 kHz (a counter or a second
   scope on the post, if one is at hand). Generator off beforehand: off after the sweep.
3. Warnings: on the RC with a clean sweep no CH1 warning. If the pre-scan artefact leaves CH1 at 2.5 V/div (the CH1
   chip shows it; hardware-status 10-20 11:16), the warning reads "CH1 level low at 2.5 V/div: sweep again" (not the
   DUT text). Stop 10 MHz with the x1 probe: points above 2 MHz flagged ch1_low show "CH1 level low above 2 MHz: the x1
   probe rolls off".
4. AUTO SET on the RC: ends on about 1 kHz .. 200 kHz after one sweep at that span, in STOP (fw 0.2.86, addendum). If a bad first record is provoked (AUTO SET right
   after entering the mode, or after changing CH1's range in the Scope view), AUTO SET ends in STOP with "Auto set: no
   consistent response. Check wiring" (or lands on the right span if only a moderate error), never RUN on a wrong span
   and never repeated sweeps. If not provoked, the host test `test_auto_span_absurd` covers it.
5. AUTO SET failures: CH2 tip on the ground clip: "Auto set: no response: CH2 tip on the DUT output?" and STOP; CH1 tip
   off the post (on the clip): "Auto set: CH1 level low: CH1 tip on the DUT input?" (or no response), STOP, settings
   unchanged in CTRL > Sweep. Divider: "No corner ...", one sweep at the kept span, STOP (fw 0.2.86).
Bug indicators: the plot drawn over the open panel; the panel's row not following the sweep; the button red after the
sweep ended; a sweep that pauses while a panel is open; the generator chip and the pin disagreeing after a sweep;
AUTO SET ending in RUN after a failure warning; any AUTO SET running more than four passes.

**Addendum, fw-fr4 (fw 0.2.86 provisional, protocol 1.24 unchanged): AUTO SET ends in STOP, never RUN** (maintainer
2026-10-20 11:47, bode-ui.md 3.2 step 4 and section 10). Branch `fw-fr4` from main ec619c4; commit a119035 (bode_view.c,
bode_ui.c/h, test_ui.c), then docs. PC_FW_VERSION not bumped. Host tests green, GCC 10 build clean. **Not benched.**
- `bv_auto_finish`'s success path (span, FLAT) ends with `bv_start(1, 0)`, a single sweep at the found settings with the
  thru's Apply back, and sets `bv.as_final` while it runs; `bv.autoset` is 0 for it, so the cards, the thru and the
  Single sweep row treat it as an ordinary single sweep (red "Stop sweep" / "n / N"). `bode_view_auto_running` (the
  "Auto..." label, `pc_glue_autoset_running`) is `bu_auto_busy(autoset, as_final, running)`. The service's sweep end
  goes through `bu_after_sweep(run, single, done)`: single, so STOP; it clears `as_final`.
- Cancelling the final sweep (second AUTO SET tap, RUN/STOP, "Stop sweep") sets RUN off; the service stops after the
  capture in progress and the table keeps what it has, no snapshot restore (that stays for the overview and passes).
  Leaving the mode or a host BODE_CTRL (`bv_auto_drop`) clears `as_final`.
- State sequence: tap AUTO SET -> RUN/STOP button red "STOP", chip RUN, "Auto...", overview (Sweep card "auto n/20")
  -> passes 2..4 if a sharp band -> verdict: fail = STOP with the warning; else span / Settle / CH2 Auto set, one
  sweep (cards "sweeping", "Auto..." still) -> STOP: button green "RUN", chip STOP, "AUTO SET", chart and cards
  standing. READ_BODE view bit 1 (`bu_view_bits`) is 0 throughout, since every AUTO SET sweep is single.
- "Stop sweep" progress (maintainer bench 11:54 / 11:57 on 0.2.85: "50 / 50" static, intermittently): `bode_done_count`
  counts the table's done flags, which `bode_start` keeps when the plan is the same (RUN overwrites point by point),
  so a single sweep over a complete same-span table read N / N throughout; a new span cleared them (hence
  intermittent). Commit 0fc2e88: `bu_sweep_measured(running, bode_current, done_count, count)` (bode_ui.c) is the
  point in progress while running, else the done count; `sweep_measured()` feeds `single_state` (the fold-out row,
  which `fold_service` already redraws whenever its text changes), the Sweep card and `table_state`'s single-sweep
  "sweeping". test_ui drives 0, 1, N-1 over a complete table.
- Bench: the RC AUTO SET walk-through above (step 4: one sweep then STOP, no further sweeps, relays quiet); a second tap
  during the final sweep (button "Auto..." until the capture in progress ends, then STOP with the partial table); RUN
  after AUTO SET repeats at the found span; two Single sweeps in a row on the same span from the fold-out: the second
  counts "0 / 50" upwards, as does the Sweep card; READ_BODE view bits = 1 (bit 1 clear) after AUTO SET.

## 56. fw-eng: the range plan, the device's flags aligned with the host's (fw 0.2.87, protocol 1.24 unchanged)

Branch `fw-eng` from main cbff758 (fw 0.2.86). Brief: task-queue items "RUN stays continuous; repeated sweeps reuse the
previous range plan" (maintainer 10-20 11:51 / 11:52, bode-ui.md section 10) and "Device sweep flags stricter than the
host's" (bench 11:14 / 12:00). The bench session's data of 12:05 (fw 0.2.86, RC 1 kOhm / 15 nF, 100 Hz .. 1 MHz / 25)
had two device sweeps. Sweep 0 was good except for unsettled / incoherent / retried points at the CH2 switches, with 6
switches. Sweep 1 had a fooled pre-scan that left CH1 on 2.5 V/div. The host sweep flagged 1 point. Commits: 1094c77
(bode_dsp flags + test_flags), 8bf9a9f (engine retake and settle sequence + test_flat_retake), 53447c4 (range plan +
test_range_plan), then docs. PC_FW_VERSION not bumped, no protocol change (READ_BODE's switch count already existed).
Files: bode_dsp.c/.h, bode_sweep.c/.h, host_tests (bode_cli.c, test_bode.py, test_bode_sweep.c). Not touched: pc_bode.c
(the plan lives in the engine), bode_view.c (nothing new to show), fpga_bus.c / pc_hal_scope.c / the FPGA tree
(fpga-gen10). Host tests all green (test_bode_sweep 1046 checks, test_bode.py incl. test_flags). The GCC 10 build has no
warnings in the changed files. **Nothing benched, nothing flashed.**

**The range plan** (the bode_sweep.h comment above `bode_rplan`).
- **What is kept.** For each point the engine keeps the CH1 / CH2 range, both offsets and the splice factor that the
  point's result was measured on. `result()` writes the entry. A floor switch in a plan sweep moves the entry to the new
  range and factor.
- **When a sweep reuses it.** `bode_start` compares the sweep's settings with the plan's (`plan_matches`: start, stop,
  points, Settle, CH2 mode, Hold's CH2 range, and every entry present). When they are equal there is no pre-scan, and
  `point_begin` applies the point's entry (`plan_apply`: a range write costs two RELAY_MS waits, an offset write one).
- **When CH2 switches during reuse.** `auto_range` steps CH2 down only below PLAN_FLOOR_C100: 30 % of full scale
  predicted on the next range, half the fresh sweep's 60 %. That gives 6 dB of hysteresis, so an unchanged circuit never
  switches. A clip still steps up at once.
- **When it starts over or is dropped.** Different settings start a new plan (entries cleared), and a thru sweep drops
  it. The plan is also dropped (`rp_set = 0`, so the next sweep ranges from scratch) when a result has CH1 clipped or
  dead, or when the first point's CH1 would fit the next finer range under 60 % of full scale. That last case means the
  pre-scan was fooled (bench 12:05 sweep 1).
- **Cancelled sweeps.** A cancelled sweep leaves the entries it wrote. If it ranged from scratch, the plan is incomplete
  and the next sweep ranges again. If it started from a complete plan, the plan stays complete.
- **Results.** `bode_plan_reused()` tells whether the latest sweep started from the plan. Splice factors are kept per
  point, so H on a plan sweep equals the first sweep's (test: within 0.015 dB).
- **Relays.** The relays still click where consecutive points' planned ranges differ (on the RC: 0.5 -> 0.2 -> 0.1 V/div,
  and back at the next sweep's start). What stops is the ranging: the pre-scan, the verify and the extra captures of
  each switch. On the RC model the first sweep takes 40 captures and 2 switches, later sweeps 36 and 0.

**The flags against the host** (item 2).
- **Thresholds.** The thresholds in bode_dsp.c were already the host's. They are now named after bode.py's constants:
  CH1_LOW_CODES, CH1_MIN_CODES, FLAT_STD_CODES, FLAT_CODES, CH2_SMALL_CODES, LOW_SNR_DB, LEAK_LIMIT_DB, SETTLE_DB /
  SETTLE_DEG; NEAR_FLOOR_DB and COLLAPSE_RATIO are in bode_sweep.c.
- **The comparison test.** test_bode.py `test_flags` runs `bode_meas_flags` and the host's `_result` flags on the same
  records. The records come from the simulator's `dut_response` of the bench RC, in 8-bit and 16-bit form, with:
  - CH2 on the host's range and one range coarser
  - a deep stopband
  - an off-bin tone beside CH1
  - a ringing CH2
  - a DC step in CH1
  - clipped and flat records
  - a toneless CH1

  The flags are identical everywhere. The test skips only flags whose host number sits within the estimators' spread of
  the threshold (low_snr / unsettled use the device's period-difference noise).
- **What differed.** The host's newer rules (mcp-gen7, written for the first-record artefact) are now in the C:
  - `bode_flat1`: CH1 standard deviation under 1 code
  - `bode_ch1_dead`: flat1, or CH1 under 0.5 code; such a point reads incoherent
  - `bode_suspect`: the record is retaken once
  - `bode_flat2` is never true while CH1 is flat
  - the settle check skips halves whose CH1 tone is under 0.5 code
  - in the engine: the retake (point, pre-scan, verify, and a constant CH2 background record); a dead CH1 keeps range 1
    in the pre-scan, ends the verify, and skips re-centring and CH2 ranging
- **Conclusion: the 12:05 excess is not the DSP.** The same records give the same flags. The device's extra flags sat on
  the first records after a CH2 range change (31.6, 46.4 and 100 kHz) and on retried points.
- **The one real sequence difference.** After `set_range` the host sleeps 50 ms after the offset write and 50 ms at the
  end (100 ms, plus USB time). The device's waits overlapped into 50 ms; `relay_wait` now adds them up. Inference: the
  offset PWM's filter was still settling in the record. The "CH1 step" case of test_flags shows that such a step reads
  incoherent + unsettled.
- **Remaining candidate.** The 0x153A first-record artefact (fpga-gen10). The retake covers its flat form.

Not done / for the next agent:
- The view shows nothing about the plan (the brief allowed it only if needed). `bode_plan_reused` is there for a card or
  a READ_BODE bit if the maintainer wants one.
- `bode_estimate_ms` still counts the pre-scan on a plan sweep, so the estimate is a few hundred ms high.
- The device's noise estimate (period differences) still differs from the host's FFT median on a transient (documented
  in section 42). It only moves low_snr / unsettled near their thresholds.

**Bench walk-through (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, no SD writes; 0x153A;
RC 1 kOhm / 15 nF on the post, wiring A, x1 probes; `#VER fw=0.2.86,...`, number not bumped)**
1. Frequency response, CTRL > Sweep 100 Hz .. 1 MHz, 25 points, Settle Normal, CH2 Auto; RUN.
   - Sweep 1: READ_BODE header switches 2 (6 on 0.2.86 at 12:05), CH1 at range 3.
   - Sweeps 2, 3, ...: switches 0, the same r1 / r2 per row as sweep 1, |H| within 0.05 dB of sweep 1.
   - The relays click only where the planned range changes (two or three clicks per sweep, none of the
     switch-and-remeasure pairs). The sweep is a little shorter (READ_BODE elapsed ms). STOP.
2. Single sweep twice from the fold-out at the same settings: switches 0 both times. Change Points to 24: the next sweep
   switches again (2), the one after reports 0.
3. AUTO SET, then RUN. AUTO SET's final sweep ranges (switches as a first sweep, because the settings are new). RUN's
   first sweep reports 0 switches.
4. Flags against the host. Run the same span on the host (`fnirsi1013d-bode --port <port> --yes --start 100Hz --stop 1MHz
   --points 25`, view closed first) and on the device (sweep 2 of step 1).
   - The device flags no more points than the host (12:05: host 1 flagged; device 0.2.86 unsettled / incoherent at
     46.4 and 100 kHz).
   - Expect no unsettled / incoherent points on the device. Spliced / retried are not "flagged" (grey) flags.
5. Plan with a changing circuit, during RUN.
   - Short the 15 nF briefly or swap it (e.g. 1.5 nF): a point that clips steps CH2 up (switches > 0 on that sweep), and
     later sweeps settle back to 0.
   - Probe off the post (CH1 clipped or dead): the next sweep ranges from scratch (switches as a first sweep).
6. If the first sweep after entering the mode shows CH1 on 2.5 V/div (a fooled pre-scan, ch1_low rows), the next sweep
   must range again (not 0 switches, CH1 back on 0.5 V/div), and the one after that reuses the plan.

Bug indicators:
- switches > 0 on every RUN sweep of an unchanged RC
- r1 / r2 differing between plan sweeps
- |H| steps at the planned range changes (the splice factor not reused)
- a plan sweep right after a Points / Settle / CH2 mode change (switches 0 right after a change)
- CH1 left on 2.5 V/div for more than one sweep
- the header's switch count above 6
## 57. fw-decfix: decimation from the divider the FPGA holds, 0x54 at every arm (fw 0.2.88, protocol 1.24 unchanged)

Branch `fw-decfix` from main 369537f (fw 0.2.86, fpga-gen10's handoff merged). Brief: fpga/al3/AGENTS.md "First-record
saturation, fpga-gen10" (cause, prescription, reproduction). Commits: 1299f7a (fpga_bus: held values, `fpga_decim_write`,
trace / counters, test_fpga_bus), 61af5dd (pc_cmds: `pc_hal_divider_now` everywhere the arm and GET_DECIM need the
divider, test_session), 2ba6566 (pc_bode.c `bh_arm`, one line), then docs. PC_FW_VERSION not bumped. Host tests green,
GCC 10 / GCC 13 / PC_DEBUG builds clean. **Not benched.**

**What the arm computes from now.** The divider is the one the FPGA holds: `fpga_bus_note_write` (every completed bus
write, `fpga_trace` on the scope, `fm_apply` in the host model) records the last 0x0D, 0x53, 0x54 and 0x56 values in
`fpga_bus_wr` (`held` bits `FPGA_HELD_*`). `pc_hal_divider_now()` (pc_hal_scope.c) returns the held 0x0D, or the UI
table's divider before the first 0x0D. It replaces `pc_dec_div()` / `pc_dec_divider` (UI conversions, the sequencer, the
EXT shift) and, in pc_cmds.c, every `s->tb_raw ? s->tb_divider : tb.divider`: `pc_decim_next` (the next arm's K, N, S,
gain), `pc_arm_geometry`'s EXT shift, GET_DECIM bytes 16..23 and 31, the capture's divider (GET_CAPTURE_INFO 16) and
READ_TRACE's divider for a UI record. `tb_raw` / `tb_divider` now only decide what GET_TIMEBASE reports and feed the
settings fingerprint. A UI time/div snap that writes no 0x0D (`acq_view_fit`, `acq_view_set`) therefore changes nothing
the arm computes. `fpga_divider` (fpga_control.c, the zero correction's clock class, acal and enc_heal) is still a second
record of the same value, kept by its writers; it differs from the held value only inside `pc_hal_enc_retry`'s 1 / 0
pair (0x1532..0x1536 at divider 0, which ends where it started).

**What the arm writes.** `pc_dec_apply` -> `fpga_decim_write(mode, has_fir, fir_en, shift)` (fpga_bus.c): 0x53 when it
differs from the held mode, 0x56 (0x1535+) when it differs from the held FIR bit, and **0x54 at every call with K >= 1**,
whatever the firmware thinks the FPGA has (one byte, 0.6 us). It returns "settle" when any written value differs from
the held one or the held divider changed since the previous call; `pc_dec_apply` then waits `pc_decim_settle_us` as
before, and writes the 16-bit level 0x55 at every arm with K >= 1 (unchanged). The order is unchanged: 0x0D (by its
writer), 0x53, 0x56, 0x54, 0x55, the settle wait, then the arm. `pc_dec_dirty`, `pc_dec_divider`,
`pc_glue_divider_written` (and its four call sites) and `fpga_set_decim_mode` / `_shift` / `_fir` are gone.
`pc_dec_cur` remains the record of the last configuration (GET_DECIM 24 / 25, the readout's scale, word counts).

**Trace and counters.** 0x54 and 0x55 return 1 from `fpga_bus_note_write`, so they are event 22 at trace level 1
(`s.event_log(fpga_trace=True)`; b = caller << 12 | value, 0x55 saturates at 0xFFF) and counted in GET_LINK_STATS 65
with 0x53 / 0x56. They do not set `since_reset` (no effect on the encode clocks; they come before 0x01 = 1 anyway).

**The Bode pre-scan.** `bh_arm` computes its configuration from the divider it means to run at (`bode_divider`) and
wrote 0x0D only when its own record `bh_divider` differed. Two ways that could leave the shift inconsistent with the
FPGA: (a) a 0x0D from anyone else between two engine steps while `bh_divider` still matched (the engine then configured
the decimator for its divider while the FPGA ran another): fixed in `bh_arm` with one line, it also rewrites 0x0D when
`pc_hal_divider_now()` differs; (b) a 0x54 that `pc_dec_apply` skipped because `pc_dec_cur.shift` matched while the FPGA
had another (lost or foreign write): covered by the shared code, 0x54 is written at every arm. From the code neither
path is shown to happen in the pre-scan (the device Bode view runs the engine in its own loop; nothing else writes 0x0D
or 0x54 there), so if the pre-scan still ranges CH1 coarse, the trace decides: 0x0D = 4095, 0x53 = 3, 0x56 = 1,
0x54 = the shift for N 4096 with CIC3 + FIR, then 0x01 = 1, followed by a record of 0xFFFF clears the decimator
configuration and puts the cause elsewhere (the input really over range at the pre-scan's first range, or the range
relay / offset still settling after the pre-scan's range change); hand that to the Bode maintainer (fw-eng) with the log.

**Host tests.** test_fpga_bus `test_decim_write` (the model now holds 0x53 / 0x54 / 0x56 and calls
`fpga_bus_note_write`): D 4095 CIC2 + FIR gives N 4096, S 15 (the UI table's 199 / 399 give 7 / 9); an unchanged arm
writes only 0x54; a lost 0x54 (model register poked, firmware record unchanged) is rewritten; a foreign 0x54 through the
bus is rewritten and settles; a divider change settles once; Filter off writes no 0x54; a Bode-engine-like sequence
(rate, mode, arm per step, D 4095 / 1023 / 255 / 15 / 0 / 65535, a UI rate write between steps) leaves the model's 0x54
equal to the shift for the model's divider, written after the 0x0D and before 0x01 = 1. test_write_record: 0x54 / 0x55
counted, since_reset kept, held values. test_session `test_decim_held_divider`: raw 4095, the snap (UI time/div and
table divider change, no write), a poll, GET_DECIM 16 = 4095 / N 4096 / S 15, ARM CIC2 + FIR writes S 15,
GET_CAPTURE_INFO divider 4095; with the old expression it fails with S 7 / N 200 (checked by swapping the mock's
`pc_hal_divider_now` for the UI divider).

**Bench (fw 0.2.87 image from this branch, 0x153A; host `from fnirsi1013d.scope import Scope; s = Scope.connect("<port>")`).**
1. fpga-gen10's deterministic reproduction (fpga/al3/AGENTS.md, steps 1..4): UI at a 1 MSa/s index, memory depth 12k or
   deeper, the longest time/div that depth allows, `s.set_decim("off")`; `s.set_timebase(divider=4095)`;
   `s.set_decim("cic2+fir")` (the fit snaps the time/div), `s.get_status()`, then `s.get_decim()`: **divider 4095,
   next_n 4096, next_shift 15** (0.2.86: 199 / 200 / 7). `s.capture(["CH1", "CH2"])` ten times: no record of 0xFFFF
   (0.2.86: every one), GET_DECIM bytes 25 / 27 = 15. If the fit does not snap on this unit, force it from the scope
   (memory depth or Filter box with the raw divider written), as fpga-gen10 describes.
2. GET_DECIM after a UI time/div snap with a raw divider in place shows the FPGA's divider (the raw one) until a
   time-base tap writes 0x0D; then it shows the new one.
3. `s.event_log(fpga_trace=True)`, one host capture with the filter on and one UI conversion: the log shows 0x54 (and
   0x55) at every arm with the filter on, after the 0x0D / 0x53 / 0x56 writes and before 0x01 = 1; GET_LINK_STATS 65
   grows by 2 per decimated arm (0x54, 0x55) besides any 0x53 / 0x56.
4. The Bode pre-scan: from a cold Scope view, a device sweep (or `BODE_RUN`) twenty times in a row; every pre-scan
   should range CH1 to 0.5 V/div on the bench RC (0.2.86: coarse in about every other sweep). With the trace on for at
   least one sweep, check the sequence above before the first pre-scan capture. A coarse pre-scan with a correct 0x54 in
   front of it is not this fault (see "The Bode pre-scan").
Bug indicators: any whole record of 0xFFFF with the filter on; GET_DECIM's divider differing from the last 0x0D in the
trace; an arm with K >= 1 and no 0x54 in the trace before its 0x01 = 1.

## 58. fw-eng2: the engine's trigger, the plan settling in one sweep (fw 0.2.89, protocol 1.24 unchanged)

Branch `fw-eng2` from main 3cc0b10 (fw 0.2.88). Brief from the bench session after the 0.2.87 / 0.2.88 bench
(hardware-status 12:30). Two problems on the RC, 100 Hz .. 1 MHz / 25, CH2 Auto:
- The plan converged over many sweeps: switches 6, 6, 6, 6, 6, 6, 4, 5, 1, 1, with jumbled CH2 ranges such as 146.6 kHz
  on r2 2.
- 2..4 points per sweep at the knee and above read unsettled / incoherent + retried, with |H| and phase varying sweep to
  sweep.

Commits: 9ca907e (`firmware/tools/bode_cells.py`), 1d4c268 (pc_bode.c trigger, bode_view.c restore), 4e3f8ff (engine
+ tests), then docs. PC_FW_VERSION not bumped. Host tests green (test_bode_sweep 1310), GCC 10 build without warnings
in the changed files. **Not benched.**

**Cause, measured.** `bode_cells.py` (run by the bench session 12:32, `backups/bode/cells_1235.npz`, log in the
bench session's scratchpad) captured the cells 10.0, 14.7, 21.6 and 31.6 kHz on the RC in three variants, eight
captures each.
- With the CH1 trigger at 128 ("host", and "imm" = no settle at all): 64 of 64 clean.
- With the level off the signal, the record starting on the FPGA auto time-out ("auto"): 15 of 32 broken (4 at 14.7 kHz, 6 at 21.6, 5 at 31.6; none at 10.0). Leak -3 ..
  -36 dB, |H| off by up to 0.4 dB, phase by 4 deg.
- In each broken record the period differences show a phase jump at a random sample (e.g. 806, 1774). The record is
  joined from two captures; it is not a filter warm-up. The first samples are ordinary codes.
- Settle time and plan N / rate are not involved: "imm" (no settle) was clean, and the plans are the host's (test_bode.py
  vectors).
- The C measurement equals the host's on these real records (leak to 0.1 dB, the same flags): checked with
  bode_cli on all 96 captures.
- The engine arms FPGA auto (`pc_hal_arm(..., normal 0)`) with the UI's trigger (source, edge, the marker's level).
  That level is often off the bench signal, so its records started on the time-out. Inference: the time-out
  record's start address on 0x153A does not match the samples written (a readout / FPGA matter for fpga-gen*, not
  chased here; the host's set-up never hits it).

**What changed.**
- **The trigger.** pc_bode.c `bh_trigger` before each arm: CH1, rising, level 128 (the engine centres CH1's mean
  there). The level is derived from the marker and CH1's range / offset at every arm (fpga_set_trigger_level), so it
  is computed first and the marker placed again when the level is outside 124..132.
  - bode_view open saves the trigger edge and marker (`s_edge`, `s_tvp`) besides the source.
  - Close writes source and edge back with the marker after the channels, so the UI's level comes back.
  - `pc_hal_trigger_write` redraws the trigger chip when the marker moves (only after a CH1 range / offset change);
    watch for it drawing over the plot.
- **Retake.** An incoherent measurement (`bode_incoherent`, CH1 leak above -40 dB) is taken once more, like a suspect
  one, in every phase. On the device it is the signature of a joined record; a CH1 that really has energy beside the
  fundamental still reads incoherent after the retake.
- **Plan, clip.** From the plan, a CH2 clip on the planned range is measured again on the same range (PH_CLIP_AGAIN,
  RETRIED) before CH2 steps up. The jumbled ranges were planned points stepping UP on a clip in a bad record (146.6 kHz
  to r2 2, ch2_small).
- **Plan, floor.** From the plan, a point under the floor steps down repeatedly in the same sweep (PH_DOWN again while
  still under 30 %), within the six switches. Up to 0.2.88 it went one range per sweep, so a point that had stepped up
  twice needed two more sweeps (the 6, 6, ..., 1 sequence).
- **What the plan stores.** The plan already stored the range of the accepted measurement: `result()` writes the
  entry and a step up rewrites it with the repeated measurement. No change was needed there.
- **Rejected.** "A plan entry coarser than both neighbours" was not added as a rule: a resonance's peak is
  legitimately coarser than both neighbours.
- **Tried and taken out.** A record lead-in skip (the warm-up hypothesis, before the bench data), and a phase-jump
  detector on the period differences (it missed records whose jump covers most of the period-difference window,
  where the median itself is inflated: 5 of 15 on the bench records). The leak test catches all 15.

Not done:
- Why the time-out record is joined (fpga / readout): queue for the FPGA side with cells_1235 as evidence.
- A per-point "why it switched" in READ_BODE: the 20-byte row has no spare bit. The flags show RETRIED, and a
  CH2 step up leaves the point on a coarser r2.

**Bench walk-through (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, 0x153A, RC on the
post, wiring A, x1 probes)**
1. Scope view first: set the trigger level marker well off the signal (e.g. top of the screen) and note source / edge.
   Open Frequency response, RUN, ten sweeps (100 Hz .. 1 MHz, 25, Normal, Auto).
   - Expect switches 2 (or the first sweep's count), then 0 on sweeps 2..10.
   - CH2 ranges monotonic (3 .. 3, 4, 4, 5 .. 5 as the host's 0.5 / 0.2 / 0.1 V/div).
   - No unsettled / incoherent points; |H| at 10.0 / 21.6 kHz within 0.05 dB of the host (-2.854 / -6.738) every sweep.
   - "retried" rare (0..1 per sweep).
2. Leave the mode: the trigger marker, edge and source are as set in step 1.
3. Host comparison as section 56 step 4: the device flags no more points than the host.
4. Changing circuit as section 56 step 5: a 15 nF -> 1.5 nF swap during RUN re-plans within one sweep (switches > 0
   once, then 0).
Bug indicators: the trigger chip redrawn over the plot at every point; the trigger marker not restored; switches still
decaying over several sweeps; incoherent points with "retried" at fixed cells (then the CH1 trigger is not reaching
the FPGA: check 0x15 / 0x16 / 0x17 in a bus trace before the 0x01).

## 59. fw-eng3: the pre-scan's check ranges like a point (fw 0.2.90, protocol 1.24 unchanged)

Branch `fw-eng3` from main a2b6929 (fw 0.2.89). Brief: the bench session's 13:10 data
(`device_rc_rows_1310.json` in the bench session's scratchpad), from the maintainer's AUTO SET + RUN on the RC
(500 Hz .. 100 kHz) and host sweeps A..D:
- CH1 left on 2.5 V/div with ch1_low on most points (C, the maintainer's table).
- CH2 on 2.5 V/div for a whole sweep (D).
- Jumbled CH2 ranges per point in the maintainer's table.

Commits: 802cc4a (engine + tests), then docs. Host tests green (test_bode_sweep 1619), GCC 10 clean. **Not benched.**

**Findings.**
- **(a) The plan's index space.** The plan stores `e->range[0]` / `e->range[1]` at `result()` and `plan_apply` writes
  the same indices back through `set_channel`, so there is no swap. The new divider test (CH1 and CH2 on different
  ranges) checks the second sweep's rows against the first's, point by point.
- **D was not a plan sweep.** C's plan was dropped by the 0.2.87 first-point rule: CH1 on 2.5 V/div with about 18 codes
  of peak. D then ranged from scratch, and its own pre-scan check went wrong for CH2.
- **The real fault is the check after the pre-scan.** Up to 0.2.89 it only ever stepped a channel UP, on a clip or (CH2)
  a saturated / collapsed record, and marked that range bad for the sweep (CH2). It never stepped a channel finer. One
  bad record in the check (or a pre-scan record whose peak reads high) therefore left the channel coarse for the whole
  sweep: CH1 on r1 1 in C, CH2 on r2 1 with bad_down in D. Inference: the bad records are still the time-out / spliced
  kind or a front end not settled; the leak retake catches CH1-incoherent ones only.
- **The maintainer's table** mixes points of several sweeps (RUN overwrites point by point, the header's switch count is
  the sweep in progress). The jumbled r2 with r1 1 is the coarse-CH1 sweeps' points next to newer ones. Inference: not
  a plan that carried those ranges.

**What changed (bode_sweep.c on_verify).**
- **Clip or saturation.** A clip (CH2: or a saturated / collapsed record) is measured again on the same range (`vclip`)
  before the channel steps up. The range that clipped twice is not tried again in this check (`vbad[c]`); a saturated
  CH2 range also goes into `bad_down` as before.
- **Unclipped channels.** An unclipped channel steps to the finest range its record's peak fits (85 % of full scale,
  `finer_range`), so CH1 lands on 0.5 V/div for the post's signal whatever the pre-scan read.
- **Limit.** Up to VERIFY_MAX = 6 captures (4 before).
- **Plan drop.** A result below 2 MHz with CH1 under 20 codes and a finer CH1 range left drops the plan (with the 0.2.87
  rules).
- **(c) AUTO SET.** The overview (100 Hz .. 10 MHz, 20, Fast) has other settings than the final sweep, so
  `plan_matches` never lends its plan. Test: overview, final (fresh), RUN (reused, 0 switches, the final sweep's ranges,
  CH1 on 0.5 V/div).

**Bench (maintainer, bench session on <port>; FEL load, 0x153A, RC on the post, wiring A, x1)**
1. AUTO SET, then RUN for ten sweeps. Every sweep's rows: r1 3 on every point; r2 monotonic (2/3 .. 5).
   Switches: the first RUN sweep 0 (the final sweep's plan), then 0. No ch1_low below 2 MHz.
2. The host's A..D sequence again (20 points twice, then 25 points twice):
   - C and D both have r1 3.
   - D's r1 / r2 per point equal C's, with 0 switches.
3. Bug indicators:
   - any sweep with r1 1 or 2 on the RC;
   - r2 constant on a coarse range over the whole span;
   - a plan sweep (0 switches) whose rows differ from the previous sweep's r1 / r2.

## 60. fw-eng4: steps up confirmed, the trigger at CH1's mean, capture statistics (fw 0.2.91, protocol 1.24)

Branch `fw-eng4` from main 312c49f (fw 0.2.90). Brief: the bench session's 0.2.90 bench (hardware-status 13:14):
- the first sweep after the load ended with both channels on 5 V/div;
- a fresh sweep put CH2 on 5 V/div at 780 Hz..1.2 kHz;
- a 2 between 4s at points 17..19;
- 7..14 retakes per sweep although the engine arms on CH1.

Commits: 3b363b2 (engine, pc_bode, view, tests), b52ca2a (protocol row 60, mcp `BodeCtrlResp`), then docs.
PC_FW_VERSION not bumped. Protocol: no number change, and the BODE_CTRL answer grows from 40 to 48 bytes (a layout
addition; hosts and older firmware tolerate both). Host tests green (test_bode_sweep 1840), mcp pytest
test_bode_device / test_protocol and the CI ruff selection green, GCC 10 clean. **Not benched.**

**(a) No range change on one record.**
- Points: a CH2 clip or saturated record is measured again on the same range (PH_CLIP_AGAIN, also in fresh sweeps
  now), and a repeated up step (PH_UP_POINT) is confirmed the same way. `clip_again` resets at every step up.
- The pre-scan's check, clip on an off-centre record: the record is re-centred instead of stepped up (its peak fits 85 %
  once centred).
- The pre-scan's check, a clip twice: steps up. Only a clip with at least 32 codes of tone behind it (REAL_CLIP_MDB)
  bars the range for the way back, so rail samples on a small tone, even twice, cannot leave a channel at 5 V/div; the
  next clean record steps it back (`finer_range`).
- VERIFY_MAX 8, so the last range is judged.
- A failed step down (a clip on the finer range) and a saturated range go into the plan's `rp_bad_down`. Plan sweeps
  start with it as `bad_down` and do not retry the step every sweep: the test with the post at 0..3 V showed one switch
  per plan sweep before.

**(b) Diagnostics.**
- The engine counts per sweep (from `bode_start`) its generator-on captures, those that ended without a trigger
  (`bode_cap.triggered`, set by pc_bode from `pc_hal_acq_poll`'s trigger flag seen during the capture; FORCE counts as
  untriggered), the records taken again for CH1 leak, and those taken again for another reason (suspect or time-out).
- BODE_CTRL answer bytes 40..47 (u16 each; `bode_capture_stats`); mcp `BodeCtrlResp.captures / timeouts /
  leak_retakes / other_retakes`.
- If `timeouts` stays near `captures`, the engine's CH1 trigger does not reach the FPGA (look at 0x15 / 0x16 / 0x17 in
  a bus trace).
- GET_STATUS's fpga_triggered describes the host's capture state, not the engine's.

**(c) The trigger level.**
- `trig_code` (bode_sweep.c) is CH1's expected record mean: the code of 0 V on CH1's range and offset (128 - dcoffset x
  100 / dc_shift_value) plus CH1's last measured DC (`v1_uv`, kept across sweeps; 0 V before the first measurement
  since boot). `bode_hw.arm` takes it; `bh_trigger(level)` places the marker when the derived level is more than 3
  codes off.
- A generator-on record that ended on the time-out is taken again (the level by then comes from that record's own mean).
- The FPGA auto time-out is twice the record (it was one record), so a trigger has the record's periods to arrive.
- Inference: on the bench pre-scan (2.5 V/div, offset 0) the post's signal sat at codes 128..143. A level of 128 was
  its bottom edge, so most pre-scan records ended on the time-out, i.e. the joined kind.
- The first pre-scan after boot still uses 0 V (code 128 at offset 0); its retake corrects the level.

**Bench (maintainer, bench session on <port>; FEL load, 0x153A, RC on the post, wiring A, x1)**
1. Right after the load: one sweep 500 Hz .. 100 kHz / 20, then BODE_CTRL action 0.
   - Bytes 40..47: captures about 25..35, timeouts 0..1 (the first pre-scan), leak retakes 0..2.
   - Rows: r1 3 everywhere, r2 monotonic; no 5 V/div anywhere.
2. RUN ten sweeps at 100 Hz .. 1 MHz / 25. Read BODE_CTRL after each.
   - Switches 2 then 0.
   - Timeouts 0, leak retakes 0..1 per sweep, retried 0..2 per sweep.
   - No 2 between 4s.
3. If timeouts stay high: report the counts and a bus trace of one arm (the trigger registers); the engine's trigger
   is then not effective and fpga-gen11's time-out record fix decides.
Bug indicators:
- r1 / r2 0 (5 V/div) on the RC;
- the trigger chip redrawn at every point (the level moving: CH1 offset changes);
- timeouts equal to captures;
- the switch count not 0 on plan sweeps of an unchanged RC.

## 61. fw-eng5: AUTO SET chooses CH2 Hold or Auto, planned vs unplanned switches (fw 0.2.93, protocol 1.24)

Branch `fw-eng5` from main 555e63a (fw 0.2.91). Brief: task-queue item "AUTO SET chooses CH2 Hold when the span fits
one range" and the maintainer's decision of 2026-10-20 13:33 (bode-ui.md 3.2 step 3 and section 10). Commits: d51979d
(engine + test_bode_sweep), 0842b95 (bode_view, pc_hal.h, test_session), 63b40a6 (protocol row 60, mcp
`BodeCtrlResp` + test), then docs. PC_FW_VERSION not bumped. Protocol number unchanged; the BODE_CTRL answer grows
from 48 to 50 bytes (a layout addition like 0.2.91's). Host tests green (test_bode_sweep 2068), mcp ruff (CI
selection) and pytest test_bode_device / test_protocol green, GCC 10 build without warnings in the changed files.
**Nothing benched, nothing flashed.**

**The Hold rule** (`bode_auto_ch2`, bode_sweep.c; pure, host-tested).
- Input: the CH2 levels (v2, dBV peak at the tip, with the splice) of every point the overview and the passes
  measured, kept by the view in `as_f` / `as_v2` / `as_have` (up to 80, cleared at AUTO SET's start, appended in
  `bv_auto_finish` after each pass), and the span AUTO SET chose (`bode_step_mhz(a.start_i)` .. `(a.stop_i)`).
- Hold when the points inside the span spread less than 20 dB (BODE_HOLD_SPREAD_MDB). The held range is the most
  sensitive range whose full scale (128 codes peak at the tip: `bode_ch2_full_scale`, from `chan_info`'s uv_q8 and
  CH2's probe index) is at least 1.5 x the largest level (BODE_HOLD_MARGIN_MDB, 3.52 dB).
- Auto otherwise, and also with fewer than two points in the span, a point without a tone (BODE_MDB_FLOOR), or a
  largest level that no range holds with the margin.
- The brief said "the coarsest range whose full scale is at least 1.5 x"; the coarsest always qualifies, so the
  implementation takes the most sensitive (finest) one that does. Inference: that is what was meant.
- The margin is on the fundamental. For a square-wave-like CH2 (a passband) the record's peak is about 2 dB below
  the fundamental, so Hold can sit one range coarser than Auto would (the tank test: Auto 0.5 V/div, Hold 1 V/div).

**The view.** `bv.hold_r2` is CH2's held range (0..5, or HOLD_CHANNEL: CH2's range when the sweep starts, the old and
BODE_CTRL rule). AUTO SET's success path sets `bv.ch2` and `bv.hold_r2` from the rule; the final sweep and RUN use it
(`bv_start_cfg`), so RUN reuses the final sweep's plan (`plan_matches` compares the held range). A tap on the Sweep
fold-out's CH2 range row toggles: Hold takes the range CH2 has at the tap, Auto forgets it. The row reads "Auto" or the
held V/div (`volt_div_texts[CH2's magnification][range]`, e.g. "1V/div"). A failed or cancelled AUTO SET keeps the
user's mode and range. Settings restore, defaults and BODE_CTRL with settings set HOLD_CHANNEL (the settings word
keeps only the mode bit, so a restored Hold holds the channel's range as before 0.2.93).

**Planned and unplanned switches.**
- `plan_apply` counts a CH2 range change at point i > 0 of a plan sweep as planned (`e->planned`,
  `bode_planned_switches`). The first point's write is not counted: it only moves the channel from where the last
  sweep left it (on the RC, 0.1 -> 0.5 V/div at the start of each RUN sweep; a third relay change per sweep).
- `e->switches` (the READ_BODE header bytes 26..27, MAX_SWITCHES) is unchanged: the unplanned ones, i.e. every step
  of `auto_range` / `up_step` / the plan's floor steps. A fresh sweep has no plan, so its ranging reads unplanned:
  AUTO SET's final sweep on the RC reads 2 unplanned / 0 planned, the RUN sweeps after it 0 unplanned / 2 planned.
- CH1 is not counted (its planned range does not change between points in practice).
- BODE_CTRL bytes 48..49 = the planned count (PC_BODE_CTRL_RESP 50); mcp `BodeCtrlResp.planned_switches`.
- `bode_snap` keeps `planned` with `switches`.

**ch2_small.** The maintainer's wording was "a held sweep whose point drops more than 20 dB below the held range's full
scale flags it ch2_small as today". Today's flag is CH2 under 3 codes (about 33 dB below 128 codes); it was not
changed. With the 20 dB spread and the finest range chosen, the smallest point of a held AUTO SET span sits at about 3.4
codes or more, so the held sweep itself is clean. A point that later drops further (the circuit changed during RUN)
reads ch2_small and is drawn grey, and the user switches to Auto.

**Host tests** (test_bode_sweep `test_auto_ch2`, and `test_range_plan`):
- The rule on synthetic tables: 18 dB -> Hold on 0.2 V/div; 20 dB -> Auto; points outside the span ignored; either span
  order; a toneless point, one point, or a level beyond 5 V/div -> Auto; `bode_ch2_full_scale` = 128 codes per range.
- The RC (1 kOhm / 15 nF): the overview and AUTO SET's span give Auto; the final sweep ranges (2 unplanned); RUN
  reuses the plan with 0 unplanned and as many planned as the plan's transitions. 100 Hz .. 1 MHz / 25: the plan
  sweeps read exactly 2 planned, 0 unplanned (CH2 3 .. 3, 4, 4, 5 .. 5).
- A parallel tank (1 mH || 100 nF, 10 kOhm load, f0 15.9 kHz, Q about 17; `dut.l` is new in the model) on f0 / 1.1 ..
  f0 x 1.1: 10.4 dB of spread -> Hold on 1 V/div; the held final sweep and RUN: 0 planned, 0 unplanned, nothing
  flagged, no ch2_small, CH2 on 1 V/div at every point.

**Bench (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, no SD writes; 0x153A; wiring A,
x1 probes; `#VER fw=0.2.91,...`, number not bumped)**
1. RC 1 kOhm / 15 nF on the post. AUTO SET. Expect a span of about 1 kHz .. 200 kHz and "Auto" in the CTRL > Sweep
   CH2 range row. BODE_CTRL action 0 after the final sweep: header switches 2 (the final sweep ranges from scratch),
   bytes 48..49 = 0. Then RUN for five sweeps, BODE_CTRL after each: header switches 0, bytes 48..49 = 2 ("2 planned,
   0 unplanned"). The relays click at the two planned transitions, and once at each sweep's start (back to the first
   point's range).
2. Narrow span with Hold. AUTO SET always chooses its own span from the overview, so on the RC it never gives a narrow
   one (the brief's "slider to 5 kHz .. 20 kHz, then AUTO SET" ends as step 1). Ways to see Hold:
   - Both tips on the post (the thru wiring) or a resistive divider: AUTO SET says "No corner ... span kept", and the
     CTRL > Sweep CH2 range row reads a V/div. On the post itself expect 1V/div (inference from the model's volts per
     code: a fundamental of about 2 V peak x 1.5 is more than 0.5 V/div's 2.2 V full scale). The final sweep and RUN:
     header switches 0, bytes 48..49 = 0, no relay click during RUN.
   - With a tank on the post (if at hand): AUTO SET on a Q above 10 gives a span f0 (1 -+ 4 / Q) snapped outwards,
     which on a real tank usually spreads more than 20 dB (Auto); Hold needs a span that holds under 20 dB.
   - The row by hand: CTRL > Sweep, tap CH2 range: it reads the V/div CH2 has now; tap again: "Auto".
3. Bug indicators:
   - CH2 range "Auto" after AUTO SET on the thru wiring;
   - a held V/div that clips (the clipped flag, the "CH2 over range" warning) on the AUTO SET span;
   - planned > 0 on a Hold sweep;
   - header switches > 0 on RUN sweeps of an unchanged RC (the 0.2.91 bug list);
   - a BODE_CTRL answer of 48 bytes (an old image).

Maintainer question left open: AUTO SET's span rules (fc / 10 .. 10 fc, f0 (1 -+ 4 / Q) snapped out) put more than 20 dB
inside the span for almost every real filter or tank, so AUTO SET will choose Hold mainly for flat responses. If the
maintainer wants a silent RUN for a narrow band the user dragged to, the same rule could be applied at RUN's first sweep on
the user's span (not built).

## 62. fw-eng6: CH2 range Auto / Constant, read-only requests inside menus (fw 0.2.94, protocol 1.24)

Branch `fw-eng6` from main 81c9a19 (fw 0.2.93). Brief: the maintainer's decisions of 2026-10-20 14:16 (Auto / Constant)
and 14:19 (the AUTO SET notes lose ": span kept"), the bench session's read of 14:19 (a flat AUTO SET ended on
Auto), and the task-queue Dispatched entry "fw-eng6" (READ_SCREEN must not unwind menus). Commits: 39213e7 (engine +
test_bode_sweep), 96eb3e5 (view, texts, BODE_CTRL 58 bytes), 93a0c0f (the CH2 menu sets Constant, alone so it can
be reverted), 70e9a9b (protocol row 60, mcp `BodeCtrlResp`), ce78bc9 (read-only requests inside menus, row 63), then
docs. PC_FW_VERSION not bumped. Host tests green (test_bode_sweep 2226, test_session 70201, test_ui incl. the
fold-out row width), mcp ruff and test_bode_device / test_protocol green, GCC 10 build clean in the changed files.
All files LF (`git diff --check`). **Nothing benched, nothing flashed.**

**Constant (the screen's name for BODE_CH2_HOLD).**
- One source: the CH2 menu's V/div, `scopesettings.channel2.displayvoltperdiv`. The menu sets it at once and copies
  it to `samplevoltperdiv` (the hardware range) only while running, so the view reads the display value
  (`ch2_constant_range`); 50 mV/div (software zoom) sweeps on the 100 mV/div range, and the engine's channel write
  then sets the display to 100 mV/div. `hold_r2` / HOLD_CHANNEL are gone; the engine keeps `bode_start`'s hold_r2
  parameter and the view passes the channel's range.
- The engine writes no other CH2 range on Constant: the pre-scan starts CH2 on the held range (it went to range 1
  before), the collapse test against the pre-scan's reference is off (the reference is on the same range), the
  saturation check in the pre-scan's check runs on the held range. A clip or a drop only flags the point
  (`auto_range` returned early already). Test `test_constant_never_switches`: no CH2 range change in the DUT model,
  fresh and plan sweeps, 0.5 V/div (ch2_small in the stopband) and 0.1 V/div (15 clipped points, no step up).
- The row reads "Auto" or "Constant" (maintainer 14:51: no V/div in the row; up to the merge it read "Constant 100mV" from `volt_div_texts[magnification][display]` without "/div"; the width
  check is in test_ui). A tap toggles; nothing else is stored.
- AUTO SET's Constant writes the chosen range into CH2 (`pc_hal_channel_write(1, .., PC_CH_RANGE)`: the top-bar
  button and its menu show it). On Auto the channel keeps what the final sweep leaves.
- Leaving the mode on Constant restores CH2's enable, coupling and offset but not its V/div: CH2 gets the range of
  the CH2 menu's V/div and keeps that V/div. On Auto it is restored as before.
- Warnings on Constant: "CH2 overrange: increase V/div in CH2 menu or Auto" (a clipped point), new BU_W_CH2_SMALL
  "CH2 small: decrease V/div in CH2 menu or Auto" (a ch2_small point). The flat note is "No corner from 100 Hz to
  10 MHz". The CSV header writes `ch2_range=Constant`.

**The CH2 menu sets Constant (93a0c0f, the bench session's addition, maintainer to confirm).** statemachine.c's channel
menu V/div handler calls `bode_view_ch2_vdiv_by_hand()` for CH2 (the menu's own buttons only; the scope's V+/V-
page does not show in this mode). With the view open it sets Constant and drops the host's exact settings
(host_cfg_valid, like every screen change). A sweep in progress (not AUTO SET's) ends at once and starts again from
its first point on the new range, the same kind (single, RUN, thru). Reason: while running, the menu has already
written the range to the front end (match_volt_per_div_settings), so the engine would finish the sweep on a range it
does not know, and an Auto sweep's later channel writes would overwrite the user's V/div (the maintainer's complaint).
During AUTO SET the change only sets the row; AUTO SET's verdict at its end decides the mode (it writes its range on
Constant). Doubt for the maintainer: a V/div tap in the CH2 menu now also restarts a running RUN sweep (one sweep's
points re-measured).

**The flat path.** `bv_auto_finish` runs the rule on BODE_AS_FLAT too (it always did: the note, then the span kept,
then the rule). Host test: both tips on the post (DUT model `thru`), Start / Stop 20 kHz .. 5 MHz kept: 9 points,
0.5 dB spread, 2.1 dBV -> Constant on 0.5 V/div. So the bench's Auto at 14:19 came from the inputs: a point without
a tone, a spread over 20 dB (e.g. the x1 probes above 2 MHz, inference), or a bad record. Bad records (clipped,
ch2_overload, unsettled, incoherent) no longer count. BODE_CTRL bytes 50..57 now say which it was:
- 50 u8 verdict (0 Auto, 1 Constant, 0xFF none since boot);
- 51 u8 reason (0 one range holds the span, 1 spread >= 20 dB, 2 fewer than two points in the span, 3 a point without
  a tone, 4 too large for 5 V/div);
- 52 u8 points counted, 53 u8 points left out;
- 54..55 i16 spread, centi-dB; 56..57 i16 largest level, centi-dBV peak.
mcp `BodeCtrlResp.auto_ch2_*`.

**Read-only requests inside menus (ce78bc9).** New poll context PC_CTX_MODAL and command flag F_MODAL (READ_SCREEN,
GET_CHANNEL, GET_TIMEBASE, GET_TRIGGER, GET_DECIM, GET_SEQ, GET_GEN, GET_CAL, READ_BODE: their handlers only build a
body from settings / held state). While the UI is modal such a request is not a yield request: `pc_ui_modal_wait`
sets `modal_req`, and `pc_glue_ui_yield` (every modal loop's check, once per iteration after its touch scan, so the
screen is drawn and no FPGA transaction is in flight) runs `pc_poll(PC_CTX_MODAL)`, which answers it; the menu stays
open. A state-changing request reached in that poll asks to yield as before, so a READ_SCREEN queued in front of a
SET_* is answered first, then the screens unwind. A hold reason gives E_BUSY with it at once, no yield check within
PC_YIELD_MS gives detail 2 (both count in GET_LINK_STATS 69). The MAIN poll clears `modal_req`. Dispatches in MODAL
count as BUSY (GET_LINK_STATS disp_busy). test_session `test_modal_read_only`; the 1.23 test's "yields as usual"
case now uses TEST_PATTERN (GET_CHANNEL no longer yields). Not done: SD_INFO / READ_TRACE stay MAIN-only (SD access and
trace conversion inside a file viewer were not reviewed).

**Bench (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`; 0x153A; wiring A, x1)**
1. Both tips on the post, Start / Stop 20 kHz .. 5 MHz, AUTO SET: "No corner from 100 Hz to 10 MHz" (no suffix).
   BODE_CTRL action 0: bytes 50..57. Expect verdict 1 (Constant), reason 0, about 9 points, spread a few dB; the CH2
   button reads the chosen V/div (0.5 or 1 V/div), the Sweep row "Constant". If verdict 0, the reason
   and spread say why (report them). RUN: header switches 0, planned 0, no relay click.
2. On Constant, open the CH2 menu and pick 200 mV/div: the row reads "Constant", the next sweep runs on it
   (READ_BODE r2 4 on every row), the warning "CH2 overrange: increase V/div in CH2 menu or Auto" if it clips. Pick a
   V/div while RUN sweeps: the sweep starts again on it. Leave the mode (main menu Scope) and come back: CH2 still
   on 200 mV/div, the row still "Constant".
3. On Auto (tap the row), RUN on the RC: two planned clicks per sweep as 0.2.93; leaving the mode restores CH2's
   pre-mode V/div.
4. Pick a CH2 V/div in the CH2 menu while on Auto: the row turns "Constant". (The bench session's addition; the
   maintainer confirms or 93a0c0f is reverted.)
5. Screenshots: open Menu > Settings (and, separately, the generator menu, a file viewer) and run
   `tools/screenshots.py --only settings`: the PNG shows Settings, and Settings is still open afterwards. A
   state-changing command (e.g. `s.set_timebase(...)`) still closes it.
Bug indicators: a CH2 relay click during a Constant sweep; CH2 back on the pre-mode V/div after leaving on Constant;
the row reading "Constant" with a V/div the CH2 button does not show; READ_SCREEN answering E_BUSY detail 2 while a
menu is simply open (a modal loop without a yield check); a menu closing on GET_CHANNEL.

## 63. fw-yield3: host requests leave Diagnostics (fw 0.2.96, protocol 1.24 unchanged)

Branch `fw-yield3` from main 6f0a273 (fw 0.2.95). Bench 2026-10-20 15:25 (fw 0.2.95, maintainer's unit): Diagnostics
(Calibration tab) open while `tools/screenshots.py --port <port> --generator` ran. Diagnostics never left, and the
PNGs (docs/screenshots/scope.png, scope-mcp.png, frequency-response-sweep.png on main) show the Diagnostics frame
with the Spectrum's waterfall rows, ACQ chip, cards and slider drawn into it, later the Frequency response's empty
plot ("Set by the sweep", markers). Commits: e8750f5 (dispatcher, glue, test), then version, docs and this handoff.
**Nothing benched, nothing flashed.** Host tests green (`make test`: test_session 70292 / 70290 with the new
`test_main_screen_leave`), `make gcc10` clean in the changed files.

**Cause.** Not fw-eng6's read-only classification: that is right (F_MODAL = READ_SCREEN, GET_CHANNEL / TIMEBASE /
TRIGGER / DECIM / SEQ / GEN / CAL, READ_BODE; every modal loop still yields for anything else, checked: the
loops all go through `PC_UI_YIELD()` = `pc_glue_ui_yield`, which answers F_MODAL at `PC_CTX_MODAL` and returns the
yield flag for the rest; no loop-specific code was added by fw-eng6). Diagnostics is not a modal loop: it is a
main-loop screen (`ui_diag_pass` instead of the scope's pass, section 34), so `ui_busy` stays 0 and the 1.23 yield
never reaches it; section 53 listed it under "Not changed" ("main-loop modes, answered there without closing
anything"). So every host request was served by the MAIN poll with Diagnostics open, since 0.2.59, not only since
0.2.94. The handlers' redraws mostly go to the kept scope screen (`ui_diag_scope_buffer`, pc_draw_begin,
pc_right_bar_follow, bode_view_close, the battery icon) but many view paths draw straight to `maindisplaybuffer`:
the time base write redraws the view (`scope_preset_values` -> spectrum / waterfall), `spectrum_set_active`,
`bode_view_open` (`bv_draw_acq_chip`), and (inference, not traced one by one) the channel / trigger writes' top-bar
button redraws. That is the waterfall
and the Frequency response inside the Diagnostics frame. (The maintainer's memory of 0.2.78 closing Diagnostics does not
match the code; the screenshots tool before 0.2.94 closed menus only.)

**Fix (the 0.2.78 rule extended to the main-loop screen).** `pc_dispatch` (MAIN context, right before the handler):
a request outside F_BUSY and F_MODAL calls `pc_hal_ui_leave_screen()`; on the target (`pc_hal_scope.c`) that is
`ui_diag_leave()` = Back (`ud_close`: the kept scope screen, which the background redraws kept current, is copied
back). The request then runs and draws on the view; the next passes redraw it as after a Back tap. A leave counts in
GET_LINK_STATS 68 (`yields`). F_BUSY (PING, GET_STATUS, GET_HEALTH, link stats, event log, SET_REMOTE, ...) and
F_MODAL requests leave Diagnostics open, so `--only diagnostics` / `diagnostics-calibration` still capture it.
The wizard and the volts calibration inside Diagnostics are modal loops: they cancel by the yield first (nothing
written), then the MAIN poll leaves Diagnostics. Draws over a screen that owns the display: with Diagnostics left
before any state-changing handler, no handler draws under it; background passes while it shows were already sent to
the kept buffer; modal loops only answer read-only requests (no drawing) inside the loop. Not changed: the view
paths that draw to `maindisplaybuffer` directly (no longer reachable with Diagnostics open by a host request; a
UI path inside Diagnostics that reached them would be a separate bug).

**Test** (test_session `test_main_screen_leave`, mock `ui_screen`, `ui_screen_leaves`, `under_screen` = setting
writes / BODE_CTRL made while the screen showed, `screen_read_under`): Diagnostics over the Spectrum (display mode 1)
and over the Frequency response (5): READ_SCREEN, GET_CHANNEL, GET_TIMEBASE, GET_STATUS, PING answered with it open
(the screenshot of Diagnostics); then the screenshots.py "scope" sequence (SET_CHANNEL x2, SET_TIMEBASE, RUN,
READ_SCREEN): the first request leaves it before writing, nothing written under it, READ_SCREEN after it reads the
view, one count in 68; SET_TIMEBASE alone the same; the wizard over Diagnostics: the yield cancels the wizard, the
MAIN poll leaves Diagnostics, then the write. BODE_CTRL open (9) and run (1) leave it first; READ_BODE does not.
Without the fix 35 checks fail.

**Bench (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`; `#VER` fw=0.2.96)**
1. Scope view, Menu > Diagnostics (Calibration tab). `tools/screenshots.py --port <port> --only scope`: Diagnostics
   closes at the first SET_CHANNEL, the Scope view shows with both channels at 1 V/div, 200 us/div, RUN, and
   docs/screenshots/scope.png is the Scope view (no Diagnostics chrome). The view under Diagnostics is the one it was
   opened from: opened from the Spectrum analyser, the Spectrum shows (the tool does not change the display mode).
2. Diagnostics open again, `--generator` (all automatic states): Diagnostics closes at the scope state's first
   request; scope / scope-mcp as 1, frequency-response / -sweep show the Frequency response. BODE_CTRL alone: open
   Diagnostics, `--only frequency-response`: Diagnostics closes, the PNG is the Frequency response just opened.
3. Diagnostics open, `--only diagnostics-calibration`: the PNG is the Diagnostics Calibration tab and Diagnostics
   stays open (READ_SCREEN is read-only). The MCP server's `scope_status` / `scope_health` leave it open too.
4. Menu > Settings > Other settings, `--only settings`: the PNG shows Settings and Settings stays open (unchanged
   from 0.2.94). `s.set_timebase(...)` closes it.
5. Diagnostics > Calibrate, screen 1, a SET_TIMEBASE: the wizard closes (nothing written) and Diagnostics closes
   too, the Scope view shows.
6. GET_LINK_STATS 68 counts one per Diagnostics leave.
Bug indicators: any view element (waterfall rows, cards, ACQ chip, markers, slider) drawn over Diagnostics;
Diagnostics closing on READ_SCREEN, GET_* or GET_STATUS; the Scope view left half drawn after the leave (parts of the
Diagnostics page still on screen).

## 64. fw-arm: auto-mode records without the splice, the stop geometry on 0x1532..0x153A (fw 0.2.97, protocol 1.24)

Branch `fw-arm` from main 6f0a273 (fw 0.2.95), rebased on ac1f2ab (fw-yield3's 0.2.96 and fpga-gen11 merged). Brief:
fpga-gen11's finding and prescription (fpga/al3/README.md "Changes" section 11, fpga/al3/AGENTS.md "Spliced time-out
records, fpga-gen11", protocol.md 7.5). Commits: 49d99bf (pc_decim helpers, fpga_model capture logic, test_fpga_bus),
13f88b1 (host ARM, UI auto mode, segments, the flag), cd1df51 (Frequency response engine), dc35b9f (mcp), then version,
docs and this handoff. **Nothing benched, nothing flashed.** Host tests green (`make test`: test_session 70348 /
70349, test_fpga_bus 11792, test_bode_sweep 2285), mcp pytest green except test_report (matplotlib missing in the
local venv, unrelated), ruff selection clean, `make gcc10` clean in the changed files. Protocol: 1.24 unchanged
(GET_CAPTURE_INFO flags bit 6 is a spare bit; no caps bit: 0x153B is told by the version word).

**The rule** (`pc_decim.h` "Auto-mode stop geometry"). 0x0E is 32 bit and counts written words at every K
(`time_base_cnt` advances on `swc_adv`, zaklad.v; 2^32 words = 43 s at 100 MHz words, so 0x0E reaches every deadline
the firmware uses); the stop comes after W = 0x0E + 2 words (`PC_STOP_LAG`, fpga-gen11 tb A1). On 0x1532..0x153A the
record of an untriggered stop is ring words 0 .. total - 1, contiguous iff W mod 8192 >= total. `pc_auto_stop_word(min,
total, ring)` returns the first W >= max(min, total + m) with total + m <= W mod ring <= ring - 1 - m (m =
`PC_STOP_MARGIN` 4 when it fits, else 0); 0x0E = W - 2. This generalises the brief's 0x0E = 8192 k + total - 2: W
need not sit at ring word total, any ring word from total on works, so the deadline is kept exactly whenever its own
ring word is allowed and moved by at most total + 8 words otherwise (the brief's rule could add a whole ring: 16 s at
1 kSa/s). Examples: 200 MSa/s, 3000 samples: deadline 100 ms = 10,000,000 words, ring word 5760: W = 10,000,000,
0x0E = 0x0098967E (no move). 1 MSa/s, 3000 samples: 50,000 words, ring word 848 < 1504: W = 50,656, 0x0E = 0xC5DE (1.3
ms later). Records up to about 4090 words never move at slow rates (2 x total stays in the first pass); deeper records
at slow rates move by up to 8192 - total words (less than one record). `pc_stop_kind(version)`: NONE (stock,
unknown), SPLICE (EF2 / EP4: Atlan4's logic by inference, not modelled: they keep the force, forced records flagged),
RING0 (0x1532..0x153A), NEWEST (0x153B..0x153F).

**Per arm path.**
- Host ARM (pc_cmds.c `h_arm`, UI mode 0, AL3 RING0, no sequencer): `arm_normal = 0`, `arm_auto = W - 2` with W from the
  0.2.41 deadline (`pc_host_auto_ms`: max(100 ms, two records, the explicit auto_timeout) at `pc_hal_divider_now()`,
  `pc_ms_to_words` in the arm's layout). `auto_geom`, `auto_stop`, `auto_stop_ms` in the session. The firmware time-out
  is the safety net at `pc_auto_net_ms(W's ms)` = + 50 ms + 1/8: if it fires, `pc_hal_force_stop` (0x0E = 0, 0x1A = 0,
  since 0x1A is already 0) and `cap_spliced`. At DONE: `addr == cap_pre_words` -> untriggered (the late edge after the
  stop); a triggered record with W's ring word inside it (`pc_auto_cut`) and DONE within (total - pre) words + 2 ms of
  W's time -> `cap_spliced`. `cap_spliced` is per arm (reset by the enc re-arm too) and goes into the capture's flags as
  `PC_CAPTURE_F_SPLICED` (0x40) just before `pc_capture_finish`. A host FORCE (PC_DEBUG builds) writes 0x0E = 0 under the
  geometry and flags the record on RING0 / SPLICE bitstreams (normal mode too: its 0x0E is long past). 0x153B+: the 0.2.41
  path unchanged (normal arm, `pc_hal_force` at the deadline, `host_forced` keeps "untriggered"), never flagged.
- Host segment runs (SEGMENTS, auto mode, RING0): `arm_auto` (default the total samples) moved to `pc_auto_stop_word(arm_auto
  + 2, tot_w, 2^m) - 2`. README section 6's bench check 2 (120 words in 128-word slots, 0x0E 240) stopped every untriggered
  segment at slot word 114 and spliced it; now 0x0E = 246 (test_session). Inference: the per-segment `cap_rst` resets
  `sample_write_address` and `time_base_cnt` together, and the slot address is the write address mod 2^m; not
  benched, no tb. Averaging runs (host or UI) are unchanged: they skip untriggered records.
- UI (pc_hal_scope.c `pc_glue_decim_conversion`, every conversion): the averaging-run check moved before the 0x1A write
  (`pc_seq_ui_conversion`, no bus access). Auto mode on RING0 (fw_FPGA 2, not an averaging run): 0x1A = 0, `fpga_conv_0e`
  = W - 2 (fpga_control.c's `fpga_do_conversion` writes it instead of `samplecount` when non-zero), `pc_ui_auto_ms` = the
  net. Other FPGA-auto conversions (calibration, auto setup, triggermode 0 without `ui`): W from `samplecount + 2`, 0x0E
  written only when it moves. The deadline now uses `pc_hal_divider_now()` (was the UI table's divider). `pc_ui_auto_service`
  forces with `pc_hal_force_stop` under the geometry and sets `pc_ui_spliced` unless NEWEST. `pc_glue_ui_capture_addr`:
  0x14 == the 0x0B words -> `ui_trig = 0` and `pc_scope_untrig`; the cut check as the host's; the flag into
  `pc_sess.cap_next.flags`.
- Frequency response (pc_bode.c `bh_arm`): still FPGA auto with the CH1 trigger and 0.2.91's 0x0E = 2 x total; on RING0
  moved to W (records above about 2700 legacy / 4090 16-bit words wrapped before). The brief's "arm normal" was not
  taken: with the geometry a missing trigger gives a contiguous record (normal + force would splice), and a trigger
  completes whenever it comes more than total words before W (the engine's 2 x total gives it the first record's
  length; the generator's period is far shorter). `bh_read`: 0x14 == pre -> untriggered; `bode_cap.spliced` for a
  forced record (`bh_force` after the engine's deadline, not on NEWEST) or a cut trigger; `bode_step` memsets the cap
  and retakes a spliced record like an untriggered one (counted as "other" in BODE_CTRL 46). 0x153B+: unchanged arm.

**Flag and host.** GET_CAPTURE_INFO byte 9 bit 6 (protocol.md row 37, 7.5 "the stop geometry"); mcp
`CaptureInfoHeader.F_MAYBE_SPLICED`, `Capture.maybe_spliced` (False when read without GET_CAPTURE_INFO: the `ctx` fast
path of long runs reads only WAIT's status, whose flags byte has no spare bit; not changed), caveat "record may be
spliced ... capture again", `.npz` meta.

**Tests.** test_fpga_bus `test_timeout_records` (fpga_model capture logic with write-index words, per bitstream, pre 0 /
200, total 2048): forced stop after 1.12 rings spliced on 0x153A / newest on 0x153B (the brief's 1.3 rings lands at ring
word 2458 > 2048, contiguous by luck: 1.12 used); FPGA auto 0x0E = total + trigger at 1024 cut at W = 2050 on 0x153A,
complete on 0x153B; the geometry for 3 rings + 500: stop exactly at W, 0x14 = pre, contiguous last pass; late edge
(0x18 = 1, 0x14 = pre); early trigger complete; cut trigger seen by `pc_auto_cut`; 24 random force times per pretrigger:
0x153B always contiguous, 0x153A spliced exactly when W mod 8192 < total (16 of 48). test_session
`test_auto_stop_geometry` (the rule, a 20000-case sweep, cut, conversions, kinds; 0x153A's arm, late edge, early and cut
triggers, the safety net with `force_stops`, FORCE in the debug build, normal mode unchanged, EF2 flagged),
`test_fw_auto_timeout` now on 0x153B (unflagged), the segment 0x0E. test_bode_sweep `test_spliced_retake`.

**Not done / limits.**
- A forced stop on 0x1532..0x153A cannot be made exact (W is not readable): flagged instead. Only the safety net and a
  host FORCE force there now.
- A trigger in the last total - pre words before W is still cut on 0x1532..0x153A; flagged by a timing check (DONE seen
  within that window + 2 ms of W's time with W inside the record). At fast rates the window is microseconds, so a
  trigger that comes near the 100 ms deadline and completes normally, with W's ring word inside its record, is flagged
  too (over-flagging, rare: about total / 8192 of the triggers that arrive in the last ms).
- The UI's elapsed time is measured at the 0x14 read, after its done poll; the cut check can over-flag a UI record
  read late. The Scope view does nothing with the flag (GET_CAPTURE_INFO only).
- Segments: the slot rule is an inference (above). EF2 / EP4 not modelled.

**Bench (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`, fw 0.2.97).**
On 0x153A (today's unit):
1. `#VER` fw=0.2.97. `s.event_log(fpga_trace=True)` around one host auto capture at 200 MSa/s, 3000 samples, level 255
   (`s.set_trigger(mode="auto", level_code=255)`): 0x1A = 0 before the reset, 0x0E = 0x0098967E inside it, no 0x1A / 0x0E
   write after the release (no force). At 1 MSa/s: 0x0E = 0xC5DE.
2. 50 host captures as fpga-gen11's bench (RC on the post, generator 21.6 kHz, CIC3+FIR 2320 .. 3408 words, level 255):
   GET_CAPTURE_INFO flags bit 1 set, bit 6 clear, capture_start 0; the two-segment fit (`splice.py` pattern) finds no
   seam in any (0.2.96: 15 of 32). `python firmware/tools/bode_cells.py <port>`: every "auto" record leak below -60 dB and
   |H| within 0.01 dB of "host".
3. Scope view, auto mode, marker above the signal, 1 kHz on CH1 at 1 MSa/s: 50 untriggered traces without a step
   (READ_TRACE each, or screenshots); then at 200 MSa/s the same. Refresh rate as on 0.2.96 (one trace per about 100 ms).
4. Frequency response, ten RUN sweeps 100 Hz .. 1 MHz / 25 on the RC: retried 0 .. 1 per sweep (0.2.89 .. 0.2.96:
   5 .. 15), BODE_CTRL 40..47 timeouts 0 .. 1, other retakes 0 .. 1, 21.6 kHz -6.74 dB in 10 of 10.
5. Slow signal (0.5 Hz square from the generator, 10 MSa/s, auto): untriggered traces until an edge, then triggered at
   the marker; no flat record flagged triggered (`s.capture()` flags; GET_CAPTURE_INFO bit 0 only with a crossing).
6. Segments in auto mode with the generator off (README section 6 step 7, m 7, total 240): every untriggered segment
   flat without a seam; the trace shows 0x0E = 246.
On 0x153B (after the maintainer flashes it; the fpga-gen11 tb verdict is still open, see fpga/al3/AGENTS.md):
7. `#VER` fpga=0x153B; the trace of step 1 shows the 0.2.41 arm (0x1A = 1, then 0x1A = 0 at about 100 ms), records
   contiguous (the newest total words, capture_start = W - total), bit 6 never set.
8. Steps 2 .. 6 again: no seam, the sweeps' retries 0 .. 1.
Bug indicators: bit 6 on ordinary auto captures (the FPGA did not stop at W: 0x0E not taken; trace the arm); a seam in
an untriggered record on 0x153A (W's rule or `PC_STOP_LAG` wrong: compare capture_start and W mod 8192); the UI's
auto traces refreshing much slower than on 0.2.96 (the deadline moved too far); "triggered" on a flat record.

**Addendum, fw-arm2 (fw 0.2.98, 2026-10-20, branch `fw-arm2` from main 54b0f32).** Bench on 0.2.97 / 0x153A (54b0f32):
host captures clean (100 untriggered, no seam, bode_cells auto leak -65..-83 dB), but the Frequency response engine
regressed: per sweep 33..40 captures, 10..22 timeouts, 8..15 other retakes, 1 flagged point on 4 of 10, 2.4..3.6 s
(0.2.91..0.2.96: 25, 0, 0, 0, 1.5 s). **Cause: the two host rules applied in `bh_read` misread the engine's records**,
which have pretrigger 0 and a two-record time-out:
- "0x14 == pretrigger -> untriggered" (the timeouts): with pretrigger 0 the trigger check runs from the first word,
  and the sampling reset clears `trigger_previous` to 0 (zaklad.v), so a first sample at or above the level is a rising
  edge at ring word 0 = the pretrigger. The engine's level is CH1's mean, so that happens in about half the arms: a
  complete, contiguous record (0.2.91..0.2.96 counted it triggered from 0x18), now counted a time-out and retaken.
- The cut check (the other retakes): two records exceed the ring above 2047 legacy words (0x0E = 2 x total samples =
  4 x total words), so the stop geometry puts W at ring word total + 4 of the second pass; W's ring word then lies
  inside every record that starts after word 4, i.e. every early trigger, and the "DONE within the window + 2 ms of W's
  time" test is always true for a capture that lasts microseconds (W's time rounds to 1 ms).
**Fix** (d1f4e6b): `pc_engine_record` (pc_decim.c): the engine's record is triggered when 0x18 was seen in
a poll (0.2.91..0.2.96's reading) and maybe spliced only when forced (not on 0x153B+). The stop geometry stays (a record
without a trigger is contiguous, so a late edge read as "triggered" costs nothing). The UI applies the 0x14 / cut rules
only to its auto mode (`pc_ui_rules`), not to calibration / auto-setup FPGA-auto conversions, whose pretrigger may be
small too. The host path is unchanged: its deadline is at least 100 ms, so the timing test excludes early triggers,
and its pretrigger is normally well above 0; a host auto capture armed with pretrigger 0 can still read a word-0
trigger as untriggered (record correct, `auto_timeout` set). test_fpga_bus reproduces the engine's arm on the 0x153A
model (pretrigger 0, total 2048 words, W = 8192 + 2052): triggers at word 0 and 37 and none, every record contiguous;
0.2.97's rules read the first as a time-out and flag the second, `pc_engine_record` reads both triggered, none spliced.
**Bench (0x153A):** ten sweeps 100 Hz .. 1 MHz / 25 on the RC: BODE_CTRL 40..47 about 25 captures, 0 timeouts (0..1 on
the first pre-scan), 0 leak retakes (0..1), 0 other retakes, no flagged point, about 1.5 s per sweep; the host checks of
0.2.97 unchanged (50 untriggered auto captures each at 1 / 200 MSa/s, maybe_spliced 0, no seam).

## 65. fw-floor: points below the floor say so and what to do (fw 0.2.99, protocol 1.24 unchanged)

Branch `fw-floor` from main 54b0f32 (fw 0.2.97). Brief: the maintainer's decision of 2026-10-20 16:22 ("Frequency response:
points below the floor say so and what to do") and the bench read behind it (hardware-status 2026-10-20 15:5x: a
two-pole RC swept 10 kHz .. 10 MHz, every point above about 500 kHz ch2_small at 100 mV/div, CH2 about -55 dBV, the
magnitude climbing back from -69 to -34 dB with the phase flipping: generator pickup; only the 540 kHz low_snr point was
grey). Version 0.2.99 because fw-arm2 holds 0.2.98 in parallel (bh_arm / time-out / retake in bode_sweep.c; this
branch touches one line of bode_sweep.c, in `bode_auto_span`'s "good" count, so the merge is clean apart from the
PC_FW_VERSION and CHANGELOG lines). Commits: a590c1f (firmware + host tests), 434365c (version), 41cf0e5 (mcp), then
docs and this handoff. Host tests green (`make test`: test_ui 1434277, test_bode_sweep 2285, test_session 70348 /
70349, test_bode.py); mcp ruff selection clean and the full pytest 459 passed (in a scratch venv with matplotlib, so
test_report ran too); `make gcc10` builds with no warning in the bode files (`#VER fw=0.2.99`). All files LF.
**Nothing benched, nothing flashed.**

**The grey set.** `BODE_F_BAD` (bode_dsp.h) gains `BODE_F_CH2_SMALL`. Everything that uses it follows: the view's
`p_ok` (grey loose dots in magnitude and phase, the slider track's grey pixels, the axis scales from valid points),
`bode_markers_find` (Level, -3dB, BW, Peak, Q, the phase crossings: so the cards ignore them; on main they counted
ch2_small points, which the brief asked to check: they did, now they do not), the cursor box (grey, reason "CH2 small",
after low SNR in `bu_flag_reason`), the thru's cal_flagged, and AUTO SET's `bode_auto_span` valid points. In
`bode_auto_span` a ch2_small point also leaves the "good" count, so an overview with only floor / small points reads
"Auto set: no response". `bode_auto_ch2` (CH2 Constant / Auto) is unchanged: it uses `BODE_CH2_JUNK`, where small
levels count on purpose. READ_BODE rows are unchanged (bit 4 was always sent); docs/protocol.md row 60 now lists bit 4
among the invalid ones. ch1_low, spliced, retried, fir_off still count.

**The note** (bode_ui.c `bu_ch2_floor_f`, `bu_fmt_f3`, `bu_warn_ch2_floor`, `bu_ch2_floor_line2`; bode_view.c
`warning()` now returns an optional second line, drawn 16 px under the first in 13 px, `BU_WARN_LINE`).
- When: a measured ch2_small point with r2 = 5 (`BU_CH2_FINE_RANGE`, 100 mV/div), on Auto or Constant, not during AUTO
  SET (`bv.autoset`: its overview to 10 MHz would always show it, and AUTO SET is what the note offers).
- Text: "CH2 too small above 541 kHz to measure: set Stop below it or press AUTO SET." / "Less pickup: short ground lead
  on CH2." Both lines fit (test_ui: every frequency 1 Hz .. 10 MHz, x 56..666 at 13 px, at most 77 characters, y 69 and
  85 inside the free top division), so the one-line fallback of the brief was not needed.
- The frequency: of the ch2_small points on range 5, the lowest of those above every other measured point (the run
  that ends the span; unmeasured points do not break it, the point order does not matter); when the highest measured
  point is not one (scattered points), the lowest of them. Three significant digits, a space, Hz / kHz / MHz
  ("541 kHz", "54.1 kHz", "1.23 MHz"). Inference for the bench: if the 540 kHz point is low_snr without ch2_small, it
  ends the run and the note names the next point up.
- Precedence (bode-ui.md section 6 now lists it): errors, thru sweep, AUTO SET failure, tap notes, Constant
  overrange, Constant "CH2 small: decrease V/div ..." (now only for small points on a range coarser than 100 mV/div:
  on the finest range there is no finer V/div to choose), **this note**, CH1 low, unsettled, near the floor, thru
  stale, offset, the other notes. No floor line is drawn (maintainer: the generator-off background does not see pickup).

**mcp** (bode.py): `BAD_FLAGS` gains "ch2_small" (markers, ThruCal cal_flagged, report.py's valid mask); `CH2_FINE_RANGE`,
`fmt_f3` (the same integer rounding as bu_fmt_f3), `ch2_floor_hz` (the same rule on `PointResult.ch2_range` and
f_actual), `ch2_floor_text`; `result_caveats` puts the sentence first (so `scope_bode_sweep`'s caveats and the CLI
show it); the per-flag ch2_small caveat says the points are left out. report.py's Bode plot joins valid points only
(the line went through flagged ones). Tests: test_bode `test_ch2_small_is_grey_and_the_floor_caveat`, test_bode_device
(the invalid bits 0, 1, 2, 4..8).

Not done / for the next agent:
- On Auto, ch2_small points on a coarser range (the ranging had not reached 100 mV/div, e.g. the six-switch budget
  used up) are grey but get no note of their own (the floor count note covers only near_floor / low_snr).
- The host-driven sweep (`where='host'`) ranges to 100 mV/div as well, so the caveat applies there; nothing on the
  host draws the note's two lines (the caveat is the sentence).

**Bench (maintainer, bench session on <port>; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`; 0x153A; wiring A, x1)**
1. `#VER fw=0.2.99`. Frequency response, the two-pole RC on the post, CTRL > Sweep 10 kHz .. 10 MHz, CH2 Auto, Single
   sweep. Expect every point from about 500 kHz up grey (magnitude and phase, the slider track too), the curve joined
   only below it, and the top-middle note in two orange 13 px lines: "CH2 too small above 5xx kHz to measure: set Stop
   below it or press AUTO SET." / "Less pickup: short ground lead on CH2." READ_BODE: those rows have bit 4 and r2 5;
   the note's frequency is the lowest of the run of such rows that reaches 10 MHz.
2. The cards (Level, -3dB, Peak, Q): as on 0.2.97 for the passband; no Peak from the pickup tail. A cursor on a grey
   point: the box row grey, reason "CH2 small" (or "low SNR" where both apply).
3. The same span from the host: `scope_bode_sweep(where='device', start='10kHz', stop='10MHz', ...)`: the first caveat
   is the same sentence with the same frequency; `scope_report` of it: the tail drawn as flagged markers, the line
   only through valid points.
4. Set Stop to 500 kHz (or below the named frequency), Single sweep: no note, no grey tail. AUTO SET from 10 kHz ..
   10 MHz: during it no note; its result a span around the corner without the note (if its final sweep still reaches
   the floor, the note shows after it).
5. Constant at 100 mV/div (CH2 menu) over the full span: this note, not "CH2 small: decrease V/div ...". Constant at
   0.5 V/div: "CH2 small: decrease V/div in CH2 menu or Auto" as on 0.2.94.
6. A short ground lead on CH2 at the DUT output: the note's frequency moves up (less pickup), if the maintainer wants to see
   it.
Bug indicators: grey points with the curve drawn through them; the note during AUTO SET; the note naming a frequency
below a measured non-small point at the top of the span; "CH2 small: decrease V/div" on Constant 100 mV/div; the
second line overlapping data in the top division when the scale leaves it free.

## 66. fw-diag: Restarts verdicts and the open-input noise limits (fw 0.2.100, protocol 1.24 unchanged)

Branch `fw-diag` from main 7d883e5 (fw 0.2.99). Brief: the maintainer's decisions of 2026-10-20 18:12 (verdict meanings,
the Restarts tile) and 18:13 (the noise rows' limits are the calibration's open-input condition). Commits: 5879193
(firmware + host tests), 53f37e1 (version), then mcp, docs and this handoff. Host tests green (`make test`: test_ui_diag
12516, test_cal_wizard 680, test_adc_cal 59851, the rest unchanged); mcp ruff selection clean, pytest green; `make
gcc10` builds (`#VER fw=0.2.100`). All files LF. **Nothing benched, nothing flashed.**

**Verdict words** (maintainer): OK = everything normal; Check = something to fix at some point; Fault = something critically
bad. Written into docs/guide/screens.md.

**Restarts tile** (`ui_diag.c` `ud_tile_restarts`, `ud_exc_crash`, `ud_tile_say`, `ud_tile_up`). A crash is an
exception of type 1..6; type 0 (jump to the image entry: a FEL load, Enter FEL, an update, a self-restart) is normal.
- OK: no crash this run and no stored crash record: "No crashes." / "Up 27 min.". Also after a deliberate restart this
  run (counter 49 > 0, counter 50 = 0) and with a stored type-0 record.
- Check: counter 49 = 0 and a stored record (GET_HEALTH 44 bit 0) of a real exception: "Crashed once before this run:"
  / "data abort." / "If it happens again," / "save a report." / "Up 27 min." (the count is the record's restarts, 46;
  a large count with a long exception name drops the count so "Up" keeps its line). The stored-record condition no
  longer looks at bit 1 (record of this power-on): with counter 49 = 0 such a record cannot be a crash of this run.
- Fault: counter 49 > 0 with a real exception in counter 50: "Crashed 2 times this run:" / "data abort at 0x80014a2c."
  / "Details on the Log page." (hex in lower case as on the Log page; the PC is LR - 8 / - 4 as there).
- Lines are wrapped at spaces to the tile's 168 px in the 13 px font (`ud_tile_say`); the tiles have five lines now,
  15 px apart from y + 50 (the last ends at y + 123 of 126; 17 px from y + 52 for four lines before). The Check case
  needs five. Every exception name with counts 65535 / 0xFFFFFFFF and the longest uptime fits (test_ui_diag).
- The Log page ("Last restart", type, LR, PC, fault registers, the stored record) and event 15 are unchanged.
- Known limits, not changed: counter 49 counts every warm restart since the cold boot / ENTER_FEL, so "Crashed N
  times" counts the deliberate ones in the same chain too when the latest was a crash; and a deliberate restart after a
  crash overwrites the stored record (`pc_life_boot` stores every warm restart), so that crash then reads OK. A crash
  count of its own (DDR record word, a new link counter) would fix both; not built.
- Firmware tile: unchanged. Its Check states are things a user can fix: no SD card (settings not saved), a card image
  of another version ("from RAM. A power cycle starts 0.2.25 from the card." already says it in those words), no open
  firmware on the card, an unknown card image.

**Noise limits** (`adc_cal.h` `ACAL_MAX_NOISE16_FULL` 3 x 16, `ACAL_MAX_NOISE16_GATED` 1 x 16, `ACAL_MAX_NOISE16(cls)`;
`ACAL_MAX_NOISE16` without a class is gone). Used by `acal_range_check` (the stored noise per class), `cal_train`'s
quiet-input check (gated within-capture RMS, now 1 code) and Diagnostics (`UD_LIM_NOISE_GATED` / `_FULL` = 100 / 300
hundredths, derived from them). **The wizard's gated criterion tightened from 3.0 to 1.0 code**, backed by the stored
values on the maintainer's unit (bench session, GET_CAL 2026-10-20 18:1x, worst range per core): gated CH1 A 0.50, B 0.06,
CH2 A 0.25, B 0.38; full CH1 B 1.19, CH2 A 2.75 (5 V/div; the high-impedance ranges 5 V, 1 V, 200 mV the worst; a
low-impedance source reads 0.5..0.6 at 200 MSa/s). The class gain ratio limit (2 %) is unchanged.
- Status page: headings "Noise RMS, inputs open, gated" / "..., full", limits "limit 1.0 code" / "limit 3.0 code"
  (`s_limit_code` from the constants); the "Limits are provisional" note is gone; the box moved down 8..10 px to give
  the reason two lines. Over a limit: "A value is over its limit (below): move chargers and the USB cable away and
  calibrate again." / "If it stays, the input stage is noisy." (`ud_cal_verdict` puts a newline in the reason;
  `ud_status_cal` draws the second line at y + 25).
- Calibration tab: "Noise RMS, inputs open: gated 0.50, full 1.19 code" (amber over a limit), then "Gated limit 1.0
  code, full limit 3.0 code" (the channel block's last line now ends at y + 269 of 270).
- Wizard: `cw_reason_text` "too much noise: unplug the probes, move chargers and USB away"; a noise failure of the stored
  value (no measured point) gets the note "Noise RMS, inputs open, limit 1.0 code: move chargers and the USB cable
  away." (or 3.0 at 200 MSa/s). Pass texts unchanged ("CH1: zero level done").
- mcp: `protocol.CAL_NOISE_LIMIT16_FULL` / `_GATED`, `cal_noise_limit16(cls)`; CAL_PROBE's `noisy` per point class.
  The simulator's probe keeps the full-rate limit for every point (its noise model has one level, 1 LSB, for both
  rates; real units read gated far lower).

**Version 0.2.100**, the first three-digit patch. Checked: `release_meta.py` (`\d+` in the tag, the PC_FW_VERSION and
the CHANGELOG regexes), release.yml's `#VER` grep (`fw=[^,]*`), `pc_ident_version_at_least` (numeric), the card scan
(up to 15 characters), the mcp (no version string compares; gates use the protocol). Nothing assumed two digits. The one
real limit: `pc_image_id.version` is 16 bytes and C accepts a 16-character initializer without its NUL;
"0.2.100-m1-nommu" (both A/B suffixes) is 16, so `build_id.c` now has a compile-time check that the version is at most
15 characters (that combination no longer builds; the default, `-m1` and `-nommu` builds do).

**Bench (bench session; FEL load of `build-gcc10-pc/fnirsi_1013d.bin`; never during a calibration)**
1. `#VER fw=0.2.100`. Menu > Diagnostics > Status. Restarts tile: green "OK", "No crashes.", "Up N min." after the
   FEL load (ENTER_FEL clears the record; a stored type-0 record no longer gives Check). Log page: as on 0.2.99.
2. Calibration box: "OK Calibrated, every value within its limit." Headings "Noise RMS, inputs open, gated" / "...,
   full", limits "limit 1.0 code" / "limit 3.0 code"; CH1 full 1.19 code OK, CH2 full 2.75 code OK, gated 0.50 / 0.38
   (the worst per channel) OK. No "provisional" line; the provenance line at the bottom of the box.
3. Calibration tab: per channel "Noise RMS, inputs open: gated 0.50, full 1.19 code" (CH2 "gated 0.38, full 2.75
   code") in white, then "Gated limit 1.0 code, full limit 3.0 code"; the last line (1.00o5 core compensations) still
   inside the block.
4. Optional: run Calibrate with the inputs open: the zero step passes as before (gated well under 1.0). With a probe
   on an input near a charger a noise failure shows the new second line with the limit.
Bug indicators: Check or Fault on Restarts right after a FEL load; any noise value under its limit drawn amber; the
advice's second line overlapping the column headings; a tile line cut.

## 67. fw-sd: the SD write window ends at the partition, capped at 16384 (fw 0.2.101, protocol 1.24 unchanged)

Branch `fw-sd` from main 6322e98 (fw 0.2.100). Brief: release blocker of the bench 2026-10-20 18:51:
`fnirsi1013d-flash --persist-only` of fw 0.2.100 (773,120 B = 1510 sectors, 750..2259) refused, because the window
was [16, min(partition, 2048)) and the image ends at 2260, on the maintainer's card partitioned at 8192. A card made per the
install guide (FAT32 from 2048) could not hold the image at all. Commits: f2c53e7 (firmware + host tests), 1563daf (SD
tools), a7c5727 (mcp), f758b51 (docs), then CHANGELOG and this handoff. Host tests green (`make test`: test_session
72125 checks with the new `test_sd_window`, test_diag 473); `make gcc10` builds (`#VER fw=0.2.101`, 774,144 B = 1512
sectors, ends at 2261; text 652,433, data 121,460, bss 8,148,216); mcp ruff selection clean, pytest green apart from
`test_report.py::test_reports_for_every_kind`, which fails the same way on main here (local renderer, not this
change). All files LF (docs/hardware-prep.md was CRLF already and stays so). **Nothing benched, nothing flashed.**

**The cap: PC_SD_LIMIT = 16384 sectors (8 MiB).** `pc_sd_write_end` = min(partition start, 16384), 0 without a valid
MBR. The image at 750 may end at 16384: 15,634 sectors, about 7.6 MiB, ten times 0.2.101. Why 16384:
- headroom: the image doubled between 0.2.25 (397,312 B) and 0.2.100 in two days of features; 8 MiB is ten times
  today's image and costs nothing on a card of 1 GB or more;
- it stays far below every other bound: the bootloader's DRAM (28 MiB, below) and our linker's
  `ASSERT(BSS_END <= 0x81A00000)` (image + 8.1 MB .bss must end 26 MiB above 0x80000000, so the image could reach
  about 17.9 MB before RAM runs out);
- one power of two in MiB that partitioning tools take directly (`parted ... 8MiB`, `diskpart offset=8192` KB).
- Considered: 8192 (4 MiB), the start the SD Association formatter uses on SDHC cards, so most cards would work
  without repartitioning. Rejected as the rule because it leaves only 3.8 MB and a card at 1 MiB still needs
  repartitioning; such a card still works today (the window ends at its partition; `make_sd_image.py` notes it).
The window still ends at the partition: the cap only bounds what a host may write, never past the partition. SD_INFO
check 3 (`pc_sd_egon`) is now "the length is invalid or the image ends past 16384" for each of 16/80/750 (was
"longer than 2048 sectors", PC_SD_VERIFY_MAX, removed), and the card identity (`pc_ident.c`, PC_ID_END = 16384, a
compile-time check ties it to PC_SD_LIMIT) refuses an image ending past it (state 5). A write past `write_end` answers
E_BAD_ARG 0 with a text built in a static buffer (`pc_sd_past_end_text`, SD_WRITE is main-loop only): "partition
starts at 2048: it must start at 2260 or later" (the write's last sector + 1), or "past sector 16383, the end of the
write window" when the cap ends it. The other checks and their order are unchanged: 16-79 need BOOT, 708-710 need
CONFIG, below 16 / past the card "outside the write window". Mock: `MOCK_SD_MAX_SECTORS` 20480 behind the 4096-sector
default card, reads and writes bounded by `mock.sd_sectors`.

**Bootloader: no size limit of its own** (inference from source and disassembly; the bench proves it for 1512).
- v0.7 source (newest published): `research/atlan4-extracted/updater 0.02l/updater 0.02l/fnirsi_1013d_startup/`
  `fnirsi_1013d_startup.c:348-400` `load_from_SDcard`: reads sector N, checks "eGON.EXE", length from +16, `blocks =
  (length + 511) / 512`, copies the first 480 bytes to 0x80000000, then one `sd_card_read(N + 1, blocks - 1,
  0x800001E0)`. `bl_sd_card_interface.c:311-381` `sd_card_read` checks only that the sectors are on the card; the
  controller's byte count is `blocks * 512` in 32 bits (`:961-962`); `sd_send_data` (`:1082-1098`) sets its poll
  budget to max(words / 64, 2000) x 100, so it grows with the transfer. Choice 0 loads 750, choice 3 loads 80
  (`fnirsi_1013d_startup.c:298-308`).
- v0.8 binary (Atlan's v1.00o5 package, sha256 eccd2f6e.., offset 0 = sector 16 = SRAM 0; disassembled with capstone
  under `python -I`): `load_from_SDcard` at 0x53A0 is the same code (eGON.EXE compare against the string at 0x7878,
  length bytes 16..19, `(length + 0x1FF) >> 9`, memcpy 0x1E0 bytes to 0x80000000, `sd_card_read` at 0x19E4 to
  0x800001E0, the "Failed reading program sector" screen with error and block count); the caller passes 0x2EE = 750
  (literal at 0x5388). `sd_card_read` at 0x19E4: only `sector + blocks - 1 > cardsectors`. The data loop at
  0x2C0C..0x2C80: words = blocks x 512 / 4, budget = max(words >> 6, 0x7D0) x 100, as v0.7. The bootloader's own
  variables are in DRAM at 0x81CBB800.. and sector 710 at 0x81BFFC00, so an image longer than about 28 MiB would
  overwrite them; 8 MiB is far below.
- **Risk to watch on the first boot (inference):** the poll budget is a total of polls while the FIFO is empty: 2.0 per
  word for the 776-sector 0.2.25 (budget floor 200,000 polls), a fixed 1.56 per word from 1000 sectors up (so larger
  images are no worse than 1512 sectors). The bootloader is built -O0; at 48 MHz 4-bit the card supplies a word in
  about 167 ns, so this should hold, but no image over 776 sectors has booted from a card on this unit yet. A
  time-out shows "SD card, Failed reading program sector", "Error: -2" (SD_ERROR_TIMEOUT) and "Sector: 1511" and
  hangs.
- Updater (sector 80, boot mode 3): it never loads the scope (`statemachine.c:423` commented out). Its "scope.bin to
  sector 750" (`research/atlan4-extracted/updater 0.02o .../fnirsi_1013d_updater/update.c:559-700`,
  `write_file_to_sdcard_with_progress`) writes the file one sector at a time with no size or partition check: on a card
  partitioned below the image's end it overwrites the FAT partition's start. The 16384 rule protects that path.
- The boot menu's "open firmware from sector 750" is choice 0 above; there is no second path.

**Tools and the MBR.** `make_sd_image.py write` never writes sector 0 (it reads the MBR, refuses GPT and a device
without an MBR), so the partition position is the user's job: the install guide's new "Prepare the card" section
(`parted ... mklabel msdos mkpart primary fat32 8MiB 100%`, `mkfs.vfat -F 32`, or `diskpart ... offset=8192`). The
tool refuses a part that runs into a partition with "scope (sectors 750..2261) runs into the FAT32 partition, which
starts at sector 2048. The partition must start at sector 2262 or later; cards for this firmware start it at 16384 (8
MiB). Copy the card's files off, ... run this again. Nothing was written.", notes a partition below 16384 that fits,
and `--create` makes an 8 MiB image. The region table bounds the scope at 16383. `make_release_sd_package.py`
produces a package from sector 16 (no MBR either); it refuses a scope ending past 16383 and prints the start the card
needs. Checked on scratch images (partition at 2048 refused, 8192 note + written, 16384 written) and with `--local` on
Atlan's zip (package 1,150,024 B, scope 750..2261, checksums OK).

**mcp.** `sdcard.image_fit_problem(info, sectors)`: None, or which limit: the partition (start, start needed, 16384,
how to fix), a running firmware with the old window (write_end < 16384 below the partition: "firmware 0.2.100 and
older stop at 2048 ... load 0.2.101 or later into RAM first"), or the cap. `fnirsi1013d-flash --persist` raises it
before any backup. `protocol.SD_WINDOW_LIMIT`; `SimCard(limit=...)` models old firmware; the simulator answers the
firmware's texts. Tests in `tests/test_sdcard.py` (1510 sectors at 8192 persisted, backup `sd_750-2260_*`; at 2048
refused, no backup; old window; cap and SD_INFO check 3).

**Bench (bench session; maintainer present; card in the scope, partition at 8192, boot mode 4 per docs/hardware-status.md)**
1. `make gcc10` on the merged main, note the sha256 `<h>` of `build-gcc10-pc/fnirsi_1013d.bin` (f2c53e7 built here:
   774,144 B; the merge changes the identity). `fnirsi1013d-flash --image <bin> --sha256 <h>`: `#VER fw=0.2.101`.
2. `fnirsi1013d-flash --persist-only --image <bin> --sha256 <h>`. Expected lines: "card ... sectors, partition at 8192,
   write window [16, 8192); image 774144 B = 1512 sectors at 750"; "sector 750 now holds eGON magic 2, length 397312"
   (0.2.25); "backup of sectors 750..2261 verified" (read twice, saved as `backups/sd_750-2262_<time>.bin`, sha256
   printed); write about 7 s at ~110 KiB/s, resends none; "SD_READ compare of all 1512 sectors ... identical"; "sector
   750 verified on the card: eGON.EXE, length 774144". (A 0.2.100-sized image backs up 750..2259.)
3. `fnirsi1013d-sd info --verify` (or `scope_sd_info`): write window [16, 8192), sector 750 eGON.EXE checksum ok,
   card image fw 0.2.101 = the running image. Diagnostics > Status, Firmware tile: OK "Running 0.2.101, the image the
   card starts."
4. Power cycle: boot menu, PECO preselected, the scope starts 0.2.101 from the card (`#VER fw=0.2.101` after it
   enumerates); Diagnostics Firmware tile OK as in 3; calibration and settings as before.
If step 4 shows "SD card, Failed reading program sector" (the time-out above): power off, on again, pick "Start FEL
mode" in the menu, FEL-load 0.2.101, and either report the error and sector numbers shown, or put 0.2.25 back with
`fnirsi1013d-flash --restore backups/sd_750-2262_<time>.bin`. Removing the card starts the stock firmware.
Bug indicators: a refusal on the 8192 card; a backup range other than 750..2261; SD_INFO check 3 on the new image; the
Firmware tile not OK after step 2.

## 68. fw-one: one bitstream, one Average (fw 0.2.103, protocol 1.25, row 65)

Branch `worktree-agent-aab3f2c1fc0fe37cd` (worktree) from main a02cc21 (fw 0.2.102). Brief: the two owner decisions of
2026-10-20 (21:52 "every firmware version only supports the latest FPGA bitstream ... detect this and show a loud banner";
21:59 "the ACQ Average is one behaviour ... consistency matters"). Commits: 6b02d8b (the constant, fixed caps, the
mismatch flag and wire fields, the removal of the compatibility code, host tests), 2891a3d (the notice screen), a014f16
(Average on both paths), then the mcp (fw-one-mcp) and the docs. **Nothing benched, nothing flashed**; the coordinator
numbers the version (`PC_FW_VERSION` is still 0.2.102 on the branch) and benches the image.

**What was built**
- `PC_FW_FPGA_VERSION` 0x153B (pc_cmds.h) is the one constant. `pc_variant_init` (was `pc_variant_from_version`) sets ring
  8192, max 16382, variant byte 2 and the fixed caps (PROG_TRIGGER_POS, GENERATOR_DDS, EXT_TRIGGER, DECIM, FIR, SEQ,
  ENC_CHECK, CAPTURE_STAGES, ENTER_FEL, SHARED_MODE, SD_SECTORS, SCREENSHOT, READ_LAG_UNVERIFIED; INTERLEAVE_UNVERIFIED
  cleared by `pc_variant_core_order` from the boot 0x33 as before) and `mismatch = version != PC_FW_FPGA_VERSION`.
  `pc_decim_has()` (FIR | CAPSTAGES) replaces `pc_decim_has_caps(caps)`; `pc_features` is a constant.
- Mismatch: GET_INFO 80 bytes (76 u16 built-for word, 78 u8 flags bit 0 `PC_FPGA_F_MISMATCH`), GET_HEALTH 128 (layout 3,
  124 / 126); the dispatch gate answers E_UNSUPPORTED detail `PC_UNSUP_FPGA_MISMATCH` (1) "FPGA bitstream mismatch: this
  firmware needs 0x153B" for everything without `F_BASIC` (GET_CAL and ENTER_FEL joined the basic set); the MAIN poll does
  not read 0x28; `pc_glue_init` skips `fpga_encode_init` / `fpga_capture_init` (no feature-register writes). The screen:
  `ud_build_fpga_mismatch` (ui_diag.c; texts in the row 65 entry and the CHANGELOG), `ui_fpga_mismatch_open` / `_pass`
  (ui_diag_view.c): a main-loop display mode after `ui_diag_pass`, three buttons (Diagnostics over it, Settings and USB
  drive as modal screens, redrawn after), the first-run screen skipped (`fnirsi_1013d_scope.c`). The Diagnostics FPGA line
  is one of two: "FPGA AL3A10 bitstream 0x153b: <the 0x33 line>" or amber "FPGA bitstream 0x1532: this firmware needs
  0x153b"; on a mismatch the pages drop Calibrate / DC shift trim / Volts calibration (Clear calibration, Save report,
  Enter FEL stay); the report says "firmware built for 0x153b: MISMATCH, the acquisition does not run".
- Removed (delete, not `#if 0`): `fpgasettings.fw_FPGA` and its users (fpga_control.c time base / arm, cal_wiz_ui.c
  `cw_init(.., 1)`, pc_hal_scope.c), `fpga_check_ready`, `fpga_conv_0e`; enc_heal: `eh_mode`, `eh_affected`,
  `eh_order_differs`, `pc_hal_enc_retry` (the 0x0D re-write), `PC_ENCF_ORDER`, `fpga_encode_init(void)`; cap_stage:
  `cs_has`; fpga_bus: `fpga_bus_contract_for` / `fpga_bus_init` / `fpga_bus` and `fpga_arm_cfg.geometry` (`FPGA_READ_DUMMY`
  1, the geometry always written early and inside the window); pc_decim: `PC_STOP_*`, `pc_stop_kind`, `pc_auto_stop_word`,
  `pc_auto_cut`, `pc_engine_record`; pc_cmds: `auto_geom` / `auto_stop` / `cap_spliced` / `arm_prog` / `pc_auto_net_ms` /
  `PC_CAPTURE_F_SPLICED` (bit 6 never set), the stock arms of SET_TRIGGER / GET_INFO / GET_GEN / pc_arm_geometry, the EXT
  / FIR / SEQ cap checks; pc_hal: `pc_hal_force_stop`, `pc_hal_arm`'s `prog`, `pc_hal_read_core`'s `read_lag`; pc_bode:
  `bh_geom` / `bh_forced` and `bode_cap.spliced`; pc_hal_scope.c: the fw 0.2.97 UI geometry (`pc_ui_geom` / `_rules` /
  `_spliced` / `_stop`), the fw_FPGA switch in `pc_glue_ui_read_start`, the cap tests in `pc_glue_ui_has` /
  `pc_dec_has` / `pc_dec_config` / `pc_ui_fix_settings`. Kept because 0x153B needs it: the 0x33 self-check with the data
  check after a rate write (`eh_capture_mode`) and after a bad boot convention, the capture stages, the dummy strobe, the
  early geometry (harmless on 0x153B and the one order).
- Average (pc_seq_hw.c, pc_seq.c): `pc_seq_ui_plan_n(per_record_ns)` is the one rule; `pc_seq_ui_plan` (hardware) and
  `pc_seq_ui_plan_mcu` (+ `PC_SEQ_UI_READ_BYTES` 4 x `PC_SEQ_UI_READ_NS` 300 per word) feed it. `pc_seq_ui_conversion`
  picks the path (hardware for a legacy record <= 2046 words, else MCU: `ui_mcu`), `pc_seq_ui_done(triggered, addr)` runs
  `ui_mcu_collect` for a triggered record 1: `pc_glue_decim_rearm` + `fpga_do_conversion` (`ui_rearm` keeps the plan) N -
  1 times, `fpga_done_conversion` polled with `PC_BUSY_POLL` until `ui_deadline` (max(100 ms, two records) + N records
  from record 1), lanes into `ui_sum[2][8192]` (uint32; legacy [2w] B / [2w + 1] A, 16-bit [w]) through `ui_mcu_add`, a
  late record discarded with `pc_hal_force` and the mean of what it holds shown; `pc_seq_ui_feed` converts the mean
  (`pc_seq_avg_code` per core, or `pc_seq_udiv` + `pc_decim_to_code` with `acal_scale(scale_q24, gain16)` / corr16).
  `pc_seq_sw_average`, `pc_seq_trace_replaced`, `PC_SW_AVERAGE`, `channel1tracebufferAVG` and friends are gone;
  scope_display_trace_data draws the record as it is. `pc_hal_ui_average`: state 0 off / 1 MCU mean / 2 hardware, log2n (0
  = single records). Memory: + 64 KB sums + 16 KB temp (DDR).
- Host tests: test_info (fixed caps, GET_INFO 80, GET_HEALTH 128 layout 3, six other words: flags, basic set OK,
  SET_REMOTE / GET_CHANNEL / GET_TRIGGER / GET_DECIM / ARM E_UNSUPPORTED detail 1, 0x28 never read); test_ui_diag
  `test_fpga_mismatch` (the notice's texts, colours, hit areas; a newer word; the pages without the measuring buttons);
  test_seq (`pc_seq_ui_plan_n`, `pc_seq_ui_plan_mcu` at 1 MSa/s 16-bit N 4, 200 MSa/s 6000 samples N 8, 16382 N 4, 10
  kSa/s 0); test_fpga_bus on the model's 0x1535..0x153B (the 0x153A cases show the fault 0x153B fixed); deleted:
  test_auto_stop_geometry, test_seq_older_bitstreams, test_contract, test_spliced_retake, the per-bitstream halves of
  test_enc_check / test_capture_stages / test_decim_* / test_ext_trigger / test_bode, test_enc_heal's `test_modes`
  version checks, test_cap_stage's `cs_has`. mock_hal: ring 8192, enc FLAG + boot33 good, cap HAS always, `pc_hal_arm`
  without `prog`, no `force_stops` / `enc_retries`. All `make -s test` green; GCC 10 builds (upstream warnings only).

**Decided and why**
- A newer word is a mismatch too: the next bitstream ships with its own firmware (brief).
- The MCU path's records 2..N are armed synchronously inside the pass (not spread over passes): the hardware run shows one
  mean per 40 ms, so the MCU path does too, and the budget already bounds the pass; the first record keeps the main
  loop's wait semantics (normal mode waits across passes, auto mode is forced at the deadline), so "no record yet" never
  blocks. Records 2..N in normal mode wait at most the deadline, then the mean of what is held is shown (the hardware
  run's rule for a run with at least one record).
- N = 1 (single records) when two records do not fit 40 ms, on either path, reported as state 1 / log2n 0: a running
  average across passes would again be a second behaviour.
- READ_RAW of a UI capture sees the last of the N records (the ring), as a hardware run leaves its last record.
- `PC_CAPTURE_F_SPLICED` is gone from the firmware; the protocol keeps bit 6 documented as never set (the mcp decodes it
  harmlessly).
- GET_CAL and ENTER_FEL are `F_BASIC`: a mismatched scope must still give its calibration to the report and a way out.

**Bench (coordinator; FEL load, nothing flashed; the unit runs 0x153B)**
1. `#VER` fw=<version>, proto=1.25, fpga=0x153b; GET_INFO 80 bytes, 76 = 0x153B, 78 = 0; GET_HEALTH 128, byte 0 = 3,
   126 = 0; caps as section 68 above (bit 12 clear after a good boot 0x33). Diagnostics Status: "FPGA AL3A10 bitstream
   0x153b: encode check ..." as before; the Calibration page's four buttons present.
2. Scope view at 200 MSa/s with Average on: GET_STATUS 46 = 2, 47 = 6; the trace as on 0.2.102.
3. Average on, Filter CIC2 (the Scope default), 1 MSa/s, 3000 samples, the 1 kHz square on CH1: GET_STATUS 46 = 1, 47 = 2
   (N 4); the trace visibly less noisy than with Average off; `pass_ms` about 30 ms more than with Average off; edges
   where they were. 10 MSa/s: 47 = 3 or 4. 10 kSa/s: 47 = 0 (single records, pass_ms as Average off). Filter off, 6000
   samples at 200 MSa/s: 46 = 1, 47 = 3.
4. Auto mode, probe unplugged, Average on at 1 MSa/s: the trace free-runs (single untriggered records, the UI responsive);
   plug the signal in: 47 stays 2 and the mean comes back. Normal mode, no signal: waits as before; with a signal that
   stops after one record: the pass ends within about 140 ms and shows one record's mean.
5. The mismatch screen (optional, needs 0x153A back in the flash, or any other bitstream): boot shows the notice with
   the two words; Diagnostics opens and Back returns to it; Settings and USB drive work and the notice comes back; over
   USB `s.info` shows fpga_mismatch, `s.capture()` fails with E_UNSUPPORTED detail 1, GET_HEALTH / READ_SCREEN / SAVE_REPORT
   work. Rollback to 0x153B as docs/hardware-status.md says.
Regression signs: 46 = 1 with 47 = 0 at 1 MSa/s (the plan not reaching the MCU path), a pass over 100 ms with a signal
present (the deadline wait instead of the record), a shifted edge between Average on / off (the mean built from a wrong
record start), GET_INFO 76 bytes.

**Cruft audit addendum (coordinator 23:29, docs/plans/fpga-0x153c.md section 13).** 0x53 bits 3 / 4 are written 0
(`PC_DECIM_FLAGS` = `PC_DECIM_SWAP`; the NEG defines, the settings-word bits and `pc_decim_config`'s per-bitstream mask are
gone; SET_DECIM accepts flags bits 1 / 2 and drops them). The 0x3C writer was only a commented-out `fpga_set_battery_level`
(and its commented call and prototype): deleted; no 0x41 reader exists in the tree (touchpanel.c's 0x3C / 0x41 are GT911
configuration bytes). Dividers: the UI table's largest is **399,999** (500 Sa/s, indices 17..28; the audit's 199,999 is the
short time bases' maximum), the Bode planner's largest is 524,287 (1 Hz), the auto-setup table smaller; all within the 20
bits the FPGA compares. `PC_DIVIDER_MAX` 0xFFFFF (pc_decim.h): SET_TIMEBASE mode 1 refuses more with E_BAD_ARG detail 4
(the one path that took any u32); test_session `test_timebase` asserts the bound, the refusal and the mock's copy of the
UI table, test_bode_sweep `test_divider_bound` the planner.

**Open**
- Queue item "0x153B+: the deadline force must write 0x0E = 0 too" (fpga-gen13): `pc_hal_force` is still 0x1A = 0 alone;
  `pc_hal_force_stop` was removed with the geometry, so the fix is now two lines in `pc_hal_force` (0x0E = 0 first) plus
  the test_fpga_bus model check. Not done here (not in the brief).
- `docs/guide/first-run.md` and `docs/guide/screens.md` (docs-one's) still describe the older behaviour; the texts for
  them are in this branch's report.
- mcp: `server.py` shows the mismatch caveat only once mcp-soft wires `Profile.mismatch_caveat()` in (fw-one-mcp's report).

## 69. fw-c5: trigger types and hold-off (fw 0.2.103 + C5, version provisional, protocol 1.26, row 66 provisional)

Branch `fw-c5` (worktree) from main 38e3aea (fw 0.2.103, protocol 1.25). Brief: item **C5** of `docs/plans/fpga-0x153c.md`
section 10 (firmware triggers + hold-off) against the merged RTL of fpga-c1 / fpga-c3 (`fpga/al3/src/zaklad.v`; semantics
and deviations in `fpga/al3/AGENTS.md` "0x153C C1 triggers"). Commits: the bus layer (fpga_bus, fpga_model, test_fpga_bus),
the session (pc_cmds, pc_hal, mock_hal, test_session), the vectors, the docs. **Nothing benched, nothing flashed**: the
unit runs 0x153B, which this firmware reports as a mismatch until C4's 0x153C bitstream is flashed; the coordinator
numbers the version (`PC_FW_VERSION` is still 0.2.103 on the branch) and benches the image.

**What was built**
- `PC_FW_FPGA_VERSION` 0x153C and `PC_FW_FPGA_VERSION_TEXT` "0x153C" (pc_cmds.h; the E_UNSUPPORTED mismatch text is built
  from it, no more literal). `PC_PROTO_MINOR` 26. Caps: `PC_CAP_HOLDOFF` (bit 2, now meaningful) and `PC_CAP_TRIG_TYPES`
  (bit 20) in the fixed word; features word `PC_FEAT_W_TRIG_TYPES` 0x0100 (bit 8: the contract's bit 5 is ENC_CHECK, 6
  BODE, 7 CAPTURE_STAGES since 1.19-1.24, so C6 PEAK takes 9 and C7 COUNTER 10, i.e. the contract's list shifted by
  three). Under the one-bitstream rule nothing is gated: the firmware assumes the registers exist.
- **fpga_bus.c / .h** (block "Trigger types and hold-off"): `fpga_trig_ext fpga_trig` (type, longer, n, h, holdoff) is the
  firmware's copy; `fpga_trig_write(t, clear)` writes 0x5E = type | longer << 3, then for type != edge 0x5F (u24 N) and
  0x60 (u24 H), then 0x1B (u32), then 0x66 = 1 when `clear`; `fpga_trig_arm()` writes 0x5E and (type != edge) 0x5F / 0x60
  and is called by `fpga_arm_reset` inside the 0x01 = 1 window after the second geometry write and before 0x0E, so both
  arms (UI `fpga_do_conversion`, host `pc_hal_arm`) carry it without any change of their own; `fpga_trig_status(out[4])`
  reads 0x37 then the 3 bytes of 0x34. `fpga_write_u24` is the 3-byte writer. 0x16 stays the UI's trigger edge
  (`fpga_set_trigger_edge`): the polarity per type as the FPGA defines it.
- **pc_hal.h**: `pc_hw_trig_ext` { type, flags, n, h, holdoff }, `PC_TRG_T_*` (0..3; RUNT 4 and WINDOW 5 named for the
  refusal), `PC_TRG_F_LONGER` 0x01 (RANGE 0x02, BOTH 0x04 named for the refusal), `PC_TRG_N_MAX` 0xFFFFFF, `PC_TRG_NTH_MAX`
  0xFFFF; `pc_hal_trig_ext_read` (a memory read, any context), `pc_hal_trig_ext_write` ([FPGA], always with the clear
  request), `pc_hal_trig_status` ([FPGA]). pc_hal_scope.c maps them onto fpga_trig; the settings fingerprint carries the
  extension (b[64]); `pc_glue_init` writes `fpga_trig` (zeros) with the clear request after `fpga_capture_init`, built-for
  word only.
- **pc_cmds.c**: `h_set_trigger2` (SET_TRIGGER2 0x0219, cap TRIG_TYPES, no F_MODAL: it writes the FPGA): validation in
  byte-offset order (type 0: runt / window "FPGA 0x153E", 6+ "type 0..3"; polarity 1; flags 2: RANGE / BOTH "0x153E",
  other bits, LONGER with a type other than pulse width; source 3; N 4: pulse width / timeout 1..16777215, Nth edge
  1..65535, edge 0; H 8: Nth edge 0..16777215, else 0; level 12: 0..255; level2 14: 0), then `pc_hal_trigger_write`
  (SOURCE | EDGE | LEVEL, edge = the polarity) and `pc_hal_trig_ext_write`; response `pc_trigger_body`. `h_set_trigger`:
  mask bit 6 writes the hold-off through `pc_hal_trig_ext_write` with the type kept (E_UNSUPPORTED detail 6 is gone).
  `pc_trigger_body` 52 bytes (12 = holdoff; 28..51 the SET_TRIGGER2 body: type, edge, flags, source, N, H, level, 0,
  holdoff, 0). `pc_status_body` 52 bytes (48 0x37, 49 0x34 byte 0, 50 u16 edges to count, from `s->trig_st`, read in
  `pc_acq_service` after `pc_hal_acq_poll` while `!done`, zeroed at ARM). `pc_capture_latch` takes the extension into
  `pc_capture` (trig_type / flags / n / h / holdoff), `h_get_capture_info` 112 bytes (96 type, 97 flags, 98 u16 0, 100 N,
  104 H, 108 holdoff). Constants `PC_STATUS_LEN` 52, `PC_TRIGGER_LEN` 52, `PC_CAPTURE_INFO_LEN` 112 (pc_cmds.h).
- **Host tests**: fpga_model models the latches (fm_len 0x5F / 0x60 = 3, 0x1B = 4), the 0x66 one-shot consumed by 0x01 = 1
  (`ho_clr_req`), the hold-off on the capture logic (`ho_until_ps` set at a taken trigger, `fm_holdoff_running()` =
  reg_1b && now < until, a masked trigger counted in `ho_blocked`), reads 0x34 (the qualifier's reset state: edges to
  count = N16 - 1, counting = H == 0) and 0x37; the 0x153C model joins `versions[]`; `fm_reset` zeroes `fpga_trig`.
  test_fpga_bus `test_trig_ext` (6 cases x 6 models: the write trace, the arm trace per type, the one-shot, the status
  reads; the hold-off at 1 ms / 200 us / 50 us: masked re-arm trigger, 0x66 + arm clears, 0x1B = 0 releases, runs out;
  0x153B unaffected). test_session `test_trigger2` (caps, features, GET_TRIGGER 52, every type's echo and mock state,
  23 refusals with their details and no write, SET_TRIGGER's hold-off and edge = polarity, settings_seq on a pure
  extension change, the status bytes of a capture in flight, GET_CAPTURE_INFO 112); test_trigger's hold-off case;
  test_info's caps and the `other[]` list (0x153B is a mismatch now); `setup(PC_FW_FPGA_VERSION)` everywhere (was
  0x153B literals); test_diag's report texts. `make -s test` green; GCC 10 build 38 warnings, all upstream and
  fw-one's two pc_seq_hw.c pointer warnings (none in the files touched here).
- **Vectors** (gen_vectors.py section 5f, `make vectors`): `req_set_trigger2` (pulse width shorter than 1500, negative,
  CH2, level 140, hold-off 200000), `req_set_trigger2_nth_edge_ext`, `_pulse_longer_max`, `_timeout`, `_edge`,
  `resp_get_trigger_1_26`, `resp_get_status_1_26`, `resp_get_capture_info_1_26`, `resp_set_trigger2_e_bad_arg_runt`;
  115 vectors, check_vectors OK, `python -m fnirsi1013d.vectors --check` (main venv) OK. The host-side (`host_`)
  vectors for the mcp structs are C8's (`--merge` once protocol.py has `SetTrigger2Req`).

**Decided and why**
- The hold-off and SET_TRIGGER2's `holdoff` are in **5 ns units** (the raw 0x1B value), not the contract 9.3's
  `holdoff_ns`: the brief's "the firmware converts nothing" wins, and GET_TRIGGER 12 (documented "raw") stays one unit.
  The mcp converts (C8).
- GET_TRIGGER appends the whole SET_TRIGGER2 body (source, level and hold-off duplicated) rather than only the new
  fields: one struct on the host round-trips the trigger, and the duplicates cost 8 bytes.
- The type registers are written both at SET time (like 0x15..0x17, so a UI conversion in RUN takes them at its next arm
  without a UI change) and inside every arm window (the contract 9.3's "as 0x57..0x5C", and the FEL case). 0x1B and
  0x66 are not part of the arm: the hold-off must survive re-arms, and the clear is a one-shot the FPGA consumes.
- Edge type with N or H != 0 is refused (fail fast; a host that switches back to edge sends zeros), LONGER only with
  pulse width, H only with Nth edge: no silently ignored fields.
- The 0x34 / 0x37 reads got a consumer (GET_STATUS 48..51 while a host capture is in flight) so the host can see "hold-off
  running" and "N edges still to count"; GET_TRIGGER stays F_MODAL (no FPGA read), so the status is not there.
- GET_CAPTURE_INFO carries the extension as armed (the contract's "if any"): the mcp's capture metadata would otherwise
  not know which trigger made the record.

**Deviations from the contract**: holdoff units (above); features bit 8 instead of 5 (taken); SET_TRIGGER2 body field
names `N` / `H` with `H` reserved for N2 on 0x153E as 9.3 says. Nothing else.

**Bench (coordinator; FEL load after C4's 0x153C is flashed, never without the backup)**
1. `#VER` proto=1.26, fpga=0x153c; GET_INFO 76 = 0x153C, 78 = 0, caps bits 2 and 20; GET_STATUS 52 bytes, 44 = 0x1FF;
   GET_TRIGGER 52 bytes with 28 = 0 (edge), 44 = 0. On the 0x153B unit before the flash: 78 = 1 and the notice screen
   "needs 0x153C" (the F_BASIC set answers).
2. Edge type unchanged: the 0x153B checks of section 68 repeat (the arm adds 2 strobes: 0x5E = 0).
3. Pulse width (contract 9.2 check 2): generator 10 kHz 20 % on the post to CH1 at 1 MSa/s (one word per us):
   `SET_TRIGGER2 type 1 longer N 15` triggers, `longer N 25` does not (auto mode records untriggered, flags bit 1),
   `shorter N 25` triggers on the falling edge: READ_RAW shows the trailing edge at the pretrigger. GET_STATUS 49 while
   armed reads type 1 (bit 2 = longer).
4. Nth edge: the generator gated by a host loop (`scope_generator` on / off), `type 2 N 3 H 100000` at 1 MSa/s (idle
   100 ms): the trigger sits on the third pulse after a gap; GET_STATUS 50 counts down 2, 1, 0 while armed.
5. Timeout: `type 3 N 2000` at 1 MSa/s with the generator on, then off: the capture triggers 2 ms after the last edge.
6. Hold-off (check 5): 1 kHz square, `holdoff 500000` (2.5 ms), segments of 4: timestamps 3 ms apart; `SET_TRIGGER
   holdoff 0` releases (segments 1 ms apart); GET_STATUS 48 bit 4 while armed right after a trigger.
7. SET_TRIGGER with mask 0x40 holdoff 200000 on an otherwise edge trigger: accepted, GET_TRIGGER 12 = 200000.
Regression signs: GET_STATUS 48 bytes or GET_TRIGGER 28 (the old bodies), a UI capture in RUN ignoring a host type
(the arm window write missing), a trigger taken inside the hold-off after an ARM without a new SET_TRIGGER2 (0x66
written per arm), E_UNSUPPORTED detail 6 on SET_TRIGGER's hold-off.

**Open**
- The UI Type row (docs/plans/ui-rows.md): drives `fpga_trig_write` with the menu's time converted to samples; the
  menu's trigger edge is the polarity for every type already.
- C6 (peak) and C7 (counter) share `pc_cmds.c`'s dispatch table and `fpga_model.c`'s `fm_apply` / `fm_len` / read switch:
  append entries; GET_STATUS and GET_CAPTURE_INFO grow by appending after 52 / 112.
- mcp (C8): `SetTrigger2Req` / the 52-byte `Trigger`, `scope_set_trigger(type=..., width=..., n=..., idle=..., holdoff=...)`
  converting time to samples from the capture's word period, the simulator's qualifier model, `host_` vectors.
- Nothing reads 0x34 / 0x37 during a UI capture (the main loop's own conversions): the status bytes describe host
  captures only.

## 70. fw-c6: peak detect (fw 0.2.103 + C5 + C6, version provisional, protocol 1.26, row 66 "Peak detect")

Branch `fw-c6` (worktree) from main c432354 (fw-c5 merged), origin/main 1635a1a (mcp-c8, ui-type) merged before the final
tests. Brief: item **C6** of `docs/plans/fpga-0x153c.md` section 10 (firmware peak detect) against the contract's register
semantics (sections 2.1, 2.2, 4) and fpga-c2's handoff (`fpga/al3/AGENTS.md` "0x153C C2 peak detect": 0x56 bit 1 alone selects
the 16-bit path, 0x29 byte 1 = { peak_act, peak_en, shift }, settle as a filter write), with the host's wire format fixed by
the coordinator (06:2x) to match mcp-c8 byte for byte. Commits: 0291abf (pc_decim, fpga_bus, fpga_model, test_fpga_bus),
22861f9 (pc_seq fit), 0134049 (pc_cmds, pc_hal_scope, pc_seq_hw, fpga_control, mock, test_session), d16129e (vectors), then
the docs. **Nothing benched, nothing flashed**: the unit runs 0x153B, the firmware needs C4's 0x153C bitstream; the
coordinator numbers the version (`PC_FW_VERSION` is still 0.2.103 on the branch).

**What was built**
- `pc_decim.h/.c`: `PC_DECIM_SET_PEAK` 0x40 in the setting byte (`PC_DECIM_SET_MASK` 0x7F), `pc_decim_cfg.peak`,
  `pc_decim_wide()` (k != 0 or peak), `pc_decim_shift_byte()` (S | 0xC0 for a peak record: `PC_DECIM_PEAK_ACT` /
  `_PEAK_EN` as the FPGA's 0x29 byte 1), `pc_decim_cmd56()` (bit 0 FIR, bit 1 peak). `pc_decim_config` with the peak bit:
  the same divider 0 / total > 8190 fallbacks as the filter (why 2 / 3, legacy, the FIR request dropped too), else
  `peak` 1, `k` 1, `n` the window, gain 1, shift 0, scale 0, `fir_en` 0, why 0; the status byte 0x81. The peak maths:
  `pc_peak_gain` / `_off` (the mean of the two cores' Q16 gain / offset), `pc_peak_code`, `pc_peak_mid` (the corrected
  midpoint, rounded once), `pc_peak_level` (the inverse for 0x55), `pc_peak_overload` (min 0 -> 0x01, max 255 -> 0xFE).
  `pc_decim_rate` / `_ext_shift` / `_ext_shift_at` / `_settle_us` take `pc_decim_wide`; the EXT lag of a peak record is
  the K = 1 boxcar's, the settle (1 + 10) N x 5 ns + 1 us.
- `fpga_bus.c/.h`: `fpga_decim_write(mode, has_fir, cmd56, shift)` takes the whole 0x56 byte (was the FIR flag); `fpga_bus_wr.fir`
  holds it. With peak: 0x53 = 1 | flags, 0x54 = 0, 0x56 = 2, each where it differs from what the FPGA holds.
- `pc_hal_scope.c`: `pc_dec_peak` (one Scope setting, not per mode: `pc_dec_ui_set()` adds the bit in the Scope view only,
  the Spectrum acquires Normal; not saved: the UI row item puts it in word 211 bit 3; `pc_glue_settings_defaults` clears
  it), `pc_hal_decim_setting` / `_set` carry it, `pc_dec_apply` writes 0x55 = `pc_peak_level(level) << 8` for CH1 / CH2
  (level << 8 on EXT), `pc_glue_decim_read` converts { max, min } words with the **full-class mean-core correction**
  (`fpga_channel_correction_full` in fpga_control.c: `acal_correction` at divider 0 plus `acal_live_apply`) into the
  midpoint feed, sets `ADoverload` by the either-byte rule, keeps the pairs in `pc_dec_raw` (READ_TRACE WIDE, the UI's bars).
  `pc_seq_hw.c` `pc_seq_ui_conversion`: no Average while peak (state 0).
- `pc_seq.h/.c`: `pc_seq_fit_rec(c, pre, total, wide, peak)` = `pc_seq_fit` plus `PC_SEQ_FIT_PEAK` 9 for averaging of a
  peak record; the three pc_cmds.c callers use it (SET_SEQ E_STATE 7 "peak detect: averaging refused (SET_DECIM filter
  first)", GET_SEQ 16 = 9, ARM E_STATE 3).
- `pc_cmds.c`: caps `PC_CAP_PEAK_DETECT` (bit 21), features `PC_FEAT_W_PEAK` 0x0200 (bit 9; C7's counter is 0x0400);
  `pc_decim_next` passes the peak bit; `h_set_decim` filter 4 (keeps the Filter's K / FIR from `pc_hal_decim_setting`,
  flags bits 0-2 applied, bit 3 E_BAD_ARG 1 "FIR with peak detect", filter 5+ E_BAD_ARG 0); `pc_decim_body` 0 = 4 while
  peak (byte 1's FIR bit masked), 3 / 25 the shift byte, 2 / 24 the status; `pc_capture_latch` stores the shift byte;
  `h_get_capture_info` 79 = (16-bit, also single captures) | peak << 1; `h_read_raw` layout 3 (per channel the core A lane
  = minima then the core B lane = maxima; `pc_raw_peak` judges the capture in flight while armed, else the latest; layout 2
  on a peak record and layout 3 on any other E_BAD_ARG 2); `h_set_trigger`'s total rule counts peak as 16-bit. GET_DECIM stays
  36 bytes and GET_CAPTURE_INFO 112 (the host's structs).
- Host tests: `fpga_model.c` (0x56 bit 1 on 0x153C, `fm_dec_on` / `fm_peak_on` / `fm_window`, the word period N x 5 ns on
  the 16-bit path, read 0x29 with the peak bits and the FIR-off rule, `fm_raw_sample` / `fm_peak_word`: synthetic windows
  with 255 spikes every 1000 and 0 dips every 1777 samples written as { max, min } lanes), `test_fpga_bus` `test_peak`,
  `test_session` `test_peak_math` / `test_peak_protocol` (the old `filter 4 = E_BAD_ARG` check is now filter 5, the flags
  0xE3 case is 0xA3: bit 6 means peak; features 0x3FF), `test_seq` the fit, `mock_hal` (peak trace = midpoint with the
  1.00o5 compensations' mean, the pairs in trace16, the overload rule, the shift byte in `decim_rb`, the setting mask 0x7F).
  `make -s test` green after the merge of 1635a1a (test_ui_diag's four 0x153b-text failures seen on c432354 are gone on
  main). GCC 10 build 38 warnings, all upstream and fw-one's two pc_seq_hw.c pointer warnings.
- Vectors: `gen_vectors.py` 5g (`resp_set_decim_e_bad_arg_fir_peak`, `resp_set_seq_e_state_peak`,
  `resp_read_raw_e_bad_arg_peak_layout2`) and the `decim_peak` maths section (41 configuration cases, 5 corrections) checked by
  `decim_check --peak` / `--peak-corr`; mcp-c8's `host_*_peak` vectors kept; 134 vectors, `check_vectors` OK.

**Decided and why**
- **The host's bytes win over the contract's wording where they differed** (coordinator 06:2x, mcp-c8 merged first): the
  peak flags live in the shift byte (bits 7 / 6, mirroring 0x29 byte 1), not in a mode-byte bit; the mode byte reads 0x81 (K
  1); GET_DECIM / GET_CAPTURE_INFO keep their lengths; `why` is 0 for an active peak record (the mcp's why table has 0..6).
- **0x53 = 1 is written** under peak (the contract section 4's "the firmware writes 0x53 K = 1 and 0x56 bit 1"), although
  the one-register switch would not need it: so the FPGA's 0x29 byte 0 bits 1..0 read the K the firmware reports (0x81 on
  both sides). "Leaves 0x53 / 0x54 as they are (the way back)" of the brief is read as the *setting*: the Filter's K / FIR
  stay in the setting byte and come back at the next Normal arm; the registers follow every arm anyway.
- **Full clock class for the mean-core correction**: the ADCs run at 200 MSa/s under peak detect (dec_on, like the filter
  path), so the gated-class correction the divider would select is the wrong one; `fpga_channel_correction_full`. The
  contract's "GET_CAL bytes 92..107" are those values at divider 0; at D > 0 GET_CAL reports the gated class, a gap for the
  mcp's volts conversion of a code or so (noted below).
- **The processed trace is the corrected midpoint** (the brief: "the midpoint, with min / max only in the raw layout"): one
  trace for the screen, the measurements and the software trigger; READ_TRACE WIDE and READ_RAW layout 3 give the pairs; the
  UI item draws the bars from `pc_dec_raw`.
- **Divider 0 and long records stay legacy with peak requested** (the filter's rules): at 200 MSa/s every sample is stored
  and the 16-bit path would only halve the word rate; the setting is kept and GET_DECIM says why (2 / 3). The FPGA's bit alone
  would run peak at D 0 (N 2, 100 MW/s); the firmware does not write it then (the shift byte reads 0x00, not 0x40: the mcp's
  simulator models "written but not active" at D 0, a cosmetic difference).
- **SET_DECIM filter 4 keeps the Filter** (ui-rows.md 3.2: greyed, not merged): the host turns peak off by sending the filter
  it wants (GET_DECIM does not report the kept K: 36 bytes).
- **GET_CAPTURE_INFO 79 bit 0 for single captures too** (mcp-c8's vector 0x03): row 56 had it for runs only.

**Deviations from the contract**: the peak flags in the shift byte instead of the contract's "GET_CAPTURE_INFO byte 79 bit 1"
alone (79 bit 1 is set too); why 0 rather than a new code; the correction class (full, above); nothing else. From the brief:
0x53 written as 1 (above).

**Bench (coordinator; FEL load after C4's 0x153C is flashed, never without the backup)**: `firmware/README.md` "Peak detect"
bench list. Regression signs: GET_DECIM 2 = 0x81 with 3 = 0x00 (the shift byte without the peak bits: the FPGA did not take
0x56 bit 1, or an older bitstream), layout 3 refused after a peak arm (`pc_raw_peak` reading the wrong capture), a trace at
the midpoint while READ_RAW layout 3 shows pairs that do not bracket it (the correction class), Average running with peak
(GET_STATUS 46 != 0).

**Open**
- The ACQ Sample row and the Scope view's bars (`docs/plans/ui-rows.md` section 3): `pc_hal_decim_set(setting | PC_DECIM_SET_PEAK)`
  or a direct `pc_dec_peak` toggle, word 211 bit 3, the Filter / Average boxes greyed, the bars from `pc_hal_trace16`.
- mcp (C8 follow-up): the volts conversion of peak bytes needs the full-class correction (GET_CAL at D > 0 gives the gated
  one); the simulator's "peak written but not active at divider 0" differs from the firmware (0x56 bit 1 not written there).
- GET_DECIM does not report the Filter kept behind Sample Peak; a host restores it by value.
- pc_seq_hw.c's two upstream pointer warnings (fw-one) are still there.

## 71. fw-c7: counter (fw 0.2.103 + C7, version provisional, protocol 1.26 row 66 addition, provisional)

Branch `fw-c7` (worktree) from main c432354 (fw-c5 merged), origin/main 1635a1a (mcp-c8, ui-type) merged in. Brief: item
**C7** of `docs/plans/fpga-0x153c.md` section 10 against fpga-c3's merged RTL (`fpga/al3/AGENTS.md` "0x153C C3 counter");
the wire format fixed by the coordinator at 06:2x to the mcp's (mcp-c8) bytes. Commits: the counter module with the bus
model and its test; GET_COUNTER, the cards and the host tests; the docs. **Nothing benched, nothing flashed**: the unit
runs 0x153B; the coordinator numbers the version (`PC_FW_VERSION` is still 0.2.103 on the branch).

**What was built**
- **fpga_counter.c / .h** (new, host-buildable against fpga_bus.h; built into the scope by `SRCS += fpga_counter.c`):
  `fpga_cnt` is the one copy. `fpga_counter_boot` writes 0x62 = 128 / 6 then 0x61 = gate 100 ms (2), source 0 (5
  strobes; `pc_glue_init` after `fpga_trig_write`, built-for word only, with `ui_ms_counter_register`).
  `fpga_counter_pass(now_ms, trig_level)` runs once per main-loop pass after the capture readout (`PC_COUNTER_PASS()` in
  fnirsi_1013d_scope.c right after the `scope_acquire_trace_data` branch, `pc_glue_counter_pass` in pc_hal_scope.c): the
  0x35 read (command + 16 strobes = **17 strobes, about 5 us**), a snapshot with "new" taken into the state (seq, source
  from the EXT / CH2 bits, gate, E, T, W, min, max, the pass clock) and derived by `fpga_counter_derive` (64-bit integer
  arithmetic: frequency in uHz by a two-stage division since E x 2e14 overflows, period / high / low time in ps, duty in
  ppm, all rounded to nearest; E = 0 = no period). `fpga_counter_current(now)`: a result is current while the gate is on
  and it is younger than three configured gates + 100 ms. `fpga_counter_config` only notes a host configuration; the
  pass writes it.
- **The rate-change rule** (fpga-c3's note): `fpga_bus_wr.wclk` counts the 0x0D and 0x53 writes (`fpga_bus_note_write`;
  0x54 / 0x55 / 0x56 do not move the write clock). A pass that sees it moved writes **0x61 = 0 before its read** (the
  running gate is aborted: nothing can be published after this), reads 0x35 and **drops the result it carries**, then
  writes **0x61 = the value** (a value change restarts the gate): the next result is one clean full gate; 4 strobes on
  such passes only, and the pass is milliseconds past the write (the transient lasts at most 5 x 41 us). Chosen over
  "skip the first result by seq" because a clean result published just before the write and read after it would be the
  one skipped while the contaminated gate's became the second (a 15..40 % chance per change with 100 ms gates and
  15..40 ms passes). The settle wait of the brief is implicit: the pass is after the readout.
- **Level**: follows the UI trigger level (`scopesettings.triggerlevel` clamped 0..255) with hysteresis 6; a change
  writes 0x62 (3 strobes) and restarts the gate the same way (0x61 = 0 / value), so no result mixes two levels. A host
  `what = 1` with level != 0 pins the thresholds; level 0 returns to following (`cfg_level` 0). The trigger source needs
  nothing: the FPGA's source 0 follows 0x15 and restarts its gate by itself; the result carries its source bits.
- **GET_COUNTER 0x0213** (pc_cmds.c, block "C7 counter"; `F_MODAL`, cap `PC_CAP_COUNTER` bit 22; features
  `PC_FEAT_W_COUNTER` 0x0400): request u8 what (0 read, 1 configure then read), u8 gate, u8 source, u8 level, u8
  hysteresis, 3 x 0 (an empty request reads; `what` = 1 with fewer than 5 bytes is E_BAD_ARG detail 0); E_BAD_ARG detail
  0 what > 1, 1 gate > 3, 2 source > 3, 4 hysteresis > a non-zero level; nothing handed to the HAL then. Response 24 bytes:
  0 flags (bit 0 NEW: the result's seq differs from the previous GET_COUNTER's, `s->cnt_seen` / `cnt_seen_seq`; bit 1
  VALID: held, current, E >= 1; bit 2 EXT, bit 3 CH2: the result's source), 1 seq, 2 gate and 3 source as configured, 4 E,
  8 T, 12 W, 16 min, 17 max, 18 level and 19 hysteresis in effect, 20 freq_mHz = round(E x 2e11 / T) saturating, 0 when
  not VALID (`pc_counter_freq_mhz`, two-stage). The handler never touches the FPGA (pc_hal.h `pc_hw_counter`,
  `pc_hal_counter_read` / `pc_hal_counter_config`, both memory only), so it is answered inside menus; a configuration
  from a menu applies when the menu closes (the next pass). Byte-exact with the mcp's `host_resp_get_counter`
  (test_session asserts the 24 bytes).
- **The cards** (ui_meas.c "C7 counter"; the rule in ui_meas.h and firmware/README.md "Hardware counter"): `ui_ms_source`
  now returns int (1 = text written, 0 = use the record; scope_functions.c falls back). `ui_ms_counter_set` takes the
  pass's copy (valid = current and has a period and not EXT, the channel, mHz / ps / ppm); the six timing items Freq,
  Period, +Width, -Width, +Duty, -Duty are registered as sources (`ui_ms_counter_register`) and answer through
  `ui_ms_counter_text`: the counter's value for the result's channel, 0 for everything else. Formats: `ui_fmt_mhz`,
  the new `ui_fmt_ps` (ui_fmt.c), `ui_fmt_sig4_fixed` for the duty in hundredths of a percent: the record's four
  significant digits, so the card text keeps its shape. `UI_MEAS_PERIOD .. UI_MEAS_NDUTY` (7..11) joined ui_fmt.h.
- **fpga_model.c**: 0x61 / 0x62 / 0x15 latches (fm_len 0x62 = 2), the gate on a synthetic square wave per source
  (`fm_cnt_signal`: period, high time, phase as multiples of 5 ns, low / high codes; edges only when the Schmitt thresholds
  are crossed), publish at every gate end (E = edges - 1, T = E x period, W = E x high time in clocks, min / max, seq++),
  the restart on a changed effective configuration (0x61 value or 0x15 with source 0; a same-value write does not), the
  snapshot at the 0x35 command byte with "new" = seq changed since the previous snapshot, 255 / 0 before the first gate
  and on EXT. The mixed-age transient is not modelled (the firmware's rule is what the test checks). `fm_reset` zeroes
  `fpga_cnt`. Versions below 0x153C ignore the registers and read zeros.
- **Host tests**: test_fpga_bus `test_counter_derive` (0 edges, 100 MHz = two clocks, 30 MHz over 1 s, thirds rounded,
  1.000000005 Hz, the 28-bit maxima, W > T clamped) and `test_counter` (the boot trace and 5 strobes, the 17-strobe pass
  with no write, 1 MHz 50 % on CH1 at the 100 ms gate exact, the same gate not re-taken, the latest of several gates, the
  0x0D case: 0x61 = 0 at trace index 0 and 0x61 = 2 at 1 around the dropped read, the gate restarted and the next result
  clean, 0x53 counts and 0x54 / 0x56 do not, the level follow's 0x62 / 0x61 / 0x61 trace, E = 0 under the thresholds, a
  host configuration 1 s / CH2 / 100 / 10 applied by the next pass with 1 kHz 20 % exact, EXT 10 MHz at 10 ms with 255
  / 0, source 0 following 0x15 = 1 with no firmware write, a same-value 0x61 not restarting, gate off and on, the current
  bounds, the 0x153B model); test_session `test_counter` (above); test_ui `test_counter_cards` (`ui_fmt_ps`, the rule for
  both channels and every item, the duty rounding, the registered sources). test_session's features word is 0x5FF now
  (bit 10; C6 adds bit 9). test_ui_diag's mismatch texts were literal "0x153b" since fw-c5 moved the constant (a
  pre-existing failure of `make test`); ui-type fixed them in parallel and the merge took its version.
  `make -s test` green (test_fpga_bus 8834 checks, test_session 72435, test_ui 1434375, test_ui_diag 12996; check_vectors
  OK against 137 vectors, the mcp's 40 `host_` ones kept by gen_vectors). GCC 10 build: no warnings in the files touched.
- **Vectors** (gen_vectors.py: GET_COUNTER in CMDS, the one-per-command `req_get_counter`, section 5g):
  `req_get_counter_follow_trigger`, `req_get_counter_ext_10ms`, `resp_get_counter_1_26_ch2`,
  `resp_get_counter_1_26_no_edges`, `resp_get_counter_e_bad_arg_hyst`; 137 vectors.

**Decided and why**
- The host's level 0 means "follow the UI trigger level" (a Schmitt level of 0 is meaningless: everything is high), and
  the hysteresis > level refusal applies to a non-zero level only. The mcp's simulator refuses hysteresis > level for
  level 0 too (`_cmd_get_counter`): C8 should accept level 0 with any hysteresis as the firmware does.
- The level follows the trigger marker rather than staying at 128 (contract section 5): an off-centre logic signal
  would otherwise never cross the thresholds and the cards would never use the counter; the marker is where the scope
  triggers, so what triggers is what is counted.
- VALID folds "current" in (gate on, younger than three gates + 100 ms): a stale result after a gate-off or a menu that
  held the main loop for seconds must not read as live. The mcp's `valid` property is a subset check (E >= 1, T > 0) and
  stays compatible.
- -Width and -Duty come from the same result as +Width and +Duty (low time = (T - W) x 5 ns / E, duty' = 1 - duty): a
  card pair from two sources would disagree in the last digit.
- Vmin / Vmax / Vpp stay record-based (the brief's card list; converting the counter's codes needs the channel's
  calibration in scope_functions.c, not host-buildable). min / max are in GET_COUNTER for the host. Deviation from the
  contract's section 5, stated in protocol.md row 66.
- GET_COUNTER is F_MODAL and memory only, with the configuration applied by the next pass, rather than a bus write in
  the handler: a pure read must not close the user's menu (fw-eng6's rule), and one writer (the pass) keeps the restart
  logic in one place.
- The cards' copy is made by the pass; the cards draw it from the next pass (the readout, which draws the cards, runs
  before the pass hook): at most one pass of lag against a 100 ms gate.

**Bench (coordinator; FEL load after C4's 0x153C is flashed, never without the backup; contract 9.2 item 7)**
1. `#VER` proto=1.26; GET_INFO caps bit 22; GET_STATUS 44 bit 10. GET_COUNTER (empty request) right after boot: 24 bytes,
   flags 0 or 2 / 3 once the first 100 ms gate ended, gate 2, source 0, level = the trigger level (18), 6 (19).
2. The post at 1 kHz on CH1, the trigger marker inside the signal: GET_COUNTER flags 3 then 2, E about 100, T = E x
   200000 +- 1, W about T / 2, freq_mHz 1000000 +- 1 (within 1e-5 of phase_inc x 50 MHz / 2^32), min / max equal to the
   record's; the Freq card reads "1.000kHz", Period "1.000ms", +Duty "50.00%", and the CH2 cards keep "no period".
   At 1 MHz the same with freq_mHz 1000000000 +- 10; the cards "1.000MHz" / "1.000us".
3. Move the trigger marker: 18 follows within a pass; move it above the signal: E = 0, VALID clear, the cards fall back to
   the record's values (same numbers, from the record). Trigger source CH2 in the menu: flags bit 3 after the next gate.
4. Time base change (0x0D): no result with E off by one: read GET_COUNTER every 100 ms across a change; every VALID
   freq_mHz stays within 1e-5 (the first result after the change may come up to 200 ms later).
5. `what` = 1 gate 3 source 3 (EXT, the generator on): 1 s results, freq within 1e-5, min 255 max 0; gate 0: VALID clear
   after 100 ms, cards on the record. `what` = 1 level 0 hysteresis 6 gate 2 source 0 restores the boot behaviour.
6. GET_COUNTER inside the trigger menu: answered, the menu stays open; `pass_ms` with the counter on within 1 ms of fw-c5's.
Regression signs: `pass_ms` up by more than 1 ms (a read per pass costs 5 us), a card flickering between two values at a
steady signal (the rule's channel test failing), E off by one after time base changes (the restart order broken),
GET_COUNTER closing a menu (F_MODAL lost in a merge).

**Open**
- C6 (peak detect) shares pc_cmds.c's dispatch table (`//C7 counter` entry after SET_TRIGGER2), the caps / features
  lines and fpga_model.c's `fm_apply` / `fm_len` / read switch: append; the features word in test_session is 0x5FF with
  bit 9 still clear.
- mcp (C8): level 0 semantics (above); `scope_counter` could surface VALID's "current" meaning in a caveat.
- The counter's min / max could feed Vmin / Vmax / Vpp through a source in scope_functions.c (the contract's list) once
  the 8-bit per-core conversion is wanted there.
- The pass hook runs in the long (roll) time base too (harmless reads); the cards are not shown there.
## 72. ui-type: the trigger Type row (fw 0.2.103 + C5 + ui-type, version provisional, protocol 1.26 unchanged)

Branch `ui-type` (worktree) from main c432354 (fw-c5 merged). Brief: the trigger-menu part of `docs/plans/ui-rows.md`
(section 2, the checklist items 3-7 of section 5; the ACQ Sample row is another item). Mockups `docs/mockups/ui-rows/
TrigMenu*.png`, `ScopePulse.png`. Commits: e3352c6 (trig_ui, the pure module and its test), d67c852 (the notice queue
and the hint), 70635b7 (the firmware: menu, touch, glue, settings, Spectrum hint, the sweep's edge arm), 84c6e2a
(test_ui_diag's 0x153B literals), then the docs. **Nothing benched**: the coordinator numbers the version and benches.

**What was built**
- **trig_ui.c/.h** (pure, `host_tests/test_trig_ui.c`, 340 checks): the state `trig_ui` { type, longer, width_i, idle_i,
  n }; the ladder 28 steps 10ns .. 10s as times in 5 ns units (`tui_step_t5`); the word period `tui_tw5(divider, wide,
  ext)` = 2 (D + 1) legacy, 2 (floor(D / 2) + 1) on the 16-bit path, 1 on EXT (all 5 ns units, so 10 s and the largest
  period fit 32 bits and `tui_words` is one 32-bit division, round half up); `tui_range` (the contiguous steps a rate can
  arm: Width words >= 2, Idle >= 1, both <= 2^24 - 1), `tui_rate` (the chosen type's parameter clamped into it, returns
  what moved), `tui_nearest` (a host value as a step), `tui_hw` (the registers per type), `tui_from_hw` (the host's
  write into the state), the steps (`tui_step_width` / `_idle` with Off below the ladder / `_n`), `tui_set_n`, the
  settings words (`tui_word0` 0xA000 | type | longer << 2 | width_i << 3 | idle_i << 8, `tui_word1` N, `tui_restore` with
  field checks), the texts (`tui_step_text` "10ns" .. "10s", `tui_idle_text` "Off", `tui_n_text`), the glyphs
  (`tui_glyph_rects`: 2 px strokes as fill rectangles in a 14 x 14 field). Defaults Edge, Shorter, Width 2us, N 2, Idle
  50us.
- **acq_view.c**: two notice slots (`acq_view_notice_queue(slot, ...)`): the time/div's (0) shows before the trigger's
  (1), each for `ACQ_NOTICE_MS`; a new notice replaces its slot's, a time/div notice drops a pending trigger notice.
  **ui_ctrl.c** `ui_no_trigger_hint(type, idle_on)` and `ui_warn_in.trig_type / trig_idle`.
- **pc_hal_scope.c** (block "The trigger menu's Type row"): `pc_glue_trig_tw5()` (the divider the FPGA holds, the mode's
  Filter setting through `pc_dec_config`, EXT from the channel row); `pc_trig_rate(tw5, notice)` derives fpga_trig's
  n / h from the ladder step when the word period changed since the last derivation (`pc_trig_tw5_last`), snaps with
  the notice; called at every UI arm (`pc_glue_decim_conversion`, before the sampling reset, so `fpga_trig_arm` writes
  the words inside the window) and when the menu opens (`pc_glue_trig_rate_check`, not in roll mode). A UI edit goes
  through `pc_trig_write` -> `fpga_trig_write(&x, 1)` (0x1B kept, 0x66 = 1 as SET_TRIGGER2) and `pc_glue_trig_ui_apply`
  sets `display_data_done` in RUN so the next pass re-arms. `pc_hal_trig_ext_write` (the host) syncs the state with
  `tui_from_hw`: the shared screen rule both ways; the host's exact words stay armed until the next derivation. Power-on:
  `pc_ui_fix_settings` restores words 202 / 203 and `pc_trig_restore_apply` snaps silently and writes the registers on
  the built-for word; `pc_glue_settings_defaults` resets to Edge (and arms when `pc_ready`). The fingerprint already
  carried fpga_trig, so settings_seq follows UI edits. `pc_glue_trig_params()` = the parameter rows are shown (Pulse /
  Nth, not in Frequency response).
- **menu.c**: `scope_trigger_menu_height()` (336 + 94 with the rows + 24 in Frequency response; `TRIGGER_MENU_HEIGHT` is
  it, `TRIGGER_MENU_SAVE_H` 432 is what the statemachine saves and restores under the menu, the type changing the height
  inside it); `scope_draw_trigger_menu()` (the drawing, reused in place after a type change; `scope_open_trigger_menu`
  slides it); `trig_box` (the 22 px choice box), `trig_row` (the fold-out's 44 px value row at x + 8, 204 wide: keys 30 x
  36 at the ends, label at + 40, value right-aligned at - 40 in PALEYELLOW, the N value box 72 x 30 outlined; without
  keys the two-state row); `scope_trigger_type_select` (Edge 65 / 44, Pulse 113 / 50, Nth 167 / 40 at y + 297, the
  chosen in TRIGGER_COLOR), `scope_trigger_param_select` (rows at y + 334 and + 380), `scope_trigger_edge_select`
  (Positive 66 / 62, Negative 147 / 62 under Pulse; Rising / Falling as 1.00o5 placed them), `scope_trigger_symbol(x, y,
  color)` (the 14 x 14 slot at x + 65, y + 19 filled in the colour set, the glyph in `color`). The keypad
  (`scope_open_keyboard_menu`) titles "TRIGGER  Set N" with MIN 1 / Default 2 / MAX 65535 and greys the prefix and sign
  keys when `trignmenuopen`.
- **statemachine.c**: the Type row at y + 283 .. 332 (boxes as drawn); the parameter rows at + 334 .. 378 and + 380 ..
  424 (halves at x + 110; N's value box x + 100 .. 172 opens `handle_trigger_n_keypad`, nested as the generator's: the
  screen under it in displaybuffer2, the number applied on OK, the current value on a touch outside or a host request,
  closed on every way out by `close_open_trigger_menus(0)`); a type change restores the saved rect and redraws the
  menu in place; the edge row's hit boxes widened to the polarity boxes (66 .. 128, 147 .. 209).
- **scope_functions.c**: words 202 / 203 written; **spectrum.c**: the hint from `ui_no_trigger_hint(fpga_trig.type,
  fpga_trig.h != 0)`; **pc_bode.c** `bh_arm`: fpga_trig.type swapped to edge around `pc_hal_arm` (restored after).
- Host tests: test_trig_ui (above), test_acq_view `test_notice_queue`, test_ui `test_warnings` (the hints, 240 px in
  font_3 / 209 in font_2 against the 420 px box); test_ui_diag fixed for 0x153C (fw-c5 left four 0x153B literals, CI
  was red). `make -s test` green; GCC 10 build without warnings in the touched files.

**Numbers (the note's table 2.4, as the code gives them)**: 200 MSa/s (Tw 10 ns): Width 20ns .. 100ms (2 .. 10^7 words),
Idle 10ns .. 100ms. 1 MSa/s with the Filter (Tw 1 us): Width 2us .. 10s (2 .. 10^7 words), Idle 500ns .. 10s (500ns is
half a word, rounded up to 1: the same rule as the note's 10us at 4 us words); legacy 1 MSa/s (Tw 2 us): Width 5us
(3 words) .. 10s. 10 kSa/s legacy (Tw 200 us): Width 500us (3 words) .. 10s (50000 words), Idle 100us .. 10s; with the
Filter (Tw 100 us) Width 200us .. 10s. 500 kSa/s: 10us legacy / 5us 16-bit .. 10s; 500 Sa/s: 10ms / 5ms .. 10s (2500
words); EXT: 10ns .. 50ms (10^7 words) for both, Idle Off then 10ns (decided 6).

**Decided and why**
- The state is the ladder index, not a time in ns: the note's persistence stores indexes, the menu steps through them,
  and a host value is shown as the nearest step (the note's rule); after a rate change the step's words replace the
  host's exact words (kept in time by the ladder, as 2.4 says).
- The snap runs at the UI arm, not at every rate writer: every path (rate box, zoom, SET_TIMEBASE, Filter, Sample, the
  channel row's EXT) changes the word period, and the arm is where the words must be right; the menu's opening runs the
  same check so stopped scopes show snapped rows. Only the chosen type's parameter snaps with a notice; the other type's
  parameter snaps silently when it is chosen (the menu shows it then).
- A host **timeout** type (3) has no box: the Type row shows none chosen and no parameter rows (the chip the edge
  glyph), a tap on a box takes over; it is stored as Edge in word 202. Nothing is hidden, nothing is invented.
- The **Nth glyph** deviates from the mockup as rendered: rows.py's one polyline at a 2 px pitch with a 2 px stroke is a
  solid block in TrigMenuNth.png (checked pixel by pixel); the firmware draws two short rising steps with stubs three
  pixels apart on the tall edge's baseline (test_trig_ui holds the pixels). Rise / fall / pulse+ / pulse- match the
  PNGs pixel for pixel.
- The menu's row labels stay white in the waveform viewer (as the five existing rows do in firmware); the mockup greys
  them. The Rising / Falling texts keep 1.00o5's x (78 / 156) instead of centring (nothing moves without a reason).
- The viewer shows the live `trig_ui` state: the waveform file has no trigger type (the stored trigger is the edge).
- The sweep arms the edge type: "Not used by the sweep" would otherwise be false (every point waiting its time-out).

**Bench (coordinator, FEL load)**
1. Trigger menu: six rows, height 336; Pulse: the polarity row, Width 2us, Pulse Shorter, height 430; Nth: N 2 in its
   box, Idle 50us; the chip's glyph per type; the viewer greys; Frequency response shows the note last and no rows.
2. 1 kHz 20 % on CH1 at 500 kSa/s (Tw 4 us legacy): Pulse Longer, Width 100us: triggers on the trailing edge of the
   200 us pulse; Width 500us: "No trigger: check the level and Width" in Normal. GET_TRIGGER 28 = 1, 32 = 25 words.
3. Zoom to 200 MSa/s with Width 10s: the plot says "Width set to 100ms for this rate"; the ACQ rate box after a time/div
   snap shows the time/div notice first, then the Width's.
4. Nth N 3, Idle Off: the third edge after the arm; the keypad: 65535 accepted, 0 becomes 1, a host request closes it.
5. Host SET_TRIGGER2 pulse N 1500 at 1 MSa/s with the Filter: the menu shows Width 1ms; GET_TRIGGER still 1500 until
   a rate change.
6. Settings saved, power cycle: the type and rows come back; Restore default settings: Edge.

**Open**
- The snap notices draw on the Scope's line only (`pc_glue_scope_warning`); the Spectrum's own line does not show them.
- Roll mode: the - / + use the roll rate's range (the rows keep their values; nothing snaps).
- The ACQ Sample row (ui-rows.md section 3) and the C6 / C7 items are separate.

## 73. ui-sample: the ACQ Sample row (fw 0.2.103 + C6 + ui-sample, version provisional, protocol 1.26 unchanged)

Branch `ui-sample` (worktree) from main ba6ce49 (fw-c6 merged). Brief: the ACQ Sample part of `docs/plans/ui-rows.md`
(section 3, checklist items 8-10, section 4's Sample bit; the "Decided" section final), mockups
`docs/mockups/ui-rows/AcqMenuSample.png`, `AcqMenuPeak.png`, `ScopePeak.png`. Commits: 7098449 (ui_ctrl: the row as data,
word 211, Trace style Bars, test_ui), 4a93304 (peak_bars, phos_add_span, test_peak_bars), acc1884 (the firmware: menu,
touch, glue, bars, phosphor, CTRL), 09dac96 (Sample Peak at 200 MSa/s), then the docs. **Nothing benched**: the firmware
needs C4's 0x153C bitstream; the coordinator numbers the version and benches.

**What was built**
- **ui_ctrl.c/.h** (pure, `test_ui.c` `test_acq_row`): the geometry (`UI_ACQ_W` 478, the boxes `UI_ACQ_BOX_X(i)` = 8 + 98 i
  at y + 163, 90 x 40, label line y + 168, value line y + 182; the Memory column's rule `UI_ACQ_COL_X` 402, header x + 415,
  chips `UI_ACQ_CHIP_X` 413 (masks at 411, 58 x 29), pass time `UI_ACQ_PASS_X` 410 (was 312), the note at x + 410, y + 254,
  four 18 px lines to y + 326, 66 px wide); `ui_acq_row_build(in, row)`: the four boxes' texts and states (OFF light grey /
  white, ON green / black, GREY light grey / LIGHTGREY1 text with the value kept and no taps, NONE not drawn) and the note;
  `ui_acq_peak_applied` (peak, Scope, not roll); `ui_acq_box_at` (the drawn box down to the rule at y 211, `UI_ACQ_ROW_HIT_BOTTOM`
  210); `ui_acq_decim_set(filter_set, peak, spectrum)` (the UI's decimation byte: the Filter's K / FIR plus
  `PC_DECIM_SET_PEAK` in the Scope view; `pc_dec_ui_set` calls it); `ui_word_ui` / `ui_word_ui_restore` (word 211: 0xC700,
  bit 0 Grid, bits 1-2 the Spectrum's variant, **bit 3 Sample Peak**, marker check 0xFFF0); `ui_trace_names` "Bars".
- **peak_bars.c/.h** (pure, `test_peak_bars.c`): `pb_walk` / `pb_start` / `pb_next`: the record's pairs walked per column with
  the step as Q16 samples per column (column c covers floor(p) .. floor(p + step) - 1, at least floor(p); zoomed in several
  columns share a sample), the lowest min byte and the highest max byte through `pc_peak_code` (the midpoint's mean-core
  full-class correction, so the bar brackets the trace), swapped and mirrored when inverted; `pb_draw` (to_y / vline
  callbacks, the rows ordered); `pb_phos_record` (one `phos_add_span` per column); `pb_overload`. **phos_map.c**
  `phos_add_span` (one vertical run, each cell once, either order, clipped).
- **pc_hal_scope.c**: `pc_dec_raw_peak[2]` / `pc_dec_peak_gain[2]` / `pc_dec_peak_off[2]` kept by `pc_glue_decim_read` for
  a peak readout; `pc_glue_peak_record(ch, ...)` (0 in roll and in the waveform viewer); `pc_dec_ui_wide` (the Filter or
  Sample Peak in the Scope view: the depth limit for the chips, `pc_dec_fit_memory` on a tap, on the mode follow and after
  the word 211 restore); `pc_glue_trig_tw5` takes `pc_dec_ui_set` (a Peak record has the 16-bit word period);
  `pc_glue_acq_row_draw` (Average, Filter, Sample, the note; replaces `pc_dec_draw_box`; the text centred on measured
  widths), `pc_glue_acq_box_live`, `pc_glue_sample_tap` (toggle, fit, chips, row, re-arm), `pc_glue_sample_peak`;
  `pc_glue_ui_setting_word` / `pc_ui_setting_restore` through the ui_ctrl codec.
- **menu.c**: the rule at x + 402, "Memory" at x + 415, the chips at x + 413; `scope_acquisition_Average_mode_select` is
  `PC_UI_MASK(PC_MASK_ACQ_FILTER)` (the whole row and note, so the roll toggle redraws Sample too). **variables.h**
  `ACQ_MENU_WIDTH` 478 (the save / restore rectangle and the tap-outside test follow it). **statemachine.c**: the row's
  branch is `ui_acq_box_at` (Roll, Average, Filter, Sample; a grey box takes no tap), the Memory column's hit from x + 402.
- **scope_functions.c** `scope_display_channel_bars(settings, ch)` before the Trace style switch of both channels (after
  `PC_PHOSPHOR_SKIP`): a peak record through `pb_draw` with `scope_code_to_y` and `display_draw_vert_line`, the trace
  points at each column's middle (thumbnail); with Sample Peak and an 8-bit record (200 MSa/s) the Bars style's drawing.
  One float multiply per channel per pass (the Q16 step), none per column. **phosphor.c**: a peak record adds bars
  (`pb_phos_record`); the column key includes `dc.peak`. **ctrl_page.c**: Trace style grey and inert with
  `pc_glue_sample_peak()` as in X-Y. **pc_seq_hw.c**: no Average while `pc_glue_sample_peak()` (also at 200 MSa/s).
- Host tests: test_peak_bars (90 checks: walks at 4, 2.5 and 0.25 samples per column, the record's end, the correction
  and inversion, the framebuffer column by column over 725 columns with the spike and dip rows, overload, phosphor twice,
  the decimation configurations for Peak and back incl. the FIR, Filter off, the Spectrum, 6000 / 9000 samples),
  test_ui `test_acq_row` (geometry, hits, both states' texts, grey boxes, the notes, Spectrum, roll, text fit, Bars, word
  211 round trip and 0.2.56..0.2.103's words), test_acq_view (the chips with Peak). `make -s test` green; GCC 10 and 13
  builds without warnings in the touched files (pc_seq_hw.c's two fw-one pointer warnings stay).

**Strings (as implemented)**: box texts "Roll" / "mode", "Average" / "mode", "Filter" / <"off", "boxcar", "CIC2", "CIC3",
"boxcar+FIR", "CIC2+FIR", "CIC3+FIR">, "Sample" / "Normal" or "Peak"; the note "Peak:" / "Filter and" / "Average" / "not
applied" (31, 53, 48, 63 px in font_2) or in roll "Not in" / "roll mode" (32, 53 px); Trace style "Linear", "Dots", "Bars".

**Decided and why**
- **Persistence**: word 211 bit 3 (the plan's choice), not a new word: the word already holds UI view settings with a
  marker, and a sector written before reads bit 3 clear (Normal). The marker check widened to 0xFFF0.
- **Roll note persistent**, not only on a tap: the menu is modal, so a plot notice queued on a tap would show only after
  the menu closes; the column's note slot is free in roll (Peak is not applied there). "Not in roll mode" is 88 px in
  font_2 against the column's 66, so it stands on two lines. The Average box greys in roll the same way (it was drawn as
  "off" with white text while not taking taps).
- **The depth limit applies with the setting** (`pc_dec_ui_wide`), in roll too, as the Filter's does: leaving roll with
  Peak would otherwise arm a 9000-sample record that falls back to legacy (why 3) with the menu saying Peak.
- **At 200 MSa/s Peak stays consistent with the menu**: the FPGA path is legacy there (fw-c6, why 2), so the setting (not
  the record's flag) skips Average and the view draws the record's per-column min / max (the Bars style), which is what
  peak detect at the full rate is. Trace style is grey whenever Peak is applied.
- The box texts are centred on their measured widths (font_2) instead of 1.00o5's hand offsets: the Average and Filter
  texts move by a pixel or so; the Roll box is pc_glue_roll_label's as before.

**Differences from the mockups (the firmware's pixels win)**: the texts are the bitmap fonts' (13 px), not the artboards'
Arial; the greyed boxes keep the off boxes' fill (LIGHTGREY 0x333333) with LIGHTGREY1 text, the mockup's grey fill is a
shade lighter; the note is in the pass time label's grey (0xA0A0A0); the rule under the row (y + 211) still spans the
Memory column as on 0.2.63+. The ScopePeak board's Vmax reads the drawn spike; the firmware's cards read the midpoint
trace (the brief), so a spike shows at about half its height in Vmax / Vpp (open below).

**Bench (coordinator, FEL load after C4's 0x153C, never without the backup)**
1. ACQ menu: 478 px, Sample "Normal" in a grey box as the fourth of the row, Memory column right of x 652, five chips,
   the pass time under the rule; a tap right of the old edge (x 631..728) is inside the menu, x 735 closes it.
2. Sample Peak: the box green "Peak", Filter and Average grey with their values, the four-line note, chips 3000 / 6000
   only (a 9000+ depth snaps to 6000 with the time/div notice when needed); GET_DECIM 0 = 4, 26 / 27 = 0x81 / 0xC0 after
   the next arm; GET_STATUS 46 = 0 with Average on; taps on the grey boxes do nothing. Normal: the Filter back (GET_DECIM
   0 = the Filter, the FIR flag as before), the five chips again.
3. 500 kSa/s, the 1 kHz square plus a short glitch (as fw-c6's bench): the trace as bars, the glitch a bar to its height
   (ScopePeak); Normal misses it. Inverted channel: bars mirrored. Overload (V/div small enough to clip): the bars red.
4. Phosphor on with Peak: the bars accumulate; X-Y with Peak: the Lissajous of the midpoints. CTRL Trace style grey.
5. 200 MSa/s with Peak: the record's per-column min / max (bars), Average not running (GET_STATUS 46 = 0).
6. Roll mode: Sample and Average grey, "Not in" / "roll mode"; Spectrum: no Sample box, the Filter / Average as there.
7. Save settings with Peak, power cycle: Peak back (chips limited); Restore default settings: Normal.
Regression signs: the Filter box text moved off centre, the memory chip hits off by 98 px (a missed `UI_ACQ_CHIP_X`),
bars on a Normal record (a stale `pc_dec_raw_peak`), Average running with Peak.

**Open**
- Vmax / Vmin / Vpp from the bytes (ui-rows.md 3.3 and the ScopePeak board) are not built: the cards read the midpoint
  trace (the brief); a peak record's min / max per channel are in `pc_dec_raw` for a later card source. Done by fw-small (section 76).
- The pass-time line's colour and the note's are literal 0xA0A0A0 in pc_hal_scope.c (no palette name for it).

## 74. fw-d4: streaming logger (fw 0.2.105, protocol 1.27, row 67)

Branch `fw-d4` (worktree) from main 55e5f6b (fw 0.2.104, protocol 1.26). Brief: item **D4** of `docs/plans/fpga-0x153c.md`
section 10 (firmware logger) against the contract's sections 2.1, 2.2 and 6, written in parallel with fpga-d1 (the RTL) and
mcp-d6 (the host). Commits: ff3a1cb (pc_logring, fpga_log, fpga_model stream, test_log), 2101a2c (pc_cmds, pc_hal, the log
loop, the linker section, mock, test_session), 46d1bb6 (vectors), then the docs. **Nothing benched, nothing flashed**: the
firmware needs the 0x153D bitstream (D3); `PC_FW_FPGA_VERSION` stays 0x153C on the branch (the 0x153D integration bumps it),
so on today's unit LOG_START answers E_FPGA detail 1 (0x38 bit 7 clear). The coordinator numbers the version.

**Agreed with the parallel agents (2026-10-21)**
- **fpga-d1** (RTL as built, tb_stream.v): 0x6A is the NEXT word to read; a proper full detector (unread count `occ` set to
  (W - R) mod 8192 at a 0x6A write, +1 per write up to 8192; a write at 8192 sets lap; never on an empty ring); 0x01 = 1
  zeroes the reader, occ and the counter but not lap; W == counter mod 8192 at every snapshot; arm with 0x1A = 0 and
  0x0E = 0 so that 0x69 = 0 stops the writes at the next edge (the tail can be drained); 0x57 must be 0. My first proposal
  (0x6A = the last word consumed, for a literal "write lands on R" detector) was dropped for this.
- **mcp-d6**: the byte layouts of row 67 are theirs (LOG_START 16 bytes with E_BAD_ARG details 0 channels / 1 rate / 2
  format / 4 divider, the 48-byte LOG, LOG_READ returning at once, the 16-byte chunk header with the FPGA word counter, whole
  words per word CH1 then CH2). Mine on top: chunk flags bit 3 (the gap includes a full DDR ring), LOG flags bit 1, ENDED
  on every read after the end, E_STATE 9 while logging, E_STATE 10 outside the Scope view.

**What was built**
- `pc_logring.c/.h` (pure): the DDR ring of whole words with segments at gaps (a chunk never crosses one, so its counter is
  exact; 64 unread segments at most, an append needing a 65th is refused like a full ring); `pc_logring_wptr` /
  `_commit` (the producer), `_read` (the consumer).
- `fpga_log.c/.h` (host-buildable against fpga_bus.h): `fpga_log_start` (0x69 = 1, 0x57 = 0, 0x1A = 0, `fpga_arm_reset`
  with 0x0E = 0, 0x38, base, 0x6A, 0x69 = 3; -1 no ready, -2 no stream), `fpga_log_drain` (a step: snapshot, a chunk of up
  to 2048 words read lane by lane straight into the ring at stride = bytes per word, a second snapshot that keeps or drops
  it, 0x6A; the duration limit), `fpga_log_stop` (0x69 = 0, the tail for STOPPED), the gap (counter check at 8176 words
  OR the lap flag: skip to the newest word, 0x69 = 3, PC_LOG_CF_GAP / _HOST on the next chunk).
- `pc_cmds.c` (`//D4 logger` blocks): `h_log_start` / `h_log_read` / `h_log_stop`, `pc_log_body` (LOG), `pc_log_service`
  (every poll: acq_state follows the log's end; MAIN: the host watchdog, PC_LOG_HOST_MS 10 s, and the link-lost flag set in
  `pc_link_service` on a reset or DTR drop), the dispatch gate (E_STATE `PC_STATE_LOGGING` 9 for everything outside F_BUSY
  / F_MODAL and LOG_READ / LOG_STOP), caps `PC_CAP_LOGGER` (23), features `PC_FEAT_W_LOGGER` 0x0800, `PC_PROTO_MINOR` 27.
- `pc_hal.h` / `pc_hal_scope.c` (`//D4 logger`): `pc_hal_log_start` (the UI auto time-out dropped, 0x0D, `pc_dec_apply`,
  the sequencer off, `fpga_log_start`), `_stop`, `_info`, `_read`; `pc_log_loop` (drain + `pc_poll` MAIN per iteration,
  `pc_log_notice` every 500 ms, the UI's 0x0D restored and "Log ended" queued in `acq_view`'s slot 1 at the end), called at
  the end of `pc_glue_poll_main`.
- `fnirsi_1013d.ld`: `.logring (NOLOAD)` after `.bss`, ALIGN(4096), with LOGRING_START / _END and two asserts (below the
  restart record at 0x81A00000; at least 8 MiB). The image grew 9 KiB (800256 -> 809472 bytes); the ring is not in it and
  start.s does not clear it.
- Host tests: `fpga_model.c` (`//D4 logger`: 0x69 / 0x6A, the occ detector, the 0x38 snapshot at the command byte, 0x01 = 1
  zeroing, the stream in `fm_capture_edge`, `strobe_ns` for a fixed strobe time, `fm_advance_ps`), `test_log.c` (new: 344
  checks), `test_session.c` `test_logger`, mock_hal's `mock_log` (pc_logring under the mock; `mock_log_feed`), test_diag's
  report string (1.27). `make -s test` green; check_vectors OK (151 vectors), `fnirsi1013d.vectors --check` OK.

**Decided and why**
- **The log loop owns the main loop** (no UI pass while logging): the FPGA ring holds 16.4 ms at the fastest tier and a UI
  pass takes 15-40 ms, so the UI cannot run between drains; the screen stays alive as the notice line with the running
  time, megabytes and gaps (the brief's "plain text line in the notice slot"). No new control: a person ends a log through
  the host, and the firmware ends it when the host is gone (DTR drop, link reset, 10 s without LOG_READ) so the screen is
  never frozen for good.
- **One rate rule**, 400 x bytes per sample x channels <= sample clocks (= 500 kB/s): the four tiers of the contract are
  all 500 kB/s, so one inequality is exact for each and needs no table.
- **The 32-bit counter decides, the lap flag confirms**: a chunk is kept only if the second snapshot's counter is at most
  8176 words ahead of its first word (16 words of margin for the write pipeline) and lap is clear. Robust to either detector
  definition; the cost is one 0x38 read per step (the next step reuses it).
- **A gap skips to the newest word** rather than salvaging the words still valid in the FPGA ring: simpler, and a gap is
  already a loss the host reports; it also clears the backlog that caused it.
- **A full DDR ring drops the newest data** (the drain stops; the FPGA laps; the gap says DDR) rather than overwriting the
  oldest unread: single producer / single consumer, no reader moves, and the host sees exactly where the hole is.
- **LOG_READ returns at once** (mcp-d6's choice; the contract said wait up to 100 ms): no deferred request, so GET_STATUS
  and LOG_STOP are never blocked behind it; the host paces itself (the cost model assumes a 1 ms turnaround).
- **LOG_READ needs MAIN** (not F_BUSY): the drain writes the ring in the main loop; after the end it is answered at the UI
  pass rate (8 MiB in about 4 s of 64 KiB reads).
- **LOG_START needs the Scope view** (display mode 0 or 4; E_STATE 10): the Spectrum and the Frequency response run their
  own captures in their passes and the notice slot is the Scope view's.
- **8 MiB DDR ring**: 16.8 s of 500 kB/s (the stalls seen on the bench), the contract's proposal, a power of two, and 9.5
  MiB of the 17.5 MiB free stay for image / .bss growth.

**Deviations from the contract**: LOG_READ returns at once (above); the header's 4..7 is the FPGA word counter as the
contract names it, not a sample index (format 0 has two samples per word: sample = word x 2); E_BAD_ARG details are the
mcp's numbering (0 channels, 1 rate, 2 format / filter, 4 divider), not byte offsets; features bit 11 (fw-c5's shift by
three; the contract's bit 8); the UI is not frozen "behind the REMOTE banner" (there is none since 1.3) but shows the notice
line; the formats are checked against the SET_DECIM setting (the contract's "the filter fits the format") strictly: format
0 needs the filter off.

**Bench (coordinator; FEL load after D3's 0x153D is flashed, never without the backup; contract 9.2 check 8)**
1. `#VER` proto=1.27; GET_INFO caps bit 23; GET_STATUS 44 bit 11. On 0x153C (today): LOG_START 1 / 0 / 399 answers E_FPGA
   detail 1 and GET_STATUS 1 stays 0.
2. The post at 1 kHz on CH1, SET_DECIM 0, LOG_START CH1 format 0 divider 399 for 10 s: the screen shows "Logging to the
   host: ..." counting up, LOG_READ pumped by the host in 64 KiB reads: 5,000,000 bytes (2,500,000 words), no gap flag, the
   square wave continuous across chunk boundaries (1 kHz = 500 samples per period), the counters consecutive; the LOG at
   the end state 2, 0 gaps; the screen "Log ended: 10 s, 4.7 MB" then the normal Scope view in STOP.
3. The same at each tier: CH1 format 2 divider 799 (CIC2), CH1 + CH2 format 0 divider 799, CH1 + CH2 format 2 divider 1599,
   CH1 format 3 divider 799 (Sample Peak): no gap over 60 s; GET_STATUS `pass_ms` (the loop's iteration) and the host's
   achieved rate recorded in hardware-status.
4. Overflow: CH1 format 0 divider 399 (500 kB/s) with the host reading slowly but steadily (LOG_READ max_bytes 1024
   every 10 ms, 100 kB/s): no gap for about 21 s (the 8 MiB ring filling), then gaps with flags 0x0A (gap | DDR), the
   counter jumping, the data between them continuous; LOG flags 3. A 9 s pause of all reads gives no gap; 10 s without a
   LOG_READ stops the log with state 4 (the watchdog); pulling the cable stops it at once (DTR / link reset).
5. LOG_STOP mid-log: state 3, the tail read (bytes produced = the counter at the stop x bytes per word), LOG_READ drains
   the rest with ENDED on the last chunk.
6. While logging: SET_CHANNEL / ARM answer E_STATE 9; GET_STATUS acq_state 5; READ_SCREEN shows the notice line; GET_HEALTH
   answers.
7. Rate refusals: LOG_START 1 / 0 / 398 E_BAD_ARG detail 1 with the hint text.
Regression signs: a gap at a tier rate with the host reading (the drain cost higher than modelled: record `pass_ms`), data
discontinuous without a gap flag (the 0x6A / counter alignment: base != 0 or W != counter mod 8192), the screen not
returning after a log (the loop's exit), the UI's time base wrong after a log (the 0x0D restore).

**Open**
- The 0x153D integration (D3 + the coordinator): `PC_FW_FPGA_VERSION` 0x153D, then bench as above.
- fw-d5 (pattern generator) edits the same `pc_cmds.c` dispatch table, caps / features lines and `fpga_model.c`'s
  `fm_apply` / `fm_len` / read switch: my additions are in `//D4 logger` blocks; test_session's features word is 0xFFF with
  bit 11 (fw-d5's PATTERN bits add to it), proto minor 27 in five places of test_session and one of test_diag.
- mcp-d6: chunk flag bit 3 and LOG flags bit 1 (mine), ENDED on every later read, E_STATE 9 / 10; the host's caveat for a
  gap (the counter jump x word clocks x 5 ns of missing time).
- The device could show the log on the screen (a strip of the last chunk) later; out of scope (MCP-only feature).

## 75. fw-d5: generator as a pattern, pattern generator and its trigger (fw 0.2.105, protocol 1.27, row 67; patterns cut in 79)

Branch `fw-d5` (worktree) from main 55e5f6b (fw 0.2.104, protocol 1.26). Brief: item **D5** of `docs/plans/fpga-0x153c.md`
section 10 (sections 2.1 / 2.2 / 2.3, 7.2-7.5, 9.3) against fpga-d2's 0x153D RTL semantics (agreed by message, listed
below). Commits 30fe638 (firmware), 4743591 (Bode on P), 4f12433 (model and bus tests), 5e5545f (vectors), then the
docs. **Nothing benched, nothing flashed**, `PC_FW_VERSION` and `PC_FW_FPGA_VERSION` (0x153C) unchanged: the generator
needs 0x153D (on 0x153C its writes go to unused codes), so this lands with D3's integration that moves the word.

**What was built**
- **`gen_pat.c` / `.h`** (new, pure C, no FPU): the contract's formulas `gp_p16_from_mhz`, `gp_h16_from_bp`,
  `gp_mhz_from_p16`, `gp_bp_from_h16`, `gp_split` (0 / 1 period / 2 a step outside 2 .. 2^32 - 1 clocks), `gp_ui_square`
  (whole Hz / %, high time clamped to 2 .. P - 2 clocks, 0 Hz / 0 % / 100 % = a static level), `gp_bode_square` (integer
  P, n_h = ceil(P / 2)). Linked in the scope build and in test_session / test_diag / test_fpga_bus.
- **`fpga_bus.c` / `.h` block "D5 pattern"**: the trigger code tables (`fpga_trig_cap_codes` {0x15, 0x16, 0x17, 0x55,
  0x5E, 0x5F, 0x60}, `fpga_trig_pat_codes` {0x76 .. 0x7C}) feeding one byte builder (`fpga_trig_type_regs`,
  `fpga_trig_edge_regs`; `fpga_trig_arm` is the capture's table now, same bytes as fw-c5's); `fpga_pat` (the firmware's
  copy of the pattern), `fpga_pat_step_write` (0x6C, 7 bytes), `fpga_pat_ctl` (0x6D), `fpga_pat_drive` (0x50, 4 bytes as
  1.00o5), `fpga_pat_square` (a running square: 0x6C x 2 + 0x6D = 0x23, 18 strobes; otherwise 0x6D = 0, 0x6F = 0 after a
  gap, the entries, 0x6D = 0x33), `fpga_pat_level` (mode 0 at the idle level), `fpga_pat_load` (SET_PATTERN: END on the
  last step, 0x6E, 0x6F, 0x6D with commit / start), `fpga_pat_status` (0x39); `fpga_ptrig` + `fpga_ptrig_write` (0x7D = 0,
  0x76..0x79, 0x7A (+ 0x7B / 0x7C), 0x7D = armed | rearm << 1 | 0x04 | route << 3), `fpga_ptrig_level16` (0x79),
  `fpga_ptrig_status` (0x42); `fpga_trig_marker` (capture source 3).
- **`fpga_control.c`**: `fpga_on_off_generator` / `fpga_set_generator_freq` / `_duty` through `fpga_gen_program`
  (gp_ui_square, then the square or a level); the set functions write nothing while the generator is off (the values are
  programmed at the switch-on); off = 0x50 = 0 and the pattern stopped low. `fpga_set_trigger_channel` writes 3 for the
  EXT channel with `fpga_trig_marker`, and clears the flag for CH1 / CH2. **`cal_wiz_ui.c`** `cal_gen_level`: the level +
  0x50 = 1 (was 0x52 / 0x51 / 0x50). **`menu.c`**: the trigger button reads "PAT" for the marker.
- **`pc_hal.h` / `pc_hal_scope.c`**: `pc_hal_gen_write(p16, h16, enable)` (p16 = 0: 0x50 alone), `pc_hal_pattern_write`
  (with start or mode 0 a stop first, then 0x50, then the load; the UI's generator switch follows the pin),
  `pc_hal_pattern_status`, `pc_hal_ptrig_read / _write / _status` (0x79 from `pc_dec_level16`, the 0x55 derivation split
  out of `pc_dec_apply`, which re-writes 0x79 at every arm while the engine is armed); boot writes `fpga_ptrig` (zeros:
  disarmed); source 3 in `pc_hal_trigger_read / _write` (the UI channel stays 2; `pc_glue_ext_selected` clears the flag);
  the settings fingerprint carries the flag.
- **`pc_cmds.c`**: `pc_gen_now` / `pc_gen_body` (GEN 40 bytes, `PC_GEN_LEN`), `h_set_gen` on P16 / H16 (a bp duty follows
  the period, a raw H16 is kept as a time; mask bit 0 alone over a pattern writes 0x50 only), `h_set_pattern`,
  `pc_trig2_check` (SET_TRIGGER2's checks shared with the engine; source 3 = edge type only), `h_set_pattern_trigger` (the
  fallback refusal E_STATE 8 when 0x42 bit 7 = 0), SET_TRIGGER source 3, `s->ctx` (GET_GEN reads 0x39 in the main loop
  only), caps 24 / 27, features 0x1000 / 0x2000, GET_INFO 24 = 200 MHz, `PC_PROTO_MINOR` 27, the DDS helpers removed.
- **Bode** (`bode_dsp.c/h`, `bode_sweep.c/h`, `pc_bode.c`): `bode_plan.period` replaces `m` / `phase_inc`, `bode_plan_p0`,
  `step` (round(n 2^34 / P)), `BODE_LEGACY_P_MAX` 133, the even-P rule above 4095 at N = 16, settle from P, the thru by
  (P, N), READ_BODE field 0 = P, the gen callback takes the period; `docs/bode-plan-vectors.json` version 2.
- **Host tests**: `fpga_model.c` runs the pattern (see its header): `test_fpga_bus.c` `test_gen_formulas`,
  `test_gen_square`, `test_gen_change`, `test_pattern_bursts`, `test_pattern_marker`, `test_ptrig` (the 0x153D model also
  joins `versions[]`); `test_session.c` `test_generator`, `test_pattern`, `test_pattern_trigger`; `test_bode.py` and
  `test_bode_sweep.c` on P; vectors section 5i. `make -s test` green except `test_bode.py`, which needs mcp-d6's bode.py
  (PointPlan.period, agreed by message): against a scratch copy of bode.py patched to the agreed planner it reports 0
  failures, and its `plan_vectors()` writes `docs/bode-plan-vectors.json` byte for byte.

**Agreed with fpga-d2 (the RTL semantics the model and the firmware assume)**: a start (0x6D bit 4, a mode-3 write while
idle, or the engine's event) is a step boundary and takes pending shadows before step 0; commit + start in one byte use
the new entries; a start or mode-3 write while running is ignored; the FPGA prefetches two steps (fpga-d2's correction after the first
TD build), so a commit during step k gives old k, k + 1 and k + 2 and new from k + 3, pending 0 from the start of k + 1;
step 0 of a start begins 40..45 ns after a 0x6D strobe (3-4 clocks of synchroniser + 5) and 30 ns after an engine event
(fpga-d2's tb_pattern, 10462 checks); the accumulator is cleared at a start only;
mode 0 stops at once; bursts done reset at a start; the marker needs both 0x15 = 3 and 0x6D bit 3; 0x39 byte 3 bit 1
clears at 0x01 = 1; 0x6C index >= 8 ignored, entry 7 acts as end; 0x50's last data byte wins; one-shot engine events
disarm, re-arm holds the qualifier in reset during the burst; 0x42 bit 7 reads 1 (own engine) unless D2's timing forces
the fallback.

**Agreed with mcp-d6 (row 67 is the contract between us)**: the SET_GEN / GEN / SET_PATTERN / PATTERN /
SET_PATTERN_TRIGGER layouts, the planner on P (P0 = round(200e9 / f), legacy P0 <= 133, the even-P rule, vectors v2 rows
and notes text), no SET_SWEEP.

**Decided and why**
- The generator's square is mode 3 with entries 0 / 1, written for every change with the commit (no "frequency first"
  order any more); the set functions write nothing while the generator is off (the DDS ran hidden; a pattern running
  hi-Z would only cost a stale marker).
- GET_GEN stays F_MODAL and reads 0x39 only in the main loop (`s->ctx`): a menu's modal loop may sit between bus
  transactions of its own, the cached status is reported there. No GET_PATTERN: the poll is GET_GEN (mcp-d6's layout).
- SET_PATTERN flags bit 0 "pin hi-Z" (not in the contract): the pattern trigger engine on EXT needs the pin as an input,
  with the marker triggering the capture internally (contract 9.2 check 10b).
- Source 3 rides on the UI's EXT channel (`fpga_trig_marker`) rather than a fourth UI channel value: sixty UI sites test
  the channel with if / else chains that would treat 3 as CH2; the marker uses EXT's apply path and position convention
  anyway, so the EXT corrections are the right ones. The generator may stay on with source 3.
- The engine's 0x79 is derived like 0x55 for the capture's current decimation and re-written at every arm while armed.
- At N = 16 an odd P above 4095 is rounded to an even P (one block per record otherwise: no noise estimate, no halves;
  P 8191 had no record at all). Found by test_bode.py's 30 kHz case.

**Deviations from the contract**: no SET_SWEEP (coordinator, 2026-10-21: the contract wins; the brief's request was
withdrawn); SET_PATTERN's start byte at offset 10 and flags bit 0 hi-Z (the contract's layout left both open); the gap in
ns rounded to clocks and echoed; GEN's layout (the contract only says "P16 and H16 beside mHz and bp"); the Bode even-P
rule at N = 16 above 4095 (the contract's "every plan maps with P = 4 M" holds; the finer grid needed it).

**Bench (coordinator; after D3's 0x153D is flashed and `PC_FW_FPGA_VERSION` moved, never without the backup)**
1. `#VER` proto=1.27; GET_INFO 24 = 200000000, caps bits 1, 24, 27; GET_GEN 40 bytes, byte 23 = 0, P16 13107200000 for
   the boot 1 kHz; the probe on the post: 1.000000 kHz on the counter (GET_COUNTER) at the 1 s gate.
2. Generator menu 10 kHz 25 %: counter 10.0000 kHz, duty 25 %; change to 12 kHz with the Scope view showing the square at
   50 traces: no short or long pulse (persistence / phosphor).
3. SET_GEN freq 999500 mHz: the counter reads 999.5000 Hz at the 1 s gate; GET_GEN byte 1 bit 5 = 0 within one period.
4. Calibration wizard gain step passes (static levels), the 1 kHz back after it.
5. SET_PATTERN pulse train (vector `req_set_pattern_pulse_train`) with SET_TRIGGER source 3, segments of 10: timestamps
   1 ms apart, the pulse at the pretrigger point (contract 9.2 check 10).
6. SET_PATTERN_TRIGGER Nth edge 3 on CH2 with SET_PATTERN start source 1: a burst after every third CH2 edge (check 10b).
7. Frequency response sweep 100 Hz .. 1 MHz, 50 points: READ_BODE rows carry P (4 x the old M), the RC's -3 dB as before.
Regression signs: a 0x51 / 0x52 in the event log's write trace, GEN 24 bytes, the generator dead on 0x153C (expected
there), the square restarting (a phase jump) on every frequency change.

**Open**
- `test_bode.py` and mcp's `tests/test_bode.py` need mcp-d6's bode.py (PointPlan.period, vectors v2) at the merge.
- The coordinator moves `PC_FW_FPGA_VERSION` to 0x153D (D3), numbers the version and benches.
- No UI for patterns (host only, as decided); the generator menu shows the square's frequency while a host pattern drives
  the pin.

## 76. fw-small: the 0x0E force, Vmax / Vmin / Vpp from the peak bytes, the readout fault message (fw 0.2.105, protocol unchanged)

Branch `fw-small` from main 803d215. Commits 53551d4 (force), 7c3ced0 (peak cards), cee3268 (message), then the docs.
Nothing benched; `PC_FW_VERSION` / `PC_FW_FPGA_VERSION` untouched (the coordinator numbers at the merge). No wire change.

**1. The forced stop writes 0x0E = 0, then 0x1A = 0** (queue item "0x153B+: the deadline force must write 0x0E = 0 too",
fpga-gen13). The FPGA ends an untriggered auto-mode capture only on `~trigger_mode_s && time_base_cnt > 0x0E` (zaklad.v
1477). With an explicit SET_TRIGGER `auto_timeout` the arm writes 0x0E = that count as words and the host deadline
(`pc_host_auto_ms`) comes at about that many samples, i.e. at half the FPGA's count in the legacy layout: 0x1A = 0 alone
stopped nothing, the record ended on the FPGA's own time-out up to twice later and a trigger in between was reported
untriggered (`host_forced`). Now `fpga_force_stop` (fpga_bus.c, host-buildable) writes 0x0E = 0 (32 bit, high byte
first, as `fpga_arm_reset` writes it; the brief said 16 bit, but the register and every other write are 32 bit) and then
0x1A = 0; `pc_hal_force` calls it. 0x0E first: while 0x1A is still 1 its partial bytes stop nothing. On 0x153B+ a 0x0E /
0x1A write before the 0x14 read moves nothing (fpga/al3/README.md section 11 change 4); every arm writes 0x0E again
(`fpga_arm_reset` with `write_0e`: always for the host, Bode and log arms; for the UI whenever timeperdiv indexes
`timebase_settings`, which is always).
Callers, all fine with the immediate stop: the host poll's fw-auto deadline (pc_cmds.c), FORCE (`h_force`, PC_DEBUG
builds only), the Scope view's auto deadline (`pc_ui_auto_service`), the UI Average pass deadline (pc_seq_hw.c: the
partial record is discarded anyway), the Frequency response engine's deadline (`bh_force`: FPGA auto with 0x0E = 2 x
total; its deadline is past, so stopping now is what it wants).
Host test `test_fpga_bus` test_force_deadline: 0x153B / C / D, firmware auto (armed normal), 0x0E = 2 x the deadline in
words, the force at the deadline (three deadlines, pretrigger 0 / 200): done within two write clocks of the force, 0x0E
reads 0, untriggered, the newest total words contiguous; with 0x1A = 0 alone it fails 33 checks. The model needed no
change (it already evaluates 0x0E on every edge); `to_force` now calls the firmware's `fpga_force_stop`, so the older
time-out tests run the real sequence too. docs/protocol.md FORCE text updated (no wire change).

**2. Vmax / Vmin / Vpp (and Vp) from the peak bytes** (ui-sample section 73 "Open", docs/plans/ui-rows.md 3.3). The cards
read `settings->max / min / peakpeak` (scope_functions.c `scope_display_vmax` and the others), not `minDC / maxDC` as the
brief said: those feed the auto set and the zero-crossing thresholds (`centerDC`, computed after the first pass) and stay
as they were. `fpga_read_sample_data` now calls `pc_glue_peak_extremes(settings)` after `center` is computed: for a peak
record (`pc_dec_raw_peak[ch]`, reset by every readout) max / min become the highest max byte and the lowest min byte of
the whole record through `pc_peak_code` (`pb_extremes` in peak_bars.c, the bars' conversion, mirrored when inverted) plus
the readout's `dcshift`, and peakpeak their difference; Vp follows since it reads max / min. Unchanged, on the midpoint
trace: `center` (the 50 % trigger level), average (Vavg), rms (Vrms), the frequency, period, widths and duties (with the
counter on the card's channel the six timing cards come from the counter as before; the counter does not feed the
voltage cards). A Normal record, a filtered or averaged one and the 200 MSa/s Peak case (8-bit, every sample stored) keep
the record's own min / max. The saved waveform header (scope_functions.c) and the reference (ref_and_math.c) store the
peak values with a peak record, consistent with the cards at save time.
Host test: the card code (scope_functions.c) is not host-buildable; `pb_extremes` is: `test_peak_bars` test_extremes (the
spike and the dip, inversion, the correction, equal to the extremes over the drawn bars at four column steps, the
midpoint 47 codes short of a 200-code spike on a 105 floor).

**3. "ADC readout fault near code 128"**: kept, with a new text. `ACAL_E_READOUT` is still reachable on the supported
bitstream: the capture stage training (`cal_train`, every wizard pass on a bitstream with the stages, 0x153A+) fails a
core with no clean stage, and the zero step turns a clip seen only at 200 MSa/s near code 128 into it
(`acal_readout_fault`). Both judge the data, not the bitstream; a unit with marginal ADC data timing can fail them. "check
the FPGA bitstream" named nothing to do now that there is one bitstream, so the text reads "ADC readout fault near code
128: power-cycle and retry; report it if it persists" (it fits the result line: test_cal_wizard's width check).
docs/guide/calibration.md has the row back (what it means, power-cycle, warm up, retry, report with the step's second
line and a Diagnostics screenshot); firmware/README.md and docs/articles/adc-capture-fault.md quote the new text.
adc_cal.c's short reason name "ADC readout near 128" (CAL_PROBE, logs) is unchanged.

**Tests**: `make -s test` all green (test_fpga_bus 10502, test_peak_bars 118, test_cal_wizard 680 checks), check_vectors
OK, `make -s gcc10` without new warnings in the touched files.

**Bench (owner / coordinator, FEL; never without the backup)**
1. Force with an explicit auto_timeout: REMOTE, SET_TRIGGER mode auto with `auto_timeout` = for example 2 x the record
   (samples), no signal (or the level off the signal), ARM, WAIT: DONE at about the deadline (`ms_since_arm` near
   `pc_host_auto_ms`, not twice it), untriggered, the record contiguous (no phase jump in a sine). Then the same with a
   trigger edge arriving between the deadline and twice it: the record ends at the deadline, untriggered (before the fix
   the capture waited and the trigger was reported untriggered). PC_DEBUG build: FORCE ends an ARMED capture at once.
2. Spike with Peak: 500 kSa/s, the 1 kHz square plus a short glitch (fw-c6's bench) or a narrow pulse, ACQ Sample = Peak,
   the Vmax / Vpp / Vmin cards on: Vmax reads the bar's top (the glitch's height), Vmin the lowest bar bottom; Sample =
   Normal: Vmax back to the trace's maximum (the glitch missed or at its sampled height). Invert the channel: Vmax / Vmin
   swap and mirror as the bars do. Vavg / Vrms / Freq read the same in Peak and Normal on a clean square.
3. Calibration wizard: a normal pass still passes (the new message shows only on a fault).

**Open**
- FORCE (debug builds only) during a sequencer run: 0x0E = 0 now also ends every later segment of the run after about two
  words (the sequencer's per-segment `cap_rst` resets `time_base_cnt`, 0x0E stays 0), i.e. records that reach before
  their reset; before, FORCE on a normal-mode run turned the rest into auto segments with the arm's 0x0E. STOP is the
  command that ends a run (`pc_hal_seq_stop`). Rejecting FORCE on a run (E_STATE) would be a wire-visible change, so it is
  left to the coordinator.
- ui-sample section 73's first "Open" bullet is done here.

## 77. fw-cruft: what the 0x153C bitstream dropped, out of the firmware and the host (fw 0.2.105, protocol 1.27 row 67)

Branch `fw-cruft` from main 703e9c9. Commits 43e2b0a (0x0F), b10bc96 (READ_LIVE, firmware + protocol + vectors), 1777a67
(READ_LIVE, mcp), 19618eb (0x53 swap, firmware + protocol), eea44e1 (0x53 swap, mcp), then the docs. Nothing benched;
`PC_FW_VERSION` / `PC_FW_FPGA_VERSION` untouched. Source: fpga/al3/README.md section 15 (fpga-c4's cruft table).

**1. Write 0x0F** (`sample_read_enable`: both mux arms were `i_mcu_clk`; 0x153C decodes nothing). Removed from
`fpga_do_conversion` (0x0F = 0 / 1 by `samplemode`), `fpga_done_conversion` (0x0F = 1 at done), `fpga_set_long_timebase`
(the only statement left: the function and its five callers' branches are gone; the callers now call
`fpga_set_time_base` when not in long mode, as before), `pc_hal_arm` and `pc_hal_acq_finish`. `scopesettings.samplemode`
only chose between those writes: deleted with its seven assignments (in-RAM struct only; `scope_save_setup` copies it in
memory, nothing on the card). The model never decoded 0x0F (a length-1 traced write), so the check that it stays gone is
`host_tests/check_retired_regs.py` in `make test`: a literal `fpga_write_cmd` / `fpga_write_u24` / `seq_w8` of 0x0F,
0x3C or 0x41 in `firmware/scope/*.[ch]` fails it (it flags the four old writes on the pre-change `fpga_control.c`).
docs/protocol.md ARM step 2 and the section 14 note on ARM / DONE say so.

**2. READ_LIVE (0x0206) / caps bit 9 LIVE_READ**: `PC_CMD_RETIRED_0206` with a NULL handler and no cap, so the answer
stays E_UNSUPPORTED "capability" (not E_UNKNOWN_CMD); `PC_CAP_LIVE_READ` is a reserved-bit comment. test_session: the
caps word has bit 9 clear, 0x0206 answers E_UNSUPPORTED. gen_vectors.py keeps the request's sequence slot (a `None`
entry) so every later `req_*` vector keeps its bytes: docs/protocol-vectors.json only loses `req_read_live` and the
command map's READ_LIVE (both checkers OK). protocol.md: command row and caps bit 9 "(reserved)", the READ_LIVE paragraph
gone, the 0x153A note says 0x153C drops the reads, row 67 says it (no new minor). The mcp: `P.RETIRED_CMDS = {0x0206}`
(the simulator answers E_UNSUPPORTED), `Cmd.READ_LIVE`, `Caps.LIVE_READ`, `ReadLiveReq`, `Scope.read_live`,
`_cmd_read_live` removed.

**3. 0x53 bits 4..2 and 0x3C / 0x41.** Bits 3 / 4 were already written 0 (fw-one). Bit 2, the core-order swap, was still
live: SET_DECIM flags bit 0 put `PC_DECIM_SWAP` into the setting and every arm wrote it to 0x53, which 0x153C ignores
while its 0x29 reads bits 4..2 as 0, so GET_DECIM's status (bit 2) and the FPGA's 0x29 (bytes 26 / 27) disagreed. Now
`PC_DECIM_SWAP` / `PC_DECIM_FLAGS`, `pc_decim_cfg.flags`, `pc_dec_flags` are gone, `pc_decim_mode` = K, `fpga_decim_write`
masks the mode to bits 1..0, SET_DECIM accepts flags bits 0..2 and drops them (GET_DECIM flags = bit 3 FIR only),
`PC_DECIM_SET_MASK` = K | SET_FIR | SET_PEAK, the Bode engine passes only its FIR bit. `fpga_model.c` reads 0x29 bits 4..2
as 0 on 0x153C+ (README section 15: "ignored on write, 0x29 byte 0 bits 4..2 read 0"; the brief's "read back" was the
pre-0x153C behaviour, kept for the model's older versions). Tests: test_session's decim maths without the swap, SET_DECIM
flags 0x07 accepted and dropped for filter 2 and 4 (setting, flags byte 0, status 0x82 / 0x81); test_fpga_bus
`fpga_decim_write(0x1E)` puts 0x02 on the bus. 0x3C / 0x41: no FPGA traffic left (fw-one deleted the commented-out
`fpga_set_battery_level`; touchpanel.c's 0x3C / 0x41 are GT911 bytes); the check script keeps it so. mcp: the DECIM_SWAP /
NEG constants, `DecimConfig.flags`, the SetDecimReq bits and server.py's flag names are gone, the simulator drops bits 0..2.

**Tests**: `make -s test` all green (test_session 73253 / 73251, test_fpga_bus 10504), check_vectors OK (both
checkers), check_retired_regs OK; mcp ruff clean, pytest 548 passed (Windows venv); `make -s gcc10` clean of new warnings
(clean rebuild; the touched files show only the upstream unused-variable / implicit-declaration ones).

**Bench (owner / coordinator, FEL; never without the backup)**: the 0x0F removal changes nothing the FPGA sees on
0x153B / 0x153C. Check a UI capture and a host ARM at 200 MSa/s and 1 MSa/s (trace and edge position as before), and
GET_DECIM after `SET_DECIM 2, flags 1`: flags byte 0, bytes 2 and 26 equal (0x82).

**Open: the roll mode reads the removed live ADC registers (a 0x153C regression, found here, reported to the
coordinator 2026-10-21; the owner decides: a logger-based roll mode for 0x153D, restore the 0x24..0x27 arms in the RTL,
or drop roll).** Long mode (time bases 100 ms/div and slower: `scope_set_timebase` in scope_functions.c sets
`long_mode` for `timeperdiv < 11`; menu.c's ACQ Long / Short box) runs `scope_get_long_timebase_data` (test.c 824, called
from fnirsi_1013d_scope.c 333) and `scope_check_long_trigger` (test.c 1054). Both take every point from
`fpga_average_trace_data` (fpga_control.c 1413; `_long` at 1457), which sends `settings->averagecommand` and strobes 10
(100) bytes. `averagecommand` is 0x24 for CH1 and 0x26 for CH2 (scope_functions.c 5864 / 5886, variables.h 555): the live
ADC pin reads that fpga-c4 removed (78ccaa0 dropped the `8'h24..8'h27` arms; the read now returns the stale output
register byte). The audit missed it because it looked for READ_LIVE and literal 0x24 reads, not a command byte held in
the channel settings. Left untouched here on the coordinator's instruction (`averagecommand` and the roll path exactly
as they were); `check_retired_regs.py` does not list 0x24..0x27 for that reason (add them when the roll path changes).
`scope_get_long_timebase_data1` (test.c 583) is a dead older copy with the same reads.

## 78. fw-roll: roll mode on the 0x153D logger stream (fw 0.2.105, protocol unchanged)

Branch `fw-roll` from main 2611de2. Commits 8e1458d (roll.c + test_roll), 827506f (roll_view.c, the stream glue, the
removals), 532f8af (host precedence), 7ca8d78 (check_retired_regs), then the docs. **Nothing benched, nothing flashed**;
`PC_FW_VERSION` / `PC_FW_FPGA_VERSION` untouched (still 0x153C: on today's unit roll mode stops at once with "Roll mode
needs FPGA bitstream 0x153D", the intended loud failure until the 0x153D integration). Owner decision 2026-10-21: roll
draws real decimated samples from the logger, both channels, continuous, no RTL change (section 77 "Open" found the
0x153C regression). User-facing description: firmware/README.md "Roll mode", docs/guide/screens.md "Roll mode".

**What was built**
- `roll.c/.h` (pure, host-tested): `roll_plan` (per time/div the 0x0D divider and the words per screen column, below),
  `roll_feed` (legacy-layout words CH1 B, CH1 A, CH2 B, CH2 A into columns: the mean of the 2 W samples per channel, each
  corrected per core with the readout's gain / offset, rounded, 0..255, mirrored when inverted; 64-bit integer maths once
  per column, no float), the 3000-column record (Auto slides, Single ends with ROLL_EV_DONE, Normal empties and waits:
  ROLL_EV_RESTART), the trigger (a crossing of the level between consecutive columns of the trigger channel in the edge's
  direction; a gap clears the previous column), `roll_code`.
- `pc_hal_scope.c` `pc_glue_roll_start / _running / _release / _read` (the `//fw-roll` block after the logger): the
  logger's start with the plan's divider, K = 0, the sequencer off, `fpga_log_start(3, FPGA_LOG_F_LEGACY)` into a 32 KiB
  ring of its own (`pc_roll_mem`, .bss; the host log's 8 MiB ring is not touched), `pc_glue_ui_capture_start` (a host
  single in flight ends, the ring is invalid, as for a UI conversion); release = `fpga_log_stop(ENDED)` (0x69 = 0, no
  tail) and 0x0D = `fpga_divider`. Running = open, RUNNING, the same time base and `fpga_divider` unchanged since the
  start (a UI rate write also wrote 0x0D). `pc_hal_arm`, `pc_hal_log_start` and `fpga_do_conversion` call the release
  first: one hand-over rule (whoever arms owns the FPGA), so calibration, AUTO SET, the Bode engine and the host never
  meet a running stream.
- `roll_view.c/.h` (device): `roll_view_pass` from the main loop's long-mode branch (was `scope_get_long_timebase_data`):
  STOP releases; a changed trigger mode / source / edge / time base, or RUN after a Single record, resets the model; the
  stream is started when not running for the time base (failure: STOP and the amber reason); `fpga_set_trigger_level`
  while waiting (the 0x17 write is ignored while streaming), the corrections from `fpga_channel_correction`; up to 8 reads
  of 2048 words per pass; the trace area redrawn only when columns came in (newest at x 726, one column per pixel, both
  enabled channels, grid, pointers, cursors, the amber line). `roll_view_reset` from `scope_preset_values`' long branch.
- Removed: `averagecommand` (variables.h, scope_functions.c), `fpga_average_trace_data(_long)` (fpga_control.c/.h),
  test.c's `scope_get_long_timebase_data`, `_data1` (dead copy) and `scope_check_long_trigger` (and test.h), the
  scopesettings fields `count` / `xpos` / `lastx` and the channel `sample1` / `sample2` (only the old roll used them), the
  global `multiply` (only the old roll trigger used it; scope_functions.c's are locals). `previoustimerticks` stays (touch
  tap timing; the old roll overwrote it).
- pc_cmds.c: ARM and LOG_START no longer refuse the long time base (E_STATE detail 2): they stop the UI as in the short
  ones (`pc_hal_ui_set_run(0)`), and the arm releases the stream. READ_RAW (latest capture) keeps detail 2 only while the
  UI rolls, and the MAIN poll clears `ring_valid` only then. mcp protocol.py: the unused `LOG_S_LONG_TIMEBASE` removed and
  the docstrings updated; docs/protocol.md rows 34 / 35 and row 67's LOG_START details say so. No wire layout change.
- check_retired_regs.py: 0x24..0x27 retired, plus a `...command = 0x..` assignment pattern (it flags the two
  averagecommand lines of 2611de2's scope_functions.c).

**Divider and rate per time/div** (column = 1/50 division = W words of 2 (D + 1) x 5 ns)

| time/div | D | W | samples / channel / column | per channel | FPGA ring before a lap |
|---|---|---|---|---|---|
| 50 s, 20 s, 10 s, 5 s, 2 s | 999,999 | 100, 40, 20, 10, 4 | 200, 80, 40, 20, 8 | 200 S/s | 82 s |
| 1 s | 499,999 | 4 | 8 | 400 S/s | 41 s |
| 500 ms | 249,999 | 4 | 8 | 800 S/s | 20 s |
| 200 ms | 99,999 | 4 | 8 | 2 kS/s | 8.2 s |
| 100 ms | 49,999 | 4 | 8 | 4 kS/s | 4.1 s |

At most 2000 words/s (about 2.4 ms of bus time per second, far below the logger's 2 ch 8-bit tier). D + 1 = 10^6 is the
largest round value under the 20-bit compare, which is why 2 s/div and slower share it and differ in W.

**Decided and why**
- **Legacy 8-bit layout, both channels always streamed**: no dependence on the UI's Filter / Sample setting, one word
  layout, a channel switched on mid-roll has its history; the column mean of 8..200 samples is the anti-alias boxcar the
  CIC would give (the 16-bit layout would need the filter set up per time base for no visible gain at these rates).
- **The mean per column, not the newest word**: a boxcar over exactly the column's time (no mains hum aliased into slow
  beats); 1.00o5 also averaged (10 peeks).
- **The roll trigger kept** (brief item 2): it worked on 0x1532..0x153B (the live-pin reads worked there; Donwulff's "roll
  does not work" in fpga/al3/README.md is about the 0x0F mux, which never mattered for these reads), but its level was the
  pointer's screen position + 25 compared with averages scaled by a constant (right at one V/div only). Now the level is
  `fpga_set_trigger_level`'s code compared with the columns as drawn. The sweep display became a scrolling one (the
  owner's "continuous"); the 3000-column records and the Single stop / Normal restart are 1.00o5's.
- **EXT runs as Auto with an amber line**: the stream has no EXT channel; 1.00o5 silently used CH2.
- **The release in every arm** instead of an explicit stop at each place that leaves roll (time base, Spectrum, viewer,
  restore, calibration, AUTO SET, host): one rule, and the roll pass restarts the stream when it is wanted.
- **A restarted stream counts as a gap** (the partial column is dropped, the trigger needs two new columns); the trace
  does not mark gaps.

**Host test** `test_roll` (161 checks): the plan (exact column period, divider bound, tier, bus cost), `roll_code`
(rounding, per-core gain / offset, limits, inversion, no overflow at 100 x 255 x 1.5), Auto across split feeds, a gap,
the sliding record, EXT / no plan as Auto, the trigger (rising / falling, equal to the level, CH2 inverted, no crossing
across a gap, Single full and stopped, Normal restart and re-trigger), and end to end on `fpga_model.c`'s 0x153D stream at
100 ms/div through `fpga_log` + `pc_logring` (34 passes of 30 ms: every column equal to the boxcar of its words' indices,
all words consumed, a 5 s stall gives one gap and correct columns after it, the release writes 0x69 = 0 only, 0x153C
refused). test_session: ARM while rolling arms and stops the UI; LOG_START while rolling starts; READ_RAW detail 2 while
rolling, detail 1 stopped. `make -s test` all green; check_vectors OK; `make gcc10`, `make` (GCC 13) and the PC_DEBUG
build without new warnings in the touched files.

**Bench (owner / coordinator; needs the 0x153D bitstream flashed and `PC_FW_FPGA_VERSION` 0x153D, i.e. after the D3
integration; FEL, never without the backup)**
1. A 1 Hz-ish signal (a function generator, or the calibration pin's generator at 1 Hz) on both CH1 and CH2, 1 V/div, ACQ
   Roll, 1 s/div, trigger Auto, RUN: the trace scrolls continuously in from the right edge, both channels, real amplitude
   (a 1 Vpp square is one division high), no pauses; taps answer at once (no busy wait). 100 ms/div: smooth, about 20
   columns per pass; 50 s/div: one new column per second. Invert a channel: it mirrors.
2. Trigger Normal, rising, level in the middle of the square: the trace waits, then starts at a rising edge. Single: stops
   after 3000 columns (60 s at 1 s/div). Source EXT with Normal: the amber line, it rolls as Auto.
3. Host while rolling: LOG_START (`scope_log_start`) takes over (STOP, "Logging to the host"), LOG_STOP, then RUN: the roll
   resumes with an empty record. ARM (`scope_capture`) while rolling: the capture completes, the UI in STOP. READ_RAW of the
   latest capture while rolling: E_STATE detail 2.
4. Leave roll with the ACQ Roll box (a tap does not cross): the short time bases unchanged (a 1 kHz capture at 200 MSa/s
   and 1 MSa/s, edges where they were). Spectrum from roll: roll goes off, the spectrum runs. AUTO SET from roll: it
   captures (the stream released) and ends where AUTO SET leaves the scope.
5. At 100 ms/div open a menu for more than 4 s: after it closes the trace continues (one gap, no garbage).
6. On 0x153C (today, before the integration): RUN in roll stops at once with "Roll mode needs FPGA bitstream 0x153D".
Regression signs: a trace that jumps every pass (the column phase lost), a flat line at mid-screen (the corrections or
the lane order), the short time bases' rate wrong after roll (0x0D not restored), a host capture that never completes
after roll (the stream not released: 0x69 still 1).

**Open**
- The viewer of a roll waveform or picture saved after this change is not rechecked: the roll no longer writes
  `disp_xstart` / `disp_xend` / `triggerhorizontalposition` (1.00o5 wrote window values of 375..2625 there); it still sets
  `disp_have_trigger` and `disp_first_sample` as before.
- After a host capture or log in a long time base the screen keeps the frozen roll (the roll view draws only while
  running): the host capture is in the trace buffers but not drawn, and "Log ended" is not shown, until RUN.
- Gaps (a long menu, a restart) are not marked on the trace.
- The trigger compares column means, so a pulse much shorter than a column (2 ms at 100 ms/div) may not trigger, as with
  1.00o5's averages.

## 79. fw-nopat: the pattern feature cut, the square kept (fw 0.2.105, protocol 1.27 row 67)

Branch `fw-nopat` from main 4fd9f2f. Owner decision 2026-10-21 15:55 (docs/plans/task-queue.md "Pattern feature cut"):
0x153D did not fit, so the pattern generator (8 steps, repeat, gap, the marker and its capture source, the pattern
trigger engine) is cut; the generator stays the fractional two-step square. Commits c7a05e3 (the removal: firmware,
model, tests, vectors), 78fc734 (the mechanical rename fpga_pat_* -> fpga_gen_*), 990922e (the square on fpga-d3's RTL
d60a0ad, as fpga-d3 sent it through the coordinator), then the mcp and docs commits. **Nothing benched, nothing
flashed**; `PC_FW_VERSION` / `PC_FW_FPGA_VERSION` untouched (still 0x153C until the 0x153D integration).

**Removed**: SET_PATTERN 0x0217 and SET_PATTERN_TRIGGER 0x021A (no table entry: E_UNKNOWN_CMD, the codes reserved;
they were never in a release, so not the READ_LIVE-style NULL handler), capture trigger source 3 (SET_TRIGGER source > 2
is E_BAD_ARG detail 0 again, SET_TRIGGER2 detail 3 "source 0..2"; GET_TRIGGER never reports 3; the "PAT" trigger button
text; `fpga_trig_marker` and its byte in the settings fingerprint), `pc_trig2_check` (SET_TRIGGER2's checks inline
again, as before fw-d5), `fpga_pat_load`, the 0x6E / 0x6F writes (also the square's "0x6F = 0 after a gap"), the MARK
and END step flags, `fpga_ptrig*` with 0x76..0x7D and read 0x42, the per-engine code tables (`fpga_trig_arm` writes
0x5E / 0x5F / 0x60 directly again, same bytes), `pc_hal_pattern_*` / `pc_hal_ptrig_*`, the session's `gen_src` /
`pat_st` / `ptrig_st` / `pat`, pc_dec_apply's 0x79 re-write. Caps bits 24 (PATTERN_GEN) and 27 (PATTERN_TRIGGER) and
features bits 12 (0x1000) and 13 (0x2000) are reserved (0): the features word is 0x0FFF on the built-for bitstream.

**Kept, same behaviour**: the square path (`fpga_gen_square` / `_level` / `_drive` / `_ctl` / `_step_write`,
`pc_hal_gen_write`, `gen_pat.c` with gp_split and the P16 / H16 formulas, SET_GEN with its raw modes, GEN 40 bytes, the
Bode engine on P = 4 M, the generator menu, the wizard's static levels, the boot 1 kHz). Its bus bytes are fw-d5's
except entry 1's byte 0 (no END bit 0x80: fpga-d3 ignores bits 7 / 6) and the 0x39 read.

**fpga-d3's square (d60a0ad, by message)**: 0x6C byte 0 { -, -, level, index }, index 0 high / 1 low, others ignored;
0x6D mode 3 continuous, 0 off (1 / 2 act as off), bit 2 idle level, bit 4 start, bit 5 commit, bits 3 / 7..6 ignored;
0x50 bit 0 of every data byte, last wins; read 0x39 = 4 bytes { running, 0, pending, 0 x 4, step }, 0, 0, { 0 x 7, mode
active }. A commit is taken at once when the step in progress has at least 3 clocks left (always for the firmware's
steps of 4 and more): that step completes with its old length, every step from its boundary is new (fw-d5's model:
new from the third boundary), PENDING for 2-3 clocks, two commits in a row take effect in order, the accumulator is not
cleared. Start latency: step 0 8..9 clocks after the strobe (fpga-d3 to confirm from tb_square). The removed registers
are ignored byte by byte; 0x15 = 3 acts as EXT.

**GEN**: byte 1 = 0x39 byte 0 masked to running / pending / step (0xA1), byte 22 = 0x39 byte 3 bit 0 (mode active),
bytes 2..3 (bursts) and 23 (the pattern source) reserved 0; the size stays 40. `fpga_gen_status(st[2])` reads the four
bytes; `pc_hal_gen_status(st[2])`.

**Renamed** (78fc734, mechanical): `fpga_pat_*` / `FPGA_PAT_*` / `fpga_pat_cfg` / `fpga_pat_step` / `fpga_pat` ->
`fpga_gen_*`, and in `fpga_model.c/.h` and test_fpga_bus.c `fm.pat_*` / `fm_pat_*` / `FM_PAT_EDGES` / `PAT_VER` ->
`gen_*`. `gen_pat.c/.h` keep their names (only the square's maths).

**Tests**: `fpga_model.c` models fpga-d3's square (entries 0 / 1, mode 3 or off, the immediate take with
`gen_take_clk`, no prefetch, the cut registers ignored); test_fpga_bus `test_gen_change` expects the first new step right
after the commit and two commits in one step (the second wins), the new `test_gen_status` (0x39 bytes 0 / 3, five
strobes, indices >= 2 and 0x6E / 0x6F / 0x76..0x7D ignored) replaces `test_pattern_bursts` / `test_pattern_marker` /
`test_ptrig`; test_session `test_generator` checks the reserved caps / features bits, 0x0217 / 0x021A E_UNKNOWN_CMD,
source 3 refused, GEN bytes 1 / 22, and the enable alone writing the square; `test_pattern` / `test_pattern_trigger`
gone; the features word 0x0FFF. `check_retired_regs.py` adds writes 0x6E, 0x6F, 0x76..0x7D and read 0x42 (it flags 7
lines of 4fd9f2f's fpga_bus.c). gen_vectors.py: the pattern vectors (`req_set_pattern_pulse_train`, `resp_set_pattern`,
`resp_set_pattern_e_bad_arg_duration`, `req_set_pattern_trigger_nth_ch2`, `resp_set_pattern_trigger`,
`resp_set_pattern_trigger_e_state_shared`, `req_set_trigger2_marker`) and the command map's SET_PATTERN /
SET_PATTERN_TRIGGER gone; `resp_gen_1_27` has byte 1 = 0xA1 and byte 22 = 1. `make -s test` all green (test_session
73124 / 73122, test_fpga_bus 10440), check_vectors OK (both checkers), check_retired_regs OK.

**Bench (owner / coordinator, after D3's 0x153D is flashed and `PC_FW_FPGA_VERSION` moved; FEL, never without the
backup)**: fw-d5's steps 1-4 and 7 (section 75) unchanged, with GEN byte 22 = 1 while the square runs and caps bits 24 /
27 clear; steps 5 and 6 (patterns) are gone. Also: SET_TRIGGER source 3 answers E_BAD_ARG detail 0, 0x0217 answers
E_UNKNOWN_CMD; a SET_GEN step on a running 1 kHz square changes the very next step (counter / phosphor: no short or long
pulse). Regression signs: a 0x6E / 0x6F / 0x76..0x7D / 0x42 in the event log's write trace, the generator restarting on
a frequency change.

**Open**
- fpga-d3 confirms the start latency (8..9 clocks) from tb_square; the model assumes 8.
- fpga/al3/README.md (fpga-d3's) still describes the 0x153D pattern generator in places; the RTL agent updates it.

## 80. fw-105: fw 0.2.105, built for FPGA 0x153D (protocol 1.27, row 67)

Branch `fw-105` from main 09a4693. Commits 387ef1e (the constants: `PC_FW_VERSION` 0.2.105, `PC_FW_FPGA_VERSION` 0x153D
and its text; the host tests' words, the mismatch cases on 0x153C / 0x153E, test_fpga_bus's built-for resets on the 0x153D
model), 5c0ea67 (the logger / roll refusal), fb3a28a (the wizard / Frequency response branches), 74eff0e (CHANGELOG fold,
protocol row 67 and 5.1, README, guide), e3f3baf (mcp docs: the simulator's default 0x153D; `FW_FPGA_VERSION` was already
0x153D). gcc10 image at e3f3baf: 811,008 bytes, sha256 prefix 3f4eb195 (the commit is part of the image, so later commits
give other hashes; the coordinator pins the image at the merge). **Nothing benched, nothing flashed.**

**Removed (one bitstream: every feature check is a mismatch check)**: `fpga_log_start`'s -2 (0x38 bit 7 clear after the
arm), LOG_START's E_FPGA detail 1 "the FPGA does not stream (needs 0x153D)", roll_view's "Roll mode needs FPGA bitstream
0x153D" (pc_glue_roll_start answers 0 / -1 / -3); the wizard's CW_GAIN_NA screen and `cw_init`'s `al3` argument (every
caller passed 1); `bode_view_available`'s caps test with BU_W_NEEDS_FPGA (the later BU_W_* renumbered). Kept: the stream
bit checked at every drain step (a stream that stops is a fault: LOG state 4, roll "FPGA stream stopped"); test_log checks
it on the 0x153C model. No caps or features bit was conditional (fw-one fixed them; fw-nopat reserved 24 / 27, 12 / 13).

**Bench (owner's batch; flash 0x153D from `fpga/al3/dist` per fpga/al3/AGENTS.md with the backup first, then FEL 0.2.105)**
1. Boot: no mismatch notice; the first-run / Scope view as on 0.2.104. `#VER`: `fw=0.2.105,proto=1.27,fpga=0x153d`;
   GET_INFO 76 = 0x153D, 78 = 0; Diagnostics Status "FPGA AL3A10 bitstream 0x153d: ...".
2. Logger from the MCP: `scope_log` CH1 format 0 at 500 kS/s for 10 s on the 1 kHz post (section 74 steps 2-7: no gap,
   the square continuous across chunks, the "Logging to the host" line, "Log ended", the Scope view back in STOP).
3. Roll mode at 1 s/div, both channels on the post (or a 1 Hz signal), Auto then Normal: section 78 steps 1-2; a host
   `scope_capture` and `scope_log` while rolling (step 3).
4. Generator menu: the square at 1 kHz, then 7.3 MHz; the counter cards on CH1 read 1.000000 kHz and 7.3 MHz (the
   fractional square's mean; GET_GEN P16 / H16 and byte 22 = 1); a frequency change on a running square shows no short
   or long pulse (phosphor).
5. A Frequency response sweep (100 Hz .. 1 MHz on an RC): READ_BODE rows carry P, the -3 dB as before; the main menu row
   is live (grey only in the waveform viewer).
6. The Type row (Pulse / Nth on the 1 kHz square) and the Sample row (Peak bars, Vmax from the peaks) as on 0.2.104;
   the calibration wizard's four steps ("Step M of 4").
7. The soak: 30 min with the MCP server polling (GET_STATUS / GET_HEALTH), a log and captures in between; no restart.
Regression signs: the mismatch notice on 0x153D (the constant), LOG state 4 at once (the stream bit), roll stopping with
"FPGA stream stopped" at once.

**Open**
- `pc_bode.c` `bh_live.has_fir` still reads caps FIR (always set); `ACAL_E_GAIN_NONE` (17) and its two texts are never
  returned (the wizard's screen is gone). Both are older one-bitstream leftovers, not 0x153D checks; left for a cruft pass.
- `docs/guide/install.md` / `agent-runbook.md` / `first-run.md` name 0x153B as the release's bitstream (the last release
  text); they move with the 0.3.0 release notes, not here.

## 81. fw-left: the one-bitstream leftovers and FORCE on a sequencer run (fw 0.2.105, protocol 1.27 row 67)

Branch `fw-left` from main 24b7a62. Commits 4865cd1, 9c56216, 0c0bf4c, a3d861a (the constant capability tests),
4b0475d (`ACAL_E_GAIN_NONE`), 710f9a3 (FORCE), then the docs. `PC_FW_VERSION` / `PC_FW_FPGA_VERSION` untouched (0.2.105,
0x153D). **Nothing benched, nothing flashed.**

**Removed (constant on 0x153D, same behaviour on it)**
- `bode_hw.has_fir` (bode_sweep.h / .c, pc_bode.c `bh_live`, test_bode_sweep's initialiser) and `pc_glue_caps`, its only
  reader: FIR_OFF is flagged by the FIR path's rule (N >= 16, N % 4 == 0, the record not FIR) as before.
- pc_hal_scope.c `pc_dec_has()` (returned PC_DECIM_HAS_FIR) and its nine tests: the apply's early return, the "+FIR" name
  and the Filter tap's no-FIR step, the 0x29 readback, the fingerprint byte, the memory fits; `fpga_decim_write`'s
  `has_fir` argument (every caller passed 1; 0x56 is written whenever it differs from what the FPGA holds).
- `PC_FEAT_GEN_DDS` / `PC_FEAT_EXT_TRIGGER` / `PC_FEAT_DECIM` (pc_glue_ui_has answered 1): the generator button, the EXT
  source box and the Filter box touch tests in statemachine.c, `PC_MASK_TRIG_CHANNEL` and its menu.c hook, the Filter
  row's guard; spectrum.c's alias hint always names the Filter (firmware/README.md's message table follows). Kept:
  `PC_FEAT_RTC` (the module) and `PC_FEAT_MEM_BASE` (the setting).
- SET_DECIM filter 4's `PC_CAP_PEAK_DETECT` test (E_UNSUPPORTED "peak detect" unreachable).
- `ACAL_E_GAIN_NONE` and its texts (adc_cal.c "gain: no generator", cal_wiz.c "no generator in this FPGA bitstream");
  17 is a reserved comment, 18 / 19 keep their numbers. The wizard result codes are not on the wire and not saved
  (GET_CAL / GET_HEALTH / CAL_PROBE carry none; docs/protocol.md lists only the texts of the gain checks in row 51), so
  renumbering would have been safe, but nothing is gained. docs/guide/calibration.md never listed it.
- Kept on purpose: the dispatch table's per-command caps test (wire semantics: a command whose cap is clear answers
  E_UNSUPPORTED "capability", e.g. FORCE in release builds, READ_LIVE, ACQUIRE_AVG), and `pc_decim_config`'s `has` input
  with `pc_decim_has()` (the pure maths the host tests and the mcp mirror exercise at the older levels; GET_DECIM why 6
  is its wire value).

**FORCE on a sequencer run** (section 76 "Open"): refused with E_STATE detail 0 "sequencer run: STOP ends it" while a
run is armed (`s->cap_next.seq_mode`, the test STOP uses). Why the refusal and not restoring 0x0E: STOP ends a run with
`pc_hal_seq_stop` (0x5D = 1, no restarts, then 0x1A = 0 against the arm's 0x0E), i.e. it never touches 0x0E and ends
the run, not a segment; restoring 0x0E after a force would still leave 0x1A = 0, turning a normal-mode run's later
segments into auto segments (the pre-fw-small behaviour), which nobody asked for. No detail code for "sequencer
running" exists; detail 0 is FORCE's state detail ("not armed"), so a host that handles FORCE's E_STATE needs no change.
PC_DEBUG builds only (release builds answer E_UNSUPPORTED as before). test_session (debug pass): FORCE on an armed
circular run answers E_STATE 0, no force and no stop reach the HAL, STOP then ends the run as before. docs/protocol.md
FORCE paragraph and the E_STATE row say so.

**Tests**: `make -s test` all green (test_session 73149 / 73151, test_bode_sweep 2229, test_cal_wizard 655, test_fpga_bus
10440), check_vectors OK, check_retired_regs OK; `make -s gcc10` without new warnings in the touched files (menu.c /
statemachine.c show only the upstream unused-variable / implicit-declaration ones), the PC_DEBUG build links. gcc10
image at 710f9a3: 810,496 bytes, sha256 10c1b294 (later commits give other hashes).

**Bench (owner / coordinator, FEL)**: nothing new to see on 0x153D beyond section 80's list; check that the generator
button, the trigger menu's EXT box and the ACQ Filter box still answer taps, the Filter steps off, boxcar, boxcar+FIR,
CIC2, ... as before, and (PC_DEBUG build) SET_SEQ circular, ARM, FORCE: E_STATE 0, then STOP: DONE.

**Open**
- `pc_decim_config`'s `has` levels (NONE / CIC, `PC_DECIM_WHY_NOCAP`, `c->cap` = 0) remain for the maths tests and the
  mcp mirror (`protocol.py` DecimConfig why 6); dropping them means test_adc_cal / test_session / decim_check cases at
  HAS_CIC / HAS_NONE and the mcp side together: a cruft pass with the mcp agent.
- Comments in pc_decim.h (PC_DECIM_HAS_* history, "on bitstreams without the FIR the bit is ignored") describe those
  levels and stay with them.

## 82. fw-diag: the mismatch notice after Diagnostics (fw 0.2.105, protocol unchanged)

Branch `fw-diag` from main 8f17c9d (fw 0.2.105). Brief: the owner's 17:4x bench entry (0.2.104 FEL-loaded on 0x153B: the
notice, Diagnostics, Back, then no response to touch; power cycle). Found by reading the code; nothing benched.

**Cause.** The notice (`ui_fpga_mismatch_pass`, ui_diag_view.c) and Diagnostics share one `ud_pg` (the page on the screen
and its hit areas). UD_ID_DIAG opened Diagnostics without asking for a rebuild; Diagnostics' Back (`ud_close`) copies the
notice's picture back from displaybuffer2, but `ud_pg` still held the Diagnostics Status page, and the notice rebuilds
only on `fm_redraw`. On a mismatch that page has Report (x 164..360, y 420..464), the tabs and Back at the top and no
Calibrate, so the centres of Diagnostics (x 124), Settings (400) and USB drive (676) hit nothing: no press, no redraw, the
screen looked hung. A tap on the Report area or the top row would have redrawn the notice. Settings and USB drive set
`fm_redraw` on return and were fine. The host cannot close Diagnostics on a mismatch (pc_dispatch refuses every non-basic
command before `pc_hal_ui_leave_screen`), so Back is the only way out of it.

**Second fault on the same screen.** `pc_glue_ui_service` ran `acal_live_service` (the calibration class probe) under the
notice: on a calibrated unit with a quiet (empty) trace buffer it arms conversions every 30 s through `fpga_arm_reset`'s
unbounded ready wait and the `fpga_done_conversion` poll. On a bitstream the firmware is not built for that wait has no
guaranteed end. Not proven to be the owner's hang (the notice answered before Diagnostics), but it broke the rule that
nothing acquires while mismatched.

**Fix (f08b5b1).** UD_ID_DIAG sets `fm_redraw` when it opens Diagnostics, so the first notice pass after Back builds the
page and its hit areas again. `acal_live_service`'s gate adds `!pc_glue_fpga_mismatch()`. Checked: the other FPGA waits
(`fpga_do_conversion` callers, `fpga_arm_reset`, `fpga_log`) are reachable only from the scope pass, the wizard and volts
calibration (hidden on a mismatch), roll mode or the host arms (E_UNSUPPORTED); Settings' Restore writes registers but
waits on nothing; USB drive's loop waits on the touch only.

**Host test.** None: ui_diag_view.c (display, touch, buffers) is not built in host_tests; `ud_build_fpga_mismatch` and
`ud_hit` are (test_ui_diag), the pass's state is not.

**Bench (owner; FEL load of a 0.2.105 image on a bitstream other than 0x153D, e.g. the unit's 0x153B; nothing flashed)**
1. The notice shows. Diagnostics, then Back: the notice; tap the centre of Settings: Settings opens; Back: the notice.
2. USB drive, Back (Exit until fw-usbback, section 85): the notice. Diagnostics, Calibration tab, Log tab, Back: the notice, its three buttons answer.
3. Leave the notice up for 2 minutes, then tap each button: it answers every time (the probe no longer runs).
4. While on the notice the MCP still gets GET_INFO (76 = 0x153D, 78 = 1) and ARM answers E_UNSUPPORTED detail 1.

## 83. fw-fullrate: one calibration clock class, FPGA 0x153D's full-rate encodes (fw 0.2.105, protocol 1.27 row 67)

**Superseded by section 91 (fw-gated, 2026-10-22): the single class, the class probe's removal and the host-test model's
full-rate write clock are reverted with the full-rate encodes; the encode check on every capture stays.**

Branch `fw-fullrate` from main 895bff7 (fw 0.2.105, `PC_FW_VERSION` and `PC_FW_FPGA_VERSION` 0x153D unchanged). Brief:
the owner's decision of 2026-10-21 17:59 (docs/plans/task-queue.md "## Firmware"): the 0x153C counter is exact at full
rate but miscounts with Filter Off at divided rates, because Atlan's K = 0 sample-skipping path gated the ADC encodes with
the divider; in 0x153D the encodes always run at 200 MSa/s (fpga-d3 changes the RTL). Register interface unchanged: at
K = 0, D > 0 the FPGA stores one pair per D + 1 write-clock edges (B 5 ns before A), same geometry, rates and record
layout; the trigger compares the stored words. fpga-d3 also confirmed: 0x33's encode check runs every clock at every
rate (its prediction part becomes trivially true), the 0x36 outlier counters compare every write-clock edge's pair at
every D and K, the 0x05 ready wait at slow rates is six write clocks (60 ns), the sequencer timestamp steps by 2 (5 ns
units) per write clock. Nothing benched. Design and wire: firmware/README.md section 6.6, docs/protocol.md row 67.

**Calibration: one class** (c8649f4). `adc_cal.h`: `acal_channel` is zero / slope / noise [core][range] and gain [core];
`acal_correction` has no divider; `acal_corr.cls` is gone. The wizard (`cal_wiz_ui.c`) keeps the 2 MSa/s centre search
(`CAL_RATE_SEARCH`: coarse points, slope points 150 counts either side, refined centre; `acal_range_meas.search16_lo/hi`,
not stored) and stores the 200 MSa/s points (zero 64 conversions, now with 60 ms settle as the gated zero had, slopes);
the gain step measures low / high / low at 200 MSa/s only. One noise limit `ACAL_MAX_NOISE16` (3 codes), also for the
training's quiet-input check (1 code up to 0.2.104). Retired: `ACAL_E_CLASS` (11), `ACAL_E_GAIN_CLASS` (16),
`acal_readout_fault` (the gated-twin rule; `ACAL_E_READOUT` is only the training's now), CAL_PROBE point 4 (never
measured). `acal_point.rate` / `acal_fail.rate` (`ACAL_RATE_FULL` 0, `ACAL_RATE_SEARCH` 1) replace `cls`, with the same
wire values. `fpga_channel_correction_full` is gone (the peak path takes `fpga_channel_correction`).

**Gain scale (decision).** `acal_gain_store` sets G'_k = (G_A span_A + G_B span_B) / 2 / span_k with the gains in use
(unity when never measured): the mean corrected span stays, so `input_calibration` (fitted on the gated scale by
0.2.45-0.2.104) keeps its fit. An old block's full-rate gains already carry that scale (about 1.1 % up). A unit that
never had measured gains reads about 1 % differently than its volts calibration was fitted on; a volts calibration
fixes that (no firmware workaround).

**Storage and wire (decision).** Layout unchanged, so no new protocol minor: the v3 block keeps its two class slots, slot
1 written 0, header classes 1 (`ACAL_BLOCK_CLASSES`); `acal_unpack` takes classes 1 and 2 (slot 0 used): old cards keep
their calibration, no forced recalibration, the next Save writes classes 1. Classes 1 makes a firmware before 0.2.105
reject the block (1.00o5 compensations) instead of reading zero gated zeros and gains on a downgrade. GET_CAL 364 bytes:
73 bit 2 reserved, the slot-1 bytes 0 (`acal_pack_channel`), 256 bits 5..6 and 280..287 reserved. `adc_cal_v3` vector
regenerated (gen_vectors.py: classes 1, channels as [core][range]).

**v4.** The class probe is removed (`acal_cm_*`, `PC_CAL_CLASS_PROBE`, `acal_live_service`, `FPGA_WHO_PROBE`, the
"Full-rate offset" line) (5aafcdb). The tracker's d / e apply to every 8-bit readout (`acal_track_apply` needs version 3
only); it learns from divider-0 records only (`acal_live_prepared`: `fpga_divider == 0`), the evenly interleaved trace
its neighbour interpolation assumes. GET_CAL 256 bit 4 = v3 and the 8-bit path at any divider.

**Diagnostics.** Status: "Core gain split, A / B" (|G_A / G_B - 1|, limit `UD_LIM_SPLIT_TENTHS` 8.0 %, the gain step's
`ACAL_GAIN_MAX_SPLIT`) and "Noise RMS, inputs open" (`UD_LIM_NOISE`, 3.0 code); the class gain ratio and the gated
noise column are gone. Calibration page: one zero column, "Gains A .., B .., range ..", "Noise RMS, inputs open: 1.19
code, limit 3.0 code".

**Counter.** No firmware condition suspected the counter at divided rates (`fpga_counter.c`, `ui_meas.c` card rule,
`pc_glue_counter_pass`): the cards take it whenever a current result with a period holds. The rate-change rule
(`fpga_bus_wr.wclk`) stays: whether a 0x0D write still disturbs the counter's stream on 0x153D is the bench's question.

**Host-test FPGA model** (f0f1f06). `fpga_model.c`: on 0x153D's legacy path the write clock runs at 10 ns and a word is
stored on every (D + 1)-th edge (`fm_store_edge`, `fm.wphase`); `fm.raw_words` makes the words the synthetic stream's
pairs (`fm_fullrate_pair`: pair (D + 1) w, B sample 2p, A 2p + 1); `test_fpga_bus` `test_fullrate` checks D 0, 4, 399
and the unchanged geometry against 0x153C. `fm.e` counts stored-word edges, so the trigger and stop rules are as before.

**MCP host** (0e7a0a4): mcp/AGENTS.md "fw-fullrate".

**Encode check over every capture** (last commit of the branch, on fpga-d3's confirmation). `fpga_encode_after_capture`
(enc_heal_hw.c) and `pc_enc_rearm` (pc_cmds.c) skipped every capture at divider >= 1 (and host 16-bit ones), on the
premise that the encodes were gated there. Now the FLAG check (0x33 bit 0) runs after every capture, any divider, filter
or peak; the DATA check (after a rate write, or an unknown encode convention) stays divider-0 legacy only, its core A
neighbours being 5 ns apart only there, so such captures go unchecked as before. A garbled divided-rate capture is
re-armed (at most `EH_MAX_TRIES`, at the slowest time bases that can take seconds; only on a real fault). The 0x36
self-check (`fpga_capture_check`) runs after every accepted capture with it. test_session: the divided-rate FLAG re-arm
and the divided-rate DATA skip. The MCP simulator still models the check at divider 0 only (not needed by any host
logic).

**Bench (owner, after flashing 0x153D; FEL load of this build)**:

1. Calibrate with both inputs open (probes off, USB cable away from the inputs): the zero step passes on both channels,
   then the gain steps with the probes on the post pass; Save. Diagnostics Status: OK, core gain split and noise within
   their limits; Calibration page shows one zero column and "Gains A .., B ..".
2. A steady level (the calibration post held static or a DC source, CH1 range 2): the trace mean and the per-core means
   (READ_RAW or READ_TRACE odd / even) read the same codes at 200 MSa/s and at 500 kSa/s with Filter Off, within noise
   (0.1 code); the same with CIC2 at 500 kSa/s. Before 0x153D core B dropped 3.6 codes at the divided rate.
3. The counter with Filter Off at 500 kSa/s: the generator at 1 kHz reads 1.000 000 kHz exactly, at 1 MHz 1.000 000 MHz
   exactly (Freq card and the host's counter result); also right after changing the time base (the rate-change rule).
4. A card calibrated by 0.2.104 or older, before step 1: it loads (Diagnostics: calibrated, no "run Calibrate"), volts
   on a known DC level match the 0.2.104 reading at 200 MSa/s within 0.2 %.

## 84. fw-acqui: trigger keys, five-column ACQ grids, Filter Off / Hi-Res / Anti-alias (fw 0.2.105, protocol unchanged)

Branch `fw-acqui` from main 408d3e2. Brief: the owner's UI tweaks from the 0x153C bench (2026-10-21 18:38) and the queue
item "ACQ Filter row: three modes" (owner 16:29-16:38), no mockup round. Commits: 06a7d24 (trigger keys), 43e513a (ACQ
grids), e5ad32d (Filter three modes), 178af30 (defaults), 311e3c2 (docs, CHANGELOG), 36f8a53 (migration of the old
defaults, owner decision relayed by the coordinator), then this section. `PC_FW_VERSION` stays 0.2.105. **Nothing
benched**; no host renderer of menu.c exists (docs/mockups/gen_ux is Python mockups), so the screens below are for the
coordinator's unit screenshots.

**What was built**
- **Trigger keys** (menu.c `trig_row`): the - / + glyphs in font_5 (24 px, the nearest guideline size to twice 16 px;
  font_7's 32 px is not a guideline font). font_5's + is 11 x 11 px with a 3 px stroke, - 6 x 3, against font_3's 6 x 5
  and 3 x 1: about twice. Each bar is on the key's middle row (y + 9 for -, y + 11 for +; the glyphs' bars sit on font
  rows 12..14 and 10..12). Keys stay 30 x 36, hit areas the row's halves (110 x 44, statemachine.c), unchanged.
- **ACQ grids** (ui_ctrl.h `UI_ACQ_GRID_*`, `ui_acq_grid_at`; menu.c `acq_grid_cell`; statemachine.c hits): five
  columns, cells 74 x 25 on a 78 x 28 pitch from x + 8; rates (18) in rows of 5/5/5/3 from y + 33 (last ends y + 142,
  rule y + 155), time/divs (24) in 5/5/5/5/4 from y + 241 (last ends y + 378, menu 386). The row's boxes widen 90 -> 92
  (`UI_ACQ_BOX_W`) so row, grids and the rule at y + 155 all span x + 8 .. 394; the Memory column (rule x + 402) is
  untouched. Texts centred on measured widths (widest 61 px, font_2), so 1.00o5's `*_text_x_offsets` tables are gone;
  the headings and the Roll box (pc_glue_roll_label) are centred too. Hit area = drawn cell; gaps take nothing (as before).
- **Filter** (ui_ctrl.c `ui_filter_name`, `ui_filter_next`, `ui_filter_migrate`; pc_hal_scope.c `pc_glue_decim_tap`,
  `pc_acq_row`, `pc_ui_fix_settings`): names "Off" (K 0, FIR bit ignored), "Hi-Res" (boxcar), "Anti-alias" (CIC3+FIR),
  host modes by name ("CIC2", "CIC3", "boxcar+FIR", "CIC2+FIR"). **Press rule**: Off -> Hi-Res -> Anti-alias -> Off;
  from any host mode -> Off (the cycle's start: never jumps to a mode the user did not see coming). Defaults
  (pc_decim.h): Scope Hi-Res (no FIR overshoot, the 10-19 reason for CIC2), Spectrum Anti-alias. **Saved words**: the
  old per-mode defaults (207 = CIC2, 212 = CIC2+FIR) load as the new defaults (owner); every other value as it is, shown
  by name; the 0.2.30..0.2.63 single-word path goes through the same rule. SET_DECIM / GET_DECIM, the MCP names and the
  vectors are unchanged. Spectrum warning: "Aliasing possible: set Acquisition > Filter Anti-alias" (buffer 160 -> 320
  chars: all warnings at once could pass 160). No Diagnostics text names the filter.
- Tests: test_ui `test_acq_grid` (geometry, fit, hits, no overlap with the row), `test_acq_filter` (names, presses from
  every mode incl. the Peak bit, the cycle, defaults, migration), `test_acq_row` with the new names and 92 px boxes;
  test_peak_bars names CIC2 itself. `make -s test` green; GCC 10 build without new warnings in the touched files.

**Bench (coordinator, FEL load; screenshots in the handoff)**
1. Trigger menu, Pulse: Width row's - and + clearly larger, centred in their keys; Nth: N and Idle rows the same; taps
   on either half step as before.
2. ACQ menu (Scope, running): Sample Rate 5/5/5/3, Time per Division 5/5/5/5/4, both grids flush with the Roll .. Sample
   row (x 258 .. 644 on screen) and the rule; headings centred; Memory column unchanged. Tap a rate / time/div in each
   column and row incl. 500Sa/s and 5ns/div (grey one: nothing); Stop: rates grey.
3. Filter: Off -> Hi-Res -> Anti-alias -> Off (green while on; 9000+ chips hidden while on). Sample Peak: Filter grey
   with its value ("Hi-Res"), the note as before. Spectrum: own Filter (default Anti-alias), warning with Filter Off
   below 200 MSa/s reads "... Filter Anti-alias".
4. Host: `scope_set_decimation filter=cic2` -> box "CIC2"; tap -> "Off"; `cic2+fir` -> "CIC2+FIR"; `boxcar` -> "Hi-Res";
   `cic3+fir` -> "Anti-alias". GET_DECIM matches each.
5. A unit whose sector holds the old defaults (the owner's): after the FEL load Scope shows "Hi-Res", Spectrum
   "Anti-alias"; Restore default settings: the same. Save with a host mode (e.g. CIC3), power cycle: "CIC3".

**Open**
- The Frequency response's own Sweep rows keep 16 px - / + keys (not asked); align them if the owner likes the trigger's.

## 85. fw-usbback: the USB drive screen's Back in the header (fw 0.2.105, protocol unchanged)

Branch `fw-usbback` from main 88c3705. Brief: the owner at the bench, 2026-10-22: the USB drive screen gets Back at the
top right like the other screens, and the Exit button at the lower right goes. Commit c2694f3 (code, test), then the
docs commit with this section. `PC_FW_VERSION` stays 0.2.105. **Nothing benched.**

**What was built**
- **One header for the full-screen pages**: Settings' `draw_header` is now `ui_page_header(title, subtitle)`
  (ui_settings_ui.c, declared in ui_settings.h) with `ui_page_back(pressed)`; the pure `ui_page_back_hit(x, y)`
  (ui_settings.c) is the UI_SET_BACK_* rectangle (692, 4, 96 x 44), used by `ui_set_hit`, the time page and USB drive.
  Diagnostics draws the same header through its page model (ui_diag.c `ud_header`, UD_BACK_X 692), unchanged.
- **USB drive** (menu.c `scope_setup_usb_screen`): `ui_page_header("USB drive", 0)` replaces the dark blue band and its
  title at 14, 13; the USB_EXIT_* button and defines are gone; the picture and the three lines stay where they were;
  line 3 reads "Eject the drive on the PC before tapping Back." A touch on Back draws it pressed; with `mounted_to_PC`
  "Not allowed!" (4 s), then after the release Back is drawn released and the screen stays; otherwise exactly Exit's
  path: `usb_device_disable`, `USB_CH340 = 1`, `usb_device_init`, `usb_device_enable` (CDC back), wait for the release,
  `pc_glue_ui_hold` released, `scope_sync_thumbnail_files`. The loop has no `PC_UI_YIELD` (USB drive mode keeps E_BUSY,
  docs/ui-guidelines.md section 4). The callers are untouched: the main menu row (statemachine.c) and the mismatch
  notice (ui_diag_view.c UD_ID_USB) copy their screen to displaybuffer2 and back; the notice sets `fm_redraw`, so its
  page and hit areas are built again after Back (section 82).
- Host text: mcp `protocol.py` busy hint for detail 5 says "leaves it with Back on the scope (Exit on firmware before
  0.2.105)"; docs/guide/screens.md "USB drive", docs/agent-measurement-guide.md (E_BUSY row) and the CHANGELOG say Back.
- Tests: test_ui checks `ui_page_back_hit` over the whole screen against the rectangle and that "USB drive" and the two
  long lines fit (title clear of Back, lines inside x 790). GCC 10 build without new warnings in the touched files;
  `make -s test` and the vector check green.

**Bench (owner, FEL load; nothing flashed)**
1. Main menu > USB drive: black header, "USB drive" left, Back at the top right where Settings has it; no button at the
   lower right; the last line says "tapping Back". The PC sees the drive; the COM port is gone.
2. With the drive still mounted on the PC, tap Back: Back lights while touched, "Not allowed!" for about 4 s, Back drawn
   normal again, the screen stays. Eject on the PC, tap Back: the scope screen as it was, the COM port is back and the
   MCP answers PING / GET_INFO.
3. FPGA mismatch notice (an image on a bitstream other than 0x153D) > USB drive > Back: the notice, and its three
   buttons answer (Diagnostics, Settings, USB drive again).
4. Settings and its Time and date page: Back still lights and leaves as before.

**Open**
- The mockup artboard UsbDrive (docs/mockups/gen_ux/boards.py) still draws the Exit button and "tap Exit"; regenerate it
  with the header Back when the mockups are next refreshed.

## 86. fw-stoprate: ACQ rate taps in STOP, the setting for the next RUN (fw 0.2.105, protocol unchanged)

Branch `fw-stoprate` from main a1217d2. Brief: the owner, 2026-10-22 08:56 (queue item "ACQ rate changes in STOP"): the
Sample Rate grid ignored taps in STOP ("rather unintuitive"); behave like bench scopes. Commit c4025cf (code, test), then
the docs commit with this section. `PC_FW_VERSION` stays 0.2.105. **Nothing benched.**

**What was built**
- **Pending rate** (acq_view.c/h): `acq_view_rate_tap(r)` running or in Single (`acq_view_rate_free`, unchanged) sets
  `scopesettings.samplerate` and returns ACQ_SET_RATE (the handler writes 0x0D and runs `acq_view_fit(1)`, as before);
  stopped it stores r in a static `acq_rate_pending` (cleared when r is the record's rate) and returns 0: nothing
  written. **`scopesettings.samplerate` stays the record's rate while stopped**, so every record path keeps the capture
  geometry without change: `scope_display_trace_data` / `tbv_window` (xpps), the slider (menu.c `tbv_slider`), the
  measurements and cursors (`freq_calc_data` / `time_calc_data[samplerate]`, fpga_control.c frequency maths), the
  stopped zoom (`acq_view_zoom(dir, 0)`), the wave file header (`scope_prepare_setup_for_file`), GET_TIMEBASE, the
  phosphor hash. Only the *setting* readers use `acq_view_rate_setting()`: the grid's green cell
  (`scope_acquisition_speed_select`), the top bar's ACQ chip (`scope_acqusition_settings`) and the settings sector
  (`scope_save_config_data`; the load's fit snaps the time/div if needed).
- **Chip / record resolution**: the chip shows the setting (the pending rate), like a bench scope's horizontal readout
  in STOP; the time/div text beside it stays the record's zoom. The record's rate stays in the trace, cursor and
  measurement maths. Nothing on screen names the record's rate separately while one is pending (not asked; the
  measurements are right either way).
- **Time/div boxes while a rate is pending** (`acq_view_tb_ok`): white = viable for the record's rate (a tap zooms the
  record, as before) **and** for the pending rate (RUN keeps it without a snap). Both are needed: a box only the new rate
  allows would draw the frozen record outside what tb_view guarantees (a sliver, or under ten samples per division),
  and a box only the record allows would snap at RUN. The plot's tap zoom still steps through the record's rate only;
  a time/div it reaches that the new rate cannot show snaps at RUN with the notice.
- **Arm** (scope_functions.c `scope_acquire_trace_data`, inside the UI arm's condition, before the conversion):
  `acq_view_rate_arm()` moves the pending rate into `samplerate`; then `fpga_set_sample_rate`, `acq_view_fit(1)` and the
  top bar redraw: the state is the one a tap while running leaves (test_acq_view checks it for every record rate,
  time/div, pending rate and depth). Not in roll (a pending rate waits for the short time bases).
- **A rate written wins**: `fpga_set_sample_rate` calls `acq_view_rate_drop()` (host SET_TIMEBASE, AUTO SET, settings
  load / DEFAULT, Diagnostics and the wizard restoring the rate, a time/div box or zoom step that moves the rate in
  Single), and so does the raw divider path of `pc_hal_timebase_write`. A pending rate is UI-only: the host neither sees
  nor sets it; a host arm in STOP uses the FPGA's rate.
- **Roll**: the grid's rule is now `acq_view_rate_live()` = not roll, for drawing and taps alike. Before, the grid was
  grey in roll but the handler took taps while running (`runstate || triggermode == 1`).
- **Point 4 of the brief (other ACQ settings locked in STOP)**: none. Roll, Average, Filter, Sample and Memory take taps
  in STOP today and only change what the next arm uses. Not the same cause, so untouched, but note: Memory, and Filter /
  Sample through `pc_dec_fit_memory`, change `fpgasettings.totalsamples` at once, which the frozen record's window maths
  read (a depth tap in STOP re-windows the old record). Single after its shot counts as free (fw 0.2.76's rule), so a
  rate tap there is applied at once and redraws the held shot at the new rate's geometry, as before.
- Tests: test_acq_view `test_stopped_rate` (2.49 M checks in the file): no change to the record's rate / time/div /
  trigger point on a stopped tap, the setting, the boxes as the intersection, the stopped zoom keeping the record's rate,
  the arm equal to the running tap + fit, tap-back and last-tap-wins, drop, Single, roll, out of range. `make -s test`
  and the vector check green; GCC 10 build without new warnings in the touched files.

**Bench (coordinator, FEL load; nothing flashed)**. CH1 on the 1 kHz calibration square, Auto trigger, memory 3000.
1. RUN at 1 MSa/s, 200 us/div, then STOP. ACQ menu: all 18 rates white. Tap 200 MSa/s: it turns green, 1 MSa/s back
   to grey cell / white text; the chip reads 200MSa/s; the trace behind the menu unchanged. Time/div grid: all grey but
   the green 200 us (no time/div is viable both for a 3000-sample record at 1 MSa/s and at 200 MSa/s). Close the menu:
   trace, slider window, T CURSOR readouts and the Frequency / period cards unchanged (1000 Hz).
2. Tap RUN: within one pass the chip shows 200MSa/s, the time/div snaps to 1us with "Time/div set to 1us for this rate",
   the trace fills the screen.
3. STOP at 200 MSa/s 1 us/div; ACQ: tap 50 MSa/s, then 200 MSa/s again: the pending choice goes (200 green). Tap
   20 MSa/s, close, tap the plot's halves: the zoom steps only through what the 200 MSa/s record shows. AUTO SET (or
   MCP `scope_set_timebase`): the pending rate is gone, the chip shows the rate written.
4. STOP, pick a rate, power off through the menu, on: the picked rate is loaded (settings sector), time/div fitted.
5. Single: after the shot, a rate tap applies at once (as before). Roll (Long mode): rates grey, taps do nothing.
6. Spectrum view, STOP: tap a rate: the spectrum's axis stays the record's; RUN: the new rate's axis.
Bug indicators: the stopped trace or the frequency card moving when a rate is tapped; a white time/div that changes
nothing or draws a sliver; RUN arming at the old rate; a pending rate surviving AUTO SET or a host SET_TIMEBASE.

**Open**
- Memory / Filter / Sample taps in STOP re-window the frozen record (above); the same pending treatment for the depth
  would close it if the owner wants it.

## 87. fw-capstage: the capture stage pick, a stronger training, the runtime guard (fw 0.2.105, protocol 1.27 additive)

Branch `fw-capstage` from main a1217d2, commit 76fbaa2 (fw 0.2.105, `PC_FW_VERSION` and `PC_FW_FPGA_VERSION` 0x153D unchanged). Not
benched. The design is in `cap_stage.h` (sections "Training", "Pick", "Runtime guard").

**Cause** (docs/hardware-status.md 2026-10-22 08:35-08:50, 0x153D, owner's unit): the wizard with the inputs open left
CH1 A4 B4, CH2 A3 B4 (the power-on 0x08E4). On a real signal (CH1 on the generator post, 200 MSa/s) CH1 A at stage 4
counted 146-177 outliers in a few captures and the counter read 2 kHz on 1 kHz; at stages 2, 3 and 5 it counted 0 over 30
captures from 1 kHz to 7.3 MHz. The quiet band training (6 points every 4 codes, 2 captures) found 2..5 clean, and
`cs_pick`'s last tie-break took the power-on stage 4. Section 54's 2026-10-20 note already had CH1 A4 garbling only at
codes 127..135. With 0x153D's encodes at full rate at every rate (section 83), every capture and the counter need all
four cores clean, so a marginal stage matters at every time base.

**Rule** (`cs_pick` / `cs_tie`): among the clean stages, the widest run, then 3 or 4 before a reserve, then the distance
to the nearer end of the run, then the lower stage; no power-on term (`cs_pick(clean)` lost its `po` argument). Two
equal centres (2..5 clean, {3, 4}, also {2, 5}) are a tie: `cs_tie` names them and the training measures them again (the
tie pass), drops a stage that counts there and picks again; two that stay clean give the lower one. "The centre whose
outer neighbour is clean too" never separates the two centres of a widest run (both outer neighbours are clean in a run
of 4, both outside the run in a run of 2), so the evidence is the tie pass and the fallback the lower stage (inference:
the only stage seen garbling beside clean ones on this design, twice, was the upper centre CH1 A4). Results: 2..5 -> 3,
{2,3,5} -> 3, {2,3,4} -> 3, {3,4,5} -> 4, {3,4} -> 3, {4,5} -> 4, {2,5} -> 2. Expected on the owner's unit: 0x08E3 (CH1
A3 B4, CH2 A3 B4).

**Training strength**: `CS_BAND_STEP` 2 and `CS_BAND_POINTS` 11 (codes 120, 122, .. 140), `CS_BAND_REPS` 4; the tie pass
(`cs_tie_pass`, `cs_io.tie_reps` = `CS_TIE_REPS` 16) parks at codes 130, 128, 126 (`CS_TIE_CODE`, `CS_TIE_POINTS`; from
the top down, the band pass ends at 140) and captures each tied core at each of its two stages 16 times per point, the
other cores at their pick. `cs_train` has `tie`, `tie_sum` and `set` (the clean set of the timing in use after the tie
pass). Mock (test_cap_stage `test_tie`): a stage that garbles 5 % of the quiet captures at 126..130 passes the band's 12
tries there and is caught by the tie pass's 48; the 0.2.81 band misses it.

**Wizard time**: the host model (test_fpga_bus `test_train_capture_cost`, 0x153D, 0.30 us per strobe) puts one training
capture at 92 us of bus and acquisition (8000 words = 80 us); the target's done poll also scans the touch panel each
poll and the arm writes the filter and sequencer registers (not modelled), so section 54's 0.3-0.5 ms per capture stays
the budget. Per channel: band 11 x 60 ms settling + 176 captures (was 6 x 60 ms + 48) and, with a tie, 3 x 60 ms + 96
captures: about 0.75 s + 0.23 s = 1.0 s with 0.5 ms captures (0.85 s with the model's), was 0.38 s; a B core's reference
pass adds about 0.75 s more. The wizard grows by about 1.2 s for both channels. `CAL_BAND_SETTLE_MS` stays 60 (2-code
steps now; the tie pass's first park is 10 codes).

**Moving signal without a probe: not possible.** The only source the wizard can drive into an open input is the offset
PWM (25 kHz, 40 us period, research/notes/fpga-verilog-analysis.md 7.5), filtered by the 2 x 1 kOhm and three-capacitor
network into the ADC node (research/notes/hack-repo-hardware-fpga.md); its settling is tens of ms (the 60 ms per point),
so over one 40-80 us capture the input moves by far less than a code per sample, and the ripple is below the noise. The
garble needs large sample-to-sample code changes (a square edge); the generator sits on the calibration post, which
reaches an input only through a probe. The quiet training can therefore only sample the codes near 0x7F / 0x80 densely,
and the runtime guard covers what it still misses.

**Runtime guard** (`cs_guard`, `cap_hw.c` `cap_guard`): when the repeat watch (3 flags within 60 s) fires for a core while
`PC_CAPF_TRAINED` holds (the stored value of this bitstream is in effect) and the stored clean sets are known, the core's
stage is removed from its stored set and `cs_pick` chooses again from the rest; at most once per core per boot
(`cap_moved`); 0x63 is written, `cap_st.stored` and the sets updated, `PC_CAPF_MOVED` (GET_HEALTH 106 bit 6) set, event
23 a = 0x20 | moved cores, b = the new value, and `cs_watch_forget` restarts that core's watch (a repeat at the new stage
sets REPEAT: Calibrate). A core with nothing left is not moved and gets REPEAT as before. The set only shrinks and never
grows outside the training, so nothing oscillates, also not over boots; the wizard's Save stores a fresh set and clears
MOVED. Storage at once: `fpga_capture_service` (called from `pc_glue_ui_service`, MAIN context) reads sector 708, packs the
trailer and the sets into bytes 488..511 and writes it back (nothing else in the sector changes; a failed read writes
nothing and the next calibration save stores it). Inference, not benched: a large signal near 100 MHz could make the
two cores' counts differ by itself and move a core once; the worst case is another stage the training passed.

**Stored sets** (`cs_ext_pack` / `cs_ext_unpack`, sector 708 bytes 504..511, after the unchanged v1 trailer): u16 sets
(core k's clean stages 2..5 in bits 4k..4k+3, `cs_sets_*`), u16 0, u32 zlib.crc32 of bytes 488..499 then 504..507. The
CRC skips the trailer's own CRC on purpose: a CRC-32 over data followed by its CRC is the same constant for every data,
so a CRC over 488..507 would not bind the sets to the value (test_trailer checks the binding). A 0.2.79..0.2.104 save
clears the bytes: no sets, no guard, until the next training. 0.2.79+ read the trailer as before (downgrade safe).

**Diagnostics / report / host**: the Calibration page line reads "(trained, a flagged core moved): ..." with MOVED; the
report file adds "; a flagged core moved to another trained stage this boot"; mcp `CAPF_MOVED`,
`describe_capture()["moved_by_guard"]`, event 23 a bit 5 "adc_capture guard moved CH1 A: now ...". docs/protocol.md row
64 has the band, the tie pass, bit 6, event 23 bit 5 and bytes 504..511 (additive, no minor bump).

Tests: test_cap_stage 226 (pick and tie table, the unit mock with the new band, `test_tie`, `test_guard` incl. the
shrinking set and the watch reset, the sets' trailer binding), test_fpga_bus `test_train_capture_cost`, test_ui_diag
the MOVED line; `make -s test`, the vector check and mcp pytest green; GCC 10 without new warnings in the touched files.

**Bench (owner; FEL load of this build; back up SD 700-710 first: the wizard's Save and the guard write sector 708)**
1. Calibrate with both inputs open (probes off, USB cable away). The "training capture stages" step takes about a second
   per channel (was half a second). Zero level notes: "Capture stages: CH1 A3 B4" and "Capture stages: CH2 A3 B4"; Save.
   GET_HEALTH 104 = 0x08E3 with TRAINED; GET_EVENT_LOG type 23 a = 0x10, b = 0x08E3.
2. The 0x36 sweep on the real signal (CH1 on the post, 200 MSa/s, 1 kHz and 100 kHz; GET_HEALTH 108..115 after each
   capture): every core 0 outliers over 30 captures, GET_HEALTH 106 bits 5 and 6 stay 0, 120 stays 0.
3. Counter with Filter Off at 500 kSa/s: 1 kHz reads 1.000 000 kHz, 1 MHz 1.000 000 MHz.
4. Guard (optional; after step 1, with the backup of 708): SD_READ_SECTORS 708, put the trailer for 0x08E4 at 488
   (`protocol.pack_cap_trailer(0x08E4, 0x153D)`) and the sets 0xFFFF (2..5 for every core) at 504 with the CRC of
   "Stored sets" above, SD_WRITE_SECTORS with CONFIG, power-cycle: GET_HEALTH 104 = 0x08E4 with TRAINED. Then 1 kHz on CH1 for a minute: one event 23 a = 0x21, b = 0x08E3,
   Diagnostics "(trained, a flagged core moved)", GET_HEALTH 106 bit 6, GET_CAL 358 = 0x08E3, and after a power cycle
   GET_HEALTH 104 = 0x08E3. Restore 708 from the backup (or Calibrate again) afterwards.

**Open**
- The guard's trigger is the repeat watch; a capture-count trigger (e.g. 3 flagged captures in a row) would react faster
  on a steady signal. Left as is until the bench shows the minute matters.

## 88. fw-ctrauto: the counter's thresholds follow the signal (fw 0.2.105, protocol 1.27 unchanged)

Branch `fw-ctrauto` from main 449d5de. Brief: the bench of 2026-10-22 (0x153D, CH1 on the RC ladder's input node: a fast
jump then a slow settle, 89..184 codes, 1 kHz): the counter at the boot default (level following the trigger level 146,
hysteresis 6) read 2.6 to 4.3 kHz (the coordinator's run; fw-hyst's run read 9.5 to 13.7 kHz, the Freq card 15.23 kHz),
with hysteresis 16 999.998 Hz, with 32 999.996 Hz; on the bare post 6 was exact. The counter compares every full-rate
sample, so noise the displayed (filtered) record hides is counted. Commits 241e4d1 (firmware, host tests), 540e75b
(vectors), f7fa203 (mcp), then the docs commit with this section. `PC_FW_VERSION` stays 0.2.105. **Nothing benched.**
Supersedes branch `fw-hyst` (a fixed boot hysteresis 16, not merged): its simulator fix (the low threshold saturates at
code 0), its "does not reach both thresholds" caveat and its test idea (a signal too small for the thresholds gives no
valid count) are reused here.

**The rule** (`fpga_counter.c` `cnt_auto_target` / `cnt_auto_check`, header comment in `fpga_counter.h`)
- After each CH1 / CH2 result taken by the pass: span = max - min; h = max(6, round(span / 6)) (`(span + 3) / 6`), at
  most span / 2; level (the upper threshold, 0x62 byte 0) = ceil((min + max + h) / 2), the low threshold level - h, so
  the band is centred on the midpoint. A pinned field (host GET_COUNTER `what` = 1 with a non-zero level or hysteresis)
  keeps its value; an auto hysteresis under a pinned level is clamped to the level (GET_COUNTER's refusal rule).
- **Deadband**: the result fits when the thresholds it was counted at are each within max(2, h / 4) codes of the ones
  it asks for. A misfit result (`fpga_cnt.misfit`) is not VALID (GET_COUNTER flags bit 1, `pc_hw_counter.misfit`) and
  does not feed the cards (`fpga_counter_usable`, `pc_glue_counter_pass`), and the pass writes the new 0x62 and restarts
  the gate (0x61 = 0 / value, as for any threshold change), so the next result is one clean gate at them
  (`fpga_cnt.retunes`). Inside the deadband nothing is written: the noise moving min / max by a code or two between
  gates never restarts the gate (fw-hyst's author's warning). Steady state: no writes, every gate valid. After a signal
  or range change: one misfit gate (the cards show the record's value for about 100 ms), then valid.
- **No signal**: span below 12 codes (`FPGA_CNT_AUTO_MIN_SPAN`): the thresholds stay; with an auto level the result is
  misfit (its count would be noise). With a pinned level the host's choice stands (fits).
- EXT results, results dropped by the rate-change rule and a pinned level + pinned hysteresis never retune.
- Boot still writes 128 / 6 for the first gate (`FPGA_CNT_LEVEL_BOOT` / `FPGA_CNT_HYST_BOOT`); `cfg_level` and `cfg_hyst`
  are 0 (auto). The trigger level is no longer an input: `fpga_counter_pass(now_ms)`.

**Why span / 6 (not the brief's span / 8)**: at the bench node span 95 gives span / 8 = 12 and span / 6 = 16 (rounded).
6 failed badly (3 to 14 counted edges per period), 16 and 32 worked; the noise there is a few codes rms at full rate
(CH2 range 0 about 3 codes rms, `cap_stage.h`), and on a slow settle the signal dwells thousands of samples in the
band, so 12 (4 sigma) still sees Gaussian tail crossings while 16 (5.3 sigma) did not on the bench. span / 6 lands on
the smallest verified value there. The floor 6 keeps small fast-edged signals (the bare post) counted; the cap span / 2
keeps both thresholds well inside min..max. A band centred on the midpoint delays the rising and the falling crossing
alike, so a symmetric signal's duty (a sine's 50 %) is not biased by the larger hysteresis.

**Decided**
- GET_COUNTER level 0 = auto (was: follow the trigger level) and hysteresis 0 = auto (was: no hysteresis). The wire
  layout is unchanged, so no new protocol minor; the 1.26 / 1.27 rows in protocol.md say so. Following the trigger
  marker is gone (the auto midpoint is the better default for the cards; a marker at the edge of a signal counted noise
  or nothing).
- Misfit folded into VALID rather than a new flag bit: the host sees E / T / W and min / max anyway and can tell
  (mcp `P.counter_auto_fits`), and a bit would need a new minor.
- The simulator keeps the thresholds in effect and runs the rule once per gate; its first read assumes the gates since
  boot already tuned them (the firmware has run seconds before a host reads).

**Host tests**: test_fpga_bus `test_counter` now runs on a pinned 128 / 6 (its first pass applies the configuration:
+4 strobes, the restart counts one higher; the old trigger-level move is a host-pinned 150); `test_counter_auto` (new):
the boot gate misfit and the 0x62 = 142 / 23 trace, the next gate exact, the deadband (58..203 writes nothing, no
restart), no signal (span 8) misfit with the thresholds kept, a pinned 128 / 3 counting an 8-code square vs auto level
calling it misfit, **the bench's slow-settle node sample by sample** (fpga_model.c `fm_cnt_wave`: a per-clock source
through the Schmitt trigger; `cnt_rc_wave`: 1 kHz, jumps 89 -> 140 / 184 -> 133, settles with tau 100 us, -7..+7 codes
of periodic noise): pinned 146 / 6 counts at least twice the edges, auto retunes once and then counts 1.000000 kHz
exactly; a pinned hysteresis 10 kept with the level centred (135); a pinned level 150 with the auto hysteresis 23; EXT
never retunes. (The model's noise is bounded and uniform, so it cannot show span / 8 failing: that is the bench's
call above.) test_session: a misfit result is not VALID; the mock keeps the hysteresis in effect for a 0. Vectors:
`req_get_counter_auto` (level 0, hysteresis 0) replaces `req_get_counter_follow_trigger`; 161 vectors, check OK.
mcp: test_trig26 `test_counter_auto_thresholds` (a fake device clock steps the gates: boot tuned, the trigger level
ignored, a range change gives one misfit gate then a fitting one, pinned hysteresis / level, back to auto, no signal),
the tool test uses `level='auto'` and refuses `'trigger'`. `make -s test` green, 542 mcp tests, ruff clean.

**Bench (owner, FEL load; nothing flashed)**
1. CH1 on the RC ladder's input node, the generator at 1 kHz, trigger on CH1 anywhere: the Freq card reads 1.000 kHz
   steadily (was 2.6..15 kHz), +Duty near the record's. MCP `scope_counter(readings=10)`: `level_auto` and
   `hysteresis_auto` true, `hysteresis_codes` about 16 (round(span / 6) of `min_code`..`max_code`), `level_code` about
   (min + max + h) / 2, `frequency_hz` 1000.00 within a few mHz, `frequency_spread_hz` a few mHz.
2. Move the trigger marker to the top and bottom of the signal: the counter's level does not move, the card stays
   1.000 kHz.
3. CH1 on the bare generator post: 1 kHz and 1 MHz exact (1.000 kHz / 1.000 MHz cards; `frequency_hz` within 1e-5).
4. Change CH1's V/div by one step: for one gate the card shows the record's value (same number, four digits), then
   the counter's again; GET_COUNTER level / hysteresis bytes 18 / 19 move to the new midpoint once (no restarts on
   later gates: `seq` advancing one per 100 ms with VALID set).
5. A host-pinned hysteresis is kept: `scope_counter(hysteresis=10)`: `hysteresis_codes` 10 on every later reading,
   `level_code` centred, `level_auto` true; `scope_counter(level=1.0)`: the level pinned at 1 V; `level='auto',
   hysteresis='auto'` restores the boot behaviour.
6. Raise CH1's V/div until the square is under half a division (span under 12 codes): `valid` false with the "spans
   ... under the 12" caveat, the cards on the record's values.
Regression signs: the card flickering between the counter and the record at a steady signal (the deadband too tight
for the unit's noise: widen max(2, h / 4)), `seq` steps of more than one gate at a steady signal (retunes every gate),
the RC node still miscounting (span / 6 too small for that noise: go to span / 5).

**Open**
- docs/plans/fpga-0x153c.md section 5 (the contract) still says the level follows the trigger level: history, left.
- The RTL comment in `fpga/al3/src/zaklad.v` ("the firmware writes 128 / 6") stays for the next FPGA change.

## 89. fw-relay: quiet relays, host requests close the Frequency response view (fw 0.2.105, protocol 1.27 unchanged)

Branch `fw-relay` from main 032158a. Brief: the 2026-10-22 soak (fw 0.2.105 on 0x153D; every 10 s a host capture of CH1 +
CH2 then `set_acq_state(True)`, GET_STATUS every 2 s, READ_SPECTRUM every 30 s, no channel change) made the relays click
on every cycle. Commits 4ca4ea0 (relay shadow), 7b797bd (the view rule, firmware + mcp), then the docs commit with this
section. `PC_FW_VERSION` stays 0.2.105. **Nothing benched.**

**Cause** (confirmed by the owner: the Frequency response plot was on the screen during the soak). The device Bode sweep
run just before the soak (mcp `device_sweep`, `close_view=False` by default) left the view open. With the view open,
`bode_view_service` starts a RUN sweep on every rising edge of `scopesettings.runstate`, and SET_ACQ_STATE RUN gave it
one each cycle; the sweep's plan switches ranges (at its start back to the first point's range and at the planned
transitions, section 67's "two planned" switches), so the relays really changed; the next ARM
(`pc_hal_ui_set_run(0)`) stopped it. Not the cause: a rewrite of an unchanged value. zaklad.v latches the 0x33 / 0x36
byte (`relay1_x <= data`, ignored above 5) and the 0x34 / 0x37 bit, so the same value never switches a relay; no path
writes the relay commands from a read (every read of 0x33 / 0x34 / 0x36 / 0x37 is cmd + read strobes), and the ISR
poll does no FPGA access.

**Rule 1: host acquisition requests close the view** (owner 2026-10-22). `pc_cmds.c` flag `F_SCOPE` on SET_CHANNEL,
SET_TIMEBASE, SET_TRIGGER, SET_TRIGGER2, SET_DECIM, SET_SEQ, SET_GEN, ARM, LOG_START, CAL_PROBE: `pc_dispatch` calls
`pc_hal_ui_leave_bode` (= `bode_view_close`, the Menu > Scope path) in the MAIN context before the handler, next to the
Diagnostics leave; `h_set_acq_state` does it for RUN only. The settings from before the view come back first, then the
request applies to them (closing at ARM after a host SET_* would have overwritten the host's setting with the view's
saved one: the reason the rule covers the setters, not only ARM / RUN). A sweep in progress ends as stopped (READ_BODE
reason 6); reason 5 (BODE_ERR_HOST, `bh_poll`'s host-capture check) is no longer reachable from a host ARM but stays as
the engine's guard. STOP, BODE_CTRL, READ_BODE and the reads keep the view. Sweeps start only from the view's own
controls and BODE_CTRL. Reads made while the view shows report the view's live settings (unchanged behaviour).

**Rule 2: relays written only on a change.** `fpga_bus.c` `fpga_relay_write(cmd, value)`: `fpga_bus_wr.relay[4]`
(FPGA_RELAY_INDEX: 0x33, 0x34, 0x36, 0x37) with `FPGA_HELD_RELAY << i` valid bits, updated by `fpga_bus_note_write`
from every write (so DEBUG_FPGA writes are followed; a range above 5 is not taken, as the FPGA ignores it), counted in
`relay_writes`. `fpga_set_channel_voltperdiv` / `_coupling` go through it, so every caller (RUN's
`match_volt_per_div_settings`, menu.c's viewer close, ui_settings_ui's load, ui_diag_view's restore, the wizard's
`cal_scratch_channel` and restores, host SET_CHANNEL, the Bode engine's `bh_channel`, AUTO SET) writes nothing when the
hardware already holds the value. Unknown at boot and after a FEL load (BSS): the first write of each goes out. No
other invalidation is needed: every relay write, the wizard's included, goes through these functions or is seen by
`fpga_bus_note_write`. Settle delays unchanged: `match_volt_per_div_settings` runs only when sample != display V/div,
and then the offset changes too (dc_calibration_offset[6] vs [5] when the relay stays), so its 50 ms stays; the wizard
and AUTO SET waits stay (they settle the offset as well).

**Host tests**: test_fpga_bus `test_relay` (fpga_model relay registers, `relay_writes` / `relay_switches`): boot writes
4; five host capture + RUN restore cycles (arm, 0x37 / 0x34 status reads, a readout, the 0x33 / 0x36 reads, the restore
of all four) write 0; a range change writes once, the next restore nothing; AC on CH2 once; a raw write is followed; a
range 6 write is ignored. test_session `test_relay_cycle` (mock HAL: `bode_view`, `bode_sweeps` = sweeps a RUN starts
while it shows, `relay_writes` by the shadow rule): three ARM / WAIT / READ_RAW / RUN cycles with the view reopened
before ARM and before a second RUN: 6 leaves, 0 sweeps, 0 relay writes; STOP and GET_CHANNEL keep the view,
SET_TIMEBASE closes it; SET_CHANNEL with the same range 0 writes, another range 1. mcp test_bode_device: a host capture
during a device sweep ends it as stopped (reason 6) with the scope view back; RUN with the view open closes it without a
sweep; SET_CHANNEL with the view open closes it and then applies; the device / host comparison closes the view before
the host sweep.

**Bench (owner, FEL load; nothing flashed)**
1. The soak cycle: a device sweep (`scope_bode_sweep(where='device')`, the view left open), then the soak (a host
   capture of CH1 + CH2 and `set_acq_state(True)` every 10 s, GET_STATUS every 2 s, READ_SPECTRUM every 30 s) for 3
   minutes: the first capture closes the Frequency response view (the scope view and its V/div come back, at most one
   click if the view's last ranges differed), then **no click for the rest of the soak**.
2. A range change (`scope_set_channel` to another hardware range): **one click**; the same request again: none. AC/DC
   in the scope's channel menu: one click each way.
3. Open the Frequency response view on the scope and tap RUN there: the sweep runs (the view's own RUN still sweeps); a
   host `scope_status` keeps the view; a host `scope_capture` closes it.
Regression signs: a click per cycle in step 1 (the view still open: check GET_STATUS display_mode), no click on a range
change in step 2 (the shadow out of step with the hardware: a relay write outside `fpga_relay_write`).

## 90. fw-guardfix: the capture-stage guard acts only on the firmware's own 0x63 (fw 0.2.105, protocol 1.27 unchanged)

Branch `fw-guardfix` from main 4d0dc75, commit bfbb7ed (`PC_FW_VERSION` and `PC_FW_FPGA_VERSION` 0x153D unchanged).

**Bug** (bench 2026-10-22, fw 0.2.105 PC_DEBUG image on 0x153D): the runtime guard (section 87) moved a core while the
FPGA's 0x63 had been set from outside the firmware. `fpga/al3/tools/capwin_sweep.py` (fpga/al3/README.md section 18)
writes 0x63 through DEBUG_FPGA to put one core on stages 2..5 in turn; with CH1 A at stage 4 (it garbles on this unit) the
self-check flagged CH1 A three times in a minute and the guard applied `cs_guard` to `cap_st.value` (what the firmware
last wrote, not what the FPGA held), wrote 0x63 = 0x08A2 over the sweep's 0x08A4 and stored it in sector 708. A manual
sweep earlier stored 0x08A3 the same way (CH2 A moved 3 -> 2, worse on this unit). Cause: `cap_read` discarded 0x36's
first word, so the guard, the repeat watch (`cs_watch`) and REPEAT all assumed the FPGA held `cap_st.value`.

**Rule** (`cap_hw.c`): `cap_read` returns the 0x63 value from 0x36's first two bytes. `fpga_capture_check` still stores
the counts, `reads`, `flagged`, `flagged_n` and FLAGGED (GET_HEALTH shows what the FPGA counted), but when the value
differs from `cap_st.value` it stops there: no `cs_watch_add`, no guard, no REPEAT, no event 23. The flags of such
captures never enter the watch, so after the sweep restores the firmware's value three new flags are needed. `cap_guard`
also requires the FPGA's value to equal `cap_st.stored` (with TRAINED: the stored value of this bitstream) before it
writes 0x63 and sector 708. Every firmware write of 0x63 goes through `fpga_capture_write` (boot, calibration load, the
wizard's training and restores, the guard), so "the value the firmware wrote" is exact; the wizard's training captures
are excluded as before (`cap_training`).

**Host test** `test_cap_hw` (new; includes `cap_hw.c` with a mock bus for 0x63 / 0x64 / 0x36, `pc_hal_ms`, the event
log and sector 708): the trained value in effect garbles -> moved once and stored (control); stored 0x08E3 and a raw
0x63 = 0x08E4 with CH1 A garbling over six captures -> no 0x63 write, no move, no store, no event, no REPEAT, counts
read; back at 0x08E3 two flags do nothing and the third moves; untrained power-on value with a raw write -> no REPEAT,
at the firmware's value REPEAT once; a raw write of the stored value while the firmware wrote another one -> nothing.
The old code fails 15 of its 55 checks. `make -s test`, the vector check and the GCC 10 build (no new warnings) green.

**Bench (owner, FEL load of this PC_DEBUG build; back up SD 700-710 first)**: run `capwin_sweep.py` over CH1 A as on
2026-10-22. Expected: no event 23 with a bit 5 during the sweep, GET_HEALTH 106 bits 5 and 6 stay 0, GET_CAL 358 and
sector 708 bytes 488..511 unchanged, and the script's "0x63 changed under the sweep" check never fires. Restore sector
708 from the backup if a value the guard stored during the earlier sweeps (0x08A2 / 0x08A3) is still on the card, or
Calibrate again.

## 91. fw-gated: gated encodes at Filter Off again, two calibration classes, the counter at full rate only (fw 0.2.105, protocol 1.27 additive)

Branch `fw-gated` from main 1bb027f (`PC_FW_VERSION` 0.2.105 and `PC_FW_FPGA_VERSION` 0x153D unchanged). Owner decisions
2026-10-22 11:45 (docs/plans/task-queue.md "## Firmware", after the 11:36 A/B in docs/hardware-status.md: 0x153D's
full-rate encodes showed 1-2 % CH2 code errors at 2 MSa/s where 0x153C's gated encodes read 0 / 0, and 0x153D's encode
timing was worse at 200 MSa/s too): fpga-d3gate rebuilds 0x153D with 0x153C's encode generation (K = 0 at D > 0 gated by
the divider; full rate with any filter, peak detect and divider 0); the cards take the counter only at full rate; the
calibration gets its GATED class back. **Nothing benched**; it needs the rebuilt 0x153D (branch fpga-d3gate, ebf3645).

**Commits**: 42f2e6e (encode check comments), 9748de6 (host-test model), 9c7f59d (calibration classes and probe),
ed6c283 (counter), a8cf41c (mcp), then the docs commit with this section.

**Calibration (9c7f59d)**: a revert of c8649f4 and 5aafcdb against everything merged since; conflicts only in comments
(cap_stage.h, cal_wiz_ui.c, enc_heal.h: the newer text kept, "2 MSa/s slope points" now "gated 2 MSa/s"). Back as in
0.2.104: `acal_channel` per class, `acal_correction`'s class from the divider (FULL at divider 0; the 16-bit path uses
gain16 / corr16 of the full class; peak records `fpga_channel_correction_full`), the wizard's gated zero point and gated
gain levels, `ACAL_E_CLASS` / `ACAL_E_GAIN_CLASS`, `acal_readout_fault`, `ACAL_MAX_NOISE16_FULL` (3) / `_GATED` (1, also
the capture-stage training's quiet check again), the gain scale g_ref = mean gated gain, the class probe (`acal_cm_*`,
`acal_live_service`, `FPGA_WHO_PROBE`, the "Full-rate offset" line), the tracker on full-class records only, Diagnostics'
class gain ratio and gated / full noise rows, GET_CAL 73 bit 2, the gated slots, 256 bits 5..6 and 280..287, CAL_PROBE
point 4, `ACAL_BLOCK_CLASSES` 2 (a block written here loads on 0.2.104), the `adc_cal_v3` vector with classes 2.
**Kept from fw-fullrate**: nothing of the class change itself. Its tracker change (d / e applied to every 8-bit readout,
learnt from divider-0 records) is not class-independent: the tracked A - B is a full-rate property and the gated class
has its own per-core zeros and gains, so the tracker applies to full-class readouts again. Kept: the encode check on
every capture (below). Merged since and unaffected: capstage's pick / tie pass / guard, ctrauto, relay, guardfix,
stoprate, usbback, acqui.

**Single-class blocks (decision)**: a block with header classes 1 (the 0.2.105 builds of 2026-10-21/22, slot 1 written
0) still loads. `acal_unpack` accepts classes 1 or 2; for 1 it copies slot 0 (zero, slope, noise, gain) into slot 1 and
sets `gain_flags` bit 1 `ACAL_GAIN_FULL_ONLY`. Gated readouts then use the 200 MSa/s values (on the owner's unit core B
up to about 4 codes and the gain about 1 % off at Filter Off below 200 MSa/s), better than zero gated values or refusing
the block (which would drop to the 1.00o5 compensations). The flag is stored with the block (the next save writes
classes 2; 0.2.104 then reads the copies), carried by the zero step's gain carry (`old->gain_flags | KEPT`) and cleared
by a gain step (gain_flags = 0). **What the UI shows**: Diagnostics Status verdict Check "Calibration lacks the gated
rates (a 0.2.105 test build): run Calibrate." (after the REPEAT reason, before "Gains not measured"), GET_CAL byte 73 bit
5, the report line ", gated class copied from the full rate (single-class 0.2.105 block: run Calibrate)". Tests:
test_adc_cal (a classes-1 block: the copies, the flag, classes 0 refused, the repack loads identically), test_ui_diag
(the verdict, also with gains not measured).

**Counter (ed6c283)**: `pc_glue_adc_full_rate()` (pc_hal_scope.c) = `fpga_divider == 0 || pc_decim_wide(&pc_dec_cur)`
(the 16-bit path: any filter, peak detect) for what the FPGA holds. `fpga_counter_pass(now_ms, full_rate)` marks a CH1 /
CH2 result taken at a gated setting (`fpga_cnt.gated`); `fpga_counter_usable` needs !gated, so the cards
(`pc_glue_counter_pass`) show the record's measurement; GET_COUNTER clears VALID (freq_mHz 0) and sets **flags bit 4
GATED** (documented additive flag, bits 4-7 were 0, no new minor). EXT is never gated. The gate the pass reads ran at
the setting the pass sees: a 0x0D / 0x53 write in between is the rate-change rule's drop. fw-ctrauto's thresholds
unchanged (a gated gate still retunes them from its min / max). Tests: test_fpga_bus `test_counter_gated` (gated CH1
held but not usable, EXT usable, full rate usable), test_session (flags 0x11, VALID back when cleared).

**Encode check (42f2e6e)**: the brief said "restore the 0x153C rule (check where the encodes ran at full rate)".
fpga-d3gate (2026-10-22) confirmed that 0x33 bit 0 is meaningful for K >= 1 at any D, peak detect and K = 0 at D > 0
gated: the FPGA compares the encode flops against the predicted write clock (`swc_sh ^ sce_q`) on every falling 200 MHz
edge and the prediction includes the divider's enable; the only false flag is the one miss after an asynchronous 0x0D /
0x53 write, which the firmware's DATA mode and 0x01 = 1 handle. So fw-fullrate's FLAG check after every capture stays
(more coverage than 0.2.104's divider-0 rule); the DATA residual stays divider-0 legacy only. Comments updated.

**Host-test model (9748de6)**: revert of f0f1f06 (one stored word per 10 (D + 1) ns write clock edge at K = 0, D > 0;
`fm_store_edge`, `raw_words`, `fm_fullrate_pair`, `test_fullrate` gone). 0x05 reads ready after **six** write-clock
edges in reset on 0x153C / 0x153D (fpga/al3/README.md section 17: six stored words, 6 (D + 1) x 10 ns legacy; the model
had one edge), older versions one. `test_gated_ready`: D 0 / 4 / 399, at least six edges in reset, the ready polls grow
with D, 150 words in 150 x 10 (D + 1) ns.

**MCP (a8cf41c)**: mcp/AGENTS.md "fw-gated".

**Verification**: `make -s test` green, vector check (161) OK, GCC 10 build without warnings in the touched files, mcp
ruff clean and 542 tests.

**Bench (owner; after flashing the rebuilt 0x153D from fpga-d3gate; FEL load of this build; back up SD 700-710 first: the
wizard's Save writes sector 708)**
1. Before calibrating (a card saved by an earlier 0.2.105 build): Diagnostics Status reads Check "Calibration lacks the
   gated rates (a 0.2.105 test build): run Calibrate."; GET_CAL byte 73 bit 5 set on both channels.
2. Calibrate with both inputs open (probes off, USB cable away from the inputs), then the gain steps with the probes on
   the post; Save. The zero step passes on both channels (gated noise limit 1.0 code again); Diagnostics Status OK, the
   class gain ratio and the gated / full noise rows within their limits; the Calibration page shows gated and full zero
   columns and, after half a minute, the "Full-rate offset" line. GET_CAL 73 bit 5 clear; sector 708 byte 136 (the
   block's classes) = 2.
3. The counter card, 1 kHz on CH1 (the post): with Filter Hi-Res the Freq card reads 1.000 kHz from the counter
   (`scope_counter`: `valid` true, `gated` false, 1000.000 Hz within a few mHz); with Filter Off at 500 kSa/s the card
   shows the record's measurement (still about 1.000 kHz) and `scope_counter` returns `gated` true, `valid` false with
   the "gates the ADC encodes" caveat; at 200 MSa/s the counter again.
4. CH2 on the RC ladder node at 2 MSa/s, Filter Off: no squiggles on the trace (the 11:36 e_ab.py metric 0 / 0 as on
   0x153C); at 200 MSa/s no worse than 0x153C's 1.01 % / 0.86 %.
5. GET_HEALTH 88..103 after a few captures at 500 kSa/s Filter Off: the 0x33 check counts those captures too (none
   flagged on a quiet signal).
Regression signs: the Freq card flickering between counter and record at a steady setting (the full-rate test wrong),
the zero step failing "zero moves with the sample rate" (reason 11: the classes' zeros over 12 codes apart, the gated
encodes not as on 0x153C), Diagnostics still asking for Calibrate after step 2 (FULL_ONLY not cleared).

## 92. fw-capmetric: the noise-aware capture-stage training metric (fw 0.2.105, protocol 1.28, row 68)

Branch `fw-capmetric` from main 63df4e7 (`PC_FW_VERSION` 0.2.105 and `PC_FW_FPGA_VERSION` 0x153D unchanged). Commits:
b9eae5a (the metric, the band, the wizard glue, test_cap_stage), 8921016 (protocol 1.28: GET_CAL 364..431, vector,
test_diag / test_cap_hw), 2f11674 (mcp), then the docs commit with this section. **Not benched.** The specification is
fpga/al3/README.md section 18 "Firmware metric for the training and the guard"; the design notes are in `cap_stage.h`
"Quiet stages".

**What changed in the training** (`cs_train_run`):
- Band: CS_BAND_POINTS 17 parks at codes 8k - 0.5, k = 8..24 (`cs_band_code16` in 1/16 code: 1016, 1144, .. 3064;
  `cs_band_pwm` takes 1/16 code), CS_BAND_REPS 2 (was 11 points 120..140, 4 captures).
- Data: `cs_io.measure(ctx, v, resid, cnt, e)`: after the capture and the 0x36 read, every core in `resid` is read,
  CS_RESID_WORDS 2048 words from the record's middle (start = `pc_glue_ui_read_start(0x14)` + (8000 - 2048) / 2, through
  `pc_hal_read_core`, READ_RAW's path), and `e[core] = cs_resid(words, 2048)`.
- `cs_resid`: sum over j = 1 .. n - 2 of (x[j] - median3)^2, u32, integer only.
- Per core and stage `q` += E / CS_RESID_DIV (127 = (2048 - 2) / 16) per capture; `cs_quiet(sum, q)`: the stages with
  sum 0 and q <= q_min x 5 / 4 + 4. `cs_train` gained `quiet`, `quiet_ref`, `q`, `q_ref`, `ref`; `clean` / `clean_ref`
  stay the outlier-only sets; `set` (stored, the guard's bound) is the quiet set after the tie pass.
- Runtime guard: unchanged (fw-guardfix's rule; the spec keeps the 0x36 counts there).

**Rules chosen where the spec is not explicit:**
- The residual is read for the cores the pass trains: both cores of the parked channel in the own-encode pass, only the
  B core in the reference pass (scored by the same rule on its own sums and q). The tie pass reads no data.
- The tie pass stays as it was (0x36 counts, 16 captures per candidate and code at 130, 128, 126). Its codes are no band
  points any more, so `cs_io.park` now takes a code in 1/16 code instead of a band index.
- E / 127 is truncated per capture before the sum (u32 only; the truncation is under 1 per capture against a noise
  floor of about 4 per capture at 0.5 code rms).
- No outlier-clean stage: no quiet stage (a core has a quiet stage exactly when it has a clean one), so the B reference
  pass and the fail-fast "ADC readout fault near code 128" trigger as before.
- Settling: 60 ms per park (8 codes, about 32 PWM counts), 180 ms when the park moves more than 16 codes (the band's
  first point from the zero step's slope point, the tie pass's first code from the band's top). The wizard checks the
  band's two ends against the PWM range (the PWM is linear in the code, so every point and the tie codes lie inside).
- Exposure needed a protocol change (GET_CAL reported only the stored value): minor 1.28, GET_CAL 432 bytes, 364 u8 flags
  (bit 0 ran since boot, bit 1 failed, bit 2 core B from the reference pass), 365 / 366 the quiet sets of core A / B, 367
  0, 368 / 384 u32[4] q of A / B at stages 2..5, 400 / 416 u32[4] the 0x36 sums. Kept in `cap_hw.c` per channel
  (`pc_hal_cap_train`), zeros until the wizard trains that channel; a failed training is reported too.

**Wizard time**: per channel about 1.5 s (17 x 60 ms + 180 ms, 136 captures of about 0.5 ms plus two 2048-word reads of
about 0.6 ms each and 2 x 2046 residual steps, the tie pass 3 x 60 ms + 96 captures), about 0.5 s more than fw-capstage's
band; the reference pass, when it runs, adds about 1.2 s.

**Tests**: test_cap_stage 284 checks (`test_resid`; `test_quiet` on the sweep's q16 x 34 and 0x36 sums: {3}, {3, 4, 5},
{3}, {3, 4, 5} -> 0x08E3, the outlier rule alone keeping CH1 A5 and CH2 A2; `test_metric` with the mock's data: a clean
unit with 0.5-code noise -> 0x08DB as before, class (a) mixes at 3 and 5 (3 % of the samples at 8k read 8k | ((8k - 1) &
0x0E), invisible to 0x36) -> the outlier rule keeps 2..5 and picks 3, the quiet rule keeps {2, 4} and picks 4; the bench's
CH1 A (mixes at 2, 4, 5) -> {3}; a class (b) floor at every stage (8k -> 8k + 4 in 9 %) changes neither pick; the
reference pass scored), test_cap_hw 70 (`test_train_note`: GET_CAL's per-channel record, the reference pass), test_diag
(bytes 364..431), test_session (1.28, 432 bytes), vector `resp_get_cal_1_28`, mcp test_capture_stages
`test_get_cal_1_28_vector` and the 1.27 simulator at 364 bytes. `make -s test`, the vector check (162), GCC 10 without
warnings in the touched files, mcp ruff and pytest (543) green.

**Bench (owner; FPGA 0x153D as flashed; FEL load of this build, `make gcc10`; back up SD sectors 700-710 first: the
wizard's Save writes sector 708)**
1. `#VER` reads `proto=1.28`; mcp `scope_calibration_info` has `capture_training` with both channels null (no training
   since boot).
2. Calibrate with both inputs open (probes off, USB cable away from the inputs). "CH1: training capture stages..." takes
   about 1.5 s per channel. Expected notes: "Capture stages: CH1 A3 B4" and "Capture stages: CH2 A3 B4" (0x08E3).
3. Before Save, `scope_calibration_info` `capture_training` (or GET_CAL 364..431 per channel): CH1 A quiet [3], CH1 B
   [3, 4, 5], CH2 A [3], CH2 B [3, 4, 5]. Note every core's q per stage (divide by 34 to compare with capwin_sweep's
   q16) and the outlier sums. **The finding to report**: whether CH1 A5 and CH2 A4 have q more than 25 % above their
   core's stage 3 on the quiet parked input. If they do not (the mixes need the moving signal of the sweep), the quiet
   training cannot separate them and the stored set still holds them: report the numbers (the q per stage of all four
   cores) before deciding anything.
4. Save. GET_HEALTH 104 = 0x08E3 with TRAINED; sector 708 bytes 504..505 = the sets, 0xE2E2 for the sets of step 3
   (core k's stages 2..5 in bits 4k..4k + 3).
5. The 0x36 sweep on the real signal as in section 87 step 2 (CH1 on the post, 200 MSa/s, 1 kHz and 100 kHz, GET_HEALTH
   108..115 after each capture): every core 0 outliers over 30 captures; GET_HEALTH 106 bits 5 and 6 stay 0.
6. Optional cross-check: `python -I fpga/al3/tools/capwin_sweep.py --port COM10 --out capwin-after.npz` on the RC
   ladder signal of section 18: its printed quiet set per core agrees with step 3.
Regression signs: the zero step failing "offset does not respond" right at the training (the band's ends 63.5 / 191.5
outside the PWM range 200..1800 on this unit), "ADC readout fault near code 128" (a core with no outlier-free stage, as
before), a training that picks stage 2 or 5 for an A core, or the wizard's training step taking several seconds per
channel (the 2048-word reads slower than 0.3 us per byte).
