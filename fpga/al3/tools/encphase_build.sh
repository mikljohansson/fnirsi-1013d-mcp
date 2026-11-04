#!/bin/bash
# Build one encode-phase experiment image (README section 19) in WSL:
#   tools/encphase_build.sh <root> <name> [encphase_variant.py options: --iob --phase-ps N --move S A ...]
# Makes <root>/<name>/tree (build.sh, build.tcl, tb and the variant src of this checkout), builds it into <root>/<name>/w
# and writes <root>/<name>/summary.txt: the image's sha256, timing per clock, area (#oreg: 15 with --iob), the encode pad
# delays and the capture margins (tb/capture_report.sh --bits). Builds of different names may run in parallel.
set -e
AL3=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$1; N=$2; shift 2
B=$ROOT/$N
rm -rf "$B"; mkdir -p "$B/tree"
cp -r "$AL3/build.sh" "$AL3/build.tcl" "$AL3/tb" "$B/tree/"
python3 -I "$AL3/tools/encphase_variant.py" "$B/tree/src" --src "$AL3/src" "$@" > "$B/variant.txt"
bash "$B/tree/build.sh" "$B/w" > "$B/build.out" 2>&1 || { echo "$N BUILD FAILED"; tail -5 "$B/build.out"; exit 1; }
{
  echo "== $N  sha256 $(sha256sum "$B/w/FPGA.bin" | cut -c1-64)  $(cat "$B/variant.txt")"
  grep -E 'Timing constraint: clock|errors detected|Minimum period' "$B/w/FPGA_phy.timing" | sed 's/^ *//' | paste - - - \
    | sed -E 's/Timing constraint: clock: //'
  grep -E '#lut|#reg|#le |#oreg' "$B/w/FPGA_phy.area" | tr -s ' '
  grep -E 'o_adc[12]_enc' "$B/w/FPGA_phy.area" | tr -s ' '
  bash "$B/tree/tb/capture_report.sh" "$B/w" --bits
} > "$B/summary.txt" 2>&1
echo "$N done"
