#!/bin/bash
# ADC capture margins of a placed build (0x153A and later). Usage (in WSL): capture_report.sh [work dir of build.sh]
# Re-times the placed design (FPGA_pr.db) with tb/capture_io.sdc appended and runs tb/capture_margins.py on it.
# The bitstream is not touched. Default work dir: ~/al3-build (build.sh's default).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-$HOME/al3-build}
[ $# -gt 0 ] && shift
TD_HOME=${TD_HOME:-$HOME/anlogic/TD_4.6.8_SP1_116866_NL}
TD_SHIM=${TD_SHIM:-$HOME/anlogic/shim}
O=$W/capture
mkdir -p "$O"
cp "$W/FPGA_pr.db" "$O/"
cat "$W/zaklad.sdc" "$HERE/capture_io.sdc" | sed 's/\r$//' > "$O/io.sdc"
cat > "$O/io.tcl" <<'EOF'
import_device al3_10.db -package LQFP144
import_db FPGA_pr.db
read_sdc io.sdc
report_timing -mode FINAL -net_info -ep_num 2000 -path_num 1 -file io.timing
exit
EOF
( cd "$O" && LD_LIBRARY_PATH="$TD_SHIM:$TD_HOME/lib" "$TD_HOME/bin/td" io.tcl > io.log 2>&1 )
python3 -I "$HERE/capture_margins.py" "$O/io.timing" "$@"
