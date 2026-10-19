#!/bin/bash
# Simulate fpga/al3/src/zaklad.v with iverilog (in WSL). Five steps, all must pass:
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
# Usage: ./run.sh [seed] ; DUT=<file> for mutation runs; ONLY=ext|decim|fir|long|seg|trigpos runs one testbench.
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
mkref() {
  git -c safe.directory='*' -C "$REPO" show "$1:fpga/al3/src/zaklad.v" \
    | sed -E 's/\b(clockdivider|pwmcounter|freq_generator_dds_pwm|decim_chan|fir_chan|fnirsi_1013D)\b/\1_ref/g' > "$2"
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
echo "wall clock $(( $(date +%s) - T0 )) s"
[ $RC -eq 0 ] && echo "ALL PASS (seed $SEED)" || echo "SOMETHING FAILED (seed $SEED)"
exit $RC
