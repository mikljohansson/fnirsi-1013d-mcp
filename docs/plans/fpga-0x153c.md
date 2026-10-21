# FPGA 0x153C, 0x153D, 0x153E: advanced triggers, peak detect, counter, logger, pattern generator

> **Owner decisions 2026-10-21:** the pattern generator, its trigger engine and the marker source were cut from 0x153D at
> 15:55 (the design did not fit; the generator is the fractional square only). 0x153E is not built for 0.3.0 (16:1x): runt,
> window and the pulse-width range are dropped; both-core triggering stays a later candidate. The rest of this document is
> the design record as planned.

Design and contract document for the bitstream generations after 0x153B (fpga-spec, 2026-10-20). It lets several
implementation agents work in parallel without colliding: every register, capture format and host interface is fixed
here; the device UI scope is decided (section 11) and its screen design is a separate mockup round after this contract.

Marks: **[F]** fact with its source; **[I]** inference, not measured; **[E]** estimate with the assumptions stated.

Decisions this document builds on:
- One supported FPGA configuration per firmware version, a loud banner on a mismatch, no fallback paths and no capability
  negotiation for old bitstreams; the caps word only tells the host what this generation has (owner, 2026-10-20 21:52 and
  21:59, `docs/plans/task-queue.md` "Firmware"). ACQ Average becomes one behaviour, a true N-record mean on both paths.
- Features live MCP-first. The device UI gets exactly two rows (owner, 2026-10-20 22:13): a **Type** row in the trigger
  menu (Edge, Pulse width, Nth edge, with their parameters) and a **Sample** row in ACQ (Normal, Peak); the hardware
  counter feeds the measurement cards with no new control. Everything else is MCP-only. Consequence for this contract:
  the pulse-width and Nth-edge registers must be cheap to drive from a menu (few registers, parameters in the units the
  menu shows, no multi-step arming), peak detect is a one-register mode switch, and the counter is one read per screen
  pass at the bus's cost (section 11 checks each).
- Deeper plain memory is out (owner, 2026-10-20 22:00; 0x1536+ holds every block RAM, `fpga/al3/AGENTS.md` "Wanted next"
  item 2).
- The trigger types, peak detect and counter exist as a built and simulated design on branch `gen8-features-wip`
  (commit 9de82ca: `zaklad.v` +525 lines, `tb_trigtypes.v`, `tb_peak.v`, `tb_counter.v`, README section "10. Trigger
  types, peak detect, hardware counter", built on 0x1539 as "0x153A", 5690 LUT) [F]. It predates the 0x153A capture fix
  and the 0x153B stop semantics and must be rebased; this document adopts its register map and semantics where they
  stand and says where they change.

> Protocol numbering (coordinator, 2026-10-21 04:2x): fw 0.2.103 (fw-one) took protocol 1.25 (GET_INFO 80 B, GET_HEALTH
> layout 3, the mismatch flag), so this document's generations are 1.26 (0x153C), 1.27 (0x153D) and 1.28 (0x153E).

## 1. Scope and generations

### 1.1 Budget

| Item | Value | Source |
|---|---|---|
| LUT sites TD counts | 8640 (TD area report; the datasheet's 10,368 LUT / 8,640 FF figure is in README "Device facts") | `fpga/al3/README.md` section 10 table, "Device facts" [F] |
| 0x153B | 4990 LUT (57.8 %), all 48 EMB9K and both EMB32K, 2 of 3 M18x18, `clk_200MHz` 4.583 ns 0 errors | README section 11 "Build" [F] |
| 0x153A | 4975 LUT / 4403 reg / 5644 LE | `fpga/al3/AGENTS.md` "0x153A status" [F] |
| gen8 features on 0x1539 | 4575 -> 5690 LUT, 3833 -> 4748 reg: **+1115 LUT, +915 reg**; no block RAM, no M18x18 | branch README section 10 "Resources and timing" [F] |
| Block RAM free | **0**: the accumulator (16 EMB9K) and the record mirror (2 EMB32K) of 0x1536 took the last blocks | AGENTS.md "0x1536 status" [F] |
| M18x18 free | 1 of 3 | README section 10 [F] |
| PLL | one PLL in use (`main_clock`); the roadmap counts one more free | `zaklad.v` 674-684, `docs/plans/roadmap.md` section 8 [F] |

The roadmap's "7,000 LUTs, 200 kbit block RAM free after 0x1535" (section 8) is stale: 0x1536 spent the block RAM and
0x1536..0x153B spent about 1,900 LUT. **The real budget is about 3,650 LUT and no block RAM.** Every design below is
register and LUT only; the sample memory ring stands in for any FIFO.

**Measured on 2026-10-20 23:05-23:25** (fpga-spec, TD 4.6.8 SP1 headless, `build.sh` on scratch copies of the 0x153B source; the
unchanged copy reproduces 4990 LUT / 4405 reg; full table in section 13): removing the DDS saves **61 LUT / 97 reg**; the
cruft of section 13 saves **78 LUT / 19 reg** (138 / 116 together with the DDS); a two-step pattern core with 16-bit
fractional durations and the double buffer costs **48 LUT / 231 reg** over the DDS-less design (13 LUT / 151 reg without the
fraction). TD prunes unloaded flops, so dead registers (0x1B) cost LUTs in the decoder only.

Timing: `clk_200MHz` has been a placement lottery since 0x1532 (the 32-bit divider compare, `AGENTS.md` "0x153A status"
item 4; 0x153B's first build rolled 2 errors, README section 11). Above about 80 % LUT use the lottery gets worse [I].
The cut order in 1.3 is for that.

### 1.2 The generations

Ordered so that each is buildable, simulatable and benchable with its own firmware, most wanted first.

| Generation | Features | LUT / reg estimate | Running LUT total | Firmware / protocol |
|---|---|---|---|---|
| **0x153C** | cruft removal (section 13, first); trigger types pulse width (shorter / longer), Nth edge, timeout, on CH1 / CH2 / EXT (gen8 design rebased); peak-detect acquisition; hardware counter with min / max; trigger hold-off (0x1B gets its meaning). The DDS stays until the pattern generator replaces it | cruft **-78 / -19** (measured); +1115 / +915 (gen8, measured on 0x1539); hold-off about 60 / 40 and min / max about 50 / 20 [E] | **about 6,140 LUT (71 %), 5,360 reg (62 %)** | protocol 1.26: SET_TRIGGER2, GET_COUNTER, peak format in GET_CAPTURE_INFO / READ_RAW; caps 2, 20, 21, 22 |
| **0x153D** | the DDS removed; the pattern generator (8 steps, u32 + u16 fractional durations, double-buffered entries 0 / 1) **is the generator** (7.2); its own trigger engine (7.5); streaming logger (free-running ring with a readable write pointer, lap detection, word counter); chirps and sweeps are MCU-stepped (7.3), no sweep logic | DDS **-61 / -97** (measured); pattern core **+48 / +231** (measured, two steps u24 + u16 with the double buffer) plus the 8-step table, u32 integers, bursts, gap and marker about +200 / +420 [E]; pattern trigger engine about 350 / 200 [E]; logger about 150 / 90 [E] | **about 6,830 LUT (79 %), 6,200 reg (72 %)** | protocol 1.27: LOG_START / LOG_READ / LOG_STOP, SET_PATTERN, SET_PATTERN_TRIGGER, SET_GEN re-based on the pattern; caps 23, 24, 27 (25 DDS_SWEEP not allocated) |
| **0x153E** | trigger on both ADC cores (legacy D = 0, 5 ns trigger resolution); runt and window types (second level); pulse width inside / outside a range | both cores about 200 / 60; runt / window about 250 / 80; range about 60 / 30 [E] | **about 7,340 LUT (85 %), 6,370 reg (74 %)** | protocol 1.28: SET_TRIGGER2 fields already reserved; caps 4, 26 |

Reconsidered on the numbers: before the owner's 23:00 decisions 0x153E stood at 91 % on paper; without the DDS (-61),
without the DDS sweep (-200 estimated), with the cruft gone (-78) and with the pattern core measured smaller than the
450 LUT estimate, it stands at **about 85 % with about 1,300 LUT of margin**. That is enough for the sites; the timing
lottery (1.1) remains the risk, which is why 0x153E still starts only after 0x153D's measured count.

Estimates [E] are scaled from measured neighbours in this design: the gen8 `trig_qual` instance pair cost about 350 LUT
for two qualifiers (the rest of the +1115 is the counter and peak detect); the sequencer's 33-bit hold-off counter and
state cost about 120 LUT (0x1536 +1363 for the whole sequencer); a 24-bit down counter with sign is about 30 LUT.

**Cut order if the budget or timing does not hold** (decide per build, never silently). 0x153D: the pattern table from
8 to 4 steps first (about 200 reg), then the pattern trigger engine's own qualifier instance (the fallback of 7.5: one
engine routed to capture, pattern or both; registers unchanged); the two-step generator core and the double buffer are
never cut (the firmware's generator depends on them). 0x153E: the window trigger first, then the pulse-width range (two
single-sided captures give the same answer), then both-core triggering; 0x153E starts once 0x153D's measured LUT count
is known. Nothing in 0x153C is cut: it is built and simulated already. Deeper memory is out (owner).

### 1.3 Why this order

- 0x153C is the owner's first three epics and is a rebase of working RTL: the shortest path to a benched gain.
- 0x153D needs no new capture logic (the logger reuses the ring and the readout path; the pattern generator lives
  beside the DDS) and both features are metal-detector value: a logger for long RX observation, a TX pattern driver.
- 0x153E touches the timed trigger path (the half-cycle compare, `zaklad.v` 1241-1282) and is the one with timing risk;
  it goes last and each of its three items is gated separately.

## 2. Register map

Conventions [F]: one command byte (PE10 high), then data bytes; multi-byte values high byte first; a multi-byte command
steps `mcu_state` (4 bits, so at most 16 bytes per command, `zaklad.v` 549, 1825-1837); reads are loaded on the falling
edge of `i_mcu_clk` (0x1539); the command byte is compared as a full 8-bit value (`mcu_command <= io_mcu_data`, line
1817), so the top bit carries no bus meaning, but every new code stays below 0x80 anyway. The stock firmware's use of
0x24 / 0x26 / 0x28 (AGENTS.md "MCU-FPGA protocol") is irrelevant since only our firmware runs on our bitstreams, but
those codes are not reused either.

Codes in use after 0x153B [F, `zaklad.v` decoders 1865-2180 and 2192-2363]: writes 0x01, 0x0B-0x0F, 0x15-0x17, 0x1A,
0x1B, 0x32-0x38, 0x3C, 0x50-0x5D, 0x63, 0x64; reads 0x05, 0x06, 0x0A, 0x14, 0x18, 0x20-0x33, 0x36, 0x41.

Allocation per feature, with spares (writes 0x65-0x7F and reads 0x37-0x40, 0x42-0x4F are free today):

| Feature | Writes | Reads | Spare |
|---|---|---|---|
| Trigger types, hold-off | 0x5E, 0x5F, 0x60, 0x65, 0x66, 0x1B (meaning changes) | 0x34, 0x37 | write 0x67, read 0x3E |
| Peak detect | 0x56 bit 1 | 0x29 byte 1 bits 7, 6 | - |
| Counter | 0x61, 0x62 | 0x35 | write 0x68, read 0x3F |
| Logger | 0x69, 0x6A | 0x38 | write 0x6B, read 0x40 |
| Pattern generator (= the generator since 0x153D) | 0x50 (kept: pin driver on / hi-Z), 0x6C, 0x6D, 0x6E, 0x6F | 0x39 | write 0x70 |
| Pattern trigger engine (7.5) | 0x76-0x7D (the capture trigger's layouts in a contiguous block) | 0x42 | write 0x7E |
| Retired in 0x153D | 0x51, 0x52 (DDS increment and duty: writes ignored, no logic) | - | not reused |
| Reserved for 0x153F+ | 0x71-0x75, 0x7F | 0x3A, 0x43-0x4F | |

### 2.1 Writes

Power-on: TD configures every flop to 0 (`AGENTS.md` "0x153A review" item 6 [F]); "power-on" below means the value 0
unless XOR-coded like 0x63. All new registers are plain (0 = off / edge type), so a firmware that writes none of them
sees 0x153B behaviour; the equivalence steps of `tb/run.sh` keep proving that.

| Cmd | Bytes | Gen | Layout (bit 7 first within a byte) | Meaning |
|---|---|---|---|---|
| 0x16 | 1 | C | bit 0 polarity | per type: edge = the edge; pulse width 0 = positive pulse, 1 = negative (trigger on its trailing edge); Nth edge = the counted edge; timeout 0 = held high after a rising edge, 1 = held low; runt 0 = positive runt (crosses L1 up, not L2), 1 = negative; window 0 = fires on leaving, 1 = on entering [gen8 branch README section 10, extended] |
| 0x1B | 4 | C | u32 hold-off, 5 ns units; 0 = off (power-on) | **meaning changes**: Atlan4 stored and never used it (`protocol.md` caps bit 2). From the trigger of a record, no trigger is accepted until the hold-off has elapsed, across sequencer restarts and MCU re-arms; cleared only by 0x01 = 1 while written 0, see 3.6 |
| 0x56 | 1 | C | bit 0 FIR enable (0x1535); **bit 1 peak detect**; bits 7..2 write 0 | peak detect: each 16-bit word = { max, min } of the window's raw samples, FIR off, shift unused. **One-register switch** (change against gen8, where it needed K >= 1): bit 1 forces the 16-bit path (`dec_on` = K != 0 or peak), the window N = 2 (floor(D / 2) + 1) from 0x0D as for the filter, whatever 0x53 holds; 0x29 byte 0 bit 7 then reads 1 |
| 0x5E | 1 | C / E | bits 2..0 type: 0 edge, 1 pulse width, 2 Nth edge, 3 timeout, 4 runt (E), 5 window (E), 6-7 reserved (act as edge); bit 3 pulse width "longer than N" (gen8's bit 2 moves to bit 3); bit 4 pulse width range mode (E): inside [N, N2] with bit 3 = 0, outside with bit 3 = 1; bit 5 both cores (E, legacy D = 0 only); bits 7..6 write 0 | trigger type |
| 0x5F | 3 | C | u24 N | pulse width and timeout: samples of the trigger stream, 1 .. 2^24 - 1; Nth edge: bits 15..0 (0 acts as 1) [gen8] |
| 0x60 | 3 | C / E | u24 H or N2 | Nth edge: idle time H in samples, 0 = count from the arm (gen8); pulse width range (E): N2 >= N |
| 0x61 | 1 | C | bits 1..0 gate: 0 off, 1 = 10 ms, 2 = 100 ms, 3 = 1 s; bits 3..2 source: 0 the trigger source (0x15), 1 CH1, 2 CH2, 3 EXT | counter control [gen8] |
| 0x62 | 2 | C | byte 0 level (raw ADC code, Schmitt high at >= level), byte 1 hysteresis (codes below the level for low) | counter thresholds [gen8]; power-on 0 / 0, the firmware writes 128 / 6 at boot |
| 0x65 | 2 | E | u16 level 2 (L2), 16-bit units when K >= 1 (as 0x55), low byte only in legacy | runt: the level the pulse must not reach; window: the second boundary (L1 = 0x17 / 0x55, L2 may be above or below L1) |
| 0x66 | 1 | C | bit 0 = 1: the next 0x01 = 1 also clears the hold-off timer | lets the firmware restart a session cleanly (section 3.6) |
| 0x69 | 1 | D | bit 0 stream on; bit 1 clear the lap flag (self-clearing) | logger: with bit 0 the capture never stops and never triggers (section 6) |
| 0x6A | 2 | D | u13 reader position (ring word) | logger: the MCU's consumed boundary; the FPGA flags a lap when the write address reaches it |
| 0x50 | 1 | - | bit 0: 1 = the pin is driven (the pattern's level), 0 = hi-Z (EXT input) | unchanged since Atlan4; the pattern replaces the DDS behind it |
| 0x51, 0x52 | - | D | retired: the decoder entries go (writes are ignored) | the DDS phase increment and duty of 0x1532..0x153C |
| 0x6C | 7 | D | byte 0 = { end, mark, level, index[4:0] }, bytes 1..4 u32 integer duration in 5 ns clocks (>= 2), bytes 5..6 u16 fraction in 1/65536 clock | pattern step `index` (0..7 in 0x153D; the field allows 32). Average duration n + f / 65536 clocks: 10 ns .. 21.47 s. Entries 0 and 1 are double-buffered (taken at the next step boundary after a commit), 2..7 take effect at once |
| 0x6D | 1 | D | bits 1..0 mode: 0 off (pin holds the idle level, bit 2), 1 single burst, 2 repeat R bursts, 3 continuous; bit 2 idle level (between bursts and in mode 0); bit 3 marker is a trigger source (0x15 = 3); bit 4 start now (self-clearing: the burst starts at the next clock, the fraction accumulator cleared); **bit 5 commit** (self-clearing: the shadow entries 0 and 1 become pending, see 7.2); **bits 7..6 start source**: 0 the MCU only (bit 4), 1 the pattern trigger engine (7.5), 2 both (either starts a burst), 3 reserved | pattern control |
| 0x6E | 2 | D | u16 R, 0 = 65536 | bursts per start in mode 2 |
| 0x6F | 4 | D | u32 gap between bursts, 5 ns clocks | 0 .. 21.47 s |
| 0x76 | 1 | D | as 0x15: bits 1..0 source 0 CH1, 1 CH2, 2 EXT (3 reserved, acts as EXT) | pattern trigger source |
| 0x77 | 1 | D | as 0x16 | pattern trigger polarity, per type |
| 0x78 | 1 | D | as 0x17 | pattern trigger level, 8-bit (legacy) |
| 0x79 | 2 | D | as 0x55 | pattern trigger level, 16-bit (K >= 1 or peak) |
| 0x7A | 1 | D | as 0x5E (types 0..3; 4, 5 and bits 4, 5 reserved: the pattern engine never gets runt / window / both cores) | pattern trigger type |
| 0x7B | 3 | D | as 0x5F | pattern trigger N |
| 0x7C | 3 | D | as 0x60 | pattern trigger H |
| 0x7D | 1 | D | bit 0 armed (1 = the engine watches; writing 1 restarts its state, as 0x01 = 1 / 0 does for the capture's qualifier); bit 1 re-arm after each burst (0 = one shot: the first event starts a burst, then disarmed); bit 2 clear the event flag (self-clearing) | pattern trigger engine control |

### 2.2 Reads

| Cmd | Bytes | Gen | Content |
|---|---|---|---|
| 0x29 | 2 | C | byte 1 bit 7 peak active (0x56 bit 1 and K >= 1), bit 6 0x56 bit 1 as written, bits 5..0 shift (gen8) |
| 0x34 | 3 | C | byte 0 = { 0, 0, counting, pulse began, source EXT, longer, type[1:0] } (gen8; type bit 2 is in 0x37), bytes 1..2 edges still to count (Nth edge). Asynchronous: read twice |
| 0x35 | 16 | C | snapshot at the command byte: byte 0 = { new, 0, EXT, CH2, 0, 0, gate[1:0] }, 1 sequence, 2..5 E, 6..9 T, 10..13 W (u32 each, 28 bits used), **14 min, 15 max** of the source's raw codes over the gate (255 / 0 when no sample) |
| 0x37 | 1 | C / E | { 0, 0, 0, hold-off running, type bit 2, both-cores on, trigger core (E: 0 = B earlier, 1 = A later; the word of 0x14 plus half a sample), 0 } |
| 0x38 | 8 | D | snapshot at the command byte: byte 0 = { streaming, lap, 0, 0, 0, W[12:8] }, 1 W[7:0] (the write address: the next word to be written), 2..5 u32 words written since 0x01 = 0, 6..7 u16 words written since the reader position (W - reader, modulo 8192; 8192 or more = lap) |
| 0x39 | 4 | D | pattern status: byte 0 = { running, in gap, **pending** (a commit not yet taken), 0, step[3:0] }, 1..2 u16 bursts done, 3 = { 0, 0, 0, 0, 0, 0, marker seen since 0x01 = 1, mode active } |
| 0x42 | 3 | D | pattern trigger status, laid out as 0x34: byte 0 = { own engine (1 = a second qualifier instance, 0 = the shared fallback of 7.5), event seen since 0x7D bit 2, counting, pulse began, source EXT, longer, type[1:0] }, bytes 1..2 edges still to count. Asynchronous |

### 2.3 Capability bits and version words

The firmware builds `caps` (GET_INFO bytes 20..23, `docs/protocol.md` section 7 "caps bits") from the version word it is
built for. Since a firmware supports one bitstream, the bits are constants per release; the host still reads them.

| Bit | Name | Set from | Means |
|---|---|---|---|
| 2 | HOLDOFF | 0x153C | 0x1B is a trigger hold-off in 5 ns units (SET_TRIGGER `holdoff` accepted) |
| 4 | TRIG_BOTH_CORES | 0x153E | 0x5E bit 5 exists; 0x37 bit 1 names the core |
| 20 | TRIG_TYPES | 0x153C | 0x5E types 1..3, 0x5F, 0x60, read 0x34; SET_TRIGGER2 |
| 21 | PEAK_DETECT | 0x153C | 0x56 bit 1; READ_RAW layout 3 |
| 22 | COUNTER | 0x153C | 0x61, 0x62, read 0x35; GET_COUNTER |
| 1 | GENERATOR (was GENERATOR_DDS) | every bitstream | a programmable square generator exists; from 0x153D it is the two-step pattern (7.2): SET_GEN's raw modes change meaning (7.4) |
| 23 | LOGGER | 0x153D | 0x69, 0x6A, read 0x38; LOG_* |
| 24 | PATTERN_GEN | 0x153D | 0x6C-0x6F, read 0x39, trigger source 3; SET_PATTERN; also means "the generator is the pattern" |
| 25 | (not allocated) | - | was DDS_SWEEP; sweeps are MCU-stepped (7.3) |
| 26 | TRIG_RUNT_WINDOW | 0x153E | 0x5E types 4, 5, bit 4 range; 0x65 |
| 27 | PATTERN_TRIGGER | 0x153D | 0x76-0x7D, read 0x42, 0x6D bits 7..6; SET_PATTERN_TRIGGER. Own engine or the shared fallback: read 0x42 byte 0 bit 7 |

Version words: 0x06 reads 0x15 0x3C / 0x3D / 0x3E. The GET_STATUS `features` word (protocol row 56, bits 5-15 free)
gets bits 5 TRIG_TYPES, 6 PEAK, 7 COUNTER, 8 LOGGER, 9 PATTERN, 10 (unused), 11 RUNT_WINDOW, 12 PATTERN_TRIGGER for
hosts that read features instead of caps.

### 2.4 Existing registers whose meaning changes

- **0x1B**: from "stored, never used" to the trigger hold-off (caps bit 2 becomes 1). No firmware path writes it today
  except SET_TRIGGER's `holdoff`, which answers E_UNSUPPORTED while the bit is 0 (`protocol.md` SET_TRIGGER) [F].
- **0x16**: polarity per type (gen8). For the edge type unchanged.
- **0x15**: value 3 becomes PATTERN (the pattern marker as trigger source, 0x153D); today "reserved, acts as EXT"
  (`FPGA commands.txt` 0x15) [F]. Value 3 with the pattern off never triggers (an auto capture times out).
- **0x55**: with peak detect the compare takes the window max (rising) or min (falling) as the high byte, so the firmware
  writes level << 8 (gen8).
- **0x0A / 0x14 / 0x18 while streaming (0x69 bit 0)**: 0x0A reads 0 for ever, 0x18 reads 0, 0x14 is not meaningful; the
  host reads 0x38.

## 3. Advanced triggers

### 3.1 Streams, units and resolution [F from the gen8 design unless marked]

- **CH1 / CH2 stream**: the sample the edge trigger compares, one per memory word: legacy (K = 0) core A of the channel
  (`trigger_data <= trigger_channel ? adc2A_w : adc1A_w`, `zaklad.v` 1280), one per `10 (D + 1)` ns; K >= 1 the 16-bit
  output sample (or the peak word), one per `N x 5` ns. N and H count these samples. **Resolution of every width or
  count is therefore one word: 10 ns at 200 MSa/s legacy, N x 5 ns filtered.** Core B is not looked at (0x153C); 0x153E
  adds it (3.8).
- **EXT stream**: `ext_level` (synchronised, 3-sample majority filtered, `zaklad.v` 762-767), one per 5 ns at every sample
  rate; N and H count 5 ns clocks (N up to 83.9 ms).
- **Level**: the edge trigger's (0x17, or 0x55 with K >= 1), no hysteresis. Runt and window add L2 (0x65).
- **Position**: for CH1 / CH2 the trigger position is exactly the edge trigger's for the qualifying edge (timeout: the
  qualifying sample): `trigger_address` = the word of the trigger sample - 1 as for every edge trigger, so 0x0B / 0x0C /
  0x14 and the firmware's record arithmetic are unchanged; EXT qualified types decide one 5 ns clock later than an EXT
  edge trigger on the same edge (`ext_qev_r`), within the firmware's existing EXT correction.
- **Auto mode, time-out, sequencer**: unchanged for every type; the qualifier restarts with each record (`cap_rst`).
  0x153B's rules (no trigger after the stop, time-out only while untriggered, `zaklad.v` 1219-1234) apply to the
  qualified trigger too because it fires through the same `sampling_triggered` path.

### 3.2 How the qualifier fits the timed path (gen8, kept)

The edge compare (`trigger_previous` against `trigger_data`, the half-cycle path into `sampling_triggered`) is untouched.
For a type other than edge, `trigger_previous` is loaded not with the previous sample but with a value on the pre-detect
side of the level when the qualifier (module `trig_qual`, one instance on the write clock for the ADC stream, one on
`clk_200MHz` for EXT) says the next edge is the trigger, else on the other side. The qualifier looks only at samples
before the next one, so the compare fires exactly on the qualified edge. `trigger_edge` holds the detect direction (0x16
for edge / Nth / timeout, its complement for pulse width), recomputed on writes of 0x15, 0x16 and 0x5E. This adds nothing
to the timed path and keeps the edge type bit-identical to 0x153B.

**Rebase notes for 0x153C** (the gen8 RTL meets 0x153A / 0x153B): the qualifier's `a` input takes `trigger_data`, which
now comes from `adc*_w` (0x153A) instead of the pins: no change to the qualifier. The 0x153B gate `trigger_check_enable &
sample_write_enable` must also gate the qualifier's `fire` into `trigger_previous`, else a qualified type could latch a
trigger on the stop edge's evaluation; `tb_timeout.v` gets one case per type. `trigger_mode_s` (0x153B) is not involved.

### 3.3 Types, parameters and state machines

| Type (0x5E) | Parameters | Fires on | State machine (a few lines) | Gen |
|---|---|---|---|---|
| 1 pulse width | 0x16 polarity; 0x5F N; bit 3 longer / shorter; (E) bit 4 range with 0x60 N2 | the trailing edge of a pulse of width w samples with w < N (shorter), w > N (longer), N <= w <= N2 (inside), or w < N or w > N2 (outside). The pulse must have begun after the arm (a sample off the pulse side was seen) | a 25-bit down counter `d` loaded with N - 1 or N - 2 at every off-side sample, decremented on pulse-side samples, sign bit = limit reached; `vld` = a post-detect sample seen. Range (E): a second counter for N2; fire = trailing edge & (inside: d1 sign & ~d2 sign; outside: ~d1 sign \| d2 sign) | C (range E) |
| 2 Nth edge | 0x16 counted edge; 0x5F N (16 bits); 0x60 H | the Nth edge in the trigger direction after the arm (H = 0) or of each burst: the count restarts when the level held for H samples without any edge | 16-bit down counter `rem` from N - 1, registered `rz` (next is the Nth); 25-bit idle counter `h` reloaded on any edge; `gap` when its sign sets | C |
| 3 timeout | 0x16 level that is held; 0x5F N | the sample that makes the run N + 1 samples long after the edge (or the arm), no edge needed | `d` counts the run of post-detect samples from N - 2; fire = sign set & not overflowed | C |
| 4 runt | 0x16 polarity; 0x17 / 0x55 L1; 0x65 L2 | a pulse that crosses L1 (the low level for a positive runt) and returns through L1 without having reached L2 | a flag `hi2` set while the sample is beyond L2, cleared at each L1 crossing in the leading direction; fire on the trailing L1 crossing with `hi2` = 0. The L2 compare needs a second comparator on the stream (`trigger_data` is 8 bits, or the 16-bit `dec_trig_sample`): 16 LUT legacy, about 40 with K >= 1 | E |
| 5 window | 0x17 / 0x55 L1; 0x65 L2; 0x16 enter / leave | the sample that leaves the band [min(L1, L2), max(L1, L2)] (0x16 = 0) or enters it (1) | `in` = between the levels; fire on `in` changing in the chosen direction, driven through the compare as "pre side = ~in" | E |

Pulse width and timeout with N = 1 degenerate to the edge trigger with one sample of context; the firmware rejects N = 0
for them (E_BAD_ARG). Nth edge N up to 65535; H up to 2^24 - 1 samples.

### 3.4 Both-core triggering (0x153E, 0x5E bit 5)

Legacy D = 0 only (at divided rates and with K >= 1 there is one sample per word). The word holds B (earlier by T/2) then
A ([F] `zaklad.v` 846-857). The compare becomes two steps per write clock: (prev A, B) and (B, A), so a crossing is found
at 5 ns resolution; the qualifier for pulse width / timeout counts two per word and sees both crossings. The trigger word
is the same as today; `0x37` bit 1 says whether the crossing was into B (0) or into A (1), and the firmware offsets the
trace by half a sample (the cursor and measurement code already works in sample units; half-sample offsets exist in the
EXT correction path, `pc_decim_ext_shift`). Cost [E]: a second 8-bit compare and a 2:1 step in the qualifier, about 200
LUT; the risk is the half-cycle compare path, so the second compare must be registered a word early (compare B against
the level on the rising edge, keep the result; the timed path still sees one compare).

### 3.5 Driving the types from a menu (the owner's cheapness rule)

Every type is a SET_TRIGGER2 field and a `scope_set_trigger` argument; pulse width and Nth edge are also the device's
Type row (section 11). What that row has to write per arm, on top of the edge trigger's 0x15 / 0x16 / 0x17 writes:
- **Pulse width**: 0x5E (1 byte: type 1, longer / shorter) and 0x5F (3 bytes: N). The menu shows a time; N = round(t /
  word period), one integer multiply with the period the firmware already knows per rate (`pc_decim.c` geometry). 5 bus
  strobes.
- **Nth edge**: 0x5E, 0x5F (N as the menu shows it: the count), 0x60 (idle time in words from the menu's time, or 0).
  9 strobes.
- **No multi-step arming**: the gen8 design latches these registers like 0x15-0x17 (any time before 0x01 = 0, no 0x01 = 1
  window, no settle wait) [F gen8 README "Writes (all may be written in any order ...)"]; the rebase keeps that. A type
  change is one write of 0x5E; back to Edge is 0x5E = 0.
- Runt, window and timeout stay MCP-only (owner); their registers are the same shape, so the firmware's SET_TRIGGER2 path
  is one function for all types and the menu uses a subset of it.

### 3.6 Hold-off (0x1B, 0x153C): added now

Today the only hold-off is the sequencer's restart delay 0x5C (end of record to restart, `FPGA commands.txt` 0x5C) [F]; a
trigger hold-off (from the trigger of a record to the next accepted trigger) does not exist on any bitstream (caps bit 2
is 0 everywhere). Design: a 33-bit down counter on `clk_200MHz` loaded with 0x1B at each `sampling_triggered` rise, not
reset by `cap_rst` / sequencer restarts, cleared by 0x01 = 1 only when 0x66 bit 0 was written (so an MCU re-arm in a
train keeps the hold-off running); while it runs, `trigger_check_enable` is masked (an AND at its source, off the timed
path). Why now: it is 60 LUT, it gives SET_TRIGGER's `holdoff` field a meaning, and with segments it lets the sequencer
skip pulses of a train without the Nth-edge qualifier. It touches the capture-logic block, so the 0x153C trigger agent
builds it.

## 4. Peak-detect acquisition (0x153C)

Design as built on gen8 (`decim_chan` ports `peak`, `wstart`, `pair_e`, `pair_l`; registers `pmx` .. `pk4`) [F]:

- **Enable**: 0x56 bit 1, one register (the owner's rule: a one-register mode switch). Change against gen8: bit 1 forces
  the 16-bit path by itself (`dec_on` = `dec_order != 0 | peak_en`, `decim_chan` runs its P counter as for K = 1), so the
  firmware's Sample row writes 0x56 and nothing else; the filter's 0x53 / 0x54 keep their values for the way back. The
  ADCs run at 200 MSa/s; the window is exactly the N = 2P raw samples (both cores) the K = 1 boxcar sums for the same
  output word, N from 0x0D as for the filter, with the same latency (12 write clocks pin to memory). The FIR is off while
  peak detect is on (`fir_ok` carries `~peak_en`), the shift 0x54 is not applied. The memory geometry is the 16-bit one
  (one word per sample per channel), which the firmware's rate / depth code already handles for K >= 1.
- **Record format**: each 16-bit word per channel = { max (high byte, read 0x21 / 0x23), min (low byte, 0x20 / 0x22) }
  of the window's raw 8-bit codes. Memory and readout path identical to any 16-bit capture: 8192 words, both channels,
  the 0x1F / 0x20..0x23 reads, segments allowed. **Averaging of peak words is refused** (the sums would mix max and min):
  SET_SEQ mode 2 with peak on is E_STATE detail 7 "peak detect"; a UI averaging run is not armed while peak is on.
- **Trigger**: the 16-bit compare uses the window max for a rising detection and the min for a falling one in the high
  byte; the firmware writes 0x55 = level << 8 (the same 0x55 write every 16-bit arm makes, with a shifted value, so peak
  adds no write to the arm). Every type of section 3 works on that stream.
- **Calibration**: the two bytes are two raw 8-bit codes from mixed cores; the firmware applies the 8-bit per-core
  correction's mean of both cores (gain_a + gain_b) / 2 and off (GET_CAL bytes 92..107) to both, marks the record "peak"
  (no gain16 / corr16).
- **Host interface**: READ_RAW layout 3 = peak pairs (count words, per channel `count` u8 min then `count` u8 max, so a
  1.24 host asking layout 2 gets E_BAD_ARG detail 2 while peak is on); GET_CAPTURE_INFO byte 79 bit 1 = peak record (bit 0
  stays "16-bit records"). SET_DECIM gains mode byte value 4 "peak" (the existing modes are off / boxcar / CIC2 / CIC3 with
  the FIR flag, `protocol.md` row 47); the firmware writes 0x53 K = 1 and 0x56 bit 1.
- **MCP**: `scope_capture(..., acquisition="peak")` returns per channel `min`, `max` arrays (volts through the 8-bit
  model) and `samples` = the midpoint for tools that need one trace; the saved `.npz` carries both; `scope_set_decimation`
  gains `mode="peak"`. The preview draws a filled band between min and max; a renderer with one pixel per word draws a
  vertical bar per column from min to max.
- **Alias check** (gen8 `tb_peak.v`): at 5 MSa/s with 6 ns spikes, sample skipping caught 31 of about 405, peak detect 202
  of 204 (two fell outside the record) [F].

## 5. Hardware measurements (0x153C)

Design as built on gen8 (`k_*` registers, snapshot `m_*` at the 0x35 command byte) [F], plus min / max:

- **Counter**: reciprocal counting of a 1-bit stream at 200 MHz. EXT: `ext_level`. CH1 / CH2: the raw pair of each write
  clock through a Schmitt trigger (high at code >= level, low below level - hysteresis, 0x62), the earlier core's state
  after the write clock's rise, the later one's after its fall: 5 ns resolution at 200 MSa/s (divider 0 or any K >= 1),
  each sample held half a word at slower legacy rates. Gate exactly 2e6 / 2e7 / 2e8 clocks (10 ms / 100 ms / 1 s at the
  50 MHz crystal's accuracy [F `SYNTHESIS.md` section 2 "Clock"]). From the first rising edge in the gate to the last:
  E periods, T clocks, W high clocks; published with a sequence number at the end of each gate; E = 0 = fewer than two
  edges.
- **Derived**: frequency = E / (T x 5 ns), period = T x 5 ns / E, positive width = W x 5 ns / E, duty = W / T.
  Resolution one clock over the gate: 5e-7 relative at 10 ms, 5e-9 at 1 s (the crystal dominates); range about 1 Hz
  (two edges in a 1 s gate) to the front end's 30 MHz (the logic takes 100 MHz). The gen8 testbench measured EXT
  810.044552 kHz to 5e-8 and CH1 1.2858 MHz to 1.6e-7 at 200 MSa/s [F].
- **Min / max** (new, same block): the source's raw code minimum and maximum over the gate (both cores at full rate),
  bytes 14, 15 of 0x35. The firmware converts them with the 8-bit per-core model (mean gain) to Vmin / Vmax; Vpp follows.
  Cost about 50 LUT [E].
- **Independence**: the counter never touches the capture (it reads the decimator's captured pair and `ext_level`); 0x35
  does not move the read pointer, so a READ_RAW interrupted by a 0x35 continues [F gen8].
- **Firmware per screen pass** (the owner's rule: one read at the bus's cost): one 0x35 read = the command byte plus 16
  data strobes, 17 x 0.3 us = about 5 us against a 15 ms pass [F pass time `docs/hardware-status.md` 10-19 08:10]; the
  snapshot at the command byte makes the 16 bytes one consistent result, and "new" says a gate ended since the last read.
  The counter runs always (gate 100 ms, source = the trigger channel; the firmware writes 0x61 / 0x62 at boot and on a
  trigger-channel change: 3 strobes). The measurement engine (`ui_meas.c`) takes Freq, Period, +Width, +Duty, Vmin,
  Vmax, Vpp from the counter when the counter's source is the card's channel; else from the record as today. Level and
  hysteresis follow the trigger level and 6 codes (about 3 x the 2 LSB noise) unless the host sets them.
- **Host**: **GET_COUNTER 0x0213** (main loop, cap COUNTER), request u8 what (0 read, 1 write config), u8 gate, u8 source,
  u8 level, u8 hysteresis, 3 bytes 0; response 24 bytes: 0 u8 flags (bit 0 new, bit 1 valid E >= 1, bit 2 EXT, bit 3
  CH2), 1 u8 seq, 2 u8 gate, 3 u8 source, 4 u32 E, 8 u32 T, 12 u32 W, 16 u8 min, 17 u8 max, 18 u16 0, 20 u32 freq_mHz
  (firmware division, saturating). `scope_measure(source="counter")` returns frequency_hz, period_s, width_s, duty, vmin,
  vmax with the gate and the resolution as caveats; `scope_status` carries the counter flags.

## 6. Streaming data logger (0x153D)

### 6.1 Why the sample ring is the FIFO

No block RAM is free (section 1.1). The sample memory (8192 x 32, `zaklad.v` 957-968) already is a ring with the MCU
reading it through 0x1F / 0x20..0x23; what a logger needs is a write that never stops and a readable write pointer.

FPGA design [E, about 150 LUT]:
- 0x69 bit 0 `stream`: `sampling_enable` forced 1, `trigger_check_enable` forced 0 (no trigger, no stop, 0x0E ignored);
  the arm is the normal 0x01 = 1 / 0 with 0x0B = 0x0C = 0; the write address and the decimator run as in any capture.
- A 32-bit word counter `log_cnt` (words written since 0x01 = 0) and the lap detector: `lap` sets when a write lands on
  the reader position 0x6A while `stream` (13-bit equality on the write edge, registered); 0x69 bit 1 clears it.
- Read 0x38 is a snapshot at the command byte (as 0x35): `W` = `sample_write_address` (the next word to be written),
  `log_cnt`, and `W - reader` modulo 8192 as the available word count.
- Everything else (decimator, FIR, peak, 16-bit or legacy layout, the readout pipeline with its dummy strobe) is unchanged.
  Timestamps come from the word counter: t = words x N x 5 ns (K >= 1) or words x 2 (D + 1) x 5 ns (legacy), exact
  because the write clock is the crystal's.

### 6.2 The arithmetic (the ceiling is the MCU, not the FPGA)

Measured inputs:
- FPGA bus read: **0.30-0.35 us per byte** (`docs/protocol.md` section 11; root `AGENTS.md`) [F]. A word's four lanes
  are four passes; a 16-bit channel = 2 bytes per sample, a legacy channel = 1 byte per sample (two samples per word).
- USB CDC: **2.35 MB/s** for 64 KiB responses in REMOTE on Windows (`docs/hardware-status.md` 2026-10-17 "Firmware 0.1.2"),
  1.0 MB/s in LOCAL, 0.93 MB/s on Linux via usbipd [F]; the F1C100s MUSB has no DMA, so these are CPU costs: 0.43 us per
  byte best case, 1.0 us per byte worst [I: the LOCAL figure includes the UI pass; the Linux one the usbipd path].
- The MCU does both in the same thread (no DMA for either), so per-sample costs add.

| Stream | Bus bytes / sample | USB bytes / sample | us per sample (best .. worst USB) | 100 % CPU rate | **Sustainable at 75 % CPU** [E] |
|---|---|---|---|---|---|
| 1 ch, 8-bit (legacy, or the high byte of a filtered word) | 1 | 1 | 0.73 .. 1.30 | 1.37 .. 0.77 MS/s | **1.0 .. 0.58 MS/s** |
| 1 ch, 16-bit filtered or peak | 2 | 2 | 1.46 .. 2.60 | 685 .. 385 kS/s | **510 .. 290 kS/s** |
| 2 ch, 8-bit | 2 | 2 | 1.46 .. 2.60 | 685 .. 385 kS/s per channel | **510 .. 290 kS/s** |
| 2 ch, 16-bit | 4 | 4 | 2.92 .. 5.20 | 345 .. 190 kS/s | **260 .. 145 kS/s** |

So "1 MS/s forever" is true only for one 8-bit channel on a Windows host at the measured REMOTE rate with nothing else
running on the MCU. **The documented specification (owner, 2026-10-20 22:38): 500 kS/s for one 8-bit channel, 250 kS/s
for one 16-bit or two 8-bit channels, 125 kS/s for two 16-bit channels, on both hosts; a higher tier is documented only
after the bench measures it (check 8 of 9.2).** The firmware's LOG_START validates the requested rate against this table
(E_BAD_ARG detail 1 "rate") and the host tool says what it accepted. The 8-bit filtered variant (high byte of the CIC word, 0x21 / 0x23 only) gives the filter's
anti-alias benefit at 8-bit cost: the default for one channel.

Ring timing: 8192 words at 500 kwords/s (1 MS/s legacy, or 500 kS/s 16-bit) is 16.4 ms; the MCU drains 2048-word chunks
every <= 4 ms (set 0x1F, dummy strobe, one pass per lane, then write 0x6A). A lap is a gap, never garbage: the firmware
records the word counter at the gap, the host reports it.

DDR ring: the image and .bss end below 0x81A00000, the restart record takes 0x81A00000 .. 0x81B00000, the start-up byte
is at 0x81BFFC1F (`fnirsi_1013d.ld` asserts, `variables.h` 94) [F]; how much of the 26 MB below is free is for the
firmware agent to read from the map [I: several MB]. Proposal: an 8 MB ring (8 s at 1 MB/s) placed by the linker, enough
for the Windows transport stalls seen on the bench (hardware-status 2026-10-17: multi-second stalls on 16 KiB chunks
before the transport fix).

### 6.3 Firmware

- **LOG_START 0x0214** (REMOTE, cap LOGGER): u8 channels (bit 0 CH1, bit 1 CH2), u8 format (0 legacy 8-bit, 1 filtered
  high byte, 2 filtered 16-bit, 3 peak pairs), u8 0, u8 0, u32 divider (the 0x0D value; the filter K / FIR / shift from
  the current SET_DECIM), u32 duration in ms (0 = until LOG_STOP), u32 0. Validation: the rate table above, the filter
  fits the format. The firmware arms with 0x69 = 1 and enters the log loop: a dedicated main-loop state (like the host
  capture wait) that drains the FPGA ring into the DDR ring and services USB; the UI is frozen behind the existing REMOTE
  banner (MCP-only feature, no new control; the banner text may say "Logging").
- **LOG_READ 0x0215** (deferred like WAIT, up to 64 KiB): request u32 max_bytes, u32 0; response 16-byte header (u32
  seq, u32 fpga_words_at_start, u32 bytes, u8 flags: bit 0 running, bit 1 gap since the last read, bit 2 ended, u8
  format, u16 0) then the bytes, oldest first, little-endian u16 for format 2, min / max pairs for 3. Returns at once
  with what is there (at least 1 byte or ended) or waits up to 100 ms.
- **LOG_STOP 0x0216**: ends the loop (0x69 = 0), the DDR ring stays readable until the next ARM.
- GET_STATUS `acq_state` gains 5 LOGGING; `run_state` unchanged.
- Simulator: `Device` streams synthetic samples at the requested rate into a model ring with the same lap rule.

### 6.4 MCP tools

- `scope_log_start(rate_hz, channels=["CH1"], bits=8, filter="auto", duration_s=None, file=None)` (tier 1): picks the
  divider and filter (K = 3 + FIR where eligible for 8 / 16-bit filtered, legacy for raw 8-bit), starts LOG_START, then
  pumps LOG_READ in a background task into `<home>/data/log-<time>.npz` (chunked, appended; header: rate, format, volts
  model, start time, FPGA word counter) and returns the accepted rate and the file path. `until stopped` when duration is
  None.
- `scope_log_status()` (tier 0): bytes written, seconds, gaps (word-counter jumps), USB rate achieved, MCU CPU margin
  from GET_STATUS pass_ms.
- `scope_log_stop()` (tier 1): LOG_STOP, finishes the file, returns the summary with caveats (gaps, the 8-bit filtered
  model, the rate table). `scope_report` renders a log like any `.npz`.

## 7. Pulse pattern generator with a trigger marker (0x153D)

### 7.1 Pins: there is one

P133 (`o_1khz_calib`) is the only FPGA signal that reaches the outside (README "Pin map"; `SYNTHESIS.md` section 2: about
24 unused pads, none routed to a connector) [F], and it stays the only one: the owner's standing rule is no soldering to
FPGA or other pins in this project, the prepared flash header is the only one (owner, 2026-10-20 22:4x). The DDS output
and the EXT input already share it (`gen_out_enable` selects, `zaklad.v` 713). So a "trigger output" pin does not exist,
and the pattern's marker is realised as an **internal trigger source** instead: 0x15 = 3 PATTERN makes the capture
trigger at the start of the step flagged `mark`, through the EXT apply path (`ext_pending`, applied on the next write
clock rise, so the trigger position convention is EXT's). Its use is a pattern started from the MCU with no external
event: the pattern is the TX drive and the scope triggers on its own marker with the pretrigger set to see the RX decay
after the pulse. Patterns that must follow an external event use the pattern trigger engine (7.5). Output level is the
logic level: about -1.23 V low / +1.87 V high on the post against the probe ground (README "Changes" section 2,
`hardware-status.md` 2026-10-18) [F]; no DAC.

### 7.2 Design: the pattern generator is the generator (owner, 2026-10-20 23:00 and 23:04)

Atlan4's DDS (`freq_generator_dds_pwm`, 50 MHz, 32-bit phase increment, `zaklad.v` 121-135, 704-716) and its registers
0x51 / 0x52 are **removed in 0x153D**; measured saving 61 LUT / 97 reg (section 13). The square-wave generator becomes a
two-step continuous pattern (high n_h clocks, low n_l clocks on the 5 ns grid), programmed by the firmware for the
generator menu, the wizard's 1 kHz, the device Bode sweep and the host Bode tool.

- **Step table**: 8 entries (0x6C index 0..7; the index field allows 32, so a deeper table is a build-time choice, about
  50 reg per entry), each `level`, `mark`, `end`, a **u32 integer duration** in 5 ns clocks (>= 2) and a **u16 fraction**
  in 1/65536 clock. Runs on `clk_200MHz`: a 33-bit down counter with a frozen sign (step done), the next step's count
  preloaded during the current step.
- **Fractional durations, one accumulator per engine** (owner 23:04): at the start of every step the engine adds the
  step's fraction to a 16-bit accumulator; when it carries, the step lasts one clock longer. The average duration of a
  step is exactly n + f / 65536 clocks and **every edge lands within one 5 ns clock of its ideal time**, deterministically
  (Bresenham). A prototype in iverilog confirmed it (`tb_pat2.v`, fpga-spec scratch: 3.5 + 2.25 clock steps, 696 edges,
  mean period 5.75 clocks, worst edge deviation 1 clock). Two accumulators (one per step kind) do **not** give that:
  the same prototype with separate accumulators put 174 of 696 edges 2 clocks off, because the residues add.
  **Reset rule**: the accumulator is cleared on every burst start (0x6D bit 4 or the pattern trigger engine's event), so
  a burst is reproducible edge for edge; it is **not** cleared when a pending table is taken (the frequency change stays
  phase-continuous) nor at the wrap of a continuous pattern (the average frequency stays exact across periods).
- **Double buffer** (owner 23:00): entries 0 and 1 have shadow registers. A 0x6C write to index 0 or 1 goes to the
  shadow; **0x6D bit 5 commit** marks both pending; at the next step boundary the active copies take the shadows and
  `pending` clears (0x39 byte 0 bit 5). The step in progress completes with its old duration, the new one applies from
  the next step: glitch-free and phase-continuous (prototype: a 3.5 / 2.25 to 10 / 10 change mid-run, shortest step 10
  clocks). A commit while one is pending overwrites the shadow and the earlier pending values are lost: the handshake is
  either to poll bit 5 before writing or to pace writes slower than one pattern period (7.3). Entries 2..7 are single
  (written before a start; a write during a burst takes effect at that entry's next use).
- **Timing** (measured on the prototype, section 13): the one-cycle form "select the next step's fraction by `out`, add
  it to the accumulator, add the carry into the next count" missed `clk_200MHz` by 0.88 ns (44 endpoints). The contract
  form is pipelined: the fraction add is registered in the first clock of the current step (`acc_sum_r`), and the
  boundary loads `cnt <= n_next - 2 + acc_sum_r[16]`, one 33-bit add with a carry-in. Steps are >= 2 clocks, so the
  pipeline always has its cycle. Cost [E on top of the measured 48 LUT / 231 reg core]: the register stage only.
- **Burst structure**: steps 0 .. the first `end`; mode 1 one burst per start, mode 2 R bursts (0x6E) with the gap 0x6F
  between them, mode 3 continuous (gap included); idle level 0x6D bit 2 between bursts and in mode 0. A start comes from
  the MCU (0x6D bit 4), from the pattern trigger engine's event (7.5), or from either (0x6D bits 7..6); a start during a
  burst is ignored. The marker respects the capture's `trigger_check_enable` (the pretrigger must have filled), so a host
  that wants the marker in a capture arms first and starts the pattern after 0x01 = 0; nothing starts a pattern from the
  capture trigger (owner: not needed).
- **Pin**: 0x50 keeps its meaning (1 = driven, 0 = hi-Z for EXT); the pattern's level is what is driven. Power-on: every
  flop 0, so the pattern is off and the pin hi-Z until the firmware writes the 1 kHz square at boot
  (`fnirsi_1013d_scope.c` 166 `fpga_on_off_generator`, rewritten in D5) [F].
- **The square wave**: mode 3 with entry 0 = high (n_h, f_h), entry 1 = low with `end` (n_l, f_l). Period P = n_h + n_l +
  (f_h + f_l) / 65536 clocks, **f = 200 MHz / P**. Frequency grid: integer periods give 200 MHz / P (a 50 % square needs
  an even P: 100 MHz / (P/2)); with fractions the average frequency is set to 1/65536 clock per period, i.e. a relative
  resolution of 2^-16 / P: 7.6e-11 at 1 kHz, 7.6e-7 at 10 MHz; every edge within 5 ns of ideal. Range: 23 mHz (n = 2^32
  - 1 per step) to 50 MHz (2 + 2 clocks); SET_GEN keeps its 25 MHz cap. Marker: a 1-clock pulse at the first clock of a
  `mark` step; 0x39 byte 3 bit 1 reports it since 0x01 = 1.

### 7.3 Chirps and sweeps are MCU-stepped (owner 23:00): no sweep logic in the FPGA

A sweep is the firmware writing the next period per step: 0x6C index 0 (8 strobes), 0x6C index 1 (8), 0x6D commit (2):
**18 strobes, about 5.4 us** at 0.3 us per strobe [F bus cost]; a 0x39 poll of the pending bit is 2 strobes more.
- **Handshake**: either poll 0x39 bit 5 = 0 before the next commit (the earlier values were taken), or pace the steps at
  least one pattern period apart, since a commit is taken at the next step boundary and a second commit before that
  overwrites it. The firmware's chirp loop uses the pace rule for steps above the pattern period and the poll below it.
- **Step-rate limit**: the bus cost alone would allow about 180,000 steps per second; the contract is **5,000 steps per
  second** (200 us per step, 2.7 % of the CPU on the bus), paced by the free-running timer in a dedicated main-loop state
  like the Bode engine's, with the UI frozen; above 5 kHz the step would be shorter than the loop's own jitter [I]. A
  linear chirp f1 -> f2 over T seconds in K <= 5000 T steps has a frequency resolution of (f2 - f1) / K per step and is
  phase-continuous by construction (the accumulator and the step in progress carry over).
- **Bode**: the device engine and the host tool program one period per point exactly as today's 0x51 write, through
  SET_GEN or the generator functions (7.4); nothing else changes in the sweep logic.

### 7.4 Host and firmware: the generator on the pattern

**SET_GEN 0x0301** (REMOTE, cap GENERATOR) keeps its 20-byte layout (`protocol.md` 8.6) with new raw modes (one-bitstream
rule, no shim): `freq_mode` 1 = the raw period P16 in 1/65536 clocks of 5 ns (u64; the integer in bits 63..16, the
fraction in 15..0), `duty_mode` 1 = the raw high duration H16 in the same unit; the mHz / basis-point modes are unchanged
for the host. GEN (the response) reports P16 and H16 beside mHz and bp. The firmware's conversion, integer only (no FPU),
with f in mHz and d in basis points:

```
P16 = (2^16 * 2e11 + f/2) / f          period in 1/65536 clocks; 2e11 = 200e6 clocks/s * 1000 mHz/Hz (<= 1.3e16, u64)
H16 = (P16 * d + 5000) / 10000         high part (<= 2^49 * 10^4 < 2^64)
L16 = P16 - H16
n_h = H16 >> 16, f_h = H16 & 0xFFFF;  n_l = L16 >> 16, f_l = L16 & 0xFFFF
require n_h >= 2 and n_l >= 2 (else E_BAD_ARG detail 1 / 2)
back: f_mHz = (2^16 * 2e11 + P16/2) / P16;  d_bp = (H16 * 10000 + P16/2) / P16
```

The average frequency is 2^16 x 2e11 / P16 mHz with P16 rounded to the nearest 1/65536 clock: relative error <= 2^-17 /
P (3.8e-11 at 1 kHz). The wizard's 1 kHz is exact (n_h = n_l = 100,000, f = 0); 1.00o5's whole-Hz / whole-percent entry
points (`fpga_set_generator_freq` / `_duty`, `fpga_control.c` 306-336) call the same two formulas. The firmware writes the
two entries and the commit (18 strobes) for every frequency or duty change; a change while the generator runs is
glitch-free (7.2), which also removes the "duty first, then frequency" write order of today's SET_GEN.

**Bode consequence** (`docs/plans/bode-design.md` 2.7; `bode.py` `phase_inc_for` / `dds_frequency`; `bode_dsp.h`
`BODE_GEN_HZ` 50 MHz / M; READ_BODE row u32 M) [F]: the stimulus period becomes P clocks of 5 ns instead of M clocks of
20 ns, so every existing plan maps exactly with **P = 4 M** (the N tie below 24 kHz, the coherent record and the vectors
hold with that substitution), and the grid is **four times finer**: the step to the neighbouring period is 0.5 % at
1 MHz (was 2 %), 1 % at 2 MHz (4 %), 2.5 % at 5 MHz (10 %), 5 % at 10 MHz (20 %); a 100-per-decade sweep no longer
merges points below 2 MHz. The plan keeps **integer P with f = 0** (a strictly periodic stimulus; the DFT's bin is then
exactly 200e6 / P, and the 2^-32 "f_a" rounding term of 2.7 disappears) and duty by n_h = ceil(P / 2), n_l = floor(P / 2)
(odd P is allowed, as odd M was). READ_BODE's row field "M in 50 MHz clocks" becomes "P in 5 ns clocks"; `plan_point`,
`bode_dsp.c`, `docs/bode-plan-vectors.json` and `host_tests/test_bode.py` change together (protocol 1.27). Fractional
periods are available to the Bode tools as an option for a later study (a dithered edge within 5 ns is far below the
front end's 30 MHz), not in this contract.

- **SET_PATTERN 0x0217** (REMOTE, cap PATTERN_GEN, tier 3): u8 mode, u8 flags (idle level, marker as trigger, start
  source bits 7..6 as 0x6D), u16 R, u32 gap_ns, u8 steps n (1..8), u8 commit, then n x { u8 flags (level, mark), u8 0,
  u16 fraction_1_65536, u32 duration_clocks } (durations in 5 ns clocks plus a fraction, so the host controls the exact
  value; `scope_pattern` converts from microseconds); the firmware writes 0x6C x n, 0x6E, 0x6F, 0x6D (commit when asked)
  and starts when u8 start = 1. Response: the plan as written and the burst length.
- **SET_PATTERN_TRIGGER 0x021A** (REMOTE, cap PATTERN_TRIGGER, tier 3): the SET_TRIGGER2 body (24 bytes, same layout and
  validation) followed by u8 armed, u8 rearm, u16 0; the firmware's SET_TRIGGER2 writer runs with the pattern engine's
  code table (7.5) and then writes 0x7D. Response: the GET_TRIGGER-style echo plus the 0x42 status. In the shared fallback
  (0x42 bit 7 = 0) a configuration that differs from the capture trigger's while the capture also uses a qualified type
  answers E_STATE detail 8 "shared trigger engine".
- No SET_SWEEP: a chirp is a sequence of SET_GEN writes (7.3); 0x0218 stays free.
- **MCP** `scope_pattern(steps=[{"level":1,"width_us":50,"mark":True},{"level":0,"width_us":950}], repeat=10,
  gap_us=0, mode="repeat", idle_level=0, trigger_on_marker=True, start="mcu", trigger=None, rearm=True,
  confirm_token=None)` (tier 3, the generator's confirmation token: it drives the pin, as `scope_generator`, `server.py`
  `confirm_token` flow); `start` is "mcu", "trigger" or "both" and `trigger` takes the same dictionary as
  `scope_set_trigger` (source, type, edge / polarity, level, width, n, idle) for the pattern engine. `scope_generator`
  keeps its interface (frequency, duty, enable) on SET_GEN. A chirp is `scope_sweep(start_hz, stop_hz, time_s,
  steps=None, confirm_token=None)` (tier 3): the host sends one SET_GEN per step at the 7.3 pace (a USB round trip per
  step, about 0.2 ms REMOTE, so the host-driven chirp is limited to about 2,000 steps per second; the device-paced 5,000
  needs a firmware CHIRP command, not in this contract). Both restore the generator state they found (as
  `scope_bode_sweep` does, `docs/plans/bode-design.md` section 5.1). `scope_set_trigger(source="pattern")` arms the
  capture on the marker.

### 7.5 Pattern trigger engine (owner, 2026-10-20 22:4x: "yes on the implementation")

The pattern generator gets its own trigger engine, orthogonal to the capture trigger and with the same vocabulary:
source CH1 / CH2 / EXT, types edge, pulse width, Nth edge, timeout, level, polarity, N, H. A detector's RX event or
its own TX sync can then start a burst without the MCU in the loop, while the capture triggers on whatever it likes
(the marker, the same event, or something else).

**Registers**: 0x76-0x7D, one contiguous block mirroring the capture trigger's byte layouts field for field (0x15, 0x16,
0x17, 0x55, 0x5E, 0x5F, 0x60 in that order, then the engine control 0x7D); status read 0x42 laid out as 0x34. The capture
trigger's registers are scattered, so "the same driver with a base offset" is literally a 7-entry table of command codes
per engine (`{0x15, 0x16, 0x17, 0x55, 0x5E, 0x5F, 0x60}` and `{0x76 .. 0x7C}`) feeding one byte builder; the field
layouts, units and validation are identical, and so is the MCP dictionary. The layout is the same whether the FPGA has
its own engine or the fallback below, so the MCP interface does not change.

**Stream and units**: the engine watches the same streams as the capture's qualifier: the ADC trigger stream (one
sample per memory word, legacy core A or the 16-bit / peak word, compared against its own level 0x78 / 0x79) and
`ext_level` at 5 ns. N and H have the capture engine's units (words for CH1 / CH2, 5 ns for EXT). The ADC stream exists
whenever the write clock runs (`trigger_data` loads on every `swc_adv`, `zaklad.v` 1268-1282, independent of the
capture's state) [F], so the pattern engine works with the capture idle, armed or streaming; its sample period follows
the capture's divider and filter, which the host sets anyway.

**Design** (own engine, the intended build) [E, about 350 LUT / 200 reg]: one `trig_qual` instance on `clk_200MHz`
(gen8's EXT instance measured about 175 LUT [I: half of the two-instance pair's share of the +1115]) with `ce` = 1 for
EXT or the write-clock snapshot strobe (`swc_rise_q & adv_d`, the sequencer's existing crossing, `zaklad.v` 1515-1529)
for CH1 / CH2, its `a` from a registered compare of `trigger_data` / `dec_trig_word` against 0x78 / 0x79 (8-bit: 10 LUT;
16-bit: about 40), `arm` = 0x7D bit 0, `rst` = the write of 0x7D bit 0 = 1 or the re-arm after a burst. Event: type 0 the
edge in the polarity's direction (`ap & ~a`), types 1 and 2 the registered `fire` with the next sample on the post side
(what `trigger_previous` does for the capture), type 3 `fire` itself. The event starts a burst (0x6D bits 6..5 = 1 or
2), sets the 0x42 flag and, with re-arm off, clears 0x7D bit 0. Latency event to pin: one write clock plus 3 to 4
`clk_200MHz` cycles for CH1 / CH2 (so the burst start jitters by one word against the signal), 4 cycles (20 ns) for EXT.
The engine never touches the capture's timed path.

**Does the second instance fit on paper?** 0x153D with the own engine: about 6,830 LUT of 8,640 (79 %, section 1.2 after the
measured DDS and cruft removal), no block RAM,
one M18x18 free; it fits in sites. The risk is the `clk_200MHz` placement lottery, not the count, so the D2 agent
builds the own engine first and reads the timing summary; the actual LUT cost of C1's two qualifier instances
(measured in the 0x153C build report) replaces the 175 LUT estimate before D2 starts.

**Fallback** (if the 0x153D build does not close timing or exceeds the budget): one qualifier engine, the capture's,
**routed** to the capture, the pattern or both by 0x7D bit 3 (0 = capture, 1 = pattern; 0x6D bits 6..5 = 2 with bit 3 =
1 gives both from the same event). Registers 0x76-0x7D exist unchanged; with bit 3 = 1 the FPGA takes the engine's
configuration from 0x76-0x7C and the capture's qualified types are unavailable (the capture still triggers on edges,
EXT and the marker), with bit 3 = 0 the pattern engine is off. Read 0x42 byte 0 bit 7 = 0 tells the firmware, which
then refuses a conflicting pair (SET_PATTERN_TRIGGER E_STATE detail 8). **Visible limitation**: pattern and capture
cannot use two different qualified triggers at the same time; "both" means both start on one event, which is the
detector's common case anyway (TX fires and the capture starts on the same RX edge). The MCP tools are the same.

## 8. Software on the new data (host side, no FPGA)

- **DVM readout**: yes. A 16-bit CIC3 + FIR capture at N = 4096 (48.8 kS/s) averages 4096 x 2 raw codes per output; the
  record mean of a few thousand outputs is a 4-digit DC reading limited by the calibration (0.5 % class per
  `protocol.md` row 51) rather than noise. `scope_measure(dvm=True)` returns Vdc with the calibration caveat; counter
  min / max give Vpp.
- **Serial decode**: UART only, as an MCP tool over a capture (metal-detector MCU debug lines; the sample rate is chosen
  from the baud rate, 16 samples per bit, the counter confirms the baud). SPI / I2C need two or three channels; the scope
  has two and the EXT pin is the pattern's; not recommended now.
- **Mask testing**: not now. It needs a reference envelope and a verdict per record; segments give the records, the
  phosphor map the envelope, but the owner's use (RX decay) is served by `scope_decay_fit`. Revisit when a community user
  asks.
- **CSV export**: yes, trivial: `scope_capture(..., csv=True)` writes the `.csv` beside the `.npz` (time, volts per
  channel, min / max for peak records); `scope_report` already renders tables.
- **Trigger on both cores as an FPGA option**: yes, in 0x153E (section 3.4): it halves the trigger uncertainty at
  200 MSa/s, which is what the phosphor view and the 50 ns/div time bases show.
- **Phosphor fast capture on segments**: unchanged plan (`docs/plans/phosphor-fast-capture.md`); peak detect gives the
  phosphor map the glitches at slow time bases without the fast loop.

## 9. Simulation and bench plan

### 9.1 Testbenches (iverilog, `fpga/al3/tb/run.sh` steps; conventions as the existing `tb_*.v`)

| Gen | Step | File | Checks |
|---|---|---|---|
| C | 14 | `tb_trigtypes.v` (from gen8: 7 cases, both polarities, every type, 1,122 checks per seed) rebased: the reference model finds the trigger sample on the stream the FPGA saw; plus hold-off cases (a train with hold-off between 1 and 3 periods, across a sequencer restart and across an MCU re-arm) and the 0x153B stop gate per type | the FPGA's trigger sample = the model's; positions equal the edge type's; no trigger after the stop |
| C | 15 | `tb_peak.v` (gen8, 29 checks) rebased onto the 0x153A capture (`adc*_w`) | every word = { max, min } of the K = 1 window; alias count; trigger on max / min; averaging refused is a firmware test |
| C | 16 | `tb_counter.v` (gen8, 36 checks) extended for 0x35 bytes 14, 15 | E, T, W, min, max equal to the stream model in every gate; consecutive results 2e6 clocks apart |
| C | 1-13 | the existing steps | unchanged behaviour with the new registers at 0 (equivalence phases 0 mismatches); `contract_0x153C.txt` replaces 0x153B's (version byte, then the new firmware sequences of 9.3) |
| D | 17 | `tb_stream.v` | streaming: the write never stops, 0x38 W and the counter match a mirror of the write port, the lap flag sets exactly when W reaches 0x6A, chunks read with the firmware's byte sequences are contiguous across laps; with 0x69 = 0 equivalence |
| D | 18 | `tb_pattern.v` | step durations and levels on the pin to the clock against the table, bursts, gap, R, continuous, idle level; **the square wave**: the firmware's SET_GEN byte sequences for 1 kHz, 999.5 Hz, 10 kHz 25 %, 1 MHz and 10 MHz against a model (every edge within one clock of ideal, the mean period equal to P16 / 65536 over 1,000 periods), the double buffer (a frequency or duty change mid-run: no step shorter than the old or new value, the accumulator not cleared, the pending bit's timing), the accumulator cleared at a start (two identical bursts edge for edge), a commit lost by a second commit; the marker triggers a capture at the EXT convention's position; the pattern trigger engine: every type on CH1 (legacy and K = 2), CH2 and EXT against the `tb_trigtypes.v` reference model, the burst starting one word (CH) / 20 ns (EXT) after the qualifying sample, re-arm and one-shot, the capture triggering on a different type at the same time (own engine) or the routed fallback's refusal path; with 0x6D = 0 and 0x7D = 0 the pin is the idle level (the DDS reference of the earlier steps is gone: tb_ext_trigger's generator checks move to the 1 kHz pattern) |
| E | 19 | `tb_trig2.v` | both-core crossings at 5 ns against a model of the interleaved stream, 0x37 bit 1; runt and window against a model with two levels; pulse-width range |

Mutation runs as for 0x1534 / 0x1536 (`DUT=` override, `mutate_seg.py` pattern). Each generation keeps `tb/run.sh` ALL
PASS on two seeds before a build is pinned (the 0x153B rule, `AGENTS.md` fpga-gen12).

### 9.2 Bench checks per generation (owner at the bench; flash per "Flashing this unit", never without the backup)

0x153C:
1. 0x06 = 0x153C; with 0x5E / 0x56 bit 1 / 0x61 / 0x1B at 0 the 0x153B checks 1-6 (README section 11) repeat.
2. Pulse width: generator 10 kHz 20 % on the post to CH1: "longer than 15 us" triggers, "longer than 25 us" does not,
   "shorter than 25 us" triggers on the falling edge (gen8 check 2).
3. Nth edge on a burst from `scope_pattern` is a 0x153D check; on 0x153C use the generator with a host loop that gates
   the output: trigger on pulse 3 after an idle gap.
4. Timeout: stop the generator during a run: the trigger comes N samples after the last edge.
5. Hold-off: 1 kHz square, hold-off 2.5 ms: every third edge triggers (segment timestamps show 3 ms spacing).
6. Peak detect at 1 MSa/s on a 100 kHz square and on a 6 ns spike train from the pattern generator (0x153D) or a
   narrow DDS duty: every spike visible, envelope stable.
7. Counter: the post at 1 kHz and 1 MHz on CH1 and on EXT: frequency within 1e-5 of phase_inc x 50 MHz / 2^32, duty
   50 %; min / max equal the record's.

0x153D:
8. Logger: 1 kHz on CH1, 1 ch 8-bit filtered at 500 kS/s for 60 s: zero gaps, the file's frequency 1000.00 Hz; then
   the next tier up until a gap appears: that is the bench ceiling, written into the rate table.
9. Logger with 2 x 16-bit at 125 kS/s for 10 min while the owner opens a menu: E_BUSY on the host, the log continues.
10. Pattern: a 50 us pulse every 1 ms, 10 bursts, marker trigger: 10 segments with timestamps 1 ms apart and the RX
    (a probe on the post) at the pretrigger point. The generator on the pattern: the wizard's gain step passes; 1 kHz
    and 1 MHz read by the counter within 1e-7 (the counter and the pattern share the crystal, so this checks the
    arithmetic, not the crystal); 999.5 Hz (a fractional period) reads 999.5000 Hz on the counter at the 1 s gate; a
    frequency change while the Scope view shows the square leaves no glitch in 50 traces; a host chirp 1 kHz .. 100 kHz
    in 1 s seen on the Spectrum view.
10b. Pattern trigger engine: the generator's 1 kHz square from a second scope or the RC ladder's node on CH2, the pattern
    engine on CH2 Nth edge 3 starting a 20 us pulse on the post, the capture on CH1 edge: the pulse sits one word after
    every third CH2 edge in 100 segments; then EXT as the engine's source with the generator off.

0x153E:
11. Both cores: 1 MHz square at 200 MSa/s, 1000 captures: the trigger core bit splits about 50 / 50 and the trace
    jitter is halved against 0x153D (the trigger-position re-measure of `firmware/AGENTS.md` section 48).
12. Runt and window on a two-level pattern (the pattern generator has one level; use the RC ladder's node as the
    second level source).

### 9.3 Firmware and MCP work per generation

| Gen | Firmware (files in `firmware/scope/`) | Protocol | MCP (`mcp/src/fnirsi1013d/`) |
|---|---|---|---|
| C | `fpga_control.c` / `fpga_bus.c` writers for 0x5E-0x62, 0x65, 0x66, 0x1B, readers 0x34, 0x35, 0x37; `pc_hal_scope.c` arm sequence (type registers inside the 0x01 window after 0x0B / 0x0C, as 0x57..0x5C); `pc_decim.c` peak mode (K = 1, 0x56 bit 1, level << 8, no shift); `pc_cmds.c` SET_TRIGGER2 / GET_COUNTER / READ_RAW layout 3 / GET_CAPTURE_INFO 79 / SET_DECIM mode 4; `ui_meas.c` counter-fed cards; `host_tests/fpga_model.c` + `test_fpga_bus.c` (every new register, the arm order), `test_protocol.c`; `pc_cmds.h` caps; the mismatch banner (`pc_ident.c`, Diagnostics) for one version word | 1.26: **SET_TRIGGER2 0x0219** (REMOTE): u8 type, u8 polarity, u8 flags (longer, range, both cores), u8 source, u32 N, u32 N2_or_H, u16 level, u16 level2, u32 holdoff_ns, u32 0 (24 bytes; SET_TRIGGER stays for the edge type and its fields); GET_TRIGGER appends the same; GET_COUNTER 0x0213; READ_RAW layout 3; vectors in `docs/protocol-vectors.json` | `protocol.py` structs and vectors, `scope.py` `set_trigger(type=..., width=..., n=..., idle=..., level2=..., holdoff=...)`, `measure(source="counter")`, `capture(acquisition="peak")`; `server.py` tool arguments; `simulator.py` qualifier model (reuse `tb_trigtypes`'s reference model, ported to Python) and the counter; `tests/` |
| D | `pc_hal_scope.c` log loop and DDR ring (`fnirsi_1013d.ld` section), `pc_cmds.c` LOG_* / SET_PATTERN / SET_PATTERN_TRIGGER and SET_GEN's P16 / H16 maths (replacing `pc_gen_phase_from_mhz` / `pc_gen_mhz_from_phase`, `pc_cmds.c` 61-72), **the generator rewrite**: `fpga_control.c` `fpga_set_generator_freq` / `_duty` / `fpga_on_off_generator` (306-336, 268-304) write 0x6C x 2 + 0x6D instead of 0x51 / 0x52 / 0x50, callers unchanged (`menu.c`, `cal_wiz_ui.c` 541, `bode_view.c` 377-379 and 658, `fnirsi_1013d_scope.c` 166); the Bode planner on P = 4 M (`bode_dsp.h` `BODE_GEN_HZ`, `bode_phase_inc`, `bode_sweep.c` 145 `hw->gen`), `generator.c` pattern writers (0x6C-0x6F), the SET_TRIGGER2 writer parameterised by the code table (0x76-0x7D), `pc_usbw.c` for 64 KiB LOG_READ frames; `test_fpga_bus.c` stream chunking and lap, the pattern engine's registers, the generator's byte sequences; `test_protocol.c`; `host_tests/test_bode.py` and `docs/bode-plan-vectors.json` on P | 1.27: LOG_START 0x0214, LOG_READ 0x0215, LOG_STOP 0x0216, SET_PATTERN 0x0217, SET_PATTERN_TRIGGER 0x021A, SET_GEN raw modes = P16 / H16, READ_BODE row M -> P, acq_state 5 (0x0218 stays free: no SET_SWEEP) | `scope_log_*`, `scope_pattern` (with `start`, `trigger`, `rearm`), `scope_sweep` as a stepped SET_GEN loop, `bode.py` `phase_inc_for` -> `period_for` (P = 4 M, `dds_frequency` -> 200e6 / P), `report.py` log rendering, simulator streaming, the pattern with its fraction accumulator and double buffer, its engine (the qualifier model shared with the capture's) |
| E | SET_TRIGGER2 flags both cores / range, type 4, 5; `pc_hal_scope.c` half-sample offset from 0x37 | 1.28: no new command (the fields are reserved in 1.26) | `set_trigger(type="runt" / "window", both_cores=True)`, simulator |

Command numbers 0x0213-0x021A are the next free after READ_BODE 0x0212 (`pc_protocol.h` 110-111) [F] (0x0218 was
SET_SWEEP and stays free); 0x020B is unused
(`bode-design.md` 5.2 says check why before using it) and stays unused.

## 10. Implementation briefs

Each item is one agent; items in one row's "same block" column must be one agent because they edit the same Verilog
block (`zaklad.v` has one `always` block per concern; two agents in one block collide). All FPGA items start from
`origin/main` at 0x153B (c173ed5 source) and branch `fpga-genNN`; firmware items start after the FPGA item's register
map is merged (the map is this document, so they can start now against `fpga_model.c`).

### 0x153C

| Item | Touches | Same block as |
|---|---|---|
| C0 **Cruft removal** (Sonnet, **first, on main before C1-C3 branch**: half a day, every edit measured in section 13): the 0x1B / 0x3C / 0x41 / 0x24-0x27 decoder and read-mux entries, `dec_neg_a/b`, `dec_swap` (0x29 bits 4..2 read 0, `decim_chan` loses its `swap` port), `sample_read_enable`, `trigger_detect_enable` with `trigger_rise/fall_enable`, `w_last` under `` `ifdef SIM`` (tb_timeout keeps it), the divider compare narrowed to 20 bits (div20: the 0x0D register keeps 4 bytes, bits 31..20 ignored; the firmware's largest divider is 199,999); the DDS stays (0x153D). Equivalence steps of `tb/run.sh` must stay green (the references keep the registers; the tb masks the read-back bits that change) | `zaklad.v`: decoder, read mux, `decim_chan` ports, the divider block 1037-1059, the trigger enables 1245-1247 and 1257-1264; `tb/*` masks; `FPGA commands.txt`; README section 12 (cruft) | lands before C1-C3, so none; C1-C3 rebase on it |
| C1 **Trigger types + hold-off rebase** (Opus): cherry-pick 9de82ca's `trig_qual`, the 0x5E/0x5F/0x60 decoder, 0x15/0x16 recompute, `trigger_previous` load and `ext_qev_r`; move "longer" to 0x5E bit 3; add 0x1B hold-off, 0x66, read 0x37; gate `fire` with the 0x153B stop; rebase `tb_trigtypes.v`, add the hold-off and stop cases; README "Changes" section 12 (triggers) | `zaklad.v`: capture logic 1159-1368, EXT block 718-779, decoder entries, read mux; `tb/tb_trigtypes.v`, `tb/run.sh` step 14; `FPGA commands.txt` | C2 (the `decim_trig_sample` lines 1155-1157 are shared: C1 owns them, C2 sends its two-line change to C1) |
| C2 **Peak detect** (Opus): cherry-pick the `decim_chan` peak ports and registers, `peak_act` / `fir_ok`, 0x56 bit 1, 0x29 byte 1; **new against gen8**: `dec_on` includes `peak_en` and `decim_chan` runs as K = 1 when peak is on with K = 0 (the one-register switch of section 4); rebase `tb_peak.v` onto `adc*_w` and add the K = 0 + peak case; README section 12 (peak) | `zaklad.v`: `decim_chan` 153-230, `dec_on` 660 and the divider force 1052, decimator control 1061-1150; `tb/tb_peak.v`, step 15 | - |
| C3 **Counter + min / max** (Opus): cherry-pick the `k_*` / `m_*` blocks and 0x61 / 0x62 / 0x35; add min / max; `tb_counter.v` to 16 bytes; README section 12 (counter) | `zaklad.v`: new block after `seq_lat` 1771-1773, decoder, read mux; `tb/tb_counter.v`, step 16 | - |
| C4 **Integration, build, contract** (Opus, after C1-C3): merge, version byte 0x3C, `contract_0x153C.txt`, run.sh ALL PASS on two seeds, `build.sh`, `capture_report.sh`, dist manifest (`tools/bitstreams.py`), AGENTS.md status section | `zaklad.v` header and 0x06, `tb/run.sh`, `tb/contract_0x153C.txt`, `dist/manifest.json`, `BITSTREAMS.md`, `fpga/al3/AGENTS.md` | - |
| C5 **Firmware triggers + hold-off** (Opus): SET_TRIGGER2 / GET_TRIGGER, the arm sequence, caps bits 2 / 20, `fpga_model.c`, vectors | `pc_cmds.c/h`, `pc_hal_scope.c`, `fpga_control.c`, `fpga_bus.c`, `host_tests/fpga_model.c`, `test_fpga_bus.c`, `test_protocol.c`, `docs/protocol.md` row 65, `docs/protocol-vectors.json` | C6, C7 share `pc_cmds.c` dispatch and `fpga_model.c`: C5 lands first, C6 / C7 rebase |
| C6 **Firmware peak detect** (Opus): SET_DECIM mode 4, READ_RAW layout 3, GET_CAPTURE_INFO 79, the 8-bit mean-core correction, averaging refusal | `pc_decim.c/h`, `pc_cmds.c`, `pc_hal_scope.c`, `pc_seq.c` (refusal), host tests | C5 |
| C7 **Firmware counter** (Sonnet): GET_COUNTER, the once-per-pass 0x35 reader, boot config (gate 100 ms, trigger channel, 128 / 6), `ui_meas.c` card sources (the cards take the counter's values; no new control, decided) | `pc_cmds.c`, `fpga_control.c`, `ui_meas.c`, host tests | C5 |
| C8 **MCP 1.26** (Opus): protocol structs, vectors, `Scope` methods, tools, simulator qualifier and counter models, tests, `docs/mcp-tools.md`, `docs/agent-measurement-guide.md` sections | `mcp/src/fnirsi1013d/{protocol,scope,server,simulator,analysis}.py`, `mcp/tests/`, docs | - |
| C9 **One-bitstream firmware cleanup** (Opus, owner's timing): the removals listed in the task queue "One supported FPGA configuration" and the version banner; independent of C1-C8 but must merge before the 0x153C firmware release | 62 capability sites in 11 files, `enc_heal.c/h`, variant code | C5-C7 touch `pc_cmds.c` too: sequence by the coordinator |

### 0x153D

| Item | Touches | Same block as |
|---|---|---|
| D1 **Logger RTL** (Opus): 0x69 / 0x6A / 0x38, `stream` gating, word counter, lap; `tb_stream.v`; README section 13 | `zaklad.v`: capture logic (`sampling_enable`, `trigger_check_enable` 1219-1234), write address block 1163-1169, decoder, read mux; step 17 | D2 shares the decoder / read mux only (append-only entries, no conflict if each adds its own case lines) |
| D2 **Pattern generator (= the generator) + pattern trigger engine RTL** (Opus): **remove the DDS** (`freq_generator_dds_pwm`, `u_freq`, `clk_out_divider` / `clk_out_duty`, the 0x51 / 0x52 decoder entries and their `mcu_state` terms: the measured -61 / -97); new module `pat_gen` (8-entry table with u32 + u16 durations, the one fraction accumulator per engine with the pipelined carry of 7.2, shadow entries 0 / 1 with commit and pending, bursts, gap, R, start sources 0x6D bits 7..6, marker), the pin assign (`gen_out_enable ? pat_level : 1'bZ`), marker into `ext_pending`, 0x15 = 3; the **pattern trigger engine** (7.5): a third `trig_qual` instance on `clk_200MHz` with its own level compares, 0x76-0x7D, read 0x42, re-arm, event to `pat_gen`; build the own engine first, read the timing summary, fall back to the routed shared engine only if it does not close, and report the measured LUT delta per block (the fpga-spec prototype `tb_pat2.v` / `pat2` module in the scratch notes of section 13 is the seed for the core); 0x6C-0x6F, 0x39; `tb_pattern.v` incl. the square-wave and engine cases; README section 13 | `zaklad.v`: generator 121-135 and 704-716, EXT block (`ext_edge` 744, `ext_pending` 775-778), the `trig_qual` instances block (C1's, read-only here: a third instance is added beside them; the shared fallback alone edits C1's `trigger_previous` load), decoder, read mux; step 18 | D1 (both append to the decoder; D2 also edits the EXT block, which C1 edited: D2 starts from the merged 0x153C). If the fallback is needed, D2 owns the capture-logic block for that change and D1 rebases |
| D3 **Integration, build, contract** (Opus) | as C4, version 0x3D | - |
| D4 **Firmware logger** (Opus): LOG_*, the log loop, the DDR ring, acq_state 5, USB framing | `pc_hal_scope.c`, `pc_cmds.c`, `pc_usbw.c`, `pc_transport.c`, `fnirsi_1013d.ld`, host tests | D5 shares `pc_cmds.c`: D4 first |
| D5 **Firmware generator rewrite + pattern + pattern trigger** (Opus): the generator functions on 0x6C / 0x6D with the P16 / H16 formulas of 7.4 (menu, wizard, boot 1 kHz, Bode engine on P = 4 M, SET_GEN raw modes, GEN response), SET_PATTERN / SET_PATTERN_TRIGGER, the SET_TRIGGER2 writer with the per-engine code table, the shared-fallback refusal (0x42 bit 7), trigger source 3 in SET_TRIGGER / SET_TRIGGER2, generator state restore; `fpga_model.c` models the pattern's registers and the pending bit; `test_bode.py` / vectors on P | `fpga_control.c`, `generator.c`, `pc_cmds.c`, `bode_dsp.h/.c`, `bode_sweep.c`, `cal_wiz_ui.c` (call sites only), host tests, `docs/bode-plan-vectors.json` | D4; shares C5's SET_TRIGGER2 writer (D5 rebases on it) |
| D6 **MCP 1.27** (Opus): `scope_log_*`, `scope_pattern`, `scope_sweep` (stepped SET_GEN), `bode.py` on P = 4 M with the finer grid (plan vectors regenerated with the C side), `scope_generator` unchanged on SET_GEN, report rendering, simulator (pattern core with the fraction accumulator, double buffer, pending) | mcp as C8 | - |

### 0x153E

| Item | Touches | Same block as |
|---|---|---|
| E1 **Both-core trigger + runt / window + range RTL** (Opus, one agent: all three edit the compare and `trig_qual`): 0x5E bits 4, 5, types 4, 5, 0x65, 0x37 bits; `tb_trig2.v`; timing report per item with the cut order of 1.2 | `zaklad.v`: trigger compare 1236-1282, `trig_qual`, decoder, read mux; step 19 | - |
| E2 **Integration, build, contract** (Opus), version 0x3E | as C4 | - |
| E3 **Firmware + MCP 1.28** (Opus): the reserved SET_TRIGGER2 fields, the half-sample offset, simulator | `pc_cmds.c`, `pc_hal_scope.c`, mcp | - |

The two device rows (section 11) are **not** in the briefs above: they get their own mockup round
(`docs/plans/ux-makeover.md` process, `docs/ui-guidelines.md`) after this contract lands, then one firmware item each
(the Type row drives C5's SET_TRIGGER2 path with the subset of 3.5; the Sample row drives C6's mode 4).

## 11. UI scope (decided)

Owner decision, 2026-10-20 22:13 (coordinator). The device UI gets exactly two rows; nothing else gets a control.

| Device control | What it drives | Interface cost it relies on (checked above) |
|---|---|---|
| **Type** row in the trigger menu: Edge, Pulse width, Nth edge, with their parameters | SET_TRIGGER2's subset: 0x5E, 0x5F, 0x60, 0x16 | 1 + 3 (+ 3) bytes per arm, latched like 0x15-0x17 with no 0x01 window (3.5); parameters in the menu's units with one multiply (time to words); back to Edge is 0x5E = 0 |
| **Sample** row in ACQ: Normal, Peak | 0x56 bit 1 | one register; forces the 16-bit path itself (section 4); the trigger level's 0x55 write is the existing one with a shifted value |
| Measurement cards (Freq, Period, +Width, +Duty, Vmin, Vmax, Vpp) fed by the counter, no new control | read 0x35 | one 16-byte read per screen pass, about 5 us (section 5); the counter runs always |

MCP-only (no device control): runt, window and timeout triggers, hold-off, the streaming logger, the pulse pattern
generator with its marker trigger and its own trigger engine, chirps, both-core triggering (always on where it
applies), and all host-side software of section 8. The generator menu keeps its rows (frequency, duty, on / off): the
pattern behind it is invisible there. Screen and menu design are out of this document.

### 11.1 The former proposal (for the record)

This document's draft of 22:05 proposed the same split (Type row for pulse width and Nth edge, a peak state in ACQ,
counter-fed cards, the rest MCP-only), with the reasoning that those three are used while holding a probe and the
others are configured once from a script; the owner's decision differs only in placing Peak in its own Sample row
(Normal / Peak) instead of the Filter box, and in dropping timeout from the device row. Peak as a separate row is
consistent with the FPGA side: peak is a one-register switch independent of the filter registers (section 4).

## 12. Questions only the owner can answer

1. 0x1B as the trigger hold-off: accept the meaning change (the register was dead) or allocate a new code and keep 0x1B
   dead.
2. 0x153E: at the measured 85 % paper estimate it fits with about 1,300 LUT of margin; build it after 0x153D's measured
   count, or stop at 0x153D.
3. When the one-bitstream cleanup (C9) and the cruft removal (C0) land relative to the 0.3.0 tag (C0 changes no
   behaviour the firmware uses, but it is a new bitstream and a flash).
4. The Sample row with Peak and the Filter box both change the record format; whether Peak greys the Filter box or the
   two are shown as one state is for the mockup round (not a contract question, listed so it is not lost).
5. The pattern table depth: 8 steps in this contract (about 50 reg per entry); 16 if a detector TX pattern needs it.

Closed by the owner's Q&A of 22:38-23:04: the logger rates are the documented spec (6.2); the trigger-out pin is closed
by the no-soldering rule (7.1); the pattern trigger engine is in (7.5); "pattern starts on the capture trigger" is
dropped; the DDS and the FPGA sweep are removed, the generator is the pattern (7.2), sweeps are MCU-stepped (7.3), the
durations carry a 16-bit fraction with one accumulator per engine (7.2).

## 13. Cruft audit (one-bitstream rule), measured

Method (fpga-spec, 2026-10-20 23:05-23:25): the 0x153B `src/` (c173ed5) copied to scratch, one block removed per copy with a
script (`mkvar.py`, exact-string edits), each copy built headless with TD 4.6.8 SP1 through `build.sh` in its own WSL
work dir (the flow of `fpga/al3/AGENTS.md` "Build"); the unchanged copy reproduces the 0x153B report, 4990 LUT / 4405 reg
/ 5658 LE, `clk_200MHz` 4.583 ns 0 errors. No RTL is committed; the numbers are from `FPGA_phy.area`. "Cruft" = logic
our firmware after the fw-one cleanup never writes or reads; the firmware's FPGA accesses were listed from
`fpga_control.c`, `pc_hal_scope.c`, `fpga_bus.c`, `pc_seq_hw.c`, `cap_stage.c`, `enc_heal_hw.c`, `bode_sweep.c`,
`generator.c` (`fpga_write_cmd(0x..)` sites) [F].

| Block (`zaklad.v`) | Why it is cruft (firmware evidence) | LUT | reg | `clk_200MHz` of the build | Decision |
|---|---|---|---|---|---|
| DDS `freq_generator_dds_pwm`, `u_freq`, 0x51 / 0x52 registers and decoder (121-135, 571-572, 704-710, 2046-2074) | replaced by the pattern generator (owner 23:00) | **-61** | **-97** | 5.155 ns, 4 errors (lottery, see below) | remove in 0x153D (D2) |
| `hold_on_divider`, 0x1B (563, 1953-1966) | stored, never used (`protocol.md` caps bit 2 = 0); no firmware write of 0x1B. TD had already pruned its 32 unloaded flops: the saving is the decoder | -11 | 0 | 4.875, 0 | re-purposed as the hold-off (0x153C, C1) |
| `touch_panel_address`, write 0x3C / read 0x41 (556, 2031-2040, 2351-2360) | the only writer is commented out (`fpga_control.c` 1577-1585 `fpga_set_battery_level`), no 0x41 read anywhere | -23 | -16 | 5.671, 24 (lottery) | remove (C0) |
| live ADC reads 0x24-0x27 (2251-2265) | no firmware reader; liveness-only since 0x153A | -27 | 0 | 4.873, 0 | remove (C0) |
| `dec_neg_a` / `dec_neg_b`, 0x53 bits 3 / 4 (626-627, 2081-2082, 0x29 bits 4 / 3) | no effect since 0x153A; the firmware still writes them from a settings word (`pc_hal_scope.c` 1989): fw-one drops that | -7 | -2 | 4.661, 0 | remove, 0x29 bits read 0 (C0) |
| `dec_swap`, 0x53 bit 2, the `swap` port of both `decim_chan` (625, 174-175, 2080) | `PC_INTERLEAVE_ORDER` = 0 (`pc_cmds.h` 154): the bit is never set; since 0x1538 the order is guaranteed B-first | -31 | -1 | 5.362, 2 (lottery) | remove, 0x29 bit 2 reads 0 (C0) |
| `sample_read_enable`, 0x0F (588, 1930-1932, 1787) | both arms of its mux are `i_mcu_clk`; the firmware writes 0x0F = 0 in every arm (6 sites, `protocol.md` ARM step 2 "dead on Atlan4"): fw-one drops the write (2 strobes per arm) | -2 | 0 | 5.099, 1 (lottery) | remove (C0) |
| `w_last` (1351-1355) | testbench only (tb_timeout) | -6 | 0 | 4.962, 0 | keep under `` `ifdef SIM`` (C0) |
| `trigger_detect_enable`, `trigger_rise_enable`, `trigger_fall_enable` (599, 1246-1247, 1262) | nothing consumes them (fpga-gen12 invariant) | -2 | 0 | 4.914, 0 | remove (C0) |
| 32-bit divider compare narrowed to 20 bits (`sample_rate_counter`, `sample_clock_enable`, `sce_q`, 1040-1059; the 0x0D register keeps 4 bytes) | the firmware's largest divider is 199,999 (200 ms/div at 1 kSa/s; raw dividers to 19,999 on the bench), the CIC's 4,095: bits 31..20 are never set | **-53** | -24 | **4.925, 0**; the worst path is `enc_nxt_n` (the lottery path of 0x1532+) with the chain 12 bits shorter | do it (C0); the firmware rejects dividers above 2^20 - 1 (E_BAD_ARG) |
| **All of the above except the DDS and div20, together** | | **-78** | **-19** | 4.993, 0 | the C0 scope (plus div20: about -130 / -43 [I: not built together]) |
| all cruft and the DDS together | | **-138** | **-116** | 5.048, 1 (lottery) | the 0x153D state before its additions |

Not cruft, kept with the reason: 0x05 reset-state delay (polled in every arm), 0x0E time-out (auto mode), 0x0A's
inversion, the 13-bit 0x14, 0x18 (trusted since 0x153B), 0x28 `ext_activity` (GET_STATUS `ext_flags`), 0x38 backlight
PWM (written at boot), 0x32 / 0x35 offsets, the relay guards, the legacy K = 0 path (200 MSa/s and the gated class), the
sequencer's legacy lanes (UI averaging of legacy records), `seq_raw` (READ_SEQ S = 0xFF), both 0x17 and 0x55. Stock-only
commands the firmware still writes without any FPGA logic behind them: 0x04 (`fpga_enable_system`), 0x0F: fw-one drops
them (no bitstream change).

Prototype measurements for the pattern generator (`mkvar2.py`, the DDS-less design plus a two-step continuous pattern
with u24 + u16 durations, one fraction accumulator, double-buffered entries, 0x71 / 0x72 / 0x73 as scratch registers):

| Variant | LUT | reg | against `nodds` | `clk_200MHz` |
|---|---|---|---|---|
| `pat2` (fraction 16 bits, double buffer) | 4977 | 4539 | **+48 / +231** | 5.881 ns, 44 errors: the one-cycle fraction add into the count (7.2 prescribes the pipelined form) |
| `pat2nf` (no fraction, double buffer) | 4942 | 4459 | +13 / +151 | 5.063 ns, 1 error |
| the fraction alone | | | **+35 / +80** | |

The 8-entry table with u32 integers, bursts, gap, R and the marker are not in the prototype: about +200 LUT / +420 reg
[E: 6 more entries x 51 flops, shadows for entries 0 / 1 are in the prototype already, 8:1 mux of 51 bits about 100 LUT,
the burst / gap counters about 60 LUT].

**The timing lottery, measured**: every variant re-rolled `clk_200MHz` between 4.58 and 5.67 ns with the same known
endpoints (`acc_ram` address, `pp_i` ce, `enc_nxt_n`) whatever was removed; the base build's 0 errors at 4.583 ns is one
roll. The removals are not the cause and the 0 / 1 / 24 error counts above say nothing about them. Two consequences for
the briefs: a build's timing summary is read for every candidate bitstream and re-rolled with a neutral edit when it
misses (the 0x153B practice), and div20 shortens the lottery path for free.
