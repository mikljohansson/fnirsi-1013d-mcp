# Phosphor fast capture with segmented capture (FPGA 0x1536): design note

fw-seq, 2026-10-19, for a later batch. Not built. The phosphor view today adds one record per main-loop pass
(`docs/plans/phosphor-design.md`, `firmware/AGENTS.md` section 24: "one record per pass (no fast loop)"; design steps
P3-P5 open). This note shows how the 0x1536 sequencer (`fpga/al3/README.md` section 6, fw support in `docs/protocol.md`
row 56) can put many records into one pass, with numbers.

Marks: [F] fact from source, a measurement or the testbench; [E] estimate from stated figures; [I] inference.

## 1. Where the 30 wfm/s comes from

- [F] The phosphor view adds one record per UI pass; the bench target is 25-33 wfm/s at a 30 ms pass (section 24,
  step 2). The pass is 1.00o5's main loop: arm, wait for the trigger, read the record, process it, redraw the screen.
- [F] Each UI record is the whole memory depth, 3000 samples minimum. At 1.00o5's readout cost of about 2.6-3.1 us per
  sample (protocol.md READ_TRACE note) that alone is 8-9 ms per channel; the raw bus is about 0.3 us per byte
  (root AGENTS.md, protocol.md READ_RAW cost).
- [E] Rasterising one record into the map: about 0.85 ms per channel for a flat trace, 22 ms worst case, with
  uncached DDR at about 90 cycles per access (phosphor-design.md section 3). [I] Since fw 0.2.48 DDR is write-back
  cached (section 26), so the map's column runs are cache hits after the first touch; the per-record cost is likely
  several times lower. The bench must measure it (`pass_ms`).
- So the screen redraw and the per-pass fixed work, not the capture, set the rate: the FPGA could capture thousands
  of records in the time one pass takes at fast time bases.

## 2. What the sequencer gives

- [F] Segment mode: the sample memory (8192 words) splits into `8192 >> m` slots of 2^m words (m = 5..12); each slot
  is a complete triggered capture with its own pretrigger, then a table entry (timestamp, trigger offset, triggered
  flag), then the next arm after the hold-off. Rule: record <= 2^m - 2 words.
- [F] Re-arm time (testbench, `tb_seg.v`): hold-off + pre x word + 1 to 3 write clocks + 50 ns, e.g. 130 ns at
  200 MSa/s with pre 6, 0.56 us at 40 ns words with pre 10. Blind time per record is therefore the pretrigger fill.
- [F] Reading a segment uses the existing 0x1F / 0x20..0x23 path: one bus pass per core byte, about 0.3 us per byte.
  The table is 8 bytes per segment (8 passes of `count` entries, so negligible).
- [F] Legacy records: one word = one A/B pair per channel = 2 samples, 2 bytes per channel. 16-bit records: one word =
  one sample per channel, 2 bytes per channel. [I] For the map, 8 bits per sample are enough (400 rows); in 16-bit
  mode the high byte alone (0x21 / 0x23) is the sample to within 1/256 of full scale because S puts full scale into
  the top bit of 16 (pc_decim.h), so a phosphor readout can read 1 byte per sample there too.

## 3. Only the visible window

The phosphor map shows 728 columns. The UI's record (3000+ samples) is longer than the screen at most time bases
(the slider scrolls through it). A segment only needs the samples that land on the screen: V samples from
`disp_first_sample` with `disp_xpos_per_sample` (scope_display_trace_data), plus the trigger point. [I] The segment's
pretrigger and total are set from that window so that the trigger sits at the same screen x as in a full record:
pre = settriggerpoint - first visible sample, total = V (+ a few samples margin), in words. That keeps the map's
column mapping (`phos_xkey`) unchanged.

| visible samples V (legacy) | words | m (slot) | segments per run | bytes per segment, 1 channel | read time per segment, 1 / 2 channels |
|---|---|---|---|---|---|
| 290 (zoomed, 2.5 px per sample) | 145 | 8 (256) | 32 | 290 | 0.09 / 0.17 ms |
| 725 (1 px per sample) | 363 | 9 (512) | 16 | 726 | 0.22 / 0.44 ms |
| 1450 (2 samples per px) | 725 | 10 (1024) | 8 | 1450 | 0.44 / 0.87 ms |
| 2900 (4 samples per px) | 1450 | 11 (2048) | 4 | 2900 | 0.87 / 1.74 ms |
| > 8190 | > 4094 | - | 1 | - | no gain: the record itself takes longer than a pass |

16-bit records: words = samples, so the same window needs twice the words and one m more (half the segments);
read cost the same as legacy if only the high byte is read [I].

## 4. What the 30 wfm/s pass becomes

Per pass: one segment run (arm, wait for `count` triggers or a deadline), the table, the visible window of every
valid triggered segment, one map add per segment, then the normal decay + render + redraw (unchanged, about 30 ms).
Extra time per pass = count x (read + rasterise). Rasterise per segment: 0.65 ms uncached [E, phosphor-design.md
column cost], about 0.15 ms with the D-cache [E, assumed 4x; bench].

| window | channels | segments | extra ms per pass (uncached / cached raster) | pass | wfm/s (now ~30) |
|---|---|---|---|---|---|
| 725 | 1 | 16 | 3.5 + 10.4 = 14 / 3.5 + 2.4 = 6 | 44 / 36 ms | about 360 / 440 |
| 725 | 2 | 16 | 7.0 + 20.8 = 28 / 7.0 + 4.8 = 12 | 58 / 42 ms | about 280 / 380 |
| 290 | 1 | 32 | 2.8 + 20.8 = 24 / 2.8 + 4.8 = 8 | 54 / 38 ms | about 590 / 840 |
| 2900 | 1 | 4 | 3.5 + 2.6 = 6 / 3.5 + 0.6 = 4 | 36 / 34 ms | about 110 / 120 |

[E] All rows assume the signal triggers at least `count` times per pass (for 16 segments in 30 ms: a trigger rate
above about 600 Hz). Slower signals fill fewer segments before the deadline; the view then gets what came (still
never fewer records than today). Capture time itself is small: 16 segments of 512 words at 200 MSa/s with pre 180
words take 16 x (3.6 + 1.9) us = 88 us plus the waits for triggers.

Upper bound [E]: the bus alone allows about 1 / (726 x 0.3 us) = 4,600 single-channel 725-sample records per second.
The pass structure (redraw every ~30 ms) keeps practical figures in the hundreds; a pipelined loop (section 5,
step 5) raises the ceiling towards the bus bound only if the redraw is also decoupled.

Choose `count` per pass from a budget (like `pc_seq_ui_plan`, which uses 40 ms for the averaging runs): count =
largest power of two with count x (read + raster estimate) <= 15 ms, capped by `8192 >> m`; measure and adapt with the
pass time.

## 5. Recommended implementation (later batch)

1. **Plan** (pure C, host-tested, next to `pc_seq_ui_plan`): from `disp_first_sample`, `disp_xpos_per_sample`,
   `settriggerpoint`, the filter's word layout and the channel count, compute pre / total words, m, count and the
   deadline. Fall back to today's single record when V > 4094 words or the bitstream has no SEQ.
2. **Arm**: `pc_seq_ui_conversion` already decides per UI conversion; add a phosphor branch (display mode 4) that
   arms segment mode with that plan through `pc_seq_hw_pre_reset` (writes 0x0B / 0x0C before the reset, which matters
   here: slot mask and trigger_read_address are write-clock registers, fpga-gen6). Auto mode: the FPGA's own 0x0E
   time-out would fill segments with untriggered records; use normal mode plus the deadline stop of the averaging
   runs (0x5D, 0x1A = 0), and show a single untriggered record when a run comes back empty (today's behaviour).
3. **Read**: at done, read the table (`count` x 8 bytes), then for each valid, triggered entry the window
   `s 2^m + ((offset - pre + j) mod 2^m)` with the dummy strobe (`pc_hal_read_core` style, split at the slot end), one
   byte per sample (legacy both cores; 16-bit high byte only). Untriggered entries are dropped like today's untriggered
   records (phosphor-design.md section 3).
4. **Add**: `phos_map` add per segment (no API change: it takes a sample array and the column mapping), rate
   normalisation with interval = pass time / segments (the increment formula of phosphor-design.md section 4 already
   handles any record rate). The wfm/s field counts segments.
5. **Trace for the rest of the UI**: feed the last segment's window through `fpga_read_adc_data` (as the averaging
   feed does) so measurements, cursors and the trigger refinement still see a record; mark the measurements as
   "last waveform" since they no longer cover the full depth [I: UX decision for the maintainer].
6. **Pipeline** (optional, after the bench): read all segments into RAM, re-arm at once, then rasterise while the
   FPGA captures the next run (the readout must finish before the re-arm, the sample memory is overwritten).

Bench checks for that batch: wfm/s against the tables above for a 1 kHz and a 100 kHz square at 1 and 2 channels;
`pass_ms` in phosphor vs scope view; a rare glitch (generator duty toggled every 0.25 s) visible within one
persistence time; no trace shift between single-record and segment pictures (the window geometry of section 3).

## 6. Risks and open points

- [I] The visible-window geometry must match the full record's to the sample, including the 1.00o5 `T + 7441`
  quirk of the legacy UI start; the segment path computes the start exactly (`(offset - pre) mod 2^m`), so a one-word
  difference between the two pictures is possible and must be checked on the bench.
- [I] Time bases where V > 4094 words get no gain; they are also where the record time limits the rate anyway.
- [F] 0x1536 uses every block RAM; a deeper-memory bitstream later would change the slot arithmetic
  (fpga/al3/AGENTS.md "Wanted next" item 2).
- Not decided: whether averaging (the second 0x1536 mode) has a phosphor use; [I] no, the phosphor shows the spread
  that averaging removes.
