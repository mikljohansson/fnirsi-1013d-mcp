# FNIRSI 1013D PC control protocol, version 1.0 (draft)

Status: design draft, 2026-10-18. Nothing here is implemented yet. Firmware plan:
`docs/plans/firmware-usb-protocol.md`. MCP tool surface built on top: `docs/mcp-tools.md`.

Markers used throughout:

- **[V]** verified in source (file and line given; `S100` = Atlan4 v1.00o5 tree
  `research/atlan4-extracted/_dso1xxx/v1.00o5/fnirsi_1013d_scope/`, `S27T` = Atlan4 v0.27t tree, `zaklad.v` =
  Atlan4 AL3 Verilog as digested in `research/notes/fpga-verilog-analysis.md`).
- **[M]** measured by someone else (source note given), not yet on the owner's 1013D.
- **[U]** unverified: depends on a fact nobody has checked on a 1013D. Every [U] item is collected in
  section 12 with the bench test that settles it.
- **[D]** a design choice made here; rationale given.

## 1. Scope and goals

The protocol lets a host (the Python MCP server in `mcp/`) own the scope's acquisition hardware over the
single USB port: configure the front end, timebase and trigger, arm captures, read the raw FPGA sample ring
or the firmware's processed trace, drive the square-wave generator, and read calibration, battery and the
screen. Primary use: metal-detector experiments (kHz signals, ~100 us decays, many averaged captures,
sync to the detector TX edge on CH2).

Non-goals for v1: roll/long-timebase mode, file access to the SD card (that stays USB mass storage, see the
firmware plan), firmware update over the protocol, writing calibration.

## 2. Why binary instead of the v0.27t hex-ASCII protocol [D]

The only previous PC protocol (Atlan4 v0.27m..v0.27t, `S27T usb_interface.c:1461-1680`, digest in
`research/notes/repo-atlan4.md` section 2.2) is a line protocol imitating the OEM FNIRSI PC app:
`#VER`, `#INFO`, `#WAV2,3` returning a 320-byte `SysParam` blob and 4801 samples per channel as ASCII hex.
It is not a usable base:

| Problem in v0.27t | Consequence | Binary v1 answer |
|---|---|---|
| Samples sent as 2 hex chars per byte, padded to the OEM app's 4801 points | ~19.8 kB per 3000-sample frame; a 16k x 2 capture would be 65 kB | raw bytes: 32 kB for 16k x 2, no padding |
| Every byte goes through `send_hex_byte` and the per-byte ring `CDCSend` (`S100 cdc_class.c:216-224`) | CPU cost per byte on a 600 MHz ARM926 with D-cache effectively off | response staged once, streamed in 512-byte packets |
| No length, no checksum, no sequence number | a lost or merged USB packet is undetectable; replies cannot be matched to requests | length + header CRC-16 + payload CRC-32 + 16-bit sequence |
| `#RUN`, `#STOP`, `#SET` only answer OK; `SysParam` is a static snapshot (`S27T usb_interface.c:1640-1660`) | nothing reaches the hardware | every command maps to concrete FPGA writes with read-back |
| Text parsing with `strncmp` on a 128-byte line, RX copied into a 64-byte `bufferCDC` although HS OUT packets are 512 bytes (`S27T usb_interface.c:28`, `:1411`) | buffer overflow on any host write over 64 bytes | frame parser over a 2 KiB ring with USB-level back-pressure |

Binary fields are fixed little-endian structs: trivial in C (`memcpy` into packed structs, the ARM926 is
little-endian) and in Python (`struct.unpack('<...')`). A one-line ASCII probe (`#VER`) is kept for terminal
use and discovery because it costs about 40 lines of firmware (section 10).

## 3. Transport

### 3.1 USB device

| Item | Value | Note |
|---|---|---|
| Class | CDC ACM, one communication interface (EP 0x81 interrupt IN, 8 bytes) + one data interface (EP 0x82 bulk IN, EP 0x02 bulk OUT) | same descriptor set as `S100 cdc_class.c:62-154` [V] |
| Speed | high speed, bulk wMaxPacketSize 512 | full-speed fallback (64) is a firmware-plan item; until then the scope must sit on a HS port or HS hub [V: no FS path, `S100 usb_interface.c:940-960`] |
| VID:PID | to be changed from ST's 0x0483:0x5740; see the firmware plan section 10 | host discovery must not rely on the VID:PID alone; it also checks the product string `FNIRSI 1013D` and `#VER` |
| Line coding | accepted and ignored (any baud rate works) | `SET_LINE_CODING` stored only [V `S27T usb_interface.c:1771`] |
| DTR | the host must assert DTR. Firmware treats DTR 1->0 as "host closed the port" | `usb_connect` is set from DTR/RTS [V `S27T usb_interface.c:1835`]; pyserial asserts DTR on open by default |

### 3.2 Byte stream, packets and flow control [D]

The protocol is a byte stream; frames may span USB packets and one packet may hold several frames.

Host to device:

- A request frame is at most **512 bytes** (`max_request_frame` in GET_INFO). So the largest request payload
  is 512 - 16 - 4 = 492 bytes. This keeps the firmware RX path to one 2 KiB ring.
- **Window: one outstanding request**, plus at most one *control* request (GET_STATUS, STOP,
  SET_REMOTE release, PING) while a deferred request (WAIT, ACQUIRE_AVG) is pending. The host waits for each
  response before sending the next normal request.
- Back-pressure is done by USB itself: when the RX ring has less than 512 bytes free, the firmware leaves the
  OUT packet in the FIFO (RxPktRdy set) and masks the EP2 RX interrupt; the controller then NAKs further OUT
  tokens and the host write blocks until the main loop drains the ring. No byte is ever dropped by the
  device. Host writes must use a write timeout (2 s) so a hung scope is detected.

Device to host:

- Responses are staged whole in a TX staging buffer and sent as 512-byte packets.
- If the total frame length is a multiple of 512, the device sends a zero-length packet after it, so a host
  read with a packet-level URB completes [D; usbser.sys behaviour without ZLP is [U], see 12].
- If the host stops reading, the device keeps the frame pending (TxPktRdy stays set). Further requests queue
  up in the RX ring and then back-pressure. After DTR drops, a pending TX frame is discarded.

Resynchronisation:

- The parser looks for the magic bytes `A5 13`. A frame whose header CRC fails is discarded silently and
  the parser restarts at the byte after the bad magic.
- A partially received frame is discarded if no byte arrives for **100 ms** (timer0 has a 1 ms tick
  [V `S100 timer.c`, `timer0_delay`]).
- Host recovery procedure: stop sending, wait 150 ms, flush input, send PING with a fresh sequence number.

## 4. Frame format

All multi-byte fields are **little-endian** [D]. (The FPGA bus is big-endian, high byte first
[V `S100 fpga_control.c:89-171`]; the firmware converts. Nothing big-endian crosses USB.)

### 4.1 Header (16 bytes)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | u8 | magic0 | 0xA5 |
| 1 | u8 | magic1 | 0x13 |
| 2 | u8 | proto_major | 1 |
| 3 | u8 | flags | bit0 RESPONSE (set by device), bit1 EVENT (reserved, never set in v1), bits 2-7 zero |
| 4 | u16 | cmd | command id (section 8). A response carries the request's cmd |
| 6 | u16 | seq | chosen by the host, echoed in the response. Host increments it per request, wraps at 0xFFFF |
| 8 | u16 | status | requests: 0. Responses: error code (section 9), 0 = OK |
| 10 | u32 | len | payload length in bytes (0 allowed) |
| 14 | u16 | hdr_crc | CRC-16/CCITT-FALSE over bytes 0..13 |

Then `len` payload bytes, then, **only if len > 0**, a u32 `payload_crc` = CRC-32/ISO-HDLC over the payload
bytes. Frame size = 16 + len + (len ? 4 : 0).

CRC definitions (host side in Python standard library):

| CRC | Poly | Init | Reflect | XorOut | Check value of "123456789" | Python |
|---|---|---|---|---|---|---|
| hdr_crc CRC-16/CCITT-FALSE | 0x1021 | 0xFFFF | no | 0x0000 | 0x29B1 | `binascii.crc_hqx(b, 0xFFFF)` |
| payload_crc CRC-32/ISO-HDLC | 0x04C11DB7 | 0xFFFFFFFF | yes | 0xFFFFFFFF | 0xCBF43926 | `zlib.crc32(b)` |

Rationale [D]: a separate header CRC lets the parser reject a corrupt `len` before waiting for a bogus
amount of data. CRC-32 over a 32 KiB response costs an estimated 1-2 ms on the ARM926 with table lookups
from uncached DDR [U: D-cache is reported to be effectively off because the MMU is never enabled,
`research/notes/repo-donwulff.md` section 5]; acceptable next to ~10 ms of FPGA readout.

### 4.2 Error responses

A response with status != 0 has an optional payload: u16 `detail` (command-specific, e.g. the index of the
offending field) followed by up to 64 bytes of ASCII text without terminator, e.g. `range 7 > 6`. Hosts
show the text in error messages.

If the header is valid but the payload CRC fails, the device answers `E_BAD_CRC` with the request's cmd and
seq. If the header CRC fails, there is no answer (the host times out and retries).

### 4.3 Duplicate requests [D]

The firmware keeps the last response if it is at most 256 bytes. A request with the same `seq` and `cmd` as
the last completed one is answered from that cache without executing again. This makes retries of ARM,
FORCE and SET_* safe after a lost response. Large responses (READ_*) are not cached; those commands are
idempotent and simply run again.

## 5. Session, versioning and capabilities

1. Host opens the port (DTR on), optionally sends `#VER\n` and checks the reply prefix (section 10).
2. Host sends GET_INFO. If `proto_major` differs from the host's, the host stops. A request with a
   different `proto_major` in its header gets `E_VERSION`.
3. Host reads `caps` and the memory geometry and never sends a command whose capability bit is 0; the
   firmware answers such commands with `E_UNSUPPORTED` anyway (fail closed).
4. Host sends SET_REMOTE(acquire) before any command that changes hardware state.

Compatibility rules [D]:

- `proto_minor` increases for backward-compatible additions: new commands, new capability bits, and new
  fields **appended** to payloads. Parsers must accept payloads longer than they expect (ignore the tail)
  and treat missing trailing fields of a shorter payload as zero.
- Field meanings never change within a major version.

### 5.1 FPGA variants the firmware recognises

Version word from FPGA command 0x06 (two bytes, high first) [V `S100 fpga_control.c:196-209`,
`zaklad.v` :847-855]:

| Word | `fpga_variant` | Ring per ADC core (words) | Max total samples per channel | Programmable pre-trigger / total | Generator (0x50-0x52) |
|---|---|---|---|---|---|
| 0x1432 | 1 stock FNIRSI | 4096 [M 1014D, acqprobe] | 3000 (fixed geometry) | no, fixed 750/1500 words | no, fixed 1 kHz |
| 0x1532 | 2 Atlan4 AL3 | 8192 | 16382 | yes | yes |
| 0x1632 | 3 Atlan4 EF2 | 6144 (wraps at 0x17FF) | 12286 | yes | yes |
| 0x1732 | 4 Atlan4 EP4 | 6144 but no wrap logic [V zaklad.v EP4 diff] | 12286, captures may corrupt above 6143 [U] | yes | yes |
| other | 0 unknown | treated as stock, `caps` = 0 except PING/INFO/STATUS | | | |

1.00o5 does not recognise 0x1732 (it sets `fw_FPGA = 0`) [V `S100 fpga_control.c:204-206`]; v1 firmware adds
it. The owner's AL3 unit runs whichever bitstream is in its 25Q80; the host must not assume 0x1532.

## 6. Control model

### 6.1 LOCAL / REMOTE ownership [D]

> **Protocol 1.3 (firmware 0.2.0) replaces 6.1 and 6.2 with shared control** (section 14, rows 30-38;
> `docs/plans/collaborative-mode.md`). There is no LOCAL/REMOTE: the UI loop always runs, host SET_* change the
> live UI settings, ARM is a host single capture, and SET_REMOTE / `#LOCAL` are accepted as no-ops. Hosts detect
> 1.3 by the SHARED_MODE capability bit and use the 1.2 model below without it.

The scope firmware is single-threaded: one main loop runs battery check, acquisition, drawing and touch
[V `S100 fnirsi_1013d_scope.c:250-384`]. The FPGA has one bus and no arbitration. So exactly one owner drives
acquisition at a time:

| State | Who arms the FPGA | Screen | Touch | Commands accepted |
|---|---|---|---|---|
| LOCAL | the UI loop (`scope_acquire_trace_data`) | normal UI | normal | read-only set (column "LOCAL" in section 8) |
| LOCAL_BUSY (sub-state) | UI is inside a modal screen, calibration, auto-setup or USB-MSC screen | UI | UI | PING, GET_INFO, GET_STATUS, GET_BATTERY only; others `E_BUSY` |
| REMOTE | the host, through ARM | frozen except a status banner (optional trace display) | ignored except a tap on the banner, which releases | everything |

Transitions:

```
            SET_REMOTE(acquire)  [UI not modal]
   LOCAL  ------------------------------------------->  REMOTE
     ^                                                    |
     |   SET_REMOTE(release)        exit_reason 1         |
     |   tap on banner              exit_reason 2         |
     +---watchdog expired           exit_reason 3   <-----+
         DTR 1->0 (unless IGNORE_DTR) exit_reason 4
         USB reset / suspend / disconnect  exit_reason 5
         ASCII "#LOCAL"             exit_reason 1
```

On entering REMOTE the firmware:

1. Waits for the current main-loop iteration to finish (the request is executed from the main loop, so this
   is automatic) and closes any open non-modal menu.
2. Snapshots `scopesettings` and `fpgasettings` (UI state) into a shadow copy.
3. Stops UI acquisition, sets acquisition state IDLE, draws the banner "REMOTE - tap here to release".
4. Leaves the FPGA configuration as the UI had it. GET_* return those values; the host sets what it needs.

On leaving REMOTE the firmware:

1. Completes any deferred request with `E_ABORTED`.
2. Restores the snapshot (unless the host set KEEP_SETTINGS), pushes every setting to the FPGA with the same
   sequence `main()` uses at boot (channel enable, V/div, coupling, sample rate, timebase, trigger channel,
   edge, level, mode, offsets, generator) [V `S100 fnirsi_1013d_scope.c:131-169`].
3. Redraws the main screen and resumes the UI loop.

Settings changed by the host are never written to the SD settings sector unless KEEP_SETTINGS was used;
the power-off handler saves configuration from an interrupt [V `S100 power_and_battery.c:94-110`], so the
firmware restores the snapshot before that save when REMOTE is active (firmware plan section 6.4).

Watchdog: SET_REMOTE carries `watchdog_s`. If no valid frame arrives for that many seconds, the scope
returns to LOCAL. 0 disables it. The MCP server uses 15 s and sends GET_STATUS while idle.

### 6.2 Acquisition state machine (REMOTE)

```
 IDLE --ARM--> ARMED --FPGA 0x18 = 1--> TRIGGERED --FPGA 0x0A = 1--> DONE
   ^             |  \                                                 |
   |             |   +--(auto mode timeout: 0x0A = 1, 0x18 = 0)--> DONE (flags.auto_timeout)
   |             +--FORCE--> (writes auto mode) --> DONE
   +----STOP (from any state) -- ABORTED ---- ARM (from DONE/ABORTED/IDLE) re-arms
```

- ARM performs the 1.00o5 conversion start sequence with bounded waits (section 8.4).
- The firmware polls 0x0A (done) and 0x18 (triggered) from the main loop while ARMED or TRIGGERED, without
  the touch-panel scan that `fpga_done_conversion` does in the UI path [V `S100 fpga_control.c:663-690`].
- `capture_id` increments when a capture reaches DONE (remote or UI). The sample ring is static once 0x0A is
  set and can be re-read any number of times with identical results [M acqprobe on a 1014D,
  `repo-donwulff.md` capture geometry]. The next ARM overwrites it.
- In REMOTE there is no automatic re-arm. Trigger mode "single" behaves like "normal".

## 7. Raw sample semantics

This section defines what READ_RAW returns. Read it before writing any analysis code.

### 7.1 Sampling hardware

- Each channel has two ADC cores, **A** and **B**, of one AD9288 on the same input, clocked in antiphase. The
  FPGA toggles core B's encode clock on every sample-enable pulse and drives core A with its complement
  [V zaklad.v :361-372]. Sample-enable pulses come every N+1 cycles of 200 MHz, N = sample-rate divider
  written with command 0x0D [V zaklad.v :384-394].
- Interleaved sample interval per channel: **dt = (N + 1) / 200 MHz**. Each core samples every 2 dt.
  N = 0 gives 200 MS/s per channel, both channels at once.
- Values are **8-bit unsigned codes 0..255**. With the calibrated offset PWM, 0 V sits near code 128
  (`zero_code_q8` in CHANNEL_CONFIG gives the firmware's value). 0 and 255 mean the ADC clipped; the
  firmware's overload flag uses exactly these codes [V `S100 fpga_control.c:1053-1054`].
- Noise is about 2 LSB; the two cores of a channel differ by an offset of roughly 1.4-5.5 codes
  [M 1014D acqprobe], corrected by the calibration values `adc_a_comp` / `adc_b_comp` (section 8.10).

### 7.2 Ring memory layout

- One memory word per write clock (every 2 dt) holds four bytes: bits 7:0 CH1 core A, 15:8 CH1 core B,
  23:16 CH2 core A, 31:24 CH2 core B [V zaklad.v / `al_ip/sample_memory.v`].
- Ring size `ring_words` per core (GET_INFO). Addresses wrap modulo `ring_words` (stock and AL3 power of
  two; EF2 wraps at 6144).
- **Both channels are always sampled**; "channel enable" only controls readout and display. FPGA commands
  0x02/0x03 that 1.00o5 sends as "enable" are not decoded by the Atlan4 FPGA [V: absent from the zaklad.v
  command table; `S100 scope_functions.c:6421` comments "not use ?"].
- Readback is per core: command 0x20 = CH1 A, 0x21 = CH1 B, 0x22 = CH2 A, 0x23 = CH2 B, one byte per bus
  strobe, read pointer set by command 0x1F and auto-incremented by each read strobe [V zaklad.v :607-619].

### 7.3 Interleave order (convention) [U]

READ_RAW layout 1 and READ_TRACE build the interleaved channel stream exactly as 1.00o5 does
[V `S100 fpga_control.c:747-833`]:

```
trace[2k]     = core B, word k      (command 0x21 / 0x23, "adc2" in firmware naming)
trace[2k + 1] = core A, word k      (command 0x20 / 0x22, "adc1")
```

So the convention is "B samples before A within a word". The true time order of A and B within a word has
not been measured. If the bench test (section 12, test T4) shows A first, the firmware flips a constant and
reports it; layout 0 (per-core blocks) is unaffected.

### 7.4 Stale first byte and read lag

- The first byte read after a 0x1F pointer write is a stale pipeline byte, off by about +-1 code and
  reproducible [M 1014D stock FPGA, `repo-donwulff.md`]. 1.00o5 does not discard it: `tracebuffer[1]` (CH A)
  and `tracebuffer[0]` (CH B) hold stale bytes in every UI capture [V `S100 fpga_control.c:758`, `:830`].
- The Verilog's BRAM output is unregistered and the pointer increments on the read strobe, so the byte
  returned on a strobe may belong to the previous address (one-word lag) [U, inference in
  `fpga-verilog-analysis.md` section 5].
- **Protocol rule [D]:** READ_RAW returns `count` bytes per core that correspond to ring addresses
  `start_word .. start_word + count - 1` exactly. The firmware performs one dummy read after each 0x1F write
  and applies a per-variant `read_lag` (0 or 1) to the pointer it writes. `read_lag` is reported in GET_INFO
  and is determined once per FPGA variant by test T3 (section 12). Until T3 has been run, firmware ships
  `read_lag = 0` and sets status flag bit `READ_LAG_UNVERIFIED` in GET_INFO `caps`.
- READ_TRACE keeps the 1.00o5 behaviour (no dummy read) unless test T3 shows it matters, so that its output
  matches what the UI displays.

### 7.5 Trigger detection and capture geometry

- The trigger compares consecutive **core A** samples of the selected channel (CH1 A or CH2 A only) against
  an 8-bit level: rising = previous < level and current >= level; falling = previous >= level and current <
  level [V zaklad.v :444-490, Atlan4 FPGA]. Core B is never looked at, so the trigger instant is quantised to
  one word = **2 dt**, and an edge that is only visible on core B is missed. The stock FPGA's trigger core is
  [U] (probably the same; pecostm32's clean-room model should be checked).
- The level is compared with **uncompensated** core-A codes. To trigger at a voltage V, the host computes the
  processed code and subtracts `adc_a_comp` (section 8.10).
- Trigger checking starts only after `pretrigger_words` words have been written since ARM (the half-way
  flag) [V zaklad.v :509-516].
- `trigger_addr` (command 0x14, 13 bits [V `S100 fpga_control.c:694-707`]) is the ring word address at the
  trigger. The capture window starts at
  `capture_start_word = (trigger_addr - pretrigger_words) mod ring_words` and is `total_words` long.
  - 1.00o5 computes the wrapped case as `T + 7441` (AL3) or `T + 5393` (EF2) or `T + 3345` (stock), which
    equals `T - 751 mod ring` and is off by one against the unwrapped branch `T - P`, and only correct when
    `P = 750` [V `S100 scope_functions.c:781-820`]. v1 firmware uses the modular formula. The exact
    alignment of `trigger_addr` with the crossing sample (+-1 word) is [U], test T5.
- Auto mode: if no trigger arrives within `auto_timeout` write clocks, the capture stops and the FPGA forces
  `trigger_addr` to `pretrigger_words` [V zaklad.v :520-542]. Data are then not synchronised to anything.
  GET_STATUS flags this as `auto_timeout` when 0x18 reads 0 at DONE [U: depends on 0x18 staying 0 on
  timeout].
- Normal mode has no timeout: the ring keeps wrapping until a trigger arrives.
- Stock FPGA geometry: fixed 750 pre-trigger and 750 post-trigger words; the readout window is
  `T - 750 .. T + 749`. Fresh data extend roughly 910 words before and 1700-2500 words after the trigger;
  the rest of the 4096-word ring holds stale data from earlier captures [M 1014D acqprobe]. READ_RAW outside
  the window is allowed on the stock FPGA but flagged by `caps` bit `PROG_TRIGGER_POS = 0`.

### 7.6 Processed trace (READ_TRACE)

READ_TRACE returns the firmware's `tracebuffer` as the UI computes it [V `S100 fpga_control.c:1004-1341`]:

1. Interleave per 7.3, `samplecount = total_samples` bytes per channel starting at `capture_start_word`.
2. Per core: `code + adc{1,2}compensation`, clamped to 0..255.
3. Near the rails the two cores are forced equal ("checkfirstadc" logic) to hide the compensation step.
4. Optional inversion `255 - code` when the channel's invert flag is set.

It does **not** add the DC-shift correction that the UI applies only for measurements
(`sample += dcoffset * 100 / dc_shift_value`, `S100 fpga_control.c:1308`); `zero_code_q8` in the channel
metadata already accounts for it [U formula, section 8.5].

## 8. Command reference

### 8.1 Index

"LOCAL" = accepted while the UI owns the scope (1.2; in 1.3 every command is accepted, section 14 row 30). "Cap" =
required GET_INFO capability bit.

| Cmd | Name | LOCAL | Cap | Deferred | Summary |
|---|---|---|---|---|---|
| 0x0001 | PING | yes | - | no | echo payload |
| 0x0002 | GET_INFO | yes | - | no | identity, geometry, capabilities |
| 0x0003 | GET_STATUS | yes | - | no | ownership, acquisition state, capture id |
| 0x0004 | SET_REMOTE | yes | - | no | acquire / release remote ownership |
| 0x0005 | GET_BATTERY | yes | - | no | battery level, charging |
| 0x0006 | TEST_PATTERN | yes | - | no | device-generated bytes for link tests |
| 0x0007 | GET_LINK_STATS | yes | - | no | link counters (1.2, section 14 row 28) |
| 0x0008 | GET_BAD_FRAME | yes | - | no | last request with a bad payload CRC (1.2, row 29) |
| 0x0009 | GET_EVENT_LOG | yes | - | no | last 64 USB / link events with timestamps (1.5, row 42) |
| 0x0100 | GET_CHANNEL | yes | - | no | channel config |
| 0x0101 | SET_CHANNEL | no | - | no | enable, range, coupling, offset, invert |
| 0x0102 | GET_TIMEBASE | yes | - | no | divider, rate |
| 0x0103 | SET_TIMEBASE | no | - | no | UI index or raw divider |
| 0x0104 | GET_TRIGGER | yes | - | no | trigger and memory geometry |
| 0x0105 | SET_TRIGGER | no | - | no | source, edge, level, mode, pre-trigger, total, hold-off |
| 0x0200 | ARM | no | - | no | start a capture |
| 0x0201 | FORCE | no | FORCE_TRIGGER | no | end a waiting capture now |
| 0x0202 | STOP | no | - | no | abort capture and pending requests |
| 0x0203 | WAIT | no | - | yes | wait until DONE or timeout |
| 0x0204 | READ_RAW | no | - | no | raw ring bytes per core |
| 0x0205 | READ_TRACE | yes | - | no | processed trace as the UI shows it |
| 0x0206 | READ_LIVE | no | LIVE_READ | no | live ADC bus bytes (no memory) |
| 0x0207 | ACQUIRE_AVG | no | DEVICE_AVERAGE | yes | n captures averaged on the device |
| 0x0208 | SET_ACQ_STATE | (1.3) | SHARED_MODE | no | UI RUN / STOP (1.3, section 14 row 34) |
| 0x0209 | GET_CAPTURE_INFO | (1.3) | SHARED_MODE | no | settings of the latest finished capture (1.3, row 37) |
| 0x020A | READ_SPECTRUM | (1.7) | - | no | the spectrum the scope displays (section 14 row 45) |
| 0x020C | GET_DECIM | (1.9) | DECIM | no | decimation filter setting and state (section 14 row 47) |
| 0x020D | SET_DECIM | (1.9) | DECIM | no | decimation filter setting, the UI's acquisition Filter (row 47) |
| 0x0500 | SD_INFO | (1.3) | SD_SECTORS | no | card, partition, write window, eGON headers (section 14 row 40) |
| 0x0501 | SD_READ_SECTORS | (1.3) | SD_SECTORS | no | up to 128 sectors |
| 0x0502 | SD_STAGE | (1.3) | SD_SECTORS | no | bytes into the 64 KiB write staging buffer |
| 0x0503 | SD_WRITE_SECTORS | (1.3) | SD_SECTORS | no | staged sectors to the card, verified |
| 0x0300 | GET_GEN | yes | - | no | generator state |
| 0x0301 | SET_GEN | no | GENERATOR_DDS | no | enable, frequency, duty |
| 0x0400 | GET_CAL | yes | - | no | calibration constants |
| 0x0401 | READ_SCREEN | yes | SCREENSHOT | no | framebuffer rows |
| 0x0F00 | DEBUG_FPGA | no | DEBUG_RAW | no | raw FPGA bus operations |
| 0x0F01 | ENTER_FEL | no | ENTER_FEL | no | reboot into the boot ROM's FEL mode |
| 0x0F02 | FFT_BENCH | (1.6) | - | no | time the on-scope FFT (section 14 row 44) |

Unknown cmd: `E_UNKNOWN_CMD`. LOCAL-only restriction violated: `E_NOT_REMOTE`. UI modal: `E_BUSY`.

### 8.2 System commands

**PING (0x0001).** Request: 0..492 arbitrary bytes. Response: the same bytes.

**GET_INFO (0x0002).** Request: empty. Response (76 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | proto_major | 1 |
| 1 | u8 | proto_minor | 0 |
| 2 | u16 | max_request_frame | 512 |
| 4 | u32 | max_response_payload | 66560 [D]: 64 KiB of samples plus headers |
| 8 | u16 | fpga_version | raw word from 0x06, e.g. 0x1532 |
| 10 | u8 | fpga_variant | section 5.1 |
| 11 | u8 | adc_bits | 8 |
| 12 | u16 | ring_words | per core: 4096 / 8192 / 6144 |
| 14 | u8 | channels | 2 |
| 15 | u8 | cores_per_channel | 2 |
| 16 | u32 | base_clock_hz | 200000000; dt = (N+1) / base_clock_hz |
| 20 | u32 | caps | bit field below |
| 24 | u32 | gen_clock_hz | 50000000, or 0 without generator |
| 28 | u16 | max_total_samples | per channel, see 5.1 |
| 30 | u16 | min_total_samples | 80 [V Donwulff: "minimum total 80"; recheck in firmware/scope] |
| 32 | u8 | read_lag | 0 or 1, section 7.4 |
| 33 | u8 | n_ranges | 7 (0..6) |
| 34 | u8 | n_hw_ranges | 6 (relay codes 0..5) |
| 35 | u8 | interleave_order | 0 = B before A (section 7.3), 1 = A before B |
| 36 | char[16] | fw_version | NUL-padded, e.g. `0.1.0` |
| 52 | u32 | fw_build | first 8 hex digits of the git commit |
| 56 | char[16] | serial | NUL-padded; the USB iSerial string |
| 72 | u16 | screen_width | 800 |
| 74 | u16 | screen_height | 480 |

`caps` bits:

| Bit | Name | Set when |
|---|---|---|
| 0 | PROG_TRIGGER_POS | FPGA has 0x0B/0x0C (variants 2-4) |
| 1 | GENERATOR_DDS | FPGA has 0x50-0x52 (variants 2-4) |
| 2 | HOLDOFF | FPGA implements 0x1B. **0 for every known bitstream**: Atlan4 stores the value and never uses it [V zaklad.v table, "stored, never used"] |
| 3 | EXT_TRIGGER | trigger source 2 exists (future FPGA work on the calibration pin) |
| 4 | TRIG_BOTH_CORES | trigger looks at A and B (future FPGA work) |
| 5 | FORCE_TRIGGER | FORCE has been verified on this variant (test T7) |
| 6 | SCREENSHOT | READ_SCREEN built in |
| 7 | DEVICE_AVERAGE | ACQUIRE_AVG built in |
| 8 | DEBUG_RAW | DEBUG_FPGA built in (debug builds only) |
| 9 | LIVE_READ | READ_LIVE built in |
| 10 | ENTER_FEL | ENTER_FEL built in |
| 11 | READ_LAG_UNVERIFIED | `read_lag` is a default, not a measured value |
| 12 | INTERLEAVE_UNVERIFIED | `interleave_order` is the 1.00o5 convention, not measured |
| 13-31 | reserved | 0 |

**GET_STATUS (0x0003).** Request: empty. Response (28 bytes). Also the response body of SET_REMOTE, ARM,
FORCE, STOP and WAIT.

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | control | 0 LOCAL, 1 REMOTE |
| 1 | u8 | acq_state | 0 IDLE, 1 ARMED, 2 TRIGGERED, 3 DONE, 4 ABORTED (REMOTE only; 0 in LOCAL) |
| 2 | u8 | flags | bit0 fpga_triggered (0x18), bit1 fpga_done (0x0A), bit2 auto_timeout, bit3 dtr, bit4 charging, bit5 deferred_pending, bit6 overload_ch1, bit7 overload_ch2 (last readout) |
| 3 | u8 | ui_busy | 1 while the UI is modal (LOCAL_BUSY) |
| 4 | u32 | capture_id | last DONE capture |
| 8 | u16 | trigger_addr | raw 0x14 value latched at DONE |
| 10 | u16 | capture_start_word | section 7.5 |
| 12 | u32 | ms_since_arm | |
| 16 | u32 | uptime_ms | |
| 20 | u16 | watchdog_remaining_s | 0xFFFF disabled |
| 22 | u8 | battery_level | as GET_BATTERY |
| 23 | u8 | exit_reason | last REMOTE->LOCAL cause, section 6.1 (0 none) |
| 24 | u32 | reserved | 0 |

In LOCAL_BUSY the FPGA flags are the last cached values (the firmware must not touch the FPGA bus from
inside a UI transaction).

**SET_REMOTE (0x0004).** Request (4 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | action | 0 release, 1 acquire (acquire while REMOTE only updates flags and watchdog) |
| 1 | u8 | flags | bit0 KEEP_SETTINGS (on release keep host settings instead of restoring the snapshot), bit1 SHOW_TRACES (draw each captured trace, costs tens of ms per capture [U]), bit2 IGNORE_DTR |
| 2 | u16 | watchdog_s | 0 disables |

Response: GET_STATUS body. Errors: `E_BUSY` when the UI is modal.

**GET_BATTERY (0x0005).** Response (4 bytes): u8 `level` (firmware `batterychargelevel`; 20 after boot,
falls with voltage; scale [U] [V `S100 power_and_battery.c:24-71`]), u8 `charging`, u8 `keyadc_raw` (6-bit
KEYADC reading), u8 reserved.

**TEST_PATTERN (0x0006).** Request (8 bytes): u32 `length` (<= max_response_payload), u8 `pattern` (0 byte
counter `i & 0xFF`, 1 all 0x00, 2 all 0xFF, 3 xorshift32 seeded with `length`), 3 bytes reserved.
Response: `length` bytes. For throughput tests and the 512-byte boundary / ZLP test.

### 8.3 Configuration commands

All SET_* requests carry a `mask`; only masked fields are applied. Every SET_* response returns the full
resulting configuration as read back from the firmware's state after clamping (the host must use these
values, not its request) [D, pattern from zinntikumugai/rigol-oscilloscope-mcp].

**GET_CHANNEL (0x0100).** Request: u8 `channel` (0 CH1, 1 CH2). Response: CHANNEL_CONFIG (24 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | channel | |
| 1 | u8 | enable | readout and display only (7.2) |
| 2 | u8 | range | 0..6 = UI V/div index at 1x probe: 5 V, 2.5 V, 1 V, 500 mV, 200 mV, 100 mV, 50 mV per div [V `S100 variables.c:620-628`] |
| 3 | u8 | hw_range | relay code written to 0x33/0x36: `min(range, 5)`; range 6 is a software zoom of range 5 [V `S100 fpga_control.c:340-353`] |
| 4 | u8 | coupling | 0 DC, 1 AC (FPGA 0x34/0x37 gets 1 for DC, 0 for AC [V `S100 fpga_control.c:320-336`]) |
| 5 | u8 | invert | |
| 6 | u8 | probe_index | UI probe factor: 0 = 0.5x, 1 = 1x, 2 = 10x, 3 = 20x, 4 = 50x, 5 = 100x, 6 = 1000x [V `S100 variables.c:901-912`]; display only, never changes hardware |
| 7 | u8 | reserved | |
| 8 | i16 | dcoffset | UI offset value |
| 10 | u16 | offset_pwm | value written to 0x32/0x35 = `dc_calibration_offset[range] + dcoffset` [V `S100 fpga_control.c:357-373`]; PWM full scale 2000 counts at 25 kHz [V zaklad.v pwmcounter LAST=1999] |
| 12 | u16 | center_cal | `dc_calibration_offset[range]` |
| 14 | u16 | reserved | |
| 16 | u32 | uv_per_lsb_q8 | microvolts per ADC code x 256 at the BNC with a 1x probe (section 8.5) |
| 20 | i32 | zero_code_q8 | processed code that represents 0 V, x 256 (section 8.5) |

**SET_CHANNEL (0x0101).** REMOTE only. Request (12 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | channel | |
| 1 | u8 | mask | bit0 enable, bit1 range, bit2 coupling, bit3 offset, bit4 invert, bit5 probe_index |
| 2 | u8 | enable | |
| 3 | u8 | range | 0..6 |
| 4 | u8 | coupling | 0 DC, 1 AC |
| 5 | u8 | invert | |
| 6 | u8 | probe_index | 0..6 |
| 7 | u8 | offset_mode | 0 = `offset` is the UI `dcoffset`; 1 = `offset` is the raw PWM compare value 0..2000 |
| 8 | i32 | offset | |

Response: CHANNEL_CONFIG. Side effects: a range or coupling change switches relays (audible click) and the
firmware rewrites the offset PWM (the centre depends on the range). The firmware then blocks ARM for 50 ms
of settling, as the UI does [V `S100 statemachine.c:6579-6596`]. Errors: `E_BAD_ARG` (detail = field index).

**GET_TIMEBASE (0x0102).** Response: TIMEBASE (16 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | timeperdiv_index | UI index 0..34 (0 = 50 s/div, 34 = 5 ns/div) or 0xFF after a raw set |
| 1 | u8 | samplerate_index | UI sample-rate table index 0..17 or 0xFF |
| 2 | u16 | reserved | |
| 4 | u32 | divider | N written to 0x0D |
| 8 | u32 | sample_rate_hz | floor(200e6 / (N + 1)), per channel interleaved |
| 12 | u32 | sample_period_ps | 5000 x (N + 1), saturates at 0xFFFFFFFF |

**SET_TIMEBASE (0x0103).** REMOTE only. Request (8 bytes): u8 `mode` (0 = UI index, 1 = raw divider), 3
bytes reserved, u32 `value`. Response: TIMEBASE.

- UI index: the firmware maps it with `time_per_div_sample_rate[]` and `sample_rate_settings[]`
  [V `S100 variables.c:386-401`, `:723-753`]. Indices 0..10 are long/roll timebases and return
  `E_UNSUPPORTED` in v1. 1.00o5 also has a `time_per_div_sample_rate_15k[]` table, shifted by one rate step,
  for deep memory settings [V `S100 variables.c:403-418`]; the protocol always uses the first table and
  reports the resulting N, so hosts should prefer raw dividers.
- Raw divider: any u32. The UI table spans N = 0 (200 MS/s) to 399999 (500 S/s).

| UI index | Time/div | N | Rate per channel |
|---|---|---|---|
| 11 | 200 ms | 399999 | 500 S/s |
| 18 | 1 ms | 1999 | 100 kS/s |
| 20 | 200 us | 399 | 500 kS/s |
| 21 | 100 us | 199 | 1 MS/s |
| 23 | 20 us | 39 | 5 MS/s |
| 24 | 10 us | 19 | 10 MS/s |
| 25 | 5 us | 9 | 20 MS/s |
| 28-34 | 500 ns .. 5 ns | 0 | 200 MS/s |

(Full table: `research/notes/repo-donwulff.md`, sample-rate table; indices from `S100 variables.c:386`.)

**GET_TRIGGER (0x0104).** Response: TRIGGER (28 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | source | 0 CH1 (core A), 1 CH2 (core A), 2 EXT (reserved, needs EXT_TRIGGER) |
| 1 | u8 | edge | 0 rising, 1 falling |
| 2 | u8 | level | raw core-A code 0..255 (7.5) |
| 3 | u8 | mode | 0 auto, 1 normal, 2 single |
| 4 | u32 | pretrigger_samples | per channel, interleaved count, even |
| 8 | u32 | total_samples | per channel, even |
| 12 | u32 | holdoff | raw; always 0 without HOLDOFF |
| 16 | u32 | auto_timeout | write clocks, value written to 0x0E |
| 20 | u16 | pretrigger_words | written to 0x0B = pretrigger_samples / 2 [V `S100 fpga_control.c:646-651`] |
| 22 | u16 | total_words | written to 0x0C = total_samples / 2 |
| 24 | u32 | reserved | |

**SET_TRIGGER (0x0105).** REMOTE only. Request (24 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u16 | mask | bit0 source, bit1 edge, bit2 level, bit3 mode, bit4 pretrigger, bit5 total, bit6 holdoff, bit7 auto_timeout |
| 2 | u8 | source | |
| 3 | u8 | edge | |
| 4 | u8 | level | |
| 5 | u8 | mode | |
| 6 | u16 | reserved | |
| 8 | u32 | pretrigger_samples | |
| 12 | u32 | total_samples | |
| 16 | u32 | holdoff | |
| 20 | u32 | auto_timeout | 0 = firmware default: `total_samples` write clocks, which is what 1.00o5 writes for Atlan4 bitstreams [V `S100 fpga_control.c:565-569`]; stock uses the UI's `timebase_settings[]` table |

Response: TRIGGER. Validation (`E_BAD_ARG` with detail = bit number of the field):

- `pretrigger_samples` and `total_samples` even; `min_total_samples <= total <= max_total_samples`;
  `pretrigger <= total - 2`.
- Without PROG_TRIGGER_POS (stock FPGA) only `pretrigger = 1500`, `total = 3000` are accepted, others give
  `E_UNSUPPORTED`.
- `holdoff != 0` without HOLDOFF gives `E_UNSUPPORTED`. `source = 2` without EXT_TRIGGER gives `E_UNSUPPORTED`.
- In REMOTE the level is used as given. The UI's derivation of the level from the on-screen trigger marker
  (`fpga_set_trigger_level`, called on every UI arm [V `S100 scope_functions.c:727`]) is not run.

### 8.4 Acquisition commands

**ARM (0x0200).** REMOTE only. Request: 4 bytes reserved (0). Response: GET_STATUS body (state ARMED).
Firmware sequence, derived from `fpga_do_conversion` [V `S100 fpga_control.c:604-660`]:

1. Wait out any pending relay settling time (8.3).
2. Write 0x0F = 0 (trigger enable; dead on Atlan4 FPGAs, meaningful on stock [V zaklad.v 0x0F; Donwulff
   inventory]).
3. Write 0x01 = 1 (sample-system reset). Poll 0x05 bit0 until 1, at most 2,000,000 reads; on timeout answer
   `E_FPGA` (1.00o5 waits forever here [V `S100 fpga_control.c:636`]).
4. With PROG_TRIGGER_POS: write 0x0B = pretrigger_words, 0x0C = total_words, 0x0E = auto_timeout.
5. Write 0x01 = 0 (release). State = ARMED, `ms_since_arm` restarts.

**FORCE (0x0201).** REMOTE only, cap FORCE_TRIGGER. Ends an ARMED capture now. Implementation: write 0x1A
= 0 (auto mode). The Atlan4 stop condition `~trigger_mode && time_base_timeout` then becomes true at once if
the auto timeout has passed [V zaklad.v :444-447]; the next ARM restores the configured mode. Whether this
works on each variant is test T7 [U]; the capability bit is only set after it passes. `E_STATE` if not
ARMED.

**STOP (0x0202).** REMOTE only. Ends polling, state ABORTED, completes a pending WAIT or ACQUIRE_AVG with
`E_ABORTED`. The FPGA is left running (in normal mode its ring keeps wrapping); the next ARM resets it.

**WAIT (0x0203).** REMOTE only, deferred. Request: u32 `timeout_ms` (0..10000). The response is sent when
the state reaches DONE or the timeout expires. It is a GET_STATUS body with status OK either way; the host
checks `acq_state`. If the state is already DONE, it returns at once.

**READ_RAW (0x0204).** REMOTE only. Request (16 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | core_mask | bit0 CH1 A (0x20), bit1 CH1 B (0x21), bit2 CH2 A (0x22), bit3 CH2 B (0x23) |
| 1 | u8 | addr_mode | 0 absolute ring word address; 1 relative to `capture_start_word`; 2 relative to `trigger_addr` |
| 2 | u8 | layout | 0 per-core blocks; 1 interleaved channel stream (7.3), needs both cores of each selected channel |
| 3 | u8 | flags | bit0 ALLOW_NOT_DONE (read while ARMED; data may be changing) |
| 4 | i32 | address | word address or signed word offset, reduced modulo `ring_words` |
| 8 | u32 | count | words per core, 1..ring_words |
| 12 | u32 | reserved | |

Response: 16-byte header, then data.

| Off | Type | Field |
|---|---|---|
| 0 | u32 | capture_id |
| 4 | u16 | start_word (resolved absolute address) |
| 6 | u16 | ring_words |
| 8 | u32 | count |
| 12 | u8 | core_mask |
| 13 | u8 | layout |
| 14 | u8 | acq_state at read time |
| 15 | u8 | read_lag applied |

Data, layout 0: one block of `count` bytes per set bit of `core_mask`, in ascending bit order. Layout 1:
per selected channel (CH1 first) one block of `2 x count` bytes interleaved per 7.3. Errors: `E_STATE` if not
DONE and ALLOW_NOT_DONE clear; `E_OVERFLOW` if 16 + count x cores > max_response_payload (a full AL3 read of all four cores is
16 + 4 x 8192 = 32,784 bytes and fits; the limit matters only for future deeper memory).

Cost: about 0.30-0.35 us per byte on the bus [M 1014D raw loop], so a full AL3 dual-channel read
(32 KiB) takes about 10-12 ms of FPGA time plus USB time.

**READ_TRACE (0x0205).** LOCAL and REMOTE. Request (12 bytes): u8 `ch_mask` (bit0 CH1, bit1 CH2), u8 `flags`
(bit0 REREAD = rebuild from the ring even if already built for this capture, bit1 NO_INVERT), u16 reserved,
u32 `start` (sample index), u32 `count` (0 = to the end). Both count samples per channel (bytes), against the
buffer's `sample_count` (the capture's length, not the live GET_TRIGGER total). `start` > sample_count is E_BAD_ARG
detail 4; since fw 0.2.1 a `count` past the end is clipped and the header's `count` says how many were sent
(0.1.x-0.2.0: E_BAD_ARG detail 8 "count").

In REMOTE, the firmware builds the processed trace from the ring on the first READ_TRACE after a capture
(section 7.6). This uses the 1.00o5 readout loop, which costs about 2.6-3.1 us per sample [M 1014D], so
roughly 85-100 ms for 2 x 16k samples. Use READ_RAW in throughput-critical loops and apply the compensation
on the host. In LOCAL, it returns a copy of the UI's last trace.

Response: 32-byte header, then 16 bytes of metadata per selected channel, then data (`count` bytes per
channel, CH1 first).

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 | capture_id | |
| 4 | u32 | sample_count | samples per channel in the buffer |
| 8 | u32 | start | |
| 12 | u32 | count | |
| 16 | i32 | hw_trigger_index | = pretrigger_samples (trigger position by geometry) |
| 20 | i32 | sw_trigger_index | firmware's refined crossing (`disp_trigger_index` from `scope_process_trigger`, steps of 2 around the centre [V `S100 scope_functions.c:886-939`]) or -1 |
| 24 | u32 | divider | N at capture time |
| 28 | u8 | ch_mask | |
| 29 | u8 | origin | 0 remote capture, 1 UI capture |
| 30 | u16 | reserved | |

Per-channel metadata (16 bytes):

| Off | Type | Field |
|---|---|---|
| 0 | u8 | channel |
| 1 | u8 | range |
| 2 | u8 | coupling |
| 3 | u8 | flags: bit0 inverted, bit1 clipped at 0, bit2 clipped at 255 (from `ADoverload`) |
| 4 | u32 | uv_per_lsb_q8 |
| 8 | i32 | zero_code_q8 |
| 12 | i16 | adc_a_comp |
| 14 | i16 | adc_b_comp |

**READ_LIVE (0x0206).** REMOTE only, cap LIVE_READ. Request (4 bytes): u8 `core` (0..3 = 0x24..0x27), u8
reserved, u16 `count` (1..4096). Response: `count` bytes read from the live ADC pins, not from memory
[V zaklad.v 0x24-0x27]. The pins are sampled asynchronously to the ADC clock, so single bytes can be torn
[U]. The UI uses this path for roll mode and for averaging 10-100 bytes [V `S100 fpga_control.c:1345-1433`].
Good for a quick DC level without arming.

**ACQUIRE_AVG (0x0207).** REMOTE only, deferred, cap DEVICE_AVERAGE (firmware phase 3). Request (20 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u16 | n | 1..4096 captures |
| 2 | u8 | ch_mask | |
| 3 | u8 | flags | bit0 REQUIRE_TRIGGER (drop captures that ended by auto timeout) |
| 4 | u32 | per_capture_timeout_ms | |
| 8 | u32 | start | sample index relative to the capture start |
| 12 | u32 | count | samples per channel |
| 16 | u32 | reserved | |

The firmware loops ARM, wait, raw interleaved readout of the window, per-core compensation, and u32
accumulation, while still answering GET_STATUS and STOP. Response: u16 `n_used`, u16 `n_dropped`, u32
`first_capture_id`, u32 `count`, u8 `ch_mask`, 3 bytes reserved, then per channel `count` x u16 = mean code x
256 (not inverted). `E_BAD_ARG` if `n x per_capture_timeout_ms > 60000`. Alignment is the hardware trigger
only (2 dt quantisation, 7.5); edge-interpolated alignment is done on the host.

### 8.5 Voltage scaling [U]

The firmware's display path converts a code to volts as
`volts = ((code - 128) x input_calibration[range]) >> 21`, then multiplies by
`volt_calc_data[probe][range].mul_factor` [V `S100 scope_functions.c:4176-4195`, `:1475-1482`,
`VOLTAGE_SHIFTER = 21` in `variables.h:152`]. The calibration routine targets `calibVolt = 30000` for a
300 mV input at range 5, 1x probe, which implies a result unit of 10 uV. From that:

```
uv_per_lsb      = input_calibration[range] / 2^21 * mul_factor[1x][range] * 10
uv_per_lsb_q8   = round(256 * uv_per_lsb)
zero_code       = 128 - dcoffset * 100 / dc_shift_value      (from S100 fpga_control.c:1308)
volts(code)     = (code - zero_code) * uv_per_lsb * 1e-6 * probe_factor
```

Example: range 5 (100 mV/div), default `input_calibration` 3,600,000 [V `S100 variables.c:920`
`signal_adjusters`], mul_factor 200 gives 3.43 mV per code, about 29 codes per division. The firmware
computes `uv_per_lsb_q8` and `zero_code_q8` with the same integer formulas so the host needs no tables. The
whole chain must be checked against a known DC voltage (test T8) before trusting absolute volts.

### 8.6 Generator

Hardware: 32-bit phase accumulator on 50 MHz, output `phase_acc < duty_threshold` on the calibration pin,
3.3 V logic, tri-state when disabled [V zaklad.v :65-80, :271-281]. f = phase_inc x 50 MHz / 2^32
(11.64 mHz steps), duty = threshold / 2^32. Edges are registered at 50 MHz, so 20 ns jitter; usable up to a
few MHz, absolute maximum 25 MHz. Stock FPGA: fixed 1 kHz, always driven, no control.

**GET_GEN (0x0300).** Response GEN (24 bytes):

| Off | Type | Field |
|---|---|---|
| 0 | u8 | enable |
| 1 | u8[3] | reserved |
| 4 | u32 | phase_inc (0x51) |
| 8 | u32 | duty_threshold (0x52) |
| 12 | u64 | freq_mhz_actual (millihertz) |
| 20 | u16 | duty_bp_actual (basis points, 0..10000) |
| 22 | u16 | reserved |

**SET_GEN (0x0301).** REMOTE only, cap GENERATOR_DDS. Request (20 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | mask | bit0 enable, bit1 freq, bit2 duty |
| 1 | u8 | enable | |
| 2 | u8 | freq_mode | 0 = `freq` in millihertz, 1 = raw phase_inc |
| 3 | u8 | duty_mode | 0 = `duty` in basis points, 1 = raw threshold |
| 4 | u64 | freq | <= 25 MHz (phase_inc <= 2^31) |
| 12 | u32 | duty | 0..10000 bp |
| 16 | u32 | reserved | |

Response: GEN. The firmware writes duty, then frequency, then enable, to avoid a burst at the old duty
[D]. Conversions use 64-bit integers: `phase_inc = round(freq_mhz x 2^32 / 50e9)`,
`threshold = round(duty_bp x (2^32 - 1) / 10000)` (1.00o5 takes whole Hz and whole percent
[V `S100 fpga_control.c:268-296`]).

### 8.7 Calibration and screen

**GET_CAL (0x0400).** Request: u8 `channel`. Response (72 bytes), copied from the channel settings loaded
from SD sector 708 [V `S100 variables.h:437-472`]:

| Off | Type | Field |
|---|---|---|
| 0 | u8 | channel |
| 1 | u8[3] | reserved |
| 4 | u16[7] | dc_calibration_offset (PWM centre per range) |
| 18 | u16 | reserved |
| 20 | i32[7] | input_calibration (gain factor per range, 2^21 scale) |
| 48 | i16 | adc_a_comp (`adc1compensation`, added to core A codes) |
| 50 | i16 | adc_b_comp (`adc2compensation`, added to core B codes) |
| 52 | u32 | dc_shift_center |
| 56 | i32 | dc_shift_size |
| 60 | i32 | dc_shift_value |
| 64 | u32 | adc_a_rawavg (last readout) |
| 68 | u32 | adc_b_rawavg (last readout) |

There is no SET_CAL in v1 [D]: calibration stays a UI procedure so a host bug cannot corrupt sector 708.

**READ_SCREEN (0x0401).** Cap SCREENSHOT. Request: u16 `row`, u16 `nrows`. Response: u16 `width`, u16 `row`,
u16 `nrows`, u16 `format` (1 = RGB565 little-endian [U]), then `nrows x width x 2` bytes from the
framebuffer `maindisplaybuffer` [V `S100 fnirsi_1013d_scope.c:81`]. 40 rows (64,000 bytes) fit one
response; a full screen is 12 requests, 768,000 bytes.

### 8.8 Debug commands

**DEBUG_FPGA (0x0F00).** REMOTE only, cap DEBUG_RAW (debug builds). Request: a list of operations, each `u8
op, u8 n, data`: op 1 WRITE_CMD (n = 1, one command byte), op 2 WRITE_DATA (n bytes), op 3 READ_DATA (n bytes
to read, 0 means 256, no data follows). Response: all READ_DATA bytes concatenated. Equivalent to
pcprogrammer's `WC/WB/RB` console [V `Test code/f1c100s_ch340/commands.c`]. Used for bring-up tests
(section 12).

**ENTER_FEL (0x0F01).** Cap ENTER_FEL. Request: u32 magic 0x4C45465F. The device answers OK, waits for the
response to leave, disables USB, and jumps to the boot ROM FEL entry so `xfel` can load a new image without
the BOOT button [U: entry address and cache handling to be taken from Donwulff's bootloader F3 path,
`repo-donwulff.md` section 5, "I-cache invalidate before the FEL jump"].

## 9. Error codes

| Code | Name | Meaning |
|---|---|---|
| 0 | OK | |
| 1 | E_BAD_CRC | payload CRC mismatch |
| 2 | E_BAD_LEN | payload length wrong for the command, or frame over 512 bytes |
| 3 | E_UNKNOWN_CMD | |
| 4 | E_BAD_ARG | field out of range; `detail` = field index or mask bit |
| 5 | E_UNSUPPORTED | capability missing on this FPGA variant or firmware build |
| 6 | E_NOT_REMOTE | command needs REMOTE ownership |
| 7 | E_BUSY | UI is modal, or a deferred request is already pending |
| 8 | E_STATE | wrong acquisition state (READ_RAW before DONE, FORCE when not ARMED) |
| 9 | E_FPGA | FPGA did not become ready (0x05 poll timed out) |
| 10 | E_ABORTED | deferred request ended by STOP, release or watchdog |
| 11 | E_VERSION | header proto_major not supported |
| 12 | E_OVERFLOW | response would exceed max_response_payload |
| 13 | E_IO | 1.3: SD card error; `detail` = driver error (2 timeout, 4 sector out of range, 1 other) or 0xFFFF read-back mismatch |
| 255 | E_INTERNAL | firmware bug; text payload says where |

## 10. ASCII probe [D]

When the parser is idle and the next byte is `#` (0x23, never a magic byte), it collects a line of up to
63 bytes until CR or LF and answers in ASCII:

| Line | Reply |
|---|---|
| `#VER` | `#OK,FNIRSI-1013D-AI,fw=<fw_version>,proto=1.0,fpga=0x<word>` + LF |
| `#LOCAL` | releases REMOTE (exit_reason 1), replies `#OK` + LF; an escape hatch from a terminal |
| anything else | `#ERR,UNKNOWN` + LF |

The OEM-app reply `V1.3.0C MOD V9B4` of v0.27t [V `S27T usb_interface.c:1636`] is not kept: compatibility
with the FNIRSI PC app is not a goal. Linux ModemManager may probe a new ttyACM with `AT` commands; those
bytes are neither magic nor `#` and are discarded by the parser (see the firmware plan risks for the udev
rule).

## 11. Throughput expectations

| Step | Estimate | Basis |
|---|---|---|
| ARM overhead | ~50 us | [M] arm-only rate 21 k/s on a 1014D |
| Fill time | total_words x 2 dt | e.g. 16384 samples at 20 MS/s = 0.82 ms |
| FPGA raw readout | 0.30-0.35 us per byte | [M] 1014D raw loop |
| USB transfer, HS bulk, PIO | 5-20 MB/s | [U] no measurement on this core; TEST_PATTERN measures it |
| Full AL3 dual-channel capture via READ_RAW | ~15-20 ms -> ~50-65 captures/s | sum of the above |
| Window of 2000 samples x 1 channel via READ_RAW | ~1-2 ms -> several hundred per second | |

## 12. Unverified facts and the bench tests that settle them

All tests run in REMOTE mode with DEBUG_RAW enabled, on the owner's AL3 unit, first with the stock bitstream
(0x1432) if that is what is flashed.

| Id | Fact | Test |
|---|---|---|
| T1 | ZLP needed / handled by usbser.sys and Linux cdc_acm | TEST_PATTERN lengths 492, 495, 496, 1008, 4076 (frames of 512, 515, 516, 1028, 4096 bytes) 1000 times each; no timeouts |
| T2 | USB throughput | TEST_PATTERN 65536 x 200, report MB/s |
| T3 | `read_lag` and the stale first byte | DONE capture; READ via DEBUG_FPGA: set 0x1F = a, read 64 bytes; set 0x1F = a + 1, read 64; compare shifted sequences; repeat for a near the wrap |
| T4 | A/B interleave order | generator square wave into CH1 at 200 MS/s; compare the edge position on core A vs core B over 1000 captures; choose the order with monotonic edges |
| T5 | `trigger_addr` alignment | same signal; locate the crossing in READ_RAW data relative to `pretrigger_words`; record the offset per variant |
| T6 | 0x18 behaviour on auto timeout | auto mode, no signal: check 0x18 = 0 at DONE |
| T7 | FORCE via 0x1A = 0 | normal mode, no signal, ARM, FORCE: DONE within 1 ms |
| T8 | voltage scaling 8.5 | known DC (e.g. 1.000 V reference) on every range; compare computed volts |
| T9 | RGB565 framebuffer format | READ_SCREEN vs a photo of the screen |
| T10 | stock trigger core | stock FPGA: signal on CH1 with B-only glitch; check whether it triggers |

## 13. Example frames

```
GET_INFO request, seq 1, no payload:
a5 13 01 00 02 00 01 00 00 00 00 00 00 00 22 a6

PING request, seq 2, payload "hi":
a5 13 01 00 01 00 02 00 00 00 02 00 00 00 f0 32  68 69  ac 2a 93 d8

PING response (flags = RESPONSE):
a5 13 01 01 01 00 02 00 00 00 02 00 00 00 b9 ea  68 69  ac 2a 93 d8

SET_TIMEBASE request, seq 3, mode 0 (UI index), value 19:
a5 13 01 00 03 01 03 00 00 00 08 00 00 00 21 28  00 00 00 00 13 00 00 00  18 27 8e 27

SET_CHANNEL response with E_NOT_REMOTE (status 6), seq 4, no payload:
a5 13 01 01 01 01 04 00 06 00 00 00 00 00 d8 e1
```

These vectors go into `mcp/tests/` and into the firmware's host-side parser test.

## 14. Implementation notes (firmware v0.1.0 - v0.2.0)

Where the firmware in `firmware/scope/` (pc_protocol.c, pc_cmds.c, pc_transport.c, pc_hal_scope.c)
differs from or fills a gap in sections 1-13. Hosts written against this document need only the
"host-visible" items. Shared test vectors: `docs/protocol-vectors.json`.

### Host-visible

| # | Topic | Firmware behaviour | Why |
|---|---|---|---|
| 1 | Oversize request (len > 492, valid header CRC) | E_BAD_LEN is sent at the header; then exactly len + 4 bytes are discarded (a huge len ends with the 100 ms silence timeout) instead of hunting through them | hunting turned payload bytes into false `#` lines; the header CRC vouches for len |
| 2 | Request with the RESPONSE flag | dropped silently, no answer | a device never receives responses; avoids loops with echoing hosts |
| 3 | Short request payloads | zero-extended (section 5 compatibility rule); E_BAD_LEN only for oversize frames and malformed DEBUG_FPGA op lists | 5 says shorter payloads read missing fields as zero |
| 4 | Error `detail` | SET_CHANNEL / SET_TRIGGER / SET_GEN: the mask bit number of the field (0xFE = mask, 0xFF = channel). Every other command: the byte offset of the field in the request | 8.3 numbers SET_* fields by mask bit; offsets are unambiguous elsewhere |
| 5 | Duplicate cache (4.3) | only OK responses with payload <= 256 bytes are cached, keyed by (cmd, seq); cleared on DTR edges and USB reset | an error must not be replayed: a retry after E_BUSY must run again |
| 6 | WAIT in IDLE or ABORTED | answers at once with the current status (8.4 only says DONE) | nothing can complete |
| 7 | ARM while ARMED or TRIGGERED | E_STATE; STOP first | 6.2 diagram re-arms only from DONE/ABORTED/IDLE |
| 8 | Unknown FPGA variant | PING, GET_INFO, GET_STATUS, GET_BATTERY, TEST_PATTERN and `#VER` work; everything else E_UNSUPPORTED (5.1 lists PING/INFO/STATUS) | battery and the link test touch no FPGA |
| 9 | LOCAL_BUSY | detected as "no main-loop pass for 250 ms". BUSY polls run in the touch-panel scan (all modal menu loops, MSC screen) and in the calibration/auto-setup spin loops. A request that needs the main loop waits there up to 250 ms, then gets E_BUSY. A very slow normal UI pass (> 250 ms) can also give E_BUSY: retry | menus are modal loops in 1.00o5 with no common hook |
| 10 | GET_STATUS flags bits 0-2 in LOCAL (1.2) | always 0: the firmware never touches the FPGA while the UI owns it. 1.3: bits 0-2 describe the host single capture (row 30) | 6.1 single owner |
| 11 | KEEP_SETTINGS (1.2 only; 1.3 has no snapshot, row 31) | SET_REMOTE release uses the flag of the release request; every other exit (tap, watchdog, DTR, USB, `#LOCAL`, power-off) uses the flag given at acquire. Kept settings are re-pushed through the UI settings, so a raw divider, a raw trigger level and raw DDS words are replaced by what the UI settings express | the UI settings cannot hold raw values |
| 12 | TEST_PATTERN 3 | xorshift32: state = length; before bytes 4k..4k+3 the state steps once (x ^= x << 13; x ^= x >> 17; x ^= x << 5); the 4 bytes are the new state little-endian | 8.2 named the generator only |
| 13 | GET_GEN | stock bitstream: enable 1, phase/threshold 0, freq 1,000,000 mHz, duty 5000 bp [U duty]. LOCAL on Atlan4: phase/threshold from the UI's Hz and % with the 1.00o5 integer formulas | no DDS registers on stock |
| 14 | SET_GEN conversions | phase_inc = round(f_mHz x 2^22 / 5^11), threshold = round(bp x (2^32 - 1) / 10000); `freq_mhz_actual` = round(phase_inc x 5^11 / 2^22) | exact form of the 8.6 formulas in 64 bits (50e9 = 2^10 x 5^11) |
| 15 | CHANNEL_CONFIG `range` | `samplevoltperdiv` (what the hardware does); SET_CHANNEL range sets display and sample V/div equal. uv_per_lsb_q8 uses input_calibration[range] and mul_factor[1x][range] | 8.3 |
| 16 | SET_CHANNEL offset | the resulting PWM value (centre + dcoffset) must be 0..2000 whenever offset or range is in the mask, else E_BAD_ARG detail 3 | the PWM period is 2000 counts |
| 17 | READ_TRACE | 1.2: REMOTE needs a DONE capture (E_STATE otherwise); 1.3: row 36. Channels are built on demand and the build is reused per capture. `sw_trigger_index` is -1 unless 20 <= pretrigger and pretrigger + 64 <= total (the 1.00o5 search window) | |
| 18 | Not built in v0.1.1 | READ_LIVE, ACQUIRE_AVG, READ_SCREEN: caps 0, E_UNSUPPORTED. DEBUG_FPGA and FORCE exist only in `PC_DEBUG=1` builds, which set DEBUG_RAW and FORCE_TRIGGER (FORCE before test T7) | phase 2/3 of the plan; fail closed |
| 19 | USB identity | VID:PID 0x1209:0x0001 (pid.codes test PID, development only), bcdDevice 0x0100, manufacturer `fnirsi-1013d-ai`, product `FNIRSI 1013D`, serial `1013D-0001` on every unit (GET_INFO `serial` is the same string) | per-unit serial and a real PID are plan phase 3 |
| 20 | `min_total_samples` | 80 on variants with PROG_TRIGGER_POS (not checked against the FPGA; the 1.00o5 UI never goes below 3000); 3000 (= max) on the stock bitstream | stock geometry is fixed [U] |
| 21 | `#LOCAL` | 1.2: releases with the acquire-time KEEP_SETTINGS flag; ASCII lines count as traffic for the watchdog. 1.3: `#OK`, no effect | |
| 22 | Retransmitted deferred request | a WAIT with the same cmd and seq as the pending one is ignored; the pending answer follows (no E_BUSY) | host retry after a read timeout |
| 23 | TX stall (v0.1.1) | a response that makes no progress for 2 s (no IN tokens taken) is dropped and the endpoint FIFO flushed; 3.2 said it stays pending | bench 2026-10-18: a stuck frame blocked the device until power-off |
| 24 | New host connection (v0.1.1) | DTR 0 -> 1 also flushes a frame still in flight, so a reopened port never receives the tail of an old response. The device parser drops partial input of the old connection, but since v0.2.11 never a complete request waiting for the main loop (0.1.1-0.2.10 dropped such a request unanswered when it arrived right after the port was opened: bench, first TEST_PATTERN after a FEL load timed out); link events are taken in every poll context, before new bytes are parsed | bench: a fresh connection read the rest of a 64 KiB pattern |
| 25 | `#RESET` (v0.1.1) | ASCII line, honoured even while a response is being sent: drops the frame in flight, a pending WAIT (no answer), the duplicate cache and partial input, then replies `#OK`. Settings and acquisition state stay. 1.3: a READ_RAW held for a UI capture (row 35) is answered first, so `#RESET` is seen at most 1 s later | bench recovery without a power cycle |
| 26 | Protocol 1.1 (v0.1.1) | GET_INFO `proto_minor` = 1, `#VER` says `proto=1.1`. GET_STATUS appends u16 `pass_ms` (offset 28: interval between the last two main-loop polls) and u16 `pass_max_ms` (offset 30: maximum since the previous GET_STATUS); the body is 32 bytes | measure the main-loop period the round trip depends on |
| 28 | GET_LINK_STATS 0x0007 (protocol 1.2, v0.1.4) | any state and poll context. Response: u16 n (27), u16 0, n x u32 counters since boot: rx_packets, rx_bytes, rx_stalls, rx_swept (packets the poll sweep took because their interrupt never came), tx_packets, tx_zlps, tx_kicked (packets loaded by the poll pump), tx_flushes, link_resets (bus reset/suspend/disconnect), dtr_up, dtr_down; parser: frames, hdr_crc_errors, payload_crc_errors, oversize, version_errors, responses_dropped, rx_timeouts, junk_bytes; session: responses, duplicates, tx_aborts, disp_main, disp_busy, disp_isr, held (polls that left a request for the main loop), isr_skipped | bench diagnosis of request loss |
| 29 | GET_BAD_FRAME 0x0008 (protocol 1.2, v0.1.5) | any state and poll context. Response: u32 bad_count, u32 rx_bytes (transport byte count when the bad request completed), u16 len, u16 0, then the last request that failed its payload CRC exactly as received (header + payload + CRC, len bytes). The host diffs it against what it sent | diagnosis of request corruption (fw 0.1.3 census: ~4% E_BAD_CRC) |
| 30 | Protocol 1.3: shared control (v0.2.0) | GET_INFO `proto_minor` = 3, caps bit 13 SHARED_MODE (0x2000) on every known bitstream, `#VER` says `proto=1.3`. No LOCAL/REMOTE, no lock, no banner, no watchdog: the UI loop always runs and every command is accepted at any time, except E_BUSY while a UI menu is modal (row 9). E_NOT_REMOTE is never returned. SET_REMOTE (any action, flags, watchdog) answers OK with the status body and changes nothing; `#LOCAL` answers `#OK` | `docs/plans/collaborative-mode.md`: the owner and the agent use the scope at the same time |
| 31 | Settings (1.3) | host SET_* write the live UI settings through the UI's own apply code and redraw what changed (channel button, time base, trigger, generator, RUN chip); the UI's changes show in GET_* at once. Last writer wins. Host changes persist and are saved at power-off like the user's. SET_TIMEBASE mode 0 applies the target index in one step (index, its sample rate, short mode, FPGA 0x0E/0x0D, then one redraw of the time base label and the trace area; v0.2.3, after the owner saw flicker with v0.2.0-0.2.2, which ran `scope_set_timebase` and in UI single trigger mode also re-armed the UI single). The trigger level is kept the UI way, as the marker position (0..401) the UI derives the level from before every conversion: SET_TRIGGER level places the marker at the nearest position, so the read-back can differ by one code, and the level then follows range and offset changes as on the screen. A raw divider (SET_TIMEBASE mode 1) and raw DDS words (SET_GEN) stay until the UI changes its time base / generator settings; SET_GEN also stores the rounded Hz and % in the UI settings | one live settings set |
| 32 | GET_STATUS (1.3) | 44 bytes. offset 0 `control` = 2 (SHARED). 1 `acq_state`: the host single capture only. 2 flags: bits 0-2 the host single (triggered, done, auto_timeout), others as before. 3 `ui_busy`: 0 idle, 1 a menu is modal, 2 STARTING (USB is up but the main loop has not finished its first pass; see row 38). 4 `capture_id`: every DONE capture of any origin. 8 `trigger_addr` / 10 `capture_start` of that latest capture. 20 watchdog 0xFFFF, 23 exit_reason 0. Appended: 32 u32 `settings_seq`, 36 u32 `capture_settings_seq`, 40 u8 `run_state` (0 STOP, 1 RUN, 2 SINGLE: a host ARM or a UI single is waiting), 41 u8 `capture_origin` (0 host ARM, 1 UI run, 2 UI single), 42 u16 0. The same body answers SET_REMOTE, ARM, STOP, WAIT and SET_ACQ_STATE | the host sees the shared state in one read |
| 33 | `settings_seq` (1.3) | +1 (wraps, never 0xFFFFFFFF) whenever a capture-relevant setting changes, from the UI or the host: per channel enable, range (samplevoltperdiv), coupling, offset, invert, probe; time base index, sample rate, host raw divider, long time base mode; trigger source, edge, mode, level (only while the UI's "always 50 %" is off, since it rewrites the level after every capture; the flag counts instead), pretrigger, total. Detected by comparing a fingerprint at every poll, UI arm and UI DONE. `capture_settings_seq` = settings_seq when the latest capture was armed if nothing changed until its DONE, else 0xFFFFFFFF (also GET_CAPTURE_INFO flags bit 4); 0xFFFFFFFF too before the first capture. The WAIT answer of a host single carries that capture's own value (capture_origin 0), so a host that sees it equal to its run's settings_seq needs no GET_CAPTURE_INFO | long host runs tag every capture with its settings; no lock |
| 34 | ARM, STOP, SET_ACQ_STATE (1.3) | ARM is a host single capture: it stops the UI acquisition (run_state STOP, the screen shows STOP), then runs the 1.2 ARM sequence (rows 7, firmware-internal ARM), so geometry, auto_timeout, WAIT, FORCE and DONE are as in 1.2. At DONE the capture is read into the trace buffers for the enabled channels and the UI shows it; the UI stays in STOP. The host single is aborted (acq_state ABORTED, a pending WAIT answers E_ABORTED) when the UI arms a conversion (RUN on the screen, or a time base tap in single mode) or on SET_ACQ_STATE. ARM in the UI long time base mode: E_STATE detail 2. STOP aborts a host single and stops the UI. New SET_ACQ_STATE 0x0208: request u8 state (0 STOP, 1 RUN) + 3 reserved; does what the RUN/STOP button does (RUN while the UI trigger mode is single is the UI single "WAIT", run_state 2); E_BAD_ARG detail 0 for other values; allowed while a WAIT is pending | the screen shows the host's capture; RUN resumes from either side |
| 35a | READ_TRACE `count` (v0.2.1) | `start` and `count` are samples per channel, checked against the buffer's `sample_count` (header offset 4; the capture's length, which can differ from GET_TRIGGER `total` after a SET_TRIGGER while stopped or before the next UI capture). count 0 or a count past the end is clipped to sample_count - start; start > sample_count: E_BAD_ARG detail 4. Up to 0.2.0 a count past the end was E_BAD_ARG detail 8 "count" | owner's scope_measure asked for the GET_TRIGGER total |
| 35 | READ_RAW (1.3) | no DONE requirement: returns the latest finished capture of any origin (header `capture_id`), while the FPGA ring still holds it (no arm since its DONE). In RUN the UI usually has the next capture armed: the request is then held for at most 1000 ms until that capture finishes, and the UI is kept from re-arming until the request is answered, so it returns the capture that finished next. Meanwhile other requests wait (one request at a time). E_BUSY detail 1 if nothing finished within 1000 ms (normal mode, no trigger). E_STATE detail 0: no capture yet or a host single still armed; detail 1: the ring was overwritten and the UI is stopped (STOP while a UI capture was armed); detail 2: UI long time base mode. flags bit 0 (ANY) reads the ring as it is, without these checks. A READ_RAW split into several requests can see different `capture_id`s in RUN (a new UI capture between chunks): the host compares them, retries, and as a last resort does SET_ACQ_STATE STOP, reads, then RUN | the UI loop rewrites the ring; reading during a conversion would mix two captures |
| 36 | READ_TRACE (1.3) | never waits: the trace buffer of the latest capture (host single: built from the ring at DONE, rebuilt on REREAD while the ring holds it; UI: the UI's own buffer and geometry). Byte 29 `origin` uses row 32's numbering (1.2: 0 remote, 1 UI, so both keep their meaning). Channel metadata are the front end settings the capture was armed with. A UI time base change clears the UI trace buffer (1.00o5) | |
| 37 | GET_CAPTURE_INFO 0x0209 (1.3) | any poll context. Request empty. Response 68 bytes, the settings the latest finished capture was taken with: 0 u32 capture_id, 4 u32 capture_settings_seq, 8 u8 origin, 9 u8 flags (bit 0 triggered, bit 1 auto timeout (0x18 not set at DONE, also for UI captures, test T6), bit 2/3 overload CH1/CH2, bit 4 settings changed while it ran), 10 u16 trigger_addr, 12 u16 capture_start_word ((trigger_addr - pretrigger_words) mod ring_words for every origin), 14 u16 0, (1.4: flags bit 5 = EXT edge seen during the capture) 16 u32 divider, 20 u8 timeperdiv index (0xFF raw divider), 21 u8 trigger source, 22 edge, 23 level, 24 mode (protocol numbering), 25-27 0, 28 u32 pretrigger samples, 32 u32 total samples, 36 + 16 x ch (CH1, CH2): u8 enable, range, coupling, invert, probe_index, 0, u16 offset_pwm, u32 uv_per_lsb_q8, i32 zero_code_q8. E_STATE detail 0 before the first capture | read_capture without ARM; settings per capture |
| 38 | Startup (v0.2.0) | GET_LINK_STATS payload: u16 n, u16 0, then n u32 counters (decode by n; word 0 is the count, not a counter). n per firmware: 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3), 35 (0.2.4), 41 (0.2.5-0.2.6), 47 (0.2.7-0.2.11), 48 (0.2.12-0.2.16: 47 = USB interrupts served by the timer poll, i.e. lost IRQs; retired in 0.2.21, always 0 since), 49 (0.2.17-0.2.18: 48 = ms spent on the boot splash step + 1), 58 (0.2.20: 57 = USB IRQ races: ticks at which the 0.2.14-0.2.19 fallback gate (two ticks without a USB interrupt) would have served while the real USB interrupt was pending in the same dispatch, i.e. what that fallback counted as lost but was not; retired in 0.2.21, always 0 since), 57 (0.2.19: 49-56 = restart record: restarts since the last cold boot or ENTER_FEL, exception type (0 = jump to the image entry, 1 undefined instruction, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved vector, 6 FIQ), exception LR (fault PC = LR - 8 for a data abort, LR - 4 for undefined / prefetch), its SPSR, its SP, then LR, SP and CPSR found at _reset; event type 15 RESTART is logged at boot after a restart: a = exception type, b = low 16 bits of the exception LR or of the reset LR). Since 0.2.19 the exception vectors record and restart instead of hanging. Since 0.2.21 n stays 58 and slots 47 and 57 are retired: always 0, kept so decoders by count keep working (row 42). Counter index, 0-based: 0-26 as row 28; 27 ms from session init to the first main-loop poll; 28 duration of the first main-loop pass; then times from the first main-loop poll, as ms + 1 (0 = not reached; v0.2.2-0.2.3 used ms OR 1): 29 first UI conversion started (after its 0x05 wait), 30 first UI capture processed, and (v0.2.4) within the first pass: 31 after the battery check, 32 after the acquisition step, 33 after the touch handler, 34 after the RTC block (end of the pass). v0.2.5 appends 6 USB device event counters (41 in all): 35 bus resets, 36 suspends, 37 resumes, 38 disconnects, 39 EP0 setup packets, 40 EP0 packets that were not 8 bytes (stalled). v0.2.7 appends 6 RX back-pressure counters (47 in all): 41 stalls entered from the RX interrupt (ring full), 42 releases (main loop made room), 43 re-stalls inside a release, 44 releases with no packet waiting, 45 largest RXCOUNT seen, 46 packets with RXCOUNT > 512. Bench 0.2.3 (SD persist): the first one-word 0.2.4 boot reading put 1515 ms in counter 27 (init to first poll; the splash `load_picture("scope.bmp")` is the prime suspect) and 31 ms in 28. Bench 0.2.1: 3 ms to the first poll, first pass 1516 ms. GET_STATUS `ui_busy` = 2 until the second main-loop poll. Bench 2026-10-18: after a FEL load GET_STATUS showed no completed pass for ~1 s and the first main-loop command waited ~5 s; the first pass is inside the 1.00o5 UI loop (first conversion's 0x05 wait or touch scan suspected, not verified) | hosts back off instead of timing out |
| 39 | MCP indicator (v0.2.3) | a steady green chip "MCP", 38 x 17 px at x 127, y 3, stacked directly above the RUN/STOP chip (same width), while a host is active (DTR up, or a frame within the last 2 s); no per-command pulse (owner decision). It uses the slot of the optional RTC clock, which is not drawn meanwhile and comes back within 0.5 s after the chip goes. Redrawn every 100 ms while on. v0.2.0-0.2.2: x 697, y 22 over the version text, with a pulse | plan: small indicator next to RUN/STOP, no banner |
| 40 | SD sectors (1.3, v0.2.2) | cap bit 14 SD_SECTORS (0x4000) on every known bitstream. Main loop only; the UI pass waits meanwhile (a 64 KiB write takes on the order of 100 ms [U]); PING etc. are still answered from the timer interrupt. **SD_INFO 0x0500**: request u8 flags (bit 0 VERIFY), 3 reserved. Response 64 bytes: 0 u32 card_sectors, 4 u32 part_start (LBA of MBR entry 0; 0 = no valid MBR), 8 u32 write_end (writes allowed in [16, write_end): min(part_start, 2048); 0 = writes off), 12 u32 stage_bytes (65536), then 3 x 16 bytes for sectors 16, 80, 750: u32 sector, u8 magic (0 none, 1 eGON.BT0, 2 eGON.EXE), u8 check (0 not checked, 1 checksum ok, 2 bad, 3 length invalid or over 2048 sectors), u16 0, u32 length, u32 stored checksum. VERIFY recomputes the eGON checksum over `length` bytes on the card (mksunxi.py rule). E_IO detail 0 without a card. **SD_READ_SECTORS 0x0501**: u32 sector, u16 count 1..128, u16 0. Response: u32 sector, u16 count, u16 0, u32 crc32 of the data, u32 0, then count x 512 bytes. E_BAD_ARG detail 4 count, detail 0 beyond the card. **SD_STAGE 0x0502** (answered from the 1 ms timer poll since v0.2.7, so staging needs no main-loop pass per piece): u32 offset, u16 length (= payload - 8, at most 484), u16 0, data. Response 12 bytes: u32 offset, u16 length, u16 0, u32 crc32 of the bytes. E_BAD_LEN detail 4 if length does not match, E_BAD_ARG detail 0 beyond 64 KiB. The stage persists until overwritten. **SD_WRITE_SECTORS 0x0503**: u32 sector, u16 count 1..128, u8 flags (bit 0 CONFIG: allow 708-710, bit 1 BOOT: allow 16-79), u8 0, u32 crc32 of stage[0, count x 512), u32 magic 0x52574453 ('SDWR'). Checks in this order, all before any write: magic (E_BAD_ARG 12), flags (6), count (4), card (E_IO 0), MBR (E_STATE 0: no valid partition table, writes off), window: sector >= 16 and the last sector < write_end (E_BAD_ARG 0), 16-79 need BOOT, any of 708-710 need CONFIG (E_BAD_ARG 0, text says which), stage crc (E_BAD_ARG 8). Then write, read back, compare: E_IO with the driver error, or 0xFFFF on a mismatch. Response 16 bytes: u32 sector, u16 count, u16 0, u32 crc32 read back from the card, u32 0. The power-key interrupt is held off during the write, so the power-off settings save (sector 709) cannot interleave [U]. An SD_WRITE blocks the main loop for its whole duration (bench 0.2.3: up to ~4 s for 128 sectors with read-back, the UI pass waits meanwhile; PING etc. still answer): hosts should write 16-32 sectors per request and allow >= 5 s. Persisting a FEL-loaded image: stage + write the image file in pieces from sector 750 (the image ends well below 2048), then SD_INFO VERIFY: sector 750 check = 1 | owner: persist an image without removing the card |
| 41 | Protocol 1.4: EXT trigger (v0.2.9, FPGA 0x1533) | GET_INFO `proto_minor` = 4. The AL3 family is decoded as (v & 0xFFF0) == 0x1530 and v >= 0x1532 (variant 2, ring 8192, max 16382, by the firmware UI too); 0x1533+ adds caps bit 3 EXT_TRIGGER (0x0008). SET_TRIGGER source 2 = EXT on the calibration pin (3.3 V logic referenced to the scope's USB ground, never more; FPGA internal pull-down; 1 kOhm to FPGA ground. The probe ground (BNC shell) sits about +1.24 V above USB ground (VRF1 via R71): a source on the probe ground reads ~1.24 V low level, unreliable; use a series red LED, anode at the source, and rising edge. Linking the grounds shifts all traces. fpga/al3/README.md "Changes" 2): E_UNSUPPORTED detail 0 without EXT_TRIGGER. With EXT the FPGA ignores the level (it is still stored and read back, as the UI marker) and the UI skips its software trigger refinement; edge (rising / falling), mode, pretrigger, total and auto time-out are unchanged, and trigger_addr has the usual meaning (pad-to-trigger latency 20-25 ns, fpga/al3/README.md; correction from the bench, not built in). Selecting EXT (host or the trigger menu, where EXT is shown only with the cap) switches the generator off, since it drives the same pin; SET_GEN enable while the source is EXT gives E_STATE detail 0 ("EXT trigger uses the pin"); the UI generator Power switch is not blocked (the scope then triggers on its own generator). GET_STATUS byte 42 `ext_flags` (EXT_TRIGGER only, else 0), FPGA cmd 0x28 read at every main-loop poll: bit 0 live EXT level, bit 1 generator enabled (FPGA drives the pin), bit 2 an EXT edge was seen since the last sampling reset (every arm clears it); byte 43 stays 0. GET_CAPTURE_INFO flags bit 5: an EXT edge was seen during that capture. A saved EXT source is kept only on bitstreams with the cap (else CH1) | owner / PI detector work: sync captures to the TX pulse |
| 42 | Protocol 1.5: USB robustness, GET_EVENT_LOG (v0.2.12) | **v0.2.21 removed the tick fallback described here**: the 0.2.20 bench and 30-minute soak showed counter 47 = 0 and counter 57 = 2176, i.e. 1.00o5's dispatcher did not lose a USB interrupt and the fallback only raced the real interrupt (verified 2026-10-18). Since 0.2.21 counters 47 and 57 are always 0 and type 14 is never emitted (the number stays reserved); GET_EVENT_LOG, the snapshot, the 2 ms RX sweep and the TX pump kick are unchanged. History: v0.2.12-0.2.20: the 1 ms timer poll also runs the USB CDC interrupt handler (since v0.2.14 only after two consecutive ticks without a real USB interrupt; 0.2.12-0.2.13 ran it on every tick, which raced the real interrupt during traffic, so their counter 47 is meaningless): the MUSB keeps its interrupt flags until they are read, and 1.00o5's irq_handler clears INTC pending by writing 0 after reading it, so a USB interrupt raised in between was lost; for EP0 nothing else would ever serve it (a control request then times out on the host, which fails the device and resets the port; suspected for the Windows error-22 drops). GET_LINK_STATS counter 47 counts the polls that found flags (v0.2.14+: lost-interrupt candidates). GET_EVENT_LOG 0x0009, any poll context, request empty. Response: u16 n (<= 64; <= 128 since v0.2.15), u16 0, u32 total events since boot, u32 now (ms), then n entries oldest first: u32 ms, u8 type, u8 a, u16 b. v0.2.15 appends the reset snapshot: u16 m (<= 16), u16 frozen (1 = a bus reset froze it), u32 reset ms, then m entries: the events right before the first bus reset since the snapshot was armed, so a later flood or Windows' repeated resets cannot evict them. Re-arming: 0.2.15-0.2.17 at every read; since v0.2.18 only with request byte 0 bit 0 set (u8 flags, other bits E_BAD_ARG detail 0), so a host that reads the log at every session open no longer throws the snapshot away. v0.2.24 appends a bus-level ring after the snapshot: u16 k (<= 16), u16 0, u32 bus-level events since boot, then k entries oldest first (same 8-byte format), holding only types 1-4, 7, 8, 12, 13 and 15-18, so EP0 setups and DTR edges cannot evict them. Since v0.2.24 an EP0 setup identical to the last logged one (bmRequestType, bRequest, wValue low) is logged at most once per second (pyserial repeats GET_LINE_CODING about five times per open); counter 39 still counts every setup. Hosts that stop at the snapshot are unaffected. `ms` is the same clock as GET_STATUS uptime_ms (timer0 ticks, 1 ms). Type 14 ignores the SOF flag (INTUSB bit 3 latches every frame with its interrupt disabled; 0.2.12-0.2.14 logged ~1000/s) and is logged at most once per 100 ms (counter 47 still counts every hit). Since v0.2.16 the tick reads the three flag registers itself and runs the handler body only for a flag other than SOF, so the fallback does nothing at all in a clean session (counter 47 stays 0); 0.2.12-0.2.15 ran the whole handler for SOF 300-500 times per second. Since v0.2.20 the tick no longer reads the flag registers to decide: it checks endpoint state (EP0 CSR0 RxPktRdy, SetupEnd, SentStall, an IN data phase with TxPktRdy clear, a SET_ADDRESS whose status stage completed; EP2 RXCSR RxPktRdy while EP2 OUT is unmasked) and serves only a condition seen on two consecutive ticks with no USB interrupt in between, after reading the flags. It skips any tick at which the USB interrupt is pending. Counter 47 counts these services. Whether the dispatcher's write of 0 to INTC pending actually loses interrupts is unverified (the F1C200s manual does not say how the pending bits clear). Types: 1 bus reset, 2 suspend, 3 resume, 4 disconnect, 5 EP0 setup (a = bRequest, b = bmRequestType << 8 or wValue low byte), 6 EP0 packet not 8 bytes (b = COUNT0), 7 SET_CONFIGURATION, 8 CLEAR_FEATURE(HALT) (a = endpoint), 9 DTR change (a = new DTR), 10 RX ring full (b = RXCOUNT), 11 RX release, 12 TX frame flushed (link / DTR; b = bytes left), 13 TX frame aborted (2 s no progress or #RESET; b = bytes left), 14 retired since v0.2.21 (0.2.12-0.2.20: USB flags found by the timer poll, a = INTUSB, b = INTTX << 8 or INTRX; since v0.2.20 a lost interrupt served by the tick: a = INTUSB as read, b = INTTX << 8 or INTRX as read, low 7 bits each, plus bit 15 = EP0 needed service, bit 7 = EP2 OUT packet waiting). A host port reset shows as suspend then bus reset (the MUSB reports the SE0 as idle first). 16 USB controller state at init (v0.2.22, row 43), 17 and 18 EP2 IN state at a TX abort (v0.2.23, row 43), 19 USB state at the end of init (v0.2.25, row 43) | diagnose USB drops: read the log right after a re-enumeration |
| 43 | USB init state, event type 16 (v0.2.22) | No protocol change (proto 1.5). `usb_device_init` (boot and every CDC <-> MSC switch) logs the USB controller state it finds, then asserts the USB OTG bus soft reset (CCU BUS_SOFT_RST0 bit 24) and the PHY0 reset (USBPHY_CFG bit 0) for 1 ms before 1.00o5's sequence releases them; up to 0.2.21 the resets were only released, so an SD boot configured whatever the bootloader left. Five consecutive type 16 entries, a = field: 0 b = POWER << 8 or FADDR (7 bits); 1 b = INTUSBE << 8 or CCU flags (bit 0 OTG reset released, bit 1 OTG bus clock on, bit 2 PHY reset released, bit 3 PHY clock on); 2 b = EP2 TXCSR; 3 b = EP2 RXCSR; 4 b = ISCR bits 4-19 (bit n of b = ISCR bit n + 4). Controller fields read 0 unless CCU flags bits 0 and 1 are both set. Compare a cold SD boot with a FEL load. EP2 data toggles and FIFOs were already cleared at every SET_CONFIGURATION (ClrDataTog + FlushFIFO on both endpoints). v0.2.23: every type 13 (TX abort) is followed by type 17 (a = POWER, b = EP2 TXCSR) and type 18 (a = bit 0 EP2 TX interrupt enabled, bit 1 ZLP pending; b = USB frame number, 11 bits). UnderRun (TXCSR bit 2) is cleared at every packet load, so TxPktRdy (bit 0) set = a packet waits and the host takes no IN data; TxPktRdy clear with UnderRun set = the host polls and the device has nothing loaded. The frame number advancing by the elapsed ms (mod 2048) between two entries means SOFs keep arriving. Device-mode TXCSR does not expose the data toggle. v0.2.25: the three PHY tuning writes (RES45 calibration 0x0C = 1, TX amplitude 0x20 = 0x14 over 5 bits, disconnect threshold 0x2A = 3 over 2 bits; values as Linux phy-sun4i-usb for suniv) go to PHYCTL 0x01C13404 as in Linux, not to 0x01C13410 as in 1.00o5. At the end of `usb_device_init` seven type 19 entries, a = field: 0 POWER << 8 or FADDR; 1 INTUSBE << 8 or CCU flags (as type 16); 2 ISCR bits 4-19; 3 USBPHY_CFG bits 0-15; 4 PHY tuning bits read back now; 5 the same read right after the PHY reset was released, before the writes; 6 PHYCTL bits 0-15. Tuning bits packed: bit 0 = PHY 0x0C, bits 1-5 = PHY 0x20-0x24, bits 6-7 = PHY 0x2A-0x2B; as written = 0x00E9. The read-back (address in PHYCTL bits 8-15, data at bit 16) is verified on the bench (2026-10-18, FEL load: field 5 = 0x0068 PHY defaults, field 4 = 0x00E9 as written) | diagnose path-dependent USB state and IN stalls |
| 44 | Protocol 1.6: FFT_BENCH (v0.2.27) | Cmd 0x0F02, main loop only (E_BUSY while a UI menu is modal), every bitstream, no cap bit; GET_INFO `proto_minor` = 6. Request: empty or u8 `reps` (0 = 3, at most 16, else E_BAD_ARG detail 0). Runs the on-scope FFT (`fx_fft.c`, Hann window) on the CH1 trace buffer as 8-bit codes minus 128: N = 1024, 2048, 4096, 16384 with the work buffers in DDR, then N = 1024, 2048, 4096 with them in on-chip SRAM A (0x1000-0x700C, borrowed; nothing else uses it at run time; since v0.2.31 the plan holds twiddles too, so only N = 1024, 2048 fit, at 0x1000-0x7008, and n is 6). The UI pass waits for the whole run (estimate about 0.15 s at 3 reps); timer-poll commands (PING, GET_STATUS) still answer. Response: u16 n (entries), u16 reps, u32 CP15 control register as read (bit 0 MMU, bit 2 D-cache, bit 12 I-cache), then n entries of 24 bytes: u16 N, u8 window (1 = Hann), u8 memory (0 DDR, 1 SRAM), u32 plan init us (window table), u32 run min us, u32 run max us (window + load + FFT + split step, over `reps`), u32 dB conversion min us (N/2 + 1 bins), u16 peak bin (bins 2..N/2), i16 peak level (centi-dB re 1 code RMS: a full-scale sine reads about 3907). Times from timer0 (1 ms ticks plus the 24 MHz down-counter, 1 us steps) with IRQs on, so they include the 1 ms timer poll. Diagnostic only; the MCP may expose it as a bench tool. |
| 45 | Protocol 1.7: spectrum view, READ_SPECTRUM (v0.2.28) | GET_INFO `proto_minor` = 7. **Display mode**: the scope has a spectrum view (main menu row "Spectrum analyzer", hidden in roll mode; firmware/README.md "Spectrum view"). It is a main-loop display mode, not a modal menu: every command keeps working, the UI keeps capturing (RUN/STOP, trigger, time base and channel settings as usual) and the trace area shows the spectrum of each new capture, host captures included. GET_STATUS byte 43 `display_mode` (was 0): 0 scope, 1 spectrum; since v0.2.32 also 2 waterfall, 3 density (sub-views of the same screen; READ_SPECTRUM serves the latest spectrum in all three); any poll context. **READ_SPECTRUM 0x020A** (main loop, every bitstream, no cap bit). Request (8 bytes, shorter is zero-extended): u8 channel (0/1, else E_BAD_ARG detail 0), u8 0, u16 first bin, u16 count (0 or more than fits = as many as fit, 33,264), u16 0. E_STATE detail 0 when the spectrum view is off or shows no spectrum of that channel (channel off, not selected, or no capture yet). Response: 32-byte header, then `count` i16 levels in centi-dBV (centi-dBA when the channel is in A mode), bin k at k x bin width; -32000 = empty bin. Header: u8 channel, u8 window (0 rect, 1 Hann, 2 flat-top), u8 log2 N, u8 flags (bit 0 levels in dBA; since v0.2.34 bit 1 = the FFT ran on the 16-bit record of the 0x1534 decimation filter, bits 2-3 = its order K, 0 off), u32 frame (spectra computed since boot; changes with every new capture or setting), u32 divider (the UI time base's 0x0D value; sample rate 200 MHz / (divider + 1) without the filter, 200 MHz / N with it, row 47), u32 sample rate Hz (rounded), u32 bin width mHz (fs / N_fft, truncated), i32 gain cdB (20 log10 volts per code, already applied: subtract it for dB re 1 code RMS), u16 bins in the spectrum (N/2 + 1), u16 first bin returned, u16 count returned, u16 ENBW milli-bins (1000 rect, 1500 Hann, 3770 flat-top; RBW = ENBW x bin width). Levels: dBV of a sine's RMS for a tone centred on a bin (fx_fft.c scaling, host-tested against numpy); volts per code from the calibration as in section 8.5 [U: T8], probe factor included. N is the largest power of two the record holds, capped by the view's FFT length (so 2048 at the 3000-sample default depth, 8192 at 15000 on AL3). The FFT input is the trace buffer minus the zero code (DC bin = the DC level). Known limit: a host capture made with a raw divider (SET_TIMEBASE mode 1) is labelled with the UI time base's rate. |
| 27 | ENTER_FEL (v0.1.1) | cap ENTER_FEL on every known bitstream (not on unknown ones). Magic 0x4C45465F, else E_BAD_ARG detail 0. The OK response is sent first; once it has left the FIFO (at most 200 ms) the scope shows "FEL mode", soft-disconnects USB for 300 ms, masks IRQs, disables the timer, power-key and INTC sources, puts the USB controller and PHY back in reset, turns the caches off (I-cache invalidated), selects high vectors, sets SVC mode with SP = 0x7C00 (the bootloader's FEL state) and jumps to 0xFFFF0020. Settings are not saved; leave FEL with a power cycle or `sunxi-fel exe` | iteration without the boot menu |
| 47 | Protocol 1.9: decimation filter (v0.2.30, FPGA 0x1534) | GET_INFO `proto_minor` = 9; `#VER` says `proto=1.9`. AL3 0x1534..0x153F adds caps bit 15 DECIM (0x8000) besides EXT_TRIGGER; on bitstreams without it nothing below is written to the FPGA and GET_DECIM / SET_DECIM answer E_UNSUPPORTED. FPGA interface (fpga/al3, fpga-gen3; bitstream sha256 83140510...b820c1): 0x53 mode (bits 1:0 K: 0 legacy 8-bit sample skipping, 1 boxcar = sum of N, 2 CIC2, 3 CIC3; bit 2 core-order swap; bit 3 / bit 4 capture the A / B cores on the falling write-clock edge, 5 ns later, default rising), 0x54 output shift S, 0x55 16-bit trigger level (0x17 is ignored while K >= 1), read 0x29 = {bit 7 16-bit layout, bit 4 neg B, bit 3 neg A, bit 2 swap, bits 1:0 K}, S. Fixed latency 12 write clocks (120 ns) from pin to memory for every K and N; a 16-bit sample is centred a further K (N - 1) 2.5 ns earlier; the ADC trigger is self-consistent (same trigger_addr convention as K = 0). N = 2 x (floor(D / 2) + 1), D = the 0x0D divider; output rate 200 MSa/s / N (equal to 200 / (D + 1) for the odd dividers of the time base table); stored sample = min(65535, y >> S), y = filter output, gain N^K; ADC code = sample x 2^S / N^K. The firmware picks S = max(0, bitlen(255 x N^K) - 16), falls back CIC3 -> CIC2 for N > 10334 (slower than 20 kSa/s) and CIC2 -> boxcar for N > 1,050,630 (exactness 255 x N^K < 2^48), and stays legacy (K = 0) at divider 0 (200 MSa/s) and for captures over 8190 samples. 16-bit layout (K >= 1): one sample per channel per ring word (low byte 0x20/0x22 = core A slot, high byte 0x21/0x23); pretrigger, total, trigger_addr and the auto time-out count words = samples (at most 8190 samples). The filter is the UI's acquisition **Filter** setting (off, boxcar, CIC2, CIC3; saved with the settings; shown only on 0x1534+; memory depth 9000+ hidden and reduced to 6000 while it is on); it applies to UI acquisitions and host ARM; calibration and auto setup always run legacy. A change of mode, shift or divider waits (K + 2) output periods before the next sampling reset (free-running filter; at most 9 ms at 500 Sa/s). It is part of settings_seq. **GET_DECIM 0x020C** (main loop), request empty; **SET_DECIM 0x020D**, request u8 filter (0..3, else E_BAD_ARG detail 0), u8 flags (bit 0 core-order swap, host only, not saved; bit 1 / bit 2 capture the A / B cores on the falling edge, hidden settings saved with the Filter word, no UI; bits 3-7 E_BAD_ARG detail 1), u16 0; the flags go into 0x53 with every mode, legacy included; filter >= 1 while the trigger total is over 8190: E_BAD_ARG detail 0 (SET_TRIGGER total first). Both answer 32 bytes: 0 u8 filter setting, 1 u8 flags (as in SET_DECIM), 2 u8 mode the next arm writes (0x29 byte 0 layout; 0 = legacy), 3 u8 its S, 4 u32 its N (0 legacy), 8 u64 gain N^K (1 legacy), 16 u32 divider, 20 u32 output rate Sa/s, 24 u8 mode written at the last arm (0xFF none yet), 25 u8 its S, 26 u8 + 27 u8 FPGA 0x29 read now, 28 u16 8190 (largest total with the filter), 30 u8 why the effective K is lower than requested (0 not lower, 1 filter off, 2 200 MSa/s, 3 total > 8190, 4 CIC3 -> CIC2, 5 CIC2 -> boxcar, 6 no filter in the bitstream), 31 u8 EXT latency shift (samples) the next arm applies with the current source. SET_TRIGGER total > 8190 while the filter is on: E_BAD_ARG detail 5. GET_TRIGGER offsets 20/22 (words) and GET_TIMEBASE rate / period follow the effective configuration. GET_CAPTURE_INFO grows to 76 bytes: 68 u8 the capture's mode (0x29 byte 0 layout, 0 = legacy), 69 u8 S, 70 u16 EXT latency shift, 72 u32 N; capture_start_word uses words of that capture. **EXT trigger with K >= 1**: trigger_addr is the last sample written before the edge, but the stored data lag it by 120 ns + K (N - 1) 2.5 ns; the firmware rounds that to the nearest output sample, shift = floor((240 + 5 K (N - 1) + 5 N) / (10 N)) (half-ns units, halves up; e.g. N 2 boxcar 12, N 20 CIC2 2, N 20 CIC3 3, 1 at slow rates), writes pretrigger - shift as the pretrigger words, and reports the shift at byte 70, so sample `pretrigger` of the capture is the edge to within half an output period (capture_start_word = trigger_addr - (pretrigger - shift)). The pad-to-trigger latency of the EXT input itself (20-25 ns, row 41) is not corrected. READ_TRACE: header byte 30 = the capture's mode byte, 31 = S (both 0 for legacy captures). Without flags bit 2 the samples are the 8-bit trace as before: for a 16-bit capture the converted codes round(sample x 2^S / N^K + (adc_a_comp + adc_b_comp) / 2), clamped 0..255, which is what the screen and the 1.00o5 measurements use. Flags bit 2 (WIDE) with a 16-bit capture returns u16 little-endian samples as the FPGA stored them (no compensation, no inversion: flags bit 1 does not apply, meta bit 0 still reports invert); E_STATE detail 3 if that channel's last readout was not 16-bit. WIDE on a legacy capture is ignored (8-bit samples, byte 30 = 0). READ_RAW layout 2: u16 little-endian per word, low byte core A (0x20/0x22), high byte core B (0x21/0x23), both cores of a channel required (as layout 1): the 16-bit samples of a capture, `count` words = samples. Swap bit (0x53 bit 2): 0 = core B is the earlier sample of each pair, the B-before-A order of fpga_read_sample_data and READ_TRACE (affects K >= 2 only) | owner / spectrum work: anti-aliased, higher-resolution samples at slow time bases |

### Firmware-internal

- USB FIFO RAM layout (v0.1.3). The F1C100s USB FIFO RAM is 2048 bytes, and the EP0 FIFO is fixed at bytes 0-63
  (MUSBMHDRC guide). The 1.00o5 CDC layout put EP2 OUT at 0 (on top of EP0), EP1 at 2048 and EP2 IN at 4096 (outside
  the RAM, aliasing onto 0): control requests landed in bulk IN data (seen: GET_LINE_CODING setup bytes inside a
  TEST_PATTERN response) and IN packets overwrote waiting requests (seen: E_BAD_CRC on correct requests). Now: EP0
  0-63, EP2 OUT 64-575 (single buffered), EP2 IN 576-1599 (2 x 512, double buffered), EP1 IN 1600-1663, checked at
  compile time. v0.1.1-0.1.2 still had EP2 OUT at 0. v0.1.6 default (size-aligned like the MSC layout): EP0 0-63,
  EP1 64-127, EP2 OUT 512-1023, EP2 IN 1024-2047 (`PC_FIFO_MAP=1` keeps the 0.1.3 map).
- RX packet read (v0.1.6): each OUT packet is read from the FIFO in one call into an aligned 512-byte buffer and
  then copied into the ring. Up to 0.1.5 a packet straddling the ring end was read in two parts, restarting 32-bit
  FIFO reads after 8/16-bit ones, which the MUSBMHDRC guide forbids ("all the transfers associated with one packet
  must be of the same width"): one corrupted or lost request per 2 KiB received (~4% in the bench census). The
  0.1.5 GET_BAD_FRAME diffs show the mechanism: every byte from the 2 KiB stream boundary on is the last 8-bit value
  replicated (constant) or the last 16-bit value replicated (2-byte alternation). v0.1.7: the ring copies in and out
  are two-segment memcpys in `pc_rx_ring.h`, tested at every ring phase by `host_tests/test_rx_ring.c`. The MSC
  layout (OUT at 0, IN at 1024) is unchanged 1.00o5 code.
- The TX pump is driven by the EP2 TX interrupt and, as a fallback, from every poll (main loop, BUSY polls and the 1 ms
  timer poll), so a missed interrupt costs at most 1 ms.
- RX sweep (v0.1.4): every poll checks the EP2 OUT FIFO; a packet still waiting 2 ms after it was first seen (its
  interrupt was lost) is read by the poll. 1.00o5's `irq_handler` clears INTC pending by writing 0 after reading it,
  which can discard a USB interrupt raised in between; the 1 ms timer interrupt makes that window frequent.
- Poll contexts (v0.1.2): MAIN (top of the main loop; everything), BUSY (touch-panel scans and UI spin loops; the safe
  set, E_BUSY for the rest once the UI is modal) and ISR (1 ms timer interrupt; the safe set PING, GET_INFO,
  GET_STATUS, GET_BATTERY, `#VER`, `#RESET`, plus error answers; everything else stays queued for MAIN, never
  E_BUSY). A guard flag keeps the ISR poll out of a running MAIN/BUSY poll and out of the REMOTE service.
- On USB bus reset, suspend or disconnect the transport flushes the EP2 FIFOs itself (v0.1.2): the 1.00o5 reset
  handler issues its flushes with EPIND = 0, so EP2 kept a stale frame across host sessions.

- ARM writes 0x0F = 0, then 0x1A = the configured mode (FORCE leaves it in auto), then the 0x01 reset with a
  bounded 0x05 wait (2,000,000 reads), then 0x0B / 0x0C (programmable variants) and 0x0E (all variants) in reset,
  then releases. At DONE the firmware writes 0x0F = 1, as `fpga_done_conversion` does in trigger mode, and
  latches 0x14.
- READ_RAW never relies on the FPGA read pointer wrapping: a read that crosses the ring end is split, and each
  part writes 0x1F again (with `read_lag`) followed by one dummy strobe.
- The UI's own acquisition is unchanged: the 1.00o5 `T + 7441` pointer shortcut and the unbounded 0x05 wait stay
  in the UI path (the wait now services USB every 65,536 polls). The modular formula and the bounded wait are
  used for host ARM and for the geometry the protocol reports (1.3: also for UI captures).
- Shared control hooks (v0.2.0, all compile away with PC_PROTOCOL=0): `fpga_do_conversion` ends with
  PC_UI_CAPTURE_START (a UI conversion started: the ring no longer holds the last capture, a host single is
  aborted, the capture's settings are latched); `scope_acquire_trace_data` reports the raw 0x14 word
  (PC_UI_CAPTURE_ADDR, which also reads 0x18) and the finished readout (PC_UI_CAPTURE_DONE, counted once per
  conversion: while the arm gate holds, 1.00o5 processes the same conversion again every pass), and its arm
  condition gains PC_UI_ARM_ALLOWED (false while a READ_RAW is held). The main loop runs the MAIN poll, then the
  indicator service, then the normal UI pass. Protocol drawing saves and restores the UI's drawing state. 0x1732 (EP4) is recognised by the protocol only; `fpgasettings.fw_FPGA` keeps the 1.00o5
  value so the UI behaves as before on every bitstream.
- There is one TX staging buffer. A deferred WAIT result is built when the buffer is free, so the plan's second
  64-byte buffer is not needed. The order of a STOP response and the aborted WAIT is: STOP first.
- `PC_FORCE_CDC=1` (default) sets the "USB CH340 mode" setting to CDC at boot, and that value is saved with the
  settings on power-off.
- Module names follow the brief: `pc_protocol.c` (the plan's `pc_proto.c`), `pc_cmds.c`, `pc_hal.h`,
  `pc_transport.c`, plus `pc_hal_scope.c` (target HAL and glue) and `pc_hooks.h`.
