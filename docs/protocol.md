# FNIRSI 1013D PC control protocol

Status: the design of 2026-10-18 (sections 1-13) as implemented. The firmware speaks protocol 1.24 (fw 0.2.102;
`PC_PROTO_MINOR` in `firmware/scope/pc_protocol.h`, `PROTO_MINOR` in `mcp/src/fnirsi1013d/protocol.py`); every change
after the draft is recorded in section 14, rows 26-64, one row per minor version, and wins where a table in sections
1-13 differs (the GET_INFO, GET_STATUS and GET_CAL tables in section 8 show the 1.0 layout with notes on the later
growth). Firmware design: `docs/plans/firmware-usb-protocol.md`. MCP tool surface built on top: `docs/mcp-tools.md`.

Markers used throughout:

- **[V]** verified in source (file and line given; `S100` = Atlan4 v1.00o5 tree
  `research/atlan4-extracted/_dso1xxx/v1.00o5/fnirsi_1013d_scope/`, `S27T` = Atlan4 v0.27t tree, `zaklad.v` =
  Atlan4 AL3 Verilog as digested in `research/notes/fpga-verilog-analysis.md`).
- **[M]** measured by someone else (source note given), not yet on the maintainer's 1013D.
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

### 5.1 The one bitstream the firmware runs on

Version word from FPGA command 0x06 (two bytes, high first) [V `S100 fpga_control.c:196-209`, `zaklad.v` :847-855].
**Since protocol 1.25 (fw-one, owner decision 2026-10-20) every firmware version supports exactly one bitstream, the
project's latest** (`PC_FW_FPGA_VERSION` in `firmware/scope/pc_cmds.h`, 0x153C since protocol 1.26, 0x153B in 1.25;
fpga/al3/dist/manifest.json "recommended"). The firmware reads the word once at boot and compares it with that constant;
nothing else is derived from it. The capability word, the geometry and every feature decision are the firmware's own:

| Field | Value on every firmware (1.25) |
|---|---|
| `fpga_variant` (GET_INFO 10) | 2 (the AL3A10 wire encoding of the old variant table; 1 stock, 3 EF2, 4 EP4 are no longer produced) |
| `ring_words` (12) | 8192 |
| `max_total_samples` (28) / `min_total_samples` (30) | 16382 / 80 |
| `caps` (20) | PROG_TRIGGER_POS, GENERATOR_DDS, EXT_TRIGGER, DECIM, FIR, SEQ, ENC_CHECK, CAPTURE_STAGES, ENTER_FEL, SHARED_MODE, SD_SECTORS, SCREENSHOT, READ_LAG_UNVERIFIED, since 1.26 HOLDOFF and TRIG_TYPES (0x153C); INTERLEAVE_UNVERIFIED clear when the boot read of 0x33 reported encode convention 1 (row 62); DEBUG_RAW and FORCE_TRIGGER in PC_DEBUG builds |
| `fw_fpga_version` (76) | the word the firmware is built for |
| `fpga_flags` (78) bit 0 | **FPGA_MISMATCH**: the word read (8) differs from 76 (older or newer) |

On a mismatch the scope boots into a full-screen notice instead of the scope (the word needed, the word found, "flash it
as the guide says, or remove the card to run the stock firmware"; Diagnostics, Settings and the USB drive stay reachable),
the acquisition never starts, and every command that is not in the basic set answers **E_UNSUPPORTED detail 1** with the
text "FPGA bitstream mismatch: this firmware needs 0x153C" (the built-for word, `PC_FW_FPGA_VERSION_TEXT`). The basic set
keeps the link, the diagnostics and the way out:
PING, GET_INFO, GET_STATUS, GET_BATTERY, TEST_PATTERN, GET_LINK_STATS, GET_BAD_FRAME, GET_EVENT_LOG, GET_HEALTH,
READ_SPECTRUM, SD_INFO / SD_READ_SECTORS / SD_STAGE / SD_WRITE_SECTORS, SAVE_REPORT, GET_CAL, READ_SCREEN, FFT_BENCH,
ENTER_FEL. GET_HEALTH 124..127 repeats the two fields (row 65). A host shows the mismatch as a caveat and does not try to
work around it: the next bitstream ships with its own firmware.

Up to fw 0.2.102 the firmware ran on the stock 0x1432, Atlan4's 0x1532 / 0x1632 (EF2) / 0x1732 (EP4) and every AL3 minor
0x1533..0x153B with capabilities per word (`pc_variant_from_version`), the encode-heal and core-order workarounds for
0x1532..0x1537 and the stop geometry for 0x1532..0x153A (section 7.5). That code is gone; the notes below describe what the
AL3 line's bitstreams changed, as the record of why 0x153B's interface is what it is.

**History of the AL3 line.** AL3 minor versions (variant 2, decoded as `(v & 0xFFF0) == 0x1530`, v >= 0x1532): 0x1533 EXT trigger, 0x1534 decimation
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
  B) or with a metastable clock, differently from arm to arm. On the maintainer's unit that was CH2 A.
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

**0x153B** (fpga/al3/README.md "Changes" section 11; flashed on the owner's unit 2026-10-20 19:31) has the interface and
feature bits of 0x153A (no new caps bit) and changes only the end of an auto capture: an untriggered time-out or forced
stop latches 0x14 = W - total + pretrigger (the record is the newest total words), the time-out ends only an untriggered
capture, and no trigger is taken after the stop. fw 0.2.97..0.2.102 told it from 0x1532..0x153A by the version word
(section 7.5 "the stop geometry"); since fw-one it is the only bitstream the firmware runs on, and a forced stop (0x1A =
0 at the firmware's auto deadline) is simply a contiguous record.

## 6. Control model

### 6.1 LOCAL / REMOTE ownership [D]

> **Protocol 1.3 (firmware 0.2.0) replaces 6.1 and 6.2 with shared control** (section 14, rows 30-38;
> `docs/plans/collaborative-mode.md`). There is no LOCAL/REMOTE: the UI loop always runs, host SET_* change the
> live UI settings, ARM is a host single capture, and SET_REMOTE / `#LOCAL` are accepted as no-ops. Hosts detect
> 1.3 by the SHARED_MODE capability bit and use the 1.2 model below without it.

The scope firmware is single-threaded: one main loop runs battery check, acquisition, drawing and touch
[V `S100 fnirsi_1013d_scope.c:250-384`]. The FPGA has one bus and no arbitration. So exactly one maintainer drives
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
  - **The forced stop splices the record on 0x1532..0x153A** (bench 2026-10-20 12:32, fpga/al3/README.md section 11):
    the FPGA's time-out branch latches 0x14 = pretrigger, so the record read is ring words 0 .. total - 1, while the
    write pointer has gone round the ring for the whole deadline and stopped at a random word W; whenever W < total
    the words after W are one ring older than the words before it (probability total / 8192 per record, 15 of 32 on the
    bench). Bitstream 0x153B reports 0x14 = W - total + pretrigger on a time-out or forced stop (the record start is
    then the newest total words), completes a triggered record instead of cutting it at the time-out, and takes no
    trigger after the stop (0x18 is the record's). Firmware on 0x1532..0x153A should arm FPGA auto with 0x0E =
    8192 k + total - 2 (the stop at ring word total) instead of forcing; details in that README section.
  - **fw 0.2.97: the stop geometry** (history: removed by fw-one, protocol 1.25, with the 0x1532..0x153A support; on 0x153B a forced stop reads the newest total words and nothing is flagged; firmware/AGENTS.md section 64, `pc_decim.h` of fw 0.2.97..0.2.102 "Auto-mode stop geometry"). 0x0E is
    32 bit and counts written words at every decimation K (`time_base_cnt` advances on `swc_adv`); the stop comes
    after W = 0x0E + 2 words. On 0x1532..0x153A every auto capture (host ARM with UI mode 0, the UI's auto mode, the
    Frequency response engine) arms the FPGA's own auto mode with 0x0E = W - 2, W the first word at or after the
    deadline (max(100 ms, two records, the explicit `auto_timeout`), converted to words; the engine: two records) with
    total + 4 <= W mod 8192 <= 8187: the stop then leaves ring words 0 .. total - 1 to the last pass, so an untriggered
    record is contiguous. The deadline overshoots by at most total + 8 words (a fraction of a ms at fast rates, up to
    one record at the slowest). A UI FPGA-auto conversion outside the auto mode (calibration, auto setup) gets its
    0x0E moved the same way when 1.00o5's `samplecount` would splice; a host segment run in auto mode moves 0x0E within
    the 2^m slot (inference from the shared reset of `sample_write_address` and `time_base_cnt` per segment; not
    benched). Triggered := 0x18 && 0x14 != pretrigger (an edge after the stop sets 0x18 but leaves 0x14 at the
    pretrigger; a real trigger on exactly that ring word, 1 in 8192, is reported untriggered with a correct record).
    The firmware time-out stays as a safety net (W's time + 50 ms + 1/8): if it fires it writes 0x0E = 0 and 0x1A = 0
    and flags the record as maybe spliced (GET_CAPTURE_INFO flags bit 6). A trigger in the last total - pre words
    before W is still cut by the stop; the firmware flags it too when W's ring word lies inside the record and DONE came
    within that window of W's time. 0x153B..0x153F keep the 0.2.41 arm (normal, firmware force): their forced record is
    the newest total words, never flagged. EF2 / EP4 keep it as well, with every forced record flagged. Normal and
    single mode are unchanged.
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
| 0x0105 | SET_TRIGGER | no | - | no | source, edge, level, mode, pre-trigger, total, hold-off (accepted since 1.26, 5 ns units) |
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
| 0x020D | SET_DECIM | (1.9) | DECIM | no | decimation filter setting, the UI's acquisition Filter (row 47; FIR flag 1.11, row 49; filter 4 = peak detect 1.26, row 66) |
| 0x020E | SET_SEQ | (1.17) | SEQ | no | sequencer setting for host ARM: segments or hardware averaging (row 56) |
| 0x020F | GET_SEQ | (1.17) | SEQ | no | sequencer setting, fit, FPGA status, latest run (row 56) |
| 0x0210 | READ_SEQ | (1.17) | SEQ | no | segment table or averaged record of the latest run (row 56) |
| 0x0211 | BODE_CTRL | (1.20) | DECIM | no | on-device Bode sweep: settings, run / stop, thru calibration, the view (row 60) |
| 0x0212 | READ_BODE | (1.20) | DECIM | no | the Bode table of the latest sweep (row 60) |
| 0x0213 | GET_COUNTER | (1.26) | COUNTER | no | the FPGA 0x153C hardware counter's latest result (E, T, W, min, max, frequency), or with `what` = 1 its configuration (gate, source, level, hysteresis) then the result (row 66, fw-c7); answered inside menus |
| 0x0214 | LOG_START | (1.27) | LOGGER | no | start the FPGA 0x153D streaming logger: channels, format, divider, duration; the main loop becomes the log loop (row 67, fw-d4) |
| 0x0215 | LOG_READ | (1.27) | LOGGER | no | the next chunk of the log from the device's 8 MiB DDR ring, at once (row 67) |
| 0x0216 | LOG_STOP | (1.27) | LOGGER | no | stop the log (the FPGA's tail is read), the summary (row 67) |
| 0x0217 | SET_PATTERN | (1.27) | PATTERN_GEN | no | the pattern generator: up to 8 steps (level, marker, duration in 5 ns clocks + 1/65536 fraction), mode (off, single burst, R bursts, continuous), idle level, gap, start source, commit, start (row 67; FPGA 0x153D) |
| 0x0219 | SET_TRIGGER2 | (1.26) | TRIG_TYPES | no | trigger type (edge, pulse width, Nth edge, timeout), polarity, source (1.27: 3 = the pattern's marker), N, H, level, hold-off (row 66; 0x0214..0x0216 are the contract's LOG_*, 0x0218 stays free) |
| 0x021A | SET_PATTERN_TRIGGER | (1.27) | PATTERN_TRIGGER | no | the pattern generator's own trigger engine: the SET_TRIGGER2 body, armed, re-arm (row 67; FPGA 0x153D) |
| 0x0500 | SD_INFO | (1.3) | SD_SECTORS | no | card, partition, write window, eGON headers (section 14 row 40) |
| 0x0501 | SD_READ_SECTORS | (1.3) | SD_SECTORS | no | up to 128 sectors |
| 0x0502 | SD_STAGE | (1.3) | SD_SECTORS | no | bytes into the 64 KiB write staging buffer |
| 0x0503 | SD_WRITE_SECTORS | (1.3) | SD_SECTORS | no | staged sectors to the card, verified |
| 0x0504 | SAVE_REPORT | (1.16) | SD_SECTORS | no | write the diagnostics report text file to the card (row 55) |
| 0x0300 | GET_GEN | yes | - | no | generator state (1.27: 40 bytes, the pattern status and P16 / H16, row 67) |
| 0x0301 | SET_GEN | no | GENERATOR | no | enable, frequency, duty (1.27: raw modes P16 / H16 on the pattern generator, row 67) |
| 0x0400 | GET_CAL | yes | - | no | calibration constants |
| 0x0401 | READ_SCREEN | yes | SCREENSHOT | no | framebuffer rows; built in fw 0.2.70 (cap set on every known bitstream; before: E_UNSUPPORTED, row 18) |
| 0x0402 | CAL_PROBE | (1.21) | - | no | the calibration wizard's zero measurement of one channel and range, nothing stored (section 14 row 61) |
| 0x0F00 | DEBUG_FPGA | no | DEBUG_RAW | no | raw FPGA bus operations (debug builds only) |
| 0x0F01 | ENTER_FEL | no | ENTER_FEL | no | reboot into the boot ROM's FEL mode |
| 0x0F02 | FFT_BENCH | (1.6) | - | no | time the on-scope FFT (section 14 row 44) |

Unknown cmd: `E_UNKNOWN_CMD`. LOCAL-only restriction violated: `E_NOT_REMOTE`. UI modal: `E_BUSY` before 1.23; since
1.23 the modal screens close by their cancel paths and the request runs, with `E_BUSY` detail 2-5 only where the UI
cannot yield (section 14 row 63).

### 8.2 System commands

**PING (0x0001).** Request: 0..492 arbitrary bytes. Response: the same bytes.

**GET_INFO (0x0002).** Request: empty. Response (80 bytes since 1.25, row 65; 76 up to 1.24):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | proto_major | 1 |
| 1 | u8 | proto_minor | 0 in the draft; 26 as of fw-c5 (section 14 rows 26-66, one row per minor) |
| 2 | u16 | max_request_frame | 512 |
| 4 | u32 | max_response_payload | 66560 [D]: 64 KiB of samples plus headers |
| 8 | u16 | fpga_version | raw word from 0x06, as read (e.g. 0x153B) |
| 10 | u8 | fpga_variant | 2 (section 5.1; the wire encoding of the old variant table) |
| 11 | u8 | adc_bits | 8 |
| 12 | u16 | ring_words | per core: 8192 |
| 14 | u8 | channels | 2 |
| 15 | u8 | cores_per_channel | 2 |
| 16 | u32 | base_clock_hz | 200000000; dt = (N+1) / base_clock_hz |
| 20 | u32 | caps | bit field below |
| 24 | u32 | gen_clock_hz | 200000000 since 1.27 (the pattern generator's 5 ns clock, row 67); 50000000 (the DDS) up to 1.26 |
| 28 | u16 | max_total_samples | per channel: 16382 |
| 30 | u16 | min_total_samples | 80 [V pc_cmds.c GET_INFO handler; Donwulff: "minimum total 80"] |
| 32 | u8 | read_lag | 0 or 1, section 7.4 |
| 33 | u8 | n_ranges | 7 (0..6) |
| 34 | u8 | n_hw_ranges | 6 (relay codes 0..5) |
| 35 | u8 | interleave_order | 0 = B before A (section 7.3), 1 = A before B |
| 36 | char[16] | fw_version | NUL-padded, e.g. `0.1.0` |
| 52 | u32 | fw_build | first 8 hex digits of the git commit |
| 56 | char[16] | serial | NUL-padded; the USB iSerial string |
| 72 | u16 | screen_width | 800 |
| 74 | u16 | screen_height | 480 |
| 76 | u16 | fw_fpga_version | 1.25: the FPGA version word this firmware is built for (`PC_FW_FPGA_VERSION`; 0x153B in 1.25, 0x153C since 1.26) |
| 78 | u8 | fpga_flags | 1.25: bit 0 FPGA_MISMATCH = byte 8 differs from 76 (section 5.1); bits 1-7 0 |
| 79 | u8 | reserved | 0 |

`caps` bits (since 1.25 the word is the firmware's, not the bitstream's: section 5.1 lists the fixed set; the "set when"
column below is the history of each bit):

| Bit | Name | Set when |
|---|---|---|
| 0 | PROG_TRIGGER_POS | FPGA has 0x0B/0x0C (variants 2-4) |
| 1 | GENERATOR (was GENERATOR_DDS) | a programmable square generator exists; since 1.27 (FPGA 0x153D) it is the pattern generator's two-step square and SET_GEN's raw modes are P16 / H16 (row 67); up to 1.26 the DDS on 0x50-0x52 |
| 2 | HOLDOFF | 0x1B is the trigger hold-off in 5 ns units: SET_TRIGGER `holdoff` and SET_TRIGGER2 `holdoff` are accepted (1.26, row 66; FPGA 0x153C). 0 up to 1.25: Atlan4 stored the value and never used it [V zaklad.v table, "stored, never used"] |
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
| 20 | TRIG_TYPES | trigger types pulse width, Nth edge and timeout (FPGA 0x5E / 0x5F / 0x60, reads 0x34 / 0x37): SET_TRIGGER2, GET_TRIGGER 52 bytes, GET_STATUS 52, GET_CAPTURE_INFO 112 (1.26, row 66; FPGA 0x153C) |
| 21 | PEAK_DETECT | peak-detect acquisition (FPGA 0x56 bit 1, 0x29 byte 1 bits 7 / 6): SET_DECIM filter 4, READ_RAW layout 3, GET_CAPTURE_INFO 79 bit 1, the shift byte's peak bits (1.26, row 66 "Peak detect"; FPGA 0x153C, fw-c6) |
| 22 | COUNTER | the hardware counter with min / max (FPGA 0x61 / 0x62, read 0x35): GET_COUNTER; the timing cards take its values (1.26, row 66, fw-c7; FPGA 0x153C) |
| 23 | LOGGER | the streaming logger (FPGA 0x69 / 0x6A, read 0x38): LOG_START / LOG_READ / LOG_STOP, GET_STATUS acq_state 5 (1.27, row 67, fw-d4; FPGA 0x153D) |
| 24 | PATTERN_GEN | the pattern generator (FPGA 0x6C-0x6F, read 0x39), capture trigger source 3 (the marker): SET_PATTERN; "the generator is the pattern" (1.27, row 67; FPGA 0x153D) |
| 25 | reserved | 0 (was DDS_SWEEP in a draft: sweeps are host-stepped SET_GEN writes) |
| 26 | (TRIG_RUNT_WINDOW) | 0x153E |
| 27 | PATTERN_TRIGGER | the pattern trigger engine (FPGA 0x76-0x7D, read 0x42, 0x6D bits 7..6): SET_PATTERN_TRIGGER (1.27, row 67; FPGA 0x153D) |
| 28-31 | reserved | 0 (bit 4 TRIG_BOTH_CORES is 0x153E) |

**GET_STATUS (0x0003).** Request: empty. Response (28 bytes in the 1.0 draft, 32 bytes in 1.1, 44 bytes since 1.3, 48 bytes since 1.17, 52 bytes since 1.26).
Also the response body of SET_REMOTE, ARM, FORCE, STOP and WAIT. The table is the 1.0 layout; the appended fields
(`pass_ms` at 28, `settings_seq` at 32, `run_state` at 40, `capture_origin` at 41, `ext_flags` at 42 and
`display_mode` at 43, values 0 scope, 1 spectrum, 2 waterfall, 3 density, 4 phosphor, 5 Frequency response (the on-device Bode plot;
1.20, row 60; the screen's name since fw 0.2.69);
the features word, UI Average state and log2 N at 44..47) are in section 14 rows 26, 32, 41, 45, 52 and 56;
features bit 5 (0x0020) ENC_CHECK in row 58, bit 6 (0x0040) BODE in row 60, bit 7 (0x0080) CAPTURE_STAGES in row 64, bit 8
(0x0100) TRIG_TYPES and bit 10 (0x0400) COUNTER in row 66 (bit 9 is C6's PEAK), bit 12 (0x1000) PATTERN and bit 13 (0x2000)
PATTERN_TRIGGER in row 67 (bit 11 is the logger's); the trigger extension status at 48..51 (FPGA 0x37, 0x34) in row 66.

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | control | 0 LOCAL, 1 REMOTE |
| 1 | u8 | acq_state | 0 IDLE, 1 ARMED, 2 TRIGGERED, 3 DONE, 4 ABORTED (REMOTE only; 0 in LOCAL); 1.27: 5 LOGGING (row 67) |
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
- Raw divider: 0 .. 1048575 (20 bits: what the FPGA compares since the 0x153C cruft audit, `PC_DIVIDER_MAX`; above it
  E_BAD_ARG detail 4 since fw-one, protocol 1.25). The UI table spans N = 0 (200 MS/s) to 399999 (500 S/s).
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

**GET_TRIGGER (0x0104).** Response: TRIGGER (28 bytes up to 1.25; 52 bytes since 1.26: the 28 below followed by the 24-byte
SET_TRIGGER2 body that reproduces the trigger, row 66):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | source | 0 CH1 (core A), 1 CH2 (core A), 2 EXT (reserved, needs EXT_TRIGGER); 1.27: 3 the pattern's marker (row 67; the edge type only, else E_BAD_ARG detail 0) |
| 1 | u8 | edge | 0 rising, 1 falling |
| 2 | u8 | level | raw core-A code 0..255 (7.5) |
| 3 | u8 | mode | 0 auto, 1 normal, 2 single |
| 4 | u32 | pretrigger_samples | per channel, interleaved count, even |
| 8 | u32 | total_samples | per channel, even |
| 12 | u32 | holdoff | the 0x1B value in 5 ns units (1.26, HOLDOFF); always 0 up to 1.25 |
| 16 | u32 | auto_timeout | write clocks, value written to 0x0E |
| 20 | u16 | pretrigger_words | written to 0x0B = pretrigger_samples / 2 [V `S100 fpga_control.c:646-651`] |
| 22 | u16 | total_words | written to 0x0C = total_samples / 2 |
| 24 | u32 | reserved | |
| 28 | 24 bytes | trigger2 | 1.26: the SET_TRIGGER2 request body below with the values in effect: type, polarity (= byte 1), flags, source (= byte 0), N, H, level (= byte 2), level2 0, holdoff (= bytes 12..15), 0 |

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
| 16 | u32 | holdoff | 1.26: the trigger hold-off in 5 ns units (0 = off), written to 0x1B with a clear request for the next arm (row 66); E_UNSUPPORTED detail 6 up to 1.25 |
| 20 | u32 | auto_timeout | 0 = firmware default: `total_samples` write clocks, which is what 1.00o5 writes for Atlan4 bitstreams [V `S100 fpga_control.c:565-569`]; stock uses the UI's `timebase_settings[]` table |

Response: TRIGGER. Validation (`E_BAD_ARG` with detail = bit number of the field):

- `pretrigger_samples` and `total_samples` even; `min_total_samples <= total <= max_total_samples`;
  `pretrigger <= total - 2`.
- Without PROG_TRIGGER_POS (stock FPGA) only `pretrigger = 1500`, `total = 3000` are accepted, others give
  `E_UNSUPPORTED`.
- `holdoff != 0` without HOLDOFF gives `E_UNSUPPORTED` (up to 1.25; since 1.26 the firmware is built for 0x153C and the
  field is accepted). `source = 2` without EXT_TRIGGER gives `E_UNSUPPORTED` (history).
- In REMOTE the level is used as given. The UI's derivation of the level from the on-screen trigger marker
  (`fpga_set_trigger_level`, called on every UI arm [V `S100 scope_functions.c:727`]) is not run.

**SET_TRIGGER2 (0x0219).** 1.26 (row 66), cap TRIG_TYPES, FPGA 0x153C. The trigger definition in one request, every field
written (no mask; the geometry, the mode and the auto time-out stay SET_TRIGGER's). Request (24 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | type | 0 edge, 1 pulse width, 2 Nth edge, 3 timeout (FPGA 0x5E bits 2..0). 4 runt and 5 window are 0x153E: `E_BAD_ARG` detail 0 |
| 1 | u8 | polarity | 0 / 1, written to 0x16 (GET_TRIGGER `edge`). Edge: the edge; pulse width 0 = positive pulse, 1 = negative (the trigger is its trailing edge); Nth edge: the counted edge; timeout 0 = held high after a rising edge, 1 = held low |
| 2 | u8 | flags | bit 0 LONGER: pulse width longer than N (0x5E bit 3; pulse width only). Bits 1 RANGE and 2 BOTH_CORES are 0x153E: `E_BAD_ARG` detail 2; other bits detail 2 |
| 3 | u8 | source | 0 CH1, 1 CH2, 2 EXT (0x15; EXT switches the generator off as SET_TRIGGER does); 1.27: 3 the pattern's marker (edge type only, E_BAD_ARG detail 3 otherwise; row 67) |
| 4 | u32 | N | pulse width / timeout: 1..16777215 samples of the trigger stream (0x5F, u24); Nth edge: 1..65535 edges; edge: 0. **The firmware converts nothing**: a sample is one memory word on CH1 / CH2 (10 (D + 1) ns legacy, N x 5 ns with the filter), one 5 ns clock on EXT |
| 8 | u32 | H | Nth edge: idle time 0..16777215 samples (0x60, u24): the count restarts when the level held for H samples without an edge, 0 = count from the arm; every other type: 0 (0x153E: the pulse-width range's N2) |
| 12 | u16 | level | 0..255, the raw 8-bit code as SET_TRIGGER's level (through the UI's marker; mapped to 0x55 at a 16-bit arm) |
| 14 | u16 | level2 | 0 (0x153E: the runt / window second level) |
| 16 | u32 | holdoff | trigger hold-off in 5 ns units, 0 = off (0x1B): from the trigger of a record no trigger is accepted until it elapsed, across sequencer restarts and re-arms; 0x1B = 0 releases a running one at once |
| 20 | u32 | reserved | 0 |

Response: TRIGGER (52 bytes). `E_BAD_ARG` with detail = the byte offset of the field (section 14 row 4): 0 type, 1 polarity,
2 flags, 3 source, 4 N, 8 H, 12 level, 14 level2. Nothing is written when a field is refused. Writes: 0x15, 0x16, 0x17 (or
the marker) as SET_TRIGGER; then 0x5E, 0x5F and 0x60 (the latter two for a type other than edge), 0x1B, and 0x66 = 1 so
that the next arm's 0x01 = 1 clears a running hold-off (a new trigger definition is a new session; re-arms between
writes keep the hold-off running, which is what skips the pulses of a train). Every arm (UI conversion or host ARM)
re-writes 0x5E (and 0x5F / 0x60) inside the sampling reset window after 0x0B / 0x0C and before 0x0E (section 8.4 ARM step
4a), so the registers apply at every rate and a FEL-loaded firmware's FPGA holds what the firmware believes. The trigger
position is the edge type's for the qualifying edge (timeout: the qualifying sample; EXT qualified types decide 5 ns
later than an EXT edge): 0x14, the pretrigger and the record arithmetic are unchanged.

**GET_COUNTER (0x0213).** 1.26 (row 66, fw-c7), cap COUNTER, FPGA 0x153C. The hardware counter (FPGA 0x61 / 0x62, read
0x35; `fpga/al3/AGENTS.md` "0x153C C3 counter", contract `docs/plans/fpga-0x153c.md` section 5): reciprocal counting of
the source's 1-bit stream over a gate of exactly 10 ms / 100 ms / 1 s, from the first rising edge in the gate to the
last: E periods, T clocks, W high clocks (5 ns clocks, 28 bits), plus the source's raw code minimum and maximum over
the gate. The firmware reads the 16-byte snapshot once per main-loop pass (17 bus strobes, about 5 us) and holds the
latest result; GET_COUNTER answers from that copy without touching the FPGA, so it is answered inside menus too
(F_MODAL). Request (8 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | what | 0 read the held result (the other fields are ignored; an empty request reads too), 1 configure then read. 2+: `E_BAD_ARG` detail 0 |
| 1 | u8 | gate | 0 off, 1 = 10 ms, 2 = 100 ms, 3 = 1 s (0x61 bits 1..0); 4+: detail 1 |
| 2 | u8 | source | 0 the trigger source (0x15, as the UI's trigger menu sets it; the FPGA restarts the gate when it changes), 1 CH1, 2 CH2, 3 EXT (0x61 bits 3..2); 4+: detail 2 |
| 3 | u8 | level | raw ADC code, the Schmitt trigger is high at code >= level (0x62 byte 0). **0 = follow the UI trigger level** (the boot behaviour: the marker the user placed inside the signal, re-written when it moves); any other value pins it |
| 4 | u8 | hysteresis | codes below the level for low (0x62 byte 1); hysteresis > a non-zero level: detail 4. Boot: 6 (about three times the 2 LSB noise) |
| 5 | 3 x u8 | reserved | 0 |

A configuration is noted and written by the next main-loop pass (0x62, then 0x61 = 0 and 0x61 = the value, which
restarts the gate; a menu open at the time delays it until the menu closes); the response already reports the gate and
source asked for, the level and hysteresis still the ones in effect. Boot writes 0x62 = 128 / 6 and 0x61 = gate 100 ms,
source 0 (built-for bitstream only). Response (24 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | flags | bit 0 NEW: the result's sequence number differs from the one the previous GET_COUNTER returned (or none was returned yet); bit 1 VALID: E >= 1 (two or more rising edges in the gate) and the result is current (the gate on, the result younger than three gates + 100 ms); bit 2 EXT, bit 3 CH2: the source the result was counted on (neither: CH1); bits 4-7 0 |
| 1 | u8 | seq | the FPGA's sequence number of the result (one per completed gate) |
| 2 | u8 | gate | as configured (0x61 bits 1..0) |
| 3 | u8 | source | as configured (0x61 bits 3..2; 0 = the trigger source: the flags say which channel that was) |
| 4 | u32 | E | periods between the first and the last rising edge in the gate; 0 = fewer than two edges (the signal under the thresholds, or slower than two edges per gate) |
| 8 | u32 | T | 5 ns clocks between those edges |
| 12 | u32 | W | clocks the stream was high in them |
| 16 | u8 | min | the source's raw code minimum over the gate (255 for EXT and before the first gate) |
| 17 | u8 | max | maximum (0 for EXT and before the first gate) |
| 18 | u8 | level | 0x62 byte 0 in effect (the trigger level when it is followed) |
| 19 | u8 | hysteresis | 0x62 byte 1 in effect |
| 20 | u32 | freq_mHz | round(E x 2e11 / T) in mHz, saturating at 0xFFFFFFFF (above 4.29 MHz: use E and T); 0 when not VALID |

Derived on the host: frequency = E / (T x 5 ns), period = T x 5 ns / E, high time = W x 5 ns / E, duty = W / T.
Resolution one clock over the gate (5e-7 relative at 10 ms, 5e-9 at 1 s; the 50 MHz crystal's accuracy on top); range
about 1 Hz (two edges in a 1 s gate) to the front end's 30 MHz. **Rate changes**: a 0x0D / 0x53 write while a gate runs
(a time base or filter change, by the UI or the host) adds or drops one edge in the gate that spans it; the firmware's
pass that sees such a write stops the gate (0x61 = 0) before its read, drops the result that read carries and restarts
the gate (0x61 = the value), so the next result is a clean full gate, and no contaminated result reaches GET_COUNTER or
the cards. **The cards**: the Freq, Period, +Width, -Width, +Duty and -Duty cards of the channel the result was counted
on show the counter's values while a VALID result is held (the format is the record's: four significant digits); the
other channel's cards, an EXT result, a result without edges and the reference traces show the record's measurement
(`firmware/README.md` "Hardware counter"). Vectors: `req_get_counter` (the one-per-command read),
`req_get_counter_follow_trigger`, `req_get_counter_ext_10ms`, `resp_get_counter_1_26_ch2`, `resp_get_counter_1_26_no_edges`,
`resp_get_counter_e_bad_arg_hyst`, and the mcp's `host_req_get_counter_read`, `host_req_get_counter_config`,
`host_resp_get_counter` (byte-exact with the firmware's test).

### 8.4 Acquisition commands

**ARM (0x0200).** REMOTE only. Request: 4 bytes reserved (0). Response: GET_STATUS body (state ARMED).
Firmware sequence, derived from `fpga_do_conversion` [V `S100 fpga_control.c:604-660`]:

1. Wait out any pending relay settling time (8.3).
2. Write 0x0F = 0 (trigger enable; dead on Atlan4 FPGAs, meaningful on stock [V zaklad.v 0x0F; Donwulff
   inventory]).
3. Write 0x01 = 1 (sample-system reset). Poll 0x05 bit0 until 1, at most 2,000,000 reads; on timeout answer
   `E_FPGA` (1.00o5 waits forever here [V `S100 fpga_control.c:636`]).
4. With PROG_TRIGGER_POS: write 0x0B = pretrigger_words, 0x0C = total_words, 0x0E = auto_timeout.
   4a. 1.26 (FPGA 0x153C): between 0x0C and 0x0E the trigger type registers 0x5E (type, longer) and, for a type other
   than edge, 0x5F (N) and 0x60 (H), from the firmware's copy (`fpga_trig`, fpga_bus.c); 0x1B and 0x66 are not part of
   the arm (SET_TRIGGER2 writes them).
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
| 2 | u8 | layout | 0 per-core blocks; 1 interleaved channel stream (7.3), needs both cores of each selected channel; 2 u16 words of a 16-bit capture (row 47); 3 peak pairs of a peak-detect record (1.26, row 66: per channel `count` minima then `count` maxima; layout 2 on a peak record and layout 3 on any other record are E_BAD_ARG detail 2) |
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

Since 1.27 (FPGA 0x153D, row 67) the generator is the pattern generator's two-step continuous pattern on the 200 MHz
clock: a high step of n_h + f_h / 65536 clocks and a low step of n_l + f_l / 65536 clocks of 5 ns, one 16-bit fraction
accumulator, so the mean period is exactly P16 / 65536 clocks and every edge lands within one 5 ns clock of its ideal
time; a change while it runs takes effect at a step boundary (double buffer: glitch-free, phase-continuous). Logic level
on the calibration pin (about -1.23 V / +1.87 V against the probe ground), hi-Z when disabled (the EXT input). Range 23.3
mHz .. 25 MHz (SET_GEN's cap: 8 clocks). Up to 1.26: Atlan4's 32-bit DDS at 50 MHz (0x51 / 0x52), f = phase_inc x 50 MHz
/ 2^32, 20 ns edge jitter. Stock FPGA: fixed 1 kHz, always driven, no control.

The firmware's conversions (integer only; f in mHz, d in basis points):

```
P16 = (2^16 x 2e11 + f / 2) / f        the period in 1/65536 of a 5 ns clock
H16 = (P16 x d + 5000) / 10000         the high part
n_h = H16 >> 16, f_h = H16 & 0xFFFF;  n_l = (P16 - H16) >> 16, f_l = (P16 - H16) & 0xFFFF;  n_h, n_l in 2 .. 2^32 - 1
back: f = (2^16 x 2e11 + P16 / 2) / P16;  d = (H16 x 10000 + P16 / 2) / P16
```

**GET_GEN (0x0300).** Response GEN (40 bytes since 1.27; 24 up to 1.26). Answered inside menus: there the pattern status
is the one last read; in the main loop GET_GEN reads FPGA 0x39 (the pending bit a host chirp paces on).

| Off | Type | Field |
|---|---|---|
| 0 | u8 | enable (0x50: the pin is driven) |
| 1 | u8 | pattern status, FPGA 0x39 byte 0: bit 7 running, 6 in the gap, **5 pending** (a commit not yet taken), 3..0 step |
| 2 | u16 | bursts done (0x39 bytes 1..2) |
| 4 | u32 | 0 (was phase_inc up to 1.26) |
| 8 | u32 | 0 (was duty_threshold up to 1.26) |
| 12 | u64 | freq_mhz_actual (millihertz, from P16; 0 with source 1) |
| 20 | u16 | duty_bp_actual (basis points, 0..10000, from H16; 0 with source 1) |
| 22 | u8 | 0x39 byte 3: bit 1 marker seen since the last sampling reset, bit 0 mode active |
| 23 | u8 | source: 0 the generator's two-step square, 1 a SET_PATTERN pattern (row 67) |
| 24 | u64 | P16 (0 with source 1, or for the UI's static levels: 0 Hz, 0 % or 100 %) |
| 32 | u64 | H16 |

**SET_GEN (0x0301).** REMOTE only, cap GENERATOR. Request (20 bytes):

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u8 | mask | bit0 enable, bit1 freq, bit2 duty |
| 1 | u8 | enable | |
| 2 | u8 | freq_mode | 0 = `freq` in millihertz (1 .. 25,000,000,000), 1 = raw P16 (1.27; up to 1.26 the DDS phase_inc) |
| 3 | u8 | duty_mode | 0 = `duty` in basis points, 1 = raw H16 (1.27; up to 1.26 the DDS threshold) |
| 4 | u64 | freq | mHz, or P16 (8 x 65536 .. 2^49 - 1) |
| 12 | u32 | duty | duty_mode 0: 0..10000 bp, bytes 16..19 must be 0 |
| 12 | u64 | duty | duty_mode 1: H16 (bytes 12..19 as one little-endian u64) |

Response: GEN (its byte 1 is the status read after this request's commit: bit 5 is usually still 1). Errors: E_BAD_ARG
detail 0 enable, 1 the frequency (0 Hz, over 25 MHz, a raw period outside 8 clocks .. 2^49 - 1), 2 the duty (over 10000,
reserved bytes, a step under 2 or over 2^32 - 1 clocks); E_STATE when enabling while the capture triggers on EXT. A duty
in basis points follows a later frequency change, a raw H16 is kept as a time. Every frequency or duty change writes the
two entries and the commit (18 strobes, 5.4 us); with mask bit 0 alone over a SET_PATTERN pattern only 0x50 changes.
1.00o5's whole-Hz / whole-percent entry points (the generator menu) use the same formulas (gen_pat.c).

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
| 5 | E_UNSUPPORTED | capability missing in this firmware build (detail 0); since 1.25 detail 1 = the FPGA runs another bitstream than the firmware is built for (section 5.1: every command outside the basic set) |
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

All tests run in REMOTE mode with DEBUG_RAW enabled, on the maintainer's AL3 unit, first with the stock bitstream
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
| 10 | GET_STATUS flags bits 0-2 in LOCAL (1.2) | always 0: the firmware never touches the FPGA while the UI owns it. 1.3: bits 0-2 describe the host single capture (row 30) | 6.1 single maintainer |
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
| 30 | Protocol 1.3: shared control (v0.2.0) | GET_INFO `proto_minor` = 3, caps bit 13 SHARED_MODE (0x2000) on every known bitstream, `#VER` says `proto=1.3`. No LOCAL/REMOTE, no lock, no banner, no watchdog: the UI loop always runs and every command is accepted at any time, except E_BUSY while a UI menu is modal (row 9). E_NOT_REMOTE is never returned. SET_REMOTE (any action, flags, watchdog) answers OK with the status body and changes nothing; `#LOCAL` answers `#OK` | `docs/plans/collaborative-mode.md`: the maintainer and the agent use the scope at the same time |
| 31 | Settings (1.3) | host SET_* write the live UI settings through the UI's own apply code and redraw what changed (channel button, time base, trigger, generator, RUN chip); the UI's changes show in GET_* at once. Last writer wins. Host changes persist and are saved at power-off like the user's. SET_TIMEBASE mode 0 applies the target index in one step (index, its sample rate, short mode, FPGA 0x0E/0x0D, then one redraw of the time base label and the trace area; v0.2.3, after the maintainer saw flicker with v0.2.0-0.2.2, which ran `scope_set_timebase` and in UI single trigger mode also re-armed the UI single). The trigger level is kept the UI way, as the marker position (0..401) the UI derives the level from before every conversion: SET_TRIGGER level places the marker at the nearest position, so the read-back can differ by one code, and the level then follows range and offset changes as on the screen. A raw divider (SET_TIMEBASE mode 1) and raw DDS words (SET_GEN) stay until the UI changes its time base / generator settings; SET_GEN also stores the rounded Hz and % in the UI settings | one live settings set |
| 32 | GET_STATUS (1.3) | 44 bytes. offset 0 `control` = 2 (SHARED). 1 `acq_state`: the host single capture only. 2 flags: bits 0-2 the host single (triggered, done, auto_timeout), others as before. 3 `ui_busy`: 0 idle, 1 a menu is modal, 2 STARTING (USB is up but the main loop has not finished its first pass; see row 38). 4 `capture_id`: every DONE capture of any origin. 8 `trigger_addr` / 10 `capture_start` of that latest capture. 20 watchdog 0xFFFF, 23 exit_reason 0. Appended: 32 u32 `settings_seq`, 36 u32 `capture_settings_seq`, 40 u8 `run_state` (0 STOP, 1 RUN, 2 SINGLE: a host ARM or a UI single is waiting), 41 u8 `capture_origin` (0 host ARM, 1 UI run, 2 UI single), 42 u16 0 (**superseded**: since 1.4 byte 42 is `ext_flags` and since 1.7 byte 43 is `display_mode`, rows 41 and 45; bytes 42-43 read 0 only without EXT_TRIGGER and while the scope view shows). The same body answers SET_REMOTE, ARM, STOP, WAIT and SET_ACQ_STATE | the host sees the shared state in one read |
| 33 | `settings_seq` (1.3) | +1 (wraps, never 0xFFFFFFFF) whenever a capture-relevant setting changes, from the UI or the host: per channel enable, range (samplevoltperdiv), coupling, offset, invert, probe; time base index, sample rate, host raw divider, long time base mode; trigger source, edge, mode, level (only while the UI's "always 50 %" is off, since it rewrites the level after every capture; the flag counts instead), pretrigger, total. Detected by comparing a fingerprint at every poll, UI arm and UI DONE. `capture_settings_seq` = settings_seq when the latest capture was armed if nothing changed until its DONE, else 0xFFFFFFFF (also GET_CAPTURE_INFO flags bit 4); 0xFFFFFFFF too before the first capture. The WAIT answer of a host single carries that capture's own value (capture_origin 0), so a host that sees it equal to its run's settings_seq needs no GET_CAPTURE_INFO | long host runs tag every capture with its settings; no lock |
| 34 | ARM, STOP, SET_ACQ_STATE (1.3) | ARM is a host single capture: it stops the UI acquisition (run_state STOP, the screen shows STOP), then runs the 1.2 ARM sequence (rows 7, firmware-internal ARM), so geometry, auto_timeout, WAIT, FORCE and DONE are as in 1.2. At DONE the capture is read into the trace buffers for the enabled channels and the UI shows it; the UI stays in STOP. The host single is aborted (acq_state ABORTED, a pending WAIT answers E_ABORTED) when the UI arms a conversion (RUN on the screen, or a time base tap in single mode) or on SET_ACQ_STATE. Since fw 0.2.68 the screen shows a host single in progress (armed or triggered, or done less than 1 s ago) as RUN, and its RUN/STOP button reads STOP: a tap aborts the host single the same way and the UI stays in STOP (wire format unchanged). ARM in the UI long time base mode: E_STATE detail 2. STOP aborts a host single and stops the UI. New SET_ACQ_STATE 0x0208: request u8 state (0 STOP, 1 RUN) + 3 reserved; does what the RUN/STOP button does (RUN while the UI trigger mode is single is the UI single "WAIT", run_state 2); E_BAD_ARG detail 0 for other values; allowed while a WAIT is pending | the screen shows the host's capture; RUN resumes from either side |
| 35a | READ_TRACE `count` (v0.2.1) | `start` and `count` are samples per channel, checked against the buffer's `sample_count` (header offset 4; the capture's length, which can differ from GET_TRIGGER `total` after a SET_TRIGGER while stopped or before the next UI capture). count 0 or a count past the end is clipped to sample_count - start; start > sample_count: E_BAD_ARG detail 4. Up to 0.2.0 a count past the end was E_BAD_ARG detail 8 "count" | maintainer's scope_measure asked for the GET_TRIGGER total |
| 35 | READ_RAW (1.3) | no DONE requirement: returns the latest finished capture of any origin (header `capture_id`), while the FPGA ring still holds it (no arm since its DONE). In RUN the UI usually has the next capture armed: the request is then held for at most 1000 ms until that capture finishes, and the UI is kept from re-arming until the request is answered, so it returns the capture that finished next. Meanwhile other requests wait (one request at a time). E_BUSY detail 1 if nothing finished within 1000 ms (normal mode, no trigger). E_STATE detail 0: no capture yet or a host single still armed; detail 1: the ring was overwritten and the UI is stopped (STOP while a UI capture was armed); detail 2: UI long time base mode. flags bit 0 (ANY) reads the ring as it is, without these checks. A READ_RAW split into several requests can see different `capture_id`s in RUN (a new UI capture between chunks): the host compares them, retries, and as a last resort does SET_ACQ_STATE STOP, reads, then RUN | the UI loop rewrites the ring; reading during a conversion would mix two captures |
| 36 | READ_TRACE (1.3) | never waits: the trace buffer of the latest capture (host single: built from the ring at DONE, rebuilt on REREAD while the ring holds it; UI: the UI's own buffer and geometry). Byte 29 `origin` uses row 32's numbering (1.2: 0 remote, 1 UI, so both keep their meaning). Channel metadata are the front end settings the capture was armed with. A UI time base change clears the UI trace buffer (1.00o5) | |
| 37 | GET_CAPTURE_INFO 0x0209 (1.3) | any poll context. Request empty. Response 68 bytes, the settings the latest finished capture was taken with: 0 u32 capture_id, 4 u32 capture_settings_seq, 8 u8 origin, 9 u8 flags (bit 0 triggered, bit 1 auto timeout (0x18 not set at DONE, also for UI captures, test T6), bit 2/3 overload CH1/CH2, bit 4 settings changed while it ran; fw 0.2.97..0.2.102: bit 6 the record may be spliced, i.e. joined from two passes of the ring, set when the firmware had to force the stop on FPGA 0x1532..0x153A / EF2 / EP4 or a trigger may have been cut by the FPGA's auto time-out, section 7.5; since fw-one (protocol 1.25) never set: the firmware runs on 0x153B only, whose forced stop reads the newest words), 10 u16 trigger_addr, 12 u16 capture_start_word ((trigger_addr - pretrigger_words) mod ring_words for every origin), 14 u16 0, (1.4: flags bit 5 = EXT edge seen during the capture) 16 u32 divider, 20 u8 timeperdiv index (0xFF raw divider), 21 u8 trigger source, 22 edge, 23 level, 24 mode (protocol numbering), 25-27 0, 28 u32 pretrigger samples, 32 u32 total samples, 36 + 16 x ch (CH1, CH2): u8 enable, range, coupling, invert, probe_index, 0, u16 offset_pwm, u32 uv_per_lsb_q8, i32 zero_code_q8. E_STATE detail 0 before the first capture. 68..75 decimation (row 47), 76..95 the sequencer run (row 56); 1.26: 112 bytes, 96..111 the trigger extension the capture was armed with (row 66) | read_capture without ARM; settings per capture |
| 38 | Startup (v0.2.0) | GET_LINK_STATS payload: u16 n, u16 0, then n u32 counters (decode by n; word 0 is the count, not a counter). n per firmware: 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3), 35 (0.2.4), 41 (0.2.5-0.2.6), 47 (0.2.7-0.2.11), 48 (0.2.12-0.2.16: 47 = USB interrupts served by the timer poll, i.e. lost IRQs; retired in 0.2.21, always 0 since), 49 (0.2.17-0.2.18: 48 = ms spent on the boot splash step + 1), 58 (0.2.20: 57 = USB IRQ races: ticks at which the 0.2.14-0.2.19 fallback gate (two ticks without a USB interrupt) would have served while the real USB interrupt was pending in the same dispatch, i.e. what that fallback counted as lost but was not; retired in 0.2.21, always 0 since), 57 (0.2.19: 49-56 = restart record: restarts since the last cold boot or ENTER_FEL, exception type (0 = jump to the image entry, 1 undefined instruction, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved vector, 6 FIQ), exception LR (fault PC = LR - 8 for a data abort, LR - 4 for undefined / prefetch), its SPSR, its SP, then LR, SP and CPSR found at _reset; event type 15 RESTART is logged at boot after a restart: a = exception type, b = low 16 bits of the exception LR or of the reset LR). Since 0.2.19 the exception vectors record and restart instead of hanging. Since 0.2.21 n stays 58 and slots 47 and 57 are retired: always 0, kept so decoders by count keep working (row 42). Since 0.2.48 n = 60: 58 = fault status of the abort recorded in 49-56 (DFSR for a data abort, IFSR for a prefetch abort, else 0; ARMv5 FS bits 3:0, domain 7:4), 59 = fault address (FAR, data abort only, else 0), read by the exception vector before the restart (no protocol minor change; decode by n). Since 0.2.49 n = 62: 60 = USB resumes inferred without a RESUME interrupt, 61 = USB soft reconnects (row 53). Counter index, 0-based: 0-26 as row 28; 27 ms from session init to the first main-loop poll; 28 duration of the first main-loop pass; then times from the first main-loop poll, as ms + 1 (0 = not reached; v0.2.2-0.2.3 used ms OR 1): 29 first UI conversion started (after its 0x05 wait), 30 first UI capture processed, and (v0.2.4) within the first pass: 31 after the battery check, 32 after the acquisition step, 33 after the touch handler, 34 after the RTC block (end of the pass). v0.2.5 appends 6 USB device event counters (41 in all): 35 bus resets, 36 suspends, 37 resumes, 38 disconnects, 39 EP0 setup packets, 40 EP0 packets that were not 8 bytes (stalled). v0.2.7 appends 6 RX back-pressure counters (47 in all): 41 stalls entered from the RX interrupt (ring full), 42 releases (main loop made room), 43 re-stalls inside a release, 44 releases with no packet waiting, 45 largest RXCOUNT seen, 46 packets with RXCOUNT > 512. Bench 0.2.3 (SD persist): the first one-word 0.2.4 boot reading put 1515 ms in counter 27 (init to first poll; the splash `load_picture("scope.bmp")` is the prime suspect) and 31 ms in 28. Bench 0.2.1: 3 ms to the first poll, first pass 1516 ms. GET_STATUS `ui_busy` = 2 until the second main-loop poll. Bench 2026-10-18: after a FEL load GET_STATUS showed no completed pass for ~1 s and the first main-loop command waited ~5 s; the first pass is inside the 1.00o5 UI loop (first conversion's 0x05 wait or touch scan suspected, not verified) | hosts back off instead of timing out |
| 39 | MCP indicator (v0.2.3) | a steady green chip "MCP", 38 x 17 px at x 127, y 3, stacked directly above the RUN/STOP chip (same width), while a host is active (DTR up, or a frame within the last 2 s); no per-command pulse (maintainer decision). It uses the slot of the optional RTC clock, which is not drawn meanwhile and comes back within 0.5 s after the chip goes. Redrawn every 100 ms while on. v0.2.0-0.2.2: x 697, y 22 over the version text, with a pulse | plan: small indicator next to RUN/STOP, no banner |
| 40 | SD sectors (1.3, v0.2.2) | cap bit 14 SD_SECTORS (0x4000) on every known bitstream. Main loop only; the UI pass waits meanwhile (a 64 KiB write takes on the order of 100 ms [U]); PING etc. are still answered from the timer interrupt. **SD_INFO 0x0500**: request u8 flags (bit 0 VERIFY), 3 reserved. Response 64 bytes: 0 u32 card_sectors, 4 u32 part_start (LBA of MBR entry 0; 0 = no valid MBR), 8 u32 write_end (writes allowed in [16, write_end): min(part_start, 16384) since fw 0.2.101, min(part_start, 2048) up to 0.2.100; 0 = writes off), 12 u32 stage_bytes (65536), then 3 x 16 bytes for sectors 16, 80, 750: u32 sector, u8 magic (0 none, 1 eGON.BT0, 2 eGON.EXE), u8 check (0 not checked, 1 checksum ok, 2 bad, 3 length invalid, or the image ends past sector 16384 (fw 0.2.101; up to 0.2.100: longer than 2048 sectors)), u16 0, u32 length, u32 stored checksum. VERIFY recomputes the eGON checksum over `length` bytes on the card (mksunxi.py rule). E_IO detail 0 without a card. **SD_READ_SECTORS 0x0501**: u32 sector, u16 count 1..128, u16 0. Response: u32 sector, u16 count, u16 0, u32 crc32 of the data, u32 0, then count x 512 bytes. E_BAD_ARG detail 4 count, detail 0 beyond the card. **SD_STAGE 0x0502** (answered from the 1 ms timer poll since v0.2.7, so staging needs no main-loop pass per piece): u32 offset, u16 length (= payload - 8, at most 484), u16 0, data. Response 12 bytes: u32 offset, u16 length, u16 0, u32 crc32 of the bytes. E_BAD_LEN detail 4 if length does not match, E_BAD_ARG detail 0 beyond 64 KiB. The stage persists until overwritten. **SD_WRITE_SECTORS 0x0503**: u32 sector, u16 count 1..128, u8 flags (bit 0 CONFIG: allow 708-710, bit 1 BOOT: allow 16-79), u8 0, u32 crc32 of stage[0, count x 512), u32 magic 0x52574453 ('SDWR'). Checks in this order, all before any write: magic (E_BAD_ARG 12), flags (6), count (4), card (E_IO 0), MBR (E_STATE 0: no valid partition table, writes off), window: sector >= 16 and the last sector < write_end (E_BAD_ARG 0; since fw 0.2.101 a last sector at or past write_end answers the text "partition starts at P: it must start at L or later", L = the last sector + 1, or "past sector 16383, the end of the write window" when the cap ends the window), 16-79 need BOOT, any of 708-710 need CONFIG (E_BAD_ARG 0, text says which), stage crc (E_BAD_ARG 8). Then write, read back, compare: E_IO with the driver error, or 0xFFFF on a mismatch. Response 16 bytes: u32 sector, u16 count, u16 0, u32 crc32 read back from the card, u32 0. The power-key interrupt is held off during the write, so the power-off settings save (sector 709) cannot interleave [U]. An SD_WRITE blocks the main loop for its whole duration (bench 0.2.3: up to ~4 s for 128 sectors with read-back, the UI pass waits meanwhile; PING etc. still answer): hosts should write 16-32 sectors per request and allow >= 5 s. Persisting a FEL-loaded image: stage + write the image file in pieces from sector 750 (the image must end below the partition and below 16384: fw 0.2.100 is 1510 sectors, 750..2259, so a card for this firmware has its FAT32 partition at 16384 or later), then SD_INFO VERIFY: sector 750 check = 1 | maintainer: persist an image without removing the card |
| 41 | Protocol 1.4: EXT trigger (v0.2.9, FPGA 0x1533) | GET_INFO `proto_minor` = 4. The AL3 family is decoded as (v & 0xFFF0) == 0x1530 and v >= 0x1532 (variant 2, ring 8192, max 16382, by the firmware UI too); 0x1533+ adds caps bit 3 EXT_TRIGGER (0x0008). SET_TRIGGER source 2 = EXT on the calibration pin (3.3 V logic referenced to the scope's USB ground, never more; FPGA internal pull-down; 1 kOhm to FPGA ground. The probe ground (BNC shell) sits about +1.24 V above USB ground (VRF1 via R71): a source on the probe ground reads ~1.24 V low level, unreliable; use a series red LED, anode at the source, and rising edge. Linking the grounds shifts all traces. fpga/al3/README.md "Changes" 2): E_UNSUPPORTED detail 0 without EXT_TRIGGER. With EXT the FPGA ignores the level (it is still stored and read back, as the UI marker) and the UI skips its software trigger refinement; edge (rising / falling), mode, pretrigger, total and auto time-out are unchanged, and trigger_addr has the usual meaning (pad-to-trigger latency 20-25 ns, fpga/al3/README.md; correction from the bench, not built in). Selecting EXT (host or the trigger menu, where EXT is shown only with the cap) switches the generator off, since it drives the same pin; SET_GEN enable while the source is EXT gives E_STATE detail 0 ("EXT trigger uses the pin"); the UI generator Power switch is not blocked (the scope then triggers on its own generator). GET_STATUS byte 42 `ext_flags` (EXT_TRIGGER only, else 0), FPGA cmd 0x28 read at every main-loop poll: bit 0 live EXT level, bit 1 generator enabled (FPGA drives the pin), bit 2 an EXT edge was seen since the last sampling reset (every arm clears it); byte 43 stays 0. GET_CAPTURE_INFO flags bit 5: an EXT edge was seen during that capture. A saved EXT source is kept only on bitstreams with the cap (else CH1) | maintainer / PI detector work: sync captures to the TX pulse |
| 42 | Protocol 1.5: USB robustness, GET_EVENT_LOG (v0.2.12) | **v0.2.21 removed the tick fallback described here**: the 0.2.20 bench and 30-minute soak showed counter 47 = 0 and counter 57 = 2176, i.e. 1.00o5's dispatcher did not lose a USB interrupt and the fallback only raced the real interrupt (verified 2026-10-18). Since 0.2.21 counters 47 and 57 are always 0 and type 14 is never emitted (the number stays reserved); GET_EVENT_LOG, the snapshot, the 2 ms RX sweep and the TX pump kick are unchanged. History: v0.2.12-0.2.20: the 1 ms timer poll also runs the USB CDC interrupt handler (since v0.2.14 only after two consecutive ticks without a real USB interrupt; 0.2.12-0.2.13 ran it on every tick, which raced the real interrupt during traffic, so their counter 47 is meaningless): the MUSB keeps its interrupt flags until they are read, and 1.00o5's irq_handler clears INTC pending by writing 0 after reading it, so a USB interrupt raised in between was lost; for EP0 nothing else would ever serve it (a control request then times out on the host, which fails the device and resets the port; suspected for the Windows error-22 drops). GET_LINK_STATS counter 47 counts the polls that found flags (v0.2.14+: lost-interrupt candidates). GET_EVENT_LOG 0x0009, any poll context, request empty. Response: u16 n (<= 64; <= 128 since v0.2.15), u16 0, u32 total events since boot, u32 now (ms), then n entries oldest first: u32 ms, u8 type, u8 a, u16 b. v0.2.15 appends the reset snapshot: u16 m (<= 16), u16 frozen (1 = a bus reset froze it), u32 reset ms, then m entries: the events right before the first bus reset since the snapshot was armed, so a later flood or Windows' repeated resets cannot evict them. Re-arming: 0.2.15-0.2.17 at every read; since v0.2.18 only with request byte 0 bit 0 set (u8 flags, other bits E_BAD_ARG detail 0), so a host that reads the log at every session open no longer throws the snapshot away. v0.2.24 appends a bus-level ring after the snapshot: u16 k (<= 16), u16 0, u32 bus-level events since boot, then k entries oldest first (same 8-byte format), holding only types 1-4, 7, 8, 12, 13 and 15-18, so EP0 setups and DTR edges cannot evict them. Since v0.2.24 an EP0 setup identical to the last logged one (bmRequestType, bRequest, wValue low) is logged at most once per second (pyserial repeats GET_LINE_CODING about five times per open); counter 39 still counts every setup. Hosts that stop at the snapshot are unaffected. `ms` is the same clock as GET_STATUS uptime_ms (timer0 ticks, 1 ms). Type 14 ignores the SOF flag (INTUSB bit 3 latches every frame with its interrupt disabled; 0.2.12-0.2.14 logged ~1000/s) and is logged at most once per 100 ms (counter 47 still counts every hit). Since v0.2.16 the tick reads the three flag registers itself and runs the handler body only for a flag other than SOF, so the fallback does nothing at all in a clean session (counter 47 stays 0); 0.2.12-0.2.15 ran the whole handler for SOF 300-500 times per second. Since v0.2.20 the tick no longer reads the flag registers to decide: it checks endpoint state (EP0 CSR0 RxPktRdy, SetupEnd, SentStall, an IN data phase with TxPktRdy clear, a SET_ADDRESS whose status stage completed; EP2 RXCSR RxPktRdy while EP2 OUT is unmasked) and serves only a condition seen on two consecutive ticks with no USB interrupt in between, after reading the flags. It skips any tick at which the USB interrupt is pending. Counter 47 counts these services. Whether the dispatcher's write of 0 to INTC pending actually loses interrupts is unverified (the F1C200s manual does not say how the pending bits clear). Types: 1 bus reset, 2 suspend, 3 resume, 4 disconnect, 5 EP0 setup (a = bRequest, b = bmRequestType << 8 or wValue low byte), 6 EP0 packet not 8 bytes (b = COUNT0), 7 SET_CONFIGURATION, 8 CLEAR_FEATURE(HALT) (a = endpoint), 9 DTR change (a = new DTR), 10 RX ring full (b = RXCOUNT), 11 RX release, 12 TX frame flushed (link / DTR; b = bytes left), 13 TX frame aborted (2 s no progress or #RESET; b = bytes left), 14 retired since v0.2.21 (0.2.12-0.2.20: USB flags found by the timer poll, a = INTUSB, b = INTTX << 8 or INTRX; since v0.2.20 a lost interrupt served by the tick: a = INTUSB as read, b = INTTX << 8 or INTRX as read, low 7 bits each, plus bit 15 = EP0 needed service, bit 7 = EP2 OUT packet waiting). A host port reset shows as suspend then bus reset (the MUSB reports the SE0 as idle first). 16 USB controller state at init (v0.2.22, row 43), 17 and 18 EP2 IN state at a TX abort (v0.2.23, row 43), 19 USB state at the end of init (v0.2.25, row 43), 20 USB soft disconnect (v0.2.49, row 53; also in the bus-level ring) | diagnose USB drops: read the log right after a re-enumeration |
| 43 | USB init state, event type 16 (v0.2.22) | No protocol change (proto 1.5). `usb_device_init` (boot and every CDC <-> MSC switch) logs the USB controller state it finds, then asserts the USB OTG bus soft reset (CCU BUS_SOFT_RST0 bit 24) and the PHY0 reset (USBPHY_CFG bit 0) for 1 ms before 1.00o5's sequence releases them; up to 0.2.21 the resets were only released, so an SD boot configured whatever the bootloader left. Five consecutive type 16 entries, a = field: 0 b = POWER << 8 or FADDR (7 bits); 1 b = INTUSBE << 8 or CCU flags (bit 0 OTG reset released, bit 1 OTG bus clock on, bit 2 PHY reset released, bit 3 PHY clock on); 2 b = EP2 TXCSR; 3 b = EP2 RXCSR; 4 b = ISCR bits 4-19 (bit n of b = ISCR bit n + 4). Controller fields read 0 unless CCU flags bits 0 and 1 are both set. Compare a cold SD boot with a FEL load. EP2 data toggles and FIFOs were already cleared at every SET_CONFIGURATION (ClrDataTog + FlushFIFO on both endpoints). v0.2.23: every type 13 (TX abort) is followed by type 17 (a = POWER, b = EP2 TXCSR) and type 18 (a = bit 0 EP2 TX interrupt enabled, bit 1 ZLP pending; b = USB frame number, 11 bits). UnderRun (TXCSR bit 2) is cleared at every packet load, so TxPktRdy (bit 0) set = a packet waits and the host takes no IN data; TxPktRdy clear with UnderRun set = the host polls and the device has nothing loaded. The frame number advancing by the elapsed ms (mod 2048) between two entries means SOFs keep arriving. Device-mode TXCSR does not expose the data toggle. v0.2.25: the three PHY tuning writes (RES45 calibration 0x0C = 1, TX amplitude 0x20 = 0x14 over 5 bits, disconnect threshold 0x2A = 3 over 2 bits; values as Linux phy-sun4i-usb for suniv) go to PHYCTL 0x01C13404 as in Linux, not to 0x01C13410 as in 1.00o5. At the end of `usb_device_init` seven type 19 entries, a = field: 0 POWER << 8 or FADDR; 1 INTUSBE << 8 or CCU flags (as type 16); 2 ISCR bits 4-19; 3 USBPHY_CFG bits 0-15; 4 PHY tuning bits read back now; 5 the same read right after the PHY reset was released, before the writes; 6 PHYCTL bits 0-15. Tuning bits packed: bit 0 = PHY 0x0C, bits 1-5 = PHY 0x20-0x24, bits 6-7 = PHY 0x2A-0x2B; as written = 0x00E9. The read-back (address in PHYCTL bits 8-15, data at bit 16) is verified on the bench (2026-10-18, FEL load: field 5 = 0x0068 PHY defaults, field 4 = 0x00E9 as written) | diagnose path-dependent USB state and IN stalls |
| 44 | Protocol 1.6: FFT_BENCH (v0.2.27) | Cmd 0x0F02, main loop only (E_BUSY while a UI menu is modal), every bitstream, no cap bit; GET_INFO `proto_minor` = 6. Request: empty or u8 `reps` (0 = 3, at most 16, else E_BAD_ARG detail 0). Runs the on-scope FFT (`fx_fft.c`, Hann window) on the CH1 trace buffer as 8-bit codes minus 128: N = 1024, 2048, 4096, 16384 with the work buffers in DDR, then N = 1024, 2048, 4096 with them in on-chip SRAM A (0x1000-0x700C, borrowed; nothing else uses it at run time; since v0.2.31 the plan holds twiddles too, so only N = 1024, 2048 fit, at 0x1000-0x7008, and n is 6). The UI pass waits for the whole run (estimate about 0.15 s at 3 reps); timer-poll commands (PING, GET_STATUS) still answer. Response: u16 n (entries), u16 reps, u32 CP15 control register as read (bit 0 MMU, bit 2 D-cache, bit 12 I-cache), then n entries of 24 bytes: u16 N, u8 window (1 = Hann), u8 memory (0 DDR, 1 SRAM), u32 plan init us (window table), u32 run min us, u32 run max us (window + load + FFT + split step, over `reps`), u32 dB conversion min us (N/2 + 1 bins), u16 peak bin (bins 2..N/2), i16 peak level (centi-dB re 1 code RMS: a full-scale sine reads about 3907). Times from timer0 (1 ms ticks plus the 24 MHz down-counter, 1 us steps) with IRQs on, so they include the 1 ms timer poll. Diagnostic only; the MCP may expose it as a bench tool. |
| 45 | Protocol 1.7: spectrum view, READ_SPECTRUM (v0.2.28) | GET_INFO `proto_minor` = 7. **Display mode**: the scope has a spectrum view (main menu row "Spectrum analyzer", hidden in roll mode; firmware/README.md "Spectrum view"). It is a main-loop display mode, not a modal menu: every command keeps working, the UI keeps capturing (RUN/STOP, trigger, time base and channel settings as usual) and the trace area shows the spectrum of each new capture, host captures included. GET_STATUS byte 43 `display_mode` (was 0): 0 scope, 1 spectrum; since v0.2.32 also 2 waterfall, 3 density (sub-views of the same screen; READ_SPECTRUM serves the latest spectrum in all three); any poll context. **READ_SPECTRUM 0x020A** (main loop, every bitstream, no cap bit). Request (8 bytes, shorter is zero-extended): u8 channel (0/1, else E_BAD_ARG detail 0), u8 0, u16 first bin, u16 count (0 or more than fits = as many as fit, 33,264), u16 0. E_STATE detail 0 when the spectrum view is off or shows no spectrum of that channel (channel off, not selected, or no capture yet). Response: 32-byte header, then `count` i16 levels in centi-dBV (centi-dBA when the channel is in A mode), bin k at k x bin width; -32000 = empty bin. Header: u8 channel, u8 window (0 rect, 1 Hann, 2 flat-top), u8 log2 N, u8 flags (bit 0 levels in dBA; since v0.2.34 bit 1 = the FFT ran on the 16-bit record of the 0x1534 decimation filter, bits 2-3 = its order K, 0 off; since v0.2.40 bits 4-6 = trace averaging applied to the levels: 0 off, 1 / 2 / 3 = exponential dB average over 4 / 16 / 64 captures, 4 = max-hold), u32 frame (spectra computed since boot; changes with every new capture or setting), u32 divider (the UI time base's 0x0D value; sample rate 200 MHz / (divider + 1) without the filter, 200 MHz / N with it, row 47), u32 sample rate Hz (rounded), u32 bin width mHz (fs / N_fft, truncated), i32 gain cdB (20 log10 volts per code, already applied: subtract it for dB re 1 code RMS), u16 bins in the spectrum (N/2 + 1), u16 first bin returned, u16 count returned, u16 ENBW milli-bins (1000 rect, 1500 Hann, 3770 flat-top; RBW = ENBW x bin width). Levels: dBV of a sine's RMS for a tone centred on a bin (fx_fft.c scaling, host-tested against numpy); volts per code from the calibration as in section 8.5 [U: T8], probe factor included. N is the largest power of two the record holds, capped by the view's FFT length (so 2048 at the 3000-sample default depth, 8192 at 15000 on AL3). The FFT input is the trace buffer minus the zero code (DC bin = the DC level). Known limit: a host capture made with a raw divider (SET_TIMEBASE mode 1) is labelled with the UI time base's rate. |
| 27 | ENTER_FEL (v0.1.1) | cap ENTER_FEL on every known bitstream (not on unknown ones). Magic 0x4C45465F, else E_BAD_ARG detail 0. The OK response is sent first; once it has left the FIFO (at most 200 ms) the scope shows "FEL mode", soft-disconnects USB for 300 ms, masks IRQs, disables the timer, power-key and INTC sources, puts the USB controller and PHY back in reset, turns the caches off (I-cache invalidated), selects high vectors, sets SVC mode with SP = 0x7C00 (the bootloader's FEL state) and jumps to 0xFFFF0020. Settings are not saved; leave FEL with a power cycle or `sunxi-fel exe` | iteration without the boot menu |
| 47 | Protocol 1.9: decimation filter (v0.2.30, FPGA 0x1534) | GET_INFO `proto_minor` = 9; `#VER` says `proto=1.9`. AL3 0x1534..0x153F adds caps bit 15 DECIM (0x8000) besides EXT_TRIGGER; on bitstreams without it nothing below is written to the FPGA and GET_DECIM / SET_DECIM answer E_UNSUPPORTED. FPGA interface (fpga/al3, fpga-gen3; bitstream sha256 83140510...b820c1): 0x53 mode (bits 1:0 K: 0 legacy 8-bit sample skipping, 1 boxcar = sum of N, 2 CIC2, 3 CIC3; bit 2 core-order swap; bit 3 / bit 4 capture the A / B cores on the falling write-clock edge, 5 ns later, default rising), 0x54 output shift S, 0x55 16-bit trigger level (0x17 is ignored while K >= 1), read 0x29 = {bit 7 16-bit layout, bit 4 neg B, bit 3 neg A, bit 2 swap, bits 1:0 K}, S. Fixed latency 12 write clocks (120 ns) from pin to memory for every K and N; a 16-bit sample is centred a further K (N - 1) 2.5 ns earlier; the ADC trigger is self-consistent (same trigger_addr convention as K = 0). N = 2 x (floor(D / 2) + 1), D = the 0x0D divider; output rate 200 MSa/s / N (equal to 200 / (D + 1) for the odd dividers of the time base table); stored sample = min(65535, y >> S), y = filter output, gain N^K; ADC code = sample x 2^S / N^K. The firmware picks S = max(0, bitlen(255 x N^K) - 16), falls back CIC3 -> CIC2 for N > 10334 (slower than 20 kSa/s) and CIC2 -> boxcar for N > 1,050,630 (exactness 255 x N^K < 2^48), and stays legacy (K = 0) at divider 0 (200 MSa/s) and for captures over 8190 samples. 16-bit layout (K >= 1): one sample per channel per ring word (low byte 0x20/0x22 = core A slot, high byte 0x21/0x23); pretrigger, total, trigger_addr and the auto time-out count words = samples (at most 8190 samples). The filter is the UI's acquisition **Filter** setting (off, boxcar, CIC2, CIC3; saved with the settings; shown only on 0x1534+; memory depth 9000+ hidden and reduced to 6000 while it is on); it applies to UI acquisitions and host ARM; calibration and auto setup always run legacy. A change of mode, shift or divider waits (K + 2) output periods before the next sampling reset (free-running filter; at most 9 ms at 500 Sa/s). It is part of settings_seq. **GET_DECIM 0x020C** (main loop), request empty; **SET_DECIM 0x020D**, request u8 filter (0..3, else E_BAD_ARG detail 0), u8 flags (bit 0 core-order swap, host only, not saved; bit 1 / bit 2 captured the A / B cores on the falling edge up to 0x1539 (hidden settings saved with the Filter word, no UI) and select nothing since 0x153A: accepted and written 0 since fw-one, protocol 1.25; bits 3-7 E_BAD_ARG detail 1), u16 0; the flags go into 0x53 with every mode, legacy included; filter >= 1 while the trigger total is over 8190: E_BAD_ARG detail 0 (SET_TRIGGER total first). Both answer 32 bytes: 0 u8 filter setting, 1 u8 flags (as in SET_DECIM), 2 u8 mode the next arm writes (0x29 byte 0 layout; 0 = legacy), 3 u8 its S, 4 u32 its N (0 legacy), 8 u64 gain N^K (1 legacy), 16 u32 divider (since v0.2.87 the 0x0D value the FPGA holds, the last one written by anyone; before, the session's raw divider or the UI table's, which a UI time/div snap without a 0x0D write could make differ from the FPGA's: firmware/AGENTS.md section 57; bytes 2..8, 20 and 31 follow it), 20 u32 output rate Sa/s, 24 u8 mode written at the last arm (0xFF none yet), 25 u8 its S, 26 u8 + 27 u8 FPGA 0x29 read now, 28 u16 8190 (largest total with the filter), 30 u8 why the effective K is lower than requested (0 not lower, 1 filter off, 2 200 MSa/s, 3 total > 8190, 4 CIC3 -> CIC2, 5 CIC2 -> boxcar, 6 no filter in the bitstream), 31 u8 EXT latency shift (samples) the next arm applies with the current source. SET_TRIGGER total > 8190 while the filter is on: E_BAD_ARG detail 5. GET_TRIGGER offsets 20/22 (words) and GET_TIMEBASE rate / period follow the effective configuration. GET_CAPTURE_INFO grows to 76 bytes: 68 u8 the capture's mode (0x29 byte 0 layout, 0 = legacy), 69 u8 S, 70 u16 EXT latency shift, 72 u32 N; capture_start_word uses words of that capture. **EXT trigger with K >= 1**: trigger_addr is the last sample written before the edge, but the stored data lag it by 120 ns + K (N - 1) 2.5 ns; the firmware rounds that to the nearest output sample, shift = floor((240 + 5 K (N - 1) + 5 N) / (10 N)) (half-ns units, halves up; e.g. N 2 boxcar 12, N 20 CIC2 2, N 20 CIC3 3, 1 at slow rates), writes pretrigger - shift as the pretrigger words, and reports the shift at byte 70, so sample `pretrigger` of the capture is the edge to within half an output period (capture_start_word = trigger_addr - (pretrigger - shift)). The pad-to-trigger latency of the EXT input itself (20-25 ns, row 41) is not corrected. READ_TRACE: header byte 30 = the capture's mode byte, 31 = S (both 0 for legacy captures). Without flags bit 2 the samples are the 8-bit trace as before: for a 16-bit capture the converted codes round(sample x 2^S / N^K + (adc_a_comp + adc_b_comp) / 2), clamped 0..255, which is what the screen and the 1.00o5 measurements use. Flags bit 2 (WIDE) with a 16-bit capture returns u16 little-endian samples as the FPGA stored them (no compensation, no inversion: flags bit 1 does not apply, meta bit 0 still reports invert); E_STATE detail 3 if that channel's last readout was not 16-bit. WIDE on a legacy capture is ignored (8-bit samples, byte 30 = 0). READ_RAW layout 2: u16 little-endian per word, low byte core A (0x20/0x22), high byte core B (0x21/0x23), both cores of a channel required (as layout 1): the 16-bit samples of a capture, `count` words = samples. Swap bit (0x53 bit 2): 0 = core B is the earlier sample of each pair, the B-before-A order of fpga_read_sample_data and READ_TRACE (affects K >= 2 only) | maintainer / spectrum work: anti-aliased, higher-resolution samples at slow time bases |
| 48 | Protocol 1.10: calibration data v2, GET_CAL 248 bytes (v0.2.43) | **Superseded by row 51 (calibration v3, GET_CAL 256 bytes): firmware 0.2.45 and later, and the MCP host, no longer read v2 blocks.** GET_INFO `proto_minor` = 10; `#VER` says `proto=1.10`. **Firmware behaviour**: the base calibration (UI) measures per channel, hardware range (0..5; 6 uses 5) and ADC core (A = 0x20/0x22, odd trace positions; B = 0x21/0x23, even) the raw zero with the inputs open, in two clock classes: *full* (ADCs at full rate: legacy divider 0 and every 0x1534 filtered capture) and *gated* (legacy divider >= 1: the FPGA gates the encode clock and the zero moves, bench 2026-10-18: CH1 core B -3.6 codes, core A -0.8), plus each core's offset slope (codes per PWM count, measured gated). It is stored in sector 708 bytes 128..455 (firmware/README.md section 6.2) and used while bound to the channel's `dc_calibration_offset[0..5]`. Every readout then corrects each core to T = 128 - trunc(dcoffset x 100 / dc_shift_value): correction_k = T - zero[class][k][range] - slope_k x dcoffset, applied as whole codes plus one more code with probability frac / 16 (dither), so READ_TRACE codes, the screen and the measurements put 0 V at T for both cores (A and B means agree to about 1/16 code). The 16-bit filter path adds corr16 / 16 codes to every converted sample (corr16 = 2 x T x 16 - full zeros of A and B with their slope terms, halved), i.e. READ_TRACE code = round(sample x 2^S / N^K + corr16 / 16), clamped; WIDE samples stay raw (the host applies corr16 / 16 after its own conversion). GET_CHANNEL `zero_q8` (offset 20) is T x 256 while v2 is in use (1.00o5 formula otherwise). READ_TRACE channel meta: the zero (meta bytes 8..11) follows the same rule; bytes 12..15 still report the stored 1.00o5 compensations, not the applied v2 correction (that is in GET_CAL). The spectrum view (READ_SPECTRUM, 16-bit records too) takes 0 V from the same correction. Without a bound v2 block everything is as in 1.9 (the 1.00o5 compensations). The base calibration also rewrites the 1.00o5 fields (`dc_calibration_offset` = PWM where the mean of both cores reads 128 gated; `adc_a_comp` / `adc_b_comp` = whole-code split from the gated zeros averaged over ranges), so an older firmware with the same card gets the better values. **GET_CAL** answers 248 bytes; bytes 0..71 unchanged. 72 u8 calibration version in use for this channel (1 = 1.00o5 compensations, 2 = v2), 73 u8 flags (bit 0 v2 block loaded, bit 1 bound to the current PWM centres, bit 2 the current divider is gated), 74 i16 comp_a and 76 i16 comp_b (whole codes the next readout adds), 78 u8 frac_a, 79 u8 frac_b (sixteenths added by dither; 0 without v2 and for 16-bit readouts), 80 i32 zero_q8 (trace code of 0 V x 256), 84 i32 corr16 (16-bit path correction, 1/16 code), 88 i32 shift (dcoffset x 100 / dc_shift_value, what the measurements add), then the stored block (zeros when not loaded): 92 u16 pwm[6] (the PWM centres it was measured at), 104 i16 zero[class][core][range] (2 x 2 x 6, class 0 full, 1 gated, core 0 A, 1 B; 1/16 code), 152 i32 slope[core][range] (Q16 codes per count), 200 u16 noise[class][core][range] (RMS 1/16 code). Host model: raw_k = zero[c][k][r] / 16 + slope[k][r] / 65536 x dcoffset at 0 V, in raw uncompensated codes of core k (the READ_RAW reference: A = 0x20/0x22, the low byte in layout 2; B = 0x21/0x23); READ_RAW stays raw. corr16 is in 8-bit codes x 16 for the mean of both cores (not a window sum): for WIDE samples code = out x 2^S / N^K + corr16 / 16 | maintainer: the screen, the measurements and the host agree on 0 V; Nyquist spur of the A/B zero spread |
| 49 | Protocol 1.11: CIC compensation FIR (v0.2.44, FPGA 0x1535) | GET_INFO `proto_minor` = 11; `#VER` says `proto=1.11`. AL3 0x1535..0x153F adds caps bit 16 FIR (0x10000) besides DECIM; older bitstreams never get command 0x56. FPGA interface (fpga/al3, fpga-gen4, testbench-verified; bitstream sha256 5040a2ae...): write 0x56 bit 0 = FIR enable (power-on 0, bits 7..1 written 0); read 0x29 byte 0 bit 6 = the enable as written, bit 5 = FIR active now. The FIR is active only when 0x56 = 1, K >= 1, N >= 16 and N % 4 == 0 (D >= 14 and D mod 4 in {2, 3}; in the time base table: 10 MSa/s and slower); otherwise the 0x1534 path of row 47 runs unchanged. Chain when active: CIC of order K decimating by N/2 (gain (N/2)^K), shift S, saturate to 16 bit, 15-tap symmetric FIR decimating by 2 with DC gain 1; output rate 200 MSa/s / N as before, same 16-bit layout and read-back. Firmware S = max(0, bitlen(255 x (N/2)^K) - 16) + 1 (the + 1 is headroom for the 6-9 % step overshoot; the worst-case FIR gain 1.21 cannot saturate); ADC code = sample x 2^S / (N/2)^K; 16-bit trigger level = round(v x (N/2)^K / 2^S); exactness 255 x (N/2)^K < 2^48, so CIC3 holds while N/2 <= 10334 (CIC3 -> CIC2 above, why 4). Latency 25 write clocks (250 ns) pin to memory; **EXT** with the FIR: the data lag the edge by 250 ns + 17.5 N ns + 1.25 K (N - 2) ns, shift = floor((1000 + 70 N + 5 K (N - 2) + 10 N) / (20 N)) (quarter-ns units, halves up; N 20: boxcar 6, CIC2 6, CIC3 7). Settling after a change of 0x0D / 0x53 / 0x54 / 0x56: (K + 10) N x 5 ns + 1 us (rounded up to whole ms + 1 by the firmware). **SET_DECIM** flags bit 3 = FIR (E_BAD_ARG detail 1 without caps bit 16; bits 4-7 E_BAD_ARG detail 1); it is written as requested for every K (0x56 = 1 with the filter off is harmless). **GET_DECIM** byte 1 bit 3 = FIR setting; bytes 2 / 24 / 26 (0x29 byte 0 layout) carry bit 6 enable and bit 5 active; byte 3 S and bytes 8..15 gain = (N/2)^K while active; byte 31 the FIR EXT shift. GET_CAPTURE_INFO byte 68 and READ_TRACE header byte 30 carry bits 5 / 6 of the capture. Every 8-bit conversion (READ_TRACE codes, the screen, the calibration v2 corr16 of row 48, the spectrum view's 16-bit path) uses the FIR-mode gain: host conversion of WIDE samples is code = out x 2^S / gain + corr16 / 16 with gain from GET_DECIM. UI: the acquisition Filter box steps off, boxcar, boxcar+FIR, CIC2, CIC2+FIR, CIC3, CIC3+FIR on 0x1535+ (the FIR variants are not offered on 0x1534); saved in settings word 207 bit 5; the spectrum header tag reads e.g. "CIC2+FIR" only while the FIR is active for that record. Vectors: docs/protocol-vectors.json `decim` cases with `fir_req` = 1 (every table rate x K, D 11..18, D 20667 / 20671 at the CIC3 limit), checked against pc_decim.c by check_vectors.py | maintainer: CIC + FIR as the eventual default acquisition mode (**superseded**: the default Filter was CIC2 from 0.2.44 and is CIC2+FIR since 0.2.48, `PC_DECIM_CIC2 | PC_DECIM_SET_FIR` in `pc_decim.h`, for new settings sectors and the DEFAULT button; since 0.2.64 per display mode, Scope CIC2 and Spectrum CIC2+FIR, `PC_DECIM_DEFAULT_SCOPE` / `_SPEC`; the FIR part takes effect only on 0x1535+ bitstreams, CHANGELOG 0.2.48); flat passband to fo/4 and about 20 dB more alias rejection (fpga/al3/README.md) |
| 50 | USB suspend / resume (v0.2.44, no protocol change) | **Audit (1.00o5 and earlier builds):** INTUSBE enables SUSPEND, RESUME, RESET, DISCONNECT; POWER (0x01C13040) is set to HS enable + soft connect, EnableSuspendM (bit 0) is never set, so the PHY is never put into suspend (SUSPENDM) and nothing was powered down or gated on suspend; the handlers touch no PHY or CCU register. Linux musb (musb_stage0_irq: musb_g_suspend / musb_g_resume) and the sunxi glue also handle suspend and resume in software only. What did differ: up to 0.2.43 a SUSPEND was handled as a link reset (TX frame flushed, EP2 OUT flushed, DTR cleared, session parser and deferred WAIT dropped), which a real host suspend / resume (configuration and line state kept, no SET_CONFIGURATION or SET_CONTROL_LINE_STATE after the resume) does not survive. **Since v0.2.44** a suspend is a state: no flush, no link reset; the following RESUME interrupt ends it (EP2 OUT unmasked again), a bus reset or disconnect resets the link as before. POWER bit 0 is cleared explicitly at enable (PHY stays powered). While suspended GET_STATUS / indicator DTR reads 0 (a pulled cable also reads as suspend, VBUS is forced valid); the stored DTR returns with the resume. The main loop checks every 20 ms whether the frame number moves again while still marked suspended (SOFs without a RESUME interrupt) and then logs an inferred resume. GET_LINK_STATS counter 10 `link_resets` no longer counts suspends; 36 counts SUSPEND interrupts, 37 RESUME interrupts only. **Event fields** (GET_EVENT_LOG, were 0): type 1 bus reset and type 4 disconnect: a = 1 if the device was suspended, b = ms since that suspend (65535 = longer); a port reset reads a = 1 with a few ms (the MUSB reports the SE0 as idle first), a reset after a real host suspend reads its length. Type 2 suspend: a = POWER, b = frame number. Type 3 resume: a = POWER (bit 7 set = inferred by the frame check, no RESUME interrupt), b = ms suspended. Reading the 0.2.43 bench case (suspend -> bus reset -> SET_CONFIGURATION at reopen, nothing before): with these fields, a reset with b of a few ms after minutes of silence means the host never suspended and reset the port for another reason; b of minutes and no type 3 means a missed resume. **v0.2.49 (row 53)**: the frame check runs every ms in the timer interrupt and acts (an inferred resume ends the suspend state, counter 60, a bits 6-5 = cause), and a soft reconnect recovers a device the host abandoned | bench 2026-10-18: Windows error 22 at the first write after ~5 min with the port closed |
| 51 | Protocol 1.12: calibration data v3, per-core and per-clock-class gain, GET_CAL 256 bytes (v0.2.45) | GET_INFO `proto_minor` = 12; `#VER` says `proto=1.12`. **Why** (bench 2026-10-18, CH1 range 2, +-1.0045 V): the two ADC cores differ in gain at full rate (A 28.62, B 27.71 codes/V, A/B 1.033, linear) and barely when the encode clock is gated (A 28.54, B 28.42, A/B 1.004), and the full-rate class has 1.1 % less gain than the gated one; a zero-only model (row 48) leaves an A/B difference that grows with the signal (the fs/2 spur at full rate) and makes 200 MSa/s and every filtered capture read 2.7-3.5 % low against the gated voltage calibration. **Model** per channel, core k (A = 0x20/0x22, odd trace positions; B = 0x21/0x23, even), clock class c (row 48: 0 full = legacy divider 0 and every filtered capture, 1 gated = legacy divider >= 1), hardware range r (6 uses 5): raw_k = z_k + V x g_k[c] (V at the ADC, codes), z_k = zero[c][k][r] / 16 + slope[c][k][r] / 65536 x dcoffset (the offset slope is now measured per class too). Gain correction G_k[c] = g_ref / g_k[c] with g_ref = the mean gain of both cores in the gated class (what `input_calibration` is fitted on, so legacy gated volts keep their calibration and the full-rate class is pulled onto it). One gain per core and class for all ranges: the mismatch is inside the ADC (**assumption**, measured on one range, bench check listed in firmware/README.md section 6.3). **Measurement** (base calibration, second step, UI only): after the zero step passes (probes unplugged, as before) the box asks for both probes (x1 or x10) on the calibration post; OK runs it, a touch outside skips it (the zeros are saved either way; the gains of the previous v3 block are carried over, else unity). The FPGA generator (AL3 bitstreams, commands 0x50-0x52) holds the post at its two static levels, about -1.24 V (output low) and +1.87 V (output high) against probe ground (docs/hardware-status.md); no level needs to be known. Per channel: a probe of both levels at range 2 picks the most sensitive range whose span is at most 120 codes (x1 probe: range 2, about 88 codes; x10: range 5), the offset PWM is moved so the two levels sit symmetric about the 0 V code (about 128; even-order INL about it cancels in the span), then low, high, low again with 8 gated (2 MSa/s) and 64 full-rate (200 MSa/s) conversions each; span_k[c] = high - mean of the two lows (linear drift cancels), G_k[c] = (span_A[gated] + span_B[gated]) / 2 / span_k[c]. Checks (first failure wins, box text "CH1: <reason>", that channel keeps its previous gains): no sample at 0 or 255 ("gain: level clips"), the two lows within 0.5 code ("gain: level drifts"), every span >= 40 codes ("gain: probe on post?"; also when the levels come out swapped), |G_A - G_B| <= 0.08 per class ("gain: cores differ"), (G_A + G_B)[full] / (G_A + G_B)[gated] within 0.9..1.1 ("gain: clock classes"). Channels pass or fail independently; the generator returns to its UI state afterwards. **Correction** per readout and core: code = clamp((raw x gain_k + off_k + u) >> 16, 0, 255), gain_k = round(G_k x 65536), off_k = round((65536 x T16 - gain_k x z16_k) / 16), T16 = 16 x T, T = 128 - trunc(dcoffset x 100 / dc_shift_value), z16_k = zero + round(slope x dcoffset / 4096) in 1/16 code, u = a uniform 16-bit dither per sample (xorshift32 top bits): the mean is exactly T + G_k x (raw - z_k), so 0 V sits at T for both cores as before and a signal has the same codes per volt on both cores and in both classes. The 1.00o5 rail rule now compares against the corrected codes of raw 0 and 255 of both cores (clip_lo = the larger of the two cores' codes for raw 0, clip_hi = the smaller of their codes for raw 255; when core B's code of a sample pair is at or below clip_lo, or at or above clip_hi, both codes of the pair are set to the lower, or the higher, of the two). **16-bit path** (filter on, ADCs at full rate, each output weighs both cores equally): gain16 = 2 G_A G_B / (G_A + G_B) of the full class (Q16), code = clamp(round(out x 2^S / gain x gain16 / 65536 + corr16 / 16)), corr16 = round(T16 - gain16 / 65536 x (z16_A + z16_B) / 2) with the full-class zeros; the spectrum view's 16-bit FFT uses the same scale. WIDE READ_TRACE and READ_RAW stay raw. Without a v3 block (or not bound) everything is as in 1.9 (1.00o5 compensations); **v2 blocks are no longer used** (the maintainer re-runs the base calibration). **Sector 708 block v3** bytes 128..431 (304 bytes): u32 'ACAL', u16 version 3, u16 length 304, u16 classes 2, u16 0; per channel (CH1, CH2, 144 bytes): u16 pwm[6] (binding, as row 48), i16 zero[class][core][range] (1/16 code), i16 slope[class][core][range] (Q16 codes per PWM count), u8 noise[class][core][range] (RMS, 1/16 code), u16 gain[class][core] (Q15, 32768 = 1), u8 gain_range (hardware range the gains were measured on, 0xFF = never measured: gains 32768), u8 gain_flags (bit 0 carried over from an earlier calibration), u16 gain_lever (gated span of the mean of both cores, 1/16 code); u32 zlib.crc32 of the 300 bytes before it. **GET_CAL** answers 256 bytes; bytes 0..71 unchanged. 72 u8 version in use for this channel (1 = 1.00o5 compensations, 3 = v3), 73 u8 flags (bit 0 v3 block loaded, bit 1 bound to the current PWM centres, bit 2 the current divider is gated, bit 3 gains measured for this channel (gain_range != 0xFF), bit 4 gains carried over), 74 i16 comp_a, 76 i16 comp_b, 78 u8 frac_a, 79 u8 frac_b (the correction at each core's 0 V point in whole codes and sixteenths, T16 - z16_k; with v1 the 1.00o5 compensations and 0), 80 i32 zero_q8 (trace code of 0 V x 256 = 256 T), 84 i32 corr16, 88 i32 shift (dcoffset x 100 / dc_shift_value), 92 u32 gain_a, 96 u32 gain_b (Q16, what the next 8-bit readout multiplies by; 65536 with v1), 100 i32 off_a, 104 i32 off_b (Q16 codes; v1: compensation x 65536), 108 u32 gain16 (Q16, 16-bit path; 65536 with v1), 112..255 the channel's stored block (144 bytes, the layout above; zeros when not loaded). Host model: READ_TRACE code_k = (raw x gain_k + off_k) / 65536 on average; WIDE code = out x 2^S / gain x gain16 / 65536 + corr16 / 16 (gain from GET_DECIM); volts as row 48 (uv_per_lsb from input_calibration, gated class). Vectors: `adc_cal_v3` in docs/protocol-vectors.json (block, checked against adc_cal.c by check_vectors.py); `adc_cal` (v2) stays until the host has moved | maintainer: 200 MSa/s, filtered and legacy volts agree; the full-rate Nyquist spur of the A/B gain difference |
| 52 | Protocol 1.13: phosphor view, GET_STATUS display_mode 4 (v0.2.46) | GET_INFO `proto_minor` = 13; `#VER` says `proto=1.13` (1.12 is fw-cal2's calibration v3). No new command and no new field. **Display mode**: the scope has a digital phosphor view (main menu row 7 "Phosphor display", next to the spectrum view's row 6; both hidden in roll mode and in the picture / waveform viewers; firmware/README.md "Phosphor view"). Like the spectrum view it is a main-loop display mode, not a modal menu: every command keeps working and the UI keeps capturing. The scope view stays as it is (grid, cursors, measurements, pointers); the CH1 / CH2 traces are drawn from per-pixel hit counts of the recent records. GET_STATUS byte 43 `display_mode` = **4** while it is on (0 scope, 1 spectrum, 2 waterfall, 3 density as before; the spectrum and phosphor views exclude each other). Records added: UI captures, and a host single capture once when it completes (READ_TRACE re-reads of it are not added); untriggered auto time-out records are skipped while a triggered one came within the last second. READ_TRACE, READ_RAW and every capture command are unchanged (the maps are display-only); READ_SCREEN shows the graded picture. Settings sector word 210 (0x9A00 marker, bit 2 palette, bits 0-1 persistence 0.2 s / 1 s / 5 s / infinite), saved at power-off like words 207-209; the mode itself is not restored. Host: the MCP's `DisplayMode` names 0-4 (scope, spectrum, waterfall, density, phosphor). |
| 53 | Protocol 1.14: USB suspend watch, inferred resume and soft reconnect (v0.2.49) | GET_INFO `proto_minor` = 14; `#VER` says `proto=1.14`. **Why** (bench 2026-10-19, fw 0.2.48-nommu): with the port closed from about 08:40 to 10:20 the device saw 17 SUSPEND interrupts and no RESUME (GET_LINK_STATS 36 / 37 = 17 / 0, no restart) and at 10:20 the host no longer listed it (no serial device, no re-enumeration) until the cable was replugged. Counter 37 has been 0 on every build and bench, so a RESUME interrupt has never been observed on this controller; and a host that gave up on the device leaves the bus idle, which the device cannot tell from a suspend. **Firmware behaviour**: a watch (`pc_usbw.c`, host-tested) runs in the 1 ms timer interrupt (0.2.44 checked from the main loop every 20 ms, which stops while a menu is modal). (1) **Inferred resume**: while marked suspended, the frame number moving (SOFs again), an EP0 setup or an EP2 OUT packet ends the suspend exactly like a RESUME interrupt (EP2 OUT unmasked again if configured, DTR reads as stored again). GET_LINK_STATS counter **60** counts them; event type 3 with a bit 7 set as before, now a bits 6-5 = cause (0 frame number moved, 1 EP0 setup, 2 EP2 OUT packet; 3 only from fw 0.2.44-0.2.48, whose a was 0x80 or POWER bits 6-0 with SoftConn and HS enable always set: frame number moved) and a bits 4-0 = POWER bits 4-0 (bit 4 high-speed mode, bit 1 the core's suspend mode), b = ms suspended (65535 = longer). A real RESUME logs type 3 with a = POWER (bit 7 clear) as before. (2) **Soft reconnect**: the watch clears POWER SoftConn (0x40, D+ pull-up released) for 1000 ms and sets it again, so the hub reports a disconnect and a connect and the host enumerates a fresh device (same descriptors and serial number, so Windows brings back the same COM port). It fires only while the bus has stayed idle (frame number frozen) since the last suspend: **storm**: 3 or more suspends since the last proof of life (SET_CONFIGURATION, a resume received or inferred) and then 10 s of silence, the wait doubling with each storm reconnect not followed by a SET_CONFIGURATION (10 s, 20 s, ... at most 640 s); a host that retries a port reset produces one suspend per attempt (at high speed the reset's SE0 is first seen as 3 ms idle) and the silence once it gives up; **long**: suspended for 15 min after a host enumerated the device, at most once per enumeration (a reconnect disarms it until the next SET_CONFIGURATION, so a sleeping host or a pulled cable costs one reconnect); the 15 min double with every long reconnect since an application last opened the port (DTR 0 to 1), up to 8 h, so an awake host that re-enumerates the device and suspends it again (selective suspend of an idle port) sees a reconnect after 15, 30, 60 ... min, not every 15 min. An open port is not suspended while the host is awake (the host keeps polling bulk IN). A normal suspend never meets the storm rule (one suspend, then resume or reset); the long rule cannot tell a 15-minute legitimate suspend (host asleep, port closed) from an abandoned device and then costs one re-enumeration. GET_LINK_STATS counter **61** counts soft reconnects; **event type 20** soft disconnect: a bits 1-0 reason (1 storm, 2 long), bits 3-2 back-off level of the rule that fired, after this reconnect (capped at 3), bits 7-4 suspends since the last proof of life (capped at 15); b = seconds suspended. The disconnect also resets the link like a bus reset (TX frame flushed, DTR cleared). Type 20 is kept in the bus-level ring too. The watch is not armed in mass-storage mode and does nothing while USB was switched off by other code (SoftConn clear without a watch disconnect). GET_LINK_STATS n = **62** (60 inferred resumes, 61 soft reconnects; decode by n). The CDC interrupt handler now handles SUSPEND before RESUME when both are pending (a resume can only follow its suspend). Nothing else changed in the suspend handling of row 50 | the device must come back after any host suspend without a replug; a resume the core does not report must not leave the transport marked suspended |
| 54 | Protocol 1.15: calibration v4, background tracking of the full-rate core mismatch (A - B) and of the full-class common mode, GET_CAL 288 bytes (v0.2.50) | GET_INFO `proto_minor` = 15; `#VER` says `proto=1.15`. **Why** (bench 2026-10-19, docs/plans/interleave-tracking.md): the gated class (legacy divider >= 1) holds the one-time v3 calibration over 9 hours (zeros within 0.3 code, gains within 0.4 %); the full-rate class (divider 0 and every filtered capture, both cores at 100 MHz, interleaved) does not: the core A/B gain ratio read 1.033, 1.013 and 1.000 within one night, the A/B zero split moved 1.1 code within 15 minutes, a bow of about 1 code at 35-50 codes from mid-scale appeared, and both cores' common mode rose 0.7-0.8 code in two hours against the gated class. Symptoms: a 1-2 code sample-by-sample split and an fs/2 spur in 200 MSa/s records; a 0.7-0.8 code zero error in every filtered capture. **A - B tracker** (per channel, CH1 / CH2 only, from every 8-bit full-class readout with a bound v3 block: UI records and host captures alike, at most one record per 50 ms; never from 16-bit readouts, the gated class or the calibration's own measurements): each interior sample s_n against the mean i_n of its two neighbours of the other core gives y = +-(s_n - i_n) (+ for core A) as an estimate of A - B at x = (s_n + i_n) / 2 - 128; a pair is used when all three samples are within 4..251 and the two neighbours differ by at most 6 codes, a record when it has at least 256 such pairs and at most 1/8 of the in-range pairs were dropped for slope (edges, energy near fs/2). The record's sums are moved back to the uncorrected domain (y + the d and e applied to that readout) and added to moments with forgetting 1/256 per accepted record (tau = 256 records: 12.8 s at the 50 ms gate, longer when records come slower); a least-squares fit gives delta(x) = A - B = d + e x + q x^2, x = code - 128. e is updated only while the accumulated spread of x is at least 8 codes RMS, q only while x visited at least three separated levels (w >= 65536 codes^4 and w >= v^2 / 4: a sine of +-27 codes or more, or a quiet input followed by a square); otherwise the last value is held (a quiet input tracks d only). **Applied** in the full class only (v3 bound, divider 0), symmetric about the mean of the cores: A' = A - (d + e x) / 2, B' = B + (d + e x) / 2, folded into the 8-bit readout's gain_k / off_k and its rail limits (no cost per sample). The mean of the cores, and therefore the 16-bit path (gain16, corr16), is unchanged by it. q is estimated and reported, not applied (a 1-code bow at 42 codes leaves q x^2: 0 at mid-scale, 0.25 code at 21, 1 code at 42 codes). **Class probe** (common mode of the full class against the gated class, the only stable reference on the board): on the channel's current range, offset PWM and coupling (no relay or PWM change), legacy conversions (K = 0): 8 conversions at 2 MSa/s (gated), 64 at 200 MSa/s (full; averages the 25 kHz offset-PWM ripple), 8 at 2 MSa/s again, about 100 ms; per class the corrected mean of both cores from the raw means and that class's v3 correction; c = full - mean of the two gated parts. Valid when every part's per-core RMS is at most 2 codes, no sample sits at 0 or 255, the two gated means agree within 0.3 code and |c| <= 8 codes. cm = c at the first valid probe after a reset, then cm += (c - cm) / 4. **Applied** (v3 bound): both cores' full-class off_k -= 65536 cm (8-bit full-rate readouts) and corr16 -= 16 cm (the 16-bit path: every filtered capture, the spectrum view's 16-bit records and the 16-bit trigger level; additive after the conversion, so independent of N, K and the FIR gain), i.e. the full class reads the gated class's zero again. The firmware runs a probe per channel at most every 30 s (the first 5 s after boot) and only when the scope or spectrum view is up with no menu open, the scope is running (RUN, not roll mode), no UI conversion is in flight, no host capture is armed or triggered and the host holds no READ_RAW hold, the channel is on and its last record was quiet (RMS about its mean <= 2 codes); it costs one screen update. A periodic signal never qualifies (cm then holds its last value; see the age). Like any firmware-internal conversion (calibration, auto setup) a probe resets the sample system: the ring no longer holds the last UI capture. **Resets**: tracker and cm start from zero (the stored v3 values alone) at boot and whenever the calibration wizard's Done writes a calibration; nothing is persisted. **GET_CAL** answers 288 bytes; bytes 0..255 keep their layout and meaning, except that 84 corr16 and 92..107 gain_a, gain_b, off_a, off_b now report what the next readout applies including v4 (bytes 74..80 stay the stored v3 correction at 0 V). New, little-endian: 256 u8 track_flags: bit 0 a record was accepted since the reset, bit 1 e identifiable at the last solve, bit 2 q identifiable at the last solve, bit 3 the last offered record was usable, bit 4 the tracker applies to this channel's next 8-bit readout (v3 bound, the current divider is the full class, and the last arm's filter configuration was legacy K = 0), bit 5 the class probe has data (a valid probe since the reset), bit 6 the last class probe was valid; 257 u8 0; 258 i16 d_q8 (A - B offset at code 128 applied, 1/256 code); 260 i32 e_q24 (A - B slope applied, Q24: 2^24 = 1); 264 i32 q_q24 (bow estimate, Q24 per code^2, not applied); 268 u32 age_ms (ms since the last accepted record, 0xFFFFFFFF = none since the reset); 272 u32 records (accepted since the reset); 276 u16 last_pairs (usable pairs of the last offered record); 278 u16 spread16 (RMS spread of the accumulated x, 1/16 code); 280 i16 cm_q8 (full-class common mode, corrected full mean minus gated mean, 1/256 code; the readouts subtract it); 282 u16 cm_probes (valid probes since the reset); 284 u32 cm_age_ms (ms since the last valid probe, 0xFFFFFFFF = none). **Host**: READ_TRACE 8-bit codes, the screen, the measurements and READ_SPECTRUM already carry v4. A host that converts READ_RAW or WIDE samples itself uses bytes 92..107 for 8-bit raw codes (code_k = (raw x gain_k + off_k) / 65536, unchanged formula) and 84 corr16 for WIDE samples, both read right before or after the capture; to model it from the fields instead: A - (d + e x) / 2, B + (d + e x) / 2 with x = code - 128 in the full class, and -cm on both cores (full class 8-bit) and on every WIDE code. GET_CAL bytes 0..255 parse as in 1.12 (a 1.12 host ignores the tail) | maintainer: 200 MSa/s A - B within 0.1 code and the fs/2 spur at the floor without re-calibrating; filtered zeros stay at 128 hours after calibration |
| 55 | Protocol 1.16: diagnostics readers: GET_HEALTH, card image identity (SD_INFO 112 bytes), calibration provenance (GET_CAL 356 bytes), SAVE_REPORT (v0.2.52) | GET_INFO `proto_minor` = 16; `#VER` says `proto=1.16`. **Why** (docs/plans/diagnostics-review.md section 4): the Diagnostics Status, Calibration and Log pages and the "Save report" file need values no command carried: the USB link state now, the identity of the image the card boots, where a calibration came from, a power-on count and operating time, a restart record that survives a power cycle, the settings save state and the CP15 control word. **GET_HEALTH 0x000A** (any poll context, also while a menu is modal; no cap bit). Request: empty, or u8 flags: bit 0 CLEAR_RESTART = clear the stored restart record after this answer was built (the answer still shows it; the cleared record reaches the card with the next settings save); PC_DEBUG builds only (caps DEBUG_RAW): bit 7 RESTART_TEST = after the answer has left, restart from RAM through the image entry (a type 0 restart, as a NULL call; a bench aid for the stored record); other bits E_BAD_ARG detail 0. Response 96 bytes, little-endian: 0 u8 layout (1), 1 u8 0, 2 u16 length (96). **USB now**: 4 u8 usb_state: 0 off (USB switched off by other code: mass-storage mode or ENTER_FEL), 1 reconnecting (the suspend watch's soft disconnect, row 53, is in progress), 2 not configured (pull-up on, no SET_CONFIGURATION since the last bus reset or boot: no host, a charger-only cable, or enumeration in progress; the defined "disconnected" state), 3 configured (enumerated, bus active), 4 suspended (configured, then the bus went idle: host asleep, an idle port under selective suspend, or the cable pulled; VBUS is forced valid in ISCR, so the device cannot tell a pulled cable from a suspend); 5 u8 usb_flags: bit 0 high speed (POWER bit 4), bit 1 configured (EP2 set up since the last bus reset), bit 2 suspended now (the watch's state, rows 50 / 53), bit 3 port open (DTR, as GET_STATUS flags bit 3), bit 4 SoftConn (POWER bit 6); 6 u8 POWER (0x01C13040) as read; 7 u8 FADDR (0 = no address assigned); 8 u32 ms in the current suspend (0 = not suspended); 12 u32 ms since the last SET_CONFIGURATION (0xFFFFFFFF = none since boot). **CPU**: 16 u32 CP15 control register c1 as read now (bit 0 MMU, bit 2 D-cache, bit 12 I-cache; 0x0005107D on 0.2.48+ images, 0x0005107C with the MMU off). **Unit life** (settings sector 709 words 224..255 below): 20 u32 power_on: this power-on's number, counted at every boot that is not a warm restart (a power-on or a FEL load; DDR contents surviving a very short power cycle make it look like a restart, row 38), 0 = unknown (no valid settings sector at boot); 24 u32 operating minutes, all power-ons together including this one (stored minutes + uptime; minutes since the last save are lost when a power-on ends without a save); 28 u32 uptime_ms (as GET_STATUS 16); 32 u8 life_flags: bit 0 a life record was found on the card at boot, bit 1 the boot write of sector 709 succeeded, bit 2 it failed (bits 1-2 both 0: no boot write, the settings sector was not valid), bit 3 sector 709 was valid at boot (else the 1.00o5 defaults were loaded); 33 u8 last_end: how the previous power-on ended: 0 unknown (no life record), 1 settings saved at power-off, 2 ended without a power-off save (a warm restart, power lost, a picture / waveform viewer open at power-off, or the write failed); 34 u8 settings_flags: bit 0 the settings changed since they were loaded or last saved (sector 709 words 2..223 as the power-off save would write them, compared by CRC-32; evaluated by the main loop once per second and on demand, so stale while a menu is modal: see 40), bit 1 the last in-session save failed; 35 u8 in-session saves of sector 709 since boot (calibration Done, DEFAULT; capped at 255); 36 i32 result of the last in-session save (0 ok, negative = driver error; 1 = none since boot); 40 u32 ms since bit 0 of 34 was evaluated. **Stored restart record** (survives power cycles until cleared): 44 u8 rec_flags: bit 0 a record is stored, bit 1 it is from this power-on (else from an earlier one); 45 u8 exception type (row 38: 0 jump to the image entry, 1 undefined, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved vector, 6 FIQ); 46 u16 restarts (counter 49 when it was stored, capped 65535); 48 u32 power_on it happened in; then as GET_LINK_STATS at that boot: 52 exception LR (51), 56 its SPSR (52), 60 its SP (53), 64 LR at _reset (54), 68 SP at _reset (55), 72 CPSR at _reset (56), 76 fault status (58), 80 fault address (59); 84 u32 operating minutes when it was stored; 88..95 0. **When stored**: at every boot after a warm restart (counter 49 > 0) the firmware copies counters 49-59 into the life record, replacing an older one, and writes sector 709 at once (the boot write below), so a crash followed by the maintainer's power cycle or a flat battery is still visible. **Sector 709 life record** (u16 words, inside the 1.00o5 checksum of words 2..255; 1.00o5 and older open firmware keep words they do not use, so the record survives a boot of an older image): 224 marker 0x11FE, 225 flags (bit 0 session open: set by the boot write, cleared by the power-off save), 226-227 power_on (u32, low word first, as every u32 below), 228-229 operating minutes, 230 restart flags (bit 0 stored), 231 restarts, 232 exception type, 233 0, 234-235 power_on of the record, 236-237 exception LR, 238-239 SPSR, 240-241 exception SP, 242-243 reset LR, 244-245 reset SP, 246-247 reset CPSR, 248-249 fault status, 250-251 fault address, 252-253 operating minutes at the record, 254-255 0. **Boot write**: when sector 709 loaded valid (checksum and version), the boot increments power_on (not on a warm restart), stores the restart record if any, sets the session-open flag and writes sector 709 once (one sector, power key held off, the other words exactly as loaded, checksum recomputed). Without a valid sector nothing is written at boot and power_on stays 0 until the first save. Every later save (power-off, calibration Done, DEFAULT) writes the record with the current minutes; the power-off save clears the session-open flag. Words 211..223 stay free for settings. **Card image identity (SD_INFO)**: request flags bit 1 IDENT: identify the eGON.EXE image at sector 750 (or reuse the cached result: the identity is cached until an SD_WRITE_SECTORS touches sector 750 or later); without it bytes 64..111 report the cache (state 0 if never read). The response is now 112 bytes (a 1.3 host reads the first 64). 64 u8 ident_state: 0 not read yet, 1 identity record (fw 0.2.52 and later), 2 commit hash and time from the build tail of fw 0.2.47-0.2.51 plus the version string, 3 version string only (images before 0.2.47: no build identity in the image), 4 no identity found ("unknown"), 5 no eGON.EXE image at sector 750, a length that ends past sector 16384 (fw 0.2.101; over 2048 sectors up to 0.2.100), or a read error; 65 u8 flags: bit 0 the card image is the running image (state 1 or 2 and version, hash and time equal to the running build's); 66 u8 proto_major, 67 u8 proto_minor (0 = unknown); 68 char[16] version (NUL-padded, as GET_INFO 36; empty when unknown); 84 u32 build (commit hash, as GET_INFO 52; 0 = unknown); 88 u8[6] commit time yy mm dd hh mm ss (UTC, zeros = unknown); 94 u16 0; 96 u32 image length (eGON header); 100 u32 ms the identification took (0 = from the cache); 104..111 0. **How the identity is found**: fw 0.2.52 links a 36-byte identity record as the last object of the image (build_id.c, linked last; .rodata is the last loaded section, so the record ends where the image ends before mksunxi's zero padding to 512): u32 0x44495746 ('FWID'), u32 0x33313031 ('1013'), char[16] version, u32 build, u8[6] commit time, u8 proto_major, u8 proto_minor; the reader looks for the two magic words at 4-byte steps in the image's last 1024 bytes (the last match wins). fw 0.2.47-0.2.51 end with build_id.c's 12 bytes, which GCC 10 emits time first: u8[6] commit time, u8[2] 0, u32 build; the reader takes the last non-zero byte p, the 12 bytes ending at the next multiple of 4 above p, and accepts them only when the time is a valid date (year 20..99, month 1..12, day 1..31, hour < 24, minute and second < 60), the two pad bytes are 0, the build is not 0, and the image's version string is 0.2.47 or later (checked on 0.2.48, 0.2.49 and 0.2.50 images from backups/). The version string: every protocol image since 0.1.x carries the `#VER` answer text `#OK,FNIRSI-1013D-AI,fw=<version>,proto=<major>.<minor>,fpga=0x`; for states 2-3 the reader scans the whole image for it (about the cost of a VERIFY: one pass over the image in 64-sector reads). **Calibration provenance (sector 708 bytes 432..487)**, 56 bytes after the v3 block, outside Atlan's checksum and outside the v3 block's CRC (the v3 block and its version are unchanged, so 0.2.45-0.2.51 still read the card; their own 708 save clears the trailer, like any older firmware's, after which the provenance reads "none"): u32 0x56525041 ('APRV'), u16 version 1, u16 length 56, u32 build (commit hash of the firmware that calibrated), char[16] fw version, u16 FPGA version word, u8 what (bit 0 CH1 zeros, bit 1 CH1 gains, bit 2 CH2 zeros, bit 3 CH2 gains: the parts this calibration wrote), u8 rtc (1 = the time below is from the DS3231 module, 0 = no module: no time), u32 uptime_ms at the calibration, u32 power_on at the calibration, u32 operating minutes at the calibration, u8[6] time yy mm dd hh mm ss (zeros without a module), u16 0, u32 zlib.crc32 of the 52 bytes before it. **Stamped** whenever sector 708 is saved with a v3 block that differs from the one loaded or last saved (the calibration wizard's Done); `what` compares each channel's PWM centres, zeros, slopes and noise (zeros) and gains, gain range, flags and lever (gains). A save with an unchanged block (DC-shift trim "End", the volts calibration) keeps the stored provenance; no v3 block, no trailer. **GET_CAL** answers 356 bytes; bytes 0..287 unchanged. 288 u8 prov_state (0 none on the card, 1 valid, 2 present but unreadable: CRC or version), 289..291 0, 292..347 the 56 stored bytes (zeros when none), 348 u32 power-ons since the calibration (current power_on minus the stored one; 0xFFFFFFFF = unknown), 352 u32 operating minutes since the calibration (0xFFFFFFFF = unknown). Provenance is per block, so both channels' GET_CAL show the same bytes. **SAVE_REPORT 0x0504** (main loop, cap SD_SECTORS): request empty. Writes one text file to the card's FAT partition, `reports/report-PPPPP-NN.txt` (PPPPP = power_on, NN the first free 00..99; folder created when missing): identity (running image, card image, FPGA word, variant, caps, serial, CP15 word in words), unit life and settings state, USB state, battery, the live restart counters and the stored record, all GET_LINK_STATS counters by name, the decoded event log (each entry in words) and the bus-level ring, the SD boot-sector checks and both channels' calibration (correction in use, v3 block values, v4 tracking, provenance) in words. Response 56 bytes: u32 bytes written, u16 0, u16 name length, char[48] name (NUL-padded, e.g. `reports/report-00012-00.txt`). Errors: E_IO detail = FatFs FRESULT (e.g. 3 not ready, 7 denied, 8 exists: 100 reports with this power_on), E_IO 0 without a card. The Diagnostics "Save report" button calls the same firmware function (`pc_glue_save_report`). Vectors: `diag_layouts` in docs/protocol-vectors.json (life record words, provenance trailer and identity record, checked against the C packers by check_vectors.py) | maintainer: the Diagnostics pages and the report show real values: "Running fw X from RAM, card boots Y", calibration age without a clock, a crash still visible after a power cycle |
| 56 | Protocol 1.17: FPGA 0x1536 sequencer, segmented capture and hardware averaging (v0.2.58; numbered 70 on the fw-seq branch until the merge, fw-readers' 1.16 took row 55 in parallel) | GET_INFO `proto_minor` = 17; `#VER` says `proto=1.17`. AL3 0x1536..0x153F adds caps bit 17 SEQ (0x20000) besides FIR; on older bitstreams nothing below is written to the FPGA and SET_SEQ / GET_SEQ / READ_SEQ answer E_UNSUPPORTED (READ_RAW addr_mode 3: E_BAD_ARG detail 1). FPGA interface (fpga/al3/README.md section 6, docs/plans/segmented-capture.md; bitstream sha256 4eaf6811...b48b, fpga-gen5): writes 0x57 (bits 1..0 mode 0 off / 1 segments / 2 averaging, bit 2 circular, bit 3 raw readout), 0x58 count (12 bit), 0x59 m, 0x5A N (0 = 65536), 0x5B S (readout shift), 0x5C hold-off (5 ns units), 0x5D stop; reads 0x0A (the whole run is done), 0x2A status (8 bytes), 0x2B..0x32 byte 0..7 of the 64-bit view word at the 0x1F pointer (one dummy strobe after 0x1F, as 0x20..0x23). The firmware writes 0x0B / 0x0C and 0x57..0x5C **before** 0x01 = 1 on 0x1536+ (write-clock registers load only on write-clock edges during the reset; at slow legacy rates the reset window can be shorter than one write clock; fpga-gen6 RTL reading), and 0x57 = 0 for every capture that is not a run when the FPGA may still hold another mode (always after boot). A "word" is a memory word as for 0x0B / 0x0C / 0x14: one A/B pair per channel in legacy mode (K = 0), one 16-bit sample per channel with the decimation filter. **Features word** (also for hosts that do not use the sequencer): GET_STATUS grows to 48 bytes: 44 u16 features (bit 0 EXT_TRIGGER = 0x1533+, 1 DECIM = 0x1534+, 2 FIR = 0x1535+, 3 SEGMENTS and 4 HW_AVERAGE = 0x1536+, bits 5-15 0), 46 u8 UI Average (0 off, 1 software: 1.00o5's per-pass (trace + previous) / 2, 2 hardware: the last UI record is the mean of an averaging run; **since 1.25** 1 = the MCU's mean of N records, the one averaging behaviour of row 65), 47 u8 log2 of the records per UI averaging run (since 1.25 for 1 and 2; 0 = single records, two do not fit the budget). GET_DECIM grows to 36 bytes with the same 4 bytes at 32..35. Both need no FPGA access. **UI hardware averaging** (no host command; firmware/README.md "Segmented capture and hardware averaging"): with the acquisition menu's Average on (auto or normal trigger mode, not the waveform viewer, not roll mode) and a 0x1536 bitstream, a UI conversion of a legacy record that fits the accumulator (the 3000-sample depth = 1500 words; i.e. filter off or 200 MSa/s) is one averaging run of N = 2..64 records (largest power of two whose N x (record + dead time) fits 40 ms; dead time = total x 5 ns + 40 ns + (2 + pre) words + 100 ns). The shown record is the mean of the lane sums through the per-core correction (code = round((mean x gain + off) / 65536) from the mean in Q8), so READ_TRACE of a UI capture returns it; the software average is skipped for it. 16-bit records (always 3000+ samples > 2046 words), deeper memory and rates where two records do not fit 40 ms kept the software average (byte 46 = 1) up to fw 0.2.102; since 1.25 (row 65) they are averaged by the MCU with the same planner. A run not done after max(100 ms, two records) + its run time is stopped (0x5D = 1, 0x1A = 0) once it holds a record (the mean of what it has); in auto mode without any record it ends untriggered and the following UI conversions are single records until one triggers. GET_CAPTURE_INFO / READ_RAW of a UI capture see the ring as before (the last raw record of the run). **SET_SEQ 0x020E** (main loop, cap SEQ). Request 16 bytes: 0 u8 mode (0 off, 1 segments, 2 averaging; else E_BAD_ARG detail 0), 1 u8 flags (bit 0 circular, segments only; else detail 1), 2 u8 m (segments: slot = 2^m words, 5..12; else detail 2), 3 u8 0, 4 u16 count (segments 1 .. 8192 >> m, or averaging positions P with P x total <= 2048; else detail 4), 6 u16 N (averaging: records per position, 0 = 65536; legacy records at most 257, above that the 16-bit lane sums would clamp; else detail 6), 8 u32 hold-off in ns (rounded up to 5 ns units), 12 u32 0. Checked against the geometry the next ARM writes (GET_TRIGGER pretrigger / total and the Filter): segments need total <= 2^m - 2 words (2 more words are written after each window) and pre < total, averaging total <= 2046 words; a misfit there is E_STATE detail 5 (total) / 6 (pre) "SET_TRIGGER first". Mode 0 clears the setting. The setting applies to host ARM only (the UI never runs it) and is not saved. Response: the GET_SEQ body. **GET_SEQ 0x020F** (main loop, cap SEQ), request empty, response 48 bytes: 0 u8 mode, 1 u8 flags, 2 u8 m, 3 u8 0, 4 u16 count, 6 u16 N (0 = 65536), 8 u32 hold-off ns, 12 u32 hold-off 5 ns units written to 0x5C, 16 u8 fit of the setting with the next ARM's geometry (0 fits, 1 mode, 2 flags, 3 m, 4 count, 5 total, 6 pre, 7 N), 17 u8 1 = the next ARM records 16-bit samples, 18 u16 its pretrigger words (0x0B, less the EXT shift in 16-bit), 20 u16 its total words, 22 u16 the largest count for the mode (8192 >> m segments, 2048 / total positions), 24 u8 FPGA 0x2A byte 0 now (bit 7 done, 6 overflow, 5 running, 4 16-bit records, 3 raw, 2 circular, 1..0 mode of the run, 0 = none since the last 0x01), 25 u8 sequencer state (0x2A bits 15..13 of bytes 6-7), 26 u16 next slot / position, 28 u32 count (segments completed or records accumulated), 32 u16 untriggered (segments ended by the auto time-out, or records skipped), 34 u8 1 = the FPGA still holds the latest capture's run (no arm since), 35 u8 0, 36 u32 capture_id of the latest capture if it is a run (else 0), 40 u32 its count at DONE, 44 u16 its untriggered, 46 u8 its mode, 47 u8 its flags (as GET_CAPTURE_INFO 77). **ARM with a setting** (mode != 0) arms a run: E_STATE detail 3 if the setting no longer fits (GET_SEQ byte 16); trigger mode auto arms the FPGA's own time-out 0x0E (counting samples: untriggered segments are recorded with triggered = 0, untriggered records are skipped by averaging) and the firmware auto time-out (row 46, fw 0.2.41) does not apply; normal / single arm normal. DONE (WAIT, GET_STATUS acq_state 3) is the end of the whole run (0x0A); the status is read once then. "triggered" (GET_STATUS flags bit 0, GET_CAPTURE_INFO flags bit 0) = the run holds a triggered segment or at least one record. An averaging run leaves its last record in the ring as a normal capture: the trace buffers are built from it at DONE (the screen shows that record), READ_RAW addr_mode 0-2 and READ_TRACE read it. A segment run builds nothing (the screen keeps its traces); READ_RAW addr_mode 1 / 2 and READ_TRACE answer E_STATE detail 4 / 5 for it (addr_mode 0 still reads the sample memory as it is). **STOP** during a run ends it instead of abandoning it: 0x5D = 1, then 0x1A = 0 (a record waiting for a trigger ends now); acq_state stays ARMED / TRIGGERED until 0x0A = 1, then DONE with flag "stopped" (GET_CAPTURE_INFO 77 bit 1). A circular run ends only this way. **GET_CAPTURE_INFO** grows to 96 bytes: 76 u8 run mode (0 = a single capture), 77 u8 flags (bit 0 circular, bit 1 stopped by STOP, bit 2 a circular run wrapped: 0x2A overflow), 78 u8 m, 79 u8 1 = 16-bit records, 80 u32 count at DONE (segments completed / records accumulated; a circular run counts beyond the slots), 84 u16 untriggered, 86 u16 count requested (segments / positions), 88 u32 N, 92 u32 hold-off ns. **READ_SEQ 0x0210** (main loop, cap SEQ). Request 12 bytes: 0 u8 what (0 segment table, 1 averaged record; else E_BAD_ARG detail 0), 1 u8 ch_mask (what 1: bit 0 CH1, bit 1 CH2, at least one; detail 1), 2 u8 S (what 1: readout shift 0..31, or 0xFF = the sums; detail 2), 3 u8 0 (detail 3), 4 u16 first (entry or word; detail 4), 6 u16 count (>= 1, first + count <= the table slots or P x total words; detail 6), 8 u32 0. Needs the latest capture to be a host run of that kind: E_STATE detail 0 none, 1 armed since (the run is gone), 2 the run is of the other mode. Response: 16-byte header (0 u32 capture_id, 4 u16 first, 6 u16 count, 8 u8 what, 9 u8 format (bit 0 16-bit records, bit 1 u32 sums), 10 u8 m (what 0) or the S applied (what 1; 0xFF for 16-bit sums; legacy 0xFF reads as S 0), 11 u8 ch_mask (what 1), 12 u16 pretrigger words of the run, 14 u16 total words), then the data. What 0: `count` u64 little-endian table entries: bits 39..0 timestamp in 5 ns units from the first write clock after the release of 0x01 to the edge that latched trigger_address (2 (D + 1) per write clock in legacy, D = divider; 10 ns per write clock with K >= 1; 22-32 ns after an EXT edge at 200 MSa/s), 52..40 trigger offset in the slot (trigger_address mod 2^m, the 0x14 convention), 53 triggered (0: the segment ended on the auto time-out, offset = pre mod 2^m), 63 valid (0 = never written, or the slot a circular run was refilling). Slots: count, or 8192 >> m for a circular run (slot s = table entry s). What 1, per selected channel (CH1 first): legacy records: a block of `count` u16 core A lanes (FPGA 0x20 / 0x22 cores) then a block of `count` u16 core B lanes, each = (sum over N records) >> S, the sums exact up to N = 257 (S 0 or 0xFF: the sums); 16-bit records: `count` u16 = min(65535, sum >> S), or with S 0xFF `count` u32 sums (no overflow up to N = 65536). Position p occupies words p x total .. p x total + total - 1 (record k goes to position k mod P). The firmware sets S and the raw bit (0x5B, 0x57 bit 3) for every read, so a record can be read again with another S. Mean in codes: legacy code = lane x 2^S / N (then the per-core correction, GET_CAL, as for READ_RAW); 16-bit: sample = value x 2^S / N, then the row 47 / 49 conversion (gain from GET_DECIM, corr16). Cost: one bus pass per byte, about 0.3 us per byte (a legacy channel of 1500 words: 4 passes, about 1.8 ms). **READ_RAW addr_mode 3** (segment-relative): address = segment << 16 | word offset from that segment's record start (record start = (table offset - pretrigger words) mod 2^m); count 1 .. 2^m words (detail 8), segment < slots (detail 4); E_STATE detail 4 without a held segment run, detail 5 for an entry with valid = 0. Header start_word = the absolute word of the first sample, ring_words = 2^m (the reads wrap at the slot end). Layouts 0, 1, 2 as for a single capture (legacy A/B pairs or 16-bit). **Limits** (fpga-gen5): accumulator 2048 x 64 (P x total <= 2048, total <= 2046 in averaging); segments 2^m x count <= 8192; re-arm time about hold-off + pre x word + 1 to 3 write clocks + 50 ns, averaging adds total x 5 ns of post-pass. Vectors: docs/protocol-vectors.json requests `req_set_seq`, `req_get_seq`, `req_read_seq`, `req_set_seq_average_256`, `req_read_seq_average_raw`, `req_read_raw_segment_5`, responses `resp_get_seq_segments_done`, `resp_read_seq_table`, `resp_get_status_1_17`, and the `seq` maths section (fit, hold-off, entry decode, slot words, UI plan, corrected mean) checked against pc_seq.c by check_vectors.py `--seq-checker`. **Unverified on hardware** (README section 6 bench checks; fw handoff firmware/AGENTS.md section 33): everything here; the readout pipeline of 0x2B..0x32 is read from the Verilog | maintainer: pulse-induction detector (coherent averaging of the decay after each TX pulse at full rate; segments with timestamps for per-pulse capture), community: multi-event capture, noise reduction; phosphor fast capture (docs/plans/phosphor-fast-capture.md) |
| 57 | Protocol 1.18: full-rate ADC capture check after rate writes, FPGA write trace (v0.2.61; the check is per capture since 1.19, row 58) | GET_INFO `proto_minor` = 18; `#VER` says `proto=1.18`. **Why** (bench 2026-10-19): on AL3 0x1532..0x1536 a 0x0D or 0x53 write can leave one ADC chip's encode clocks half a word out of phase with the RAM write clock (fpga/al3/README.md "Findings" item 3); at 200 MSa/s one core of that chip then returns garbage (CH1 core B 15-29 codes RMS in 27 of 60 rate changes on 0x1535). On 0x1537 static captures are clean, but after a 0x0D write to divider 0 CH2 core A read 32-34 codes in 35 of 40 trials, persisting until the next write (no settling with time). **Firmware**: after any 0x0D write, before the next UI conversion and before a host ARM, and in the calibration wizard before every full-rate measurement, one full-rate capture (500 samples per core, raw) is checked: per channel the RMS of core B against the mean of its two core A neighbours (smaller of the two neighbour choices, about the mean; signal and core offset cancel, noise leaves about 1.3 codes). Above 8 codes the firmware re-writes 0x0D (1, then 0) and checks again, up to 16 times (`enc_heal.h`: EH_LIMIT16, EH_MAX_TRIES, EH_WAIT_MS). Divider 0 only; every bitstream. **GET_LINK_STATS** grows to 64 counters: **62** first checks above the limit, **63** of those recovered. **GET_EVENT_LOG** type **21**: a check found a garbled capture, a = retries (bit 7: not recovered), b = the larger residual of the first check in 1/16 code. Type **22**: an FPGA write of 0x0D, 0x53, 0x56 or 0x01 = 1 (sampling reset), a = command, b = caller << 12 \| value (saturated at 0xFFF); callers 0 UI, 1 host (inside pc_poll), 2 calibration wizard, 3 v4 class probe, 4 the check. Type 22 is in the main ring only (not the bus ring); with the UI running each conversion adds one 0x01 entry, so the 128-entry ring covers a few seconds of UI acquisition: read GET_EVENT_LOG right after the capture under study. The wizard reports a check that never recovered as "ADC capture timing: the 200 MSa/s data stays garbled" (ACAL_E_TIMING 18). |
| 58 | Protocol 1.19: every full-rate capture checked, FPGA 0x1538 encode check (read 0x33), GET_HEALTH 104 bytes (v0.2.62) | GET_INFO `proto_minor` = 19; `#VER` says `proto=1.19`. **Why** (bench 2026-10-19 16:48, fw 0.2.61 on 0x1537): with one 0x0D write and then 1221 host captures in 60 s and no other write, CH2 core A was garbled (14-30 codes) in about 40 % of captures, randomly interleaved with clean ones: 0x1537 re-resolves chip 2's encode phase at every sampling reset (a marginal flop), so 1.18's check, which ran only after a 0x0D write, never fired. fpga-gen7's 0x1538 derives the encodes statelessly from the write clock and reports a per-capture self-check. **Caps bit 18 ENC_CHECK** (AL3 0x1538..0x153F) and features word bit 5 (0x0020, GET_STATUS 44 / GET_DECIM 32). **FPGA read 0x33** (one byte; the 0x33 write is still the CH1 relays): bit 0 sticky "an encode check failed since the last 0x01 = 1"; bit 1 the last write-clock prediction was right; bits 3:2 = 01 encode convention 1, core order guaranteed, B first (`interleave_order` 0); bits 7:4 the last pin check {CH2 A, CH2 B, CH1 A, CH1 B} (bit 7 .. bit 4), 1 = right. A good capture reads 0xF6. An asynchronous 0x0D / 0x53 write can make one prediction miss, which is why bit 0 clears at 0x01 = 1 and why those writes go before the reset. **Firmware**: after every capture at 200 MSa/s (divider 0, always the legacy 8-bit layout), before anything reads it (the UI readout, READ_RAW, the calibration wizard, the v4 class probe, the 1.00o5 calibration and auto setup), the capture is checked: on 0x1538+ by reading 0x33 (bit 0 set = bad); on 0x1532..0x1537 by the 1.18 residual (per channel the RMS of core B against the mean of its two core A neighbours, the smaller of the two neighbour choices, about its mean) over the record's first 250 words, from the same ring the readout reads (1000 bus bytes, about 0.3 ms; no extra conversion), bad above 8 codes. A bad capture is discarded and the conversion re-armed with the same setting (on 0x1532..0x1536 after re-writing 0x0D = 1, then 0: there only a rate write re-rolls the phase), at most 16 times per capture (`enc_heal.h` EH_MAX_TRIES; then the last capture is accepted and flagged). A host ARM therefore stays ARMED / TRIGGERED a little longer; WAIT, GET_CAPTURE_INFO and READ_RAW only ever see the accepted capture; in normal or single mode a re-arm waits for the next trigger. Not checked: other rates, other bitstreams (stock, EF2, EP4), and on 0x1536 / 0x1537 sequencer runs (segments and lane sums are not samples; 0x1538's flag covers a whole run, and a run STOP ended is not restarted). The 1.18 check after 0x0D writes (an extra conversion) is gone. On 0x1538 the firmware also reads 0x33 once at boot and requires bits 3:2 = 01 (else flag bit 1 below and an event). **GET_HEALTH** grows to 104 bytes (`length` 104; a 1.16 host reads the first 96): 88 u8 check mode: 0 none, 1 record data (0x1532..0x1537), 2 FPGA read 0x33 (0x1538+); 89 u8 the last 0x33 byte read after a capture (valid when 90 bit 0); 90 u8 flags: bit 0 a 0x33 byte was read since boot, bit 1 the encode convention at boot was not 1 (0x33 bits 3:2 != 01), bit 2 the last checked capture was accepted bad after 16 re-arms (cleared by the next good one), bit 3 core order differs from the convention (0x1537: recalibrate after flashing 0x1538); 91 u8 0x33 at boot (0x1538+, else 0); 92 u32 captures found bad (= GET_LINK_STATS 62); 96 u32 of those recovered by a re-arm (= 63); 100 u32 full-rate captures checked since boot. **GET_LINK_STATS 62-63** keep their slots, now per capture: 62 captures whose first check failed, 63 of those whose re-arm brought a good one (62 - 63 = accepted bad, or abandoned by a new conversion during the re-arms). **GET_EVENT_LOG type 21** (one entry per capture that needed re-arms, when it is accepted, and at boot): a = reason << 5 \| re-arms (bits 4:0, saturated at 31), bit 7 = not recovered; reasons 0 the 1.18 check after a rate write (no longer logged), 1 data check, 2 FPGA 0x33, 3 boot convention check (a = 0xE0); b = caller << 12 \| value (12 bits, saturated): callers as type 22 (0 UI, 1 host, 2 calibration wizard, 3 v4 class probe, 4 check), value = the first bad capture's larger residual in 1/16 code (reason 1) or its 0x33 byte (reasons 2, 3). Type 22 unchanged, except that the check writes (caller 4) are now only the 0x0D re-writes of a re-arm on 0x1532..0x1536. **Diagnostics** FPGA line: 0x1532..0x1536 amber "ADC capture timing race, fixed in 0x1538"; 0x1537 amber "ADC core order differs: recalibrate after flashing 0x1538"; 0x1538 from GET_HEALTH 88..103: "encode check OK" (with ", N captures re-taken" when 92 > 0), amber "encode check failed on CH2 A, ..." (the pins whose bit is 0) when 89 bit 0 is set, amber "unknown encode convention, 0x33 = 0x.." when 90 bit 1 is set, "encode check, no 200 MSa/s capture yet" before the first. The report file has a "Full-rate capture check" line. Vector: `resp_get_health_1_19`. | maintainer: on 0x1538 the rate-toggle test and 60 s of back-to-back 200 MSa/s captures show no garbled capture and GET_HEALTH 92 stays 0 (or 62 = 63); on 0x1537 the 40 % arms are re-taken (62 grows, 63 follows) and no READ_RAW returns a garbled core |
| 59 | Not used: the row numbers of parallel branches were settled at their merges and 59 was skipped (1.19 is row 58, 1.20 is row 60); no protocol change carries this number | - |
| 60 | Protocol 1.20: the on-device Bode plot, BODE_CTRL / READ_BODE, GET_STATUS display_mode 5, features bit 6 (v0.2.66) | GET_INFO `proto_minor` = 20; `#VER` says `proto=1.20`. **Mode** (firmware/README.md "Bode plot"): a main-loop display mode like the spectrum view, opened from the generator menu's fourth row "Bode plot" (maintainer) and left through the main menu's Scope or Spectrum analyser row (or the same row, "Close"). While it shows, the scope's own acquisition is off and the sweep engine drives the generator (duty 50 %), both channels (enabled, DC, ranges and offsets), the raw divider and the decimation filter; leaving gives back the generator, the channels (range, display V/div, offset, coupling, enable), the time base, the trigger source (EXT is set to CH1 on entry, protocol row 41) and RUN / STOP. Every command keeps working; host captures during a sweep end it (reason 5). **Plan** (docs/plans/bode-design.md 3.1-3.2): identical to the host tool's `plan_point` / `plan_sweep` (mcp/src/fnirsi1013d/bode.py): f = 50 MHz / M for integer M, N = 16 x 2^k with CIC3 + FIR below 1.5 MHz (257..512 samples per period below 24 kHz, M tied to N), legacy 200 MSa/s above, a coherent record of at least 8 periods and 2048 samples with an even number of p-blocks; points log spaced from start to stop (numpy geomspace), merged where M repeats. `host_tests/test_bode.py` checks the C planner against docs/bode-plan-vectors.json and 3000 random points and 300 random sweeps of the host's. **Per point** as the host tool's runner: a pre-scan at the first frequency on range 1 chooses both ranges (85 % of full scale) and centres the offsets; a background capture with the generator off per decimation and CH2 range; the settle time (presets 3 / 10 / 50 / 200 periods, three times at the first point and after the generator was off, at least 1 ms); a capture in FPGA auto mode with pretrigger 0 (no trigger needed: the record's start cancels in H); re-centring beyond 12 codes; one retry with twice the settle when the record halves disagree; CH2 auto-ranging (up on a clip or a saturated, flat record; down when the next range would stay under 60 % of full scale) with the splice measured at the switch point. **BODE_CTRL 0x0211** (main loop, cap DECIM; E_BUSY while a menu is open). Request 16 bytes (zero-extended): u8 action (0 status only, 1 run one sweep, 2 run repeated sweeps (RUN), 3 stop after the point in progress, 4 abort now, 5 measure the thru calibration (one sweep, both probe tips on the DUT input), 6 apply the thru, 7 do not apply it (kept), 8 clear it, 9 open the Bode view (no sweep), 10 close it), u8 flags (bit 0: bytes 2..13 hold the settings for this and later host sweeps, the screen's 1-2-5 steps follow rounded; without it the screen's settings are used; bit 1: no background captures), u8 settle (0 Fast, 1 Normal, 2 Resonant, 3 High Q), u8 CH2 range mode (0 auto, 1 hold: CH2's range when the sweep starts; the screen calls it Constant since fw 0.2.94: the V/div of the CH2 menu, never changed during the sweep), u32 start cHz, u32 stop cHz (1 Hz .. 10 MHz, either order), u16 points (1..200), u16 0. Actions 1, 2, 5 and 9 open the view first. Errors: E_BAD_ARG detail 0 action, 1 settle, 2 CH2 mode, 3 start / stop, 4 points; E_STATE detail 1 the view is not available (a picture or waveform viewer is open), 2 a sweep runs already (1, 2, 5), 4 no thru measured (6, 7). Response 40 bytes: the READ_BODE header, u32 the estimated duration of a sweep with the settings in effect (ms), u32 0. fw 0.2.91 (a layout addition, protocol number unchanged; hosts read 40 bytes and older firmware answers 40): 48 bytes, bytes 40..47 the latest sweep's capture statistics, u16 each: generator-on captures, those that ended on the FPGA's auto time-out (no CH1 trigger; the engine arms CH1 rising at CH1's expected mean code since 0.2.91, at 128 in 0.2.89-0.2.90), records taken again for CH1 leak above -40 dB (a record joined from two captures), records taken again for another reason (flat / toneless, or ended on the time-out). Counted from the sweep's start; the background captures (generator off) are not counted. fw 0.2.93 (a layout addition, protocol number unchanged; hosts read 48 bytes and older firmware answers 48): 50 bytes, bytes 48..49 u16 the latest sweep's planned CH2 range switches: a CH2 range change between two neighbouring points that the range plan prescribes (a plan sweep: on the bench RC 0.5 -> 0.2 -> 0.1 V/div, 2 per sweep; a fresh sweep has no plan and reads 0). The header's CH2 range switches (bytes 26..27) are the unplanned ones: the ranging of a fresh sweep, a step up after a clip, a step down after a drop below the floor; an unchanged circuit reads "2 planned, 0 unplanned" on every RUN sweep. On the screen (maintainer 2026-10-20 13:33) AUTO SET sets CH2 range Hold when the CH2 levels inside its span spread under 20 dB, on the most sensitive range whose full scale is at least 1.5 x the largest level, else Auto; a host's BODE_CTRL Hold keeps CH2's range when the sweep starts, as before. fw 0.2.94 (maintainer 2026-10-20 14:16): the screen says Constant for mode 1; Constant is the CH2 menu's V/div (the channel's own setting), which AUTO SET writes when it picks Constant; a sweep on Constant writes no other CH2 range (not even in the pre-scan): a clip or a drop only flags the point. A V/div chosen by hand in the CH2 menu while the view shows sets Constant (a sweep in progress starts again on it). AUTO SET's rule leaves out points whose record was bad (clipped, ch2_overload, unsettled, incoherent). The answer grows to 58 bytes (hosts read 50; older firmware answers 50): byte 50 u8 the latest on-screen AUTO SET's CH2 verdict (0 Auto, 1 Constant, 0xFF none since boot), 51 u8 its reason (0 one range holds the span, 1 a spread of 20 dB or more, 2 fewer than two points in the span, 3 a point without a tone, 4 no range holds the largest level), 52 u8 the points that counted, 53 u8 the points left out (a bad record), 54..55 i16 the spread (centi-dB), 56..57 i16 the largest CH2 level (centi-dBV peak at the tip). fw 0.2.71: action 3 (and the screen's STOP) ends the sweep after the capture in progress; that point keeps the capture's result without re-centring, retry or range change (after the whole point before). The sweep runs in the main loop: poll READ_BODE. **READ_BODE 0x0212** (main loop, cap DECIM). Request 8 bytes: u16 first row, u16 count (0 or more than fit = all that fit, 3326), u32 0. Response: the 32-byte header, then 20-byte rows. Header: u32 sweep id (increments with every start; 0 = none since boot), u8 state (0 idle, 1 running, 2 done, 3 stopped, 4 error), u8 reason (0 none, 1 the FPGA did not become ready, 2 no capture within twice the record time + 1 s, also after FORCE, 3 CH2 saturated by a DC level with CH2 range hold, 4 no frequency in the range, 5 the PC armed its own capture during the sweep, 6 stopped, 7 no decimation filter or no DDS generator on this bitstream, 8 (fw 0.2.71) ADC readout fault, the screen says "ADC readout fault: see Diagnostics"; fw 0.2.83: on a bitstream with the capture self-check (caps bit 19, 0x153A+) the self-check flagged a capture of the sweep (0x36 outliers after a 200 MSa/s capture, or GET_HEALTH 106 bit 5 REPEAT), at any phase; without it (0.2.71 rule) the pre-scan reached 5 V/div (range 0) with a channel's record still at code 0 or 255 and its fundamental under 20 codes, which the post's 3.1 Vpp cannot do. The sweep ends rather than produce a table of flagged points. Clipping (row flags bits 0, 1) is judged from each capture's own record since 0.2.83; 0.2.71-0.2.82 also took the readout's sticky overload flag of earlier readouts, so a clean readout could end in reason 8), u8 view (bit 0 the Bode view shows, bit 1 RUN repeats sweeps, bit 2 this sweep is a thru measurement, bits 3-4 CH1 / CH2's offset reached the PWM limit), u8 thru (bits 0-6: 0 none, 1 being measured, 2 valid for the current start / stop / points, 3 stale; bit 7 applied), u16 planned points, u16 measured points, i16 the point being measured (-1 none), u8 settle, u8 CH2 range mode, u32 start cHz, u32 stop cHz, u16 requested points (the latest sweep's settings; fw 0.2.71: before the first sweep since boot, sweep id 0, the settings in effect, the ones the BODE_CTRL estimate and the screen's Single sweep row describe; 0.2.66-0.2.69 showed the engine's defaults there), u16 CH2 range switches (fw 0.2.93: the unplanned ones; the planned ones are BODE_CTRL bytes 48..49), u32 ms since the sweep started (its duration once it ended). Row: u32 M (stimulus period in 50 MHz DDS clocks: f = round(2^32 / M) x 50 MHz / 2^32 exactly; N follows from M by the plan), u32 that f in cHz (rounded), i16 abs H in centi-dB, i16 arg H in centi-degrees (-180 .. +180], i16 V1 and i16 V2 in centi-dBV RMS (the fundamentals at the probe tips, probe factor included; V2 with the splice), u8 CH1 and u8 CH2 hardware range (0..5), u16 flags: bit 0 ch1_clipped, 1 clipped (CH2), 2 ch2_overload (a flat CH2 record: the front end saturated by a DC level), 3 ch1_low (CH1 under 20 codes), 4 ch2_small (CH2 under 3 codes: +-1 dB), 5 low_snr (under 20 dB above the noise), 6 near_floor (under 20 dB above the generator-off background in its bin), 7 unsettled, 8 incoherent (CH1 one bin beside the fundamental above -40 dB), 9 spliced, 10 fir_off, 11 no_cal, 12 cal_range, 13 cal_flagged, 14 retried, 15 not measured in the latest sweep (the rest of the row 0). Bits 0, 1, 2, 4, 5, 6, 7 and 8 make a point invalid (the host's BAD_FLAGS; the screen draws it grey; bit 4 ch2_small since fw 0.2.99, maintainer 2026-10-20 16:22: CH2 under 3 codes is under the floor). With the thru applied, H is divided by the thru's H at the same (M, N) (bits 11-13 say where that does not hold). **Measurement** (bode_dsp.c, integer only): mean removed, rectangular window over the coherent record (Hann where the 32-bit increment leaves it more than 1e-3 periods off, as the host), single-bin DFT on a 2^-34 turn phase grid at f exactly, H = X2 / X1 with each channel's scale as its readout converts (decimation scale x gain16, corr16, volts per code of the range, probe), levels in mdB from fx_log2_q16, angles from a CORDIC; host-tested against measure_pair: H within 0.002 dB / 0.002 deg on the simulator's RC. Noise: the median of the period differences x[n] - 2 x[n + p] + x[n + 2p] (white noise as the host's median over FFT bins; a transient spanning most of the record raises it where the host's does not, so such a point reads low_snr rather than unsettled). Markers on the screen by linear interpolation in log f (host: monotone cubic). **GET_STATUS** byte 43 = 5 while the view shows; GET_STATUS / GET_DECIM features word bit 6 (0x0040) = the bitstream has what the Bode plot needs (DECIM and GENERATOR_DDS: AL3 0x1534+). Settings words 219-220 keep start, stop, points, settle and CH2 range mode (markers 0xB000 / 0xB100); the thru calibration lives in RAM. Not built: the harmonic fast mode, impedance mode, AC coupling for CH2 (bode-design D5) | bode-design steps D1-D4; the device is about 2-3 x faster than the host above 1 kHz (no USB round trip per point) and runs without a PC |
| 61 | Protocol 1.21: CAL_PROBE, the calibration wizard's zero measurement from the host (v0.2.67) | GET_INFO `proto_minor` = 21; `#VER` says `proto=1.21`. **Why** (bench 2026-10-19 17:55, fw 0.2.64 on 0x1538, nothing connected): the wizard's Zero level step failed every time (CH1 "too much noise, range 1 at 200 MSa/s: core B 7.2 codes, 7.2 per capture", CH2 "the input is not at 0 V" = a sample at code 0 or 255) while host READ_RAW at the same range and rate read 0.6-2.0 codes RMS per core and no clipped sample over 1233 captures. CAL_PROBE runs the wizard's own measurement, so the host can see what the wizard sees without the maintainer. **CAL_PROBE (0x0402)**, no capability bit (an older firmware answers E_UNKNOWN_CMD). Request: u8 `channel` (0, 1), u8 `range` (hardware range 0..5, 0 = 5 V/div = the wizard's "range 1"), u8 `settle` (optional, 0..15: extra reads of the FPGA data port after each sample strobe before the sampled read, an A/B test of the bus readout), u8 `prestrobe` (optional, 0 or 1: a data-write strobe with the bus undriven before each sample strobe, so the sample memory's read register holds the address before the strobe that takes it; the samples then come one word earlier); both 0 = the wizard's readout; u8 `mode` (optional: 0 one range as described below; 1 the whole zero step exactly as the wizard runs it after a Calibrate tap, in its screen context: the step dialog over the dimmed screen, the progress lines and the Cancel polls, both channels, every range, `range` ignored; the response then carries `channel`'s last measured range, the failing one, or range index 5 when it passed, with rc -1 when it was cancelled on the screen; about 15 s). Main loop only (a BUSY or timer poll leaves it for the main loop); refused with E_STATE while the UI acquisition runs (detail 1: send SET_ACQ_STATE 0 first) or a host capture is ARMED / TRIGGERED (detail 0); E_BAD_ARG for channel > 1 (detail 0), range > 5 (detail 1), settle > 15 (detail 2), prestrobe > 1 (detail 3) or mode > 1 (detail 4). It runs `cal_begin`, the scratch channel of `channel`, the relay write and 60 ms, then `cal_range` (cal_wiz_ui.c: coarse points PWM 500 and 1200 gated, 2 conversions each; slope points centre -+ 150 gated, 4 each; the zero gated as two points d counts either side of the refined centre, 8 each (d from the gated slope so that each is 12 codes from 128, 8..120 counts); then 200 MSa/s: the zero pair, + 150, - 150, 64 each; every conversion 1000 samples, 500 per core, read from word 100), then `cal_end` (channels, rate, raw host divider, time base, trigger mode back). Nothing is stored; the first 1000 bytes of the channel's trace buffer are put back. About 1 s (mode 1 about 15 s); the host should allow 5 s (30 s). The session's modal watch restarts after it, so the next request is answered at once (the 0.2.67 diagnostic image answered E_BUSY "UI busy" right after a probe). Response, 252 bytes, little-endian: 0 u8 channel; 1 u8 range; 2 i16 rc (ACAL_* of adc_cal.h: 0 OK, 4 RESPONSE, 5 CLIP, 6 NOISE, 7 ZERO, 8 SPREAD, 9 SLOPE, 10 DRIFT, 11 CLASS, 18 TIMING, 19 READOUT: samples at 0 / 255 only at 200 MSa/s with the gated twin point quiet and within 8 codes of 128, the FPGA 0x1538 readout fault); 4 u8 fail point (0xFF none); 5 u8 fail class (0 full rate, 1 gated); 6 u8 fail core (0 A, 1 B); 7 u8 flags (bit 0 a full-rate capture was accepted garbled after the re-arms, bit 1 the coarse centre failed: no fine points, bit 2 prestrobe used, bit 3 mode 1, bits 7:4 the settle reads used); 8 i32 fail value (as the wizard's `acal_fail`: noise / drift 1/16 code, slope Q16, ...); 12 u32 within-capture RMS of the failing value, 1/16 code; 16 u16 coarse centre PWM (0 none); 18 u16 refined zero PWM (0 none); 20 u16 full-rate captures flagged by the encode check during the probe (GET_HEALTH 92 delta); 22 u16 of those recovered (96 delta); 24 + 28 i: point i = 0 coarse high (PWM 500), 1 coarse low (1200), 2 gated slope low (centre - 150), 3 gated slope high, 4 gated zero (the pair, merged: PWM the centre, mean the mean of the two, RMS values the RMS of the two points' own, extremes and clipped of both), 5 full-rate zero (the same), 6 full-rate high, 7 full-rate low: +0 u16 PWM, +2 u8 class, +3 u8 conversions (0 = not measured), then per core A at +4 and B at +16: i32 mean (1/16 code), u16 RMS about the mean of all its conversions (1/16 code, the value the noise limit of 48 checks), u16 RMS inside the conversions (1/16 code), u8 lowest code, u8 highest code, u16 samples at code 0 or 255; 248 u16 the zero split d in PWM counts (0 when not reached); 250 u16 0. The packer is `acal_probe_pack` (adc_cal.c, host test test_adc_cal `test_points`). The wizard's failure line got the same numbers as a note: range, rate, point, PWM, per-core mean with lowest..highest code and clipped samples (`cw_zero_note`). |
| 62 | Protocol 1.22: the FPGA bus contract, legacy EXT shift, FPGA write counters and the type 22 trace level (v0.2.72) | GET_INFO `proto_minor` = 22; `#VER` says `proto=1.22`. **Why**: fpga-gen6's contract findings on 0x1532..0x1536 (fpga/al3/README.md section 7, findings 2, 4 and 5) and the 0.2.62 bench on 0x1538 (a host rate write set the 0x33 sticky bit and cost a re-arm: 112 of 5798 captures; every UI conversion's 0x01 entry of type 22 flooded the 128-entry event ring). **Readout** (firmware/AGENTS.md section 48, `fpga_bus.c`): every reader starts with 0x1F, the read command and the bitstream's dummy strobes (one on every known bitstream, 0x1539 included). The UI readout, and with it **READ_TRACE**, the spectrum and the 16-bit trace, read mem[S], mem[S], mem[S + 1], ... up to v0.2.69: one word (two samples) late with a stale first byte; since 1.22 they return the same words as READ_RAW (section 7.4). The UI record start is (0x14 - the 0x0B words) mod the ring (1.00o5 wrapped with ring - 751, one word off on stock and (751 - pretrigger) words off on AL3 / EF2 when 0x14 lay within the pretrigger of the ring start). **Arm**: 0x0B / 0x0C are written before 0x01 = 1 as well as inside the window, on every bitstream that takes them (0x1532..0x1536 kept the previous pretrigger when no write clock edge came between a 0x0B written after the 0x05 poll and the release). **EXT in the legacy layout**: the AD9288 pipeline delays the data 4 words behind an EXT trigger; the firmware writes (pretrigger - shift) / 2 words, shift = 8 samples (4 at divider 0), at most the pretrigger in whole words; GET_CAPTURE_INFO 70 and GET_DECIM 31 now report it for K = 0 too (samples; the record start at 12 already includes it). Channel enable toggles keep an EXT source. **GET_LINK_STATS** grows to 68 counters: **64** FPGA 0x0D (rate) writes, **65** 0x53 / 0x56 (filter) writes (since v0.2.87 also 0x54 shift and 0x55 16-bit level writes), **66** 0x01 = 1 sampling resets (UI, host, calibration, probe and re-arms alike), **67** full-rate captures on 0x1538+ judged by their record data instead of read 0x33 because a 0x0D / 0x53 / 0x56 write came after their sampling reset (0x33 bit 0 is sticky until the next 0x01 = 1, so it cannot judge them; such a capture is still checked, by the 1.18 residual, and re-armed only when its data are garbled; event 21 reason 1 then). **GET_EVENT_LOG** request flags bits 2:1: 0 leave the FPGA write trace as it is, 1 off, 2 on, 3 E_BAD_ARG; bit 0 as before. Event type 22 is logged only while the trace is on, which it is not after boot (since v0.2.87 it also logs 0x54 and 0x55; a 0x55 level above 0xFFF reads 0xFFF). The response's u16 at byte 2 (0 before) has bit 0 = trace on. Vectors: `resp_get_link_stats_1_22`, `req_get_event_log_fpga_trace_on`. **Core order** (row 58): caps bit 12 INTERLEAVE_UNVERIFIED is clear on 0x1538+ when read 0x33 at boot had bits 3:2 = 01 (the bitstream reports its encode convention, B first); otherwise it stays set, and on a 0x1538+ with another convention (GET_HEALTH 90 bit 1) the full-rate check also moves to the record data (GET_HEALTH 88 = 1): nothing assumes the order there. | maintainer: firmware/AGENTS.md section 48 bench list |
| 63 | Protocol 1.23: host requests close the modal screens instead of E_BUSY (v0.2.78) | GET_INFO `proto_minor` = 23; `#VER` says `proto=1.23`. **Why** (maintainer 2026-10-19 23:27): "avoid locking up the device from the perspective of the MCP server when it's in menus; if the agent sends commands it just exits all modals, since that wouldn't be destructive; just exiting to the base screen". **Rule** (`docs/ui-guidelines.md` section 4, firmware/AGENTS.md section 53): a request that needs the main loop (not in the F_BUSY set that the timer interrupt and the touch scans answer: PING, GET_INFO, GET_STATUS, SET_REMOTE, GET_BATTERY, GET_LINK_STATS, GET_BAD_FRAME, GET_EVENT_LOG, GET_HEALTH, GET_CAPTURE_INFO, SD_STAGE) while the UI is modal (no main-loop pass for 250 ms, GET_STATUS `ui_busy` = 1) is no longer answered E_BUSY. The receiver (timer or touch-scan poll) sets a yield flag and keeps the request queued; every modal loop checks the flag once per iteration and leaves by its own cancel path (a menu or fold-out closes, a confirmation answers Cancel / No, Settings goes Back, the file browser and viewers Return, a file message is dismissed, the first-run screen answers Later, the calibration wizard and the volts calibration Cancel between steps, nothing written), nested ones one after the other, down to the base screen; the main loop then answers the request as usual, silently (fw 0.2.78-0.2.80 showed "Host command: menu closed" for 4 s; fw 0.2.81 dropped the note, maintainer 2026-10-20). The yield never takes a path that writes something the person did not confirm. The spectrum and the Frequency response views are main-loop modes and stay open. Diagnostics is a main-loop screen over the view: up to fw 0.2.95 it stayed open too and the request's redraws of the view went over it; since fw 0.2.96 the main loop leaves it (as Back) before running a request outside the F_BUSY set and the read-only set below, so the view shows. **fw 0.2.94 (protocol number unchanged): read-only requests do not unwind.** READ_SCREEN, GET_CHANNEL, GET_TIMEBASE, GET_TRIGGER, GET_DECIM, GET_SEQ, GET_GEN, GET_CAL and READ_BODE (they only read state) are answered at the modal loop's yield check, between its iterations, with the menu, dialog or screen left open, so a screenshot of Settings, a menu, a file viewer, First start or the wizard shows that screen (0.2.78-0.2.93 closed it first). The F_BUSY set (PING, GET_INFO, GET_STATUS, ...) is answered from the timer as before; every other command still closes the modal screens. A read-only request queued in front of a state-changing one is answered first, then the screens unwind for the second. The E_BUSY details are the same: a hold reason (3, 4, 5) at once, detail 2 when no yield check comes within 1.5 s (a UI pass without one, e.g. auto set); these count in GET_LINK_STATS 69, while 68 counts only real yields. BODE_CTRL (all actions, also status) runs on the main loop, so it closes a menu and Diagnostics too; READ_BODE is read-only (above) since fw 0.2.94. **E_BUSY now** (detail and text, section 4.2): 0 "deferred request pending" as before; 1 "UI capture running" (READ_RAW, as before); 2 "UI did not return to the main screen" (no main-loop pass within 1.5 s of the yield request: auto set, a slow capture); 3 "calibration measurement running" (a wizard step or its Save, a volts calibration step; the step runs to its end); 4 "file save in progress" (SAVE PIC, SAVE WAVE, the Frequency response CSV, the Diagnostics report); 5 "USB drive mode" (the serial port is gone there anyway). Before 1.23 a modal UI gave detail 0 "UI busy": hosts show "close the menu on the scope" only for proto_minor < 23. **GET_LINK_STATS** grows to 70 counters: **68** requests that asked the modal UI to yield, **69** E_BUSY answers instead (a hold reason or the time-out). GET_STATUS `ui_busy` is unchanged (1 while modal, also while a yield is under way). Vectors: `resp_get_link_stats_1_23`, `resp_e_busy_cal_1_23`. | maintainer: firmware/AGENTS.md section 53 bench list |
| 64 | Protocol 1.24: FPGA 0x153A capture stages, training, self-check and the stored value; GET_HEALTH 124 bytes, GET_CAL 364 bytes, event 23, the EXT shifts on 0x153A, the wizard's zero back at code 128 (v0.2.79) | GET_INFO `proto_minor` = 24; `#VER` says `proto=1.24`. **Why** (bench 2026-10-19, section 5.1 "0x153A"): every build 0x1532..0x1539 sampled the ADC pins at an instant set by placement and garbled some core near code 128 (0x1538 CH2 A + B, 0x1539 both A cores); fpga-gen8 / gen9's 0x153A takes every pin in an IOB register and picks one of six 2.5 ns stages per core (write 0x63), and counts outliers per core (read 0x36), so the firmware can choose the stage on the unit and check every full-rate capture. **Caps bit 19 CAPTURE_STAGES** (AL3 0x153A..0x153F) and features word bit 7 (0x0080, GET_STATUS 44 / GET_DECIM 32). **Boot** (and after a calibration load): 0x64 = 8 (outlier threshold, codes) and 0x63 = the stored value when it was trained on the running bitstream (its FPGA version word), else the power-on 0x08E4 (CH1 A4 B4, CH2 A3 B4); always written, since a FEL load leaves the FPGA as the previous firmware left it. **Self-check**: after every accepted 200 MSa/s capture (UI, wizard, host ARM; after the encode check of row 58) the firmware reads 0x36; a core is flagged when its count minus its chip partner's is above 8 (a signal moves both cores of a chip alike; inference, not benched: a large signal near 100 MHz could make them differ by itself). Nothing is re-armed. **Training** (calibration wizard, zero step, first thing per channel on 0x153A): range 0, the input parked at code 128 by the gated centre search, a quiet input required (gated within-capture RMS <= 3 codes, else ACAL_E_NOISE); per core stages 2..5 in turn, one 8000-word 200 MSa/s capture each, 0x36 read (a clean stage reads 0); per core the clean stage of {3, 4} whose outer neighbour (2 or 5) is clean too (the power-on one of the two when the clean run is symmetric), else the clean one of {3, 4}, else a clean reserve (2, 5); a B core with no clean stage is measured again with its chip's reference bit (0x63 bit 12 / 13, A-encode timing); **fw 0.2.81 (band training, wire unchanged)**: instead of one capture at the centre, the quiet input is parked by the offset PWM at codes 120, 124, .. 140 (interpolated from the centre search's gated slope points) with two 8000-word captures per stage and point, the 0x36 counts summed per core and stage (clean = sum 0; threshold 0x64 = 32 since 0.2.80), and per core the clean stage inside the widest run of clean stages with the most clean stages on both sides is taken, 3 or 4 before a reserve, the power-on stage only as the tie-break inside that run (bench 2026-10-20: CH1 A4 garbled at codes 127..135 only, CH1 A clean at 2, 3, 5 now gives 3); a core with no clean stage fails the channel with ACAL_E_READOUT, "ADC readout fault near code 128: check the FPGA bitstream" (nothing stored). A passed channel's note reads "Capture stages: CH1 A4 B4". Save stores the value; Cancel and CAL_PROBE restore the previous 0x63. **Stored** in sector 708 bytes 488..503 (after the provenance trailer, outside the v3 block; an older firmware's 708 save clears it): u32 'ACAP' (0x50414341), u16 version 1, u16 length 16, u16 the 0x63 value, u16 the FPGA version word it was trained on, u32 zlib.crc32 of the 12 bytes before. **GET_HEALTH** grows to 124 bytes, byte 0 layout = 2 (was 1), byte 2 length 124 (a 1.19 host reads the first 104): 104 u16 the 0x63 value in effect (0 without the stages); 106 u8 flags: bit 0 HAS (0x153A+), bit 1 TRAINED (the stored value for this bitstream is in effect), bit 2 READ (counts read after a full-rate capture since boot), bit 3 FLAGGED (the last read flagged a core), bit 4 OTHER (a stored value exists for another bitstream, not applied: recalibrate), bit 5 REPEAT (fw 0.2.81, GET_HEALTH only: a core was flagged in 3 captures within a minute since the last saved training: recalibrate; cleared by the wizard's Save; 0.2.79-0.2.80 send 0); 107 u8 cores flagged by the last read (bit 0 CH1 A, 1 CH1 B, 2 CH2 A, 3 CH2 B); 108 u16[4] the last 0x36 counts CH1 A, CH1 B, CH2 A, CH2 B; 116 u32 full-rate captures whose counts were read; 120 u32 of those with a flagged core. **GET_CAL** grows to 364 bytes (the same for both channels): 356 u8 trailer state (0 none, 1 valid, 2 bad: magic present, version, length or CRC wrong); 357 u8 flags (HAS, TRAINED, OTHER as GET_HEALTH 106); 358 u16 the stored 0x63 value and 360 u16 the FPGA version word it was trained on (0 unless valid); 362 u16 the 0x63 value in effect. **GET_EVENT_LOG type 23**: a bits 3:0 = the cores flagged by the self-check, b = the largest excess (0.2.79-0.2.80: logged when the set of flagged cores changes; fw 0.2.81: logged once per core when that core has been flagged in 3 captures within a minute, a = the cores that reached it with this capture, and again only after a saved training); a bit 4 set = a training ended: bits 3:0 the cores without a clean stage, b = the 0x63 value in effect. **EXT** on 0x153A (the IOB capture and the stage pick delay the data, not EXT): legacy layout 12 samples at divider 0 and 14 above (0x1539 and older: 4 / 8), K >= 1 lag + 40 ns (GET_CAPTURE_INFO 70, GET_DECIM 31); 0x53 bits 3 / 4 (NEG_A / NEG_B) are written 0 on 0x153A (they select nothing). **CAL_PROBE** (row 61): byte 248 (the zero split d) is always 0: the wizard measures the zero at the centre PWM again on every bitstream (0.2.67's two-point off-centre zero removed, maintainer 2026-10-19 20:30: a bad bitstream must fail loudly). **Diagnostics** Calibration page and the report file name the stages, trained or not, and a flagged core. Vector: `resp_get_health_1_24`. | maintainer: firmware/AGENTS.md section 54 bench list (flash 0x153A first) |
| 65 | Protocol 1.25: one built-for FPGA bitstream with a mismatch notice, and one averaging behaviour (fw 0.2.103) | GET_INFO `proto_minor` = 25; `#VER` says `proto=1.25`. **Why** (owner, 2026-10-20 21:52 and 21:59): "every firmware version only supports the latest FPGA bitstream ... detect this and show a loud banner", so no combinatorial flags; and the ACQ Average must give one kind of result whatever the filter and time base. **One bitstream** (section 5.1): `PC_FW_FPGA_VERSION` (0x153B) is the one constant; the capability word, ring 8192, max total 16382 and every feature decision are the firmware's; the version word is read once at boot and compared. GET_INFO grows to 80 bytes: 76 u16 the built-for word, 78 u8 flags bit 0 FPGA_MISMATCH (the word read differs, older or newer), 79 0; a 1.24 host reads the first 76. GET_HEALTH grows to 128 bytes, layout byte 3: 124 u16 the built-for word, 126 u8 the same flags, 127 0. On a mismatch every command outside the basic set (section 5.1 lists it) answers E_UNSUPPORTED detail 1 "FPGA bitstream mismatch: this firmware needs 0x153B", GET_STATUS 42 ext_flags stays 0 (0x28 is not read), nothing is written to feature registers at boot (0x33, 0x63, 0x64), and the scope shows the notice screen (Diagnostics: the FPGA line reads "FPGA bitstream 0x1532: this firmware needs 0x153b" in amber, no Calibrate / DC shift trim / Volts calibration buttons; the report file says "firmware built for 0x153b: MISMATCH, the acquisition does not run"). Removed with it: the stock 0x1432 / Atlan 0x1532 / EF2 / EP4 arms (fixed geometry, no generator, `fpga_variant` 0 / 1 / 3 / 4, "unknown FPGA" E_UNSUPPORTED detail 0), the 0x1532..0x1537 encode-heal and core-order workarounds (GET_HEALTH 88 is 2, or 1 only after a bad boot convention; 90 bit 3 "core order differs" is never set), the fw 0.2.97 stop geometry and GET_CAPTURE_INFO flags bit 6 (never set), the per-bitstream bus contract, and the capability branches in SET_TRIGGER (EXT, the fixed 1500 / 3000), SET_DECIM (the FIR flag), READ_RAW addr_mode 3, GET_GEN (the stock 1 kHz). **One averaging behaviour** (no host command): with the acquisition menu's Average on, every displayed UI record is the mean of N records on both paths. N comes from one planner (`pc_seq_ui_plan_n`, pc_seq.c): the largest power of two in 2..64 whose N records fit the 40 ms budget, a record costed at its acquisition time plus the sequencer's dead time (row 56) and, on the MCU path, its readout (4 bus bytes per ring word at 0.3 us). A legacy record that fits the accumulator (total <= 2046 words) is a hardware run as in row 56; every other record (16-bit, or deeper than 4092 samples) is averaged by the MCU: the main loop's conversion is record 1, and once it is done and triggered the firmware arms the same conversion N - 1 times within the pass, sums the lanes per sample (32-bit sums) and feeds the exact mean (legacy: per core through the core's gain / offset as row 56; 16-bit: the rounded mean through the row 47 / 49 conversion). Deadline as the hardware run's: max(100 ms, two records) + N records; a record 2..N not done by then is discarded (0x1A = 0) and the pass shows the mean of what it holds; an untriggered record 1 (the firmware auto time-out) is shown alone and the next conversion tries again. Numbers: 3000 16-bit samples at 1 MSa/s N = 4 (about 27 ms), 6000 legacy samples at 200 MSa/s N = 8, 16382 legacy N = 4, 10 kSa/s and slower N = 1 (single records). Atlan's running (trace + previous) / 2 average is gone. GET_STATUS 46 = 0 off, 1 the MCU's mean, 2 a hardware run; 47 = log2 N (0 = single records). READ_RAW of a UI capture reads the last record of the N; READ_TRACE the mean. Vectors: `resp_get_info_1_25`, `resp_get_health_1_25` (if regenerated by the mcp agent). | owner: one firmware, one bitstream, one Average |
| 66 | Protocol 1.26: FPGA 0x153C trigger types and hold-off: SET_TRIGGER2, GET_TRIGGER 52 bytes, the hold-off accepted, caps bits 2 and 20 (fw-c5, version) | GET_INFO `proto_minor` = 26; `#VER` says `proto=1.26`. **Built for 0x153C**: `PC_FW_FPGA_VERSION` 0x153C (GET_INFO 76; a scope still on 0x153B is a mismatch and shows the notice, section 5.1); the mismatch text names the word (`PC_FW_FPGA_VERSION_TEXT`). **Caps**: bit 2 HOLDOFF (0x1B is the trigger hold-off in 5 ns units) and bit 20 TRIG_TYPES (0x5E / 0x5F / 0x60, reads 0x34 / 0x37, SET_TRIGGER2); features word (GET_STATUS 44, GET_DECIM 32) bit 8 (0x0100) TRIG_TYPES (the contract's bit 5 was taken by ENC_CHECK, 6 by BODE, 7 by CAPTURE_STAGES; the contract's later bits shift by three: C6 PEAK takes 9, C7 COUNTER 10). **SET_TRIGGER2 0x0219** (section 8.3): 24 bytes, 0 u8 type (0 edge, 1 pulse width, 2 Nth edge, 3 timeout; 4 runt / 5 window = 0x153E, E_BAD_ARG detail 0; 6+ detail 0), 1 u8 polarity (0 / 1, = 0x16 = GET_TRIGGER edge; per type as the FPGA defines it), 2 u8 flags (bit 0 LONGER, pulse width only; bits 1 RANGE / 2 BOTH_CORES = 0x153E and other bits E_BAD_ARG detail 2), 3 u8 source (0 CH1, 1 CH2, 2 EXT; detail 3), 4 u32 N (pulse width / timeout 1..16777215 samples, Nth edge 1..65535 edges, edge 0; detail 4), 8 u32 H (Nth edge 0..16777215 samples, 0 = count from the arm; other types 0; detail 8), 12 u16 level (0..255; detail 12), 14 u16 level2 (0; detail 14), 16 u32 holdoff (5 ns units, 0 off), 20 u32 0. No mask: every field is written; the firmware converts nothing (N and H are samples of the trigger stream: one memory word on CH1 / CH2, 5 ns on EXT; the UI's Type row converts time to samples later, docs/plans/ui-rows.md). Response TRIGGER. **GET_TRIGGER** grows to 52 bytes: 12 u32 holdoff = the 0x1B value; 28..51 = the SET_TRIGGER2 body that reproduces the trigger (type, polarity, flags, source, N, H, level, 0, holdoff, 0), so a host round-trips the trigger with one struct. **SET_TRIGGER** mask bit 6 `holdoff` is accepted (5 ns units, written to 0x1B with the clear request; the type and its parameters are kept); E_UNSUPPORTED detail 6 up to 1.25. **Writes** (fpga_bus.c `fpga_trig_write`): SET_TRIGGER2 writes 0x15 / 0x16 / 0x17 as SET_TRIGGER, then 0x5E, 0x5F and 0x60 (the last two for a type other than edge), 0x1B, 0x66 = 1 (the next 0x01 = 1 clears a running hold-off: a new definition is a new session; ARMs between definitions keep it running across re-arms, the FPGA's rule, so a train's pulses are skipped); boot writes the power-on state (edge, 0x1B = 0) and 0x66 = 1 once (a FEL load leaves the FPGA as the previous firmware left it). **Arm** (8.4 step 4a, `fpga_trig_arm`): every arm, UI or host, re-writes 0x5E and (type != edge) 0x5F / 0x60 inside the 0x01 = 1 window after the second 0x0B / 0x0C and before 0x0E; the FPGA holds its qualifier in reset until the first write clock edge after the release, so they apply at every rate (fpga/al3/AGENTS.md "0x153C C1", deviation 2). **GET_STATUS** grows to 52 bytes: 48 u8 FPGA 0x37 ({ 0, 0, 0, hold-off running, type bit 2, 0, 0, 0 }), 49 u8 0x34 byte 0 ({ 0, 0, counting, pulse began, EXT, longer, type[1:0] }), 50 u16 the edges still to count (Nth edge), read at every MAIN poll while a host capture is in flight (6 strobes), zeroed at ARM, the last values kept after DONE. **GET_CAPTURE_INFO** grows to 112 bytes: 96 u8 type, 97 u8 flags, 98 u16 0, 100 u32 N, 104 u32 H, 108 u32 holdoff as armed (the polarity is byte 22, the level 23). The trigger extension is part of the settings fingerprint (`settings_seq` moves on a type / N / H / hold-off change). Trigger positions are the edge type's (no new EXT correction); an auto capture whose time-out falls inside the hold-off ends untriggered with the 0x153B address rule. Vectors: `req_set_trigger2`, `req_set_trigger2_nth_edge_ext`, `req_set_trigger2_pulse_longer_max`, `req_set_trigger2_timeout`, `req_set_trigger2_edge`, `resp_get_trigger_1_26`, `resp_get_status_1_26`, `resp_get_capture_info_1_26`, `resp_set_trigger2_e_bad_arg_runt`. **Peak detect (fw-c6, item C6, cap bit 21 PEAK_DETECT, features bit 9; FPGA 0x56 bit 1, `docs/plans/fpga-0x153c.md` section 4).** The ACQ Sample row's state, one Scope setting (the Spectrum view always acquires Normal), not saved in the settings sector until the UI row lands. **SET_DECIM filter 4** = Sample Peak: the Filter's K and FIR are kept behind it for the way back (not applied); flags bits 0-2 as for the filter, flags bit 3 (FIR) with filter 4 is E_BAD_ARG detail 1 "FIR with peak detect", filter 5+ detail 0; filter 0..3 = Sample Normal with that filter. The eligibility rules are the filter's: at divider 0 (200 MSa/s: every sample is stored, nothing to catch) and for totals over 8190 the record stays legacy (GET_DECIM why 2 / 3) with the setting kept; SET_TRIGGER total > 8190 is refused while Sample is Peak as with the filter on. **The record**: the 16-bit geometry (one word per sample per channel, pretrigger / total / 0x14 / EXT shift as for K >= 1, the K = 1 boxcar's latency), each word = { max (high byte, 0x21 / 0x23), min (low byte, 0x20 / 0x22) } of the window's N = 2 (floor(D / 2) + 1) raw 8-bit codes, the ADCs at 200 MSa/s; the firmware writes 0x53 = 1 (K 1, the contract's register value), 0x54 = 0, 0x56 = 2 and the trigger level 0x55 = raw << 8 (the raw code whose mean-core correction gives the UI level; the FPGA compares the window max for a rising, the min for a falling detection). **The bytes**: the mode byte (GET_DECIM 2 / 24, GET_CAPTURE_INFO 68, READ_TRACE 30) reads 0x81 (16-bit path, K 1, no FIR); the **shift byte** (GET_DECIM 3 / 25 / 27, GET_CAPTURE_INFO 69, READ_TRACE 31) carries bit 7 peak active, bit 6 peak as written (equal on 0x153C: active whenever written) and S in bits 5..0, so a peak record reads 0xC0; GET_DECIM 0 = 4 while Sample is Peak (byte 1 bit 3 reads 0: the FIR is not applied), 4 N = the window, 8 gain 1, 20 the rate 200 MSa/s / N, 30 why 0 (2 / 3 when legacy), 31 the EXT shift of the boxcar's latency; **GET_CAPTURE_INFO 79** bit 1 = a peak record, bit 0 = 16-bit records (since 1.26 for single captures too, not only runs); GET_SEQ 17 = 1. **READ_RAW layout 3** = peak pairs: per selected channel (both cores required) `count` u8 minima then `count` u8 maxima; layout 2 asked on a peak record is E_BAD_ARG detail 2 "peak-detect record: layout 3" (a 1.24 host would take the pairs for filter samples), layout 3 on any other record detail 2 "layout 3 needs a peak-detect record"; the record judged is the capture in flight while armed (ALLOW_NOT_DONE), else the latest capture; layouts 0 / 1 read the lanes as they are. **READ_TRACE** of a peak record: the 8-bit trace is the **corrected midpoint** of min and max per word (the one trace the screen, the measurements and the software trigger use; a spike shows at half its height there, the Scope view's bars of the UI item draw the pairs); flags bit 2 (WIDE) returns the pairs as u16 (min low byte, max high byte) as stored. **Correction**: the two bytes of a word are raw codes from either core, so one 8-bit correction is applied to both: the mean of the two cores' gains (round((gain_a + gain_b) / 2)) and offsets ((off_a + off_b) / 2) of the **full clock class** (the ADCs run at 200 MSa/s under peak detect as on the filter path; GET_CAL 92..107 report the class of the current divider, i.e. the gated class at D > 0, so a host reproducing the firmware's codes needs the full-class values: at divider 0, or from the sector), with the v4 tracking folded in; code = clamp((raw x gain + off + 2^15) >> 16), midpoint = clamp(((min + max) gain + 2 off + 2^16) >> 17); vectors section `decim_peak`. **Overload**: a peak word is overloaded when either byte is 0 or 255 (GET_CAPTURE_INFO flags bits 2 / 3, GET_STATUS flags bits 6 / 7, READ_TRACE meta bits 1 / 2), whatever the midpoint reads. **Averaging**: SET_SEQ mode 2 while the next arm is a peak record is E_STATE detail 7 "peak detect: averaging refused (SET_DECIM filter first)" (the lane sums would mix max and min bytes); an averaging setting made before Sample Peak reads GET_SEQ 16 = 9 and ARM answers E_STATE 3; segments fit; the UI's Average is not applied while Sample is Peak (GET_STATUS 46 = 0). **Settle**: after the 0x56 write as after a filter write, (1 + 10) N x 5 ns + 1 us. Vectors: `resp_set_decim_e_bad_arg_fir_peak`, `resp_set_seq_e_state_peak`, `resp_read_raw_e_bad_arg_peak_layout2`, the host side's `host_req_set_decim_peak`, `host_resp_get_decim_peak`, `host_req_read_raw_peak`, `host_resp_read_raw_peak`, `host_resp_get_capture_info_1_26_peak` (mcp-c8), the `decim_peak` maths section. **fw-c7 addition, the hardware counter** (item C7, caps bit 22 COUNTER, features bit 10 (0x0400); the wire format fixed by the coordinator and the mcp (mcp-c8), byte-exact with its `host_` vectors): **GET_COUNTER 0x0213** (section 8.3): request 8 bytes u8 what (0 read, 1 configure then read), u8 gate (0 off, 1 = 10 ms, 2 = 100 ms, 3 = 1 s), u8 source (0 the trigger source, 1 CH1, 2 CH2, 3 EXT), u8 level (0 = follow the UI trigger level), u8 hysteresis, 3 x 0; `E_BAD_ARG` detail 0 what, 1 gate, 2 source, 4 hysteresis > a non-zero level. Response 24 bytes: 0 flags (bit 0 NEW per GET_COUNTER, 1 VALID = E >= 1 and current, 2 EXT, 3 CH2), 1 seq, 2 gate and 3 source as configured, 4 u32 E, 8 u32 T, 12 u32 W, 16 min, 17 max, 18 level and 19 hysteresis in effect, 20 u32 freq_mHz = round(E x 2e11 / T) saturating, 0 when not VALID. **Firmware**: boot (built-for word only) writes 0x62 = 128 / 6 then 0x61 = gate 100 ms, source 0; one 0x35 read per main-loop pass after the capture readout (17 strobes, about 5 us) into the held result; the level follows the UI trigger level with hysteresis 6 (0x62 re-written when the marker moves, the gate restarted) unless pinned by the host; a host configuration is written by the next pass; the rate-change rule (a 0x0D / 0x53 write since the previous pass: 0x61 = 0 before the read, that read's result dropped, 0x61 = value after; 4 strobes on such passes); a result is current for three gates + 100 ms. **Cards** (`ui_meas.c`): Freq, Period, +Width, -Width, +Duty, -Duty of the result's channel show the counter's values while a VALID result is held (the record's formats); the other channel, EXT, no edges and the reference traces keep the record. Deviations from the contract's section 5: Vmin / Vmax / Vpp stay record-based (the brief's card list; min / max are in GET_COUNTER for the host); the frequency's resolution statement is the host's (the firmware converts only freq_mHz). Vectors: `req_get_counter`, `req_get_counter_follow_trigger`, `req_get_counter_ext_10ms`, `resp_get_counter_1_26_ch2`, `resp_get_counter_1_26_no_edges`, `resp_get_counter_e_bad_arg_hyst`. | docs/plans/fpga-0x153c.md items C5, C6 and C7; the units and the no-conversion rule from the brief; the peak and counter bytes from the coordinator (2026-10-21), byte-exact with mcp-c8's host vectors |
| 67 | Protocol 1.27 (provisional numbering): FPGA 0x153D streaming logger: LOG_START / LOG_READ / LOG_STOP, acq_state 5, caps bit 23, features bit 11 (fw-d4, item D4; version provisional); D5 pattern: FPGA 0x153D, the generator as a two-step pattern, the pattern generator and its trigger engine: SET_GEN P16 / H16, GEN 40 bytes, SET_PATTERN, SET_PATTERN_TRIGGER, capture trigger source 3, READ_BODE P (fw-d5, version provisional) | GET_INFO `proto_minor` = 27; `#VER` says `proto=1.27`. **Why** (contract `docs/plans/fpga-0x153c.md` section 6): long RX observation for the metal detector, at rates the MCU sustains, not limited by the 16,382-sample record. **FPGA** (contract 2.1 / 2.2, fpga-d1's RTL): 0x69 bit 0 stream (the capture never stops or triggers), bit 1 clear the lap flag; 0x6A u13 the next ring word the MCU reads; read 0x38 = 8 bytes from one snapshot (stream, lap, W, u32 words since 0x01 = 0, u16 unread count 0..8192); a word written while 8192 are unread sets the lap flag. The built-for word stays 0x153C until the 0x153D integration: on 0x153C LOG_START reads 0x38 bit 7 clear and answers E_FPGA detail 1. **Caps** bit 23 LOGGER, features word (GET_STATUS 44, GET_DECIM 32) bit 11 (0x0800; the contract's bit 8, shifted by three as fw-c5 numbered). **LOG_START 0x0214** (16 bytes): 0 u8 channels (bit 0 CH1, bit 1 CH2), 1 u8 format (0 legacy 8-bit: raw samples, two per word per channel; 1 filtered, the high byte of the 16-bit word; 2 filtered 16-bit; 3 peak min / max pairs), 2 u16 0, 4 u32 divider (the 0x0D value), 8 u32 duration_ms (0 = until LOG_STOP), 12 u32 0. The filter is the current SET_DECIM setting, effective at this divider: format 0 needs filter 0 (legacy), 1 / 2 a filter 1..3, 3 filter 4 (peak); divider 0 has no filter (200 MSa/s). Sample rate per channel: format 0 200 MSa/s / (D + 1), formats 1..3 200 MSa/s / N, N = 2 (floor(D / 2) + 1). **The rates** (contract 6.2, the documented specification): sample rate x bytes per sample (1 for formats 0 / 1, 2 for 2 / 3) x channels <= 500,000 B/s, i.e. 500 kS/s 1 ch 8-bit, 250 kS/s 1 ch 16-bit or peak or 2 ch 8-bit, 125 kS/s 2 ch 16-bit; higher is E_BAD_ARG detail 1 "rate > 500 kB/s: 500k 1ch 8b, 250k 16b or 2ch, 125k 2ch 16b". Other refusals: E_BAD_ARG detail 0 channels (0 or > 3), 2 format (> 3, or the filter does not fit), 4 divider (> 2^20 - 1); E_STATE detail 0 a host capture armed, 2 the UI long time base, 10 not in the Scope view (GET_STATUS 43 not 0 or 4); E_FPGA detail 0 0x05 not ready, 1 the FPGA does not stream. Duration: the log ends after exactly ceil(duration_ms x 200,000 / word clocks) FPGA words (word clocks 2 (D + 1) legacy, N otherwise). **What the scope does**: the UI's RUN stops (the screen shows STOP afterwards, as after ARM), 0x0D = divider, the filter / peak registers settled, the sequencer off, 0x69 = 1, 0x1A = 0, the sampling reset with 0x0B = 0x0C = 0x0E = 0, 0x38, 0x6A = 0, 0x69 = 3; then the main loop is the **log loop**: a drain step (a 0x38 snapshot, up to 2048 words in one pass per lane as READ_RAW, a second snapshot, 0x6A) and a MAIN poll per iteration, no UI pass (the FPGA ring holds 16.4 ms at the fastest tier, a UI pass takes 15-40 ms); the Scope view stays frozen with its notice slot reading "Logging to the host: 12 s, 6.0 MB, 1 gap", redrawn every 500 ms; after the end the UI's divider is written back and "Log ended: ..." shows for a while. Response: LOG. **LOG** (LOG_START / LOG_STOP response, 48 bytes): 0 u8 state (0 idle, 1 running, 2 ended by the duration, 3 stopped by LOG_STOP, 4 ended by an error: the FPGA left stream mode, or the host went away: a link reset or DTR drop, or no LOG_READ for 10 s), 1 u8 channels, 2 u8 format, 3 u8 flags (bit 0 a gap happened, bit 1 a gap happened because the DDR ring was full), 4 u32 divider, 8 u32 duration_ms, 12 u32 sample clocks (5 ns per sample: D + 1 for format 0, N otherwise), 16 u64 bytes produced into the DDR ring, 24 u64 bytes sent by LOG_READ, 32 u32 gaps, 36 u32 DDR ring bytes (8,388,608), 40 u32 ms since the start (frozen at the end), 44 u32 0. **LOG_READ 0x0215** (8 bytes: u32 max_bytes 1..65536, else E_BAD_ARG detail 0; u32 0); before any LOG_START E_STATE detail 0. Returns at once, possibly with 0 bytes while running: a 16-byte header, 0 u32 seq (+1 per non-empty chunk; an empty read repeats the last), 4 u32 the FPGA word counter of the chunk's first word (words since the start, modulo 2^32), 8 u32 bytes, 12 u8 flags (bit 0 running, bit 1 gap: words are missing before this chunk, the counter jumps (or a lap flag was seen with none lost), bit 2 ended: the log ended and nothing is left after this chunk, also on every later read, bit 3 the gap includes words the full DDR ring refused: the host read too slowly), 13 u8 format, 14 u16 0; then the bytes: whole words only (max_bytes rounded down), oldest first, never across a gap. **A word** in the wire layout: the enabled channels in order CH1, CH2, per channel format 0 the two samples in time order (core B then core A: FPGA lanes 0x21, 0x20 / 0x23, 0x22), format 1 the high byte (0x21 / 0x23), format 2 u16 little-endian (0x20 low, 0x21 high / 0x22, 0x23), format 3 min then max (0x20, 0x21 / 0x22, 0x23); bytes per word 2 / 1 / 2 / 2 per channel. Timestamps from the counter: t = word x 2 (D + 1) x 5 ns (format 0) or word x N x 5 ns, exact (the crystal's write clock). **LOG_STOP 0x0216** (no request bytes): a running log stops (state 3): 0x69 = 0, the FPGA stops writing at the next write clock edge (auto mode with 0x0E = 0) and the words written up to there are read into the DDR ring; then LOG. Idempotent (an idle or ended log: LOG as it stands, nothing written). The DDR ring stays readable after the end until the next LOG_START; LOG_READ needs the main loop, so after the end it is answered at the UI's pass rate. **Gaps (the overflow)**: the drain keeps a chunk only when the second snapshot shows the lap flag clear and the counter at most 8176 words ahead of the chunk's first word; else the words from there to the counter are lost, the drain resumes at the newest word (0x6A, 0x69 = 3), the next chunk carries the gap flag and the counter jumps: the log continues. A full DDR ring (the host reads too slowly) stops the drain; the FPGA ring then laps and the gap carries bit 3. **While logging** (acq_state 5, GET_STATUS 1) only these are answered: the F_BUSY set (PING, GET_INFO, GET_STATUS, SET_REMOTE, GET_BATTERY, GET_LINK_STATS, GET_BAD_FRAME, GET_EVENT_LOG, GET_HEALTH, GET_CAPTURE_INFO, SD_STAGE), the read-only set (READ_SCREEN, GET_CHANNEL, GET_TIMEBASE, GET_TRIGGER, GET_DECIM, GET_SEQ, GET_GEN, GET_CAL, READ_BODE, GET_COUNTER), LOG_READ and LOG_STOP; everything else (LOG_START included) answers E_STATE detail 9 "logging: LOG_STOP first". GET_STATUS `pass_ms` reads the log loop's iteration time. **Cost** (host_tests/test_log.c, the model at the measured 0.30-0.35 us per bus byte and 0.43-1.0 us per USB byte counted as CPU time, a 1 ms host turnaround per LOG_READ): every tier delivers its 500 kB/s for 2 s without a gap, CPU duty 52-60 % at the best case and 78-82 % at the worst; twice the 1 ch 8-bit tier with a 256 KiB ring falls behind and logs gaps (bit 3) with every delivered byte right. Vectors: `req_log_start`, `req_log_read`, `req_log_stop`, `req_log_start_2ch_16bit_10s`, `req_log_start_peak`, `resp_log_start_1_27`, `resp_log_stop_1_27`, `resp_log_read_1_27_gap`, `resp_log_read_1_27_ended`, `resp_log_start_e_bad_arg_rate`, `resp_e_state_logging`. **Unverified on hardware**: everything (needs the 0x153D bitstream); bench list in firmware/AGENTS.md section 74. **D5 pattern (fw-d5)**: GET_INFO `proto_minor` = 27; `#VER` says `proto=1.27`; GET_INFO 24 `gen_clock_hz` = 200000000. Built from docs/plans/fpga-0x153c.md 2.1, 2.2, 7.2-7.5 (item D5); the RTL is fpga-d2's (0x153D; the firmware on this branch is still built for 0x153C, `PC_FW_FPGA_VERSION` changes at the integration). Caps bit 1 is GENERATOR (was GENERATOR_DDS), 24 PATTERN_GEN, 27 PATTERN_TRIGGER; features bit 12 (0x1000) PATTERN, 13 (0x2000) PATTERN_TRIGGER. **The generator** (section 8.6): the DDS writes 0x51 / 0x52 are gone; the square is the pattern's entries 0 (high, n_h + f_h / 65536 clocks) and 1 (low, end) with the contract's P16 / H16 formulas, written through the double buffer (0x6C x 2 + 0x6D commit, 18 strobes; the first enable 0x6D = 0, the entries, 0x6D = 0x33 continuous + start + commit), so a frequency or duty change is glitch-free at a step boundary (the FPGA prefetches two steps: a commit during step k is taken at the k / k + 1 boundary, pending clears there, and the new entries apply from step k + 3; step 0 of a start begins about 35-40 ns after the 0x6D strobe, 20 ns after a pattern trigger event). The generator menu, the wizard (its gain step's static levels are mode 0 with the idle level; its 1 kHz is exact, n_h = n_l = 100,000), the boot state (the UI's generator, else the pattern stopped low and the pin hi-Z) and the on-device Bode engine all go through it. **SET_GEN 0x0301** keeps 20 bytes: freq_mode 1 = bytes 4..11 u64 P16 (the period in 1/65536 of a 5 ns clock), duty_mode 1 = bytes 12..19 u64 H16 (with duty_mode 0 bytes 12..15 are the duty in bp and 16..19 must be 0); E_BAD_ARG detail 1 the period (0 Hz, over 25 MHz, P16 outside 8 x 65536 .. 2^49 - 1), 2 the duty (n_h or n_l outside 2 .. 2^32 - 1, over 10000 bp, reserved bytes). **GEN** 40 bytes: 0 u8 enable (0x50), 1 u8 0x39 byte 0 ({ running, in gap, pending, 0, step[3:0] }), 2 u16 bursts done, 4 u32 0, 8 u32 0, 12 u64 freq_mhz, 20 u16 duty_bp, 22 u8 0x39 byte 3 ({ 0 x 6, marker seen, mode active }), 23 u8 source (0 the two-step square, 1 a SET_PATTERN pattern: then 12..39 are 0), 24 u64 P16, 32 u64 H16. GET_GEN reads 0x39 in the main loop (inside a menu the last status read): a host chirp (`scope_sweep`, the contract's 7.3 stepped SET_GEN) polls GET_GEN until byte 1 bit 5 clears before its next step, or paces steps at least one period apart; a second commit before the take overwrites the first. Worked examples: 1 kHz 50 % = P16 13107200000 (200,000 clocks), H16 6553600000: n_h = n_l = 100,000, no fraction; 123.456 kHz 25 % = P16 106168999 (1620.0225 clocks), H16 26542250: n_h 405 + 170 / 65536, n_l 1215 + 509 / 65536, back 123456.001 Hz / 2500 bp; 10 MHz 50 % = P16 1310720 (20 clocks), n_h = n_l = 10. **SET_PATTERN 0x0217** (cap PATTERN_GEN, no F_MODAL). Request 12 + 8 n bytes: 0 u8 mode (0 off: the pin at the idle level, 1 one burst per start, 2 R bursts with the gap between them, 3 continuous with the gap after every burst; mode 3 runs from the write), 1 u8 flags (bit 0 the pin stays hi-Z: 0x50 = 0, the post remains the EXT input, e.g. for the pattern trigger engine on EXT; bit 2 idle level; bit 3 the marker is a capture trigger source (needs SET_TRIGGER / SET_TRIGGER2 source 3 as well; either alone never triggers); bits 7..6 start source 0 MCU, 1 the pattern trigger engine, 2 both), 2 u16 R (0 = 65536), 4 u32 gap in ns (written as round(gap / 5) clocks to 0x6F), 8 u8 n (1..8), 9 u8 commit (entries 0 and 1 are double-buffered: without the commit they stay in the shadow), 10 u8 start (0x6D bit 4: the burst starts at the next clock with the fraction accumulator cleared), 11 u8 0, then n x { u8 flags (bit 0 level, bit 1 marker: a 1-clock marker at the step's first clock), u8 0, u16 fraction (1/65536 clock), u32 duration (5 ns clocks, >= 2) }; the firmware sets `end` on step n - 1. Writes: with start = 1 or mode 0 first 0x6D = the idle level (stop), then 0x50 (driven when mode != 0 and flags bit 0 = 0), 0x6C x n, 0x6E, 0x6F, 0x6D = mode \| flags bits 2, 3, 7..6 \| commit << 5 \| start << 4. The UI's generator switch follows the pin (its frequency / duty stay the square's: changing them on the screen replaces the pattern; switching it off stops it). Response PATTERN 24 + 8 n bytes: 0..11 the header as written (4 = the gap written, clocks x 5), 12 u8 0x39 byte 0, 13 u8 0x39 byte 3, 14 u16 bursts done, 16 u64 the burst length in 1/65536 clock (the steps' sum), 24.. the n steps as written. Errors (E_BAD_ARG, detail = the byte offset): 0 mode > 3; 1 flags bits 1, 4, 5, start source 3, mode 3 with start source 1; 8 n 0 or > 8; 9 commit > 1; 10 start > 1, start with mode 0 or with start source 1; 11 byte 11; 12 + 8 i step flags bits 2..7, 13 + 8 i byte 1, 16 + 8 i duration < 2; E_BAD_LEN a payload other than 12 + 8 n; E_STATE detail 0 "EXT trigger uses the pin" for a driven pattern while the capture triggers on EXT or the armed pattern trigger engine watches EXT. Afterwards GEN byte 23 = 1; SET_GEN with mask bit 0 alone switches 0x50 and keeps the pattern, a frequency or duty puts the square back. **SET_PATTERN_TRIGGER 0x021A** (cap PATTERN_TRIGGER). Request 28 bytes: 0..23 the SET_TRIGGER2 body (same fields, units and checks: type 0..3, polarity, flags bit 0 longer, source 0 CH1 / 1 CH2 / 2 EXT, N, H, level 0..255, level2 0; the hold-off at 16 must be 0: the engine has none, detail 16), 24 u8 armed (detail 24), 25 u8 re-arm after each burst (0 = one shot: the first event starts a burst and disarms; detail 25), 26 u16 0 (detail 26). Writes 0x7D = 0, 0x76 source, 0x77 polarity, 0x78 level, 0x79 the 16-bit level (from the level through the decimation the capture runs, as 0x55; re-written at every arm while the engine is armed), 0x7A type \| longer << 3, for types 1..3 0x7B N and 0x7C H, then 0x7D = armed \| re-arm << 1 \| 0x04 (clear the event flag) \| route << 3: one byte builder with two code tables ({0x15, 0x16, 0x17, 0x55, 0x5E, 0x5F, 0x60} for the capture, {0x76 .. 0x7C} for the pattern). The event starts a burst when SET_PATTERN's start source is 1 or 2. Response 32 bytes: 0..23 the body in effect (hold-off 0), 24 armed, 25 re-arm, 26 u16 0, 28 u8 FPGA 0x42 byte 0 ({ own engine, event seen since the last 0x7D, counting, pulse began, EXT, longer, type[1:0] }), 29 0, 30 u16 0x42 bytes 1..2 (edges still to count). The shared fallback (0x42 byte 0 bit 7 = 0, contract 7.5): an armed engine is routed to the pattern (0x7D bit 3); a qualified type that differs from the capture's qualified trigger (type, flags, N, H, source, polarity, level) answers E_STATE detail 8 "shared trigger engine". EXT as the engine's source while the pin is driven: E_STATE detail 0. **Capture trigger source 3** (SET_TRIGGER source, SET_TRIGGER2 source, GET_TRIGGER echoes it): 0x15 = 3, the pattern's marker through the EXT apply path (the EXT trigger position convention and shift); the edge type only (SET_TRIGGER E_BAD_ARG detail 0, SET_TRIGGER2 detail 3 while a qualified type is set); the UI's trigger channel stays EXT and the trigger button reads "PAT"; any trigger source chosen on the screen clears it; the generator may stay enabled (the pin is the pattern's). **READ_BODE**: the row's u32 at 0 is P, the stimulus period in 5 ns clocks (was M in 50 MHz DDS clocks): f = 200 MHz / P exactly. The planner (bode_dsp.c, mcp bode.py, docs/bode-plan-vectors.json version 2: rows period, n, divider, f_mhz, p, q, record, total): P0 = round(200e9 mHz / f), at least 4; legacy 200 MSa/s when P0 <= 133 (was M <= 33); else N = 16 doubled while P0 > 512 N, P = P0 at N = 16 (an even P above 4095, so a record holds two blocks) and round(P0 / N) x N above; p / q = P / N; f_mhz = round(200e9 / P); every DDS-era plan maps with P = 4 M and the grid is four times finer (0.5 % at 1 MHz, 5 % at 10 MHz); the record is exactly coherent (no Hann window); the DFT steps the phase by round(N 2^34 / P) per sample (under 2.5e-7 turn over a record); settle = periods x P / 200e6 s. The thru calibration matches points by (P, N). **Not here**: a device-paced chirp (no SET_SWEEP; 0x0218 stays free), fractional Bode periods (contract 7.4: a later study). Host tests: test_session `test_generator` / `test_pattern` / `test_pattern_trigger`, test_fpga_bus `test_gen_formulas` / `test_gen_square` / `test_gen_change` / `test_pattern_bursts` / `test_pattern_marker` / `test_ptrig` (fpga_model.c models the pattern engine), test_bode.py / test_bode_sweep on P; vectors section 5i (`req_set_gen_p16_h16`, `resp_gen_1_27`, `req_set_pattern_pulse_train`, `resp_set_pattern`, `resp_set_pattern_e_bad_arg_duration`, `req_set_pattern_trigger_nth_ch2`, `resp_set_pattern_trigger`, `resp_set_pattern_trigger_e_state_shared`, `req_set_trigger2_marker`) | docs/plans/fpga-0x153c.md item D4 and section 6; the byte layouts agreed with mcp-d6 (2026-10-21), the register semantics with fpga-d1; FPGA 0x153D; mcp-d6 implements the host side from this row |

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
