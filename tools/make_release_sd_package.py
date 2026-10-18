#!/usr/bin/env python3
"""Assemble an SD package for the FNIRSI 1013D from Atlan4's v1.00o5 package and our scope image.

Our releases attach only the scope image (GPL source in this repository). The SD boot chain also needs
Atlan4's bootloader "BOOT fw v0.8" (sector 16) and updater v0.02v (sector 80). Both are published only as
binaries, so this script fetches them from Atlan4's repository on your machine instead of us redistributing
them (docs/releasing.md explains why).

Commands:

  build      download Atlan's v1.00o5 package, verify its sha256, replace the scope at sector 750 with
             yours and write an SD package (same layout and trailer as `make package`). --parts DIR also
             writes bootloader.bin, updater.bin and scope.bin for `make_sd_image.py write`.
  reference  write Atlan's scope image and SD package under the names firmware/scope/Makefile expects
             (`make check-reference`, `make package`). Used by CI.

  python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-0.3.0.bin
  python3 make_release_sd_package.py build --scope build-gcc10-pc/fnirsi_1013d.bin --parts parts/
  python3 make_release_sd_package.py build --scope our.bin --local path/to/v1.00o5.zip   # no download
  python3 make_release_sd_package.py reference --dest firmware/reference

--local takes the v1.00o5 zip or a directory that holds its extracted files (searched recursively).
Every source is accepted only when the two files match the pinned sha256 values below.

WARNING: writing a whole SD package from sector 16 (Atlan's `dd ... bs=1024 seek=8`) also zeroes sectors
708-710: calibration, settings and the display/boot-mode sector. Prefer `make_sd_image.py write` with the
parts from --parts, which touches only the sectors of the parts you pass, and back up the card first.

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

# name in the zip -> (size, sha256, name in firmware/reference/)
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
LAST_SECTOR = 2047                                                   # FAT32 must start at 2048 or later
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
    if end > LAST_SECTOR:
        raise SystemExit("%s: ends at sector %d, past %d (the FAT32 partition may start at 2048)"
                         % (path, end, LAST_SECTOR))
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
    print("\nNOTE: dd-ing the whole package from sector 16 zeroes sectors 708-710 "
          "(calibration, settings, display config). See the script header.")
    return 0


def cmd_reference(args):
    atlan = get_atlan_files(args.local, args.url)
    for name, data in atlan.items():
        path = os.path.join(args.dest, EXPECTED[name][2])
        if os.path.isfile(path):
            with open(path, "rb") as f:
                if f.read() == data:
                    print("%s: already present, sha256 %s" % (path, EXPECTED[name][1]))
                    continue
        write_file(path, data)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def source_opts(p):
        p.add_argument("--local", help="Atlan's v1.00o5.zip or a directory with its extracted files (no download)")
        p.add_argument("--url", help="download URL override (default: %s)" % zip_url())

    p = sub.add_parser("build", help="SD package (and optionally parts) with our scope image")
    p.add_argument("--scope", required=True, help="our scope image (eGON.EXE), e.g. fnirsi_1013d-fw-<ver>.bin")
    p.add_argument("--scope-sha256", help="expected sha256 of --scope (from the release SHA256SUMS)")
    p.add_argument("-o", "--output", help="SD package path (default fnirsi_1013d-fw-<ver>-sdpackage.bin)")
    p.add_argument("--parts", help="also write bootloader.bin, updater.bin, scope.bin to this directory")
    source_opts(p)
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("reference", help="Atlan's reference files for make check-reference / make package")
    p.add_argument("--dest", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                  "..", "firmware", "reference"))
    source_opts(p)
    p.set_defaults(func=cmd_reference)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
