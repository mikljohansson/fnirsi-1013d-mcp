#!/bin/bash
# Fetch the exact toolchain Atlan4 built v1.00o5 with (ELF .comment:
# "GCC: (15:10.3-2021.07-4) 10.3.1 20210621", the Ubuntu 22.04 "jammy"
# package) and unpack it under $PREFIX without installing anything.
# Used by `make check-reference` to prove the tree reproduces Atlan's binary.
#
#   firmware/tools/fetch_gcc10.sh [PREFIX]     (default ~/opt/gcc10)
set -euo pipefail
PREFIX=${1:-$HOME/opt/gcc10}
DL=${PREFIX}-dl
POOL=http://archive.ubuntu.com/ubuntu/pool/universe
DEBS=(
  g/gcc-arm-none-eabi/gcc-arm-none-eabi_10.3-2021.07-4_amd64.deb
  b/binutils-arm-none-eabi/binutils-arm-none-eabi_2.38-3ubuntu1+15build1_amd64.deb
  n/newlib/libnewlib-dev_3.3.0-1.3_all.deb
)
mkdir -p "$DL" "$PREFIX"
for d in "${DEBS[@]}"; do
  f="$DL/$(basename "$d")"
  [ -s "$f" ] || wget -q -O "$f" "$POOL/$d"
  dpkg -x "$f" "$PREFIX"
done
"$PREFIX/usr/bin/arm-none-eabi-gcc" --version | head -1
echo "CROSS_COMPILE=$PREFIX/usr/bin/arm-none-eabi-"
echo "newlib headers: -isystem $PREFIX/usr/include/newlib"
