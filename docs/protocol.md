# FNIRSI 1013D PC control protocol, version 1.0 (draft)

Status: design draft of 2026-10-18 (sections 1-13), since implemented. The firmware speaks protocol 1.15; every change
after the draft is recorded in section 14, rows 26-54, and wins where a table in sections 1-13 differs (the GET_INFO,
GET_STATUS and GET_CAL tables in section 8 show the 1.0 layout with notes on the later growth). Firmware plan:
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
| 0x1533 .. 0x153F | 2 AL3 family (this project's bitstreams, `fpga/al3`) | 8192 | 16382 | yes | yes; later minors add caps bits: 0x1533 EXT_TRIGGER (row 41), 0x1534 DECIM (row 47), 0x1535 FIR (row 49). 0x1536 and later decode as variant 2 with the 0x1535 caps; the segment and averaging commands of 0x1536 are not in this protocol yet (`pc_variant_from_version` in pc_cmds.c) |
| 0x1632 | 3 Atlan4 EF2 | 6144 (wraps at 0x17FF) | 12286 | yes | yes |
| 0x1732 | 4 Atlan4 EP4 | 6144 but no wrap logic [V zaklad.v EP4 diff] | 12286, captures may corrupt above 6143 [U] | yes | yes |
| other | 0 unknown | treated as stock, `caps` = 0 except PING/INFO/STATUS | | | |

1.00o5 does not recognise 0x1732 (it sets `fw_FPGA = 0`) [V `S100 fpga_control.c:204-206`]; v1 firmware adds
it. The owner's AL3 unit runs whichever bitstream is in its 25Q80; the host must not assume 0x1532.

AL3 minor versions (variant 2, decoded as `(v & 0xFFF0) == 0x1530`, v >= 0x1532): 0x1533 EXT trigger, 0x1534 decimation
filter, 0x1535 FIR, 0x1536 segmented capture and averaging, **0x1537** the same interface with two fixes and no new
command (fpga/al3/README.md "Changes" section 7): (1) 0x0B and the segment slot mask (0x57/0x59) written just before
0x01 = 0 now apply at every rate (0x1532..0x1536 kept the previous value when no write clock edge came between the write
and the release, i.e. at slow legacy rates); (2) the A/B core order within a word is fixed (on 0x1532..0x1536 a 0x0D or
0x53 write could swap it until the next such write, so caps bit 12 INTERLEAVE_UNVERIFIED and the 0x53 swap bit are only
meaningful from 0x1537 on). Feature bits for 0x1537 are those of 0x1536 (EXT_TRIGGER, DECIM, FIR, and SEQ where the
firmware has it); a firmware that knows 0x1536 runs 0x1537 unchanged.

**0x1538** (fpga/al3/README.md "Changes" section 8) has the same interface plus one optional read. Feature bits are as
for 0x1537 plus caps bit 18 ENC_CHECK (fw 0.2.62, protocol 1.19, row 58: the firmware reads 0x33 after every 200 MSa/s
capture and re-arms a bad one), and a firmware that knows 0x1536 runs it unchanged.
- On 0x1537 the A encode flop missed timing. At 200 MSa/s one A core sampled correctly, one clock late (together with
  B) or with a metastable clock, differently from arm to arm. On the owner's unit that was CH2 A.
- 0x1538 makes the encodes a stateless function of the write clock with timing met. At every rate and after any write,
  core B is the earlier sample of each word (encode convention 1, the 1.00o5 / `PC_INTERLEAVE_ORDER` 0 order, and
  0x1535's normal state).

Read 0x33 is the encode self-check, one byte:
- bit 0: sticky, a check failed since the last 0x01 = 1;
- bit 1: the last write-clock prediction was right;
- bits 3:2: 01 = convention 1;
- bits 7:4: the last pin check {CH2 A, CH2 B, CH1 A, CH1 B}.

A good capture reads 0xF6. fw 0.2.62 reads it after every 200 MSa/s capture and re-arms when bit 0 is set (row 58), and
checks bits 3:2 = 01 once at boot. Older AL3 bitstreams do not decode read 0x33 (fw 0.2.62 checks their full-rate
captures from the data instead).

Core order per bitstream:

| Bitstream | Core order |
|---|---|
| 0x1532..0x1536 | B earlier, except after a write that hit the race window |
| 0x1537 | chip 1 A earlier; chip 2 undefined. Do not calibrate on it. |
| 0x1538 | B earlier on both chips, always |

**0x1539** (fpga/al3/README.md "Changes" section 9) has the interface and feature bits of 0x1538; a firmware that knows
0x1538 runs it unchanged. It fixes the readout: 0x1532..0x1538 loaded each read byte on the rising edge of the MCU's bus
clock, and the MCU reads the port right after that edge, sooner than the 9.4..10.9 ns the byte needs from the clock pad
to the data pads, so bytes that differ from the previous one in several bits (0x7F / 0x80, 0x8F / 0x90, ...) could come
back mixed on the slow lanes (bench 2026-10-19, fw 0.2.67, 0x1538: outliers up to 0xFF near code 128). 0x1539 loads the
byte on the falling edge, the start of the strobe, so it is settled before the MCU raises the clock. Byte values, the
read pointer, the dummy strobe after 0x1F and the 1.00o5 one-word lag are unchanged; asynchronous status reads are
sampled at the start of the strobe instead of its end. Calibrations taken on 0x1537 / 0x1538 should be redone.

**0x153A** (fpga/al3/README.md "Changes" section 10) has the interface and feature bits of 0x1539 plus the ADC capture
registers; a firmware that knows 0x1539 runs it unchanged, but the data latency changed (below). 0x1532..0x1539 sampled
the ADC pins straight into the write-clock domain at an instant set by placement; each core's data is only settled for
T - 4 ns per encode period T (AD9288 tV 2 ns min, tPD 6 ns max), so every build garbled some core near codes where many
bits flip (bench 2026-10-19: 0x1538 CH2 A + B, 0x1539 both A cores at 128..145). 0x153A samples every pin in an IOB DDR
register on the 200 MHz clock and takes, per core, one of six 2.5 ns stages against the write clock edge the core is
encoded on (A: falling, B: rising), so the capture instant no longer depends on placement or on the sample rate, and a
word holds the B sample encoded T/2 before its A sample (B first) at every rate.
- **Write 0x63**, two bytes high first: bits 2:0 CH1 A stage, 5:3 CH1 B, 8:6 CH2 A, 11:9 CH2 B (stage k = the pins
  5 + 2.5 k ns before the core's edge; 3 and 4 are the allowed pair, 2 and 5 reserves), bit 12 / 13 = CH1 B / CH2 B
  timed by the chip's A encode (AD9288 data-align mode; 0 = by its own encode). Power-on 0x08E4 (CH1 A 4, CH1 B 4,
  CH2 A 3, CH2 B 4, own encode), the best stage per core from TD's numbers for the build. Write it before 0x01 = 1.
- **Write 0x64**, one byte: outlier threshold in codes, power-on 8.
- **Read 0x36**, 10 bytes: the 0x63 value (2), then 16-bit outlier counts of CH1 A, CH1 B, CH2 A, CH2 B, high byte
  first: words (from the third after the release of 0x01) whose sample differs from the core's previous word by more
  than the threshold, counted while the capture writes, cleared by 0x01 = 1. A quiet input at a good stage gives 0; a
  core captured inside its transition gives about every word (rail values 0 / 255 and jumps, the bench's symptom).
- **Self-check and training** (the calibration wizard's zero step at code 128, the worst code, is the natural place):
  with a quiet input, for each core try stages 3 and 4 (then 2 and 5 if neither is clean), 8k words at 200 MSa/s each,
  read 0x36; keep the stage with a zero count whose neighbours are clean too, else the clean one; if no stage of the
  pair is clean for a B core, try bit 12 / 13 set (data-align mode); write the result to 0x63 and store it with the
  calibration. Read 0x36 after normal full-rate captures to flag a bad core (non-zero on a quiet input).
- **Latency**: the legacy data reach the memory 4 words later than on 0x1539 at 200 MSa/s and 3 words later at
  divided rates (in time: 40 ns more at the full-rate write clock, so the K >= 1 EXT lag grows by 40 ns); EXT is not
  delayed. tb_trigpos (fpga/al3/tb) prints the offsets per case: legacy EXT at 200 MSa/s +14..15 samples (0x1538:
  +6..7), at D 9 +16..17, at D 199 +17..18; CH1 level +2..3 at every rate (unchanged: trigger and data move
  together). `pc_decim_ext_shift` and the legacy EXT table must follow.
- 0x53 bits 3 / 4 read back in 0x29 but select nothing; keep them 0. Reads 0x24..0x27 return the pin as the IOB
  register took it (liveness only). 0x33 is unchanged.

fw 0.2.79 (protocol 1.24, row 64) detects 0x153A..0x153F as caps bit 19 CAPTURE_STAGES, writes 0x64 = 8 and 0x63 (the
value stored with the calibration when it was trained on this bitstream, else 0x08E4) at boot, trains the stages in
the calibration wizard's zero step, reads 0x36 after every accepted full-rate capture, and uses the EXT shifts above.

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
| LOCAL_BUSY (sub-state) | UI is inside a modal screen, calibration, auto-setup or USB-MSC screen | UI | UI | PING, GET_INFO, GET_STATUS, GET_BATTERY only; others `E_BUSY` (1.23: others close the modal screens and run; E_BUSY only for a calibration measurement, a file save or USB drive mode, section 14 row 63) |
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

So the convention is "B samples before A within a word" (GET_INFO byte 35 `interleave_order` = 0). Since fw 0.2.62
the firmware fixes it at 0. FPGA 0x1538 guarantees it at every rate (encB = the RAM write clock, encA its complement,
B - A = -5 ns at 200 MSa/s; 0x33 bits 3:2 = 01 says so, checked at boot, row 58). On 0x1532..0x1536 a 0x0D or 0x53
write could swap the order until the next such write, and 0x1537 had chip 1 the other way round, so there the
convention held only by chance. Caps bit 12 INTERLEAVE_UNVERIFIED: since v0.2.72 (row 62) clear on 0x1538+ when read
0x33 at boot had bits 3:2 = 01 (the bitstream reports the order), set everywhere else (bench test T4, section 12, is
the check on hardware). Layout 0 (per-core blocks) is unaffected.

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
- READ_TRACE kept the 1.00o5 behaviour (no dummy read, one word late) up to v0.2.69. Since protocol 1.22 (v0.2.72,
  row 62) every readout, the UI's included, makes the dummy strobe, so READ_TRACE and the screen hold the words READ_RAW
  returns (fpga/al3/README.md section 7 finding 5 showed the lag in simulation on every AL3 bitstream; 0x1539 keeps it).

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
  timeout]. **Corrected in v0.2.41** (bench: at 10 MS/s with a 1 kHz square, 2 of 6 auto captures came back flat but
  flagged triggered): the time-out counts samples, so at fast rates it expires long before a slow signal's next edge,
  and 0x18 keeps watching after sampling stopped (zaklad.v: trigger detection is not gated by the stop), so an edge
  between the stop and the read sets it. Since v0.2.41 auto captures on Atlan bitstreams (host ARM with UI mode 0, and
  the UI's own acquisition) arm in normal mode, and the firmware gives up after max(100 ms, two record lengths, an
  explicit `auto_timeout` converted to time): it writes 0x1A = 0, the FPGA stops at once, and the capture is reported
  untriggered (`auto_timeout` set, GET_CAPTURE_INFO flags triggered clear) whatever 0x18 says afterwards. Stock
  bitstreams, calibration and auto setup keep the FPGA's own auto mode. The `auto_timeout` value is still written to
  0x0E (it no longer acts in normal mode).
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
| 0x0007 | GET_LINK_STATS | yes | - | no | link counters (1.2, section 14 row 28; the count n grew to 62 by fw 0.2.49, rows 38, 50 and 53; 68 in 1.22, 70 in 1.23, rows 62 and 63; `h_get_link_stats` in pc_cmds.c) |
| 0x0008 | GET_BAD_FRAME | yes | - | no | last request with a bad payload CRC (1.2, row 29) |
| 0x0009 | GET_EVENT_LOG | yes | - | no | last 64 USB / link events with timestamps (1.5, row 42) |
| 0x000A | GET_HEALTH | (1.16) | - | no | USB state now, CP15 word, power-on count, operating minutes, settings save state, stored restart record (row 55); full-rate capture check (1.19, row 58); FPGA 0x153A capture stages (1.24, row 64) |
| 0x0100 | GET_CHANNEL | yes | - | no | channel config |
| 0x0101 | SET_CHANNEL | no | - | no | enable, range, coupling, offset, invert |
| 0x0102 | GET_TIMEBASE | yes | - | no | divider, rate |
| 0x0103 | SET_TIMEBASE | no | - | no | UI index or raw divider |
| 0x0104 | GET_TRIGGER | yes | - | no | trigger and memory geometry |
| 0x0105 | SET_TRIGGER | no | - | no | source, edge, level, mode, pre-trigger, total, hold-off |
| 0x0200 | ARM | no | - | no | start a capture |
| 0x0201 | FORCE | no | FORCE_TRIGGER | no | end a waiting capture now (debug builds only, row 18) |
| 0x0202 | STOP | no | - | no | abort capture and pending requests |
| 0x0203 | WAIT | no | - | yes | wait until DONE or timeout |
| 0x0204 | READ_RAW | no | - | no | raw ring bytes per core |
| 0x0205 | READ_TRACE | yes | - | no | processed trace as the UI shows it |
| 0x0206 | READ_LIVE | no | LIVE_READ | no | live ADC bus bytes (no memory); not built, E_UNSUPPORTED (row 18) |
| 0x0207 | ACQUIRE_AVG | no | DEVICE_AVERAGE | yes | n captures averaged on the device; not built, E_UNSUPPORTED (row 18) |
| 0x0208 | SET_ACQ_STATE | (1.3) | SHARED_MODE | no | UI RUN / STOP (1.3, section 14 row 34) |
| 0x0209 | GET_CAPTURE_INFO | (1.3) | SHARED_MODE | no | settings of the latest finished capture (1.3, row 37) |
| 0x020A | READ_SPECTRUM | (1.7) | - | no | the spectrum the scope displays (section 14 row 45) |
| 0x020C | GET_DECIM | (1.9) | DECIM | no | decimation filter setting and state (section 14 row 47) |
| 0x020D | SET_DECIM | (1.9) | DECIM | no | decimation filter setting, the UI's acquisition Filter (row 47; FIR flag 1.11, row 49) |
| 0x020E | SET_SEQ | (1.17) | SEQ | no | sequencer setting for host ARM: segments or hardware averaging (row 56) |
| 0x020F | GET_SEQ | (1.17) | SEQ | no | sequencer setting, fit, FPGA status, latest run (row 56) |
| 0x0210 | READ_SEQ | (1.17) | SEQ | no | segment table or averaged record of the latest run (row 56) |
| 0x0211 | BODE_CTRL | (1.20) | DECIM | no | on-device Bode sweep: settings, run / stop, thru calibration, the view (row 60) |
| 0x0212 | READ_BODE | (1.20) | DECIM | no | the Bode table of the latest sweep (row 60) |
| 0x0500 | SD_INFO | (1.3) | SD_SECTORS | no | card, partition, write window, eGON headers (section 14 row 40) |
| 0x0501 | SD_READ_SECTORS | (1.3) | SD_SECTORS | no | up to 128 sectors |
| 0x0502 | SD_STAGE | (1.3) | SD_SECTORS | no | bytes into the 64 KiB write staging buffer |
| 0x0503 | SD_WRITE_SECTORS | (1.3) | SD_SECTORS | no | staged sectors to the card, verified |
| 0x0504 | SAVE_REPORT | (1.16) | SD_SECTORS | no | write the diagnostics report text file to the card (row 55) |
| 0x0300 | GET_GEN | yes | - | no | generator state |
| 0x0301 | SET_GEN | no | GENERATOR_DDS | no | enable, frequency, duty |
| 0x0400 | GET_CAL | yes | - | no | calibration constants |
| 0x0401 | READ_SCREEN | yes | SCREENSHOT | no | framebuffer rows; built in fw 0.2.70 (cap set on every known bitstream; before: E_UNSUPPORTED, row 18) |
| 0x0402 | CAL_PROBE | (1.21) | - | no | the calibration wizard's zero measurement of one channel and range, nothing stored (section 14 row 61, provisional) |
| 0x0F00 | DEBUG_FPGA | no | DEBUG_RAW | no | raw FPGA bus operations (debug builds only) |
| 0x0F01 | ENTER_FEL | no | ENTER_FEL | no | reboot into the boot ROM's FEL mode |
| 0x0F02 | FFT_BENCH | (1.6) | - | no | time the on-scope FFT (section 14 row 44) |

Unknown cmd: `E_UNKNOWN_CMD`. LOCAL-only restriction violated: `E_NOT_REMOTE`. UI modal: `E_BUSY` before 1.23; since
1.23 the modal screens close by their cancel paths and the request runs, with `E_BUSY` detail 2-5 only where the UI
cannot yield (section 14 row 63).

### 8.2 System commands

**PING (0x0001).** Request: 0..492 arbitrary bytes. Response: the same bytes.

**GET_INFO (0x0002).** Request: empty. Response (76 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | proto_major | 1 |
| 1 | u8 | proto_minor | 0 in the draft; 15 as of fw 0.2.50 (section 14 rows 26-54, one row per minor) |
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
| 30 | u16 | min_total_samples | 80 with PROG_TRIGGER_POS, else equal to max_total (3000 on the stock bitstream) [V pc_cmds.c GET_INFO handler; Donwulff: "minimum total 80"] |
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
| 3 | EXT_TRIGGER | trigger source 2 exists: AL3 bitstreams 0x1533 and later (section 14 row 41) |
| 4 | TRIG_BOTH_CORES | trigger looks at A and B (future FPGA work) |
| 5 | FORCE_TRIGGER | FORCE has been verified on this variant (test T7) |
| 6 | SCREENSHOT | READ_SCREEN built in |
| 7 | DEVICE_AVERAGE | ACQUIRE_AVG built in |
| 8 | DEBUG_RAW | DEBUG_FPGA built in (debug builds only) |
| 9 | LIVE_READ | READ_LIVE built in |
| 10 | ENTER_FEL | ENTER_FEL built in |
| 11 | READ_LAG_UNVERIFIED | `read_lag` is a default, not a measured value |
| 12 | INTERLEAVE_UNVERIFIED | `interleave_order` is the 1.00o5 convention, not measured |
| 13 | SHARED_MODE | collaborative mode, no LOCAL/REMOTE (1.3, section 14 row 30) |
| 14 | SD_SECTORS | SD_INFO / SD_READ_SECTORS / SD_STAGE / SD_WRITE_SECTORS (fw 0.2.2) |
| 15 | DECIM | decimation filter, 16-bit samples: AL3 0x1534 and later (1.9, row 47) |
| 16 | FIR | CIC compensation FIR: AL3 0x1535 and later (1.11, row 49) |
| 17 | SEQ | sequencer: segmented capture and hardware averaging, AL3 0x1536 and later (1.17, row 56) |
| 18 | ENC_CHECK | the FPGA checks the ADC encodes of every capture (read 0x33) and guarantees the core order (B first): AL3 0x1538 and later (1.19, row 58) |
| 19 | CAPTURE_STAGES | per-core ADC capture stage (write 0x63) and outlier counters (read 0x36), trained by the calibration wizard: AL3 0x153A and later (1.24, row 64) |
| 20-31 | reserved | 0 |

**GET_STATUS (0x0003).** Request: empty. Response (28 bytes in the 1.0 draft, 32 bytes in 1.1, 44 bytes since 1.3, 48 bytes since 1.17).
Also the response body of SET_REMOTE, ARM, FORCE, STOP and WAIT. The table is the 1.0 layout; the appended fields
(`pass_ms` at 28, `settings_seq` at 32, `run_state` at 40, `capture_origin` at 41, `ext_flags` at 42 and
`display_mode` at 43, values 0 scope, 1 spectrum, 2 waterfall, 3 density, 4 phosphor, 5 Frequency response (the on-device Bode plot;
1.20, row 60; the screen's name since fw 0.2.69);
the features word, UI Average state and log2 N at 44..47) are in section 14 rows 26, 32, 41, 45, 52 and 56;
features bit 5 (0x0020) ENC_CHECK in row 58, bit 6 (0x0040) BODE in row 60, bit 7 (0x0080) CAPTURE_STAGES in row 64.

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | control | 0 LOCAL, 1 REMOTE |
| 1 | u8 | acq_state | 0 IDLE, 1 ARMED, 2 TRIGGERED, 3 DONE, 4 ABORTED (REMOTE only; 0 in LOCAL) |
| 2 | u8 | flags | bit0 fpga_triggered (0x18), bit1 fpga_done (0x0A), bit2 auto_timeout, bit3 dtr, bit4 charging, bit5 deferred_pending, bit6 overload_ch1, bit7 overload_ch2 (last readout) |
| 3 | u8 | ui_busy | 1 while the UI is modal (LOCAL_BUSY; 1.23: a request for the main loop closes it, row 63) |
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
- Since fw 0.2.64 (wire format unchanged): the UI keeps its time/div viable for the rate and the record (the record
  spans the 724 px trace area, at least one sample per division; `firmware/scope/tb_view.h`). Mode 0 sets the index's
  rate and, when the index does not fit the current record length (SET_TRIGGER total), the nearest index that does;
  the response's `timeperdiv_index` is the applied one. SET_TRIGGER pretrigger / total refit the UI time/div the same
  way. The acquisition Filter (GET_DECIM / SET_DECIM filter and FIR bits) is kept per display mode: Scope and Spectrum
  each have their own (defaults CIC2 and CIC2+FIR); the commands read and write the mode on screen.
- Since fw 0.2.76 (wire format unchanged): the screen's tap zoom and ACQ time/div boxes pick the rate from a ladder
  (the fastest rate whose record covers the trace area with at least ten samples per division; docs/ui-guidelines.md
  section 2). Mode 0 does not: it already picks a rate for every index, the table above (100 samples per division
  down to 500 ns, viable for any record of 1450 samples or more), and keeps it so hosts and the simulator see the same
  rate for an index. A host that wants the screen's pair reads it back with GET_TIMEBASE after a zoom.

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

**GET_CAL (0x0400).** Request: u8 `channel`. Response (364 bytes since 1.24, row 64; 356 in 1.16-1.23, 288 in 1.15, 256 in 1.12-1.13, 248 in 1.10-1.11, 72 before), copied from the channel settings loaded
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
| 72..255 | | 1.12: calibration data v3 and the correction for the current settings (section 14 row 51; 1.10-1.11: bytes 72..247, v2, row 48). 1.15: bytes 84 and 92..107 include the calibration v4 tracking below |
| 256..287 | | 1.15: calibration v4, background interleave tracking (A - B) and the full-class common-mode probe (section 14 row 54) |
| 288..355 | | 1.16: calibration provenance (sector 708 bytes 432..487) and the power-ons / operating minutes since (section 14 row 55) |
| 356..363 | | 1.24: the FPGA 0x153A capture stages stored with the calibration (sector 708 bytes 488..503) and the 0x63 value in effect (section 14 row 64) |

There is no SET_CAL in v1 [D]: calibration stays a UI procedure so a host bug cannot corrupt sector 708.

**READ_SCREEN (0x0401).** Cap SCREENSHOT. Request: u16 `row`, u16 `nrows`. Response: u16 `width`, u16 `row`,
u16 `nrows`, u16 `format` (1 = RGB565 little-endian [U]), then `nrows x width x 2` bytes from the
framebuffer `maindisplaybuffer` [V `S100 fnirsi_1013d_scope.c:81`]. 40 rows (64,000 bytes) fit one
response; a full screen is 12 requests, 768,000 bytes.
Built in fw 0.2.70 (`h_read_screen`, `pc_hal_screen_rows`): `maindisplaybuffer` is the buffer DEBE layer 0
scans (views drawn in `displaybuffertmp` are copied into it), so the rows are what the panel shows. Main loop only:
while a modal menu is open the request got E_BUSY before 1.23; since 1.23 the menu closes first (row 63). Errors (as the simulator): E_BAD_ARG detail 0 for `row` >= 480,
`nrows` 0 or `row + nrows` > 480; E_OVERFLOW detail 2 above 41 rows (8 + 42 x 1600 > 66,560). Vectors:
`req_read_screen`, `resp_read_screen_last_row`, `resp_read_screen_bad_arg`, `host_req_read_screen_tail`,
`host_resp_read_screen_e_overflow`.

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
| 7 | E_BUSY | a deferred request is already pending (detail 0), or the UI is modal (before 1.23, detail 0); since 1.23 detail 2 the UI did not reach its main screen in 1.5 s, 3 a calibration measurement runs, 4 a file save runs, 5 USB drive mode (section 14 row 63); 1 READ_RAW waited for the UI capture in vain |
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
| T9 | RGB565 framebuffer format | READ_SCREEN (fw 0.2.70) vs a photo of the screen: `scope_screenshot` |
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
| 18 | Not built in v0.1.1 | READ_LIVE, ACQUIRE_AVG, READ_SCREEN: caps 0, E_UNSUPPORTED (READ_SCREEN built in fw 0.2.70, cap SCREENSHOT). DEBUG_FPGA and FORCE exist only in `PC_DEBUG=1` builds, which set DEBUG_RAW and FORCE_TRIGGER (FORCE before test T7) | phase 2/3 of the plan; fail closed |
| 19 | USB identity | VID:PID 0x1209:0x0001 (pid.codes test PID, development only), bcdDevice 0x0100, manufacturer `fnirsi-1013d-ai`, product `FNIRSI 1013D`, serial `1013D-0001` on every unit (GET_INFO `serial` is the same string) | per-unit serial and a real PID are plan phase 3 |
| 20 | `min_total_samples` | 80 on variants with PROG_TRIGGER_POS (not checked against the FPGA; the 1.00o5 UI never goes below 3000); 3000 (= max) on the stock bitstream | stock geometry is fixed [U] |
| 21 | `#LOCAL` | 1.2: releases with the acquire-time KEEP_SETTINGS flag; ASCII lines count as traffic for the watchdog. 1.3: `#OK`, no effect | |
| 22 | Retransmitted deferred request | a WAIT with the same cmd and seq as the pending one is ignored; the pending answer follows (no E_BUSY) | host retry after a read timeout |
| 23 | TX stall (v0.1.1) | a response that makes no progress for 2 s (no IN tokens taken) is dropped and the endpoint FIFO flushed; 3.2 said it stays pending | bench 2026-10-18: a stuck frame blocked the device until power-off |
| 24 | New host connection (v0.1.1) | DTR 0 -> 1 also flushes a frame still in flight, so a reopened port never receives the tail of an old response. The device parser drops partial input of the old connection, but since v0.2.11 never a complete request waiting for the main loop (0.1.1-0.2.10 dropped such a request unanswered when it arrived right after the port was opened: bench, first TEST_PATTERN after a FEL load timed out); link events are taken in every poll context, before new bytes are parsed | bench: a fresh connection read the rest of a 64 KiB pattern |
| 25 | `#RESET` (v0.1.1) | ASCII line, honoured even while a response is being sent: drops the frame in flight, a pending WAIT (no answer), the duplicate cache and partial input, then replies `#OK`. Settings and acquisition state stay. 1.3: a READ_RAW held for a UI capture (row 35) is answered first, so `#RESET` is seen at most 1 s later | bench recovery without a power cycle |
| 26 | Protocol 1.1 (v0.1.1) | GET_INFO `proto_minor` = 1, `#VER` says `proto=1.1`. GET_STATUS appends u16 `pass_ms` (offset 28: interval between the last two main-loop polls) and u16 `pass_max_ms` (offset 30: maximum since the previous GET_STATUS); the body is 32 bytes | measure the main-loop period the round trip depends on |
| 28 | GET_LINK_STATS 0x0007 (protocol 1.2, v0.1.4) | any state and poll context. Response: u16 n (27), u16 0, n x u32 counters since boot: rx_packets, rx_bytes, rx_stalls, rx_swept (packets the poll sweep took because their interrupt never came), tx_packets, tx_zlps, tx_kicked (packets loaded by the poll pump), tx_flushes, link_resets (bus reset/disconnect; suspend too up to v0.2.43, row 50), dtr_up, dtr_down; parser: frames, hdr_crc_errors, payload_crc_errors, oversize, version_errors, responses_dropped, rx_timeouts, junk_bytes; session: responses, duplicates, tx_aborts, disp_main, disp_busy, disp_isr, held (polls that left a request for the main loop), isr_skipped | bench diagnosis of request loss |
| 29 | GET_BAD_FRAME 0x0008 (protocol 1.2, v0.1.5) | any state and poll context. Response: u32 bad_count, u32 rx_bytes (transport byte count when the bad request completed), u16 len, u16 0, then the last request that failed its payload CRC exactly as received (header + payload + CRC, len bytes). The host diffs it against what it sent | diagnosis of request corruption (fw 0.1.3 census: ~4% E_BAD_CRC) |
| 30 | Protocol 1.3: shared control (v0.2.0) | GET_INFO `proto_minor` = 3, caps bit 13 SHARED_MODE (0x2000) on every known bitstream, `#VER` says `proto=1.3`. No LOCAL/REMOTE, no lock, no banner, no watchdog: the UI loop always runs and every command is accepted at any time, except E_BUSY while a UI menu is modal (row 9). E_NOT_REMOTE is never returned. SET_REMOTE (any action, flags, watchdog) answers OK with the status body and changes nothing; `#LOCAL` answers `#OK` | `docs/plans/collaborative-mode.md`: the owner and the agent use the scope at the same time |
| 31 | Settings (1.3) | host SET_* write the live UI settings through the UI's own apply code and redraw what changed (channel button, time base, trigger, generator, RUN chip); the UI's changes show in GET_* at once. Last writer wins. Host changes persist and are saved at power-off like the user's. SET_TIMEBASE mode 0 applies the target index in one step (index, its sample rate, short mode, FPGA 0x0E/0x0D, then one redraw of the time base label and the trace area; v0.2.3, after the owner saw flicker with v0.2.0-0.2.2, which ran `scope_set_timebase` and in UI single trigger mode also re-armed the UI single). The trigger level is kept the UI way, as the marker position (0..401) the UI derives the level from before every conversion: SET_TRIGGER level places the marker at the nearest position, so the read-back can differ by one code, and the level then follows range and offset changes as on the screen. A raw divider (SET_TIMEBASE mode 1) and raw DDS words (SET_GEN) stay until the UI changes its time base / generator settings; SET_GEN also stores the rounded Hz and % in the UI settings | one live settings set |
| 32 | GET_STATUS (1.3) | 44 bytes. offset 0 `control` = 2 (SHARED). 1 `acq_state`: the host single capture only. 2 flags: bits 0-2 the host single (triggered, done, auto_timeout), others as before. 3 `ui_busy`: 0 idle, 1 a menu is modal, 2 STARTING (USB is up but the main loop has not finished its first pass; see row 38). 4 `capture_id`: every DONE capture of any origin. 8 `trigger_addr` / 10 `capture_start` of that latest capture. 20 watchdog 0xFFFF, 23 exit_reason 0. Appended: 32 u32 `settings_seq`, 36 u32 `capture_settings_seq`, 40 u8 `run_state` (0 STOP, 1 RUN, 2 SINGLE: a host ARM or a UI single is waiting), 41 u8 `capture_origin` (0 host ARM, 1 UI run, 2 UI single), 42 u16 0 (**superseded**: since 1.4 byte 42 is `ext_flags` and since 1.7 byte 43 is `display_mode`, rows 41 and 45; bytes 42-43 read 0 only without EXT_TRIGGER and while the scope view shows). The same body answers SET_REMOTE, ARM, STOP, WAIT and SET_ACQ_STATE | the host sees the shared state in one read |
| 33 | `settings_seq` (1.3) | +1 (wraps, never 0xFFFFFFFF) whenever a capture-relevant setting changes, from the UI or the host: per channel enable, range (samplevoltperdiv), coupling, offset, invert, probe; time base index, sample rate, host raw divider, long time base mode; trigger source, edge, mode, level (only while the UI's "always 50 %" is off, since it rewrites the level after every capture; the flag counts instead), pretrigger, total. Detected by comparing a fingerprint at every poll, UI arm and UI DONE. `capture_settings_seq` = settings_seq when the latest capture was armed if nothing changed until its DONE, else 0xFFFFFFFF (also GET_CAPTURE_INFO flags bit 4); 0xFFFFFFFF too before the first capture. The WAIT answer of a host single carries that capture's own value (capture_origin 0), so a host that sees it equal to its run's settings_seq needs no GET_CAPTURE_INFO | long host runs tag every capture with its settings; no lock |
| 34 | ARM, STOP, SET_ACQ_STATE (1.3) | ARM is a host single capture: it stops the UI acquisition (run_state STOP, the screen shows STOP), then runs the 1.2 ARM sequence (rows 7, firmware-internal ARM), so geometry, auto_timeout, WAIT, FORCE and DONE are as in 1.2. At DONE the capture is read into the trace buffers for the enabled channels and the UI shows it; the UI stays in STOP. The host single is aborted (acq_state ABORTED, a pending WAIT answers E_ABORTED) when the UI arms a conversion (RUN on the screen, or a time base tap in single mode) or on SET_ACQ_STATE. Since fw 0.2.68 the screen shows a host single in progress (armed or triggered, or done less than 1 s ago) as RUN, and its RUN/STOP button reads STOP: a tap aborts the host single the same way and the UI stays in STOP (wire format unchanged). ARM in the UI long time base mode: E_STATE detail 2. STOP aborts a host single and stops the UI. New SET_ACQ_STATE 0x0208: request u8 state (0 STOP, 1 RUN) + 3 reserved; does what the RUN/STOP button does (RUN while the UI trigger mode is single is the UI single "WAIT", run_state 2); E_BAD_ARG detail 0 for other values; allowed while a WAIT is pending | the screen shows the host's capture; RUN resumes from either side |
| 35a | READ_TRACE `count` (v0.2.1) | `start` and `count` are samples per channel, checked against the buffer's `sample_count` (header offset 4; the capture's length, which can differ from GET_TRIGGER `total` after a SET_TRIGGER while stopped or before the next UI capture). count 0 or a count past the end is clipped to sample_count - start; start > sample_count: E_BAD_ARG detail 4. Up to 0.2.0 a count past the end was E_BAD_ARG detail 8 "count" | owner's scope_measure asked for the GET_TRIGGER total |
| 35 | READ_RAW (1.3) | no DONE requirement: returns the latest finished capture of any origin (header `capture_id`), while the FPGA ring still holds it (no arm since its DONE). In RUN the UI usually has the next capture armed: the request is then held for at most 1000 ms until that capture finishes, and the UI is kept from re-arming until the request is answered, so it returns the capture that finished next. Meanwhile other requests wait (one request at a time). E_BUSY detail 1 if nothing finished within 1000 ms (normal mode, no trigger). E_STATE detail 0: no capture yet or a host single still armed; detail 1: the ring was overwritten and the UI is stopped (STOP while a UI capture was armed); detail 2: UI long time base mode. flags bit 0 (ANY) reads the ring as it is, without these checks. A READ_RAW split into several requests can see different `capture_id`s in RUN (a new UI capture between chunks): the host compares them, retries, and as a last resort does SET_ACQ_STATE STOP, reads, then RUN | the UI loop rewrites the ring; reading during a conversion would mix two captures |
| 36 | READ_TRACE (1.3) | never waits: the trace buffer of the latest capture (host single: built from the ring at DONE, rebuilt on REREAD while the ring holds it; UI: the UI's own buffer and geometry). Byte 29 `origin` uses row 32's numbering (1.2: 0 remote, 1 UI, so both keep their meaning). Channel metadata are the front end settings the capture was armed with. A UI time base change clears the UI trace buffer (1.00o5) | |
| 37 | GET_CAPTURE_INFO 0x0209 (1.3) | any poll context. Request empty. Response 68 bytes, the settings the latest finished capture was taken with: 0 u32 capture_id, 4 u32 capture_settings_seq, 8 u8 origin, 9 u8 flags (bit 0 triggered, bit 1 auto timeout (0x18 not set at DONE, also for UI captures, test T6), bit 2/3 overload CH1/CH2, bit 4 settings changed while it ran), 10 u16 trigger_addr, 12 u16 capture_start_word ((trigger_addr - pretrigger_words) mod ring_words for every origin), 14 u16 0, (1.4: flags bit 5 = EXT edge seen during the capture) 16 u32 divider, 20 u8 timeperdiv index (0xFF raw divider), 21 u8 trigger source, 22 edge, 23 level, 24 mode (protocol numbering), 25-27 0, 28 u32 pretrigger samples, 32 u32 total samples, 36 + 16 x ch (CH1, CH2): u8 enable, range, coupling, invert, probe_index, 0, u16 offset_pwm, u32 uv_per_lsb_q8, i32 zero_code_q8. E_STATE detail 0 before the first capture | read_capture without ARM; settings per capture |
| 38 | Startup (v0.2.0) | GET_LINK_STATS payload: u16 n, u16 0, then n u32 counters (decode by n; word 0 is the count, not a counter). n per firmware: 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3), 35 (0.2.4), 41 (0.2.5-0.2.6), 47 (0.2.7-0.2.11), 48 (0.2.12-0.2.16: 47 = USB interrupts served by the timer poll, i.e. lost IRQs; retired in 0.2.21, always 0 since), 49 (0.2.17-0.2.18: 48 = ms spent on the boot splash step + 1), 58 (0.2.20: 57 = USB IRQ races: ticks at which the 0.2.14-0.2.19 fallback gate (two ticks without a USB interrupt) would have served while the real USB interrupt was pending in the same dispatch, i.e. what that fallback counted as lost but was not; retired in 0.2.21, always 0 since), 57 (0.2.19: 49-56 = restart record: restarts since the last cold boot or ENTER_FEL, exception type (0 = jump to the image entry, 1 undefined instruction, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved vector, 6 FIQ), exception LR (fault PC = LR - 8 for a data abort, LR - 4 for undefined / prefetch), its SPSR, its SP, then LR, SP and CPSR found at _reset; event type 15 RESTART is logged at boot after a restart: a = exception type, b = low 16 bits of the exception LR or of the reset LR). Since 0.2.19 the exception vectors record and restart instead of hanging. Since 0.2.21 n stays 58 and slots 47 and 57 are retired: always 0, kept so decoders by count keep working (row 42). Since 0.2.48 n = 60: 58 = fault status of the abort recorded in 49-56 (DFSR for a data abort, IFSR for a prefetch abort, else 0; ARMv5 FS bits 3:0, domain 7:4), 59 = fault address (FAR, data abort only, else 0), read by the exception vector before the restart (no protocol minor change; decode by n). Since 0.2.49 n = 62: 60 = USB resumes inferred without a RESUME interrupt, 61 = USB soft reconnects (row 53). Counter index, 0-based: 0-26 as row 28; 27 ms from session init to the first main-loop poll; 28 duration of the first main-loop pass; then times from the first main-loop poll, as ms + 1 (0 = not reached; v0.2.2-0.2.3 used ms OR 1): 29 first UI conversion started (after its 0x05 wait), 30 first UI capture processed, and (v0.2.4) within the first pass: 31 after the battery check, 32 after the acquisition step, 33 after the touch handler, 34 after the RTC block (end of the pass). v0.2.5 appends 6 USB device event counters (41 in all): 35 bus resets, 36 suspends, 37 resumes, 38 disconnects, 39 EP0 setup packets, 40 EP0 packets that were not 8 bytes (stalled). v0.2.7 appends 6 RX back-pressure counters (47 in all): 41 stalls entered from the RX interrupt (ring full), 42 releases (main loop made room), 43 re-stalls inside a release, 44 releases with no packet waiting, 45 largest RXCOUNT seen, 46 packets with RXCOUNT > 512. Bench 0.2.3 (SD persist): the first one-word 0.2.4 boot reading put 1515 ms in counter 27 (init to first poll; the splash `load_picture("scope.bmp")` is the prime suspect) and 31 ms in 28. Bench 0.2.1: 3 ms to the first poll, first pass 1516 ms. GET_STATUS `ui_busy` = 2 until the second main-loop poll. Bench 2026-10-18: after a FEL load GET_STATUS showed no completed pass for ~1 s and the first main-loop command waited ~5 s; the first pass is inside the 1.00o5 UI loop (first conversion's 0x05 wait or touch scan suspected, not verified) | hosts back off instead of timing out |
| 39 | MCP indicator (v0.2.3) | a steady green chip "MCP", 38 x 17 px at x 127, y 3, stacked directly above the RUN/STOP chip (same width), while a host is active (DTR up, or a frame within the last 2 s); no per-command pulse (owner decision). It uses the slot of the optional RTC clock, which is not drawn meanwhile and comes back within 0.5 s after the chip goes. Redrawn every 100 ms while on. v0.2.0-0.2.2: x 697, y 22 over the version text, with a pulse | plan: small indicator next to RUN/STOP, no banner |
| 40 | SD sectors (1.3, v0.2.2) | cap bit 14 SD_SECTORS (0x4000) on every known bitstream. Main loop only; the UI pass waits meanwhile (a 64 KiB write takes on the order of 100 ms [U]); PING etc. are still answered from the timer interrupt. **SD_INFO 0x0500**: request u8 flags (bit 0 VERIFY), 3 reserved. Response 64 bytes: 0 u32 card_sectors, 4 u32 part_start (LBA of MBR entry 0; 0 = no valid MBR), 8 u32 write_end (writes allowed in [16, write_end): min(part_start, 2048); 0 = writes off), 12 u32 stage_bytes (65536), then 3 x 16 bytes for sectors 16, 80, 750: u32 sector, u8 magic (0 none, 1 eGON.BT0, 2 eGON.EXE), u8 check (0 not checked, 1 checksum ok, 2 bad, 3 length invalid or over 2048 sectors), u16 0, u32 length, u32 stored checksum. VERIFY recomputes the eGON checksum over `length` bytes on the card (mksunxi.py rule). E_IO detail 0 without a card. **SD_READ_SECTORS 0x0501**: u32 sector, u16 count 1..128, u16 0. Response: u32 sector, u16 count, u16 0, u32 crc32 of the data, u32 0, then count x 512 bytes. E_BAD_ARG detail 4 count, detail 0 beyond the card. **SD_STAGE 0x0502** (answered from the 1 ms timer poll since v0.2.7, so staging needs no main-loop pass per piece): u32 offset, u16 length (= payload - 8, at most 484), u16 0, data. Response 12 bytes: u32 offset, u16 length, u16 0, u32 crc32 of the bytes. E_BAD_LEN detail 4 if length does not match, E_BAD_ARG detail 0 beyond 64 KiB. The stage persists until overwritten. **SD_WRITE_SECTORS 0x0503**: u32 sector, u16 count 1..128, u8 flags (bit 0 CONFIG: allow 708-710, bit 1 BOOT: allow 16-79), u8 0, u32 crc32 of stage[0, count x 512), u32 magic 0x52574453 ('SDWR'). Checks in this order, all before any write: magic (E_BAD_ARG 12), flags (6), count (4), card (E_IO 0), MBR (E_STATE 0: no valid partition table, writes off), window: sector >= 16 and the last sector < write_end (E_BAD_ARG 0), 16-79 need BOOT, any of 708-710 need CONFIG (E_BAD_ARG 0, text says which), stage crc (E_BAD_ARG 8). Then write, read back, compare: E_IO with the driver error, or 0xFFFF on a mismatch. Response 16 bytes: u32 sector, u16 count, u16 0, u32 crc32 read back from the card, u32 0. The power-key interrupt is held off during the write, so the power-off settings save (sector 709) cannot interleave [U]. An SD_WRITE blocks the main loop for its whole duration (bench 0.2.3: up to ~4 s for 128 sectors with read-back, the UI pass waits meanwhile; PING etc. still answer): hosts should write 16-32 sectors per request and allow >= 5 s. Persisting a FEL-loaded image: stage + write the image file in pieces from sector 750 (the image ends well below 2048), then SD_INFO VERIFY: sector 750 check = 1 | owner: persist an image without removing the card |
| 41 | Protocol 1.4: EXT trigger (v0.2.9, FPGA 0x1533) | GET_INFO `proto_minor` = 4. The AL3 family is decoded as (v & 0xFFF0) == 0x1530 and v >= 0x1532 (variant 2, ring 8192, max 16382, by the firmware UI too); 0x1533+ adds caps bit 3 EXT_TRIGGER (0x0008). SET_TRIGGER source 2 = EXT on the calibration pin (3.3 V logic referenced to the scope's USB ground, never more; FPGA internal pull-down; 1 kOhm to FPGA ground. The probe ground (BNC shell) sits about +1.24 V above USB ground (VRF1 via R71): a source on the probe ground reads ~1.24 V low level, unreliable; use a series red LED, anode at the source, and rising edge. Linking the grounds shifts all traces. fpga/al3/README.md "Changes" 2): E_UNSUPPORTED detail 0 without EXT_TRIGGER. With EXT the FPGA ignores the level (it is still stored and read back, as the UI marker) and the UI skips its software trigger refinement; edge (rising / falling), mode, pretrigger, total and auto time-out are unchanged, and trigger_addr has the usual meaning (pad-to-trigger latency 20-25 ns, fpga/al3/README.md; correction from the bench, not built in). Selecting EXT (host or the trigger menu, where EXT is shown only with the cap) switches the generator off, since it drives the same pin; SET_GEN enable while the source is EXT gives E_STATE detail 0 ("EXT trigger uses the pin"); the UI generator Power switch is not blocked (the scope then triggers on its own generator). GET_STATUS byte 42 `ext_flags` (EXT_TRIGGER only, else 0), FPGA cmd 0x28 read at every main-loop poll: bit 0 live EXT level, bit 1 generator enabled (FPGA drives the pin), bit 2 an EXT edge was seen since the last sampling reset (every arm clears it); byte 43 stays 0. GET_CAPTURE_INFO flags bit 5: an EXT edge was seen during that capture. A saved EXT source is kept only on bitstreams with the cap (else CH1) | owner / PI detector work: sync captures to the TX pulse |
| 42 | Protocol 1.5: USB robustness, GET_EVENT_LOG (v0.2.12) | **v0.2.21 removed the tick fallback described here**: the 0.2.20 bench and 30-minute soak showed counter 47 = 0 and counter 57 = 2176, i.e. 1.00o5's dispatcher did not lose a USB interrupt and the fallback only raced the real interrupt (verified 2026-10-18). Since 0.2.21 counters 47 and 57 are always 0 and type 14 is never emitted (the number stays reserved); GET_EVENT_LOG, the snapshot, the 2 ms RX sweep and the TX pump kick are unchanged. History: v0.2.12-0.2.20: the 1 ms timer poll also runs the USB CDC interrupt handler (since v0.2.14 only after two consecutive ticks without a real USB interrupt; 0.2.12-0.2.13 ran it on every tick, which raced the real interrupt during traffic, so their counter 47 is meaningless): the MUSB keeps its interrupt flags until they are read, and 1.00o5's irq_handler clears INTC pending by writing 0 after reading it, so a USB interrupt raised in between was lost; for EP0 nothing else would ever serve it (a control request then times out on the host, which fails the device and resets the port; suspected for the Windows error-22 drops). GET_LINK_STATS counter 47 counts the polls that found flags (v0.2.14+: lost-interrupt candidates). GET_EVENT_LOG 0x0009, any poll context, request empty. Response: u16 n (<= 64; <= 128 since v0.2.15), u16 0, u32 total events since boot, u32 now (ms), then n entries oldest first: u32 ms, u8 type, u8 a, u16 b. v0.2.15 appends the reset snapshot: u16 m (<= 16), u16 frozen (1 = a bus reset froze it), u32 reset ms, then m entries: the events right before the first bus reset since the snapshot was armed, so a later flood or Windows' repeated resets cannot evict them. Re-arming: 0.2.15-0.2.17 at every read; since v0.2.18 only with request byte 0 bit 0 set (u8 flags, other bits E_BAD_ARG detail 0), so a host that reads the log at every session open no longer throws the snapshot away. v0.2.24 appends a bus-level ring after the snapshot: u16 k (<= 16), u16 0, u32 bus-level events since boot, then k entries oldest first (same 8-byte format), holding only types 1-4, 7, 8, 12, 13 and 15-18, so EP0 setups and DTR edges cannot evict them. Since v0.2.24 an EP0 setup identical to the last logged one (bmRequestType, bRequest, wValue low) is logged at most once per second (pyserial repeats GET_LINE_CODING about five times per open); counter 39 still counts every setup. Hosts that stop at the snapshot are unaffected. `ms` is the same clock as GET_STATUS uptime_ms (timer0 ticks, 1 ms). Type 14 ignores the SOF flag (INTUSB bit 3 latches every frame with its interrupt disabled; 0.2.12-0.2.14 logged ~1000/s) and is logged at most once per 100 ms (counter 47 still counts every hit). Since v0.2.16 the tick reads the three flag registers itself and runs the handler body only for a flag other than SOF, so the fallback does nothing at all in a clean session (counter 47 stays 0); 0.2.12-0.2.15 ran the whole handler for SOF 300-500 times per second. Since v0.2.20 the tick no longer reads the flag registers to decide: it checks endpoint state (EP0 CSR0 RxPktRdy, SetupEnd, SentStall, an IN data phase with TxPktRdy clear, a SET_ADDRESS whose status stage completed; EP2 RXCSR RxPktRdy while EP2 OUT is unmasked) and serves only a condition seen on two consecutive ticks with no USB interrupt in between, after reading the flags. It skips any tick at which the USB interrupt is pending. Counter 47 counts these services. Whether the dispatcher's write of 0 to INTC pending actually loses interrupts is unverified (the F1C200s manual does not say how the pending bits clear). Types: 1 bus reset, 2 suspend, 3 resume, 4 disconnect, 5 EP0 setup (a = bRequest, b = bmRequestType << 8 or wValue low byte), 6 EP0 packet not 8 bytes (b = COUNT0), 7 SET_CONFIGURATION, 8 CLEAR_FEATURE(HALT) (a = endpoint), 9 DTR change (a = new DTR), 10 RX ring full (b = RXCOUNT), 11 RX release, 12 TX frame flushed (link / DTR; b = bytes left), 13 TX frame aborted (2 s no progress or #RESET; b = bytes left), 14 retired since v0.2.21 (0.2.12-0.2.20: USB flags found by the timer poll, a = INTUSB, b = INTTX << 8 or INTRX; since v0.2.20 a lost interrupt served by the tick: a = INTUSB as read, b = INTTX << 8 or INTRX as read, low 7 bits each, plus bit 15 = EP0 needed service, bit 7 = EP2 OUT packet waiting). A host port reset shows as suspend then bus reset (the MUSB reports the SE0 as idle first). 16 USB controller state at init (v0.2.22, row 43), 17 and 18 EP2 IN state at a TX abort (v0.2.23, row 43), 19 USB state at the end of init (v0.2.25, row 43), 20 USB soft disconnect (v0.2.49, row 53; also in the bus-level ring) | diagnose USB drops: read the log right after a re-enumeration |
| 43 | USB init state, event type 16 (v0.2.22) | No protocol change (proto 1.5). `usb_device_init` (boot and every CDC <-> MSC switch) logs the USB controller state it finds, then asserts the USB OTG bus soft reset (CCU BUS_SOFT_RST0 bit 24) and the PHY0 reset (USBPHY_CFG bit 0) for 1 ms before 1.00o5's sequence releases them; up to 0.2.21 the resets were only released, so an SD boot configured whatever the bootloader left. Five consecutive type 16 entries, a = field: 0 b = POWER << 8 or FADDR (7 bits); 1 b = INTUSBE << 8 or CCU flags (bit 0 OTG reset released, bit 1 OTG bus clock on, bit 2 PHY reset released, bit 3 PHY clock on); 2 b = EP2 TXCSR; 3 b = EP2 RXCSR; 4 b = ISCR bits 4-19 (bit n of b = ISCR bit n + 4). Controller fields read 0 unless CCU flags bits 0 and 1 are both set. Compare a cold SD boot with a FEL load. EP2 data toggles and FIFOs were already cleared at every SET_CONFIGURATION (ClrDataTog + FlushFIFO on both endpoints). v0.2.23: every type 13 (TX abort) is followed by type 17 (a = POWER, b = EP2 TXCSR) and type 18 (a = bit 0 EP2 TX interrupt enabled, bit 1 ZLP pending; b = USB frame number, 11 bits). UnderRun (TXCSR bit 2) is cleared at every packet load, so TxPktRdy (bit 0) set = a packet waits and the host takes no IN data; TxPktRdy clear with UnderRun set = the host polls and the device has nothing loaded. The frame number advancing by the elapsed ms (mod 2048) between two entries means SOFs keep arriving. Device-mode TXCSR does not expose the data toggle. v0.2.25: the three PHY tuning writes (RES45 calibration 0x0C = 1, TX amplitude 0x20 = 0x14 over 5 bits, disconnect threshold 0x2A = 3 over 2 bits; values as Linux phy-sun4i-usb for suniv) go to PHYCTL 0x01C13404 as in Linux, not to 0x01C13410 as in 1.00o5. At the end of `usb_device_init` seven type 19 entries, a = field: 0 POWER << 8 or FADDR; 1 INTUSBE << 8 or CCU flags (as type 16); 2 ISCR bits 4-19; 3 USBPHY_CFG bits 0-15; 4 PHY tuning bits read back now; 5 the same read right after the PHY reset was released, before the writes; 6 PHYCTL bits 0-15. Tuning bits packed: bit 0 = PHY 0x0C, bits 1-5 = PHY 0x20-0x24, bits 6-7 = PHY 0x2A-0x2B; as written = 0x00E9. The read-back (address in PHYCTL bits 8-15, data at bit 16) is verified on the bench (2026-10-18, FEL load: field 5 = 0x0068 PHY defaults, field 4 = 0x00E9 as written) | diagnose path-dependent USB state and IN stalls |
| 44 | Protocol 1.6: FFT_BENCH (v0.2.27) | Cmd 0x0F02, main loop only (E_BUSY while a UI menu is modal), every bitstream, no cap bit; GET_INFO `proto_minor` = 6. Request: empty or u8 `reps` (0 = 3, at most 16, else E_BAD_ARG detail 0). Runs the on-scope FFT (`fx_fft.c`, Hann window) on the CH1 trace buffer as 8-bit codes minus 128: N = 1024, 2048, 4096, 16384 with the work buffers in DDR, then N = 1024, 2048, 4096 with them in on-chip SRAM A (0x1000-0x700C, borrowed; nothing else uses it at run time; since v0.2.31 the plan holds twiddles too, so only N = 1024, 2048 fit, at 0x1000-0x7008, and n is 6). The UI pass waits for the whole run (estimate about 0.15 s at 3 reps); timer-poll commands (PING, GET_STATUS) still answer. Response: u16 n (entries), u16 reps, u32 CP15 control register as read (bit 0 MMU, bit 2 D-cache, bit 12 I-cache), then n entries of 24 bytes: u16 N, u8 window (1 = Hann), u8 memory (0 DDR, 1 SRAM), u32 plan init us (window table), u32 run min us, u32 run max us (window + load + FFT + split step, over `reps`), u32 dB conversion min us (N/2 + 1 bins), u16 peak bin (bins 2..N/2), i16 peak level (centi-dB re 1 code RMS: a full-scale sine reads about 3907). Times from timer0 (1 ms ticks plus the 24 MHz down-counter, 1 us steps) with IRQs on, so they include the 1 ms timer poll. Diagnostic only; the MCP may expose it as a bench tool. |
| 45 | Protocol 1.7: spectrum view, READ_SPECTRUM (v0.2.28) | GET_INFO `proto_minor` = 7. **Display mode**: the scope has a spectrum view (main menu row "Spectrum analyzer", hidden in roll mode; firmware/README.md "Spectrum view"). It is a main-loop display mode, not a modal menu: every command keeps working, the UI keeps capturing (RUN/STOP, trigger, time base and channel settings as usual) and the trace area shows the spectrum of each new capture, host captures included. GET_STATUS byte 43 `display_mode` (was 0): 0 scope, 1 spectrum; since v0.2.32 also 2 waterfall, 3 density (sub-views of the same screen; READ_SPECTRUM serves the latest spectrum in all three); any poll context. **READ_SPECTRUM 0x020A** (main loop, every bitstream, no cap bit). Request (8 bytes, shorter is zero-extended): u8 channel (0/1, else E_BAD_ARG detail 0), u8 0, u16 first bin, u16 count (0 or more than fits = as many as fit, 33,264), u16 0. E_STATE detail 0 when the spectrum view is off or shows no spectrum of that channel (channel off, not selected, or no capture yet). Response: 32-byte header, then `count` i16 levels in centi-dBV (centi-dBA when the channel is in A mode), bin k at k x bin width; -32000 = empty bin. Header: u8 channel, u8 window (0 rect, 1 Hann, 2 flat-top), u8 log2 N, u8 flags (bit 0 levels in dBA; since v0.2.34 bit 1 = the FFT ran on the 16-bit record of the 0x1534 decimation filter, bits 2-3 = its order K, 0 off; since v0.2.40 bits 4-6 = trace averaging applied to the levels: 0 off, 1 / 2 / 3 = exponential dB average over 4 / 16 / 64 captures, 4 = max-hold), u32 frame (spectra computed since boot; changes with every new capture or setting), u32 divider (the UI time base's 0x0D value; sample rate 200 MHz / (divider + 1) without the filter, 200 MHz / N with it, row 47), u32 sample rate Hz (rounded), u32 bin width mHz (fs / N_fft, truncated), i32 gain cdB (20 log10 volts per code, already applied: subtract it for dB re 1 code RMS), u16 bins in the spectrum (N/2 + 1), u16 first bin returned, u16 count returned, u16 ENBW milli-bins (1000 rect, 1500 Hann, 3770 flat-top; RBW = ENBW x bin width). Levels: dBV of a sine's RMS for a tone centred on a bin (fx_fft.c scaling, host-tested against numpy); volts per code from the calibration as in section 8.5 [U: T8], probe factor included. N is the largest power of two the record holds, capped by the view's FFT length (so 2048 at the 3000-sample default depth, 8192 at 15000 on AL3). The FFT input is the trace buffer minus the zero code (DC bin = the DC level). Known limit: a host capture made with a raw divider (SET_TIMEBASE mode 1) is labelled with the UI time base's rate. |
| 27 | ENTER_FEL (v0.1.1) | cap ENTER_FEL on every known bitstream (not on unknown ones). Magic 0x4C45465F, else E_BAD_ARG detail 0. The OK response is sent first; once it has left the FIFO (at most 200 ms) the scope shows "FEL mode", soft-disconnects USB for 300 ms, masks IRQs, disables the timer, power-key and INTC sources, puts the USB controller and PHY back in reset, turns the caches off (I-cache invalidated), selects high vectors, sets SVC mode with SP = 0x7C00 (the bootloader's FEL state) and jumps to 0xFFFF0020. Settings are not saved; leave FEL with a power cycle or `sunxi-fel exe` | iteration without the boot menu |
| 47 | Protocol 1.9: decimation filter (v0.2.30, FPGA 0x1534) | GET_INFO `proto_minor` = 9; `#VER` says `proto=1.9`. AL3 0x1534..0x153F adds caps bit 15 DECIM (0x8000) besides EXT_TRIGGER; on bitstreams without it nothing below is written to the FPGA and GET_DECIM / SET_DECIM answer E_UNSUPPORTED. FPGA interface (fpga/al3, fpga-gen3; bitstream sha256 83140510...b820c1): 0x53 mode (bits 1:0 K: 0 legacy 8-bit sample skipping, 1 boxcar = sum of N, 2 CIC2, 3 CIC3; bit 2 core-order swap; bit 3 / bit 4 capture the A / B cores on the falling write-clock edge, 5 ns later, default rising), 0x54 output shift S, 0x55 16-bit trigger level (0x17 is ignored while K >= 1), read 0x29 = {bit 7 16-bit layout, bit 4 neg B, bit 3 neg A, bit 2 swap, bits 1:0 K}, S. Fixed latency 12 write clocks (120 ns) from pin to memory for every K and N; a 16-bit sample is centred a further K (N - 1) 2.5 ns earlier; the ADC trigger is self-consistent (same trigger_addr convention as K = 0). N = 2 x (floor(D / 2) + 1), D = the 0x0D divider; output rate 200 MSa/s / N (equal to 200 / (D + 1) for the odd dividers of the time base table); stored sample = min(65535, y >> S), y = filter output, gain N^K; ADC code = sample x 2^S / N^K. The firmware picks S = max(0, bitlen(255 x N^K) - 16), falls back CIC3 -> CIC2 for N > 10334 (slower than 20 kSa/s) and CIC2 -> boxcar for N > 1,050,630 (exactness 255 x N^K < 2^48), and stays legacy (K = 0) at divider 0 (200 MSa/s) and for captures over 8190 samples. 16-bit layout (K >= 1): one sample per channel per ring word (low byte 0x20/0x22 = core A slot, high byte 0x21/0x23); pretrigger, total, trigger_addr and the auto time-out count words = samples (at most 8190 samples). The filter is the UI's acquisition **Filter** setting (off, boxcar, CIC2, CIC3; saved with the settings; shown only on 0x1534+; memory depth 9000+ hidden and reduced to 6000 while it is on); it applies to UI acquisitions and host ARM; calibration and auto setup always run legacy. A change of mode, shift or divider waits (K + 2) output periods before the next sampling reset (free-running filter; at most 9 ms at 500 Sa/s). It is part of settings_seq. **GET_DECIM 0x020C** (main loop), request empty; **SET_DECIM 0x020D**, request u8 filter (0..3, else E_BAD_ARG detail 0), u8 flags (bit 0 core-order swap, host only, not saved; bit 1 / bit 2 capture the A / B cores on the falling edge, hidden settings saved with the Filter word, no UI; bits 3-7 E_BAD_ARG detail 1), u16 0; the flags go into 0x53 with every mode, legacy included; filter >= 1 while the trigger total is over 8190: E_BAD_ARG detail 0 (SET_TRIGGER total first). Both answer 32 bytes: 0 u8 filter setting, 1 u8 flags (as in SET_DECIM), 2 u8 mode the next arm writes (0x29 byte 0 layout; 0 = legacy), 3 u8 its S, 4 u32 its N (0 legacy), 8 u64 gain N^K (1 legacy), 16 u32 divider, 20 u32 output rate Sa/s, 24 u8 mode written at the last arm (0xFF none yet), 25 u8 its S, 26 u8 + 27 u8 FPGA 0x29 read now, 28 u16 8190 (largest total with the filter), 30 u8 why the effective K is lower than requested (0 not lower, 1 filter off, 2 200 MSa/s, 3 total > 8190, 4 CIC3 -> CIC2, 5 CIC2 -> boxcar, 6 no filter in the bitstream), 31 u8 EXT latency shift (samples) the next arm applies with the current source. SET_TRIGGER total > 8190 while the filter is on: E_BAD_ARG detail 5. GET_TRIGGER offsets 20/22 (words) and GET_TIMEBASE rate / period follow the effective configuration. GET_CAPTURE_INFO grows to 76 bytes: 68 u8 the capture's mode (0x29 byte 0 layout, 0 = legacy), 69 u8 S, 70 u16 EXT latency shift, 72 u32 N; capture_start_word uses words of that capture. **EXT trigger with K >= 1**: trigger_addr is the last sample written before the edge, but the stored data lag it by 120 ns + K (N - 1) 2.5 ns; the firmware rounds that to the nearest output sample, shift = floor((240 + 5 K (N - 1) + 5 N) / (10 N)) (half-ns units, halves up; e.g. N 2 boxcar 12, N 20 CIC2 2, N 20 CIC3 3, 1 at slow rates), writes pretrigger - shift as the pretrigger words, and reports the shift at byte 70, so sample `pretrigger` of the capture is the edge to within half an output period (capture_start_word = trigger_addr - (pretrigger - shift)). The pad-to-trigger latency of the EXT input itself (20-25 ns, row 41) is not corrected. READ_TRACE: header byte 30 = the capture's mode byte, 31 = S (both 0 for legacy captures). Without flags bit 2 the samples are the 8-bit trace as before: for a 16-bit capture the converted codes round(sample x 2^S / N^K + (adc_a_comp + adc_b_comp) / 2), clamped 0..255, which is what the screen and the 1.00o5 measurements use. Flags bit 2 (WIDE) with a 16-bit capture returns u16 little-endian samples as the FPGA stored them (no compensation, no inversion: flags bit 1 does not apply, meta bit 0 still reports invert); E_STATE detail 3 if that channel's last readout was not 16-bit. WIDE on a legacy capture is ignored (8-bit samples, byte 30 = 0). READ_RAW layout 2: u16 little-endian per word, low byte core A (0x20/0x22), high byte core B (0x21/0x23), both cores of a channel required (as layout 1): the 16-bit samples of a capture, `count` words = samples. Swap bit (0x53 bit 2): 0 = core B is the earlier sample of each pair, the B-before-A order of fpga_read_sample_data and READ_TRACE (affects K >= 2 only) | owner / spectrum work: anti-aliased, higher-resolution samples at slow time bases |
| 48 | Protocol 1.10: calibration data v2, GET_CAL 248 bytes (v0.2.43) | **Superseded by row 51 (calibration v3, GET_CAL 256 bytes): firmware 0.2.45 and later, and the MCP host, no longer read v2 blocks.** GET_INFO `proto_minor` = 10; `#VER` says `proto=1.10`. **Firmware behaviour**: the base calibration (UI) measures per channel, hardware range (0..5; 6 uses 5) and ADC core (A = 0x20/0x22, odd trace positions; B = 0x21/0x23, even) the raw zero with the inputs open, in two clock classes: *full* (ADCs at full rate: legacy divider 0 and every 0x1534 filtered capture) and *gated* (legacy divider >= 1: the FPGA gates the encode clock and the zero moves, bench 2026-10-18: CH1 core B -3.6 codes, core A -0.8), plus each core's offset slope (codes per PWM count, measured gated). It is stored in sector 708 bytes 128..455 (firmware/README.md section 6.2) and used while bound to the channel's `dc_calibration_offset[0..5]`. Every readout then corrects each core to T = 128 - trunc(dcoffset x 100 / dc_shift_value): correction_k = T - zero[class][k][range] - slope_k x dcoffset, applied as whole codes plus one more code with probability frac / 16 (dither), so READ_TRACE codes, the screen and the measurements put 0 V at T for both cores (A and B means agree to about 1/16 code). The 16-bit filter path adds corr16 / 16 codes to every converted sample (corr16 = 2 x T x 16 - full zeros of A and B with their slope terms, halved), i.e. READ_TRACE code = round(sample x 2^S / N^K + corr16 / 16), clamped; WIDE samples stay raw (the host applies corr16 / 16 after its own conversion). GET_CHANNEL `zero_q8` (offset 20) is T x 256 while v2 is in use (1.00o5 formula otherwise). READ_TRACE channel meta: the zero (meta bytes 8..11) follows the same rule; bytes 12..15 still report the stored 1.00o5 compensations, not the applied v2 correction (that is in GET_CAL). The spectrum view (READ_SPECTRUM, 16-bit records too) takes 0 V from the same correction. Without a bound v2 block everything is as in 1.9 (the 1.00o5 compensations). The base calibration also rewrites the 1.00o5 fields (`dc_calibration_offset` = PWM where the mean of both cores reads 128 gated; `adc_a_comp` / `adc_b_comp` = whole-code split from the gated zeros averaged over ranges), so an older firmware with the same card gets the better values. **GET_CAL** answers 248 bytes; bytes 0..71 unchanged. 72 u8 calibration version in use for this channel (1 = 1.00o5 compensations, 2 = v2), 73 u8 flags (bit 0 v2 block loaded, bit 1 bound to the current PWM centres, bit 2 the current divider is gated), 74 i16 comp_a and 76 i16 comp_b (whole codes the next readout adds), 78 u8 frac_a, 79 u8 frac_b (sixteenths added by dither; 0 without v2 and for 16-bit readouts), 80 i32 zero_q8 (trace code of 0 V x 256), 84 i32 corr16 (16-bit path correction, 1/16 code), 88 i32 shift (dcoffset x 100 / dc_shift_value, what the measurements add), then the stored block (zeros when not loaded): 92 u16 pwm[6] (the PWM centres it was measured at), 104 i16 zero[class][core][range] (2 x 2 x 6, class 0 full, 1 gated, core 0 A, 1 B; 1/16 code), 152 i32 slope[core][range] (Q16 codes per count), 200 u16 noise[class][core][range] (RMS 1/16 code). Host model: raw_k = zero[c][k][r] / 16 + slope[k][r] / 65536 x dcoffset at 0 V, in raw uncompensated codes of core k (the READ_RAW reference: A = 0x20/0x22, the low byte in layout 2; B = 0x21/0x23); READ_RAW stays raw. corr16 is in 8-bit codes x 16 for the mean of both cores (not a window sum): for WIDE samples code = out x 2^S / N^K + corr16 / 16 | owner: the screen, the measurements and the host agree on 0 V; Nyquist spur of the A/B zero spread |
| 49 | Protocol 1.11: CIC compensation FIR (v0.2.44, FPGA 0x1535) | GET_INFO `proto_minor` = 11; `#VER` says `proto=1.11`. AL3 0x1535..0x153F adds caps bit 16 FIR (0x10000) besides DECIM; older bitstreams never get command 0x56. FPGA interface (fpga/al3, fpga-gen4, testbench-verified; bitstream sha256 5040a2ae...): write 0x56 bit 0 = FIR enable (power-on 0, bits 7..1 written 0); read 0x29 byte 0 bit 6 = the enable as written, bit 5 = FIR active now. The FIR is active only when 0x56 = 1, K >= 1, N >= 16 and N % 4 == 0 (D >= 14 and D mod 4 in {2, 3}; in the time base table: 10 MSa/s and slower); otherwise the 0x1534 path of row 47 runs unchanged. Chain when active: CIC of order K decimating by N/2 (gain (N/2)^K), shift S, saturate to 16 bit, 15-tap symmetric FIR decimating by 2 with DC gain 1; output rate 200 MSa/s / N as before, same 16-bit layout and read-back. Firmware S = max(0, bitlen(255 x (N/2)^K) - 16) + 1 (the + 1 is headroom for the 6-9 % step overshoot; the worst-case FIR gain 1.21 cannot saturate); ADC code = sample x 2^S / (N/2)^K; 16-bit trigger level = round(v x (N/2)^K / 2^S); exactness 255 x (N/2)^K < 2^48, so CIC3 holds while N/2 <= 10334 (CIC3 -> CIC2 above, why 4). Latency 25 write clocks (250 ns) pin to memory; **EXT** with the FIR: the data lag the edge by 250 ns + 17.5 N ns + 1.25 K (N - 2) ns, shift = floor((1000 + 70 N + 5 K (N - 2) + 10 N) / (20 N)) (quarter-ns units, halves up; N 20: boxcar 6, CIC2 6, CIC3 7). Settling after a change of 0x0D / 0x53 / 0x54 / 0x56: (K + 10) N x 5 ns + 1 us (rounded up to whole ms + 1 by the firmware). **SET_DECIM** flags bit 3 = FIR (E_BAD_ARG detail 1 without caps bit 16; bits 4-7 E_BAD_ARG detail 1); it is written as requested for every K (0x56 = 1 with the filter off is harmless). **GET_DECIM** byte 1 bit 3 = FIR setting; bytes 2 / 24 / 26 (0x29 byte 0 layout) carry bit 6 enable and bit 5 active; byte 3 S and bytes 8..15 gain = (N/2)^K while active; byte 31 the FIR EXT shift. GET_CAPTURE_INFO byte 68 and READ_TRACE header byte 30 carry bits 5 / 6 of the capture. Every 8-bit conversion (READ_TRACE codes, the screen, the calibration v2 corr16 of row 48, the spectrum view's 16-bit path) uses the FIR-mode gain: host conversion of WIDE samples is code = out x 2^S / gain + corr16 / 16 with gain from GET_DECIM. UI: the acquisition Filter box steps off, boxcar, boxcar+FIR, CIC2, CIC2+FIR, CIC3, CIC3+FIR on 0x1535+ (the FIR variants are not offered on 0x1534); saved in settings word 207 bit 5; the spectrum header tag reads e.g. "CIC2+FIR" only while the FIR is active for that record. Vectors: docs/protocol-vectors.json `decim` cases with `fir_req` = 1 (every table rate x K, D 11..18, D 20667 / 20671 at the CIC3 limit), checked against pc_decim.c by check_vectors.py | owner: CIC + FIR as the eventual default acquisition mode (**superseded**: the default Filter was CIC2 from 0.2.44 and is CIC2+FIR since 0.2.48, `PC_DECIM_CIC2 | PC_DECIM_SET_FIR` in `pc_decim.h`, for new settings sectors and the DEFAULT button; since 0.2.64 per display mode, Scope CIC2 and Spectrum CIC2+FIR, `PC_DECIM_DEFAULT_SCOPE` / `_SPEC`; the FIR part takes effect only on 0x1535+ bitstreams, CHANGELOG 0.2.48); flat passband to fo/4 and about 20 dB more alias rejection (fpga/al3/README.md) |
| 50 | USB suspend / resume (v0.2.44, no protocol change) | **Audit (1.00o5 and earlier builds):** INTUSBE enables SUSPEND, RESUME, RESET, DISCONNECT; POWER (0x01C13040) is set to HS enable + soft connect, EnableSuspendM (bit 0) is never set, so the PHY is never put into suspend (SUSPENDM) and nothing was powered down or gated on suspend; the handlers touch no PHY or CCU register. Linux musb (musb_stage0_irq: musb_g_suspend / musb_g_resume) and the sunxi glue also handle suspend and resume in software only. What did differ: up to 0.2.43 a SUSPEND was handled as a link reset (TX frame flushed, EP2 OUT flushed, DTR cleared, session parser and deferred WAIT dropped), which a real host suspend / resume (configuration and line state kept, no SET_CONFIGURATION or SET_CONTROL_LINE_STATE after the resume) does not survive. **Since v0.2.44** a suspend is a state: no flush, no link reset; the following RESUME interrupt ends it (EP2 OUT unmasked again), a bus reset or disconnect resets the link as before. POWER bit 0 is cleared explicitly at enable (PHY stays powered). While suspended GET_STATUS / indicator DTR reads 0 (a pulled cable also reads as suspend, VBUS is forced valid); the stored DTR returns with the resume. The main loop checks every 20 ms whether the frame number moves again while still marked suspended (SOFs without a RESUME interrupt) and then logs an inferred resume. GET_LINK_STATS counter 10 `link_resets` no longer counts suspends; 36 counts SUSPEND interrupts, 37 RESUME interrupts only. **Event fields** (GET_EVENT_LOG, were 0): type 1 bus reset and type 4 disconnect: a = 1 if the device was suspended, b = ms since that suspend (65535 = longer); a port reset reads a = 1 with a few ms (the MUSB reports the SE0 as idle first), a reset after a real host suspend reads its length. Type 2 suspend: a = POWER, b = frame number. Type 3 resume: a = POWER (bit 7 set = inferred by the frame check, no RESUME interrupt), b = ms suspended. Reading the 0.2.43 bench case (suspend -> bus reset -> SET_CONFIGURATION at reopen, nothing before): with these fields, a reset with b of a few ms after minutes of silence means the host never suspended and reset the port for another reason; b of minutes and no type 3 means a missed resume. **v0.2.49 (row 53)**: the frame check runs every ms in the timer interrupt and acts (an inferred resume ends the suspend state, counter 60, a bits 6-5 = cause), and a soft reconnect recovers a device the host abandoned | bench 2026-10-18: Windows error 22 at the first write after ~5 min with the port closed |
| 51 | Protocol 1.12: calibration data v3, per-core and per-clock-class gain, GET_CAL 256 bytes (v0.2.45) | GET_INFO `proto_minor` = 12; `#VER` says `proto=1.12`. **Why** (bench 2026-10-18, CH1 range 2, +-1.0045 V): the two ADC cores differ in gain at full rate (A 28.62, B 27.71 codes/V, A/B 1.033, linear) and barely when the encode clock is gated (A 28.54, B 28.42, A/B 1.004), and the full-rate class has 1.1 % less gain than the gated one; a zero-only model (row 48) leaves an A/B difference that grows with the signal (the fs/2 spur at full rate) and makes 200 MSa/s and every filtered capture read 2.7-3.5 % low against the gated voltage calibration. **Model** per channel, core k (A = 0x20/0x22, odd trace positions; B = 0x21/0x23, even), clock class c (row 48: 0 full = legacy divider 0 and every filtered capture, 1 gated = legacy divider >= 1), hardware range r (6 uses 5): raw_k = z_k + V x g_k[c] (V at the ADC, codes), z_k = zero[c][k][r] / 16 + slope[c][k][r] / 65536 x dcoffset (the offset slope is now measured per class too). Gain correction G_k[c] = g_ref / g_k[c] with g_ref = the mean gain of both cores in the gated class (what `input_calibration` is fitted on, so legacy gated volts keep their calibration and the full-rate class is pulled onto it). One gain per core and class for all ranges: the mismatch is inside the ADC (**assumption**, measured on one range, bench check listed in firmware/README.md section 6.3). **Measurement** (base calibration, second step, UI only): after the zero step passes (probes unplugged, as before) the box asks for both probes (x1 or x10) on the calibration post; OK runs it, a touch outside skips it (the zeros are saved either way; the gains of the previous v3 block are carried over, else unity). The FPGA generator (AL3 bitstreams, commands 0x50-0x52) holds the post at its two static levels, about -1.24 V (output low) and +1.87 V (output high) against probe ground (docs/hardware-status.md); no level needs to be known. Per channel: a probe of both levels at range 2 picks the most sensitive range whose span is at most 120 codes (x1 probe: range 2, about 88 codes; x10: range 5), the offset PWM is moved so the two levels sit symmetric about the 0 V code (about 128; even-order INL about it cancels in the span), then low, high, low again with 8 gated (2 MSa/s) and 64 full-rate (200 MSa/s) conversions each; span_k[c] = high - mean of the two lows (linear drift cancels), G_k[c] = (span_A[gated] + span_B[gated]) / 2 / span_k[c]. Checks (first failure wins, box text "CH1: <reason>", that channel keeps its previous gains): no sample at 0 or 255 ("gain: level clips"), the two lows within 0.5 code ("gain: level drifts"), every span >= 40 codes ("gain: probe on post?"; also when the levels come out swapped), |G_A - G_B| <= 0.08 per class ("gain: cores differ"), (G_A + G_B)[full] / (G_A + G_B)[gated] within 0.9..1.1 ("gain: clock classes"). Channels pass or fail independently; the generator returns to its UI state afterwards. **Correction** per readout and core: code = clamp((raw x gain_k + off_k + u) >> 16, 0, 255), gain_k = round(G_k x 65536), off_k = round((65536 x T16 - gain_k x z16_k) / 16), T16 = 16 x T, T = 128 - trunc(dcoffset x 100 / dc_shift_value), z16_k = zero + round(slope x dcoffset / 4096) in 1/16 code, u = a uniform 16-bit dither per sample (xorshift32 top bits): the mean is exactly T + G_k x (raw - z_k), so 0 V sits at T for both cores as before and a signal has the same codes per volt on both cores and in both classes. The 1.00o5 rail rule now compares against the corrected codes of raw 0 and 255 of both cores (clip_lo = the larger of the two cores' codes for raw 0, clip_hi = the smaller of their codes for raw 255; when core B's code of a sample pair is at or below clip_lo, or at or above clip_hi, both codes of the pair are set to the lower, or the higher, of the two). **16-bit path** (filter on, ADCs at full rate, each output weighs both cores equally): gain16 = 2 G_A G_B / (G_A + G_B) of the full class (Q16), code = clamp(round(out x 2^S / gain x gain16 / 65536 + corr16 / 16)), corr16 = round(T16 - gain16 / 65536 x (z16_A + z16_B) / 2) with the full-class zeros; the spectrum view's 16-bit FFT uses the same scale. WIDE READ_TRACE and READ_RAW stay raw. Without a v3 block (or not bound) everything is as in 1.9 (1.00o5 compensations); **v2 blocks are no longer used** (the owner re-runs the base calibration). **Sector 708 block v3** bytes 128..431 (304 bytes): u32 'ACAL', u16 version 3, u16 length 304, u16 classes 2, u16 0; per channel (CH1, CH2, 144 bytes): u16 pwm[6] (binding, as row 48), i16 zero[class][core][range] (1/16 code), i16 slope[class][core][range] (Q16 codes per PWM count), u8 noise[class][core][range] (RMS, 1/16 code), u16 gain[class][core] (Q15, 32768 = 1), u8 gain_range (hardware range the gains were measured on, 0xFF = never measured: gains 32768), u8 gain_flags (bit 0 carried over from an earlier calibration), u16 gain_lever (gated span of the mean of both cores, 1/16 code); u32 zlib.crc32 of the 300 bytes before it. **GET_CAL** answers 256 bytes; bytes 0..71 unchanged. 72 u8 version in use for this channel (1 = 1.00o5 compensations, 3 = v3), 73 u8 flags (bit 0 v3 block loaded, bit 1 bound to the current PWM centres, bit 2 the current divider is gated, bit 3 gains measured for this channel (gain_range != 0xFF), bit 4 gains carried over), 74 i16 comp_a, 76 i16 comp_b, 78 u8 frac_a, 79 u8 frac_b (the correction at each core's 0 V point in whole codes and sixteenths, T16 - z16_k; with v1 the 1.00o5 compensations and 0), 80 i32 zero_q8 (trace code of 0 V x 256 = 256 T), 84 i32 corr16, 88 i32 shift (dcoffset x 100 / dc_shift_value), 92 u32 gain_a, 96 u32 gain_b (Q16, what the next 8-bit readout multiplies by; 65536 with v1), 100 i32 off_a, 104 i32 off_b (Q16 codes; v1: compensation x 65536), 108 u32 gain16 (Q16, 16-bit path; 65536 with v1), 112..255 the channel's stored block (144 bytes, the layout above; zeros when not loaded). Host model: READ_TRACE code_k = (raw x gain_k + off_k) / 65536 on average; WIDE code = out x 2^S / gain x gain16 / 65536 + corr16 / 16 (gain from GET_DECIM); volts as row 48 (uv_per_lsb from input_calibration, gated class). Vectors: `adc_cal_v3` in docs/protocol-vectors.json (block, checked against adc_cal.c by check_vectors.py); `adc_cal` (v2) stays until the host has moved | owner: 200 MSa/s, filtered and legacy volts agree; the full-rate Nyquist spur of the A/B gain difference |
| 52 | Protocol 1.13: phosphor view, GET_STATUS display_mode 4 (v0.2.46) | GET_INFO `proto_minor` = 13; `#VER` says `proto=1.13` (1.12 is fw-cal2's calibration v3). No new command and no new field. **Display mode**: the scope has a digital phosphor view (main menu row 7 "Phosphor display", next to the spectrum view's row 6; both hidden in roll mode and in the picture / waveform viewers; firmware/README.md "Phosphor view"). Like the spectrum view it is a main-loop display mode, not a modal menu: every command keeps working and the UI keeps capturing. The scope view stays as it is (grid, cursors, measurements, pointers); the CH1 / CH2 traces are drawn from per-pixel hit counts of the recent records. GET_STATUS byte 43 `display_mode` = **4** while it is on (0 scope, 1 spectrum, 2 waterfall, 3 density as before; the spectrum and phosphor views exclude each other). Records added: UI captures, and a host single capture once when it completes (READ_TRACE re-reads of it are not added); untriggered auto time-out records are skipped while a triggered one came within the last second. READ_TRACE, READ_RAW and every capture command are unchanged (the maps are display-only); READ_SCREEN shows the graded picture. Settings sector word 210 (0x9A00 marker, bit 2 palette, bits 0-1 persistence 0.2 s / 1 s / 5 s / infinite), saved at power-off like words 207-209; the mode itself is not restored. Host: the MCP's `DisplayMode` names 0-4 (scope, spectrum, waterfall, density, phosphor). |
| 53 | Protocol 1.14: USB suspend watch, inferred resume and soft reconnect (v0.2.49) | GET_INFO `proto_minor` = 14; `#VER` says `proto=1.14`. **Why** (bench 2026-10-19, fw 0.2.48-nommu): with the port closed from about 08:40 to 10:20 the device saw 17 SUSPEND interrupts and no RESUME (GET_LINK_STATS 36 / 37 = 17 / 0, no restart) and at 10:20 the host no longer listed it (no serial device, no re-enumeration) until the cable was replugged. Counter 37 has been 0 on every build and bench, so a RESUME interrupt has never been observed on this controller; and a host that gave up on the device leaves the bus idle, which the device cannot tell from a suspend. **Firmware behaviour**: a watch (`pc_usbw.c`, host-tested) runs in the 1 ms timer interrupt (0.2.44 checked from the main loop every 20 ms, which stops while a menu is modal). (1) **Inferred resume**: while marked suspended, the frame number moving (SOFs again), an EP0 setup or an EP2 OUT packet ends the suspend exactly like a RESUME interrupt (EP2 OUT unmasked again if configured, DTR reads as stored again). GET_LINK_STATS counter **60** counts them; event type 3 with a bit 7 set as before, now a bits 6-5 = cause (0 frame number moved, 1 EP0 setup, 2 EP2 OUT packet; 3 only from fw 0.2.44-0.2.48, whose a was 0x80 or POWER bits 6-0 with SoftConn and HS enable always set: frame number moved) and a bits 4-0 = POWER bits 4-0 (bit 4 high-speed mode, bit 1 the core's suspend mode), b = ms suspended (65535 = longer). A real RESUME logs type 3 with a = POWER (bit 7 clear) as before. (2) **Soft reconnect**: the watch clears POWER SoftConn (0x40, D+ pull-up released) for 1000 ms and sets it again, so the hub reports a disconnect and a connect and the host enumerates a fresh device (same descriptors and serial number, so Windows brings back the same COM port). It fires only while the bus has stayed idle (frame number frozen) since the last suspend: **storm**: 3 or more suspends since the last proof of life (SET_CONFIGURATION, a resume received or inferred) and then 10 s of silence, the wait doubling with each storm reconnect not followed by a SET_CONFIGURATION (10 s, 20 s, ... at most 640 s); a host that retries a port reset produces one suspend per attempt (at high speed the reset's SE0 is first seen as 3 ms idle) and the silence once it gives up; **long**: suspended for 15 min after a host enumerated the device, at most once per enumeration (a reconnect disarms it until the next SET_CONFIGURATION, so a sleeping host or a pulled cable costs one reconnect); the 15 min double with every long reconnect since an application last opened the port (DTR 0 to 1), up to 8 h, so an awake host that re-enumerates the device and suspends it again (selective suspend of an idle port) sees a reconnect after 15, 30, 60 ... min, not every 15 min. An open port is not suspended while the host is awake (the host keeps polling bulk IN). A normal suspend never meets the storm rule (one suspend, then resume or reset); the long rule cannot tell a 15-minute legitimate suspend (host asleep, port closed) from an abandoned device and then costs one re-enumeration. GET_LINK_STATS counter **61** counts soft reconnects; **event type 20** soft disconnect: a bits 1-0 reason (1 storm, 2 long), bits 3-2 back-off level of the rule that fired, after this reconnect (capped at 3), bits 7-4 suspends since the last proof of life (capped at 15); b = seconds suspended. The disconnect also resets the link like a bus reset (TX frame flushed, DTR cleared). Type 20 is kept in the bus-level ring too. The watch is not armed in mass-storage mode and does nothing while USB was switched off by other code (SoftConn clear without a watch disconnect). GET_LINK_STATS n = **62** (60 inferred resumes, 61 soft reconnects; decode by n). The CDC interrupt handler now handles SUSPEND before RESUME when both are pending (a resume can only follow its suspend). Nothing else changed in the suspend handling of row 50 | the device must come back after any host suspend without a replug; a resume the core does not report must not leave the transport marked suspended |
| 54 | Protocol 1.15: calibration v4, background tracking of the full-rate core mismatch (A - B) and of the full-class common mode, GET_CAL 288 bytes (v0.2.50) | GET_INFO `proto_minor` = 15; `#VER` says `proto=1.15`. **Why** (bench 2026-10-19, docs/plans/interleave-tracking.md): the gated class (legacy divider >= 1) holds the one-time v3 calibration over 9 hours (zeros within 0.3 code, gains within 0.4 %); the full-rate class (divider 0 and every filtered capture, both cores at 100 MHz, interleaved) does not: the core A/B gain ratio read 1.033, 1.013 and 1.000 within one night, the A/B zero split moved 1.1 code within 15 minutes, a bow of about 1 code at 35-50 codes from mid-scale appeared, and both cores' common mode rose 0.7-0.8 code in two hours against the gated class. Symptoms: a 1-2 code sample-by-sample split and an fs/2 spur in 200 MSa/s records; a 0.7-0.8 code zero error in every filtered capture. **A - B tracker** (per channel, CH1 / CH2 only, from every 8-bit full-class readout with a bound v3 block: UI records and host captures alike, at most one record per 50 ms; never from 16-bit readouts, the gated class or the calibration's own measurements): each interior sample s_n against the mean i_n of its two neighbours of the other core gives y = +-(s_n - i_n) (+ for core A) as an estimate of A - B at x = (s_n + i_n) / 2 - 128; a pair is used when all three samples are within 4..251 and the two neighbours differ by at most 6 codes, a record when it has at least 256 such pairs and at most 1/8 of the in-range pairs were dropped for slope (edges, energy near fs/2). The record's sums are moved back to the uncorrected domain (y + the d and e applied to that readout) and added to moments with forgetting 1/256 per accepted record (tau = 256 records: 12.8 s at the 50 ms gate, longer when records come slower); a least-squares fit gives delta(x) = A - B = d + e x + q x^2, x = code - 128. e is updated only while the accumulated spread of x is at least 8 codes RMS, q only while x visited at least three separated levels (w >= 65536 codes^4 and w >= v^2 / 4: a sine of +-27 codes or more, or a quiet input followed by a square); otherwise the last value is held (a quiet input tracks d only). **Applied** in the full class only (v3 bound, divider 0), symmetric about the mean of the cores: A' = A - (d + e x) / 2, B' = B + (d + e x) / 2, folded into the 8-bit readout's gain_k / off_k and its rail limits (no cost per sample). The mean of the cores, and therefore the 16-bit path (gain16, corr16), is unchanged by it. q is estimated and reported, not applied (a 1-code bow at 42 codes leaves q x^2: 0 at mid-scale, 0.25 code at 21, 1 code at 42 codes). **Class probe** (common mode of the full class against the gated class, the only stable reference on the board): on the channel's current range, offset PWM and coupling (no relay or PWM change), legacy conversions (K = 0): 8 conversions at 2 MSa/s (gated), 64 at 200 MSa/s (full; averages the 25 kHz offset-PWM ripple), 8 at 2 MSa/s again, about 100 ms; per class the corrected mean of both cores from the raw means and that class's v3 correction; c = full - mean of the two gated parts. Valid when every part's per-core RMS is at most 2 codes, no sample sits at 0 or 255, the two gated means agree within 0.3 code and |c| <= 8 codes. cm = c at the first valid probe after a reset, then cm += (c - cm) / 4. **Applied** (v3 bound): both cores' full-class off_k -= 65536 cm (8-bit full-rate readouts) and corr16 -= 16 cm (the 16-bit path: every filtered capture, the spectrum view's 16-bit records and the 16-bit trigger level; additive after the conversion, so independent of N, K and the FIR gain), i.e. the full class reads the gated class's zero again. The firmware runs a probe per channel at most every 30 s (the first 5 s after boot) and only when the scope or spectrum view is up with no menu open, the scope is running (RUN, not roll mode), no UI conversion is in flight, no host capture is armed or triggered and the host holds no READ_RAW hold, the channel is on and its last record was quiet (RMS about its mean <= 2 codes); it costs one screen update. A periodic signal never qualifies (cm then holds its last value; see the age). Like any firmware-internal conversion (calibration, auto setup) a probe resets the sample system: the ring no longer holds the last UI capture. **Resets**: tracker and cm start from zero (the stored v3 values alone) at boot and whenever the calibration wizard's Done writes a calibration; nothing is persisted. **GET_CAL** answers 288 bytes; bytes 0..255 keep their layout and meaning, except that 84 corr16 and 92..107 gain_a, gain_b, off_a, off_b now report what the next readout applies including v4 (bytes 74..80 stay the stored v3 correction at 0 V). New, little-endian: 256 u8 track_flags: bit 0 a record was accepted since the reset, bit 1 e identifiable at the last solve, bit 2 q identifiable at the last solve, bit 3 the last offered record was usable, bit 4 the tracker applies to this channel's next 8-bit readout (v3 bound, the current divider is the full class, and the last arm's filter configuration was legacy K = 0), bit 5 the class probe has data (a valid probe since the reset), bit 6 the last class probe was valid; 257 u8 0; 258 i16 d_q8 (A - B offset at code 128 applied, 1/256 code); 260 i32 e_q24 (A - B slope applied, Q24: 2^24 = 1); 264 i32 q_q24 (bow estimate, Q24 per code^2, not applied); 268 u32 age_ms (ms since the last accepted record, 0xFFFFFFFF = none since the reset); 272 u32 records (accepted since the reset); 276 u16 last_pairs (usable pairs of the last offered record); 278 u16 spread16 (RMS spread of the accumulated x, 1/16 code); 280 i16 cm_q8 (full-class common mode, corrected full mean minus gated mean, 1/256 code; the readouts subtract it); 282 u16 cm_probes (valid probes since the reset); 284 u32 cm_age_ms (ms since the last valid probe, 0xFFFFFFFF = none). **Host**: READ_TRACE 8-bit codes, the screen, the measurements and READ_SPECTRUM already carry v4. A host that converts READ_RAW or WIDE samples itself uses bytes 92..107 for 8-bit raw codes (code_k = (raw x gain_k + off_k) / 65536, unchanged formula) and 84 corr16 for WIDE samples, both read right before or after the capture; to model it from the fields instead: A - (d + e x) / 2, B + (d + e x) / 2 with x = code - 128 in the full class, and -cm on both cores (full class 8-bit) and on every WIDE code. GET_CAL bytes 0..255 parse as in 1.12 (a 1.12 host ignores the tail) | owner: 200 MSa/s A - B within 0.1 code and the fs/2 spur at the floor without re-calibrating; filtered zeros stay at 128 hours after calibration |
| 55 | Protocol 1.16: diagnostics readers: GET_HEALTH, card image identity (SD_INFO 112 bytes), calibration provenance (GET_CAL 356 bytes), SAVE_REPORT (v0.2.52) | GET_INFO `proto_minor` = 16; `#VER` says `proto=1.16`. **Why** (docs/plans/diagnostics-review.md section 4): the Diagnostics Status, Calibration and Log pages and the "Save report" file need values no command carried: the USB link state now, the identity of the image the card boots, where a calibration came from, a power-on count and operating time, a restart record that survives a power cycle, the settings save state and the CP15 control word. **GET_HEALTH 0x000A** (any poll context, also while a menu is modal; no cap bit). Request: empty, or u8 flags: bit 0 CLEAR_RESTART = clear the stored restart record after this answer was built (the answer still shows it; the cleared record reaches the card with the next settings save); PC_DEBUG builds only (caps DEBUG_RAW): bit 7 RESTART_TEST = after the answer has left, restart from RAM through the image entry (a type 0 restart, as a NULL call; a bench aid for the stored record); other bits E_BAD_ARG detail 0. Response 96 bytes, little-endian: 0 u8 layout (1), 1 u8 0, 2 u16 length (96). **USB now**: 4 u8 usb_state: 0 off (USB switched off by other code: mass-storage mode or ENTER_FEL), 1 reconnecting (the suspend watch's soft disconnect, row 53, is in progress), 2 not configured (pull-up on, no SET_CONFIGURATION since the last bus reset or boot: no host, a charger-only cable, or enumeration in progress; the defined "disconnected" state), 3 configured (enumerated, bus active), 4 suspended (configured, then the bus went idle: host asleep, an idle port under selective suspend, or the cable pulled; VBUS is forced valid in ISCR, so the device cannot tell a pulled cable from a suspend); 5 u8 usb_flags: bit 0 high speed (POWER bit 4), bit 1 configured (EP2 set up since the last bus reset), bit 2 suspended now (the watch's state, rows 50 / 53), bit 3 port open (DTR, as GET_STATUS flags bit 3), bit 4 SoftConn (POWER bit 6); 6 u8 POWER (0x01C13040) as read; 7 u8 FADDR (0 = no address assigned); 8 u32 ms in the current suspend (0 = not suspended); 12 u32 ms since the last SET_CONFIGURATION (0xFFFFFFFF = none since boot). **CPU**: 16 u32 CP15 control register c1 as read now (bit 0 MMU, bit 2 D-cache, bit 12 I-cache; 0x0005107D on 0.2.48+ images, 0x0005107C with the MMU off). **Unit life** (settings sector 709 words 224..255 below): 20 u32 power_on: this power-on's number, counted at every boot that is not a warm restart (a power-on or a FEL load; DDR contents surviving a very short power cycle make it look like a restart, row 38), 0 = unknown (no valid settings sector at boot); 24 u32 operating minutes, all power-ons together including this one (stored minutes + uptime; minutes since the last save are lost when a power-on ends without a save); 28 u32 uptime_ms (as GET_STATUS 16); 32 u8 life_flags: bit 0 a life record was found on the card at boot, bit 1 the boot write of sector 709 succeeded, bit 2 it failed (bits 1-2 both 0: no boot write, the settings sector was not valid), bit 3 sector 709 was valid at boot (else the 1.00o5 defaults were loaded); 33 u8 last_end: how the previous power-on ended: 0 unknown (no life record), 1 settings saved at power-off, 2 ended without a power-off save (a warm restart, power lost, a picture / waveform viewer open at power-off, or the write failed); 34 u8 settings_flags: bit 0 the settings changed since they were loaded or last saved (sector 709 words 2..223 as the power-off save would write them, compared by CRC-32; evaluated by the main loop once per second and on demand, so stale while a menu is modal: see 40), bit 1 the last in-session save failed; 35 u8 in-session saves of sector 709 since boot (calibration Done, DEFAULT; capped at 255); 36 i32 result of the last in-session save (0 ok, negative = driver error; 1 = none since boot); 40 u32 ms since bit 0 of 34 was evaluated. **Stored restart record** (survives power cycles until cleared): 44 u8 rec_flags: bit 0 a record is stored, bit 1 it is from this power-on (else from an earlier one); 45 u8 exception type (row 38: 0 jump to the image entry, 1 undefined, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved vector, 6 FIQ); 46 u16 restarts (counter 49 when it was stored, capped 65535); 48 u32 power_on it happened in; then as GET_LINK_STATS at that boot: 52 exception LR (51), 56 its SPSR (52), 60 its SP (53), 64 LR at _reset (54), 68 SP at _reset (55), 72 CPSR at _reset (56), 76 fault status (58), 80 fault address (59); 84 u32 operating minutes when it was stored; 88..95 0. **When stored**: at every boot after a warm restart (counter 49 > 0) the firmware copies counters 49-59 into the life record, replacing an older one, and writes sector 709 at once (the boot write below), so a crash followed by the owner's power cycle or a flat battery is still visible. **Sector 709 life record** (u16 words, inside the 1.00o5 checksum of words 2..255; 1.00o5 and older open firmware keep words they do not use, so the record survives a boot of an older image): 224 marker 0x11FE, 225 flags (bit 0 session open: set by the boot write, cleared by the power-off save), 226-227 power_on (u32, low word first, as every u32 below), 228-229 operating minutes, 230 restart flags (bit 0 stored), 231 restarts, 232 exception type, 233 0, 234-235 power_on of the record, 236-237 exception LR, 238-239 SPSR, 240-241 exception SP, 242-243 reset LR, 244-245 reset SP, 246-247 reset CPSR, 248-249 fault status, 250-251 fault address, 252-253 operating minutes at the record, 254-255 0. **Boot write**: when sector 709 loaded valid (checksum and version), the boot increments power_on (not on a warm restart), stores the restart record if any, sets the session-open flag and writes sector 709 once (one sector, power key held off, the other words exactly as loaded, checksum recomputed). Without a valid sector nothing is written at boot and power_on stays 0 until the first save. Every later save (power-off, calibration Done, DEFAULT) writes the record with the current minutes; the power-off save clears the session-open flag. Words 211..223 stay free for settings. **Card image identity (SD_INFO)**: request flags bit 1 IDENT: identify the eGON.EXE image at sector 750 (or reuse the cached result: the identity is cached until an SD_WRITE_SECTORS touches sector 750 or later); without it bytes 64..111 report the cache (state 0 if never read). The response is now 112 bytes (a 1.3 host reads the first 64). 64 u8 ident_state: 0 not read yet, 1 identity record (fw 0.2.52 and later), 2 commit hash and time from the build tail of fw 0.2.47-0.2.51 plus the version string, 3 version string only (images before 0.2.47: no build identity in the image), 4 no identity found ("unknown"), 5 no eGON.EXE image at sector 750, a length over 2048 sectors, or a read error; 65 u8 flags: bit 0 the card image is the running image (state 1 or 2 and version, hash and time equal to the running build's); 66 u8 proto_major, 67 u8 proto_minor (0 = unknown); 68 char[16] version (NUL-padded, as GET_INFO 36; empty when unknown); 84 u32 build (commit hash, as GET_INFO 52; 0 = unknown); 88 u8[6] commit time yy mm dd hh mm ss (UTC, zeros = unknown); 94 u16 0; 96 u32 image length (eGON header); 100 u32 ms the identification took (0 = from the cache); 104..111 0. **How the identity is found**: fw 0.2.52 links a 36-byte identity record as the last object of the image (build_id.c, linked last; .rodata is the last loaded section, so the record ends where the image ends before mksunxi's zero padding to 512): u32 0x44495746 ('FWID'), u32 0x33313031 ('1013'), char[16] version, u32 build, u8[6] commit time, u8 proto_major, u8 proto_minor; the reader looks for the two magic words at 4-byte steps in the image's last 1024 bytes (the last match wins). fw 0.2.47-0.2.51 end with build_id.c's 12 bytes, which GCC 10 emits time first: u8[6] commit time, u8[2] 0, u32 build; the reader takes the last non-zero byte p, the 12 bytes ending at the next multiple of 4 above p, and accepts them only when the time is a valid date (year 20..99, month 1..12, day 1..31, hour < 24, minute and second < 60), the two pad bytes are 0, the build is not 0, and the image's version string is 0.2.47 or later (checked on 0.2.48, 0.2.49 and 0.2.50 images from backups/). The version string: every protocol image since 0.1.x carries the `#VER` answer text `#OK,FNIRSI-1013D-AI,fw=<version>,proto=<major>.<minor>,fpga=0x`; for states 2-3 the reader scans the whole image for it (about the cost of a VERIFY: one pass over the image in 64-sector reads). **Calibration provenance (sector 708 bytes 432..487)**, 56 bytes after the v3 block, outside Atlan's checksum and outside the v3 block's CRC (the v3 block and its version are unchanged, so 0.2.45-0.2.51 still read the card; their own 708 save clears the trailer, like any older firmware's, after which the provenance reads "none"): u32 0x56525041 ('APRV'), u16 version 1, u16 length 56, u32 build (commit hash of the firmware that calibrated), char[16] fw version, u16 FPGA version word, u8 what (bit 0 CH1 zeros, bit 1 CH1 gains, bit 2 CH2 zeros, bit 3 CH2 gains: the parts this calibration wrote), u8 rtc (1 = the time below is from the DS3231 module, 0 = no module: no time), u32 uptime_ms at the calibration, u32 power_on at the calibration, u32 operating minutes at the calibration, u8[6] time yy mm dd hh mm ss (zeros without a module), u16 0, u32 zlib.crc32 of the 52 bytes before it. **Stamped** whenever sector 708 is saved with a v3 block that differs from the one loaded or last saved (the calibration wizard's Done); `what` compares each channel's PWM centres, zeros, slopes and noise (zeros) and gains, gain range, flags and lever (gains). A save with an unchanged block (DC-shift trim "End", the volts calibration) keeps the stored provenance; no v3 block, no trailer. **GET_CAL** answers 356 bytes; bytes 0..287 unchanged. 288 u8 prov_state (0 none on the card, 1 valid, 2 present but unreadable: CRC or version), 289..291 0, 292..347 the 56 stored bytes (zeros when none), 348 u32 power-ons since the calibration (current power_on minus the stored one; 0xFFFFFFFF = unknown), 352 u32 operating minutes since the calibration (0xFFFFFFFF = unknown). Provenance is per block, so both channels' GET_CAL show the same bytes. **SAVE_REPORT 0x0504** (main loop, cap SD_SECTORS): request empty. Writes one text file to the card's FAT partition, `reports/report-PPPPP-NN.txt` (PPPPP = power_on, NN the first free 00..99; folder created when missing): identity (running image, card image, FPGA word, variant, caps, serial, CP15 word in words), unit life and settings state, USB state, battery, the live restart counters and the stored record, all GET_LINK_STATS counters by name, the decoded event log (each entry in words) and the bus-level ring, the SD boot-sector checks and both channels' calibration (correction in use, v3 block values, v4 tracking, provenance) in words. Response 56 bytes: u32 bytes written, u16 0, u16 name length, char[48] name (NUL-padded, e.g. `reports/report-00012-00.txt`). Errors: E_IO detail = FatFs FRESULT (e.g. 3 not ready, 7 denied, 8 exists: 100 reports with this power_on), E_IO 0 without a card. The Diagnostics "Save report" button calls the same firmware function (`pc_glue_save_report`). Vectors: `diag_layouts` in docs/protocol-vectors.json (life record words, provenance trailer and identity record, checked against the C packers by check_vectors.py) | owner: the Diagnostics pages and the report show real values: "Running fw X from RAM, card boots Y", calibration age without a clock, a crash still visible after a power cycle |
| 56 | Protocol 1.17 (provisional): FPGA 0x1536 sequencer, segmented capture and hardware averaging (v0.2.58). **Numbered 70 on the fw-seq branch (brief: fw-readers' 1.16 took row 55 in parallel, now merged); the coordinator renumbers the row at the merge.** | GET_INFO `proto_minor` = 17; `#VER` says `proto=1.17`. AL3 0x1536..0x153F adds caps bit 17 SEQ (0x20000) besides FIR; on older bitstreams nothing below is written to the FPGA and SET_SEQ / GET_SEQ / READ_SEQ answer E_UNSUPPORTED (READ_RAW addr_mode 3: E_BAD_ARG detail 1). FPGA interface (fpga/al3/README.md section 6, docs/plans/segmented-capture.md; bitstream sha256 4eaf6811...b48b, fpga-gen5): writes 0x57 (bits 1..0 mode 0 off / 1 segments / 2 averaging, bit 2 circular, bit 3 raw readout), 0x58 count (12 bit), 0x59 m, 0x5A N (0 = 65536), 0x5B S (readout shift), 0x5C hold-off (5 ns units), 0x5D stop; reads 0x0A (the whole run is done), 0x2A status (8 bytes), 0x2B..0x32 byte 0..7 of the 64-bit view word at the 0x1F pointer (one dummy strobe after 0x1F, as 0x20..0x23). The firmware writes 0x0B / 0x0C and 0x57..0x5C **before** 0x01 = 1 on 0x1536+ (write-clock registers load only on write-clock edges during the reset; at slow legacy rates the reset window can be shorter than one write clock; fpga-gen6 RTL reading), and 0x57 = 0 for every capture that is not a run when the FPGA may still hold another mode (always after boot). A "word" is a memory word as for 0x0B / 0x0C / 0x14: one A/B pair per channel in legacy mode (K = 0), one 16-bit sample per channel with the decimation filter. **Features word** (also for hosts that do not use the sequencer): GET_STATUS grows to 48 bytes: 44 u16 features (bit 0 EXT_TRIGGER = 0x1533+, 1 DECIM = 0x1534+, 2 FIR = 0x1535+, 3 SEGMENTS and 4 HW_AVERAGE = 0x1536+, bits 5-15 0), 46 u8 UI Average (0 off, 1 software: 1.00o5's per-pass (trace + previous) / 2, 2 hardware: the last UI record is the mean of an averaging run), 47 u8 log2 of the records per UI averaging run (2 only, else 0). GET_DECIM grows to 36 bytes with the same 4 bytes at 32..35. Both need no FPGA access. **UI hardware averaging** (no host command; firmware/README.md "Segmented capture and hardware averaging"): with the acquisition menu's Average on (auto or normal trigger mode, not the waveform viewer, not roll mode) and a 0x1536 bitstream, a UI conversion of a legacy record that fits the accumulator (the 3000-sample depth = 1500 words; i.e. filter off or 200 MSa/s) is one averaging run of N = 2..64 records (largest power of two whose N x (record + dead time) fits 40 ms; dead time = total x 5 ns + 40 ns + (2 + pre) words + 100 ns). The shown record is the mean of the lane sums through the per-core correction (code = round((mean x gain + off) / 65536) from the mean in Q8), so READ_TRACE of a UI capture returns it; the software average is skipped for it. 16-bit records (always 3000+ samples > 2046 words), deeper memory and rates where two records do not fit 40 ms keep the software average (byte 46 = 1). A run not done after max(100 ms, two records) + its run time is stopped (0x5D = 1, 0x1A = 0) once it holds a record (the mean of what it has); in auto mode without any record it ends untriggered and the following UI conversions are single records until one triggers. GET_CAPTURE_INFO / READ_RAW of a UI capture see the ring as before (the last raw record of the run). **SET_SEQ 0x020E** (main loop, cap SEQ). Request 16 bytes: 0 u8 mode (0 off, 1 segments, 2 averaging; else E_BAD_ARG detail 0), 1 u8 flags (bit 0 circular, segments only; else detail 1), 2 u8 m (segments: slot = 2^m words, 5..12; else detail 2), 3 u8 0, 4 u16 count (segments 1 .. 8192 >> m, or averaging positions P with P x total <= 2048; else detail 4), 6 u16 N (averaging: records per position, 0 = 65536; legacy records at most 257, above that the 16-bit lane sums would clamp; else detail 6), 8 u32 hold-off in ns (rounded up to 5 ns units), 12 u32 0. Checked against the geometry the next ARM writes (GET_TRIGGER pretrigger / total and the Filter): segments need total <= 2^m - 2 words (2 more words are written after each window) and pre < total, averaging total <= 2046 words; a misfit there is E_STATE detail 5 (total) / 6 (pre) "SET_TRIGGER first". Mode 0 clears the setting. The setting applies to host ARM only (the UI never runs it) and is not saved. Response: the GET_SEQ body. **GET_SEQ 0x020F** (main loop, cap SEQ), request empty, response 48 bytes: 0 u8 mode, 1 u8 flags, 2 u8 m, 3 u8 0, 4 u16 count, 6 u16 N (0 = 65536), 8 u32 hold-off ns, 12 u32 hold-off 5 ns units written to 0x5C, 16 u8 fit of the setting with the next ARM's geometry (0 fits, 1 mode, 2 flags, 3 m, 4 count, 5 total, 6 pre, 7 N), 17 u8 1 = the next ARM records 16-bit samples, 18 u16 its pretrigger words (0x0B, less the EXT shift in 16-bit), 20 u16 its total words, 22 u16 the largest count for the mode (8192 >> m segments, 2048 / total positions), 24 u8 FPGA 0x2A byte 0 now (bit 7 done, 6 overflow, 5 running, 4 16-bit records, 3 raw, 2 circular, 1..0 mode of the run, 0 = none since the last 0x01), 25 u8 sequencer state (0x2A bits 15..13 of bytes 6-7), 26 u16 next slot / position, 28 u32 count (segments completed or records accumulated), 32 u16 untriggered (segments ended by the auto time-out, or records skipped), 34 u8 1 = the FPGA still holds the latest capture's run (no arm since), 35 u8 0, 36 u32 capture_id of the latest capture if it is a run (else 0), 40 u32 its count at DONE, 44 u16 its untriggered, 46 u8 its mode, 47 u8 its flags (as GET_CAPTURE_INFO 77). **ARM with a setting** (mode != 0) arms a run: E_STATE detail 3 if the setting no longer fits (GET_SEQ byte 16); trigger mode auto arms the FPGA's own time-out 0x0E (counting samples: untriggered segments are recorded with triggered = 0, untriggered records are skipped by averaging) and the firmware auto time-out (row 46, fw 0.2.41) does not apply; normal / single arm normal. DONE (WAIT, GET_STATUS acq_state 3) is the end of the whole run (0x0A); the status is read once then. "triggered" (GET_STATUS flags bit 0, GET_CAPTURE_INFO flags bit 0) = the run holds a triggered segment or at least one record. An averaging run leaves its last record in the ring as a normal capture: the trace buffers are built from it at DONE (the screen shows that record), READ_RAW addr_mode 0-2 and READ_TRACE read it. A segment run builds nothing (the screen keeps its traces); READ_RAW addr_mode 1 / 2 and READ_TRACE answer E_STATE detail 4 / 5 for it (addr_mode 0 still reads the sample memory as it is). **STOP** during a run ends it instead of abandoning it: 0x5D = 1, then 0x1A = 0 (a record waiting for a trigger ends now); acq_state stays ARMED / TRIGGERED until 0x0A = 1, then DONE with flag "stopped" (GET_CAPTURE_INFO 77 bit 1). A circular run ends only this way. **GET_CAPTURE_INFO** grows to 96 bytes: 76 u8 run mode (0 = a single capture), 77 u8 flags (bit 0 circular, bit 1 stopped by STOP, bit 2 a circular run wrapped: 0x2A overflow), 78 u8 m, 79 u8 1 = 16-bit records, 80 u32 count at DONE (segments completed / records accumulated; a circular run counts beyond the slots), 84 u16 untriggered, 86 u16 count requested (segments / positions), 88 u32 N, 92 u32 hold-off ns. **READ_SEQ 0x0210** (main loop, cap SEQ). Request 12 bytes: 0 u8 what (0 segment table, 1 averaged record; else E_BAD_ARG detail 0), 1 u8 ch_mask (what 1: bit 0 CH1, bit 1 CH2, at least one; detail 1), 2 u8 S (what 1: readout shift 0..31, or 0xFF = the sums; detail 2), 3 u8 0 (detail 3), 4 u16 first (entry or word; detail 4), 6 u16 count (>= 1, first + count <= the table slots or P x total words; detail 6), 8 u32 0. Needs the latest capture to be a host run of that kind: E_STATE detail 0 none, 1 armed since (the run is gone), 2 the run is of the other mode. Response: 16-byte header (0 u32 capture_id, 4 u16 first, 6 u16 count, 8 u8 what, 9 u8 format (bit 0 16-bit records, bit 1 u32 sums), 10 u8 m (what 0) or the S applied (what 1; 0xFF for 16-bit sums; legacy 0xFF reads as S 0), 11 u8 ch_mask (what 1), 12 u16 pretrigger words of the run, 14 u16 total words), then the data. What 0: `count` u64 little-endian table entries: bits 39..0 timestamp in 5 ns units from the first write clock after the release of 0x01 to the edge that latched trigger_address (2 (D + 1) per write clock in legacy, D = divider; 10 ns per write clock with K >= 1; 22-32 ns after an EXT edge at 200 MSa/s), 52..40 trigger offset in the slot (trigger_address mod 2^m, the 0x14 convention), 53 triggered (0: the segment ended on the auto time-out, offset = pre mod 2^m), 63 valid (0 = never written, or the slot a circular run was refilling). Slots: count, or 8192 >> m for a circular run (slot s = table entry s). What 1, per selected channel (CH1 first): legacy records: a block of `count` u16 core A lanes (FPGA 0x20 / 0x22 cores) then a block of `count` u16 core B lanes, each = (sum over N records) >> S, the sums exact up to N = 257 (S 0 or 0xFF: the sums); 16-bit records: `count` u16 = min(65535, sum >> S), or with S 0xFF `count` u32 sums (no overflow up to N = 65536). Position p occupies words p x total .. p x total + total - 1 (record k goes to position k mod P). The firmware sets S and the raw bit (0x5B, 0x57 bit 3) for every read, so a record can be read again with another S. Mean in codes: legacy code = lane x 2^S / N (then the per-core correction, GET_CAL, as for READ_RAW); 16-bit: sample = value x 2^S / N, then the row 47 / 49 conversion (gain from GET_DECIM, corr16). Cost: one bus pass per byte, about 0.3 us per byte (a legacy channel of 1500 words: 4 passes, about 1.8 ms). **READ_RAW addr_mode 3** (segment-relative): address = segment << 16 | word offset from that segment's record start (record start = (table offset - pretrigger words) mod 2^m); count 1 .. 2^m words (detail 8), segment < slots (detail 4); E_STATE detail 4 without a held segment run, detail 5 for an entry with valid = 0. Header start_word = the absolute word of the first sample, ring_words = 2^m (the reads wrap at the slot end). Layouts 0, 1, 2 as for a single capture (legacy A/B pairs or 16-bit). **Limits** (fpga-gen5): accumulator 2048 x 64 (P x total <= 2048, total <= 2046 in averaging); segments 2^m x count <= 8192; re-arm time about hold-off + pre x word + 1 to 3 write clocks + 50 ns, averaging adds total x 5 ns of post-pass. Vectors: docs/protocol-vectors.json requests `req_set_seq`, `req_get_seq`, `req_read_seq`, `req_set_seq_average_256`, `req_read_seq_average_raw`, `req_read_raw_segment_5`, responses `resp_get_seq_segments_done`, `resp_read_seq_table`, `resp_get_status_1_17`, and the `seq` maths section (fit, hold-off, entry decode, slot words, UI plan, corrected mean) checked against pc_seq.c by check_vectors.py `--seq-checker`. **Unverified on hardware** (README section 6 bench checks; fw handoff firmware/AGENTS.md section 33): everything here; the readout pipeline of 0x2B..0x32 is read from the Verilog | owner: pulse-induction detector (coherent averaging of the decay after each TX pulse at full rate; segments with timestamps for per-pulse capture), community: multi-event capture, noise reduction; phosphor fast capture (docs/plans/phosphor-fast-capture.md) |
| 57 | Protocol 1.18: full-rate ADC capture check after rate writes, FPGA write trace (v0.2.61; the check is per capture since 1.19, row 58) | GET_INFO `proto_minor` = 18; `#VER` says `proto=1.18`. **Why** (bench 2026-10-19): on AL3 0x1532..0x1536 a 0x0D or 0x53 write can leave one ADC chip's encode clocks half a word out of phase with the RAM write clock (fpga/al3/README.md "Findings" item 3); at 200 MSa/s one core of that chip then returns garbage (CH1 core B 15-29 codes RMS in 27 of 60 rate changes on 0x1535). On 0x1537 static captures are clean, but after a 0x0D write to divider 0 CH2 core A read 32-34 codes in 35 of 40 trials, persisting until the next write (no settling with time). **Firmware**: after any 0x0D write, before the next UI conversion and before a host ARM, and in the calibration wizard before every full-rate measurement, one full-rate capture (500 samples per core, raw) is checked: per channel the RMS of core B against the mean of its two core A neighbours (smaller of the two neighbour choices, about the mean; signal and core offset cancel, noise leaves about 1.3 codes). Above 8 codes the firmware re-writes 0x0D (1, then 0) and checks again, up to 16 times (`enc_heal.h`: EH_LIMIT16, EH_MAX_TRIES, EH_WAIT_MS). Divider 0 only; every bitstream. **GET_LINK_STATS** grows to 64 counters: **62** first checks above the limit, **63** of those recovered. **GET_EVENT_LOG** type **21**: a check found a garbled capture, a = retries (bit 7: not recovered), b = the larger residual of the first check in 1/16 code. Type **22**: an FPGA write of 0x0D, 0x53, 0x56 or 0x01 = 1 (sampling reset), a = command, b = caller << 12 \| value (saturated at 0xFFF); callers 0 UI, 1 host (inside pc_poll), 2 calibration wizard, 3 v4 class probe, 4 the check. Type 22 is in the main ring only (not the bus ring); with the UI running each conversion adds one 0x01 entry, so the 128-entry ring covers a few seconds of UI acquisition: read GET_EVENT_LOG right after the capture under study. The wizard reports a check that never recovered as "ADC capture timing: the 200 MSa/s data stays garbled" (ACAL_E_TIMING 18). |
| 58 | Protocol 1.19: every full-rate capture checked, FPGA 0x1538 encode check (read 0x33), GET_HEALTH 104 bytes (v0.2.62) | GET_INFO `proto_minor` = 19; `#VER` says `proto=1.19`. **Why** (bench 2026-10-19 16:48, fw 0.2.61 on 0x1537): with one 0x0D write and then 1221 host captures in 60 s and no other write, CH2 core A was garbled (14-30 codes) in about 40 % of captures, randomly interleaved with clean ones: 0x1537 re-resolves chip 2's encode phase at every sampling reset (a marginal flop), so 1.18's check, which ran only after a 0x0D write, never fired. fpga-gen7's 0x1538 derives the encodes statelessly from the write clock and reports a per-capture self-check. **Caps bit 18 ENC_CHECK** (AL3 0x1538..0x153F) and features word bit 5 (0x0020, GET_STATUS 44 / GET_DECIM 32). **FPGA read 0x33** (one byte; the 0x33 write is still the CH1 relays): bit 0 sticky "an encode check failed since the last 0x01 = 1"; bit 1 the last write-clock prediction was right; bits 3:2 = 01 encode convention 1, core order guaranteed, B first (`interleave_order` 0); bits 7:4 the last pin check {CH2 A, CH2 B, CH1 A, CH1 B} (bit 7 .. bit 4), 1 = right. A good capture reads 0xF6. An asynchronous 0x0D / 0x53 write can make one prediction miss, which is why bit 0 clears at 0x01 = 1 and why those writes go before the reset. **Firmware**: after every capture at 200 MSa/s (divider 0, always the legacy 8-bit layout), before anything reads it (the UI readout, READ_RAW, the calibration wizard, the v4 class probe, the 1.00o5 calibration and auto setup), the capture is checked: on 0x1538+ by reading 0x33 (bit 0 set = bad); on 0x1532..0x1537 by the 1.18 residual (per channel the RMS of core B against the mean of its two core A neighbours, the smaller of the two neighbour choices, about its mean) over the record's first 250 words, from the same ring the readout reads (1000 bus bytes, about 0.3 ms; no extra conversion), bad above 8 codes. A bad capture is discarded and the conversion re-armed with the same setting (on 0x1532..0x1536 after re-writing 0x0D = 1, then 0: there only a rate write re-rolls the phase), at most 16 times per capture (`enc_heal.h` EH_MAX_TRIES; then the last capture is accepted and flagged). A host ARM therefore stays ARMED / TRIGGERED a little longer; WAIT, GET_CAPTURE_INFO and READ_RAW only ever see the accepted capture; in normal or single mode a re-arm waits for the next trigger. Not checked: other rates, other bitstreams (stock, EF2, EP4), and on 0x1536 / 0x1537 sequencer runs (segments and lane sums are not samples; 0x1538's flag covers a whole run, and a run STOP ended is not restarted). The 1.18 check after 0x0D writes (an extra conversion) is gone. On 0x1538 the firmware also reads 0x33 once at boot and requires bits 3:2 = 01 (else flag bit 1 below and an event). **GET_HEALTH** grows to 104 bytes (`length` 104; a 1.16 host reads the first 96): 88 u8 check mode: 0 none, 1 record data (0x1532..0x1537), 2 FPGA read 0x33 (0x1538+); 89 u8 the last 0x33 byte read after a capture (valid when 90 bit 0); 90 u8 flags: bit 0 a 0x33 byte was read since boot, bit 1 the encode convention at boot was not 1 (0x33 bits 3:2 != 01), bit 2 the last checked capture was accepted bad after 16 re-arms (cleared by the next good one), bit 3 core order differs from the convention (0x1537: recalibrate after flashing 0x1538); 91 u8 0x33 at boot (0x1538+, else 0); 92 u32 captures found bad (= GET_LINK_STATS 62); 96 u32 of those recovered by a re-arm (= 63); 100 u32 full-rate captures checked since boot. **GET_LINK_STATS 62-63** keep their slots, now per capture: 62 captures whose first check failed, 63 of those whose re-arm brought a good one (62 - 63 = accepted bad, or abandoned by a new conversion during the re-arms). **GET_EVENT_LOG type 21** (one entry per capture that needed re-arms, when it is accepted, and at boot): a = reason << 5 \| re-arms (bits 4:0, saturated at 31), bit 7 = not recovered; reasons 0 the 1.18 check after a rate write (no longer logged), 1 data check, 2 FPGA 0x33, 3 boot convention check (a = 0xE0); b = caller << 12 \| value (12 bits, saturated): callers as type 22 (0 UI, 1 host, 2 calibration wizard, 3 v4 class probe, 4 check), value = the first bad capture's larger residual in 1/16 code (reason 1) or its 0x33 byte (reasons 2, 3). Type 22 unchanged, except that the check writes (caller 4) are now only the 0x0D re-writes of a re-arm on 0x1532..0x1536. **Diagnostics** FPGA line: 0x1532..0x1536 amber "ADC capture timing race, fixed in 0x1538"; 0x1537 amber "ADC core order differs: recalibrate after flashing 0x1538"; 0x1538 from GET_HEALTH 88..103: "encode check OK" (with ", N captures re-taken" when 92 > 0), amber "encode check failed on CH2 A, ..." (the pins whose bit is 0) when 89 bit 0 is set, amber "unknown encode convention, 0x33 = 0x.." when 90 bit 1 is set, "encode check, no 200 MSa/s capture yet" before the first. The report file has a "Full-rate capture check" line. Vector: `resp_get_health_1_19`. | owner: on 0x1538 the rate-toggle test and 60 s of back-to-back 200 MSa/s captures show no garbled capture and GET_HEALTH 92 stays 0 (or 62 = 63); on 0x1537 the 40 % arms are re-taken (62 grows, 63 follows) and no READ_RAW returns a garbled core |
| 60 | Protocol 1.20: the on-device Bode plot, BODE_CTRL / READ_BODE, GET_STATUS display_mode 5, features bit 6 (v0.2.66) | GET_INFO `proto_minor` = 20; `#VER` says `proto=1.20`. **Mode** (firmware/README.md "Bode plot"): a main-loop display mode like the spectrum view, opened from the generator menu's fourth row "Bode plot" (owner) and left through the main menu's Scope or Spectrum analyser row (or the same row, "Close"). While it shows, the scope's own acquisition is off and the sweep engine drives the generator (duty 50 %), both channels (enabled, DC, ranges and offsets), the raw divider and the decimation filter; leaving gives back the generator, the channels (range, display V/div, offset, coupling, enable), the time base, the trigger source (EXT is set to CH1 on entry, protocol row 41) and RUN / STOP. Every command keeps working; host captures during a sweep end it (reason 5). **Plan** (docs/plans/bode-design.md 3.1-3.2): identical to the host tool's `plan_point` / `plan_sweep` (mcp/src/fnirsi1013d/bode.py): f = 50 MHz / M for integer M, N = 16 x 2^k with CIC3 + FIR below 1.5 MHz (257..512 samples per period below 24 kHz, M tied to N), legacy 200 MSa/s above, a coherent record of at least 8 periods and 2048 samples with an even number of p-blocks; points log spaced from start to stop (numpy geomspace), merged where M repeats. `host_tests/test_bode.py` checks the C planner against docs/bode-plan-vectors.json and 3000 random points and 300 random sweeps of the host's. **Per point** as the host tool's runner: a pre-scan at the first frequency on range 1 chooses both ranges (85 % of full scale) and centres the offsets; a background capture with the generator off per decimation and CH2 range; the settle time (presets 3 / 10 / 50 / 200 periods, three times at the first point and after the generator was off, at least 1 ms); a capture in FPGA auto mode with pretrigger 0 (no trigger needed: the record's start cancels in H); re-centring beyond 12 codes; one retry with twice the settle when the record halves disagree; CH2 auto-ranging (up on a clip or a saturated, flat record; down when the next range would stay under 60 % of full scale) with the splice measured at the switch point. **BODE_CTRL 0x0211** (main loop, cap DECIM; E_BUSY while a menu is open). Request 16 bytes (zero-extended): u8 action (0 status only, 1 run one sweep, 2 run repeated sweeps (RUN), 3 stop after the point in progress, 4 abort now, 5 measure the thru calibration (one sweep, both probe tips on the DUT input), 6 apply the thru, 7 do not apply it (kept), 8 clear it, 9 open the Bode view (no sweep), 10 close it), u8 flags (bit 0: bytes 2..13 hold the settings for this and later host sweeps, the screen's 1-2-5 steps follow rounded; without it the screen's settings are used; bit 1: no background captures), u8 settle (0 Fast, 1 Normal, 2 Resonant, 3 High Q), u8 CH2 range mode (0 auto, 1 hold: CH2's range when the sweep starts), u32 start cHz, u32 stop cHz (1 Hz .. 10 MHz, either order), u16 points (1..200), u16 0. Actions 1, 2, 5 and 9 open the view first. Errors: E_BAD_ARG detail 0 action, 1 settle, 2 CH2 mode, 3 start / stop, 4 points; E_STATE detail 1 the view is not available (a picture or waveform viewer is open), 2 a sweep runs already (1, 2, 5), 4 no thru measured (6, 7). Response 40 bytes: the READ_BODE header, u32 the estimated duration of a sweep with the settings in effect (ms), u32 0. fw 0.2.71: action 3 (and the screen's STOP) ends the sweep after the capture in progress; that point keeps the capture's result without re-centring, retry or range change (after the whole point before). The sweep runs in the main loop: poll READ_BODE. **READ_BODE 0x0212** (main loop, cap DECIM). Request 8 bytes: u16 first row, u16 count (0 or more than fit = all that fit, 3326), u32 0. Response: the 32-byte header, then 20-byte rows. Header: u32 sweep id (increments with every start; 0 = none since boot), u8 state (0 idle, 1 running, 2 done, 3 stopped, 4 error), u8 reason (0 none, 1 the FPGA did not become ready, 2 no capture within twice the record time + 1 s, also after FORCE, 3 CH2 saturated by a DC level with CH2 range hold, 4 no frequency in the range, 5 the PC armed its own capture during the sweep, 6 stopped, 7 no decimation filter or no DDS generator on this bitstream, 8 (fw 0.2.71) ADC readout fault, the screen says "ADC readout fault: see Diagnostics"; fw 0.2.83: on a bitstream with the capture self-check (caps bit 19, 0x153A+) the self-check flagged a capture of the sweep (0x36 outliers after a 200 MSa/s capture, or GET_HEALTH 106 bit 5 REPEAT), at any phase; without it (0.2.71 rule) the pre-scan reached 5 V/div (range 0) with a channel's record still at code 0 or 255 and its fundamental under 20 codes, which the post's 3.1 Vpp cannot do. The sweep ends rather than produce a table of flagged points. Clipping (row flags bits 0, 1) is judged from each capture's own record since 0.2.83; 0.2.71-0.2.82 also took the readout's sticky overload flag of earlier readouts, so a clean readout could end in reason 8), u8 view (bit 0 the Bode view shows, bit 1 RUN repeats sweeps, bit 2 this sweep is a thru measurement, bits 3-4 CH1 / CH2's offset reached the PWM limit), u8 thru (bits 0-6: 0 none, 1 being measured, 2 valid for the current start / stop / points, 3 stale; bit 7 applied), u16 planned points, u16 measured points, i16 the point being measured (-1 none), u8 settle, u8 CH2 range mode, u32 start cHz, u32 stop cHz, u16 requested points (the latest sweep's settings; fw 0.2.71: before the first sweep since boot, sweep id 0, the settings in effect, the ones the BODE_CTRL estimate and the screen's Single sweep row describe; 0.2.66-0.2.69 showed the engine's defaults there), u16 CH2 range switches, u32 ms since the sweep started (its duration once it ended). Row: u32 M (stimulus period in 50 MHz DDS clocks: f = round(2^32 / M) x 50 MHz / 2^32 exactly; N follows from M by the plan), u32 that f in cHz (rounded), i16 abs H in centi-dB, i16 arg H in centi-degrees (-180 .. +180], i16 V1 and i16 V2 in centi-dBV RMS (the fundamentals at the probe tips, probe factor included; V2 with the splice), u8 CH1 and u8 CH2 hardware range (0..5), u16 flags: bit 0 ch1_clipped, 1 clipped (CH2), 2 ch2_overload (a flat CH2 record: the front end saturated by a DC level), 3 ch1_low (CH1 under 20 codes), 4 ch2_small (CH2 under 3 codes: +-1 dB), 5 low_snr (under 20 dB above the noise), 6 near_floor (under 20 dB above the generator-off background in its bin), 7 unsettled, 8 incoherent (CH1 one bin beside the fundamental above -40 dB), 9 spliced, 10 fir_off, 11 no_cal, 12 cal_range, 13 cal_flagged, 14 retried, 15 not measured in the latest sweep (the rest of the row 0). Bits 0, 1, 2, 5, 6, 7 and 8 make a point invalid (the host's BAD_FLAGS; the screen draws it grey). With the thru applied, H is divided by the thru's H at the same (M, N) (bits 11-13 say where that does not hold). **Measurement** (bode_dsp.c, integer only): mean removed, rectangular window over the coherent record (Hann where the 32-bit increment leaves it more than 1e-3 periods off, as the host), single-bin DFT on a 2^-34 turn phase grid at f exactly, H = X2 / X1 with each channel's scale as its readout converts (decimation scale x gain16, corr16, volts per code of the range, probe), levels in mdB from fx_log2_q16, angles from a CORDIC; host-tested against measure_pair: H within 0.002 dB / 0.002 deg on the simulator's RC. Noise: the median of the period differences x[n] - 2 x[n + p] + x[n + 2p] (white noise as the host's median over FFT bins; a transient spanning most of the record raises it where the host's does not, so such a point reads low_snr rather than unsettled). Markers on the screen by linear interpolation in log f (host: monotone cubic). **GET_STATUS** byte 43 = 5 while the view shows; GET_STATUS / GET_DECIM features word bit 6 (0x0040) = the bitstream has what the Bode plot needs (DECIM and GENERATOR_DDS: AL3 0x1534+). Settings words 219-220 keep start, stop, points, settle and CH2 range mode (markers 0xB000 / 0xB100); the thru calibration lives in RAM. Not built: the harmonic fast mode, impedance mode, AC coupling for CH2 (bode-design D5) | bode-design steps D1-D4; the device is about 2-3 x faster than the host above 1 kHz (no USB round trip per point) and runs without a PC |
| 61 | Protocol 1.21 (provisional numbering): CAL_PROBE, the calibration wizard's zero measurement from the host (v0.2.67) | GET_INFO `proto_minor` = 21; `#VER` says `proto=1.21`. **Why** (bench 2026-10-19 17:55, fw 0.2.64 on 0x1538, nothing connected): the wizard's Zero level step failed every time (CH1 "too much noise, range 1 at 200 MSa/s: core B 7.2 codes, 7.2 per capture", CH2 "the input is not at 0 V" = a sample at code 0 or 255) while host READ_RAW at the same range and rate read 0.6-2.0 codes RMS per core and no clipped sample over 1233 captures. CAL_PROBE runs the wizard's own measurement, so the host can see what the wizard sees without the owner. **CAL_PROBE (0x0402)**, no capability bit (an older firmware answers E_UNKNOWN_CMD). Request: u8 `channel` (0, 1), u8 `range` (hardware range 0..5, 0 = 5 V/div = the wizard's "range 1"), u8 `settle` (optional, 0..15: extra reads of the FPGA data port after each sample strobe before the sampled read, an A/B test of the bus readout), u8 `prestrobe` (optional, 0 or 1: a data-write strobe with the bus undriven before each sample strobe, so the sample memory's read register holds the address before the strobe that takes it; the samples then come one word earlier); both 0 = the wizard's readout; u8 `mode` (optional: 0 one range as described below; 1 the whole zero step exactly as the wizard runs it after a Calibrate tap, in its screen context: the step dialog over the dimmed screen, the progress lines and the Cancel polls, both channels, every range, `range` ignored; the response then carries `channel`'s last measured range, the failing one, or range index 5 when it passed, with rc -1 when it was cancelled on the screen; about 15 s). Main loop only (a BUSY or timer poll leaves it for the main loop); refused with E_STATE while the UI acquisition runs (detail 1: send SET_ACQ_STATE 0 first) or a host capture is ARMED / TRIGGERED (detail 0); E_BAD_ARG for channel > 1 (detail 0), range > 5 (detail 1), settle > 15 (detail 2), prestrobe > 1 (detail 3) or mode > 1 (detail 4). It runs `cal_begin`, the scratch channel of `channel`, the relay write and 60 ms, then `cal_range` (cal_wiz_ui.c: coarse points PWM 500 and 1200 gated, 2 conversions each; slope points centre -+ 150 gated, 4 each; the zero gated as two points d counts either side of the refined centre, 8 each (d from the gated slope so that each is 12 codes from 128, 8..120 counts); then 200 MSa/s: the zero pair, + 150, - 150, 64 each; every conversion 1000 samples, 500 per core, read from word 100), then `cal_end` (channels, rate, raw host divider, time base, trigger mode back). Nothing is stored; the first 1000 bytes of the channel's trace buffer are put back. About 1 s (mode 1 about 15 s); the host should allow 5 s (30 s). The session's modal watch restarts after it, so the next request is answered at once (the 0.2.67 diagnostic image answered E_BUSY "UI busy" right after a probe). Response, 252 bytes, little-endian: 0 u8 channel; 1 u8 range; 2 i16 rc (ACAL_* of adc_cal.h: 0 OK, 4 RESPONSE, 5 CLIP, 6 NOISE, 7 ZERO, 8 SPREAD, 9 SLOPE, 10 DRIFT, 11 CLASS, 18 TIMING, 19 READOUT: samples at 0 / 255 only at 200 MSa/s with the gated twin point quiet and within 8 codes of 128, the FPGA 0x1538 readout fault); 4 u8 fail point (0xFF none); 5 u8 fail class (0 full rate, 1 gated); 6 u8 fail core (0 A, 1 B); 7 u8 flags (bit 0 a full-rate capture was accepted garbled after the re-arms, bit 1 the coarse centre failed: no fine points, bit 2 prestrobe used, bit 3 mode 1, bits 7:4 the settle reads used); 8 i32 fail value (as the wizard's `acal_fail`: noise / drift 1/16 code, slope Q16, ...); 12 u32 within-capture RMS of the failing value, 1/16 code; 16 u16 coarse centre PWM (0 none); 18 u16 refined zero PWM (0 none); 20 u16 full-rate captures flagged by the encode check during the probe (GET_HEALTH 92 delta); 22 u16 of those recovered (96 delta); 24 + 28 i: point i = 0 coarse high (PWM 500), 1 coarse low (1200), 2 gated slope low (centre - 150), 3 gated slope high, 4 gated zero (the pair, merged: PWM the centre, mean the mean of the two, RMS values the RMS of the two points' own, extremes and clipped of both), 5 full-rate zero (the same), 6 full-rate high, 7 full-rate low: +0 u16 PWM, +2 u8 class, +3 u8 conversions (0 = not measured), then per core A at +4 and B at +16: i32 mean (1/16 code), u16 RMS about the mean of all its conversions (1/16 code, the value the noise limit of 48 checks), u16 RMS inside the conversions (1/16 code), u8 lowest code, u8 highest code, u16 samples at code 0 or 255; 248 u16 the zero split d in PWM counts (0 when not reached); 250 u16 0. The packer is `acal_probe_pack` (adc_cal.c, host test test_adc_cal `test_points`). The wizard's failure line got the same numbers as a note: range, rate, point, PWM, per-core mean with lowest..highest code and clipped samples (`cw_zero_note`). |
| 62 | Protocol 1.22 (provisional numbering): the FPGA bus contract, legacy EXT shift, FPGA write counters and the type 22 trace level (v0.2.72) | GET_INFO `proto_minor` = 22; `#VER` says `proto=1.22`. **Why**: fpga-gen6's contract findings on 0x1532..0x1536 (fpga/al3/README.md section 7, findings 2, 4 and 5) and the 0.2.62 bench on 0x1538 (a host rate write set the 0x33 sticky bit and cost a re-arm: 112 of 5798 captures; every UI conversion's 0x01 entry of type 22 flooded the 128-entry event ring). **Readout** (firmware/AGENTS.md section 48, `fpga_bus.c`): every reader starts with 0x1F, the read command and the bitstream's dummy strobes (one on every known bitstream, 0x1539 included). The UI readout, and with it **READ_TRACE**, the spectrum and the 16-bit trace, read mem[S], mem[S], mem[S + 1], ... up to v0.2.69: one word (two samples) late with a stale first byte; since 1.22 they return the same words as READ_RAW (section 7.4). The UI record start is (0x14 - the 0x0B words) mod the ring (1.00o5 wrapped with ring - 751, one word off on stock and (751 - pretrigger) words off on AL3 / EF2 when 0x14 lay within the pretrigger of the ring start). **Arm**: 0x0B / 0x0C are written before 0x01 = 1 as well as inside the window, on every bitstream that takes them (0x1532..0x1536 kept the previous pretrigger when no write clock edge came between a 0x0B written after the 0x05 poll and the release). **EXT in the legacy layout**: the AD9288 pipeline delays the data 4 words behind an EXT trigger; the firmware writes (pretrigger - shift) / 2 words, shift = 8 samples (4 at divider 0), at most the pretrigger in whole words; GET_CAPTURE_INFO 70 and GET_DECIM 31 now report it for K = 0 too (samples; the record start at 12 already includes it). Channel enable toggles keep an EXT source. **GET_LINK_STATS** grows to 68 counters: **64** FPGA 0x0D (rate) writes, **65** 0x53 / 0x56 (filter) writes, **66** 0x01 = 1 sampling resets (UI, host, calibration, probe and re-arms alike), **67** full-rate captures on 0x1538+ judged by their record data instead of read 0x33 because a 0x0D / 0x53 / 0x56 write came after their sampling reset (0x33 bit 0 is sticky until the next 0x01 = 1, so it cannot judge them; such a capture is still checked, by the 1.18 residual, and re-armed only when its data are garbled; event 21 reason 1 then). **GET_EVENT_LOG** request flags bits 2:1: 0 leave the FPGA write trace as it is, 1 off, 2 on, 3 E_BAD_ARG; bit 0 as before. Event type 22 is logged only while the trace is on, which it is not after boot. The response's u16 at byte 2 (0 before) has bit 0 = trace on. Vectors: `resp_get_link_stats_1_22`, `req_get_event_log_fpga_trace_on`. **Core order** (row 58): caps bit 12 INTERLEAVE_UNVERIFIED is clear on 0x1538+ when read 0x33 at boot had bits 3:2 = 01 (the bitstream reports its encode convention, B first); otherwise it stays set, and on a 0x1538+ with another convention (GET_HEALTH 90 bit 1) the full-rate check also moves to the record data (GET_HEALTH 88 = 1): nothing assumes the order there. | owner: firmware/AGENTS.md section 48 bench list |
| 63 | Protocol 1.23 (provisional numbering): host requests close the modal screens instead of E_BUSY (v0.2.78) | GET_INFO `proto_minor` = 23; `#VER` says `proto=1.23`. **Why** (owner 2026-10-19 23:27): "avoid locking up the device from the perspective of the MCP server when it's in menus; if the agent sends commands it just exits all modals, since that wouldn't be destructive; just exiting to the base screen". **Rule** (`docs/ui-guidelines.md` section 4, firmware/AGENTS.md section 53): a request that needs the main loop (not in the F_BUSY set that the timer interrupt and the touch scans answer: PING, GET_INFO, GET_STATUS, SET_REMOTE, GET_BATTERY, GET_LINK_STATS, GET_BAD_FRAME, GET_EVENT_LOG, GET_HEALTH, GET_CAPTURE_INFO, SD_STAGE) while the UI is modal (no main-loop pass for 250 ms, GET_STATUS `ui_busy` = 1) is no longer answered E_BUSY. The receiver (timer or touch-scan poll) sets a yield flag and keeps the request queued; every modal loop checks the flag once per iteration and leaves by its own cancel path (a menu or fold-out closes, a confirmation answers Cancel / No, Settings goes Back, the file browser and viewers Return, a file message is dismissed, the first-run screen answers Later, the calibration wizard and the volts calibration Cancel between steps, nothing written), nested ones one after the other, down to the base screen; the main loop then answers the request as usual, silently (fw 0.2.78-0.2.80 showed "Host command: menu closed" for 4 s; fw 0.2.81 dropped the note, owner 2026-10-20). The yield never takes a path that writes something the person did not confirm. Diagnostics, the spectrum and the Frequency response views are main-loop modes and stay open. BODE_CTRL (all actions) and READ_BODE run on the main loop, so they close a menu too. **E_BUSY now** (detail and text, section 4.2): 0 "deferred request pending" as before; 1 "UI capture running" (READ_RAW, as before); 2 "UI did not return to the main screen" (no main-loop pass within 1.5 s of the yield request: auto set, a slow capture); 3 "calibration measurement running" (a wizard step or its Save, a volts calibration step; the step runs to its end); 4 "file save in progress" (SAVE PIC, SAVE WAVE, the Frequency response CSV, the Diagnostics report); 5 "USB drive mode" (the serial port is gone there anyway). Before 1.23 a modal UI gave detail 0 "UI busy": hosts show "close the menu on the scope" only for proto_minor < 23. **GET_LINK_STATS** grows to 70 counters: **68** requests that asked the modal UI to yield, **69** E_BUSY answers instead (a hold reason or the time-out). GET_STATUS `ui_busy` is unchanged (1 while modal, also while a yield is under way). Vectors: `resp_get_link_stats_1_23`, `resp_e_busy_cal_1_23`. | owner: firmware/AGENTS.md section 53 bench list |
| 64 | Protocol 1.24: FPGA 0x153A capture stages, training, self-check and the stored value; GET_HEALTH 124 bytes, GET_CAL 364 bytes, event 23, the EXT shifts on 0x153A, the wizard's zero back at code 128 (v0.2.79) | GET_INFO `proto_minor` = 24; `#VER` says `proto=1.24`. **Why** (bench 2026-10-19, section 5.1 "0x153A"): every build 0x1532..0x1539 sampled the ADC pins at an instant set by placement and garbled some core near code 128 (0x1538 CH2 A + B, 0x1539 both A cores); fpga-gen8 / gen9's 0x153A takes every pin in an IOB register and picks one of six 2.5 ns stages per core (write 0x63), and counts outliers per core (read 0x36), so the firmware can choose the stage on the unit and check every full-rate capture. **Caps bit 19 CAPTURE_STAGES** (AL3 0x153A..0x153F) and features word bit 7 (0x0080, GET_STATUS 44 / GET_DECIM 32). **Boot** (and after a calibration load): 0x64 = 8 (outlier threshold, codes) and 0x63 = the stored value when it was trained on the running bitstream (its FPGA version word), else the power-on 0x08E4 (CH1 A4 B4, CH2 A3 B4); always written, since a FEL load leaves the FPGA as the previous firmware left it. **Self-check**: after every accepted 200 MSa/s capture (UI, wizard, host ARM; after the encode check of row 58) the firmware reads 0x36; a core is flagged when its count minus its chip partner's is above 8 (a signal moves both cores of a chip alike; inference, not benched: a large signal near 100 MHz could make them differ by itself). Nothing is re-armed. **Training** (calibration wizard, zero step, first thing per channel on 0x153A): range 0, the input parked at code 128 by the gated centre search, a quiet input required (gated within-capture RMS <= 3 codes, else ACAL_E_NOISE); per core stages 2..5 in turn, one 8000-word 200 MSa/s capture each, 0x36 read (a clean stage reads 0); per core the clean stage of {3, 4} whose outer neighbour (2 or 5) is clean too (the power-on one of the two when the clean run is symmetric), else the clean one of {3, 4}, else a clean reserve (2, 5); a B core with no clean stage is measured again with its chip's reference bit (0x63 bit 12 / 13, A-encode timing); **fw 0.2.81 (band training, wire unchanged)**: instead of one capture at the centre, the quiet input is parked by the offset PWM at codes 120, 124, .. 140 (interpolated from the centre search's gated slope points) with two 8000-word captures per stage and point, the 0x36 counts summed per core and stage (clean = sum 0; threshold 0x64 = 32 since 0.2.80), and per core the clean stage inside the widest run of clean stages with the most clean stages on both sides is taken, 3 or 4 before a reserve, the power-on stage only as the tie-break inside that run (bench 2026-10-20: CH1 A4 garbled at codes 127..135 only, CH1 A clean at 2, 3, 5 now gives 3); a core with no clean stage fails the channel with ACAL_E_READOUT, "ADC readout fault near code 128: check the FPGA bitstream" (nothing stored). A passed channel's note reads "Capture stages: CH1 A4 B4". Save stores the value; Cancel and CAL_PROBE restore the previous 0x63. **Stored** in sector 708 bytes 488..503 (after the provenance trailer, outside the v3 block; an older firmware's 708 save clears it): u32 'ACAP' (0x50414341), u16 version 1, u16 length 16, u16 the 0x63 value, u16 the FPGA version word it was trained on, u32 zlib.crc32 of the 12 bytes before. **GET_HEALTH** grows to 124 bytes, byte 0 layout = 2 (was 1), byte 2 length 124 (a 1.19 host reads the first 104): 104 u16 the 0x63 value in effect (0 without the stages); 106 u8 flags: bit 0 HAS (0x153A+), bit 1 TRAINED (the stored value for this bitstream is in effect), bit 2 READ (counts read after a full-rate capture since boot), bit 3 FLAGGED (the last read flagged a core), bit 4 OTHER (a stored value exists for another bitstream, not applied: recalibrate), bit 5 REPEAT (fw 0.2.81, GET_HEALTH only: a core was flagged in 3 captures within a minute since the last saved training: recalibrate; cleared by the wizard's Save; 0.2.79-0.2.80 send 0); 107 u8 cores flagged by the last read (bit 0 CH1 A, 1 CH1 B, 2 CH2 A, 3 CH2 B); 108 u16[4] the last 0x36 counts CH1 A, CH1 B, CH2 A, CH2 B; 116 u32 full-rate captures whose counts were read; 120 u32 of those with a flagged core. **GET_CAL** grows to 364 bytes (the same for both channels): 356 u8 trailer state (0 none, 1 valid, 2 bad: magic present, version, length or CRC wrong); 357 u8 flags (HAS, TRAINED, OTHER as GET_HEALTH 106); 358 u16 the stored 0x63 value and 360 u16 the FPGA version word it was trained on (0 unless valid); 362 u16 the 0x63 value in effect. **GET_EVENT_LOG type 23**: a bits 3:0 = the cores flagged by the self-check, b = the largest excess (0.2.79-0.2.80: logged when the set of flagged cores changes; fw 0.2.81: logged once per core when that core has been flagged in 3 captures within a minute, a = the cores that reached it with this capture, and again only after a saved training); a bit 4 set = a training ended: bits 3:0 the cores without a clean stage, b = the 0x63 value in effect. **EXT** on 0x153A (the IOB capture and the stage pick delay the data, not EXT): legacy layout 12 samples at divider 0 and 14 above (0x1539 and older: 4 / 8), K >= 1 lag + 40 ns (GET_CAPTURE_INFO 70, GET_DECIM 31); 0x53 bits 3 / 4 (NEG_A / NEG_B) are written 0 on 0x153A (they select nothing). **CAL_PROBE** (row 61): byte 248 (the zero split d) is always 0: the wizard measures the zero at the centre PWM again on every bitstream (0.2.67's two-point off-centre zero removed, owner 2026-10-19 20:30: a bad bitstream must fail loudly). **Diagnostics** Calibration page and the report file name the stages, trained or not, and a flagged core. Vector: `resp_get_health_1_24`. | owner: firmware/AGENTS.md section 54 bench list (flash 0x153A first) |

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
- On USB bus reset or disconnect (and up to v0.2.43 suspend, row 50) the transport flushes the EP2 FIFOs itself (v0.1.2): the 1.00o5 reset
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
- Shared control hooks (v0.2.0): `fpga_do_conversion` ends with
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
