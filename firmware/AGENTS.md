# firmware/ handoff for agents

State on 2026-10-18, fw 0.1.5 (protocol 1.2), commit c31bd01 and later. Read `CLAUDE.md` (repo root) first, then
this file. For details, this file links to `firmware/README.md` (build, SD layout, FEL, memory budget, bring-up
checklist), `docs/protocol.md` (the spec; section 14 lists every deviation from it) and
`docs/plans/firmware-usb-protocol.md` (the original plan). It does not repeat them.

## 1. Tree

```
firmware/scope/            Atlan4 v1.00o5 scope app + the USB control protocol (PC_PROTOCOL)
  pc_protocol.c/.h         frame layer: 16-byte header, CRC-16/CCITT-FALSE + CRC-32, parser with 100 ms
                           timeout and resync replay queue, ASCII probe lines. Pure C, no globals, host-buildable
  pc_cmds.c/.h             session: dispatch table, all handlers, LOCAL/REMOTE + acquisition state machines,
                           duplicate cache, deferred WAIT, link counters. Hardware only via pc_hal.h. Host-buildable
  pc_hal.h                 the HAL interface (front end, timebase, trigger, generator, FPGA acquisition, trace,
                           UI ownership, transport, FEL)
  pc_hal_scope.c           HAL on the scope (scopesettings/fpgasettings, fpga_control.c, display), the static
                           pc_session, the glue called from 1.00o5 (pc_glue_*), ENTER_FEL jump
  pc_transport.c/.h        USB CDC transport: 2 KiB RX ring + NAK back-pressure, 66,580 B TX staging + pump,
                           ZLP, link events, counters, FIFO layout constants (PC_FIFO_MAP)
  pc_hooks.h               the hook macros used in 1.00o5 files; empty with PC_PROTOCOL=0
  host_tests/              test_protocol.c, test_session.c (+ mock_hal.c FPGA/transport model), vector_check.c,
                           check_vectors.py, gen_vectors.py, bench_link.py (pyserial, bench only)
firmware/tools/            mksunxi.py, make_sd_image.py, fetch_gcc10.sh, spiflash tools (not this agent's)
firmware/reference/        Atlan's v1.00o5 binaries (not committed)
docs/protocol-vectors.json shared frame vectors; the MCP agent merges entries with "source": "host" into it
```

1.00o5 files touched (all under `#if PC_PROTOCOL`): `fnirsi_1013d_scope.c` (init + main loop), `scope_functions.c`
(12 spin-loop BUSY polls, UI capture counter), `touchpanel.c` (BUSY poll in `tp_i2c_read_status`),
`fpga_control.c` (BUSY poll in the 0x05 wait), `power_and_battery.c` (snapshot restore before the power-off save),
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
  OUT packets are read in one `usb_read_from_fifo` call into an aligned bounce buffer, then copied into the ring.
  Splitting the read at the ring end corrupted one request per 2 KiB received (0.1.2-0.1.5).
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
- DTR: pyserial asserts DTR on open. DTR 1->0 releases REMOTE (unless IGNORE_DTR) and flushes TX; DTR 0->1 flushes
  a leftover frame. Windows usbser sends GET_LINE_CODING when the app calls SetCommState (pyserial reconfigure),
  also mid-transfer. Windows-side reads of 20-33 KiB stalled in the host stack (fixed in mcp/); Linux cdc_acm
  reads every size up to 64 KiB.
- A response with no progress for 2 s is dropped; `#RESET` aborts one at any time.

## 5. Service contexts

| context | where | may touch | answers |
|---|---|---|---|
| ISR (`PC_CTX_ISR`) | timer0 IRQ, every 1 ms | transport, session reads | PING, GET_INFO, GET_STATUS, GET_BATTERY, GET_LINK_STATS, GET_BAD_FRAME, `#VER`, `#RESET`, error replies; never E_BUSY |
| BUSY (`PC_CTX_BUSY`) | touch-panel scans, UI spin loops, 0x05 wait | transport, session | same set; others E_BUSY once no MAIN poll for 250 ms (UI modal) |
| MAIN (`PC_CTX_MAIN`) | top of the main loop | everything (FPGA, UI, display) | all commands, deferred WAIT completion, link events, watchdog, ENTER_FEL |

Guard: `s->in_poll`. A poll returns at once if another is running (ISR skips are counted); `pc_remote_service`
holds it too. Everything not in the safe set (`F_BUSY` flag in the dispatch table) stays queued as `pending_ev`
for MAIN. One response in flight at a time (TX staging buffer); one further event is parsed and held, so `#RESET`
still works. In REMOTE the main loop only polls (+ banner touch scan every 100 ms, battery every 1 s): pass_ms ~0.

## 6. LOCAL / REMOTE

Protocol section 6. SET_REMOTE acquire (MAIN only, refused with E_BUSY when the UI is modal): memcpy snapshot of
scopesettings + fpgasettings, red banner in the top bar (x 150-690). Exit reasons: release, banner tap, watchdog,
DTR drop, USB reset/suspend/disconnect, `#LOCAL`. Exit restores the snapshot unless KEEP_SETTINGS, re-pushes all
settings with main()'s boot sequence, redraws (`pc_hal_remote_exit`). Power-off ISR restores the snapshot before
saving. LOCAL never touches the FPGA (GET_* read UI state; READ_TRACE copies the UI buffer).

## 7. FPGA layer and capture geometry

Variant table from the 0x06 word in `pc_variant_from_version` (0x1432 stock: ring 4096, fixed 1500/3000;
0x1532 AL3: ring 8192, max 16382; 0x1632 EF2 / 0x1732 EP4: ring 6144, max 12286; unknown: caps 0). The owner's
unit runs AL3 0x1532. REMOTE ARM uses its own sequence (`pc_hal_arm`: 0x0F=0, 0x1A=mode, 0x01=1, bounded 0x05
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

- Request corruption: root cause found in 0.1.6 (RX ring-wrap split FIFO read, see section 4); acceptance census
  still to be run on 0.1.6. History: fw 0.1.3, Windows, no retries, idle scope: PING x1000 LOCAL 42
  E_BAD_CRC + 4 no response, REMOTE 40 + 5; 64 KiB x100: 1 (LOCAL) / 8 (REMOTE) no response. 0.1.2 (OUT FIFO at
  0) had 0 E_BAD_CRC but ~2% no response. Responses are always clean. 0.1.5 adds GET_BAD_FRAME, the RX sweep,
  GET_LINK_STATS and PC_FIFO_MAP=1. Next step: `bench_link.py COM10 --census 2000` on map 0 and map 1, read the
  diffs (which byte offsets, what content: EP0 setup bytes `a1 21 ...`? shifted data? constant pattern?) and the
  counters (rx_swept, link_resets, rx_packets vs requests sent). Acceptance: 0 E_BAD_CRC, 0 no response in 2000
  PINGs + 100 x 64 KiB. Ideas not yet tried: unload EP2 OUT with 32-bit reads only (pad the ring position to 4),
  read RXCOUNT twice, check RXCSR DataError/Overrun on bad packets, try the FIFO on Linux to separate host effects.
- pass_max_ms showed 1515 after boot in 0.1.2: that was the boot interval, excluded since 0.1.4.
- Latency: PING ~1 ms (ISR poll), LOCAL non-safe commands wait one UI pass (35-75 ms), REMOTE ~0.2-0.6 ms.
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

1. Run the acceptance census on 0.1.6 (`bench_link.py COM10 --census 2000`: 0 E_BAD_CRC, 0 no response in
   2000 PINGs + 100 x 64 KiB, GET_BAD_FRAME count 0). If it passes, drop PC_FIFO_MAP=1.
2. SD_INFO / SD_READ_SECTORS / SD_WRITE_SECTORS (owner's ask: persist a FEL-loaded image to sector 750 without
   removing the card). Design agreed with the lead, not started:
   - REMOTE only, hardware-changing tier; MAIN context (SD access is not reentrant; the power-off ISR also writes
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
