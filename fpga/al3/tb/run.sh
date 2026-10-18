#!/bin/bash
# Simulate the external-trigger design against Atlan4's original with iverilog (in WSL).
# Usage: ./run.sh [seed] ; REF_REV=<git rev> selects the reference zaklad.v (default a947e78 = 0x1532).
# Needs iverilog (as root: apt-get install -y iverilog). Work files go to ~/al3-sim (WSL-local).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
REF_REV=${REF_REV:-a947e78}
SEED=${1:-12345}
WORK=${SIM_WORK:-$HOME/al3-sim}
DUT=${DUT:-$REPO/fpga/al3/src/zaklad.v}   # design under test (override for mutation runs)
mkdir -p "$WORK"
command -v iverilog >/dev/null || { echo "iverilog not installed"; exit 1; }
# Reference: the original file with every module renamed to <name>_ref (pll/sample_memory stay shared)
git -c safe.directory='*' -C "$REPO" show "$REF_REV:fpga/al3/src/zaklad.v" \
  | sed -E 's/\b(clockdivider|pwmcounter|freq_generator_dds_pwm|fnirsi_1013D)\b/\1_ref/g' > "$WORK/zaklad_ref.v"
iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -o "$WORK/tb.vvp" \
  "$HERE/sim_models.v" "$DUT" "$WORK/zaklad_ref.v" "$HERE/tb_ext_trigger.v" \
  2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
T0=$(date +%s)
vvp -n "$WORK/tb.vvp" +seed=$SEED | tee "$WORK/sim.log"
echo "wall clock $(( $(date +%s) - T0 )) s"
grep -q 'RESULT: .* -> PASS' "$WORK/sim.log"
