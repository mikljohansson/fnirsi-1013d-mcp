#!/bin/bash
# Simulate fpga/al3/src/zaklad.v with iverilog (in WSL). Eleven steps, all must pass:
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
#      transcript must equal tb/contract_0x153A.txt (the reference for the firmware's host tests).
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
# Usage: ./run.sh [seed] ; DUT=<file> for mutation runs; ONLY=ext|decim|fir|long|seg|trigpos|contract|encphase|mcuread|adccap|firstrec runs one testbench.
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
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -o "$1" "$HERE/sim_models.v" "$DUT" "$2" "$3" \
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
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_long.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_long.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
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
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_trigpos.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_trigpos.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_trigpos.vvp" +seed=$SEED +runs=${TRIGPOS_RUNS:-6} +logfile="$WORK/trigpos.log" | tee "$WORK/sim_trigpos.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_trigpos.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = contract ]; then
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_seqcontract.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_seqcontract.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_seqcontract.vvp" +transcript="$WORK/contract.txt" | tee "$WORK/sim_contract.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_contract.log" || RC=1
  if cmp -s "$WORK/contract.txt" "$HERE/contract_0x153A.txt"; then echo "contract transcript = tb/contract_0x153A.txt"
  else echo "contract transcript differs from tb/contract_0x153A.txt"; RC=1; fi
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
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_adccap.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_adccap.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_adccap.vvp" +seed=$SEED | tee "$WORK/sim_adccap.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_adccap.log" || RC=1
fi
if [ "$ONLY" = all ] || [ "$ONLY" = firstrec ]; then
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/tb_firstrec.vvp" "$HERE/sim_models.v" "$DUT" "$HERE/tb_firstrec.v"     2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
  vvp -n "$WORK/tb_firstrec.vvp" +seed=$SEED +total=${FIRSTREC_TOTAL:-16} | tee "$WORK/sim_firstrec.log"
  grep -q 'RESULT: .* -> PASS' "$WORK/sim_firstrec.log" || RC=1
fi
echo "wall clock $(( $(date +%s) - T0 )) s"
[ $RC -eq 0 ] && echo "ALL PASS (seed $SEED)" || echo "SOMETHING FAILED (seed $SEED)"
exit $RC
