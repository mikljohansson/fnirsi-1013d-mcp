#!/bin/bash
# Headless TD build of fpga/al3/src (Atlan4's AL3A10 project plus our changes, see README.md "Changes").
# Adapted from Donwulff's AL3_1014D/build.sh. Setup: td/td_install.sh, see TD_SETUP.md.
# Usage (in WSL): ./build.sh [workdir]
#   TD_HOME, TD_SHIM default to the td_install.sh locations under ~/anlogic.
#   SRC_REV=<git rev> builds fpga/al3/src as it was at that commit instead of the working tree,
#   e.g. SRC_REV=a947e78 rebuilds Atlan4's unmodified 0x1532 design (FPGA.bin sha256 0d4c829c...).
#   The build runs in a WSL-local workdir (default ~/al3-build; /mnt/d is slow) and the
#   bitstreams, reports and log are copied to fpga/al3/build/ (gitignored).
set -e
TD_HOME=${TD_HOME:-$HOME/anlogic/TD_4.6.8_SP1_116866_NL}
FLOW=${FLOW:-atlan}   # atlan = build.tcl (replay of Atlan4 GUI flow), donwulff = build_donwulff.tcl
TD_SHIM=${TD_SHIM:-$HOME/anlogic/shim}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:-$HOME/al3-build}
OUT=$HERE/build
[ -x "$TD_HOME/bin/td" ] || { echo "no TD at $TD_HOME (run td/td_install.sh)"; exit 1; }
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"
if [ -n "$SRC_REV" ]; then
  REPO=$(cd "$HERE/../.." && pwd)
  git -c safe.directory='*' -C "$REPO" archive "$SRC_REV" fpga/al3/src | tar -x -C "$WORK" --strip-components=3
  echo "sources: fpga/al3/src at $SRC_REV"
else
  cp -r "$HERE/src/." "$WORK/"
fi
cat "$WORK/al_ip/pll.v" "$WORK/al_ip/sample_memory.v" "$WORK/zaklad.v" > "$WORK/all.v"
TCL=$HERE/build.tcl; [ "$FLOW" = donwulff ] && TCL=$HERE/build_donwulff.tcl
UCODE=$(grep -o '<UCode>[01]*</UCode>' "$WORK/FPGA.al" | tr -dc 01)
[ ${#UCODE} -eq 8 ] || { echo "no 8-bit <UCode> in src/FPGA.al"; exit 1; }
sed -e "s|@TD@|$TD_HOME|" -e "s|@UCODE@|$UCODE|" "$TCL" > "$WORK/flow.tcl"
cd "$WORK"
T0=$(date +%s)
# "sh: Bad fd number" lines are TD shelling bashisms at dash; cosmetic.
set +e
LD_LIBRARY_PATH="$TD_SHIM:$TD_HOME/lib" "$TD_HOME/bin/td" flow.tcl > build.log 2>&1
RC=$?
set -e
T1=$(date +%s)
echo "td exit $RC, wall clock $((T1-T0)) s" | tee -a build.log
cp -f build.log *.rpt *.area *.timing *.tsm FPGA.bit FPGA.bin "$OUT/" 2>/dev/null || true
if [ $RC -ne 0 ] || [ ! -s FPGA.bin ]; then
  echo "BUILD FAILED:"; grep -iE 'ERROR' build.log | grep -v 'Bad fd' | head -20; exit 1
fi
grep -iE 'ERROR' build.log | grep -v 'Bad fd' | head -10 || true
cat FPGA_phy.tsm 2>/dev/null | head -40 || grep -A8 'Timing group' timing.rpt | head -11 || true
ls -l FPGA.bit FPGA.bin
sha256sum FPGA.bit FPGA.bin
echo "outputs copied to $OUT"
