#!/bin/bash
# Simulate fpga/al3/src/zaklad.v with iverilog (in WSL). The steps below, all must pass:
#   1. tb_ext_trigger.v against Atlan4's original (REF_REV, default a947e78 = 0x1532): CH1/CH2
#      equivalence and the external trigger.
#   2. tb_decim.v against our 0x1533 (REF1533_REV, default 2839258): K = 0 equivalence including
#      EXT, then decimated captures (logged), trigger position, EXT and time-out with K >= 1.
#   3. cic_model.py check: every logged decimated sample against the direct-form reference.
#   4. tb_fir.v against our 0x1534 (REF1534_REV, default 6d4c8ad): FIR off / not eligible
#      equivalence with K = 0..3, then FIR captures (logged and checked by cic_model.py), trigger
#      position, EXT, the EXT data lag and the time-out with the FIR (0x1535).
#   5. tb_long.v: decim_chan and fir_chan free-running for up to 100k outputs per configuration with a
#      periodic input: periodic, wrap-independent (random 48-bit start state), bounded, correct mean.
#      LONG_SCALE (percent, default 100) scales the output counts.
#   6. tb_seg.v against our 0x1535 (REF1535_REV, default e2aec2e): sequencer off equivalence, then
#      segment and averaging runs (0x1536), logged and checked by seg_model.py. SEG_P1 (default 12)
#      sets the number of random equivalence captures of its first phase.
#   7. tb_trigpos.v (0x1537): trigger position determinism with an AD9288 pipeline model, the firmware's
#      byte sequences, shuffled cases (K = 0..3, FIR, dividers 0..399, CH1 / EXT) and random phases, then
#      the configuration race at slow legacy rates. TRIGPOS_RUNS (default 6) runs per case.
#   8. tb_seqcontract.v: the firmware's sequencer byte sequences (protocol 1.17) against the simulated FPGA; the
#      transcript must equal tb/contract_0x153D.txt (the reference for the firmware's host tests).
#   9. encphase.sh MODE=check (0x1538): tb_encphase.v, the encode pins against the write clock after the firmware's
#      UI and host arm sequences at swept write phases, plain and with delay-annotated encode flop inputs, and the
#      0x33 self-check catching a late CH2 A flop.
#  10. mcuread.sh (0x1539): tb_mcuread.v, the MCU readout with the firmware's read sequences at swept strobe timing and
#      per-bit pad delays: every byte right at every sample time after the rising edge; the 0x1538 reference
#      (REF1538_REV, default ce943bf) shows the bench's mixed bytes when the MCU samples sooner than the pad delay.
#  11. tb_adccap.v (0x153A): the ADC capture stages against a behavioural AD9288 whose buses are in transition from tV
#      to tPD after the encode (own encode, or the A encode in data-align mode), encode pad delay swept: clean exactly
#      where the window formula says, outlier counters exact, B first by T/2 at every rate with the matching reference.
#  12. tb_firstrec.v (fpga-gen10): the first decimated record after a mode / shift / rate change at divider 4095 with the
#      firmware's exact byte sequences (both arm orders), exact from word 0; the transient without the settle wait and
#      the stale-shift fault model (whole record 0xFFFF) for comparison. FIRSTREC_TOTAL words per record (default 16).
#  13. tb_timeout.v (0x153B, fpga-gen11): records that end on the auto time-out or the firmware's forced stop (0x1A = 0
#      after the ring has wrapped), with the firmware's byte sequences, checked word by word against a mirror of the
#      memory write port: equal to the words written, contiguous in write order, the newest total words (triggered: the
#      post-trigger region complete), 0x18 true for the record. TIMEOUT_TOTAL words per record (default 2048),
#      TIMEOUT_RUNS forces per case (default 4), TIMEOUT_BENCH=0 skips case B (fpga-gen13: the host capture of the
#      2026-10-20 19:52 bench on 0x153B, D 199 CIC3+FIR, pretrigger 3000, total 6000, untriggered with the force and
#      triggered, read with READ_RAW's split at the ring end). Against the 0x153A source it reproduces the bench splice.
#  14. tb_trigtypes.v (0x153C C1): pulse width, Nth edge and timeout triggers on CH1 / CH2 / EXT, legacy and decimated,
#      against a reference model on the stream the FPGA saw, at the edge type's trigger position; the reserved types
#      and the 0x34 / 0x37 read-back; the trigger hold-off (0x1B, 0x66) across sequencer restarts and MCU re-arms; the
#      0x153B stop gate per type. TRIGTYPES_RUNS (default 2) runs per type, polarity and case.
#  15. tb_peak.v (0x153C C2): peak detect (0x56 bit 1): every 16-bit word { max, min } of exactly the K = 1 boxcar's
#      window of raw samples (adc*_w), at K = 1..3 with the FIR enabled (it stays off) and at K = 0 (the one-register
#      switch forces the 16-bit path, the full-rate ADCs and the window from 0x0D; 0x56 = 0 gives the legacy path back),
#      the 0x29 read-back, READ_RAW bytes, the alias demonstration (6 ns spikes at 5 MSa/s) and the window max / min trigger.
#  16. tb_counter.v (0x153C C3): the hardware counter (0x61, 0x62, read 0x35 with 16 bytes): gates exactly 2e6 clocks apart,
#      E / T / W and min / max of every gate against a model of the input stream and of the captured pair, accuracy against
#      the stimulus on EXT and CH1 (200 MSa/s, 10 MSa/s, K = 1), the source select (0x61 bits 3..2 and the 0x15 follow),
#      the Schmitt thresholds at their boundaries, the hysteresis against noise, the "new" flag and the snapshot read.
#  17. tb_stream.v (0x153D D1): the streaming logger (0x69, 0x6A, read 0x38): the write never stops and never triggers
#      while streaming at legacy and filtered rates (0x0A / 0x18 read 0 with the time-out elapsed or the level crossed),
#      the 0x38 snapshot against a mirror of the write port (counter, W, unread), chunks read with the firmware's
#      sequences at the contract's rate tiers contiguous across the ring end, the lap flag exactly at the write that
#      overwrites the oldest unread word, the resync, the empty ring, stop and restart, normal captures before and after
#      (0x69 = 0 = 0x153C). STREAM_CHUNKS chunks per keep-up tier (default 2).
#  18. tb_square.v (0x153D D2 / D3): the fractional square generator (0x6C index 0 / 1, 0x6D, 0x50, read 0x39; the pulse
#      pattern generator of the D2 branch was cut to this by the owner): the generator on 0x50 alone (the cut registers
#      and the retired DDS registers change nothing), the start latency, the firmware's SET_GEN square waves (1 kHz,
#      999.5 Hz, 10 kHz 25 %, 1 MHz, 10 MHz, 7.3 MHz, 3.5 + 2.25 clocks, 2 + 2 clocks) with every edge within one clock
#      of ideal and the mean period exact, the double buffer (glitch-free change at once, accumulator kept, two commits
#      in a row, a commit while idle, modes 1 / 2 as off, stop, index >= 2 ignored).
#  19. tb_skip.v (0x153D D3): the encodes always at full rate with sample skipping for K = 0 (the owner's decision of
#      2026-10-21 17:59): at D in {1, 2, 3, 9, 19, 199, 399} every stored word is the captured pair of its write clock edge
#      and consecutive words are D + 1 edges apart (the write clock toggles every 10 ns at every rate), the auto time-out,
#      0x14, the 0x05 ready wait and a CH1 step's trigger position on the stored words, EXT at D = 49, the hardware counter
#      with the filter off at D = 399 (E, T, duty exact: the 0x153C bench miscount), divider changes while idle, K = 1 as
#      before, and (fpga-d3c's register audit) at D = 199999 (1 kSa/s) 0x14, 0x0A, 0x18, 0x38 and 0x34 read their reset
#      values between the 0x05 ready and the first stored word after a record that left them non-zero, then 0x38 W = 1.
#      The K = 0 lockstep random phases of steps 1, 2, 4 and 6 run at D = 0 since 0x153D (the references gate their
#      encodes at D > 0 and cannot model the skipping); steps 1 and 2 end with 12 more random K = 0 captures at D = 1 .. 9
#      checked against the model of the dut's own pair stream instead (the reference is out of step after them).
# Usage: ./run.sh [seed] ; DUT=<file> for mutation runs; ONLY=ext|decim|fir|long|seg|trigpos|contract|encphase|mcuread|adccap|firstrec|timeout|trigtypes|peak|counter|stream|square|skip runs one testbench.
# Needs iverilog (as root: apt-get install -y iverilog) and python3. Work files go to ~/al3-sim.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
REF_REV=${REF_REV:-a947e78}
REF1533_REV=${REF1533_REV:-2839258}
REF1534_REV=${REF1534_REV:-6d4c8ad}
REF1535_REV=${REF1535_REV:-e2aec2e}
SEED=${1:-12345}
WORK=${SIM_WORK:-$HOME/al3-sim}
DUT=${DUT:-$REPO/fpga/al3/src/zaklad.v}   # design under test (override for mutation runs)
ONLY=${ONLY:-all}
mkdir -p "$WORK"
command -v iverilog >/dev/null || { echo "iverilog not installed"; exit 1; }
# Reference: the file at a git revision with every module renamed to <name>_ref (pll/sample_memory stay shared)
# REF_SRC_DIR=<dir> takes <dir>/<rev>.v instead of git (a git worktree made on Windows is not readable from WSL git)
# 0x1539 loads the MCU read byte on the falling edge of i_mcu_clk (README "Changes" section 9). The references get the
# same edge (the one always block after "requested byte is loaded"), so that asynchronous status and live-pin reads
# are sampled at the same instant and the equivalence steps compare everything else exactly; tb_mcuread.v tests the
# edge change itself. REF_READ_EDGE=pos keeps the references as they were.
mkref() {
  { if [ -n "$REF_SRC_DIR" ]; then cat "$REF_SRC_DIR/$1.v"; else git -c safe.directory='*' -C "$REPO" show "$1:fpga/al3/src/zaklad.v"; fi; } \
    | sed -E 's/\b(clockdivider|pwmcounter|freq_generator_dds_pwm|decim_chan|fir_chan|fnirsi_1013D)\b/\1_ref/g' \
    | if [ "${REF_READ_EDGE:-neg}" = neg ]; then
        sed -E '/requested byte is loaded/,/always @\(posedge i_mcu_clk\)/ s/always @\(posedge i_mcu_clk\)/always @(negedge i_mcu_clk)/'
      else cat; fi > "$2"
  grep -q 'negedge i_mcu_clk' "$2" || [ "${REF_READ_EDGE:-neg}" != neg ] || { echo "mkref: read edge not changed in $1"; exit 1; }
}
compile() {  # <out.vvp> <ref.v> <tb.v>
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -o "$1" "$HERE/sim_models.v" "$DUT" "$2" "$3" \
    2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
}
T0=$(date +%s)
RC=0
if [ "$ONLY" = all ] || [ "$ONLY" = ext ]; then
  mkref "$REF_REV" "$WORK/zaklad_ref.v"
  compile "$WORK/tb.vvp" "$WORK/zaklad_ref.v" "$HERE/tb_ext_trigger.v"
  vvp -n "$WORK/tb.vvp" +seed=$SEED | tee "$WORK/sim.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = decim ]; then
  mkref "$REF1533_REV" "$WORK/zaklad_ref1533.v"
  compile "$WORK/tb_decim.vvp" "$WORK/zaklad_ref1533.v" "$HERE/tb_decim.v"
  vvp -n "$WORK/tb_decim.vvp" +seed=$SEED +logfile="$WORK/decim_edges.log" | tee "$WORK/sim_decim.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_decim.log" || RC=1
  python3 -I "$HERE/cic_model.py" check "$WORK/decim_edges.log" | tee "$WORK/model.log"
  grep -q -- '-> PASS' "$WORK/model.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = fir ]; then
  mkref "$REF1534_REV" "$WORK/zaklad_ref1534.v"
  compile "$WORK/tb_fir.vvp" "$WORK/zaklad_ref1534.v" "$HERE/tb_fir.v"
  vvp -n "$WORK/tb_fir.vvp" +seed=$SEED +logfile="$WORK/fir_edges.log" | tee "$WORK/sim_fir.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_fir.log" || RC=1
  python3 -I "$HERE/cic_model.py" check "$WORK/fir_edges.log" | tee "$WORK/model_fir.log"
  grep -q -- '-> PASS' "$WORK/model_fir.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = long ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_long.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_long.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_long.vvp" +seed=$SEED +scale=${LONG_SCALE:-100} | tee "$WORK/sim_long.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_long.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = seg ]; then
  mkref "$REF1535_REV" "$WORK/zaklad_ref1535.v"
  compile "$WORK/tb_seg.vvp" "$WORK/zaklad_ref1535.v" "$HERE/tb_seg.v"
  vvp -n "$WORK/tb_seg.vvp" +seed=$SEED +p1=${SEG_P1:-12} +logfile="$WORK/seg.log" | tee "$WORK/sim_seg.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_seg.log" || RC=1
  python3 -I "$HERE/seg_model.py" check "$WORK/seg.log" | tee "$WORK/model_seg.log"
  grep -q -- '-> PASS' "$WORK/model_seg.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = trigpos ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_trigpos.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_trigpos.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_trigpos.vvp" +seed=$SEED +runs=${TRIGPOS_RUNS:-6} +logfile="$WORK/trigpos.log" | tee "$WORK/sim_trigpos.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_trigpos.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = contract ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_seqcontract.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_seqcontract.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_seqcontract.vvp" +transcript="$WORK/contract.txt" | tee "$WORK/sim_contract.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_contract.log" || RC=1
  if cmp -s "$WORK/contract.txt" "$HERE/contract_0x153D.txt"; then echo "contract transcript = tb/contract_0x153D.txt"
  else echo "contract transcript differs from tb/contract_0x153D.txt"; RC=1; fi
fi
if [ "$ONLY" = all ] || [ "$ONLY" = encphase ]; then
  MODE=check SIM_WORK="$WORK" DUT="$DUT" bash "$HERE/encphase.sh" "$SEED" | tee "$WORK/sim_encphase.log"
  grep -q 'encphase: PASS' "$WORK/sim_encphase.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = mcuread ]; then
  SIM_WORK="$WORK" DUT="$DUT" bash "$HERE/mcuread.sh" "$SEED" > "$WORK/sim_mcuread_all.log" 2>&1 || true
  grep -E 'sweep points|RESULT' "$WORK/sim_mcuread_all.log"
  grep -q 'mcuread: PASS' "$WORK/sim_mcuread_all.log" && echo "mcuread: PASS" || { echo "mcuread: FAIL"; RC=1; }
fi
if [ "$ONLY" = all ] || [ "$ONLY" = adccap ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_adccap.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_adccap.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_adccap.vvp" +seed=$SEED | tee "$WORK/sim_adccap.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_adccap.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = firstrec ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_firstrec.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_firstrec.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_firstrec.vvp" +seed=$SEED +total=${FIRSTREC_TOTAL:-16} | tee "$WORK/sim_firstrec.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_firstrec.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = timeout ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_timeout.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_timeout.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_timeout.vvp" +seed=$SEED +total=${TIMEOUT_TOTAL:-2048} +runs=${TIMEOUT_RUNS:-4} +bench=${TIMEOUT_BENCH:-1} | tee "$WORK/sim_timeout.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_timeout.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = trigtypes ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_trigtypes.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_trigtypes.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_trigtypes.vvp" +seed=$SEED +runs=${TRIGTYPES_RUNS:-2} +logfile="$WORK/trigtypes.log" | grep -v '^  intent' | tee "$WORK/sim_trigtypes.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_trigtypes.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = peak ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_peak.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_peak.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_peak.vvp" +seed=$SEED | tee "$WORK/sim_peak.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_peak.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = counter ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_counter.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_counter.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_counter.vvp" +seed=$SEED | tee "$WORK/sim_counter.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_counter.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = stream ]; then
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_stream.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_stream.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_stream.vvp" +seed=$SEED +chunks=${STREAM_CHUNKS:-2} | tee "$WORK/sim_stream.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_stream.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = square ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_square.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_square.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_square.vvp" +seed=$SEED +logfile="$WORK/square.log" | tee "$WORK/sim_square.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_square.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = skip ]; then
  iverilog -g2012 -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_skip.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_skip.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_skip.vvp" +seed=$SEED | tee "$WORK/sim_skip.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_skip.log" || RC=1
fi
echo "wall clock $(( $(date +%s) - T0 )) s"
[ $RC -eq 0 ] && echo "ALL PASS (seed $SEED)" || echo "SOMETHING FAILED (seed $SEED)"
exit $RC
