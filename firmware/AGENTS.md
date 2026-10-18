# firmware/ handoff for agents

Handoff fw-gen3 -> fw-gen4, 2026-10-18. Latest build: **fw 0.2.20** (protocol 1.5, race-free USB tick fallback,
section 11), 8 bench passes clean, 30-minute capture soak running at handoff. The SD card runs **0.2.11**, which stays there until the self-restart below is fixed. Read the root `AGENTS.md`
first, then this file. Details live in `firmware/README.md` (build, SD layout, FEL, memory budget, boot, bring-up
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
  pc_hooks.h               the hook macros used in 1.00o5 files; empty with PC_PROTOCOL=0
  pc_decim.c/.h            decimation filter maths for FPGA 0x1534 (N, K fallback, S, L16, Q24 scale, settle). Pure C,
                           host-buildable (section 19)
  host_tests/              test_protocol.c, test_rx_ring.c, test_session.c (+ mock_hal.c FPGA/transport model), vector_check.c,
                           check_vectors.py, gen_vectors.py, bench_link.py (pyserial, bench only)
firmware/tools/            mksunxi.py, make_sd_image.py, fetch_gcc10.sh, spiflash tools (not this agent's)
firmware/reference/        Atlan's v1.00o5 binaries (not committed)
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
| `make check-reference` | PC_PROTOCOL=0 with GCC 10 + Atlan's build_time.h, `cmp` against Atlan's binary: must print IDENTICAL |
| `make PC_DEBUG=1 BUILD=build-dbg` | adds DEBUG_FPGA + FORCE (caps 0x120) for bench tests T3/T7 |
| `make PC_FIFO_MAP=1 ...` | alternative USB FIFO layout (version string `-m1`) |
| `make -C host_tests test` | host gcc: test_protocol, test_rx_ring (all 2048 ring phases), test_session + mock_hal (release + PC_DEBUG; 762 / 760 checks on 0.2.19), check_vectors.py (79 vectors). None of them models usb_interface.c, the IRQ dispatcher or the timer-tick USB path |
| `python host_tests/bench_link.py COM10 [--census 2000] [--fel]` | on the bench PC, needs pyserial |

"IDENTICAL to reference" means: with PC_PROTOCOL=0 every hook compiles away and the image is byte-identical to
what Atlan shipped, so the protocol cannot have changed the base when disabled. Also verified for GCC 13 against a
GCC 13 build of the untouched tree. Keep it that way: every change in a 1.00o5 file goes under `#if PC_PROTOCOL`,
and `make check-reference` runs before every commit that touches one. Warning policy: the 32-33 upstream warnings
stay; new code must add none under GCC 13 or GCC 10 (compare sorted, line-number-free warning lists of
PC_PROTOCOL=0 vs 1 builds; new files are also clean with -Wextra).

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
| 0.2.37 | PENDING | | dB conversion in ARM assembly (3 accesses per bin, byte-identical to C) | built, host-tested incl. qemu |

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
[5] exception type, [6] exception LR, [7] SPSR, [8] exception SP, [9] exception magic.

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

**GET_EVENT_LOG 0x0009** (protocol 1.5, any poll context). Request: empty, or since 0.2.18 u8 flags (bit 0 = re-arm
the snapshot after this read; other bits E_BAD_ARG detail 0). Response: u16 n (<= 128; 64 before 0.2.15), u16 0,
u32 total events since boot, u32 now (ms, same clock as uptime_ms), n entries oldest first (u32 ms, u8 type, u8 a,
u16 b). Since 0.2.15 a snapshot tail follows: u16 m (<= 16), u16 frozen (1 = a bus reset froze it), u32 reset ms, m
entries (the events before the first bus reset since the snapshot was armed). 0.2.15-0.2.17 re-armed at every read;
0.2.18+ only with flag bit 0. 0.2.24+ appends the bus-level ring: u16 k (<= 16), u16 0, u32 total, k entries (types 1-4, 7, 8, 12, 13, 15-18 only); identical EP0 setups are logged at most once per second. Types (pc_transport.h): 1 bus reset, 2 suspend, 3 resume, 4 disconnect, 5 EP0 setup
(a = bRequest, b = bmRequestType << 8 | wValue low), 6 EP0 packet not 8 bytes (b = COUNT0), 7 SET_CONFIGURATION,
8 CLEAR_FEATURE(HALT) (a = endpoint), 9 DTR (a = new DTR), 10 RX ring full, 11 RX release, 12 TX flushed, 13 TX
aborted, 14 retired since 0.2.21 (0.2.12-0.2.20: flags found by the tick, a = INTUSB, b = INTTX << 8 | INTRX; at most one per 100 ms; 0.2.20: low 7 bits each as read, bit 15 EP0 condition, bit 7 EP2 OUT condition), 15 restart
(0.2.19, section 10).

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
- PC_PROTOCOL=0 must stay byte-identical to Atlan's image: `make check-reference` prints IDENTICAL before every
  commit that touches a 1.00o5 file, and every such change goes under `#if PC_PROTOCOL` or a pc_hooks.h macro.
- No new warnings under GCC 13 or GCC 10 (compare sorted, line-number-free warning lists against PC_PROTOCOL=0).
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

## 20. Spectrum analyser (fw-gen5, fw 0.2.26-0.2.31)

Spec and design: docs/plans/dpos350p-features.md section 2; README section 5 "FFT core" and "Spectrum view"; protocol
section 14 rows 44 (FFT_BENCH) and 45 (READ_SPECTRUM, GET_STATUS byte 43).
- Files: `fx_fft.c/.h` (FFT, windows, dB; host-buildable, ARM assembly kernels with C twins), `fx_fft_tables.c`
  (generated by `firmware/tools/gen_fft_tables.py`, committed), `spec_fmt.c/.h` (number formatting, host-tested),
  `spectrum.c/.h` (FFT_BENCH, the view, READ_SPECTRUM). Hooks: `PC_SPECTRUM_DISPLAY` at the top of
  `scope_display_trace_data`, `PC_SPECTRUM_TOUCH` in the trace-area branch of `scan_for_touch`, `PC_MAIN_MENU_*` for
  the sixth main-menu row (`MAIN_MENU_HEIGHT` is runtime in protocol builds), `spectrum_new_data` in the UI capture
  hook and in `pc_hal_trace_build`, `spectrum_service` in `pc_glue_ui_service`.
- Bench facts: CP15 control 0x0005107C (I-cache on, MMU off, so the D-cache bit is ineffective); about 90 cycles per
  uncached data access, the stack included; SRAM A only ~25% faster than DDR. That is why the hot loops are assembly.
  Every change to them: `make -C host_tests test-arm` (qemu-arm, byte-compares with the C twin) and objdump the loop
  for stack traffic. Inline asm with 64-bit operands forces even register pairs and made GCC spill; write whole loops.
- Constraints: u8 input offset 0..255 (clamped) for the Q22 window trick; i32 input is C only (64-bit products).
  Buffers 8-byte aligned (`fx_fft_init` refuses otherwise).
- The view: settings in RAM only; N limited by the record (`scopesettings.samplecount`); fs from the UI time base
  (`sample_rate_settings`), so a host capture with a raw divider is labelled wrong (documented in row 45).
Open (fw-gen5's list): owner verdict on the 0.2.32 waterfall / density views; the view on 0x1534 once it is flashed (0.2.34 code path: 16-bit records, C load; an assembly u16 load if it
matters).
- TODO (lead, later, not fw-gen5's): enable the MMU with a flat 1:1 section map, DDR cacheable, the framebuffers and
  any DMA regions uncached, peripherals strongly ordered. The D-cache bit is already set (CP15 0x0005107C) but does
  nothing without the MMU. Numbers that motivate it: 0.2.27 FFT_BENCH 4096 points 24.3 ms in DDR (~90 cycles per
  data access), 18.1 ms with buffers in SRAM; 0.2.31 after the assembly rewrite 10.9 ms DDR, SRAM 2048 points 2.3 ms
  vs 4.9 ms DDR. 1.00o5's per-pixel display fills pay the same price on every pass.
- Plan cache: `fx_fft_init` runs only when N, window or the plan's memory (SRAM for N <= 2048, DDR above) changes, or
  after FFT_BENCH borrowed SRAM (init 0.84 ms at 2048 in SRAM, 2.17 ms at 4096 in DDR on 0.2.31).

