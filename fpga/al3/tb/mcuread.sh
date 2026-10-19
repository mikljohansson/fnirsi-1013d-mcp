#!/bin/bash
# MCU readout timing (tb_mcuread.v, 0x1539): the design under test must return every byte at every MCU sample time
# after the rising edge; the 0x1538 reference (REF1538_REV, default ce943bf) must show the bench's mixed bytes when
# the MCU samples sooner than the pad delay, and none after it. Prints "mcuread: PASS" or "mcuread: FAIL".
# Usage: mcuread.sh [seed]; DUT, SIM_WORK, REF_SRC_DIR (<dir>/<rev>.v instead of git) as in run.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SEED=${1:-12345}
WORK=${SIM_WORK:-$HOME/al3-sim}
DUT=${DUT:-$REPO/fpga/al3/src/zaklad.v}
REF1538_REV=${REF1538_REV:-ce943bf}
mkdir -p "$WORK"
comp() {  # <out.vvp> <design.v>
  iverilog -g2012 -Wall -Wno-timescale -Wno-implicit -s tb -o "$1" "$HERE/sim_models.v" "$2" "$HERE/tb_mcuread.v" \
    2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits' || true
}
RC=0
comp "$WORK/tb_mcuread.vvp" "$DUT"
vvp -n "$WORK/tb_mcuread.vvp" +seed=$SEED +expect_mix=0 | tee "$WORK/sim_mcuread.log"
grep -q 'RESULT: .* -> PASS' "$WORK/sim_mcuread.log" || RC=1
if [ -n "$REF_SRC_DIR" ]; then cp "$REF_SRC_DIR/$REF1538_REV.v" "$WORK/zaklad_1538.v"
else git -c safe.directory='*' -C "$REPO" show "$REF1538_REV:fpga/al3/src/zaklad.v" > "$WORK/zaklad_1538.v"; fi
comp "$WORK/tb_mcuread_1538.vvp" "$WORK/zaklad_1538.v"
vvp -n "$WORK/tb_mcuread_1538.vvp" +seed=$SEED +expect_mix=1 | tee "$WORK/sim_mcuread_1538.log"
grep -q 'RESULT: .* -> PASS' "$WORK/sim_mcuread_1538.log" || RC=1
[ $RC -eq 0 ] && echo "mcuread: PASS" || echo "mcuread: FAIL"
exit $RC
