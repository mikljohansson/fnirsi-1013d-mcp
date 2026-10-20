# firmware/ handoff for agents

**Current state (coordinator merge, 2026-10-19 14:35): latest build fw 0.2.58, protocol 1.17** = main with fw-ui2 (UX makeover
step 2: CTRL page with fold-outs, strips and spectrum header out, spectrum cards, warnings in every variant, top bar chips;
section 33) and fw-seq (FPGA 0x1536 segmented capture and hardware averaging, SET_SEQ / GET_SEQ / READ_SEQ, protocol row 56;
section 35, with the 0x1536 flash session plan). Still running: fw-ui4 (step 4, section 34), fpga-gen6.
**Branch fw-cap (2026-10-20): fw 0.2.79, protocol 1.24 (row 64): FPGA 0x153A capture stages (training in the wizard's zero step, 0x36 self-check, stored with the calibration), EXT offsets, the wizard's zero back at code 128, section 54.**
**Branch fw-yield (2026-10-19): fw 0.2.78, protocol 1.23 (row 63), both provisional: host commands close the modal screens (every modal loop leaves by its cancel path; E_BUSY only for a calibration measurement, a file save, USB drive mode or a 1.5 s time-out), section 53.**
**Branch fw-screen (2026-10-19): fw 0.2.70, protocol 1.21 unchanged: READ_SCREEN built (cap SCREENSHOT), section 46.**
**Branch fw-bus (2026-10-19): fw 0.2.72, protocol 1.22 (row 62): FPGA bus contract (dummy strobe in every readout, geometry before the reset, legacy EXT shift, encode check after async writes, type 22 trace level, core order from 0x33), section 48.**
**Branch fw-bode (2026-10-19): fw 0.2.66, protocol 1.20 (row 60): the Bode plot on the device (bode_dsp / bode_sweep / bode_view / pc_bode, BODE_CTRL / READ_BODE), section 42.**
**Branch fw-polish1 (2026-10-19): fw 0.2.63 provisional, protocol unchanged: UI polish batch 1 (owner's walk-through of 0.2.59, items a-q), section 39.**
**Branch fw-ui4 (2026-10-19): fw 0.2.59 provisional, protocol unchanged: UX makeover step 4 (Diagnostics as a main-loop
display mode with Status, Calibration, Log, Developer; the four-screen calibration wizard; the first-run screen), section 34.**
**Branch fw-ui3 (2026-10-19): fw 0.2.55 provisional, protocol 1.16 unchanged: UX makeover step 3 (Settings fold-out
trimmed, full Settings screen, confirmation dialog), section 31.**
**Current state (coordinator merge, 2026-10-19 13:50): latest build fw 0.2.52, protocol 1.16** = main with fw-ui1 (UX makeover
step 1: main menu, USB drive screen, top bar, Display row, strips, measurement cards; section 29) and fw-readers (diagnostics
readers: GET_HEALTH, card image identity, calibration provenance, unit life record, SAVE_REPORT; section 30) merged. Running in
parallel: fw-ui2 (step 2), fw-ui3 (step 3), fw-touch1, fw-seg (0x1536, protocol 1.17). 
**Previous state (coordinator merge, 2026-10-19): latest build fw 0.2.50, protocol 1.15** (fw-cal3's calibration wizard, interleave
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

All builds run in WSL Ubuntu (`wsl -d Ubuntu`); from PowerShell use
`wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make -j8"`. Git Bash mangles quoting
in long WSL command lines and Windows Python heredocs halve backslashes: put multi-step commands in a script file.

| command | result |
|---|---|
| `make` | GCC 13.2.1, protocol on: `build/fnirsi_1013d.bin` |
| `make gcc10` | Atlan's GCC 10.3.1 (`~/opt/gcc10`, from `tools/fetch_gcc10.sh`): `build-gcc10-pc/`. The bench uses this one |
| `make PC_DEBUG=1 BUILD=build-dbg` | adds DEBUG_FPGA + FORCE (caps 0x120) for bench tests T3/T7 |
| `make PC_FIFO_MAP=1 ...` | alternative USB FIFO layout (version string `-m1`) |
| `make -C host_tests test` | host gcc: test_protocol, test_rx_ring (all 2048 ring phases), test_session + mock_hal (release + PC_DEBUG; 762 / 760 checks on 0.2.19), check_vectors.py (79 vectors). None of them models usb_interface.c, the IRQ dispatcher or the timer-tick USB path |
| `python host_tests/bench_link.py COM10 [--census 2000] [--fel]` | on the bench PC, needs pyserial |

Since 0.2.47 there is no PC_PROTOCOL=0 build and no `make check-reference` (owner dropped the byte-identical rule on
2026-10-18; the proof up to then is in the CI logs). Images are reproducible: the identity comes from the commit, so a
change that only removes dead code can be checked by building both commits with a fixed `PC_FW_BUILD=11111111
SOURCE_DATE_EPOCH=<any>` and comparing sha256 (section 25). Warning policy: no new warning under GCC 13 or GCC 10
(compare sorted, line-number-free warning lists against the parent commit; new files are also clean with -Wextra).
0.2.47: GCC 10 36, GCC 13 31, PC_DEBUG 31, all upstream.

## 3. Dev loop (no SD writes)

1. Scope runs a protocol build: host sends ENTER_FEL (cmd 0x0F01, u32 0x4C45465F). Fallback: boot menu, "FEL".
2. `usbipd attach --wsl --busid 9-3` (owner's machine; FEL id 1f3a:efe8 is bound) or `--hardware-id 1f3a:efe8`.
3. `sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000` (~2 s, run as root in WSL).
4. The scope enumerates as COM10 on Windows (VID:PID 1209:0001, serial 1013D-0001), or attach it to WSL as
   /dev/ttyACM0 for Linux cdc_acm.

The SD card boots our 0.2.11 from sector 750 (persisted with SD_STAGE + SD_WRITE_SECTORS, see section 13). One serial user at a time; ask the owner to keep hands off
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
  cannot flicker (0.2.3-0.2.9 cleared the slot and redrew it every 100 ms on the screen; owner saw flicker while the
  main loop ran). The slot is cleared only on on/off transitions. 0.2.3 steady green, 38 x 17 at x 127 y 3, directly
  above the RUN/STOP chip, in the RTC clock's slot (the main-loop RTC draw is gated by PC_RTC_ALLOWED while the chip shows);
  on while DTR or a frame in the last 2 s, redrawn every 100 ms. Owner rejected the 0.2.1 spot (x 697 y 22 under the
  battery) and the pulse.
- SET_TIMEBASE mode 0 (0.2.3): sets index + rate + short mode, FPGA, then one scope_preset_values +
  scope_acqusition_settings. 0.2.1 called scope_set_timebase and the owner saw flicker through time/div values; the
  exact mechanism was not identified (host requests never use the UI's one-index step routine). Bench: confirm.
- Unverified on the bench (acceptance list in README section 5, step 6): screen redraws after host SET_*, marker
  search for the trigger level on every range, the UI showing a host capture, the chip position and colours.

## 7. FPGA layer and capture geometry

Variant table from the 0x06 word in `pc_variant_from_version` (0x1432 stock: ring 4096, fixed 1500/3000;
0x1532 AL3: ring 8192, max 16382; 0x1533..0x153F AL3 family, same geometry, 0x1533+ adds EXT_TRIGGER (cmd 0x15 = 2,
status cmd 0x28; `fpga_get_version` maps the family to fw_FPGA 2 under PC_PROTOCOL); 0x1632 EF2 / 0x1732 EP4: ring 6144, max 12286; unknown: caps 0). The owner's
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
| 0.2.13 | 7ebda04 | 691d0d28 | System settings compacted without a DS3231; battery level averaged with hysteresis while charging | not benched alone; owner verdicts pending |
| 0.2.14 | d6a6b37 | aa4a142c | fallback only after two quiet ticks; EXT ground note | counter 47 = 11.8k in 2 min (SOF defeats the gate), 1/100 64 KiB lost |
| 0.2.15 | c4989e5 | b0cec01f | SOF ignored for counter 47 / type 14, event log 128, frozen pre-reset snapshot | superseded unbenched, not pinned |
| 0.2.16 | 079f648 | 5f25b9e7 | CDC handler split into entry + body; the tick reads the flags and runs the body only for non-SOF flags | 300 x 64 KiB clean, counter 47 = 16 in 7 min (real lost RX IRQs), **self-restarts** under bench traffic |
| 0.2.17 | 0c13045 | 02aa445a | quiet boot: no "Failed to open file" box without scope.bmp, counter 48 | built, host-tested; owner verdict pending |
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
| 0.2.26 | bf0651b | 6a7049bf | channel-menu FFT switch hidden, Invert/Coupling/V/A one row up (`PC_CH_DY`) | built, host-tested; owner verdict pending |
| 0.2.27 | efc5170 | 29db9cd4 | fx_fft.c FFT core (6edb719) + FFT_BENCH 0x0F02 (protocol 1.6, row 44) | bench clean; FFT_BENCH 4096 points 24.3 ms (4x the estimate, section 20); channel menus accepted |
| 0.2.28 | e6a7c4c | b7109d34 | spectrum view (main menu row 6), READ_SPECTRUM 0x020A, GET_STATUS byte 43 (protocol 1.7, row 45); spec_fmt.c (bed2dd4) | owner: works; harmonics within 0.4 dB of theory |
| 0.2.29-0.2.30 | (fw-decim) | | decimation filter for FPGA 0x1534 (protocol 1.9, row 47, section 19) | merged, not benched alone |
| 0.2.31 | 0930c72 | b5685268 | FFT hot loops in ARM assembly (LDRD/STRD, no spills, twiddle table), qemu-arm host test; includes 0.2.30 | bench clean on 0x1533; FFT_BENCH 2.2-2.5x faster (4096: 10.9 ms; SRAM 2048: 2.3 ms); spectrum as before |
| 0.2.32 | 83aa5b5 | ed9fb0f8 | spectrum view: Waterfall and Density sub-views (View field), GET_STATUS byte 43 = 2 / 3; trace area cleared with memset | built, host-tested; owner verdict pending |
| 0.2.33 | d125a41 | e002ab21 | 0.2.32 plus the view's FFT from SRAM A for N <= 2048 | built, host-tested; bench with 0.2.32's items |
| 0.2.34 | 1a9a773 | 00847286 | spectrum view on the 0x1534 16-bit records (fx_fft_run_u16, filter rate 200 MHz / N, scale_q24 levels) | built, host-tested; on 0x1533 identical in behaviour to 0.2.33 |
| 0.2.35 | 36149d1 | b1be459d | owner's UX: tap left/right on the plot = zoom out/in, drag = pan, Auto and Reset replace Span/Start; FFT length capped at 4096, default 2048; density with per-cell age (no band) and intensity ramp | built, host-tested |
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
wsl -d Ubuntu -- arm-none-eabi-addr2line -f -e /mnt/d/src/fnirsi-1013d-ai/backups/fw-0.2.19-9b94c9a8.elf 0x800xxxxx
wsl -d Ubuntu -- bash -lc "arm-none-eabi-objdump -d /mnt/d/src/fnirsi-1013d-ai/backups/fw-0.2.19-9b94c9a8.elf > /tmp/fw.dis"
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
  selective suspend. On the owner's PC selective suspend is off on AC, on on battery (turning it off on battery is the
  lead's call: `powercfg /setdcvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3
  48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0`, then `powercfg /setactive SCHEME_CURRENT`). The mcp soak saves the
  frozen snapshot before re-arming it (mcp commits 2c6a4c5, 56bf8ad); no session-open path reads the log.
- **Drops seen** (Windows WriteFile error 22, device re-enumerates): 0.2.2 idle with the port closed, 0.2.3 during an
  SD write under back-pressure, 0.2.11 during 10 Hz GET_STATUS polling, 0.2.12 soak (two drops 7 s apart, the first
  with three suspend+reset pairs). The owner turned Windows USB selective suspend off before the 0.2.12 soak.
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
  case) it showed "Failed to open file" for 700 ms: the brief error the owner saw at every boot.
- 0.2.17 (quiet boot): `pc_glue_boot_splash` probes the file silently with `f_open` and skips the splash without a
  message; with the file present it shows as in 1.00o5 (3 s). Counter 48 = ms spent + 1. Owner verdict pending.
- `scope.bmp` must be the scope's own screenshot format: 800x480 RGB565 BMP, 768,070 bytes, 1.00o5's 70-byte header.
  It adds about 4 s per boot, so the release package leaves it out. The FAT partition needs nothing at boot; picture
  and waveform folders are created on the first save. README section 4 ("Boot", "SD card FAT partition").
- Image placement: 740 sectors from 750, ends at 1489, below the FAT partition at 2048+. Persist path
  (SD_STAGE + SD_WRITE_SECTORS, 32 sectors per write, SD_STAGE served from the timer poll since 0.2.7): 0.2.7 5.3 s,
  0.2.11 5.7 s with read-back identical and the header verified. SD command design: protocol section 14 row 40;
  writes need a valid MBR, window [16, min(first partition, 2048)), flag BOOT for 16-79 and CONFIG for 708-710,
  power-key EINT masked during writes.

## 14. Owner's UI verdicts

Accepted: MCP chip (38 x 17 at x 127 y 3, above RUN/STOP, steady, one idempotent blit, stable while free-running on
0.2.11), compacted generator and trigger menus (0.2.8), acquisition "Roll mode on/off" box (0.2.10), menus hiding
what the hardware lacks (0.2.6), battery indicator stable on 0.2.11
(battery likely full). Pending on 0.2.13+: System settings without the empty row (compacted when no DS3231 answers),
battery hysteresis while charging from a lower level. Pending on 0.2.17+: quiet boot. Pending with fpga-gen2's 0x1533
bitstream: EXT trigger (caps bit 3, trigger menu EXT, GET_STATUS byte 42). Owner policy: hide unsupported options;
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
- (Until 0.2.46: PC_PROTOCOL=0 byte-identical to Atlan's image. Dropped by the owner; the switch is gone in 0.2.47.)
  Hidden features are removed in code, not masked (owner, 2026-10-18); keep settings sector / file slots stable.
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
- Bench: the agent never touches hardware. The coordinator (team-lead) owns COM10, FEL loads and SD writes. Never
  flash anything without a verified backup; the stock firmware must stay bootable. Only the coordinator persists an
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
1c. Host-capture trigger position varies between runs at decimated rates (lead, 0x1533 / 0.2.21, lower priority; owner
   of the fix TBD, firmware or host geometry). Capture.pretrigger_samples vs the measured CH1 edge is stable within a
   run but differs between runs: 1 MS/s (divider 199) EXT +11/+12 us in the first 10 captures after selecting EXT,
   then -1.0 us in 17 captures over five setting orders; CH1 source +2/+3 us in one run, -1.0 us in another; 20 MS/s
   both -50 ns consistently. Suspects: FPGA decimation phase or trigger_addr-to-sample mapping at divider > 0 (words
   vs samples), 1.00o5 trace position state inherited by host-origin captures, a stale divider in the host geometry
   when UI and host disagree. Test: alternate CH1/EXT and timebases, log GET_CAPTURE_INFO (trigger_addr,
   capture_start_word, divider) next to the measured edge index (method in docs/hardware-status.md).
2. Read the 0.2.18+ frozen snapshot after sessions to explain the two suspend+reset pairs per session (section 11).
3. Owner verdicts on 0.2.13+ System settings and battery, 0.2.17+ quiet boot; EXT trigger bench with 0x1533.
4. Bench tests T3-T8 and T10 (section 7), then clear the UNVERIFIED caps and set FORCE_TRIGGER in release builds.
5. Phase 2/3 commands: READ_LIVE, READ_SCREEN (T9), ACQUIRE_AVG (128 KiB accumulators), full-speed fallback.
6. Per-unit USB serial (SID/eFuse) and a real pid.codes PID before anyone else flashes this.
7. Optional: composite CDC + MSC (needs a TinyUSB-style rework; plan section 9).

## 18. Handoff from fw-gen4 (retired at about 280k tokens, 2026-10-18)

State: 0.2.21-0.2.25 built, host-tested, pinned in backups/ with ELFs (ladder, section 8). **0.2.25 (c71935f9) is
on the card.** Nothing is uncommitted. The coordinator is running the deciding test: cold boot with the cable in,
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
eaten; put WSL steps in a script and run it through PowerShell `wsl -d Ubuntu -- bash /mnt/c/...`. Every rebuild
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
- Owner verdicts so far: spectrum view works, harmonics within 0.4 dB; waterfall "very nice"; density band fixed in
  0.2.35 (verdict pending); 0.2.35-0.2.40 UX items pending on the screen (header layout, gestures, Auto, averaging).
- Firmware auto trigger mode (0.2.41): README section 5 "Firmware auto trigger mode", protocol section 7.5. The
  FPGA's 0x0E time-out counts samples and 0x18 is not gated by the stop, so auto records at fast rates were untriggered
  but could read triggered. Auto now arms normal on Atlan bitstreams; `pc_ui_auto_service` (main poll) and the host
  poll write 0x1A = 0 at the deadline and clear the triggered flag. Calibration and auto setup keep the FPGA's auto
  (`pc_glue_decim_conversion` writes 0x1A per conversion). Bench: the 10 MS/s / 1 kHz square case from the lead's
  host captures (2 of 6 flat "triggered" records on 0.2.40) should give 6 of 6 single-edge records in auto mode, and
  the spectrum view's comb must not flicker.
- Owner verdicts open: 0.2.41 auto mode and zoom-in rate stepping; 0.2.42 Auto procedure (README "Auto"), warnings.
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

fw-cal retired at about 500k tokens, 2026-10-18. Branch `fw-cal` = main 0.2.42 + the calibration work; the owner ran
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

**Bench results (coordinator, 2026-10-18, 0.2.43)**
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
  +1 V: possibly non-linear (core INL); the owner offers a second point (-1 V or 2 V): ask for -1 V on CH1 range 2
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
  (the owner re-runs base calibration, and the voltage calibration if the gain comes from it). GET_CAL bytes 92+
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
  (`PC_SETTINGS_DEFAULTS` in scope_reset_config_data). Owner: CIC2 | PC_DECIM_SET_FIR once 0x1535 is benched.
- Spectrum repaint (README "Redraws in the spectrum view"): `scope_preset_values` starts with `PC_SPECTRUM_DISPLAY()`;
  `spectrum_display` has a re-entry guard (spec_tb_set -> pc_hal_timebase_write -> preset -> display). Audit: no other
  host-driven redraw reaches the trace area. If the owner still sees the strip flicker on SET_TRIGGER / SET_GEN alone,
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
- v2 blocks are not read (no conversion): the owner re-runs the base calibration.

**Host tests**: test_adc_cal 8590 checks: block, correction identities (v1 exact, unity-gain v3 = v2), every gain
check, and a model of the owner's CH1 from the gain bench (v2 reproduces A - B 0.91 code at +-1 V full rate and the
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
  are clean with -Wextra. (`make check-reference` against Atlan's binary no longer matches on main since the owner
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

**Bench steps (owner, with the coordinator on COM10)**
1. FEL-load the image (`sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), check #VER
   fw=0.2.46 proto=1.13. CH1 probe (x1) on the calibration pin, generator on, 1 kHz, duty 50 %; CH1 1 V/div DC, 200
   us/div, trigger CH1 rising, Auto.
2. Main menu: a seventh row "Phosphor display" under "Spectrum analyzer" (icon: three grey edges). Tap it. Expect: the
   scope screen unchanged except the bottom line: `Persistence 1 s | Palette Mono | Clear | Scope` boxes and "NN wfm/s"
   in grey at the right (about 25-33). The trace is yellow, sharp, both edges drawn, grid visible around it; pointers,
   measurement tiles and cursors (switch them on) where they always are. The main menu row now reads "Scope view".
3. Generator duty: toggle 30 % / 70 % (generator menu, or the coordinator loops SET_GEN every 0.25 s): both falling
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

**Bench checks (coordinator)**
1. FEL-load the image, `#VER` fw=0.2.47 proto=1.13; GET_INFO byte 52 = 0x463b4895 (or the hash of the built
   commit); diagnostics screen identity line "git 463b4895 of 2026-10-18 23:04" (UTC).
2. Census (bench_link.py --census 2000) and the mcp soak for 10 minutes: clean as 0.2.46.
3. Owner taps: channel menus CH1 / CH2 (Open CH, Invert, Coupling, V/A, Probe, V/div; no FFT row); generator
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

**Bench sequence** (coordinator + owner; FEL loads only, nothing written to the card unless the coordinator decides)
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
   -nommu read. The persist path (SD_STAGE + SD_WRITE_SECTORS) only if the coordinator wants this image on the card;
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

**The case** (docs/hardware-status.md 2026-10-19, coordinator's brief): fw 0.2.48-nommu, port closed 08:40-10:20, at
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
If the owner used the guide's command on 2026-10-18, the setting never changed; `fnirsi1013d-soak` prints the real state.

**Bench plan** (coordinator + owner; FEL load only; record `powercfg /query SCHEME_CURRENT
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
      a = 0 and type 7 (fresh enumeration), Windows' unplug / plug sound, COM10 back; the reopen at the round's end is
      fine and resets the long back-off. At most one type 20 per 30-min round.
2. Two hours idle, port closed, selective suspend still on, PC awake: `fnirsi1013d-soak --mode reopen --idle 7200
   --rounds 1`. If the host suspends: type 20 (reason 2) about 15, 45 and 105 min after the first suspend (15 + 30 + 60),
   each followed by type 1 a = 0 and type 7; reopen fine; 61 <= 3. Then (owner's call) once with the PC put to sleep
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
- Coordinator's bench facts this rests on (docs/hardware-status.md 2026-10-19): full-class common mode +0.7-0.8 code in
  2 h while the gated class stays (design section 8); the FIR path benched at 78-83 dB alias rejection and CIC2+FIR
  becomes the default, so gain16 / corr16 must keep working with the FIR gain (N/2)^K: they do, cm enters corr16 as an
  additive 1/16-code term after `pc_decim_to_code` (code = out x scale / 2^24 + corr16 / 16), independent of N, K and
  the FIR; the 03:48-type gated step is not tracked (above).
- Design-doc change made while building (interleave-tracking.md section 2): q identifiable needs w >= 65536 codes^4
  **and** w >= v^2 / 4 (the first draft's 256 codes^4 let the noise inside the post square's two levels pull q to 0
  and bias e; test_acal_track (c)).
- Gotchas: the main menu reorder 7c62586 put Spectrum / Phosphor first; the system settings fold-out stays anchored at
  y 46 while its row moved to y 164 (owner to judge). Pre-existing, unfixed: in `fpga_read_adc_data` the core-B rail
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
  box (was y 470). The fold-out stays anchored at y 46 while its main-menu row is the fifth (owner to judge; step 3
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
  RANGES) run from PowerShell, and builds through scripts run with `wsl -d Ubuntu -- bash <script>` (WSL /tmp did not
  survive between calls; keep logs and the base build under $HOME). The Write tool strips trailing blanks, so never
  anchor an edit on a 1.00o5 blank line.

**Bench walk-through (owner, coordinator on COM10)**
1. FEL-load the image, `#VER` fw=0.2.51 proto=1.15. Top bar: no "v0.2.51" text at the right (the diagnostics view
   still shows the identity line).
2. Menu: seven rows Scope, Spectrum analyser, Saved pictures, Saved waveforms, Settings, Diagnostics, USB drive, each
   with an icon, no row marked as current. Tap every row near its top and bottom edge (just below / above the black
   separator lines): each opens its own function (Settings: the fold-out; Saved pictures / waveforms: the file
   browsers; Diagnostics: the diagnostics screen; USB drive: step 4); a tap on a separator line belongs to the row above.
3. Settings fold-out: no X-Y row; Notification confirmation toggles in the fourth slot, RTC settings (with a DS3231)
   in the fifth; the box ends under them.
4. USB drive: title "USB drive", the scope and PC picture, the three lines, an Exit button bottom right. With the PC
   showing the drive, Exit gives "Not allowed!"; eject on the PC, Exit returns to the scope and COM10 comes back.
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
  (a bump would have made the owner's fallback images uncalibrated). Stamped by comparing the block on save with the
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

**Bench checks** (coordinator + owner; FEL load, the card keeps 0.2.25)
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

**Bench walk-through (owner, coordinator on COM10)**
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
   menu choice unchanged at the next power-on, calibration unchanged (diagnostics lines), COM10 still there (CDC), and
   USB drive > Exit still returns to CDC.
9. Saved pictures: select a file, Delete: "Delete from the SD card?", "This cannot be undone.", Cancel left, Delete
   right (red), both 44 px tall and 16 px apart; Cancel keeps the file and the browser redraws cleanly; Delete removes
   it. Same in Saved waveforms and in the waveform viewer's Delete wave.
Bug indicators: a fold-out tap opening the neighbouring row; the Settings screen leaving parts of itself on the scope
after Back; the main-menu area showing an old picture after Restore; a row's value overflowing into its label; the
boot menu appearing (or not) against the At power-on value; a tap between rows or outside the dialog buttons acting;
COM10 disappearing after Restore default settings; the delete dialog leaving a box behind in the browser.

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
- Left for batch 2 / 3 as briefed: trace-body grab and radius 40, 8 px dead band, tap zones (the owner keeps the hidden
  V/div bands), hidden objects (the trigger level hit while its pointer is hidden, X-Y moving disabled channels),
  locked cursors, the blocking loop, the second GT911 reader, long-mode presets per step, spectrum pan.
- Merge note: fw-ui2 edits the right-bar handlers in statemachine.c and fw-ui3 the settings fold-out; this branch only
  touches `touch_handler`, `scan_for_touch`'s plot branch, the three `change_*` functions, `scope_draw_pointers`, the new
  auto setup's DC blocks and the two loaders. Both Makefiles gained one SRCS / test line each.

**Bench walk-through (owner, coordinator on COM10)** - FEL-load the image, `#VER` fw=0.2.54 proto=1.16. Normal
display, both channels on, CH1 trigger, Move fast (not slow) unless noted.
1. Defect 1, pointer is not a handle. Old repro: drag the yellow "1" pointer up: nothing moved. Now: touch the "1"
   pointer and drag up and down: CH1 follows the finger; the same for "2". A touch up to about 20 px above or below the
   pointer, within the left 44 px, still grabs it.
2. Defect 3, nearest enabled. Old repro: drag CH2 to about 10 px below CH1, switch CH2 off in its menu, touch 5-10 px
   below CH1's line and drag: nothing moved. Now: CH1 moves (both from its pointer and from the line on the plot). With
   CH2 on and parked exactly on CH1: the drag moves CH2 (drawn on top); move it away and CH1 is reachable again.
3. Owner's report (CH2 at the bottom, CH1 only from its zero line): CH1 now drags from its pointer at any height; on the
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
  fold-out (the plan's owner text and the SpectrumCtrlAnalysis board; there is no free slot for an Axis button, and the
  spectrum's 50 % slot stays empty). Grid shows a slide switch, not "on" (guidelines: a toggle never reads on / off).
  MEASURES is an empty slot in Spectrum (the spectrum's own list is Measurements v2; the cards are a fixed set). The
  Display button values are abbreviations that fit 47 px ("Phos", "XY Ph", "Spec", "Wfall"; test_ui checks the widths).
- **Lost or open until later steps**: the spectrum's MEASURES list and THD card (Measurements v2); one-tap phosphor
  Clear (now CTRL, Display, Clear, accepted by the owner); a live wfm/s while working (it shows only in the fold-out;
  a card is the review's suggestion A8); the viewer's and DC shift pages keep 1.00o5's look; the Settings fold-out and
  screen are step 3 (fw-ui3, 0.2.55, merged here); fold-outs pause the trace and the PC (E_BUSY after 250 ms)
  like every dropdown.

**Bench walk-through (owner, coordinator on COM10)**
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
0.2.58: the coordinator renumbers.

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
- Wizard: no Skip (owner); Cancel is live during a measurement (polled between points, `CW_RC_CANCELLED`). The zero step
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

**Bench walk-through (owner, coordinator on COM10; FEL load, back up SD sectors 700-710 first: Save writes 708 and 709)**
1. FEL-load, `#VER fw=0.2.59,proto=1.17`. On the owner's calibrated unit no first-run screen. Menu > Settings: the
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

Version 0.2.58 as assigned by the coordinator (main: 0.2.56 fw-ui2, 0.2.59 fw-ui4); protocol 1.17 with the row kept at
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

**Flash session plan (owner with the coordinator; agents never flash)**
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

**Open / for the coordinator**
- fpga-gen6 findings that hold for 0x1532-0x1536 (fixed in their 0x1537, not built yet): (1) the A/B order inside a word can
  flip after a 0x0D or 0x53 write and stays flipped until the next such write, so PC_INTERLEAVE_ORDER and the swap bit are
  right by chance (bench T4); (2) the 1.00o5 readout and `fpga_read_decim_data` are one word late (see above); (3) on
  0x1532-0x1535 the 0x0B-in-reset race at slow legacy rates still exists (I kept their arm order byte-identical as the brief
  asked): moving 0x0B / 0x0C before 0x01 = 1 there too is a one-line change in `pc_hal_arm` / `fpga_do_conversion`; queue it.
- 0x1537 will need the version rule only (`pc_variant_from_version` already maps 0x1537..0x153F to SEQ).
- Phosphor fast capture: design in docs/plans/phosphor-fast-capture.md (hundreds of wfm/s at fast time bases), not built.
- UI 16-bit hardware averaging (see "decided" above) and an Average count setting are UX decisions for the owner.

## 36. fw-calnoise: the wizard's "too much noise" on 0.2.59 (fw 0.2.60, protocol unchanged)

Bench fact (owner, 2026-10-19 15:45, 0.2.59 in RAM, FPGA 0x1535): the Zero level step failed with "too much noise" on
both channels in most runs, with probes and USB unplugged. 0.2.48's base calibration had passed in the morning (stored
noise 0.6-1.3 codes).

**Correction (coordinator, after 16:15).** The step also fails with nothing connected, and with USB connected, on
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

Confirmed on the bench (coordinator, 2026-10-19 16:20-16:45):
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
  else changes. Caps bit 12 INTERLEAVE_UNVERIFIED stays set until T4 on 0x1538 (coordinator's call).
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

**Bench check** (owner with the coordinator; FEL load only, nothing written):
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
- `docs/release-notes/0.3.0.md` (rel-gen1) does not mention 0x1538 or the per-capture check; the coordinator decides.
- Caps bit 12 INTERLEAVE_UNVERIFIED: clear it for 0x1538 once T4 confirms B first on hardware (one line in
  `pc_variant_from_version`).
- 0x1532..0x1536 still re-roll only on rate writes, so their re-arm re-writes 0x0D first (two extra writes per re-arm;
  0x1535 measured 50 % odds per write, so about one re-arm per bad capture on average).
- A large signal above about 15 MHz raises the DATA residual (a 100-code 10 MHz sine stays under the limit,
  test_enc_heal); on 0x1532..0x1537 such a signal could make every full-rate capture re-arm 16 times and be flagged.
  0x1538 has no such limit.
## 39. fw-polish1: UI polish batch 1 (fw 0.2.63 provisional, protocol unchanged)

Branch from main 8f8687a (fw 0.2.61), merged with origin/main a179408 in bc32b8b. Brief: `docs/plans/task-queue.md` "UI
polish batch 1" items a-p plus the coordinator's item q; rules `docs/ui-guidelines.md`; user-facing description README
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
fw-enc's); the coordinator renumbers if needed.

Decisions (owner may veto each):
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

**Bench walk-through (owner, coordinator on COM10; FEL load, no SD writes needed)**
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
step overshoot pulsing" (owner decision: Filter per mode) and "Touch fixes batch 2"; docs/plans/touch-review.md (b), (c),
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

**Bench walk-through (owner, coordinator on COM10)** - FEL-load, `#VER fw=0.2.64,proto=1.18`. CH1 on the 1 kHz
calibration square, 1X, DC, 1 V/div, Auto trigger, memory 3000, Scope view.
1. ACQ menu, owner's repro: 500 us/div at 500 KSa/s, then tap 200 MSa/s. The time/div box jumps to 1 us (green); the
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

**Bench walk-through (owner, coordinator on COM10)**
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

**Bench walk-through (owner, coordinator on COM10; FEL load only, no SD writes; parts on the bench: the RC 1 kOhm / 15 nF
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
   Host: `fnirsi1013d-bode --port COM10 --yes --start 100Hz --stop 1MHz --points 50` (close the view first: Menu > Scope)
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
   with Scope.connect("COM10") as s:
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

Bench fact (coordinator, docs/hardware-status.md 2026-10-19 17:55-18:40). On fw 0.2.64 / FPGA 0x1538 with nothing
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
    about 27 counts on range 0 of the owner's unit).
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
  coordinator.

**Bench check** (FEL-load only, nothing written before Save; back up SD 700-710 first because Save writes 708 / 709):
1. `#VER fw=0.2.67,proto=1.21`. Run `python firmware/tools/cal_probe.py COM10 both all` with probes, post and generator
   off.
   - Every range OK on both channels.
   - The "gated zero pair" and "full zero pair" rows print the two points' average as the mean (near 128), with "+- d"
     about 20-45 counts in the header line, and lowest..highest about 112..144 (both points together).
   - No clip at any point after the coarse ones.
2. Mode 1 on both channels: `python firmware/tools/cal_probe.py COM10 CH2 0 1 0 0 1`. CH2 ends at range 5 (index 5) with
   OK.
3. The owner runs the wizard: Zero level gives two ticks, then the gain screens and Save. GET_CAL then shows CH2 A - B
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
Brief: the owner's walk-through of 0.2.65 (2026-10-19 18:00), items 1-5, plus the coordinator's item 6 (owner decision
18:45: Bode plot as a main-menu mode row). User-facing description: firmware/README.md "Screens: UI polish batch 2 (fw
0.2.68)". Commits: 95c1033 (1 RUN/STOP), ec9f319 (2 icons), a9daba6 (3 time/div snap), d51dcb7 + ee754c7 (4 brightness),
3dc0ed9 (5 wizard step, Calibration row), 67d5204 (version, CHANGELOG, README), 1972245 (6 Bode row), the merges, this
handoff. Host tests green (test_ui 1,420,883 checks, test_tb_view 530,790, test_ui_diag 5516, the rest unchanged,
check_vectors OK, CHANGELOG check OK). Warnings: GCC 10 36 (gcc10 and gcc10-nommu), GCC 13 / PC_DEBUG 31, all in the
pre-existing places (none new). **Nothing benched.** No protocol change (docs/protocol.md row 34 gains one sentence: the
screen's STOP during a host single aborts it). **Settings sector:** 1.00o5's max-light byte (OTHER_SETTING_OFFSET, after
new_autosetup) now holds `0x80 | brightback` (item 4); no new words.

Points for a successor:
- **1 RUN/STOP.** The owner's picture (chip red STOP, button amber "Stopped", trace updating) is what 0.2.65 showed while
  the PC captured with ARM: a host single stops the UI (`pc_hal_ui_set_run(0)`, row 34) and the screen shows each host
  capture, so the scope looked running with both controls saying stopped (`pc_hal_ui_set_run` already redrew both on
  SET_ACQ_STATE / STOP). Now `ui_run_view(runstate, single_wait, host_busy)` (ui_ctrl.c, pure) is the one state;
  host_busy (`pc_glue_host_busy`) = a host ARM armed / triggered, or DONE less than `UI_RUN_HOST_HOLD_MS` (1000) ago, so
  an averaging burst or the host's Bode sweep reads RUN without flicker. `scope_run_stop_text` / `scope_run_stop_button`
  draw from `scope_run_view()` and record what they drew (`scope_run_chip_shown`, `scope_run_button_shown`);
  `pc_run_follow` (pc_glue_ui_service, every pass) redraws a stale one (not in the file viewer; the button only on page
  one and not in the waveform viewer), so every writer is covered without a hook at each. The button shows the action:
  green "RUN" (black text) while stopped, red "STOP" (white text) otherwise, 16 px font; owner exception recorded in
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
  this state can cycle). **Inference to check on the bench:** if the owner's "by hand" was a plot tap,
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
  owner's "two fixed values" read as 1.00o5's pair: maximum and the slider's value.
- **5** `wiz_dlg.step` ("Step M of N": 4 with the generator, 3 without, the save screen then being step 3) is set next to
  the title in `wiz_build` (cal_wiz_ui.c title line only; fw-cal5's measurement code untouched). Calibration tab:
  `UD_CAL_BTN_X(i)` / `UD_CAL_BTN_W` 185, row at `UD_BTN_Y` 420, trim line and note at `UD_CAL_TEXT_Y` 374 / 394.
- **6 Bode row.** `UI_MM_BODE` 2, eight rows, `UI_MM_PITCH` 52, `UI_MM_BTN_DY` 9 (button 131 x 35 unchanged, 8 px to the
  separator), `MAIN_MENU_HEIGHT` 416 (y 46..461). `ui_mm_choose` row Bode: bode 1, spectrum 0, phosphor 0, roll_off in
  roll mode, no roll note (the note is the spectrum view's). `pc_glue_main_menu_mode` closes the view on any other mode
  row and opens it after the spectrum / phosphor / roll changes. Grey icon and label, tap ignored (the menu stays open)
  while `bode_view_available()` is 0. Removed: the generator menu's fourth row (`GEN_MENU_HEIGHT` back to 169,
  `bode_gen_row_*`, `PC_GEN_BODE_*`, `bode_view_request` and `bv.req`). Per the coordinator the Bode view's right bar and
  CTRL page are untouched (ux-bode mockup, separate batch). Section 42's walk-through enters the view with Menu > Bode
  plot now.

Answers for the owner:
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

**Bench walk-through (owner, coordinator on COM10; FEL load, no SD writes)** - `#VER fw=0.2.68,proto=1.20`. CH1 on the
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
6. Time/div snap (owner's repro): ACQ menu, a slow time/div at a low rate with deep memory, then a high rate: the
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
the icon de14622, the slider be0c93e / 6d849f6). Brief: implement docs/plans/bode-ui.md with the owner's decisions of
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

**Bench walk-through (owner, coordinator on COM10; FEL load only, no SD writes; parts: the RC 1 kOhm / 15 nF ceramic and
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
bumped (the coordinator numbers it). Host tests green (test_session 69,896 / 69,894 checks, check_vectors 98 vectors,
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

**Bench step (coordinator + owner; FEL load only, no SD writes):** FEL-load `build-gcc10-pc/fnirsi_1013d.bin`, then
`scope_info`: caps include SCREENSHOT (bit 6). `scope_screenshot` with the scope view running (calibration square on
CH1): the PNG must match the panel (grid, trace colours: CH1 yellow, CH2 cyan, top bar text, the MCP chip) - compare
with a photo of the screen; that is **T9**. Wrong colours (red and blue swapped, or a byte-swapped mess) = the format
assumption; a shifted or torn picture = row stride. Repeat in the spectrum view, the phosphor view, Frequency response
and Diagnostics. With a menu open: E_BUSY (expected). Time a full screenshot (about 1 s expected on Windows).
## 47. fw-freq2: Frequency response follow-ups, Spectrum slider, the grid rule (fw 0.2.71, protocol 1.21 unchanged, row 60 reason 8)

Branch `fw-freq2` from main 6c1e5bc (fw 0.2.69). Brief: the fw-freq follow-ups (section 45 "Not done" 1-6), the owner's
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
   root cause was not reproduced: if the owner still sees STOP ignored, note whether the chip read WAIT and whether
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
- The mini overview drawn in the slider tracks (owner 20:00, "design first"): not designed, not built.
- The mcp simulator's SimBode: reason 8 and the header-before-first-sweep rule (only if a test needs them).
- A Single sweep started from the CTRL page leaves the bar on the CTRL page, where RUN/STOP is not visible (the chip
  shows RUN); the owner may want the bar to return to page one. Ask before changing.
- The generator chip after a sweep shows the user's generator although the engine left the pin off (as 0.2.69); the
  view gives the generator back only on leaving.
- Spectrum: the slider has no Move-fast scaling (as the Scope's); touches at y 426..449 (the cards) do nothing (from
  y 436 before), y 450 down take the slider.

**Bench walk-through (owner, coordinator on COM10; FEL load only, no SD writes; parts: the RC 1 kOhm / 15 nF ceramic,
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

**Bench list** (owner with the coordinator; FEL load only, nothing flashed; unit on 0x1539). Build from the branch head
(`make gcc10`, the image hash follows the commit). Python: `from fnirsi1013d.scope import Scope; from fnirsi1013d import
analysis as A; s = Scope.connect("COM10")`.
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

**Open / for the coordinator**
- D 1..8 legacy EXT shift values are extrapolated (B5 measures them); 200 MSa/s uses 2 words from the 0x1538 model.
- Caps bit 11 READ_LAG_UNVERIFIED stays set until B2 is accepted (then clear it for AL3 in `pc_variant_from_version`).
- `pc_hal_seq_read` keeps its own strobe loop after `fpga_read_start` (same contract); fpga_read_adc_data's loop too
  (hot path with the correction).
- acal_track's A - B model drops sloped pairs, so a flipped order on 0x1532..0x1536 affects it little; not gated.
- mcp: `scope_event_log` gained `fpga_trace`; docs/mcp-tools.md was not regenerated.

## 49. fw-over: the slider track overview and tap to jump (fw 0.2.73, protocol 1.21 unchanged)

Branch `fw-over` from main 113ec80 (fw 0.2.71; not merged with main since: fw-bus took 0.2.72 / protocol 1.22 and
section 48 there, so this branch renumbered itself to 0.2.73 and this section is 49). Brief: docs/plans/slider-overview.md
with the owner's decisions of section 10 F (items 1-5, 21:20) and item 6 (21:36: the track takes the whole row, the
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
2. **Geometry** (slider_ov.h; owner item 6): a white line at y 452 over x 2..727, the track x 2..727 (726 columns) by
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

**Bench walk-through (owner, coordinator on COM10; FEL load only, no SD writes; CH1 on the calibration post, CH2 free;
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

**Addendum: decision 7, fw-over2 (fw 0.2.74, branch fw-over2).** The owner looked at 0.2.73 on the unit
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

## 50. fw-zoom: Scope tap to jump at touch-down, the zoom ladder (fw 0.2.76, protocol 1.22 unchanged)

Branch `fw-zoom` from main 1173abd (fw 0.2.74). Brief: docs/plans/task-queue.md "Scope tap-to-jump and zoom stepping"
(owner 10-19 22:20 on 0.2.73) with the owner's additions relayed at 22:22 (zooming to either end and back must never
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
   roll (the ACQ menu's mode button is the way). **The owner may want this differently** (0.2.74 stepped from
   200 ms/div into roll and back).
5. **statemachine.c** ACQ time/div box: on ACQ_SET_RATE writes `fpga_set_sample_rate` and redraws the rate boxes.
6. **SET_TIMEBASE mode 0 left alone** (docs/protocol.md note): it already picks a rate for every index (the documented
   table, 100 samples a division down to 500 ns, viable for records of 1450 samples and more) and the simulator mirrors
   it. Moving it to the ladder changes the rate a host gets for an index: it needs the simulator (`mcp` simulator.py
   `TIME_PER_DIV_SAMPLE_RATE`) and a protocol note; queue it if the owner wants it.
7. **test_acq_view.c** builds acq_view.c + tb_view.c + slider_ov.c on the host with the firmware's tables and stub
   globals (variables.h compiles on the host). It checks every (rate, time/div) cell at depths 250..16382 in both
   directions; five-leg walks (fast end, slow end, fast, slow, fast) from every cell, asserting the pair equals the
   ladder's after every step and at the ends, that an end is a plain stop and the opposite tap works at once; hand
   rates; the boxes; the stopped mode and its notice; the snap notice text; the owner's tap (y 460 left of the window,
   five Scope states: the window centres on the finger or stops at the track's start; inside the window, y 440 and
   y 449 never jump) and the Spectrum's (sov_jump on a Spectrum-shaped window). It tests the function the new
   touch-down call uses; the control-flow move in `scan_for_touch` itself is target code and needs the bench.

Not done / for the next agent: the jump is not drawn until the finger lifts or moves (the blocking touch loop); a left
handle pointer at the plot's bottom can still win a touch at x < 44 on the track (the handle column comes first, as in
0.2.73); X-Y and roll have no jump.

**Bench walk-through (owner, coordinator on COM10; FEL load only)** - FEL-load (`sunxi-fel -p write 0x7FFFFFE0
build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`), `#VER fw=0.2.76,proto=1.22`. CH1 on the 1 kHz calibration post.
1. The owner's tap: Scope, 16k memory, 5 MSa/s at 2 us/div (window narrow), RUN or STOP. Tap the track at y about 460
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

Owner 2026-10-19 22:28: spectrum.c lost `spec_draw_legend` and `INFO_Y1` (Waterfall and Density colour legends); the
warning line at the plot's top middle and the rest of both variants are unchanged. No test checked the legend texts.

## 52. fw-spzoom: the Spectrum's zoom ladder, span drives the sample rate (fw 0.2.77, protocol 1.22 unchanged)

Branch `fw-spzoom` from main 25fa3e6 (fw 0.2.76). Brief: docs/plans/task-queue.md "Spectrum zoom ladder" (owner 10-19
23:09, "let's try your recommendation"); coordinator 23:2x: option (a), the plain rule (the full axis runs at 50 MSa/s,
not 200 MSa/s as the queue item said; fixed there). Commits: 4a85e8a (the rule, docs/ui-guidelines.md section 2 "Span
drives the sample rate", queue wording), 3ec9e17 (spec_ladder.c/.h, acq_view_tb_for_rate, host_tests/test_spec_zoom.c,
test_acq_view.c test_tb_for_rate), 52c1c76 (spectrum.c), d49f9b6 (README "Spectrum view", CHANGELOG), then this
section. Image of d49f9b6 (GCC 10): `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the worktree, 741,376 B, sha256
91946f1598b9b0489606072588c6385b89a419ce34385883aeeca4af86ba316f (a later commit differs only in the identity).
Warnings GCC 10 36, GCC 13 31, as on main; none in the changed files; gcc10-nommu builds. Host tests all green (new
test_spec_zoom 685,416 checks; test_acq_view 1,900,576). **Nothing benched. No protocol change, no settings word,
PC_FW_VERSION not bumped** (the coordinator bumps at merge).

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
   it), 500 Hz at 1 kSa/s (N 2048), 1 kHz at 2 kSa/s (N 4096): there the 2.048 s capture ends it, as the owner asked.
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

Decisions to confirm with the owner / not done:
- **Option (b)**, the one-line alternative the coordinator named: the whole axis (or any stop at 25 MHz) at 200 MSa/s
  instead of 50 MSa/s: in `spl_rate`, `if((n > 1) && (stop >= SPL_FMAX_MHZ)) return 0;` (and test_spec_zoom's 50 MSa/s
  expectations, test_rate / test_walks / test_end_numbers, move to index 0).
- **The Time card is the waterfall's history span**, not the capture time the brief mentioned; nothing shows N / fs.
  If the owner wants it, a card "Capture" = N / fs (ui_meas.h kinds) is the place.
- **Capture time is N / fs as the owner put it; the record actually takes depth / fs.** At 16k memory and 2 kSa/s
  (N 4096) a record takes 8 s although N / fs = 2.048 s. Using `fpgasettings.totalsamples` instead of N in
  `spl_time_ok` is the fix if the bench shows long waits.
- Changing the FFT length (or the memory depth) does not change the rate; the next step uses the new N.
- A drag across rate boundaries blanks the view at each crossing ("Waiting for a capture at the new rate") until a
  capture arrives; the touch loop blocks, so the new spectrum shows after the finger lifts (as before).
- Auto's probe and period search still go by 1.00o5 time base indices; the snap notice of `acq_view_fit` could arm
  there (as on 0.2.76).
- Section 20's "span drives the time base past the ends (`spec_zoom`, `spec_tb_set`)" is history; this section is
  current.

**Bench walk-through (owner, coordinator on COM10; FEL load only)** - FEL-load (`sunxi-fel -p write 0x7FFFFFE0
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
## 53. fw-yield: host commands leave the modal screens (fw 0.2.78, protocol 1.23, row 63)

Branch `fw-yield` from main dd46518 (fw 0.2.76). Owner 2026-10-19 23:27 (task queue "Host commands leave the modal
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
modes, the request is answered there without closing anything); auto set and slow UI captures (no modal loop: the
request waits for the main loop, E_BUSY detail 2 after 1.5 s). BODE_CTRL (every action, also status) and READ_BODE
have no F_BUSY flag, so they close a menu like any main-loop command.

**Simulator and host** (0f63317): `protocol.py` `YIELD_MINOR`, `BusyDetail`, `BUSY_TEXTS`, `busy_hint` (DeviceError's
E_BUSY hint), link names 68-69; `server.py` `ensure_control` proceeds with `MENU_CLOSED_NOTE` on 1.23 and raises the
old "close it on the scope" error below it; the simulator's `cfg.proto_minor` (below 23 = old behaviour), `ui_hold`,
`ui_yields`, `ui_yield_refused` (its GET_LINK_STATS stays the 31-counter 1.2 layout, as before).

**Bench list** (owner present, coordinator on COM10; `fnirsi1013d-bench` or the MCP tools; `#VER` fw=0.2.78
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

## 54. fw-cap: FPGA 0x153A capture stages, the wizard's zero back at 128 (fw 0.2.79, protocol 1.24, row 64)

Branch `fw-cap` from origin/main 6125aee (fw 0.2.78, 0x153A merged). Brief: firmware for bitstream 0x153A (fpga/al3/AGENTS.md
"0x153A status" firmware notes, README section 10) and the owner's fail-fast decision of 2026-10-19 20:30 (task queue
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

**Bench list** (owner present, coordinator on COM10; fw 0.2.79 FEL-loaded on the unit with 0x153A flashed per
fpga/al3/README.md section 10 and docs/hardware-status.md 00:30; back up SD 700-710 before the wizard's Save):
1. Identity (README 10 item 1): `#VER fw=0.2.79,proto=1.24,fpga=0x153A`; GET_INFO caps bit 19 (0x80000) and bit 18;
   GET_STATUS 44 = 0x00FF; `s.health()`: layout 2, 104 = 0x08E4, 106 = 0x01 (HAS, not trained); FPGA 0x36 via
   READ_REG / DEBUG if available reads 0x08E4. After one 200 MSa/s capture with nothing connected: 106 bit 2 set, counts
   108..115 all 0, 107 = 0; the Calibration page line "(not trained: calibrate): capture clean".
2. dcoffset sweep at 200 / 100 / 50 / 20 MSa/s, range 0 (README 10 item 2; host): every core clean at means 126..146;
   GET_HEALTH 120 (flagged) stays 0 at the quiet points.
3. RC low-pass with the 1 kHz square (item 3): no rails, no jumps; Frequency response unflagged; GET_HEALTH flagged
   captures 0 (both cores of a chip see the edges alike).
4. The owner runs the wizard (item 4, on the screen): Zero level shows "Capture stages: CH1 A4 B4" / "CH2 A3 B4" (or a
   neighbour) under the two ticks, then the gain screens and Save. Then GET_CAL 356 = 1, 358 = the value, 360 = 0x153A,
   357 bits 0-1; Calibration page "(trained): capture clean"; power-cycle (FEL-load again): 104 = the trained value.
5. Training cross-check (item 5, coordinator over USB): CAL_PROBE mode 1 on CH1 then CH2 runs the zero step with the
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
   (the A cores at 128..145): that is the owner's intended behaviour.
Regression signs: the wizard passing on 0x153A with no "Capture stages" note (cs_has / init order), GET_HEALTH layout 1,
EXT edges 8 samples early (shift table not followed), the trained value lost after a power cycle (trailer not saved).

**Open**
- Coordinator: protocol row 64 and the version numbers are provisional; the mcp, vector and doc commits are on this
  branch (see the commit list); docs/hardware-status.md gets the bench results.
- 0x153B (gen8-features on top of 0x153A): `cs_has` already covers it; add its own caps when it lands.
- A per-core absolute check (both cores of a chip) would need a quiet-input moment outside the wizard; not built.

**Addendum (coordinator, fw 0.2.80, 2026-10-20 10:15).** Bench on 0x153A: the training's clean criterion failed on CH2 because the
0x64 threshold of 8 codes is under CH2's analog noise at range 0 (about 3 codes rms near code 128; the counters saturate at 65535
on noise); the firmware now writes 32 (`CS_THR_USE`), with which a clean stage set reads 0 on every core and a bad stage reads
hundreds (CH1 A4) or 65535 (CH2 B2). The stage map of this unit: CH1 A bad at 4 only, CH1 B bad at 2, CH2 A clean everywhere,
CH2 B bad at 2, reference bit without effect; expected training result 0x08E3. docs/hardware-status.md 10:14.
