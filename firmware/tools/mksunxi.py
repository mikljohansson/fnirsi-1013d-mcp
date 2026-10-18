#!/usr/bin/env python3
"""Fix the Allwinner eGON boot header (length + checksum) of a raw image.

Python re-implementation of the `mksunxi` helper that pecostm32 and Atlan4
commit as a prebuilt x86-64 ELF next to every project (it derives from
xboot's tools/mksunxi). Behaviour reproduced from black-box tests against that
binary (md5 d900017a6d037250e313ee3d9601678a), byte-identical on all inputs
where the binary's behaviour is defined:

  * input must be larger than 96 bytes (sizeof(struct boot_head_t)), else
    error, file untouched
  * the file is zero-padded to a multiple of 512 bytes
  * header length (offset 0x10, u32 LE) is rounded up to a multiple of 512
    and written back
  * checksum (offset 0x0C, u32 LE): set to the stamp 0x5F0A6C39, then the
    sum (mod 2^32) of the first `length` bytes taken as u32 LE words is
    stored there
  * the magic is not checked: eGON.BT0 (BROM SPL) and eGON.EXE (images loaded
    by the open bootloader) are both processed the same way

Difference from the binary: when the (rounded) header length is larger than
the padded file, the C tool sums past the end of its buffer (undefined
result); this script refuses that case instead.

Only the BROM checks the checksum (eGON.BT0 at SD sector 16). The Atlan4 /
pecostm32 bootloaders check only the "eGON.EXE" magic and the length field of
the images at sectors 80 and 750.

Usage:
  mksunxi.py IMAGE              fix IMAGE in place (like the binary)
  mksunxi.py IMAGE -o OUT       write the fixed image to OUT
  mksunxi.py --check IMAGE      verify header length/checksum, exit 1 if bad
"""

import argparse
import struct
import sys

HEAD_SIZE = 96          # sizeof(struct boot_head_t) in xboot's mksunxi
CHECKSUM_STAMP = 0x5F0A6C39
BLOCK = 512


def align(value, to=BLOCK):
    return (value + to - 1) & ~(to - 1)


def egon_checksum(buf, length):
    """Return the eGON checksum of buf[:length] with the stamp in the field."""
    tmp = bytearray(buf[:length])
    struct.pack_into("<I", tmp, 12, CHECKSUM_STAMP)
    words = struct.unpack_from("<%dI" % (length // 4), tmp, 0)
    return sum(words) & 0xFFFFFFFF


def fix(data):
    """Return the fixed image for raw input bytes (raises ValueError)."""
    if len(data) <= HEAD_SIZE:
        raise ValueError("image too small (%d bytes, need > %d)" % (len(data), HEAD_SIZE))
    buf = bytearray(align(len(data)))
    buf[: len(data)] = data
    length = align(struct.unpack_from("<I", buf, 16)[0])
    if length > len(buf):
        raise ValueError(
            "header length 0x%X exceeds padded image size 0x%X" % (length, len(buf)))
    struct.pack_into("<I", buf, 16, length)
    struct.pack_into("<I", buf, 12, egon_checksum(buf, length))
    return bytes(buf)


def check(data):
    """Return a list of problems with an already fixed image (empty = OK)."""
    problems = []
    if len(data) <= HEAD_SIZE:
        return ["image too small"]
    magic = data[4:12]
    if magic not in (b"eGON.BT0", b"eGON.EXE"):
        problems.append("unexpected magic %r" % magic)
    length = struct.unpack_from("<I", data, 16)[0]
    if length % BLOCK or length > len(data):
        problems.append("bad length field 0x%X (file 0x%X)" % (length, len(data)))
        return problems
    stored = struct.unpack_from("<I", data, 12)[0]
    calc = egon_checksum(data, length)
    if stored != calc:
        problems.append("checksum 0x%08X, expected 0x%08X" % (stored, calc))
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image")
    ap.add_argument("-o", "--output", help="output file (default: fix in place)")
    ap.add_argument("--check", action="store_true", help="only verify the header")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)

    with open(args.image, "rb") as f:
        data = f.read()

    if args.check:
        problems = check(data)
        for p in problems:
            print("%s: %s" % (args.image, p), file=sys.stderr)
        if not problems and not args.quiet:
            print("%s: %s length 0x%X checksum OK" % (
                args.image, data[4:12].decode("ascii", "replace"),
                struct.unpack_from("<I", data, 16)[0]))
        return 1 if problems else 0

    try:
        out = fix(data)
    except ValueError as e:
        print("mksunxi: %s: %s" % (args.image, e), file=sys.stderr)
        return 1
    with open(args.output or args.image, "wb") as f:
        f.write(out)
    if not args.quiet:
        print("mksunxi: %s length 0x%X checksum 0x%08X" % (
            args.output or args.image, struct.unpack_from("<I", out, 16)[0],
            struct.unpack_from("<I", out, 12)[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
