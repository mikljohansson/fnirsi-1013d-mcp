#!/usr/bin/env python3
"""Assemble an SD package for the FNIRSI 1013D from Atlan4's v1.00o5 package and our scope image.

Our releases attach only the scope image (GPL source in this repository). The SD boot chain also needs
Atlan4's bootloader "BOOT fw v0.8" (sector 16) and updater v0.02v (sector 80). Both are published only as
binaries, so this script fetches them from Atlan4's repository on your machine instead of us redistributing
them (docs/releasing.md explains why).

Commands:

  build      download Atlan's v1.00o5 package, verify its sha256, replace the scope at sector 750 with
             yours and write an SD package (Atlan's layout and 72-byte flashfilepacker trailer). --parts DIR also
             writes bootloader.bin, updater.bin and scope.bin for `make_sd_image.py write`.

  python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-0.3.0.bin
  python3 make_release_sd_package.py build --scope build-gcc10-pc/fnirsi_1013d.bin --parts parts/
  python3 make_release_sd_package.py build --scope our.bin --local path/to/v1.00o5.zip   # no download

--local takes the v1.00o5 zip or a directory that holds its extracted files (searched recursively).
Every source is accepted only when the two files match the pinned sha256 values below.

WARNING: writing a whole SD package from sector 16 (Atlan's `dd ... bs=1024 seek=8`) also zeroes sectors
708-710: calibration, settings and the display/boot-mode sector. Prefer `make_sd_image.py write` with the
parts from --parts, which touches only the sectors of the parts you pass, and back up the card first.

The card's FAT32 partition must start after the scope image (sector 750 + its length); cards for this firmware start
it at sector 16384 (8 MiB), the end of the firmware's SD write window. `make_sd_image.py write` refuses a card whose
partition starts too low and says what to do.

Needs make_sd_image.py and mksunxi.py next to this script (release zip) or in ../firmware/tools (checkout).
The downloaded zip is treated as untrusted data: only the two named members are read, nothing is extracted.
"""

import argparse
import hashlib
import importlib.util
import io
import os
import re
import sys
import urllib.parse
import urllib.request
import zipfile

ATLAN_REPO = "Atlan4/Fnirsi1013D"
# Last commit that touched the zip (2026-06-21). Pinned so the download cannot change under us.
ATLAN_COMMIT = "b0397d631092ff929a6528bf9b9090b420430226"
ZIP_PATH = "Verzia 1.xxx/DSO 1.xxx/backup dso/v1.00o5.zip"
ZIP_SHA256 = "ab63e7ba2ebb12e0218f9a74f85d4ddd5d6a285a472c973635d71b8aa4f6335f"   # informational
MEMBER_DIR = "fnirsi_1013d_scope/dist/Debug/GNU_ARM-Linux/"

# name in the zip -> (size, sha256, descriptive name)
SCOPE_FILE = "fnirsi_1013d_scope.bin"
PACKAGE_FILE = "fnirsi_1013d.bin"
EXPECTED = {
    SCOPE_FILE: (346112, "d2a6f262163923c3383b34c38a16d0895af90902584d12f2eb7c81292a25c4e8",
                 "fnirsi_1013d_v1.00o5_atlan.bin"),
    PACKAGE_FILE: (721992, "eccd2f6e5b7216e35f868eac22d1c89c87ae51bd2f36feea89d96de93749130a",
                   "fnirsi_1013d_v1.00o5_atlan_sdpackage.bin"),
}

SECTOR = 512
PACKAGE_BASE_SECTOR = 16
UPDATER_SECTOR = 80
SCOPE_SECTOR = 750
SCOPE_LOCATION = (SCOPE_SECTOR - PACKAGE_BASE_SECTOR) * SECTOR      # 0x5BC00, as in the Makefile
# The firmware writes the card only below sector 16384 (PC_SD_LIMIT, fw 0.2.101), so the image must end below it, and
# cards for this firmware have their FAT32 partition at 16384 (8 MiB) or later (firmware/README.md section 3).
SD_LIMIT = 16384
VER_RX = re.compile(rb"#OK,FNIRSI-1013D-AI,fw=([^,\x00]*),proto=")


def zip_url(commit=ATLAN_COMMIT):
    return "https://raw.githubusercontent.com/%s/%s/%s" % (ATLAN_REPO, commit, urllib.parse.quote(ZIP_PATH))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_sd_tools():
    """Import make_sd_image.py (which imports mksunxi.py from its own directory)."""
    here = os.path.dirname(os.path.abspath(__file__))
    for d in (here, os.path.join(here, "..", "firmware", "tools")):
        path = os.path.join(d, "make_sd_image.py")
        if os.path.isfile(path) and os.path.isfile(os.path.join(d, "mksunxi.py")):
            spec = importlib.util.spec_from_file_location("make_sd_image", path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            return mod
    raise SystemExit("make_sd_image.py and mksunxi.py not found next to this script or in ../firmware/tools")


# --------------------------------------------------------------------------- sources

def check_member(name, data, origin):
    size, digest, _ = EXPECTED[name]
    got = sha256(data)
    if len(data) != size or got != digest:
        raise SystemExit("%s: %s is %d bytes sha256 %s, expected %d bytes sha256 %s"
                         % (origin, name, len(data), got, size, digest))
    return data


def from_zip_bytes(blob, origin):
    print("%s: zip %d bytes sha256 %s%s" % (origin, len(blob), sha256(blob),
                                            "" if sha256(blob) == ZIP_SHA256 else " (differs from the pinned zip; members checked)"))
    try:
        z = zipfile.ZipFile(io.BytesIO(blob))
    except zipfile.BadZipFile as e:
        raise SystemExit("%s: not a zip file (%s)" % (origin, e))
    names = z.namelist()
    out = {}
    for name in EXPECTED:
        hits = [n for n in names if n.endswith(MEMBER_DIR + name)]
        if len(hits) != 1:
            raise SystemExit("%s: expected one %s%s in the zip, found %d" % (origin, MEMBER_DIR, name, len(hits)))
        out[name] = check_member(name, z.read(hits[0]), origin)
    return out


def from_directory(root):
    out = {}
    for dirpath, _, files in os.walk(root):
        for name in EXPECTED:
            if name in out or name not in files:
                continue
            path = os.path.join(dirpath, name)
            if os.path.getsize(path) != EXPECTED[name][0]:
                continue
            with open(path, "rb") as f:
                data = f.read()
            if sha256(data) == EXPECTED[name][1]:
                out[name] = data
                print("%s: %s" % (name, path))
    missing = [n for n in EXPECTED if n not in out]
    if missing:
        raise SystemExit("%s: no file with the expected sha256 for %s" % (root, ", ".join(missing)))
    return out


def get_atlan_files(local=None, url=None):
    """{name: bytes} for Atlan's scope image and SD package, sha256-verified."""
    if local:
        if os.path.isdir(local):
            return from_directory(local)
        with open(local, "rb") as f:
            return from_zip_bytes(f.read(), local)
    url = url or zip_url()
    print("downloading %s" % url)
    req = urllib.request.Request(url, headers={"User-Agent": "fnirsi-1013d-mcp make_release_sd_package"})
    with urllib.request.urlopen(req, timeout=120) as r:
        blob = r.read()
    return from_zip_bytes(blob, "download")


# --------------------------------------------------------------------------- package

def egon_part(msi, data, offset, magic, what):
    head = data[offset:offset + 32]
    if head[4:12] != magic:
        raise SystemExit("Atlan package: no %s header for the %s at offset 0x%X" % (magic.decode(), what, offset))
    length = int.from_bytes(head[16:20], "little")
    part = data[offset:offset + length]
    problems = msi.mksunxi.check(part)
    if problems and magic == b"eGON.BT0":
        raise SystemExit("Atlan package: %s %s" % (what, "; ".join(problems)))
    return part


def check_scope(msi, data, path):
    if data[4:12] != b"eGON.EXE":
        raise SystemExit("%s: not an eGON.EXE scope image (bytes 4..11 = %r)" % (path, data[4:12]))
    problems = msi.mksunxi.check(data)
    if problems:
        raise SystemExit("%s: %s" % (path, "; ".join(problems)))
    end = SCOPE_SECTOR + (len(data) + SECTOR - 1) // SECTOR - 1
    if end >= SD_LIMIT:
        raise SystemExit("%s: ends at sector %d, past %d: the firmware writes the card only below sector %d"
                         % (path, end, SD_LIMIT - 1, SD_LIMIT))
    m = VER_RX.search(data)
    return m.group(1).decode("ascii", "replace") if m else None


def write_file(path, data):
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes sha256 %s" % (path, len(data), sha256(data)))


def cmd_build(args):
    msi = load_sd_tools()
    with open(args.scope, "rb") as f:
        scope = f.read()
    if args.scope_sha256 and sha256(scope) != args.scope_sha256.lower():
        raise SystemExit("%s: sha256 %s, expected %s" % (args.scope, sha256(scope), args.scope_sha256))
    version = check_scope(msi, scope, args.scope)
    print("%s: %d bytes sha256 %s, firmware %s" % (args.scope, len(scope), sha256(scope), version or "unknown"))

    atlan = get_atlan_files(args.local, args.url)
    base = atlan[PACKAGE_FILE]
    # Self-check: putting Atlan's own scope back must reproduce Atlan's package byte for byte.
    if msi.flashfilepacker(base, atlan[SCOPE_FILE], SCOPE_LOCATION) != base:
        raise SystemExit("self-check failed: flashfilepacker does not reproduce Atlan's package")
    bootloader = egon_part(msi, base, 0, b"eGON.BT0", "bootloader")
    updater = egon_part(msi, base, (UPDATER_SECTOR - PACKAGE_BASE_SECTOR) * SECTOR, b"eGON.EXE", "updater")

    out = args.output or "fnirsi_1013d-fw-%s-sdpackage.bin" % (version or "custom")
    package = msi.flashfilepacker(base, scope, SCOPE_LOCATION)
    write_file(out, package)
    msi.main(["info", out, "--package"])

    if args.parts:
        for name, data in (("bootloader.bin", bootloader), ("updater.bin", updater), ("scope.bin", scope)):
            write_file(os.path.join(args.parts, name), data)
        p = args.parts.rstrip("/\\")
        print("\nwrite only these parts (after a full card backup):\n"
              "  python3 make_sd_image.py write --device /dev/sdX --bootloader %s/bootloader.bin "
              "--updater %s/updater.bin --scope %s/scope.bin --boot-mode 4" % (p, p, p))
    end = SCOPE_SECTOR + (len(scope) + SECTOR - 1) // SECTOR
    print("\nThe card's FAT32 partition must start at sector %d or later; %d (8 MiB) leaves room for later images. "
          "make_sd_image.py write refuses a card whose partition starts too low." % (end, SD_LIMIT))
    print("NOTE: dd-ing the whole package from sector 16 zeroes sectors 708-710 "
          "(calibration, settings, display config) and overwrites a partition that starts below sector %d. "
          "See the script header." % end)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def source_opts(p):
        p.add_argument("--local", help="Atlan's v1.00o5.zip or a directory with its extracted files (no download)")
        p.add_argument("--url", help="download URL override (default: %s)" % zip_url().replace("%", "%%"))

    p = sub.add_parser("build", help="SD package (and optionally parts) with our scope image")
    p.add_argument("--scope", required=True, help="our scope image (eGON.EXE), e.g. fnirsi_1013d-fw-<ver>.bin")
    p.add_argument("--scope-sha256", help="expected sha256 of --scope (from the release SHA256SUMS)")
    p.add_argument("-o", "--output", help="SD package path (default fnirsi_1013d-fw-<ver>-sdpackage.bin)")
    p.add_argument("--parts", help="also write bootloader.bin, updater.bin, scope.bin to this directory")
    source_opts(p)
    p.set_defaults(func=cmd_build)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
