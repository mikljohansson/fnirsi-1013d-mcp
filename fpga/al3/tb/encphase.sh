#!/bin/bash
# tb_encphase.v over src/zaklad.v (0x1538) and, as a demonstration, over 0x1535 and 0x1537 with and without a
# delay annotation of the encode flops' D inputs (fpga/al3/README.md "Changes" section 8).
# Usage (WSL): tb/encphase.sh [seed]   REF_SRC_DIR=<dir> takes <dir>/<rev>.v instead of git (Windows worktrees).
# MODE=check runs only the pass/fail variants (src plain, src annotated at 3.5 ns, and src with CH2 A at 3.9 ns, which
# must fail through 0x33), as run.sh step 9 does.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SEED=${1:-1}
WORK=${SIM_WORK:-$HOME/al3-sim}/encphase
DUT=${DUT:-$REPO/fpga/al3/src/zaklad.v}
IVDEFS=${IVDEFS-$(python3 -I "$HERE/encphase_defs.py" "$DUT")}   # fpga-encphase images (README section 19)
# the encode flops' D windows (A, B): 3.75 ns for the source, enc_cur's edge to the encode clock edge for an --iob image
read -r WA WB <<< "$(python3 -I "$HERE/encphase_defs.py" --windows "$DUT")"
dl() { python3 -I -c "import sys; print('%.3f' % (float(sys.argv[1]) + float(sys.argv[2])))" "$1" "$2"; }
REV1535=${REV1535:-e2aec2e}
REV1537=${REV1537:-c15981e}
MODE=${MODE:-all}
mkdir -p "$WORK"
getrev() { if [ -n "$REF_SRC_DIR" ]; then cat "$REF_SRC_DIR/$1.v"; else git -c safe.directory='*' -C "$REPO" show "$1:fpga/al3/src/zaklad.v"; fi; }
# annotate <in> <out> <DA1> <DA2> <DB1> <DB2>: encode flop D inputs through transport delays
annotate() {
  python3 -I - "$@" <<'EOF'
import sys
src, dst, da1, da2, db1, db2 = sys.argv[1:7]
s = open(src).read()
if 'reg enc_cur_n = 1;' in s:                         # fpga-encphase --iob: fabric copies and ODDR inputs
    anchor, b, a = 'reg enc_cur_n = 1;', 'enc_cur', 'enc_cur_n'
    pairs = [('adc1_encB <= enc_cur;', 'adc1_encB <= anB1;'), ('adc2_encB <= enc_cur;', 'adc2_encB <= anB2;'),
             ('adc1_encA <= enc_cur_n;', 'adc1_encA <= anA1;'), ('adc2_encA <= enc_cur_n;', 'adc2_encA <= anA2;')]
    for pin, an in (('o_adc1_encA', 'anA1'), ('o_adc2_encA', 'anA2')):
        pairs.append(('.q(%s), .clk(clk_ADCA), .d1(enc_cur_n), .d2(enc_cur_n)' % pin,
                      '.q(%s), .clk(clk_ADCA), .d1(%s), .d2(%s)' % (pin, an, an)))
    for pin, an in (('o_adc1_encB', 'anB1'), ('o_adc2_encB', 'anB2')):
        pairs.append(('.q(%s), .clk(clk_ADCB), .d1(enc_cur),   .d2(enc_cur)' % pin,
                      '.q(%s), .clk(clk_ADCB), .d1(%s), .d2(%s)' % (pin, an, an)))
elif 'reg swc_enc_n = 1;' in s:                         # 0x1537
    anchor, b, a = 'reg swc_enc_n = 1;', 'swc_enc_n', '~swc_enc_n'
    pairs = [('adc1_encB <= swc_enc_n;', 'adc1_encB <= anB1;'), ('adc2_encB <= swc_enc_n;', 'adc2_encB <= anB2;'),
             ('adc1_encA <= ~swc_enc_n;', 'adc1_encA <= anA1;'), ('adc2_encA <= ~swc_enc_n;', 'adc2_encA <= anA2;')]
elif 'reg enc_nxt_n = 1;' in s:                       # 0x1538
    anchor, b, a = 'reg enc_nxt_n = 1;', 'enc_nxt', 'enc_nxt_n'
    pairs = [('adc1_encB <= enc_nxt;', 'adc1_encB <= anB1;'), ('adc2_encB <= enc_nxt;', 'adc2_encB <= anB2;'),
             ('adc1_encA <= enc_nxt_n;', 'adc1_encA <= anA1;'), ('adc2_encA <= enc_nxt_n;', 'adc2_encA <= anA2;')]
else:
    raise SystemExit('annotate: unknown encode block')
for o, n in pairs:
    assert s.count(o) == 1, o
    s = s.replace(o, n)
i = s.index(anchor) + len(anchor)
w = ('\n  wire anB1, anB2, anA1, anA2;\n  assign #(%s) anB1 = %s;\n  assign #(%s) anB2 = %s;\n'
     '  assign #(%s) anA1 = %s;\n  assign #(%s) anA2 = %s;\n') % (db1, b, db2, b, da1, a, da2, a)
s = s[:i] + w + s[i:]
open(dst, 'w').write(s)
EOF
}
run() {  # <name> <dut.v> <plusargs...>
  local name=$1 dut=$2; shift 2
  iverilog -g2012 $IVDEFS -DSIM -Wall -Wno-timescale -Wno-implicit -s tb -o "$WORK/$name.vvp" "$HERE/sim_models.v" "$dut" "$HERE/tb_encphase.v" \
    2>&1 | grep -v -E 'sensitive to all|implicit|Extra digits|is not used' || true
  vvp -n "$WORK/$name.vvp" +seed=$SEED "$@" > "$WORK/$name.log"
  echo "== $name: $(grep -E 'polarity changed' "$WORK/$name.log") $(grep RESULT "$WORK/$name.log")"
  grep -E '^phase' "$WORK/$name.log" | head -${SHOW:-0}
}
RC=0
annotate "$DUT" "$WORK/src_d35.v" "$(dl $WA -0.25)" "$(dl $WA -0.25)" "$(dl $WB -0.35)" "$(dl $WB -0.35)"
run src_plain "$DUT"; grep -q 'RESULT: .* -> PASS' "$WORK/src_plain.log" || RC=1
run src_d35 "$WORK/src_d35.v"; grep -q 'RESULT: .* -> PASS' "$WORK/src_d35.log" || RC=1
# self-check: CH2 A's D input later than its 3.75 ns window must be caught (0x33 bit 0 and the pin checks fail)
annotate "$DUT" "$WORK/src_a2late.v" "$(dl $WA -0.25)" "$(dl $WA 0.15)" "$(dl $WB -0.35)" "$(dl $WB -0.35)"
run src_a2late "$WORK/src_a2late.v"
if grep -q '0x33 bit 0' "$WORK/src_a2late.log" && grep -q 'RESULT: .* -> FAIL' "$WORK/src_a2late.log"; then
  echo "   (expected FAIL: the self-check caught the late CH2 A flop)"
else echo "   self-check missed the late CH2 A flop"; RC=1; fi
if [ "$MODE" = all ]; then
  getrev "$REV1537" > "$WORK/z1537.v"
  getrev "$REV1535" > "$WORK/z1535.v"
  annotate "$WORK/z1537.v" "$WORK/z1537_a2slow.v" 0.9 1.45 0.8 0.8
  annotate "$WORK/z1537.v" "$WORK/z1537_aslow.v" 1.45 1.45 0.8 0.8
  SHOW=3 run z1535 "$WORK/z1535.v" +exp_ok=0 +has33=0
  SHOW=3 run z1537 "$WORK/z1537.v" +exp_ok=0 +has33=0
  SHOW=3 run z1537_a2slow "$WORK/z1537_a2slow.v" +exp_ok=0 +has33=0
  SHOW=3 run z1537_aslow "$WORK/z1537_aslow.v" +exp_ok=0 +has33=0
fi
[ $RC -eq 0 ] && echo "encphase: PASS" || echo "encphase: FAIL"
exit $RC
