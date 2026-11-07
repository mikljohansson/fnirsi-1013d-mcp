# Core order one sample off (fpga-coreorder, 2026-11-06)

Finding 1 of the 2026-11-06 16:21-16:45 MCP battery (`docs/hardware-status.md`): with Filter Off at a divided rate the
two ADC cores of a channel come out interleaved one sample off. This note verifies the claim from the saved data,
works out where the offset can and cannot come from, recommends a fix and lists the bench runs that must come first.
No hardware was touched; no bitstream was built. [M] = measured from saved data, [V] = read in the source, [I] =
inference.

**Superseded in part (fpga-153f, 2026-11-06).** The per-divider measurements of 2026-11-06 17:22 and 17:59
(`docs/hardware-status.md`) show that the order changes with the word period and the stage, so section 2.2 ("the ADC
alone decides it", a one-conversion latency difference at every rate) and the fix of section 3.1 (core B one register
earlier) do not hold. What fits every cell is both buses changing after the write clock's rising edge; the fix is
bitstream 0x153F, both cores of a chip captured at one instant: `fpga/al3/README.md` section 22. Sections 1 (the
verification), 2.4, 2.5 and 3.4 stand.

## 1. Verification

Conventions. A word k holds core A's sample A[k] and core B's sample B[k] (firmware reads both cores at the same ring
word, `firmware/scope/pc_cmds.c:1767-1786`, `fpga_bus.c:21-41`; the UI path the same, `fpga_control.c:779-791` A into
`tracebuffer[1::2]`, `:849-859` B into `tracebuffer[0::2]`). The host interleaves trace[2k] = B[k], trace[2k+1] = A[k]
(`mcp/src/fnirsi1013d/scope.py:279-285`, protocol 7.3). "Designed" = A[k] sampled T/2 after B[k] (T = one word);
"off by one" = A[k] sampled 1.5 T after B[k], so the smooth order is B[k+1], A[k], B[k+2], A[k+1].

Metrics (scripts kept in the session scratchpad, not in the repository):
- **mid**: RMS of B[k+s] - (A[k-1] + A[k]) / 2 after removing the mean (s = 0 designed, s = 1 off by one). Unlike total
  variation it is not biased by the A - B code offset (total variation on these records falls monotonically with s by
  about 165 per step and picks s = 2 on every record: a core offset artefact, so it is not used for the margins).
- **tau fit**: least squares B[k] = g A(k + tau) + o with A linearly interpolated; tau = -0.5 designed, -1.5 off by one.

### 1.1 Divided rates (gated encodes): confirmed, every core, every stage, every bitstream [M]

Raw per-core records at divider 99, CH2 on the RC ladder (16 records of 1500 words each), mid RMS in codes for
s = -1 / 0 / 1 / 2, and the winner per record:

| File | Image, CH2 stages | s = -1 | s = 0 (designed) | s = 1 (off by one) | s = 2 | wins s = 1 |
|---|---|---|---|---|---|---|
| ab_rel-1 | 0x153D, A3 B4 | 0.942 | 0.777 | **0.715** | 0.780 | 16 / 16 |
| ab_rel-2 | 0x153D, A3 B4 | 0.929 | 0.764 | **0.705** | 0.774 | 16 / 16 |
| ab_c153c | 0x153C, A3 B4 | 0.996 | 0.841 | **0.787** | 0.852 | 16 / 16 |
| ab_enc0 | enc0, A3 B3 | 0.940 | 0.776 | **0.715** | 0.781 | 16 / 16 |
| ab_0x153E | 0x153E, A3 B3 | 0.932 | 0.770 | **0.711** | 0.782 | 16 / 16 |
| ab_0x153E_warm | 0x153E, A3 B3 | 0.923 | 0.757 | **0.698** | 0.768 | 16 / 16 |
| ab_0x153E_warmcal | 0x153E, A3 B3 | 0.967 | 0.805 | **0.744** | 0.806 | 16 / 16 |
| ab_enc-250 | enc-250, A3 B3 | 1.075 | 0.941 | **0.900** | 0.963 | 16 / 16 |

The slope is only 0.32 codes per word, so the margins look small, but they are exactly what a one-word error costs on
that slope: sqrt(0.777^2 - 0.715^2) = 0.30 codes, and s = 0 and s = 2 are symmetric around s = 1 on every file. So
B[k+1] sits half way between A[k-1] and A[k]: A is 1.5 T after B within a word.

Host captures of 2026-11-06 (`b2_rates.json` and the later battery files under
`C:\Users\miklj\.fnirsi1013d\data\2026-11-06`), words where core A moves by 6 to 60 codes, mid RMS s = 0 / s = 1:

| Capture | Rate (divider) | Stages at the time | CH1 s0 / s1 | CH2 s0 / s1 |
|---|---|---|---|---|
| R1b c51775 | 20 MSa/s (9) | CH1 A3 B3, CH2 A3 B3 | 14.9 / **4.0** (3 words) | flat |
| R3b c51777 | 200 kSa/s (999) | same | 20.0 / **11.8** | 7.9 / **0.6** |
| R4b c51778 | 20 kSa/s (9999) | same | 43.3 / **16.2** | 29.3 / **2.6** |
| F0b c51779 | 1 MSa/s (199) | same | 30.8 / **15.7** | 0.57 / **0.12** (5 words) |
| c51854 .. c51885, 11 captures | 200 kSa/s (999) | CH1 A4 B3, CH2 A3 B4 (after the guard's moves) | s = 1 in 11 / 11 | 7.4..7.9 / **0.5..0.7** (11 / 11) |
| c51932 | 100 kSa/s (1999) | same | 41.3 / **17.2** | 11.9 / **1.1** |
| c601 .. c604, c672, c673, c4420 (capgate fw) | 20 MSa/s .. 20 kSa/s | same | s = 1 in 6 / 7 (c602: 3 steep words, tie) | s = 1 in 5 / 5 |

tau fits over whole records: R3b CH2 tau -1.5 (RMS 0.55 against 3.06 at -0.5), c603 CH2 -1.5 (0.55 / 3.05), R1b CH1
-1.5 (0.54 / 0.82), R4b CH2 -1.25, c604 CH1 -1.25 (square edges, the interpolation limits the fit). **The claim holds at
every divided rate from 20 MSa/s to 20 kSa/s, on both channels, on CH1 at A3 B3 and A4 B3, on CH2 at A3 B3 and A3 B4, on
0x153C, 0x153D, enc0, enc-250 and 0x153E.** Not covered by any record: dividers 1 to 8 (100, 66, 50, 40 MSa/s ...).

The filtered captures F1b..F6b (boxcar, CIC, FIR at 1 MSa/s) give s = 0 on CH1, as reported; there the two cores feed
the decimator and the 16-bit output has no A / B pair to mis-order, so this says nothing about the pairing (section
3.4).

### 1.2 Divider 0: probably also off by one, masked by a marginal A stage in one capture [M], [I]

- The `ab_*` d0 records (32 x 1500 words each) have no usable slope: the per-word step is noise (mean |dA| 1.3..2.0
  codes, peak to peak 11). Their noise is dominated by a tone at 1/3 cycle per word whose A-B cross-correlation is the
  same on every file (B3 or B4), but its phase gives the core delay only modulo 3 words and only if the tone is on the
  input; read as a 33 MHz input tone it favours the designed order, read as a 67 MHz alias it favours the off-by-one
  order. **The d0 records decide nothing.**
- **R0b (c51774, 200 MSa/s, CH1 A3 B3, 16:22:26)** has one rising edge on CH1 (code 92 to 135 over about 20 words).
  On its steep part (words 714-725) the tau fit gives -1.75 (RMS 0.61), -1.5: 0.84, **-0.5 (designed): 2.06**. The raw
  values: B = 92, 93, 97, 101, 106, 109, 112 and A = 95, 100, 104, 106, 109, 111, 113 for words 716-722; the host trace
  92, 95, 93, 100, 97, 104, 101, 106 zigzags by 3-5 codes on a 2.5 codes per sample slope, the order B[k+1], A[k] gives
  93, 95, 97, 100, 101, 104, 106, 106. **At divider 0 with the trained A3, the same off-by-one.**
- **c600 (16:50:26, 200 MSa/s, CH1 A4 B3, after the guard moved CH1 A 3 -> 4 at 16:26:39 and the capgate image was
  FEL-loaded)** has one edge too (words 720-731): tau -0.5 (RMS 0.73), -1.5: 1.54. **Designed order.**
- README section 19 (`fpga/al3/README.md:3153-3156`) records that this unit's A cores have one clean stage, A3, and
  calls A4 the surprise. [I] At 200 MSa/s the A transition lies between the A4 instant (F - 15 ns) and the A3 instant
  (F - 12.5 ns): A3 takes conversion n, A4 the previous one, n - 1, so A4 makes A one conversion older, which cancels
  the off-by-one exactly. That is the only difference between R0b and c600 besides the reboot, and it predicts both.
- So the coordinator's "200 MSa/s is in order" holds for the capture taken with the guard's A4, not for the trained A3.
  Two edges are thin evidence; bench run E1 (section 4) settles it. **Until then: the relation is probably the same at
  every rate, including divider 0, whenever the stages sit inside the clean windows.**
- No firmware check sees the order at any rate: the stage training and the runtime self-check work per core (`cs_resid`
  in `firmware/scope/cap_stage.c:96`, the 0x36 outlier counters), the encode check (0x33) looks at the encode flops.

## 2. Mechanism

### 2.1 What the RTL does [V]

`fpga/al3/src/zaklad.v` (0x153E):
- Encodes, `:1416-1449` and the ODDRs `:3383-3392`: `enc_cur` is the write clock as it is after a falling `clk_200MHz`
  edge; encB = write clock (rising at R), encA = its complement (rising at F = R + T/2), the B pads about 6.97 ns and the
  A pads about 7.35 ns after the internal edge (README section 21). At divider 0 the write clock toggles on every falling
  `clk_200MHz` edge (T = 10 ns per core); at divider D > 0 with K = 0 it toggles every D + 1 clocks, so each core gets
  exactly one encode per word, T = 10 (D + 1) ns.
- Capture, `:1253-1331`: IOB DDR registers and two more stages give the pins at E - 5 - 2.5 k ns for every write clock
  edge E. Core A: `sA` on the falling edge F, the pick `a_m` on the next F, `adc*A_w` on the next R. Core B: `sB` on R,
  the pick `b_m` on the next R, `adc*B_w` on the R after (`:1329`). So the word written at R_j holds A as sampled at
  F_{j-2} - 5 - 2.5 kA and B as sampled at R_{j-2} - 5 - 2.5 kB: the A capture instant is T/2 after the B one at every
  rate. The pairing in the RTL is symmetric and right.

### 2.2 Why the pairing at a divided rate is decided by the ADC alone [I, from 2.1]

With T >= 100 ns (every observed divided rate) each core's pins change once per T, about 10-17 ns after its own encode
edge, and both capture instants lie 12.5 or 15 ns before the core's own write clock edge, i.e. at the end of a long
stable period. Each core therefore delivers simply **the last conversion its output bus showed before its own encode
edge**, for any stage 2..5 and either encode phase. If both channels of the chip output conversion n after their own
(n + P)-th encode with the same P (the datasheet's Fig. 2 / 3, P = 4), the word holds B(R_{m-P-1}) and A(F_{m-P-1}) with
F = R + T/2: designed. With data-align on per the datasheet (Fig. 4, S1 = S2 = 1: B re-timed to the A encode, pairing
B sample N - 1 with A sample N) the B bus changes at F_j instead of R_j; at R_m - c the latest change is still F_{m-1}
and the pair is the same. **No model that follows the datasheet, in either S1 / S2 mode, gives the measured 1.5 T at
slow rates, and no FPGA timing (pads, stages, IOB delays) can: those only shift instants inside a stable period.** The
stage-independence of the effect (1.1) is the signature of that.

What fits the data is a **one-conversion latency difference between the chip's two channels**: in encode counts, the
B output lags the A output by one conversion more than the datasheet shows (B's output after encode n + P + 1, or A's
after n + P - 1). It is the same on both chips (CH1 and CH2) and on every bitstream since 0x153C, so it is a property
of the AD9288s as strapped on this board, not of the FPGA. [I] The leading candidate is the data-align mode: pecostm32's
schematic note says S1 = S2 = 1 (`research/notes/hack-repo-hardware-fpga.md:148`), and the stage windows measured on
this unit fit align-on better than align-off once a common delay of about 4 ns over the datasheet typical is allowed
(A clean at stage 3 only, transition at about F - 15 ns; B clean at 3, 4, 5 and not at 2, transition at about
R - 10 ns, i.e. on the A encode's timing, F + 15 ns). README section 10 concluded align-off from 0x1539's lane pattern
with datasheet delays; with the same 4 ns that pattern is borderline either way. If align is on, the chip re-times B
one A edge later than its Fig. 4 shows. Bench run E3 discriminates (section 4).

### 2.3 Why the stage does not change it, and why divider 0 can look right [I]

At a divided rate the 2.5 ns stage steps move the capture instant inside a stable period of at least 85 ns: nothing
changes, as measured (A3 / A4, B3 / B4 all give s = 1). At divider 0 the period is 10 ns, a core's bus is stable for
only about 6 ns, and a stage step can cross a transition: a stage whose instant lies before the transition takes the
previous conversion. The trained clean stages (A3, B3 or B4 on this unit) see the same pairing as the slow rates (R0b);
CH1 A4, which on this unit lies on the A transition (README section 19), takes A one conversion older and happens to
restore the designed order (c600), at the price of carry errors near the transition. So divider 0 is not immune; its
order depends on which side of the transition the A stage sits, and the stages are trained at a quiet input where the
conversion index cannot be seen.

### 2.4 Why the simulation passes [V]

`tb/tb_trigpos.v:83-113` models each core as "sample on the encode rising edge, output pl = 4 encode cycles later, tPD
3.3 ns", identical for A and B, without pad delays, and `tb/tb_adccap.v:35-65, :97-110` models both datasheet modes
(align: B's outputs on the A encode, still `pv[pl]` of B's own pipeline, i.e. the datasheet's pairing). Both models
are symmetric, so the dut's symmetric pipeline passes. In addition the check at `tb_trigpos.v:434-439` accepts either
sign of the B - A offset (it fixes the sign from the first run and checks |B - A| = T/2), so it would not catch a
B-after-A order either; `tb_adccap.v:330-338` does check B first. The testbenches verify the RTL against the datasheet,
and the datasheet pairing is what the unit does not do.

### 2.5 Consequences beyond the host trace [I]

- The UI's own Filter Off trace at divided rates is built the same way (`fpga_control.c:779-859`): the screen zigzags on
  every edge, and its frequency / duty measurements double on a square at slow rates as the host's did (1 kHz read as
  2006 Hz at 20 kSa/s).
- The FPGA's hardware counter takes the pair from `adc*_w` with core B as the earlier sample (`zaklad.v:2481-2482`):
  at a divided rate each edge of a square gives it an extra crossing whenever the swapped neighbours straddle both
  thresholds. That is most likely the 0x153C "counter miscounts the 1 kHz post with the filter off at D > 0" (README
  section 17, bench 2026-10-21 17:53), which the firmware now hides as `COUNTER_F_GATED` (`fpga_counter.c:122, :299`).
- Averaging (host, FPGA legacy sums), segments and the 8-bit logger carry the same pairing: an averaged edge is smeared
  by one sample on the B half. Trigger positions are unaffected (the trigger compares core A only, protocol 7.5).
- At divider 0 with the trained stages (if E1 confirms R0b): rise times and the interleave tracker's A - B model see a
  10 ns skew of one core; every K >= 1 filter's polyphase prefilter (B(k-1) A(k-1) B(k) A(k)) is mis-weighted by the same
  sample, a small droop error at 100 MSa/s per core that the 1 MSa/s checks cannot see.

## 3. Where the fix belongs

### 3.1 Recommended: the testbench model first, then one register less on the B lanes (FPGA)

1. **(c) Testbench model.** Give the AD9288 models a per-channel pipeline depth (`plB = pl + 1`, or `+plB=` on the
   command line) in `tb_trigpos.v` and `tb_adccap.v` (and `sim_models.v` if it carries a copy). With `plB = 5` the
   current RTL must fail "legacy A/B order" (with the sign check made strict: B first, not "same sign in every run") at
   every legacy rate. That reproduces the bench in simulation before any RTL changes.
2. **(a) RTL.** Take core B one write clock earlier: drop the `b_m` stage from the own-encode B path,
   ```verilog
   //zaklad.v :1324-1331, the rising-edge block
   adc1B_w <= cap_sel[12] ? bf_m[7:0]  : cap_pick(cap_sel[5:3],  sB[47:0]);
   adc2B_w <= cap_sel[13] ? bf_m[15:8] : cap_pick(cap_sel[11:9], sB[95:48]);
   ```
   The path `sB -> 6:1 mux -> adc*B_w` is one write clock period, the same as today's `sB -> b_m` (no new half-cycle
   path). Core A and everything that times on it (trigger compare, trigger address, the 0x14 / 0x18 offsets, the EXT
   tables) stay exactly as they are; B in a word becomes one conversion newer, which is the designed pairing on this
   unit. Every consumer of the ring is then right without a change: READ_RAW all layouts, READ_TRACE, the UI trace,
   host and FPGA averaging, segments, the 8-bit logger, the hardware counter (which could then drop `COUNTER_F_GATED`),
   protocol 7.3 and the mcp simulator. The A-encode reference path (`bf_m`, bit 12 / 13) needs its own decision after
   E3 (if align is on and that path is used, it must lose a stage too, which is a half-period path through the mux:
   re-time it).
3. **Make it a switch, not a constant.** The relation is a chip property measured on one unit. Put it on the free 0x63
   bits: bit 14 = CH1 B one word earlier, bit 15 = CH2 B (power-on value chosen by E1 / E2, stored XOR like the rest
   of `cap_sel`), so the firmware can set it per channel and per clock class from a measurement. Whether the bit must
   differ between divider 0 and the divided class follows from E1: if the trained clean stages give s = 1 at divider 0
   too, one value serves all rates; if not, the firmware writes it per class (it already keeps per-class stages and
   calibration, `P.cal_clock_class`).
4. Firmware: a cheap order self-check that the training can run with the generator's 1 kHz on the post (a record at
   divider 99 through the probe, the mid metric of section 1, both values of bit 14 / 15, keep the smoother), and
   protocol 7.3 / caps bit 12 INTERLEAVE_UNVERIFIED set until that check has passed on the unit (today the bit is
   cleared from 0x33, which reports the encode flops, not the data pairing).

Cost: a new bitstream (0x153F), a timing roll, run.sh on two seeds and one flash, which the 0.3.0 release (0x153E) may
not want to wait for. Risk: low in RTL (one register less, timing equal or better); the relation itself is the risk
(3.3).

### 3.2 Alternative: a firmware-only fix through the existing A-encode reference (0x63 bit 12 / 13)

If E3 shows that capturing B against the falling edge (bit 12 / 13 set, the existing `sBf` / `bf_m` path) gives the
designed order at divided rates, then B's bus changes on its own encode and capturing it T/2 later takes the next
conversion. The firmware could then write bits 12 / 13 for the divided class only (and its own B stages for that
class): no bitstream, every consumer right at divided rates, protocol unchanged. Limits: at T/2 shorter than the B
transition delay plus 15 ns (dividers up to about 4) the instant may fall into B's transition, so it needs E2's small
dividers; divider 0 is not covered (there the bit garbles B under align-off). If align is on (E3 negative), this
alternative does not exist.

### 3.3 Not recommended: shift the pairing in every reader (firmware and host)

Reading B one word later in READ_RAW (layouts 1 / 0), READ_TRACE, `fpga_read_sample_data`, the averaged and segment
readers, the logger's word stream, the Bode engine's legacy reads, and in `scope.interleave` plus the simulator touches
about ten consumers, and the record's last B sample lies one word past the record (the FPGA stops at the record's end;
the next ring word is from an earlier lap), so either a sample is dropped, the arm geometry grows by one word for the
divided class, or A is read one word early instead (the word before the record start is not always written in this
capture, and the A-based trigger point moves by two trace samples). It also cannot fix the hardware counter. Use it
only as a host-side interim for analysis (e.g. an explicit `order` / `b_shift` argument when re-interleaving saved
raw per-core arrays), never as the shipped behaviour.

### 3.4 What else depends on the order

- Trigger position (0x14 / 0x18 pipeline offsets, the firmware's legacy EXT table): core A only; unchanged by 3.1 and
  3.2, moved by two samples by the "A one word early" variant of 3.3.
- UI Filter Off trace, measurements on it, phosphor / spectrum views at divided rates: fixed by 3.1 and 3.2 for free.
- Averaging and segments in the legacy layout: per-lane sums of the same words; fixed by 3.1 / 3.2.
- Protocol 7.3, `interleave_order`, the mcp simulator (`simulator.py` interleave 0): unchanged under 3.1 / 3.2; 7.3
  should say that the pairing is set by 0x63 bits 14 / 15 (3.1) and verified by the training's check.
- Filtered modes (K >= 1) and peak: the decimator reads `adc*_w`, so 3.1 also corrects their polyphase pairing at the
  full rate (if E1 shows divider 0 is off with clean stages); peak is unaffected (min / max of both cores).

### 3.5 Risk if the relation is unit- or stage-dependent

At divided rates it was the same on both chips, at every stage the unit has used, on five bitstreams and two days:
nothing in the data suggests dependence. It is a chip property, so another unit with the same straps should behave the
same, but that is one unit's evidence [I]; a hard-coded RTL constant would be wrong on a unit that follows the
datasheet, hence the 0x63 switch and the training check in 3.1. At divider 0 it does depend on the stage (2.3): any fix
for divider 0 must be set after the stages are trained and checked with an edge, never from the quiet-input training
alone.

## 4. Bench runs before any fix ships

All Filter Off, raw per-core captures with the debug firmware, 16 records each, the mid metric and the tau fit of
section 1 per record (s = 0 / tau -0.5 designed, s = 1 / tau -1.5 off by one). Generator on the calibration post.
Restore the trained 0x63 afterwards.

- **E1, divider 0 versus stage.** CH1 and CH2 probes x1 directly on the post (not the ladder; the probe's own
  bandwidth gives an edge of some 50-100 ns, 10-20 samples), generator 1 MHz square, 200 MSa/s, triggered on each
  channel. For each channel: A3 B3, A3 B4, A4 B3, A4 B4, A2 B3 (and the 0x36 outlier counts of each). Expect, if 2.3 is
  right: s = 1 at every clean pair (A3 with B3 / B4), s = 0 at A4 (A one older) with outliers; A2 garbled or s = 1. If
  the clean A3 pairs give s = 0, divider 0 is in order and the fix is for the divided class only.
- **E2, every divided rate class.** Same wiring (or CH2 on the ladder's first node for the slow rates), dividers 1, 2,
  3, 4, 9, 99, 999, 9999, generator about fs / 40 (square), stages as trained and A4 / B4. Expect s = 1 at every
  divider; any divider with another value (most likely 1..4, where T/2 is close to the transition delays) needs its
  own entry in the fix.
- **E3, data-align on or off.** Divider 99 and divider 9, CH2 on the ladder (1 kHz) for 99, the post for 9: 0x63 with
  bit 13 (CH2) and bit 12 (CH1) set, B stages 3 and 4. If the order becomes designed (s = 0), B's bus follows its own
  encode (align off) and alternative 3.2 exists. If it stays s = 1, B's bus changes on the A encode (align on; the chip
  re-times B one A edge later than its Fig. 4 shows) and only 3.1 fixes it. Also at divider 0 with bit 12 / 13 set and
  B stages 2..5 at a quiet input (README section 10, bench check 5, never run): B clean at A-like stages = align on.
- **E4, the hardware counter.** CH1 on the post, generator 1 kHz, 20 kSa/s Filter Off, GET_COUNTER with level / hysteresis
  set by hand (the firmware reports it not VALID while gated; read E, T anyway): about 2 kHz confirms 2.5; after a fix it
  must read 1 kHz.
- **E5, stability.** Repeat one E1 and one E2 point after a power cycle and after warming the FPGA (hair dryer): the
  order must not change. A change at divider 0 is expected only for stages near a transition.
- **E6 (optional, needs a second source).** EXT trigger from an external generator, divider 99, CH1 on the same
  signal: the CH1 core A and core B crossings against the EXT edge (tb_trigpos's +16..17 samples) tell whether A is one
  conversion early or B one late against the datasheet latency. 3.1 does not depend on the answer (B is moved, A kept),
  but the legacy EXT table does if A is the early one.
