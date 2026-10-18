# firmware/ handoff for agents

State on 2026-10-18, fw 0.2.7 (protocol 1.3: shared control, SD sector commands; 0.2.7: SD_STAGE from the timer poll,
RX back-pressure counters, our version on screen, chip drawn with the top bar, temperature only with an RTC; 0.2.6:
menus hide what the hardware lacks, README "Hidden menu options"; 0.2.5: USB event counters; 0.2.3: chip above RUN/STOP and
steady, one-step SET_TIMEBASE). 0.2.1 passed the protocol-level
shared-control acceptance on the bench (owner's visual checks pending); 0.2.2 adds SD_* and two startup counters,
built and host-tested, not benched. Read `CLAUDE.md` (repo root) first, then
this file. For details, this file links to `firmware/README.md` (build, SD layout, FEL, memory budget, bring-up
checklist), `docs/protocol.md` (the spec; section 14 lists every deviation from it) and
`docs/plans/firmware-usb-protocol.md` (the original plan). It does not repeat them.

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
`variables.h` (trace buffers rounded up to 4096 words). List with reasons: README section 5.

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
| `make -C host_tests test` | host gcc: test_protocol, test_session (release + PC_DEBUG), check_vectors.py |
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

The SD card keeps Atlan's v1.00o5 package (boot mode 4). One serial user at a time; ask the owner to keep hands off
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
  was first seen (counter rx_swept): 1.00o5's `irq_handler` clears INTC pending by writing 0 after reading it,
  which can drop an interrupt.
- INDEX (EPIND) discipline: every non-USB-ISR path that touches indexed endpoint registers masks IRQs (CPSR I) and
  saves/restores EPIND (`pc_irq_save`). USB ISR paths set EPIND explicitly; IRQs do not nest.
- 1.00o5's bus-reset handler flushes CSRs with EPIND = 0 (not EP2); `pc_usb_isr_link` flushes EP2 itself.
- SOF interrupt is not enabled in protocol builds.
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
- MCP chip (`pc_ui_service`, `pc_hal_indicator`): 0.2.3 steady green, 38 x 17 at x 127 y 3, directly above the
  RUN/STOP chip, in the RTC clock's slot (the main-loop RTC draw is gated by PC_RTC_ALLOWED while the chip shows);
  on while DTR or a frame in the last 2 s, redrawn every 100 ms. Owner rejected the 0.2.1 spot (x 697 y 22 under the
  battery) and the pulse.
- SET_TIMEBASE mode 0 (0.2.3): sets index + rate + short mode, FPGA, then one scope_preset_values +
  scope_acqusition_settings. 0.2.1 called scope_set_timebase and the owner saw flicker through time/div values; the
  exact mechanism was not identified (host requests never use the UI's one-index step routine). Bench: confirm.
- Unverified on the bench (acceptance list in README section 5, step 6): screen redraws after host SET_*, marker
  search for the trigger level on every range, the UI showing a host capture, the chip position and colours.

## 7. FPGA layer and capture geometry

Variant table from the 0x06 word in `pc_variant_from_version` (0x1432 stock: ring 4096, fixed 1500/3000;
0x1532 AL3: ring 8192, max 16382; 0x1632 EF2 / 0x1732 EP4: ring 6144, max 12286; unknown: caps 0). The owner's
unit runs AL3 0x1532. Host ARM uses its own sequence (`pc_hal_arm`: 0x0F=0, 0x1A=mode, 0x01=1, bounded 0x05
wait, 0x0B/0x0C/0x0E in reset, 0x01=0); DONE latches 0x14 and writes 0x0F=1; capture start =
(trigger_addr - pretrigger_words) mod ring (the UI keeps 1.00o5's `T + 7441` math). READ_RAW reads per core with
one dummy strobe after each 0x1F write, splits at the ring end, `read_lag` = 0. READ_TRACE runs the 1.00o5
readout (`fpga_read_sample_data`, B before A, no dummy read).

Unverified (caps UNVERIFIED bits stay set until they pass; `BENCH: Tn` comments mark the code):
T1 ZLP (passed on Windows and Linux), T2 throughput (1.0 MB/s LOCAL, 2.35 MB/s REMOTE on Windows, >1 MB/s Linux),
T3 read_lag / stale first byte (DEBUG_FPGA image), T4 A/B interleave order (generator loopback, 200 MS/s),
T5 trigger_addr alignment, T6 0x18 after auto timeout, T7 FORCE via 0x1A=0 (PC_DEBUG), T8 volts per code with a DC
reference on every range, T9 RGB565 framebuffer (READ_SCREEN not built), T10 stock trigger core (needs the stock
bitstream restored from backups/). Procedures: protocol section 12, README section 5 checklist.

## 8. Known issues (numbers from the bench, 2026-10-18)

- Request corruption: fixed in 0.1.6 (RX ring-wrap split FIFO read, see section 4) and confirmed on the bench: 0.1.6
  census 2000 PINGs 0 E_BAD_CRC 0 no response, 100 x 64 KiB 0 failed, hdr_crc 0, payload_crc 0. The 0.1.5 census had
  77 E_BAD_CRC + 9 no response, every diff starting exactly at a 2 KiB boundary of rx_bytes. 0.1.7 adds
  pc_rx_ring.h + the all-phase host test (no behaviour change). PC_FIFO_MAP=1 can go.
- Startup, 0.2.2 bench: counters 27-30 reported as 2, 1515, 30, 1 (the first capture "processed" before the first
  conversion started, which program order rules out: probably an index or decoding slip, raw list requested).
  0.2.4 adds first-pass marks 31-34 (after battery, acquisition, touch, RTC; ms + 1). Suspects for the ~1.5 s: the
  touch handler's first scan, the RTC block (only with the RTC enabled), the first full draw. Earlier:
  bench 0.2.1 counters 27/28 = 3 ms to the first MAIN poll, 1516 ms first UI pass (the earlier "5 s"
  included the host's startup grace). Nothing slow runs between usb_device_init and the loop. 0.2.2 adds counters
  29/30: first MAIN poll -> first UI conversion started (after its 0x05 wait), -> first UI capture processed. Read
  them to split the pass into battery/0x05 wait, capture, and drawing/touch/RTC; then trim the slow part (or let the
  first pass skip it). Until then hosts see `ui_busy` = 2 STARTING. History: fw 0.1.3, Windows, no retries, idle scope: PING x1000 LOCAL 42
  E_BAD_CRC + 4 no response, REMOTE 40 + 5; 64 KiB x100: 1 (LOCAL) / 8 (REMOTE) no response. 0.1.2 (OUT FIFO at
  0) had 0 E_BAD_CRC but ~2% no response. Responses are always clean. 0.1.5 adds GET_BAD_FRAME, the RX sweep,
  GET_LINK_STATS and PC_FIFO_MAP=1. Next step: `bench_link.py COM10 --census 2000` on map 0 and map 1, read the
  diffs (which byte offsets, what content: EP0 setup bytes `a1 21 ...`? shifted data? constant pattern?) and the
  counters (rx_swept, link_resets, rx_packets vs requests sent). Acceptance: 0 E_BAD_CRC, 0 no response in 2000
  PINGs + 100 x 64 KiB. Ideas not yet tried: unload EP2 OUT with 32-bit reads only (pad the ring position to 4),
  read RXCOUNT twice, check RXCSR DataError/Overrun on bad packets, try the FIFO on Linux to separate host effects.
- RX corruption under back-pressure (bench 0.2.3 SD persist): rx_stalls 307,837, hdr_crc 660, duplicates 658, then
  the host's WriteFile failed (error 22) and the device re-enumerated. Not explained by code reading: only the ring
  code unloads EP2 OUT, the stall path clamps the count before the room check, and the release path always has
  room for one packet. The stall count is far above the packet count, so something re-enters the stall branch
  without new data: 0.2.7 counts the branches (GET_LINK_STATS 41-46). The hdr_crc/duplicate pairing looks like host
  retries of requests that had been executed (each SD_STAGE waited a UI pass, each SD_WRITE up to 4 s). 0.2.7
  serves SD_STAGE from the timer poll, which removes the back-pressure regime; hosts should write <= 32 sectors.
- Startup: 1515 ms from session init to the first MAIN poll (0.2.4/0.2.5 bench, counter 27); the first pass is
  31 ms. Prime suspect: `load_picture("scope.bmp")` (800x480 BMP, ~1.1 MB from FAT at ~730 KB/s) after
  usb_device_init. Not changed: dropping or deferring the splash is the owner's call.
- USB "Error state" incident (0.2.2, lead): port closed, scope idle ~10 min, then Windows showed the device present but
  in an error state (CreateFile failed); a cable replug recovered it. Not reproduced, cause unknown. Code reading:
  the suspend path calls pc_usb_isr_link, which flushed a held-back OUT packet but left EP2 OUT masked in INTRXE
  (only SET_CONFIGURATION unmasks it, and a resume sends none), so RX fell back to the 2 ms sweep: fixed in 0.2.5
  (unmask in the link hook and on RESUME when configured). That would not make Windows fail the device, though.
  Other candidates not verified: Windows selective suspend of the idle CDC device followed by a failed resume or
  reset-resume; the 1.00o5 bus-reset handler writes TXCSR/RXCSR = 0 and the TX FLUSH bit with EPIND 0 (on CSR0 that
  bit is DataEnd). 0.2.5 counts bus resets, suspends, resumes, disconnects and EP0 setups (GET_LINK_STATS 35-40):
  read them before and after a reproduction (port closed 10+ min). Host-side check: disable "Allow the computer to
  turn off this device" for the port / USB selective suspend and see whether it still happens.
- pass_max_ms showed 1515 after boot in 0.1.2: that was the boot interval, excluded since 0.1.4.
- Latency: PING ~1 ms (ISR poll); non-safe commands wait one UI pass (35-75 ms; 1.2 REMOTE was ~0.2-0.6 ms).
- Upstream: 32 GCC 13 warnings (unused variables, implicit `usb_CDC_in_ep_callback` in menu.c), menu debug helpers
  print nothing in protocol builds, 1.00o5 UI pointer math off by one in the wrapped case (left as is for LOCAL).
- ENTER_FEL works on the bench (hands-free loop proven). It does not save settings.

## 9. Conventions

- No libc (`-nostdlib`): no strcmp/printf; memcpy/memset/memcmp/memmove come from the .s files. No `double` or
  float in acquisition or protocol paths (soft float). 64-bit integer math is fine (libgcc).
- PC_PROTOCOL=0 must stay byte-identical (`make check-reference`). Hardware behaviour in LOCAL must not change.
- Small commits, one topic each, touching only firmware/ (and docs/protocol.md section 14 for deviations),
  message ending with the Co-Authored-By line. Python edits from Windows: open files with `newline=''` (and
  `encoding='latin-1'` for the 1.00o5 sources, which contain non-ASCII comments) or line endings/encoding break.
- Bump `PC_FW_VERSION` (pc_cmds.h) for every image handed to the bench; additions to the protocol bump
  `PC_PROTO_MINOR` (pc_protocol.h) and get a row in protocol section 14; tell the MCP agent.
- Report every bench image with path + sha256 (`build-gcc10-pc/fnirsi_1013d.bin`).

## 10. TODO, in priority order

1. Bench the shared-control acceptance on 0.2.0 (README section 5 step 6) and read the startup counters.
2. Bench the SD commands (0.2.2, protocol section 14 row 40): SD_INFO VERIFY on the owner's card (sector 750 must
   show eGON.EXE, check 1), SD_READ_SECTORS of 16 / 80 / 750 against backups/, then persist an image with
   SD_STAGE + SD_WRITE_SECTORS from 750 (back up 750..2047 first), SD_INFO VERIFY, power-cycle boot. Host tool:
   mcp/ (not this agent's). Design as built (the original notes below are kept for reference):
   - SD_STAGE fills a 64 KiB staging buffer (484 B per request); SD_WRITE_SECTORS commits up to 128 sectors with
     magic 'SDWR' and the stage CRC-32, reads back, compares, answers the CRC on the card. Window [16, min(first
     partition, 2048)); no valid MBR = writes off; 16-79 need flag BOOT, 708-710 flag CONFIG. Power-key EINT held off
     during writes (IRQs stay on: the SD driver times out on timer0 ticks). New error E_IO 13.
   Original design notes:
   - hardware-changing tier (1.3: no REMOTE; refuse while a capture runs or require STOP); MAIN context (SD access is not reentrant; the power-off ISR also writes
     SD, so mask the power interrupt or check `viewactive` around the write).
   - Allowed range: sectors 16..2047 and below the first partition start read from the MBR at runtime; refuse
     708-710 (calibration, settings, display/boot byte) unless an explicit flag targets them.
   - Payload: u32 sector, u16 count, data (count x 512, at most 492 B per request frame, so sectors go in pieces:
     either a 512-byte staging sector assembled from offsets, or raise the request frame limit for this command).
     Read-back verify after every write, answer the CRC-32 of what is on the card.
   - SD_INFO: card size, partition start, eGON header (magic, length, checksum) at sectors 16, 80 and 750.
   - Driver: `sd_card_read`/`sd_card_write` in sd_card_interface.c (used by close_open_menus for sector 710).
3. Bench tests T3-T8 and T10, then clear the UNVERIFIED caps / set FORCE_TRIGGER in release builds.
4. Phase 2/3 commands: READ_LIVE, READ_SCREEN (T9), ACQUIRE_AVG (128 KiB accumulators), full-speed fallback.
5. Per-unit USB serial (SID/eFuse) and a real pid.codes PID before anyone else flashes this.
6. Optional: composite CDC + MSC (needs TinyUSB-style rework; see plan section 9).
