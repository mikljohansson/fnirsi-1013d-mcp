#!/bin/bash
# Headless TD build of fpga/al3/src (UNTESTED here; adapted from Donwulff's AL3_1014D/build.sh).
# Needs TD 5.0.x Linux with a valid license/Anlogic.lic and the two stub GTK2 libs (see README.md).
# Usage: TD_HOME=~/tools/anlogic-td/TD_5.0.3_28716_NL TD_SHIM=~/tools/anlogic-td/shim ./build.sh [builddir]
set -e
: "${TD_HOME:?set TD_HOME to the TD install dir}"
: "${TD_SHIM:?set TD_SHIM to the dir holding the stub libgtk-x11/libgdk-x11 .so files}"
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${1:-$HERE/build}
mkdir -p "$BUILD"
cat "$HERE/src/al_ip/pll.v" "$HERE/src/al_ip/sample_memory.v" "$HERE/src/zaklad.v" > "$BUILD/all.v"
cp "$HERE/src/zaklad.adc" "$HERE/src/zaklad.sdc" "$BUILD/"
sed "s|@TD@|$TD_HOME|" "$HERE/build.tcl" > "$BUILD/flow.tcl"
cd "$BUILD"
# "sh: Bad fd number" lines are TD shelling bashisms at dash; cosmetic.
LD_LIBRARY_PATH="$TD_SHIM:$TD_HOME/lib" "$TD_HOME/bin/td" flow.tcl > build.log 2>&1 || {
  echo "BUILD FAILED:"; grep -iE 'ERROR' build.log | grep -v 'Bad fd' | head -20; exit 1; }
grep -A8 'Timing group' timing.rpt | head -11
ls -la FPGA.bit FPGA.bin
