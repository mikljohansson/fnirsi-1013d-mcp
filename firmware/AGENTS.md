# firmware/ handoff for agents

**Branch fw-ui2 (2026-10-19): fw 0.2.53 (provisional), protocol 1.16 unchanged: UX makeover step 2 (CTRL page with
fold-outs, strips and spectrum header out, spectrum cards, warnings in every variant, top bar chips), section 31.**
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
  cal_wiz_ui.c             the wizard's full-screen view, touch loop and the zero / gain measurements (section 27)
  phos_map.c/.h            phosphor hit-count map: rasteriser, decay, render, palettes. Pure C, host-buildable (section 24)
  phosphor.c/.h            phosphor view (the Display fold-out's switch since 0.2.53; hooks in scope_display_trace_data;
                           section 24, 31)
  ui_ctrl.c/.h             CTRL page geometry, fold-outs, value names, warning line, spectrum cards. Pure C (test_ui; 31)
  ctrl_page.c/.h           the CTRL page and its fold-outs: drawing and the modal touch loops (section 31)
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
reference on every range, T9 RGB565 framebuffer (READ_SCREEN not built), T10 stock trigger core (needs the stock
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
| 0.2.53 | f372581 | a182ba84 | UX makeover step 2 (fw-ui2, section 31; provisional number): CTRL page in both modes (Display fold-out with icons and the Phosphor switch with Persistence / Palette / Clear / wfm/s, Trace style or Analyse with Scale and Ref level, Grid switch, compact V+ / CH / V- steppers, 50 % TRIG in Scope), ACQ menu acquisition only, phosphor and spectrum strips and spectrum header gone (spectrum cards Peak / Level / RBW / Time), empty cursor and measures slots in Spectrum, warning line in every scope variant, top bar chips one construction, settings word 211 | built, host-tested (test_ui 6672 checks); bench walk-through in section 31 |
| 0.2.54 | f9fcac6 | a4f0c832 | touch fix batch 1 (fw-touch1, section 32; provisional number): left pointers are drag handles (`touch_pick.c`, 44 px boxes, nearest / on-top pick), pick at touch-down, nearest enabled zero line, no trigger-point move in a channel drag, channel + trigger level share one clamped delta, EXT no longer follows CH2, traceposition 6..396 in the new auto setup and at load |
| 0.2.51 | 8f5bf16 | 38580def | UX makeover step 1 (fw-ui1, section 29): main menu Scope / Spectrum analyser / Saved pictures / Saved waveforms / Settings / Diagnostics / USB drive with one 59 px hit area per row, Spectrum in roll mode switches Roll off; USB drive screen text + Exit; no top-bar version; Display row Normal / Phosphor / X-Y in the acquisition menu (X-Y out of System settings); strips without Scope / Auto, AUTO SET = spectrum Auto; phosphor warnings top-middle; cards with chip, scope-maker names, 4 significant digits, defaults Freq / Vpp / Vavg | built, host-tested (test_ui 1513 checks); bench walk-through in section 29 |
| 0.2.50 | e871c50 | 1b55efc2 | calibration wizard (one modal three-step flow, 48e5737) and calibration v4: A - B interleave tracker applied in the full class, full-class common-mode class probe, GET_CAL 288 bytes, diagnostics lines (protocol 1.14, row 53; fw-cal3, section 28) | built, host-tested; bench lists README 6.4 and 6.5 |
| 0.2.52 | c8f874b | e0968392 | diagnostics readers (fw-readers, section 30): GET_HEALTH 0x000A (USB state, CP15, power-on count, operating minutes, settings save state, stored restart record), unit life record in settings words 224..255 with one sector-709 write per boot, image identity record + SD_INFO IDENT (112 bytes), calibration provenance in sector 708 432..487 + GET_CAL 356 bytes, SAVE_REPORT 0x0504 (protocol 1.16, row 55) | built, host-tested (test_diag 462 checks); bench list in section 30 |

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
  `spec.wait` until a capture at the new rate); Auto is a 3-stage state machine (`spec_auto_start` / `spec_auto_step`);
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



## 31. fw-ui2: UX makeover step 2 (fw 0.2.53 provisional, protocol 1.16 unchanged)

Branch from main 9cc599c (fw 0.2.51), merged with main d31ee84 (0.2.52 readers, 0.2.54 touch) in 2ebef4d. Plan:
`docs/plans/ux-makeover.md` section 7 step 2; rules `docs/ui-guidelines.md`; user-facing description README section 5
"Screens: CTRL page, fold-outs, chips, warnings, spectrum cards". Commits, one topic each: 93f1b68 (`ui_ctrl.c/.h`, the
card formatters in `ui_fmt.c`, test_ui), aaa0613 (top bar chips), a41ca39 (CTRL page, fold-outs, strips and header out,
warnings, ACQ menu, settings word 211), e1900ff (0.2.53, CHANGELOG, README), 2ebef4d (merge), f372581 (AUTO SET label
only on page one), then this handoff. Image from f372581: `firmware/scope/build-gcc10-pc/fnirsi_1013d.bin` in the
worktree, 536,576 B, sha256 a182ba84645d52ff9eaf147436ab7dac6dfcdbb53f97b0dbc7ffef4cf38a507c, ELF
37df0b0c1c996aeb8ce2cea807e11bbc7df6821a5236b96eeeb5f70f9bca8316 (nommu bin 3316da3c...). **Nothing benched.** Host
tests green (test_ui 6672 checks, the rest unchanged). GCC 10 warnings 36, GCC 13 / PC_DEBUG 31, upstream only; the
new files are clean with -Wall -Wextra. No protocol change: GET_STATUS byte 43 keeps its meaning (4 = phosphor drawn,
now also in X-Y). Settings words: 210 bit 3 (the Phosphor switch), 211 (0xC700: bit 0 Grid, bits 1-2 the spectrum's
Display variant); 212..223 stay free.

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
  a card is the review's suggestion A8); the viewer's and DC shift pages keep 1.00o5's look; the Settings fold-out is
  step 3 (fw-ui3); fold-outs pause the trace and the PC (E_BUSY after 250 ms) like every dropdown.

**Bench walk-through (owner, coordinator on COM10)**
1. FEL-load the image, `#VER` fw=0.2.53 proto=1.16. Top bar: CH1, CH2, ACQ and the trigger chip each a grey box with
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
   Waterfall. DEFAULT (settings): Normal, Phosphor off, Grid on.
12. Waveform viewer: page two is 1.00o5's (V+ / V-, Show grid).
Bug indicators: a stepper half acting on the other channel or the wrong direction; a fold-out leaving a stripe of
the panel on the plot (stopped scope); AUTO SET painted over Analyse; the right bar showing Scope items in the spectrum
or empty slots in the scope after a mode change; the warning line in the waveform viewer; cards overlapping the
frequency labels; Phosphor switch on but no graded picture in the scope view outside roll mode.

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
