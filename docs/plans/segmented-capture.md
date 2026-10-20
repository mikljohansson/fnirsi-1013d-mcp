# Segmented capture and hardware averaging (FPGA 0x1536)

Roadmap section 8 item 1. Maintainer case: a pulse-induction metal detector. Each TX pulse is one trigger, and the receive
decay after it (tens to hundreds of microseconds) is the signal. Dead time between pulses wastes memory today, and
coherent averaging of many decays at full rate is what buys sensitivity. Community case: multi-event capture with
per-event timestamps, and noise reduction by averaging.

Status: built and verified in simulation as designed (fpga-gen5, 2026-10-19), not flashed. Section 9 has the results.
Firmware support: fw 0.2.58 / protocol 1.17 (fw-seq, both numbers provisional), built and host-tested, not benched.
What was built differs from section 7 in details; the spec is `docs/protocol.md` section 14 row 56 (SET_SEQ 16 bytes
without S, which READ_SEQ takes per read; GET_SEQ 48 bytes; READ_SEQ with its own 16-byte header; GET_CAPTURE_INFO 96
bytes with a different field order; STOP ends a run as DONE).
Facts about the existing design come from `fpga/al3/README.md` sections 3 and 5 and `fpga/al3/AGENTS.md`. Statements
marked "inference" are not measured.

## 1. Terms

- **Word**: one 32-bit memory word, written on one write-clock edge that advances the capture logic (`swc_adv`).
  - Legacy (0x53 K = 0): one word holds one A/B sample pair per channel, so 2 samples per channel.
  - 16-bit (K >= 1, with or without the FIR): one word holds one filtered 16-bit sample per channel.
- **Record**: the window a normal capture returns: `total` words (0x0C) starting at `trigger_address - pretrigger`
  (0x14, 0x0B), modulo the ring.
- **Engine**: the existing 0x1535 capture logic (write address, pretrigger fill, trigger, post-trigger count, stop).
- **Restart**: the internal equivalent of the MCU's sampling reset (0x01 = 1 then 0). It resets the engine registers
  on one write-clock edge.

Every pretrigger, total, time-out and trigger-address value keeps counting words, exactly as in 0x1534/0x1535. In
other words, "sample" means one word.

## 2. Architecture

A sequencer in the `clk_200MHz` domain restarts the unchanged engine after each record. In segment mode it remaps
the engine's write address into a slot of the sample memory and logs a table entry per segment. In averaging mode,
after each record, it adds that record into an accumulator RAM.

```
            pins / decimator ──> dia ──┬──> sample memory 8192 x 32 (unchanged, MCU port unchanged)
                                       └──> record mirror 2048 x 32 (2 x EMB32K, write side = main write side)
 engine (swc domain) ──> stop ──> sequencer (clk_200) ──> post-pass: mirror ──(+)──> accumulator 2048 x 64 (16 x EMB9K)
        ^                                  │                                             │
        └──────── restart / slot base ─────┘           MCU reads 0x2B..0x32 <───────────┘ (also the segment table)
```

- **Why a mirror.** Averaging must be trigger-aligned, and the pretrigger part of a record is written before the
  trigger is known. The sample memory's only read port belongs to the MCU, on `i_mcu_clk`. Giving it a second user
  would mean re-clocking that port, which changes the proven readout path.
  - The mirror is a second write-only copy of the last 2048 words. Its write port is the sample memory's own
    (same `dia`, same `cea`, address bits 10..0). Its read port is free for the sequencer.
  - The cost is both EMB32K blocks.
- **Post-pass.** When a triggered record ends, the sequencer reads it from the mirror at one word per 5 ns and adds
  it into the accumulator. During that time the engine stays stopped.
  - The read-add-write pipeline is 7 stages on `clk_200MHz`, with 16-bit adders only.
  - 2048 words take 10.3 us.
- **Restart.** The sequencer raises a request (one flop, a half-cycle path into the write-clock domain). The
  write-clock domain turns it into a one-edge pulse. That pulse resets exactly the registers that the MCU sampling
  reset resets, in both domains. The decimator, FIR, sample-rate divider and generator are never reset (as today).
- **Modes off (0x57 = 0, the power-on value).** The sequencer stays idle and the restart pulse never fires. The
  address remap mask is all ones, so the write address passes through unchanged, and 0x0A reads as before. The
  design is then cycle-identical to 0x1535; the testbench proves it against 0x1535 from git.

## 3. Segment mode (0x57 mode 1)

- **Slots.** The sample memory is split into `nseg` slots of `L = 2^m` words (0x59: m = 5..12; 0x58: nseg = 1 to
  8192 / L). The engine runs on its usual 13-bit virtual address. The physical address is
  `slot_base | (virtual & (L - 1))`, so each segment is a ring of L words.
- **Per segment.** Each segment is a complete normal capture: the pretrigger fill (0x0B), the trigger (CH1/CH2 level,
  EXT, or the auto time-out 0x0E), then the post-trigger words (0x0C total).
  - Rule: `total <= L - 2` and `pre < total`. The hardware does not check them.
- **After each segment:**
  1. A table entry is written.
  2. The sequencer waits the hold-off (0x5C, 5 ns units, 32 bit).
  3. The engine is restarted at the next slot.
  After `nseg` segments the run is done, and 0x0A reads 1.
- **Circular option** (0x57 bit 2). The slot index wraps, so the run keeps going until stopped (0x5D). The status
  reports overflow (at least one wrap), and the count keeps growing. The entry of the slot about to be refilled is
  invalidated at its restart, so after a stop every valid entry describes complete data.
- **Re-arm (dead) time** from the last word of a segment to the next accepted trigger:
  - 3 cycles to write the table entry;
  - plus the hold-off;
  - plus about 2 write clocks for the restart;
  - plus `pre + 1` words of pretrigger fill.
  At 200 MSa/s with pre = 32 words, that is about 0.4 us plus the hold-off. Section 9 holds the measured value.

| L (words) | m | segments | legacy samples / channel / segment | 16-bit samples / channel / segment |
|---|---|---|---|---|
| 32 | 5 | 256 | 64 | 32 |
| 64 | 6 | 128 | 128 | 64 |
| 128 | 7 | 64 | 256 | 128 |
| 256 | 8 | 32 | 512 | 256 |
| 512 | 9 | 16 | 1024 | 512 |
| 1024 | 10 | 8 | 2048 | 1024 |
| 2048 | 11 | 4 | 4096 | 2048 |
| 4096 | 12 | 2 | 8192 | 4096 |

The brief's 128 to 2048 samples is m = 6..10 in legacy and m = 7..11 in 16-bit mode. Fewer, shorter segments are
fine (nseg below the maximum).

**Segment table** (accumulator RAM word s, read as 8 bytes, little-endian):

| Bits | Field |
|---|---|
| 39..0 | timestamp: 5 ns units from the first write-clock edge after the release of 0x01 to the edge that latched trigger_address (40 bit = 5,497 s) |
| 52..40 | trigger offset = trigger_address mod L: the last word written before the trigger, as 0x14 |
| 53 | triggered (0: the segment ended on the auto time-out; then the offset is pre mod L, as 0x14 after a time-out) |
| 62..54 | 0 |
| 63 | valid |

- The timestamp counts write-clock edges times 2 (D + 1), where D is the effective divider (0 when K >= 1). It is
  therefore exact in write-clock units: 10 ns at 200 MSa/s and in every 16-bit mode, 2 (D + 1) x 5 ns in legacy.
- With K >= 1, the trigger can only be latched on an output edge. Timestamp differences are exact multiples of the
  write-clock period. A timestamp sits on the edge after the trigger word (inference; the testbench measures the
  offset).
- Segment s data are at physical words `s L + ((offset - pre + j) mod L)`, for j = 0 .. total - 1.

## 4. Averaging mode (0x57 mode 2)

- **Run structure.** A run accumulates N records (0x5A, 1..65535, 0 = 65536) at each of P positions (0x58, P >= 1).
  Triggered record k goes to position k mod P. The run ends after N x P triggered records.
  - P = 1 is plain averaging.
  - P = 2 averages alternate pulses separately (bipolar TX).
  - P > 1 relies on every trigger being captured in order. Use normal mode.
- **Untriggered records** (auto time-out) are not accumulated and do not advance the position. They are counted
  (status bytes 4..5).
- **Accumulator** (2048 x 64, position p at words p x total .. p x total + total - 1; rule P x total <= 2048,
  total <= 2046):

| Format (latched at arm) | Lanes per word | Lane width | Largest N without saturation | Saturation |
|---|---|---|---|---|
| legacy (K = 0) | CH1A, CH1B, CH2A, CH2B | 16 bit | 257 (255 x 257 = 65,535) | each lane clamps at 65,535 |
| 16-bit (K >= 1) | CH1, CH2 | 32 bit | 65,536 (65,535 x 65,536 < 2^32) | cannot happen |

  - The 16-bit lanes take the decimated samples unshifted. A shift on the way in would lose the low bits that
    averaging is meant to recover, and 32 bits hold the whole range of N.
  - The first record of each position is written, not added, so no clear pass is needed.
- **Records.** The record is the normal record (pretrigger included), so 2,048 samples per channel in 16-bit mode and
  4,096 per channel in legacy mode. The sample memory still holds the last raw record after the run, as a normal
  capture, so the firmware can show one shot next to the average.
- **Readout** (0x2B..0x32, through the 0x1F read pointer, bits 10..0). Each 64-bit view word holds four 16-bit
  lanes, L0..L3 = bytes 0-1, 2-3, 4-5, 6-7:
  - legacy: Li = accumulator lane i >> S (CH1A, CH1B, CH2A, CH2B);
  - 16-bit: L0 = min(65535, CH1 sum >> S), L2 = min(65535, CH2 sum >> S), L1 = L3 = 0;
  - raw (0x57 bit 3): the accumulator word itself (16-bit format: CH1 sum in bytes 0..3, CH2 sum in bytes 4..7);
  - S comes from 0x5B. S = log2 N gives the mean. Smaller S keeps more bits.
- **Dead time per record**: the post-pass (total x 5 ns + 40 ns), the hold-off, the restart (2 write clocks), and
  the pretrigger fill.
  - Example, 16-bit CIC at N = 10 (20 MSa/s), total = 1000 (50 us), pre = 50: 5 + 0.02 + 2.5 us = about 7.5 us.
  - At a 1 kHz pulse rate, 256 averages take 0.26 s and reduce white noise by 16 (24 dB). No pulse is lost unless
    the pulse period is shorter than the record plus the dead time.

## 5. Register map (0x1536)

Writes (free codes 0x57 and up). Write them while 0x01 = 1 (in the arm sequence); they are latched at the release.

| Cmd | Bytes | Meaning |
|---|---|---|
| 0x57 | 1 | bits 1..0 mode: 0 off (power-on), 1 segments, 2 averaging, 3 reserved (acts as off). bit 2 circular segments. bit 3 raw averaging readout. bits 7..4 write 0 |
| 0x58 | 2 | count: segments (1..8192 / L) or positions (1..2048 / total); 12 bits used |
| 0x59 | 1 | bits 3..0 m: slot L = 2^m words (5..12) |
| 0x5A | 2 | N: records per position (1..65535, 0 = 65536) |
| 0x5B | 1 | bits 4..0 S: readout shift |
| 0x5C | 4 | hold-off in 5 ns units (0 = none) |
| 0x5D | 1 | bit 0 stop: no further restarts. An untriggered record in progress is abandoned; a triggered one completes. Cleared by 0x01 = 1 |

Reads:

| Cmd | Bytes | Meaning |
|---|---|---|
| 0x06 | 2 | 0x15 0x36 |
| 0x0A | 1 | bit 0: with a run armed in mode 1 or 2, the run is done; otherwise unchanged (~sample_write_enable) |
| 0x2A | 8 | status. byte 0 = {bit 7 done, bit 6 overflow, bit 5 running, bit 4 16-bit format, bit 3 raw, bit 2 circular, bits 1..0 mode of the run (0 = no run since the last 0x01)}; bytes 1..3 count (24 bit: segments completed, or records accumulated); bytes 4..5 untriggered (16 bit, saturating); bytes 6..7 {bits 15..13 sequencer state, bits 11..0 next slot or position} |
| 0x2B..0x32 | 1 each | byte 0..7 of the view word at the read pointer; each read advances the pointer by one word, exactly like 0x20..0x23, with the same one-strobe pipeline (READ_RAW's dummy read applies) |

- Progress while running: poll 0x2A. The count can change between its bytes, so read it twice and compare.
- 0x14 still reads the current or last record's trigger_address.
- 0x18 still reads the engine's triggered flag.

## 6. Resources and timing (estimate; measured numbers in section 9)

| Item | 0x1535 | 0x1536 estimate |
|---|---|---|
| BRAM9K | 32 of 48 | 48 of 48 (accumulator 2048 x 64 as 16 x 2K x 4) |
| BRAM32K | 0 of 2 | 2 of 2 (mirror 2048 x 32) |
| LUT / reg | 3082 / 2716 | about +900 / +800: sequencer, post-pass pipeline, 40-bit timestamp, readout shifters, status |
| M18x18 | 2 of 3 | 2 of 3 |

Timing risks:
- **`clk_200MHz` post-pass.** The BRAM outputs are registered (OUTREG plus a fabric register), and the adds are
  16-bit. The 32-bit lanes add the upper half in the next stage. The risk is the counters: hold-off (32 bit),
  count (24 bit) and the timestamp snapshot compare. They are as wide as the existing 32-bit sample-rate counter,
  which meets 200 MHz.
- **Half-cycle crossings `clk_200MHz` to write clock:** only two new flops, the restart request and the slot wrap
  flag.
- **Write clock to `clk_200MHz`:** these crossings are sampled only on the clk_200 edge 7.5 ns after a write-clock
  rise, through a qualifying enable. The existing multicycle-2 constraint covers them and the capture is never
  metastable.
- **Write-clock domain:** a 40-bit timestamp adder at 100 MHz, and one LUT level of address remap in front of the
  sample memory address. The 0x1535 decimator had +3.2 ns there.
- **The existing critical path** (write clock into `sampling_triggered`, +0.3 ns) gains a restart term on its
  reset. That term is an OR of the MCU reset and one registered pulse, not on the data input.
- **Conflict.** Roadmap section 3 (deeper memory) wanted the free EMB9K and EMB32K blocks. 0x1536 uses all of them.
  A later deeper memory would have to drop the mirror (re-clock the MCU read port to `clk_200MHz` and read the
  history from the sample memory) or halve the accumulator.

## 7. Firmware and MCP interface (proposal)

Capability bit 17 SEQ (0x20000) on 0x1536..0x153F.

- **SET_SEQ 0x020E** (REMOTE). Request 16 bytes:
  - u8 mode (0 off, 1 segments, 2 average);
  - u8 flags (bit 0 circular, bit 1 raw);
  - u8 m;
  - u8 S;
  - u16 count (segments or positions);
  - u16 N (0 = 65536);
  - u32 hold-off in ns (the firmware converts it to 5 ns units);
  - u32 0.
  - Validation: segments need m 5..12, count 1..8192 >> m, and the trigger total <= 2^m - 2. Averaging needs
    count x total <= 2048 and total <= 2046. The capture memory limits follow (GET_TRIGGER). E_BAD_ARG details per
    field.
  - ARM then writes 0x57..0x5C inside the reset, after 0x0B/0x0C/0x0E.
- **GET_SEQ 0x020F.** The setting, plus the 0x2A status as read now.
- **WAIT / DONE.** DONE is 0x0A = 1, as today. STOP and FORCE write 0x5D = 1 first. FORCE also writes 0x1A = 0, so
  a waiting record ends.
- **GET_CAPTURE_INFO** grows to 96 bytes. 76 u8 seq mode, 77 u8 seq flags, 78 u8 m, 79 u8 S, 80 u32 count, 84 u16
  untriggered, 86 u16 positions or segments, 88 u32 N, 92 u32 hold-off in ns.
- **READ_RAW addr_mode 3**: segment-relative. The address is `segment << 16 | offset`, with the offset relative to
  that segment's record start `(entry offset - pre) mod L`. The firmware maps it to `s L + ((start + j) mod L)` and
  splits at the slot end. Layouts 0, 1 and 2 are unchanged.
- **READ_SEQ 0x0210.** Request u8 what (0 segment table, 1 averaged record), u8 lanes (bit mask L0..L3), u16 first,
  u16 count.
  - The response is a 16-byte header plus data: 8-byte table entries, or u16 lane samples per word (lane-major
    blocks, little-endian).
  - The firmware reads by setting 0x1F, doing one dummy strobe, then one pass per byte (0x2B + 2 i and 0x2C + 2 i
    for lane i), the same pattern as `pc_hal` READ_RAW.
  - In raw mode it returns u32 per channel.
- **MCP**:
  - `scope_capture_segments(count, length, pre, holdoff_us, circular)` returns a list of segments: timestamp in s,
    trigger offset, triggered flag, and data per channel;
  - `scope_capture_averaged(n, positions, length, pre, shift)` returns float means per channel, with the 16-bit or
    legacy conversion (code = lane x 2^S / N, then the usual decimation gain and calibration);
  - both reuse the existing channel, trigger and decimation settings.
- **Conversions:**
  - legacy average code = Li x 2^S / N;
  - 16-bit average = L x 2^S / N, then the 0x1534/0x1535 rule (`x 2^S_dec / gain` from GET_DECIM, plus corr16 / 16);
  - segment timestamp in seconds = ts x 5e-9.

## 8. Verification plan

`tb/tb_seg.v` against 0x1535 from git (`REF1535_REV` = e2aec2e), with the step selected by `ONLY=seg` in `tb/run.sh`:

1. Modes off: random captures, K = 0..3, FIR eligible or not, EXT, random writes of 0x57 (mode 0 or 3) and 0x58..0x5D.
   Pins, capture registers, decimator, every bus read (0x0A included) and the whole memory must be identical.
2. Segments: slot remap and slot contents against a model of the write stream, table offsets against the EXT edge
   write address, timestamps against the injected edge times, untriggered segments, hold-off and re-arm time,
   circular overflow and invalidation, stop, K = 1..3 with the FIR.
3. Averaging: N = 1 (mirror equals the sample memory), random records for every format, P = 3 positions, legacy
   saturation (N = 300 at 255), readout shift and the 16-bit clamp, raw readout, skipped untriggered records, two
   runs in a row (first-record write), trigger position in the averaged record, FIR + EXT. `tb/seg_model.py`
   recomputes every accumulator word and every table entry from the logged records and write stream.
4. Modes off again: random equivalence.

Also: mutation runs of the new logic, and the existing four steps unchanged (`tb_fir.v` against 0x1534 still
passes, because it does not touch the new commands).

## 9. Results

The full tables are in `fpga/al3/README.md` "Changes" section 6.

**Bitstream.** `out/FPGA-seg.bin`, sha256 `4eaf6811...b48b`, pinned in `backups/` with the padded image
`66866c84...7408`. The build is deterministic.

**Resources and timing:**

| Item | Value |
|---|---|
| LUT / reg | 4445 / 3817 (+1363 / +1101) |
| Block RAM | all 48 EMB9K and 2 EMB32K |
| `clk_200MHz` | +0.282 ns (an existing path); worst new path +0.38 ns |
| `sample_write_clock` | +0.48 ns |

The first build missed 200 MHz by 1.1 ns because sequencer decisions were computed combinationally. Precomputing
every increment and compare into registers fixed it.

**Testbench.** `tb/run.sh` step 6: 50 checks, and `seg_model.py` checks 22 runs, 0 failures. The equivalence with
0x1535 holds when the modes are off. 20 of 21 mutants are caught; the 21st is equivalent.

**Measured behaviour:**
- 2 words are written after a record window, so `total <= L - 2`.
- Re-arm time is about hold-off + pre x word + 1 to 3 write clocks + 50 ns. Averaging adds the post-pass.
- Timestamps lie 22 to 32 ns after an EXT edge at 200 MSa/s, and up to one word later at slower rates.
