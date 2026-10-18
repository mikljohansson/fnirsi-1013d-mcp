#!/bin/bash
# Fetch the exact toolchain Atlan4 built v1.00o5 with (ELF .comment:
# "GCC: (15:10.3-2021.07-4) 10.3.1 20210621", the Ubuntu 22.04 "jammy"
# package) and unpack it under $PREFIX without installing anything.
# Used by `make gcc10` (the release image) and CI.
# Every .deb is checked against a pinned sha256 before it is unpacked
# (hashes from the downloads that rebuilt v1.00o5 IDENTICAL, 2026-10-18).
#
#   firmware/tools/fetch_gcc10.sh [PREFIX]     (default ~/opt/gcc10)
set -euo pipefail
PREFIX=${1:-$HOME/opt/gcc10}
DL=${PREFIX}-dl
POOL=https://archive.ubuntu.com/ubuntu/pool/universe
# sha256  pool path
DEBS=(
  "236c000f69d94897c3ca9bea619152030b879156f1018be3753d2b9f1a27fd33 g/gcc-arm-none-eabi/gcc-arm-none-eabi_10.3-2021.07-4_amd64.deb"
  "5b2e7d51d169b4e48f25d0d3792be97a4c8409c07d49e78bca929c532d95816d b/binutils-arm-none-eabi/binutils-arm-none-eabi_2.38-3ubuntu1+15build1_amd64.deb"
  "73d10595a2f26c6d663a855b5ee0e9717688313536c0083883576deba0763bf3 n/newlib/libnewlib-dev_3.3.0-1.3_all.deb"
)
mkdir -p "$DL" "$PREFIX"
for entry in "${DEBS[@]}"; do
  sum=${entry%% *}
  d=${entry#* }
  f="$DL/$(basename "$d")"
  [ -s "$f" ] || wget -q -O "$f" "$POOL/$d"
  if ! echo "$sum  $f" | sha256sum -c --quiet -; then
    echo "ERROR: sha256 mismatch for $f (expected $sum); file removed, nothing unpacked" >&2
    rm -f "$f"
    exit 1
  fi
done
for entry in "${DEBS[@]}"; do
  d=${entry#* }
  dpkg -x "$DL/$(basename "$d")" "$PREFIX"
done
"$PREFIX/usr/bin/arm-none-eabi-gcc" --version | head -1
echo "CROSS_COMPILE=$PREFIX/usr/bin/arm-none-eabi-"
echo "newlib headers: -isystem $PREFIX/usr/include/newlib"
