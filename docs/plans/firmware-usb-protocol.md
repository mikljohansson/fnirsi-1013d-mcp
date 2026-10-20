# Plan: USB control protocol in the scope firmware

Status: plan, 2026-10-18. Target tree: `firmware/scope/` (bootstrapped from Atlan4 v1.00o5 in parallel; file
names below are the 1.00o5 names, `S100` = `research/atlan4-extracted/_dso1xxx/v1.00o5/fnirsi_1013d_scope/`,
`S27T` = the v0.27t "clear usb interface code" tree). Protocol: `docs/protocol.md` (cited as P-section).
Markers [V]/[M]/[U]/[D] as defined there.

## 1. Goal and acceptance

Done means: with the scope booted from SD into our firmware and plugged into a PC,

1. it enumerates as a CDC ACM port with the new VID:PID (section 10) on Windows 11 (in-box usbser.sys) and
   Linux (cdc_acm) without a driver install;
2. `#VER` answers in a terminal;
3. the Python client passes the protocol conformance suite (section 11) including 1,000 full AL3 captures
   read with READ_RAW without a CRC error or timeout;
4. the UI still works normally when no host is connected, and returns to normal after REMOTE release,
   user tap, watchdog and cable pull;
5. MSC mode (SD card as a USB disk) still works from the USB menu.

## 2. Starting point (what 1.00o5 has)

| Area | 1.00o5 state | Evidence |
|---|---|---|
| Descriptors | complete CDC ACM set, ST VID/PID 0x0483:0x5740, HS only | [V] `S100 cdc_class.c:45-154` |
| EP setup | EP1 interrupt IN 8 B (FIFO 64 B DPB), EP2 bulk OUT and IN 512 B double-buffered | [V] `S100 usb_interface.c:1120-1185` |
| Class requests | SET/GET_LINE_CODING, SET_CONTROL_LINE_STATE (DTR/RTS -> `usb_connect`) | [V] `S27T usb_interface.c:1771-1846`, same code in S100 |
| RX path | EP2 RX ISR reads up to 512 bytes into `bufferCDC[64]` (overflow), then calls the ASCII debug parser **inside the ISR** | [V] `S100 usb_interface.c:28`, `:1392-1420` |
| RX ring | `usb_rx[1024]` and `usb_CDC_out_ep_callback` exist but are unused | [V] `S100 cdc_class.c:8-10`, `:228-238` |
| TX path | `usb_tx[32768]` ring; `usb_CDC_in_ep_callback` drains it in 512-byte packets, busy-waiting on TxPktRdy, writes `EPIND` without saving it, no ZLP, `usb_tx_busy` is a plain flag | [V] `S100 cdc_class.c:245-296` |
| SOF interrupt | enabled in CDC mode (8,000 IRQs/s at HS) for nothing | [V] `S100 usb_interface.c:226` |
| Mode | `USB_CH340` setting: 1 = CDC at boot, 0 = MSC; the USB menu screen switches to MSC and back | [V] `S100 usb_interface.c:132`, `menu.c:284-314`, persisted `scope_functions.c:6920` |
| Parser | debug letters `a`..`k` set globals; `PC_interface.c` holds an unused `#ECHO` stub and the OEM `sysparam` struct | [V] `repo-atlan4.md` 2.3 |
| Main loop | battery, acquisition (non-blocking: arm when `display_data_done`, poll done once per pass), DEV overlay, touch, RTC | [V] `S100 fnirsi_1013d_scope.c:250-384`, `scope_functions.c:719-880` |

v0.27t vs 1.00o5: `cdc_class.c` is byte-identical in both trees (diff is empty) [V]. `usb_interface.c` differs
only from line ~1414 on: v0.27t calls `usb_CDC_receive_callback()` from the main loop
(`S27T fnirsi_1013d_scope.c:343`) and holds the `#WAV2`/`#VER` text protocol; 1.00o5 calls a debug parser
from the ISR [V diff hunks 1414-1680]. Enumeration, EP0 and class-request code are shared.

## 3. Reuse decisions

| Piece | Decision | Why |
|---|---|---|
| EP0, enumeration, PHY setup, `usb_device_init/enable/disable`, `usb_read_from_fifo`, `usb_write_to_fifo` (`usb_interface.c` up to ~1390) | **keep from 1.00o5** | works with Windows per Atlan4 ("YES work it with APP", v0.27r) [M]; identical to v0.27t |
| CDC descriptors (`cdc_class.c`) | **keep structure**, change VID/PID, bcdDevice, strings, serial | section 10 |
| Class requests (line coding, control line state) | keep; add a DTR edge event for the protocol layer | P-3.1 |
| Main-loop placement of the receive handler | **bring back from v0.27t** (call from the main loop, never parse in the ISR) | ISR-context parsing blocks other IRQs and races with main-loop FPGA access |
| v0.27t `#WAV2`/`#VER`/`#INFO` parser, `PC_interface.c` `sysparam` | **drop** | P-2 |
| 1.00o5 debug parser (`a`..`k`, `bufferCDC[64]`) | **drop** | overflow, ISR context |
| RX ring code in `cdc_class.c` | rewrite as 2 KiB ring filled from the ISR with back-pressure | P-3.2 |
| TX ring + blocking `usb_CDC_in_ep_callback` | replace by a staged-frame TX pump driven by the EP2 TX interrupt | no busy-wait in the main loop, EPIND-safe |
| `usb_CDC_send_text` | keep for the ASCII probe only | |
| pcprogrammer's `f1c100s_ch340` | reference only: SOF-kicked TX and `ch340Receive` polling from the main loop show the same structure works on this core [V `Test code/f1c100s_ch340/f1c100s_ch340.c:70-85`, `usb_interface.c:436-442`] | |
| TinyUSB `sunxi` port | **not now** | rewriting the stack is a bigger change than fixing two paths; revisit for composite CDC+MSC (section 9) |

## 4. Module layout

New files in `firmware/scope/`:

| File | Content | Host-buildable |
|---|---|---|
| `pc_proto.c/.h` | frame parser (byte-at-a-time state machine), CRC-16/CRC-32 (table, 1 KiB), frame builder, duplicate cache, ASCII probe line handling, dispatcher table `{cmd, handler, flags(LOCAL_OK, DEFERRED, CAP)}` | yes, no hardware includes |
| `pc_cmds.c/.h` | command handlers; translate protocol structs to `scopesettings`/`fpgasettings` and calls into `fpga_control.c`; remote state machine and acquisition state machine | via a HAL shim |
| `pc_hal.h` | the only interface `pc_cmds.c` uses for hardware: `hal_fpga_*`, `hal_ms()`, `hal_display_banner()`, `hal_readout_trace()` | yes (mock in tests) |
| `pc_transport.c/.h` | glue to `cdc_class.c`: RX ring read, TX staged-frame submit, link state (DTR, reset, suspend) | no |

Modified files:

| File | Change |
|---|---|
| `usb_interface.c` | EP2 RX ISR -> ring with back-pressure (6.1); EP2 TX ISR -> TX pump (6.2); remove `bufferCDC`, debug parser, `a`..`k`; disable SOF IRQ; DTR edge and RESET/SUSPEND/DISCONNECT -> `pc_transport_link_event()`; save/restore `EPIND` in the ISR |
| `cdc_class.c/.h` | descriptors (VID/PID/strings/serial), RX ring 2 KiB, delete `usb_tx[32768]` ring and `CDCSend` busy-wait, new `cdc_tx_submit(ptr, len)` |
| `fnirsi_1013d_scope.c` | main loop hooks (section 5), remote branch |
| `fpga_control.c/.h` | bounded waits, `fpga_read_raw()`, `fpga_set_capture_geometry()`, `fpga_read_status()`, 0x1732 recognition, poll hook in the reset-ready loop (section 7) |
| `scope_functions.c` | factor the readout out of `scope_acquire_trace_data()` into `scope_readout_capture(trigger_addr)` used by the UI and by READ_TRACE; modular pointer math (P-7.5) |
| `power_and_battery.c` | restore the UI snapshot before `scope_save_configuration_data()` when REMOTE is active (6.4) |
| `statemachine.c`, `menu.c` | USB menu: keep the MSC screen; refuse to enter it while REMOTE; nothing else |
| `variables.c/.h` | `UINT32_SAMPLE_BUFFER_SIZE` rounds down: `16382 / 4 = 4095` words = 16,380 bytes, 2 bytes short of `MAX_SAMPLE_BUFFER_SIZE` [V `S100 variables.h:383-384`]; use `(MAX_SAMPLE_BUFFER_SIZE + 3) / 4` (Donwulff fix, `repo-donwulff.md` section 5) |
| `PC_interface.c/.h` | delete (OEM `sysparam` stub) |

## 5. Where USB is serviced

### 5.1 Interrupt vs main loop split [D]

- **ISR (IRQ 26, priority 2):** only moves bytes between FIFOs and buffers and records link events. Never
  touches the FPGA bus, `scopesettings` or the display.
- **Main loop:** `pc_poll(ctx)` parses frames from the RX ring, runs handlers, and submits responses.

The FPGA bus is master-clocked by the MCU (`FPGA_PULSE_CLK`, data latched on the rising edge
[V `S100 fpga_control.h:31`, zaklad.v :577-581]), so a USB interrupt in the middle of a 16 k-byte readout only
stretches that loop; it cannot corrupt it [inference from the Verilog, consistent with pecostm32's open
question in `hack-repo-software.md` item 309]. No interrupt masking around FPGA transactions is needed for
USB. (The power-off ISR is different: it runs a full settings save and then hangs, see 6.4.)

### 5.2 Call sites of `pc_poll`

`pc_poll(PC_CTX_MAIN)` may touch the FPGA; `pc_poll(PC_CTX_BUSY)` must not (it answers PING, GET_INFO,
GET_STATUS with cached flags, GET_BATTERY, and `E_BUSY` for everything else).

| Site | Context | Why |
|---|---|---|
| top of the main loop, before `battery_check_status()` (`fnirsi_1013d_scope.c:258`) | MAIN | normal path; one main-loop pass is one acquisition + draw + touch scan |
| REMOTE branch of the main loop (5.3) | MAIN | runs continuously while REMOTE |
| `fpga_do_conversion` reset-ready loop `while((fpga_read_byte() & 1) == 0)` (`fpga_control.c:636`) | BUSY | bounded loop now (7.2); inside an FPGA read sequence, so no FPGA access |
| blocking `while (fpga_done_conversion() == 0);` loops in calibration and auto-setup (`scope_functions.c:1154`, `:1172`, `:1224`, `:1460`, `:1689`, `:1722`, `:1802`, `:1872`, `:2192`, `:2254`, `:2464`, `:2527`) | BUSY | these can wait a long time; keep the link alive and report `ui_busy` |
| modal touch loops: USB-MSC screen (`menu.c:288-325`), `tp_i2c_wait_for_touch_release()` | BUSY | in the MSC screen the CDC device is not enumerated at all, so only the release-wait matters |
| `timer0_delay()` calls longer than 20 ms | BUSY | e.g. 50 ms relay settling (`statemachine.c:6594`) |

In LOCAL the 1.00o5 acquisition is already non-blocking: `scope_acquire_trace_data()` arms when the
previous trace was displayed, checks done once per pass and returns [V `S100 scope_functions.c:719-880`].
So the main-loop site gives a poll every pass; the pass time is dominated by `scope_display_trace_data()`
and the touch scan [U: not measured; Atlan4 reports 90-128 captures/s on a 1014D, i.e. ~8-11 ms per pass
[M]].

Latency budget: the host's default response timeout is 1 s. The only known longer stalls are the
calibration and auto-setup routines (covered by BUSY polls) and SD writes for screenshots/waveform saves
from the UI [U duration].

### 5.3 Main loop with the remote branch

```c
while (1) {
    pc_poll(PC_CTX_MAIN);
    if (pc_remote_active()) {
        pc_remote_service();      /* poll FPGA 0x0A/0x18 if ARMED, deferred WAIT, watchdog,
                                     banner touch check every 100 ms, optional trace draw */
        continue;                 /* no UI acquisition, no touch_handler, no RTC redraw */
    }
    battery_check_status();
    /* ... unchanged 1.00o5 body ... */
}
```

`battery_check_status()` also redraws the battery icon; in REMOTE it runs once per second from
`pc_remote_service()` without drawing, so GET_BATTERY stays fresh.

## 6. Transport details

### 6.1 RX: ring with USB back-pressure

- Ring `pc_rx[2048]`, single producer (ISR), single consumer (main loop), indices `volatile uint32`.
- EP2 RX ISR: read `RXCOUNT`; if free space >= count, `usb_read_from_fifo` straight into the ring (two
  copies at the wrap), clear RxPktRdy. Otherwise **leave RxPktRdy set**, clear the EP2 bit in `INTRXE`, set
  `rx_stalled = 1`. The controller NAKs further OUT tokens; with the double-buffered FIFO up to two packets
  wait in hardware.
- Main loop, after consuming bytes: if `rx_stalled` and free space >= 512, run the same FIFO-read code with
  the USB IRQ masked at the INTC, then re-enable `INTRXE`.
- Fix the 64-byte buffer overflow by construction: there is no intermediate buffer.

### 6.2 TX: staged frames, interrupt-driven pump

- One static staging buffer `pc_tx[66560 + 20]` (P-8.2 `max_response_payload` + header + CRC), 32-bit
  aligned, in `.bss` (DDR). Handlers write their payload directly into `pc_tx + 16`; `pc_proto` fills header
  and CRC; `cdc_tx_submit(pc_tx, frame_len)` starts the pump.
- Pump state: `tx_ptr`, `tx_left`, `tx_zlp_pending`. `cdc_tx_submit` masks the USB IRQ, writes the first
  one or two packets (double buffer) if TxPktRdy is clear, unmasks. The EP2 TX-complete interrupt writes the
  next packet. When `tx_left` reaches 0 and the frame length is a multiple of 512, it sends one ZLP
  (TxPktRdy with an empty FIFO).
- The main loop does not parse the next request while a frame is in flight (window = 1, P-3.2), except
  control requests during a deferred op, whose small responses go through a second 64-byte staging buffer
  queued behind the current frame.
- Every ISR path and every main-loop path that touches indexed endpoint registers saves and restores
  `USBC_REG_EPIND` (1.00o5's TX callback sets EPIND = 2 without restoring it [V `S100 cdc_class.c:257`]).
- Remove the SOF interrupt enable in CDC mode (`usb_interface.c:226`); the TX-complete interrupt replaces it.
- If DTR drops or the bus resets while a frame is in flight: flush the FIFO (FlushFIFO bit), drop the frame.

### 6.3 Link events

| Event | Source | Action |
|---|---|---|
| DTR 0 -> 1 | SET_CONTROL_LINE_STATE | reset parser, clear duplicate cache |
| DTR 1 -> 0 | SET_CONTROL_LINE_STATE | drop TX frame; release REMOTE (exit_reason 4) unless IGNORE_DTR |
| bus reset, suspend, disconnect | MUSB INTUSB | as DTR 1 -> 0, exit_reason 5 |

All are flagged in the ISR and acted on in `pc_poll`.

### 6.4 Remote-mode hook into the state machine

Remote state lives in `pc_cmds.c`: `pc_ctl` (LOCAL/REMOTE), `pc_acq` (IDLE/ARMED/TRIGGERED/DONE/ABORTED),
`shadow_scope` (copy of `SCOPESETTINGS`), `shadow_fpga` (copy of `FPGASETTINGS`), `capture_id`,
`remote_flags`, `watchdog_deadline`.

Enter (SET_REMOTE acquire), executed from `pc_poll(MAIN)`:

1. If a UI conversion is running, nothing special: the next ARM resets the sample system.
2. Close open menus: call the same close path the statemachine uses when tapping outside a menu
   [U: identify the function in `statemachine.c`; if menus are modal loops instead, refuse with `E_BUSY`].
3. `memcpy(&shadow_scope, &scopesettings, sizeof)`; same for `fpgasettings`. `SCOPESETTINGS` holds ten
   `CHANNELSETTINGS` (two channels + eight refs), a few KiB [V `S100 variables.h:551-648`].
4. Draw the banner (top bar, 1 line), set `pc_ctl = REMOTE`.

While REMOTE, handlers modify `scopesettings`/`fpgasettings` directly and call the existing setters
(`fpga_set_channel_voltperdiv`, `fpga_set_channel_offset`, `fpga_set_sample_rate`, ...). So the existing
readout code sees consistent settings and READ_TRACE needs no second code path.

Exit (release, user tap, watchdog, DTR, reset):

1. Complete a deferred request with `E_ABORTED` (if the link is still up).
2. Unless KEEP_SETTINGS: copy the shadows back.
3. Re-push all settings with the `main()` sequence `S100 fnirsi_1013d_scope.c:131-169`, factored into a new
   `scope_apply_all_settings()` (also used at boot).
4. `scope_setup_main_screen()`, `display_data_done = 1`, `pc_ctl = LOCAL`, record `exit_reason`.

Power-off during REMOTE: `power_interrupt_handler` saves settings from the ISR and hangs
[V `S100 power_and_battery.c:94-110`]. Add at its top:
`if (pc_ctl == REMOTE && !(remote_flags & KEEP_SETTINGS)) { memcpy(&scopesettings, &shadow_scope, ...); ... }`.
The handler can interrupt a main-loop FPGA transaction; it does not touch the FPGA, only the SD card, which
the main loop also uses (screenshots, waveform saves). That race exists in 1.00o5 already and is out of scope
here [U].

Touch in REMOTE: `pc_remote_service()` calls `tp_i2c_read_status()` every 100 ms; a touch inside the banner
rectangle releases (exit_reason 2). Other touches are ignored. This keeps a person at the scope in control.

## 7. FPGA access layer

Reuse `fpga_control.c` as the only code that wiggles Port E. Additions:

| Function | Purpose |
|---|---|
| `fpga_get_version()` | add 0x1732 -> `fw_FPGA = 4` (EP4); code paths that test `fw_FPGA == 2` for AL3 must also cover 4 [V `S100 scope_functions.c:796` uses `== 2` "AL3 or EP4"] |
| `fpga_ring_words()` | 4096 / 8192 / 6144 by variant |
| `fpga_reset_and_wait(timeout_reads)` | the 0x01/0x05 sequence of `fpga_do_conversion` with a bound of 2,000,000 reads and a `pc_poll(BUSY)` every 65,536 reads; returns error. Replace the unbounded loop at `fpga_control.c:636` with it, for the UI too |
| `fpga_set_capture_geometry(pre_words, total_words, auto_timeout)` | 0x0B/0x0C/0x0E, only on variants >= 2, written while the reset is asserted as 1.00o5 does [V `fpga_control.c:641-655`, comment "send when the system is in reset"] |
| `fpga_read_status(&done, &triggered)` | 0x0A and 0x18 bit 0, no touch scan |
| `fpga_read_trigger_addr()` | existing `fpga_prepare_for_transfer()` (0x14 & 0x1FFF) |
| `fpga_read_raw(cmd, addr, count, dst)` | write 0x1F = `(addr + read_lag) mod ring`, write `cmd`, one dummy read, then `count` reads into `dst`; tight loop like `fpga_dump_ring` in Donwulff's tree (0.30 us/byte [M]) |
| `fpga_force()` | 0x1A = 0 (P-8.4 FORCE) |
| `fpga_set_generator_raw(phase_inc, threshold, enable)` | 0x52, 0x51, 0x50 in that order |

Keep `fpga_read_sample_data()` / `fpga_read_adc_data()` unchanged for the UI and READ_TRACE. Fix the
pointer translation in `scope_acquire_trace_data()` to `(T - pretrigger_words) mod ring` for all variants
(P-7.5) in the same change, since READ_TRACE reuses it; this shifts the UI trace by one word in the wrapped
case and must be checked on screen.

Write order constraints: no FPGA command from `pc_poll(BUSY)`; no FPGA command while the UI is between a
`fpga_write_cmd(0x20..0x23)` and the end of its read loop (guaranteed by only calling `pc_poll(MAIN)` at the
top of the main loop).

## 8. Memory budget

All static in `.bss` (DDR, 32 MB total; the current image and buffers use a few MB [U exact]).

| Buffer | Size | Note |
|---|---|---|
| `pc_rx` ring | 2 KiB | P-3.2 |
| `pc_tx` staging | 65 KiB + 20 B | largest response |
| control response buffer | 64 B | responses during a deferred op |
| duplicate cache | 256 B | P-4.3 |
| CRC-32 table | 1 KiB | `const`, in `.rodata` |
| `usb_tx[32768]` ring | -32 KiB | removed |
| ACQUIRE_AVG accumulators (phase 3) | 2 x 16384 x 4 = 128 KiB | u32 sums |
| shadow settings | ~4 KiB | `sizeof(SCOPESETTINGS) + sizeof(FPGASETTINGS)` |

## 9. USB personality: CDC or MSC, not composite (for now) [D]

Keep the 1.00o5 model: one personality at a time, chosen by `USB_CH340`.

- Default `USB_CH340 = 1` (CDC) in our firmware, so a connected PC gets the control port at boot.
- The UI's USB menu switches to MSC for SD file access and back to CDC on exit [V `menu.c:284-314`]. While
  in MSC the protocol is unavailable (the scope is in a modal screen anyway).
- Refuse the MSC screen while REMOTE.

Why not composite CDC + MSC now: it needs an Interface Association Descriptor, a third bulk endpoint pair
and FIFO RAM layout changes (EP1 64 B at 256, EP2 512 B DPB at 0 and 512, units of 8 B
[V `S100 usb_interface.c:1120-1185`]), a second class driver in the same ISR, and it exposes the FAT volume
to the PC while the scope writes it, which corrupts the filesystem unless the scope stops writing
(`repo-pecostm32.md` section 5 item 1). It brings no benefit for MCP control. Revisit with TinyUSB if a
use case appears (e.g. the server pulling saved waveforms).

Full-speed fallback (phase 3): at bus reset, read the HS flag (`usb_interface.c:940-960` already detects
it) and use 64-byte bulk packets and a full-speed config descriptor when the host is full speed.
Throughput would drop to ~1 MB/s [U], still ~30 full captures/s.

## 10. VID/PID and descriptor identity

Today: 0x0483:0x5740 (STMicroelectronics "Virtual COM Port") for CDC and 0x0483:0x5720 for MSC
[V `S100 cdc_class.c:57-58`, `repo-pecostm32.md` 2.8].

Problems:

- Those IDs belong to ST. Using them in a distributed firmware is not permitted by the USB-IF vendor-ID
  rules, and they collide with real STM32 VCP devices on the same PC: host tools that pick a port by VID:PID
  pick the wrong device.
- If ST's own VCP driver (STSW-STM32102) is installed, Windows binds it to 0483:5740 by hardware ID instead
  of in-box usbser.sys. That usually works but behaves differently [U].
- Windows caches descriptors per VID/PID/bcdDevice; changing descriptors under the same IDs can leave stale
  driver bindings. Any descriptor change must bump bcdDevice.

Recommendation [D]:

1. **Development now:** pid.codes test PID, VID 0x1209, PID from its test range (0x1209:0x0001..0x0010
   are listed as test PIDs [U: re-check https://pid.codes before use]). Free, no paperwork, never to be
   shipped.
2. **Before anyone else flashes it:** apply to pid.codes for a dedicated PID (free for open-source projects;
   requires the repository to carry an OSI licence, which this repo does not yet [U]). Use one PID for CDC
   and a second for MSC so Windows keeps separate bindings.
3. Product string `FNIRSI 1013D`, manufacturer `fnirsi-1013d-ai` (not "FNIRSI", to avoid implying an
   official product), interface strings `FNIRSI 1013D control` / `FNIRSI 1013D data`.
4. iSerial: unique per unit so Windows assigns a stable COM number and two scopes can coexist. Source: the
   F1C100s SID/eFuse chip ID if readable [U: register map not checked]; fallback: a random 32-bit value
   created once and stored in a spare byte range of the settings sector (sector 709 has unused space
   [U]), printed as 8 hex digits.
5. Device class stays 0x02 with no IAD (single function), so Windows 10+ binds usbser.sys through the
   compatible ID `USB\Class_02&SubClass_02` without an INF [V Microsoft behaviour since Windows 10; not
   tested with this device].
6. Linux: ship a udev rule for the chosen VID:PID with `ENV{ID_MM_DEVICE_IGNORE}="1"` so ModemManager does
   not send AT probes, and `MODE="0660", GROUP="dialout"`.

## 11. Testing plan

### 11.1 Host-side protocol tests (no hardware)

- `mcp/tests/test_protocol.py`: frame encode/decode round trips, CRC check values (P-4.1), the example
  frames of P-13 byte for byte, fuzzed byte streams (random splits into packets, junk between frames,
  truncated frames, bad header CRC, bad payload CRC) against the decoder.
- `firmware/scope/tests/` (host gcc in WSL, not cross): build `pc_proto.c` and `pc_cmds.c` against a mock
  `pc_hal.c` that models the FPGA: 8192-word ring with four cores, write pointer, pretrigger/total registers,
  a trigger comparator on core A, 0x05/0x0A/0x18/0x14 behaviour from zaklad.v, configurable `read_lag`.
  Feed the same vectors as the Python tests through the parser; run handler-level tests (validation rules,
  REMOTE gating, duplicate cache, deferred WAIT completion, STOP abort, watchdog, DTR drop).
- Virtual scope: wrap the host build in a small program that speaks the protocol on a pty (Linux) or a TCP
  socket; the Python client gets a `socket://` transport. The MCP server's tool tests run against it in CI.
  This replaces any need for an emulator for protocol work.

### 11.2 pcprogrammer's emulators

`Scope_emulator` / `1014D_Emulator` have no USB model (USB-OTG is a NULL stub) and are X11-only
[V `hack-repo-software.md` 4.1]. They cannot exercise the protocol. Use them only to capture FPGA command
traces of the stock firmware if a question about stock FPGA behaviour comes up (e.g. test T10).

### 11.3 On hardware (maintainer present; agents prepare commands)

Load path: FEL, so nothing is written to SD or flash during iteration: `xfel` / `sunxi-fel` write the image
to DRAM and execute it (procedure in `docs/flashing.md` when written). The firmware must wait ~200 ms before
enabling USB so FEL's USB session is gone [V pcprogrammer `f1c100s_ch340.c:62-66`, 1.00o5 waits 200 ms too].

Order, each step gated on the previous:

1. Enumeration on Windows and Linux; `#VER` in a terminal; PING 10,000 x random 0..492 bytes.
2. TEST_PATTERN suite (protocol tests T1, T2): boundary lengths, 64 KiB x 200, measure MB/s; repeat with the
   UI loop running (LOCAL) to see the main-loop latency.
3. REMOTE enter/exit, all exit reasons including cable pull and tap; check the UI returns with the user's
   settings; power-off while REMOTE, then boot and confirm settings were the user's.
4. Configuration read-back for every range, coupling and timebase; listen for relay clicks; check the UI
   after release.
5. Raw reads: protocol tests T3 (read_lag), T6, T7 via DEBUG_FPGA; then READ_RAW repeated reads byte-identical
   (acqprobe-style).
6. Loopback with the generator (Atlan4 bitstream only): calibration pin to CH1 via the probe; tests T4, T5,
   T8 (with an external DC reference), then the MCP `self_test_loopback`.
7. Soak: 1,000 full captures with READ_RAW, then 1 hour of `averaged_capture(n=64)` loops; zero CRC errors,
   zero timeouts, no UI corruption.

On the stock bitstream (0x1432) steps 1-5 apply; generator tests are skipped (E_UNSUPPORTED is the expected
result).

## 12. Phases

| Phase | Content | Exit criterion |
|---|---|---|
| 0 | descriptors + VID/PID, RX ring with back-pressure, TX pump with ZLP, remove ISR parser and SOF, ASCII `#VER`, PING, GET_INFO, TEST_PATTERN | 11.3 steps 1-2 |
| 1 | remote state machine, GET/SET channel/timebase/trigger, ARM/STOP/WAIT, READ_RAW, READ_TRACE, GET_STATUS, GET_CAL, GET_BATTERY, bounded FPGA waits, pointer-math fix, buffer-size fix | 11.3 steps 3-5; Python client + MCP tools `capture`, `averaged_capture` (host-side) usable |
| 2 | SET_GEN/GET_GEN, FORCE (after T7), READ_LIVE, READ_SCREEN, DEBUG_FPGA, ENTER_FEL | step 6 |
| 3 | ACQUIRE_AVG, assembly readout loop, full-speed fallback, unique serial, pid.codes PID | step 7; throughput numbers recorded in `docs/hardware-status.md` |

Each phase is one or more small commits on `main` touching `firmware/scope/` and `mcp/` only.

## 13. Risks and unverified facts

| Risk | Impact | Mitigation |
|---|---|---|
| Windows usbser.sys with the current ST VID/PID, or a stale cached binding after descriptor changes | port does not appear or opens with the wrong driver | new VID/PID (section 10), bump bcdDevice on every descriptor change, document "uninstall device" in Device Manager |
| ZLP handling on usbser.sys / cdc_acm unverified | hangs on responses of exactly n x 512 bytes | always send ZLP; test T1 |
| HS-only device on a full-speed port (some hubs, VMs, USB-C docks) | enumeration with illegal 512-byte bulk packets | phase 3 FS fallback; document "use a USB 2.0 HS port" |
| PIO FIFO throughput unknown | fewer captures per second | TEST_PATTERN measures; 32-bit FIFO writes already used by `usb_write_to_fifo` [V `S100 usb_interface.c:263-295`] |
| Main-loop pass time in LOCAL unknown | slow LOCAL responses | only REMOTE needs speed; measure in 11.3 step 2 |
| Modal UI loops not polling USB | host timeouts while the user is in a menu | BUSY polls at the known sites; host treats `E_BUSY`/timeouts in LOCAL as "UI busy" |
| Power-off ISR saves settings from interrupt context | host settings persisted, or SD race | snapshot restore in the handler (6.4); SD race unchanged from 1.00o5 |
| `read_lag`, interleave order, trigger alignment, FORCE, voltage formula all [U] | wrong sample timing or volts | capability bits marked unverified until tests T3-T8 pass; MCP surfaces this in caveats |
| EP4 bitstream has no ring wrap | corrupted captures on EP4 units | not the maintainer's variant; cap max total at 12286 and flag in `scope_profile` |
| Host settings for long/roll mode | not supported | `E_UNSUPPORTED` for timebase indices 0..10 |
| Linux ModemManager probing the port | AT bytes on the wire, port busy | parser ignores them; udev rule |
| pid.codes eligibility needs a licence | cannot get a PID | choose a licence for the repo (maintainer decision) |
| Atlan4 code quality (giant `menu.c`/`statemachine.c`, duplicated blocks) | merge pain with the parallel bootstrap | keep protocol code in new files; touch existing files only at the hooks listed in section 4 |
