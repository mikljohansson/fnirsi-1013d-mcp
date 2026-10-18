#!/usr/bin/env python3
"""Assemble, inspect and update FNIRSI 1013D open-firmware SD card images.

SD layout used by the Atlan4 "BOOT fw v0.8" bootloader chain (512 B sectors;
see firmware/README.md for the sources this is confirmed from):

  sector   0       MBR (partition table; must stay intact)
  sector  16..79   bootloader, eGON.BT0, max 32 KiB (BROM checks checksum)
  sector  80..707  updater,    eGON.EXE (boot mode 3)
  sector 708       calibration data
  sector 709       scope settings
  sector 710       display/touch config; byte 28/29 TP X/Y swap; byte 31 boot mode
  sector 750..     scope,      eGON.EXE (boot mode 0); must end before the
                   first partition (FAT32 normally starts at sector 2048)

Boot mode byte (sector 710 offset 31): 0 = PECO/Atlan scope from sector 750,
1 = stock FNIRSI firmware from SPI flash, 2 = FEL, 3 = updater from sector 80,
> 3 = show the touch menu (preselect = value & 3).

An "SD package" (upstream fnirsi_1013d.bin, what `dd ... bs=1024 seek=8`
writes) is the same layout starting at sector 16, i.e. package offset =
(sector - 16) * 512. NOTE: dd-ing a full package also overwrites sectors
708-710 (zeros in Atlan's package), destroying calibration, settings and the
display config. `write` below only touches the sectors of the parts you pass.

Commands:
  info    IMAGE                      show what is at each sector
  extract IMAGE -d DIR               dump parts (bootloader/updater/scope/708-710)
  pack    -o OUT -i IN -l LOCATION   byte-compatible flashfilepacker
  write   (--image FILE | --device DEV) [parts...]

`write` targets an image file by default. Writing to a physical device needs
--device, an interactive confirmation (type the device path), and always
saves a backup of sectors 0..(end of written area) to --backup-dir first and
verifies by reading back. Never use it without a full card backup.
"""

import argparse
import datetime
import os
import struct
import sys

SECTOR = 512
PACKAGE_BASE_SECTOR = 16

BOOTLOADER_SECTOR = 16
UPDATER_SECTOR = 80
CALIBRATION_SECTOR = 708
SETTINGS_SECTOR = 709
DISPLAY_CONFIG_SECTOR = 710
SCOPE_SECTOR = 750
BOOT_MODE_OFFSET = 31            # inside sector 710 (DRAM 0x81BFFC1F)

BOOT_MODES = {0: "PECO/Atlan scope (SD sector 750)", 1: "FNIRSI stock (SPI flash 0x27000)",
              2: "FEL", 3: "updater (SD sector 80)"}

# (name, first sector, last sector inclusive or None, expected magic)
REGIONS = [
    ("bootloader", BOOTLOADER_SECTOR, UPDATER_SECTOR - 1, b"eGON.BT0"),
    ("updater", UPDATER_SECTOR, CALIBRATION_SECTOR - 1, b"eGON.EXE"),
    ("calibration", CALIBRATION_SECTOR, CALIBRATION_SECTOR, None),
    ("settings", SETTINGS_SECTOR, SETTINGS_SECTOR, None),
    ("display_config", DISPLAY_CONFIG_SECTOR, DISPLAY_CONFIG_SECTOR, None),
    ("scope", SCOPE_SECTOR, None, b"eGON.EXE"),
]
REGION = {r[0]: r for r in REGIONS}

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mksunxi  # noqa: E402


# --------------------------------------------------------------------------- flashfilepacker

def flashfilepacker(outdata, indata, location):
    """Exact re-implementation of pecostm32's flashfilepacker.c.

    Output = outdata[:location] (zero-filled up to location) + indata + 72
    bytes: 36 zero bytes, then 9 big-endian u32 check words over the first
    location+len(indata) bytes: word0 = sum of bytes, words1..8 = for bit 7..0
    the sum of the indexes of bytes having that bit set (the stock FNIRSI
    update program verifies these on a firmware update file). The SD
    bootloaders ignore the trailer.
    """
    lastbyte = location + len(indata)
    length = lastbyte + 72
    buf = bytearray(length)
    keep = min(len(outdata), location)
    buf[:keep] = outdata[:keep]
    buf[location:lastbyte] = indata
    check = [0] * 9
    for index in range(lastbyte):
        byte = buf[index]
        if not byte:
            continue
        check[0] += byte
        for bit in range(8):
            if byte & (0x80 >> bit):
                check[bit + 1] += index
    pos = length - 36
    for value in check:
        struct.pack_into(">I", buf, pos, value & 0xFFFFFFFF)
        pos += 4
    return bytes(buf)


def parse_int(text):
    return int(text, 0)


# --------------------------------------------------------------------------- image access

def is_device_path(path):
    p = path.replace("\\", "/").lower()
    return p.startswith("/dev/") or p.startswith("//./") or "physicaldrive" in p


class Target:
    """Sector access to an image file, an SD package file or a raw device."""

    def __init__(self, path, writable=False, package=None):
        self.path = path
        self.f = open(path, "r+b" if writable else "rb")
        if package is None:
            head = self.read_bytes(0, SECTOR)
            package = head[4:12] == b"eGON.BT0"
        self.package = package
        self.base = PACKAGE_BASE_SECTOR if package else 0

    def close(self):
        self.f.close()

    def size(self):
        self.f.seek(0, os.SEEK_END)
        return self.f.tell()

    def read_bytes(self, offset, length):
        self.f.seek(offset)
        return self.f.read(length)

    def sector_offset(self, sector):
        if sector < self.base:
            raise ValueError("sector %d is not inside an SD package (starts at %d)" % (sector, self.base))
        return (sector - self.base) * SECTOR

    def read_sectors(self, sector, count):
        data = self.read_bytes(self.sector_offset(sector), count * SECTOR)
        return data + bytes(count * SECTOR - len(data))

    def write_sectors(self, sector, data):
        assert len(data) % SECTOR == 0
        self.f.seek(self.sector_offset(sector))
        self.f.write(data)
        self.f.flush()
        os.fsync(self.f.fileno())


def egon_at(t, sector):
    try:
        head = t.read_sectors(sector, 1)
    except ValueError:
        return None
    magic = head[4:12]
    if magic not in (b"eGON.BT0", b"eGON.EXE"):
        return None
    length = struct.unpack_from("<I", head, 16)[0]
    return magic, length, struct.unpack_from("<I", head, 12)[0]


def mbr_partitions(t):
    """Return list of (type, first_lba, count) or None when no MBR is present."""
    if t.package:
        return None
    mbr = t.read_bytes(0, SECTOR)
    if len(mbr) < SECTOR or mbr[510:512] != b"\x55\xaa":
        return None
    parts = []
    for i in range(4):
        e = mbr[446 + 16 * i: 446 + 16 * (i + 1)]
        ptype = e[4]
        first, count = struct.unpack_from("<II", e, 8)
        if ptype:
            parts.append((ptype, first, count))
    return parts


def sector_name(sector):
    for name, first, last, _ in REGIONS:
        if sector >= first and (last is None or sector <= last):
            return name
    return "?"


# --------------------------------------------------------------------------- commands

def cmd_info(args):
    t = Target(args.image, package=args.package)
    print("%s: %s, %d bytes" % (args.image, "SD package (starts at sector 16)" if t.package
                               else "card image (starts at sector 0)", t.size()))
    parts = mbr_partitions(t)
    if parts is not None:
        for ptype, first, count in parts:
            print("  MBR partition type 0x%02X: sectors %d..%d" % (ptype, first, first + count - 1))
    for name, first, last, magic in REGIONS:
        if magic:
            e = egon_at(t, first)
            if not e:
                print("  %-14s sector %4d: (no eGON header)" % (name, first))
                continue
            m, length, csum = e
            data = t.read_sectors(first, (length + SECTOR - 1) // SECTOR)
            ok = not mksunxi.check(data)
            end = first + (length + SECTOR - 1) // SECTOR - 1
            over = last is not None and end > last
            print("  %-14s sector %4d..%4d: %s length 0x%X (%d B) checksum 0x%08X %s%s"
                  % (name, first, end, m.decode(), length, length, csum,
                     "OK" if ok else "MISMATCH" + (" (not checked by bootloader)" if m == b"eGON.EXE" else ""),
                     "  OVERLAPS sector %d!" % (last + 1) if over else ""))
            if parts and name == "scope":
                for ptype, pfirst, count in parts:
                    if pfirst <= end and pfirst + count > first:
                        print("    WARNING: scope overlaps partition at sector %d" % pfirst)
        else:
            data = t.read_sectors(first, 1)
            if not any(data):
                state = "all zero"
            elif all(b == 0xFF for b in data):
                state = "all 0xFF"
            else:
                state = "data"
            extra = ""
            if name == "display_config" and any(data):
                w = struct.unpack_from("<7I", data, 0)
                valid = (w[0] == 0xAAAAAAAA and w[1] == 0x55555555 and w[4] == 0xCCCCCCCC
                         and w[5] == 0x33333333 and w[6] == (sum(w[:6]) & 0xFFFFFFFF))
                mode = data[BOOT_MODE_OFFSET]
                desc = BOOT_MODES.get(mode, "menu, preselect %s" % BOOT_MODES[mode & 3])
                extra = (" display cfg %s (timing1 0x%08X timing2 0x%08X), TP swap x=0x%02X y=0x%02X,"
                         " boot mode %d = %s" % ("valid" if valid else "INVALID/default", w[2], w[3],
                                                  data[28], data[29], mode, desc))
            print("  %-14s sector %4d: %s%s" % (name, first, state, extra))
    t.close()
    return 0


def cmd_extract(args):
    t = Target(args.image, package=args.package)
    os.makedirs(args.dir, exist_ok=True)
    for name, first, last, magic in REGIONS:
        if magic:
            e = egon_at(t, first)
            if not e:
                continue
            data = t.read_sectors(first, (e[1] + SECTOR - 1) // SECTOR)[: e[1]]
        else:
            data = t.read_sectors(first, 1)
        out = os.path.join(args.dir, "%s.bin" % name)
        with open(out, "wb") as f:
            f.write(data)
        print("%s: %d bytes from sector %d" % (out, len(data), first))
    t.close()
    return 0


def cmd_pack(args):
    with open(args.in_file, "rb") as f:
        indata = f.read()
    outdata = b""
    if os.path.exists(args.out_file):
        with open(args.out_file, "rb") as f:
            outdata = f.read()
    result = flashfilepacker(outdata, indata, args.location)
    with open(args.out_file, "wb") as f:
        f.write(result)
    print("%s: %d bytes (%s at 0x%X)" % (args.out_file, len(result), args.in_file, args.location))
    return 0


def load_part(name, path):
    with open(path, "rb") as f:
        data = f.read()
    _, first, last, magic = REGION[name]
    if magic:
        if data[4:12] != magic:
            raise SystemExit("%s: expected %s header, found %r" % (path, magic.decode(), data[4:12]))
        problems = mksunxi.check(data)
        if problems:
            raise SystemExit("%s: %s (run mksunxi.py on it)" % (path, "; ".join(problems)))
        length = struct.unpack_from("<I", data, 16)[0]
        data = data[:length]
    elif len(data) != SECTOR:
        raise SystemExit("%s: %s must be exactly 512 bytes, got %d" % (path, name, len(data)))
    data += bytes(-len(data) % SECTOR)
    count = len(data) // SECTOR
    if last is not None and first + count - 1 > last:
        raise SystemExit("%s: %s is %d sectors, does not fit sectors %d..%d"
                         % (path, name, count, first, last))
    return first, data


def cmd_write(args):
    if args.device and args.image:
        raise SystemExit("use either --image or --device, not both")
    path = args.device or args.image
    if not path:
        raise SystemExit("need --image FILE (or --device DEV for a physical card)")
    if args.image and is_device_path(args.image):
        raise SystemExit("%s looks like a device; use --device for physical media" % args.image)

    writes = {}   # sector -> bytes
    for name in ("bootloader", "updater", "calibration", "settings", "display_config", "scope"):
        p = getattr(args, name)
        if p:
            first, data = load_part(name, p)
            writes[first] = (name, data)
    if args.boot_mode is None and not writes:
        raise SystemExit("nothing to write")

    if args.image and not os.path.exists(args.image):
        if not args.create:
            raise SystemExit("%s does not exist (pass --create to make a new zero-filled image)" % args.image)
        with open(args.image, "wb") as f:
            f.truncate(args.create_size)
        print("created %s (%d bytes, no partition table)" % (args.image, args.create_size))

    t = Target(path, writable=True, package=args.package if args.image else False)
    if t.package:
        print("note: %s is an SD package (offset 0 = sector 16)" % path)

    # boot mode edits sector 710 (merged with a new display config if given)
    if args.boot_mode is not None:
        if DISPLAY_CONFIG_SECTOR in writes:
            sec = bytearray(writes[DISPLAY_CONFIG_SECTOR][1])
        else:
            sec = bytearray(t.read_sectors(DISPLAY_CONFIG_SECTOR, 1))
        sec[BOOT_MODE_OFFSET] = args.boot_mode
        writes[DISPLAY_CONFIG_SECTOR] = ("display_config(boot mode %d)" % args.boot_mode, bytes(sec))

    end_sector = max(s + len(d) // SECTOR for s, (_, d) in writes.items())

    parts = mbr_partitions(t)
    if parts:
        if any(p[0] == 0xEE for p in parts):
            raise SystemExit("GPT disk: GPT entries live in sectors 2..33 and clash with the bootloader at 16")
        for ptype, pfirst, count in parts:
            for s, (name, d) in writes.items():
                if s < pfirst + count and s + len(d) // SECTOR > pfirst:
                    raise SystemExit("%s (sectors %d..%d) overlaps partition at sector %d"
                                     % (name, s, s + len(d) // SECTOR - 1, pfirst))
    elif args.device:
        raise SystemExit("%s has no MBR partition table; refusing to write a device without one" % path)

    for s in sorted(writes):
        name, d = writes[s]
        print("  %-28s -> sectors %d..%d (%d bytes)" % (name, s, s + len(d) // SECTOR - 1, len(d)))

    if args.device:
        print("\nAbout to WRITE RAW SECTORS on physical device %s." % path)
        print("Make sure you have a full backup of this card first.")
        answer = input("Type the device path again to confirm: ").strip()
        if answer != path:
            raise SystemExit("not confirmed, nothing written")

    if args.device or args.backup:
        os.makedirs(args.backup_dir, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        first = t.base
        backup = os.path.join(args.backup_dir, "sd_sectors_%d-%d_%s.bin" % (first, end_sector - 1, stamp))
        data = t.read_sectors(first, end_sector - first)
        with open(backup, "wb") as f:
            f.write(data)
        with open(backup, "rb") as f:
            if f.read() != data:
                raise SystemExit("backup verify failed, nothing written")
        print("backup of sectors %d..%d saved to %s" % (first, end_sector - 1, backup))

    if args.dry_run:
        print("dry run, nothing written")
        t.close()
        return 0

    for s in sorted(writes):
        t.write_sectors(s, writes[s][1])
    bad = [writes[s][0] for s in sorted(writes)
           if t.read_sectors(s, len(writes[s][1]) // SECTOR) != writes[s][1]]
    t.close()
    if bad:
        print("READ-BACK MISMATCH: %s" % ", ".join(bad), file=sys.stderr)
        return 1
    print("written and verified")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="FNIRSI 1013D SD image tool",
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def pkg_opt(p):
        g = p.add_mutually_exclusive_group()
        g.add_argument("--package", dest="package", action="store_true", default=None,
                       help="treat file as SD package (offset 0 = sector 16)")
        g.add_argument("--card", dest="package", action="store_false",
                       help="treat file as card image (offset 0 = sector 0)")

    p = sub.add_parser("info", help="show layout of a card image / package")
    p.add_argument("image")
    pkg_opt(p)
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("extract", help="dump parts to a directory")
    p.add_argument("image")
    p.add_argument("-d", "--dir", required=True)
    pkg_opt(p)
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("pack", help="flashfilepacker-compatible packer")
    p.add_argument("-o", "--out-file", required=True)
    p.add_argument("-i", "--in-file", required=True)
    p.add_argument("-l", "--location", required=True, type=parse_int)
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("write", help="write parts at their sectors")
    p.add_argument("--image", help="card image or SD package file")
    p.add_argument("--device", help="PHYSICAL device (/dev/sdX, \\\\.\\PhysicalDriveN); asks for confirmation")
    p.add_argument("--create", action="store_true", help="create --image if missing")
    p.add_argument("--create-size", type=parse_int, default=2048 * SECTOR,
                   help="size for --create (default 1 MiB = sectors 0..2047)")
    p.add_argument("--bootloader", help="eGON.BT0 image for sector 16")
    p.add_argument("--updater", help="eGON.EXE image for sector 80")
    p.add_argument("--scope", help="eGON.EXE image for sector 750 (build/fnirsi_1013d.bin)")
    p.add_argument("--calibration", help="512 B sector 708")
    p.add_argument("--settings", help="512 B sector 709")
    p.add_argument("--display-config", help="512 B sector 710")
    p.add_argument("--boot-mode", type=parse_int, choices=range(256), metavar="N",
                   help="set sector 710 byte 31 (0 scope, 1 stock, 2 FEL, 3 updater, >3 menu)")
    p.add_argument("--backup", action="store_true", help="also back up before writing an image file")
    p.add_argument("--backup-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                         "..", "..", "backups"))
    p.add_argument("--dry-run", action="store_true")
    pkg_opt(p)
    p.set_defaults(func=cmd_write)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
