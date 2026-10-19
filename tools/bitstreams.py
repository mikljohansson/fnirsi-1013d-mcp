#!/usr/bin/env python3
"""Check and package the committed FPGA bitstreams in fpga/al3/dist (manifest.json, BITSTREAMS.md).

The AL3A10 bitstreams are build outputs of fpga/al3/src (Anlogic TD, not available in CI), so the released
images are committed under fpga/al3/dist with their sha256 and source commit in manifest.json. This script
is the gate between that directory and a release:

  python3 -I tools/bitstreams.py check              # CI: files, hashes, headers, manifest <-> BITSTREAMS.md
  python3 -I tools/bitstreams.py check --release    # release.yml: also one benched "recommended" image, no placeholders
  python3 -I tools/bitstreams.py pack --out dist    # release.yml: raw .bin + 1 MiB flash image per bitstream, BITSTREAMS.md

pack writes, for every entry whose status is not "withdrawn":
  fnirsi_1013d-fpga-al3-<version>.bin           the bitstream as TD's bitgen wrote it (282,898 bytes)
  fnirsi_1013d-fpga-al3-<version>-flash1m.bin   the same padded with 0xFF to the 1 MiB of the 25Q80 config flash
and checks the padded image against flash_image_sha256 before it writes it. Exit 1 with a message on any mismatch.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "fpga/al3/dist"
STATUSES = ("recommended", "supported", "unbenched", "withdrawn")
# 32 bytes of 0xFF, the AL3 sync word, then the device-id packet of the AL3A10 (IDCODE 0x18006C31).
HEADER = b"\xff" * 32 + bytes.fromhex("cc55aa33f000000618006c31")
FIELDS = ("version", "file", "size", "sha256", "flash_image_sha256", "source_commit", "built_with",
          "min_firmware", "features", "status", "bench")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load(dist=DIST):
    manifest = json.loads((dist / "manifest.json").read_text(encoding="utf-8"))
    return manifest, (dist / "BITSTREAMS.md").read_text(encoding="utf-8")


def flash_image(raw, flash_size):
    return raw + b"\xff" * (flash_size - len(raw))


def check(dist=DIST, release=False):
    """Return a list of error strings; empty means the directory is consistent."""
    errors = []
    manifest, doc = load(dist)
    flash_size = manifest["flash_size"]
    entries = manifest["bitstreams"]
    seen_files = set()
    versions = [e.get("version") for e in entries]
    if len(set(versions)) != len(versions):
        errors.append("manifest: duplicate versions %r" % versions)
    for e in entries:
        tag = e.get("version", "?")
        missing = [f for f in FIELDS if f not in e]
        if missing:
            errors.append("%s: manifest entry lacks %s" % (tag, ", ".join(missing)))
            continue
        if not re.fullmatch(r"0x15[0-9a-f]{2}", e["version"], re.I):
            errors.append("%s: version must look like 0x15xx" % tag)
        if e["status"] not in STATUSES:
            errors.append("%s: status %r is not one of %s" % (tag, e["status"], ", ".join(STATUSES)))
        if not re.fullmatch(r"[0-9a-f]{40}", e["source_commit"]):
            errors.append("%s: source_commit must be a full 40-digit commit hash" % tag)
        seen_files.add(e["file"])
        path = dist / e["file"]
        if not path.is_file():
            errors.append("%s: %s is missing" % (tag, path))
            continue
        raw = path.read_bytes()
        if len(raw) != e["size"]:
            errors.append("%s: %s is %d bytes, manifest says %d" % (tag, e["file"], len(raw), e["size"]))
        if sha256(raw) != e["sha256"]:
            errors.append("%s: %s sha256 %s, manifest says %s" % (tag, e["file"], sha256(raw), e["sha256"]))
        if not raw.startswith(HEADER):
            errors.append("%s: %s does not start with the AL3A10 header (32 x FF, cc55aa33 f0000006 18006c31)"
                          % (tag, e["file"]))
        if len(raw) > flash_size:
            errors.append("%s: %s is larger than the %d-byte flash" % (tag, e["file"], flash_size))
        elif sha256(flash_image(raw, flash_size)) != e["flash_image_sha256"]:
            errors.append("%s: the 0xFF-padded image hashes to %s, manifest says %s"
                          % (tag, sha256(flash_image(raw, flash_size)), e["flash_image_sha256"]))
        for what in ("version", "sha256", "flash_image_sha256"):
            if e[what] not in doc:
                errors.append("%s: BITSTREAMS.md does not mention %s %s" % (tag, what, e[what]))
    for stray in sorted(p.name for p in dist.glob("*.bin")):
        if stray not in seen_files:
            errors.append("%s is in %s but not in manifest.json" % (stray, dist))
    recommended = [e for e in entries if e.get("status") == "recommended"]
    if len(recommended) > 1:
        errors.append("more than one recommended bitstream: %s" % ", ".join(e["version"] for e in recommended))
    if release:
        if len(recommended) != 1:
            errors.append("release: exactly one bitstream must have status 'recommended' (found %d)" % len(recommended))
        elif not recommended[0].get("bench"):
            errors.append("release: the recommended bitstream %s has no bench record" % recommended[0]["version"])
        if "{{" in doc:
            errors.append("release: BITSTREAMS.md still has {{placeholders}}")
    return errors


def pack(out, dist=DIST):
    manifest, _ = load(dist)
    out.mkdir(parents=True, exist_ok=True)
    written = []
    for e in manifest["bitstreams"]:
        if e["status"] == "withdrawn":
            continue
        raw = (dist / e["file"]).read_bytes()
        padded = flash_image(raw, manifest["flash_size"])
        if sha256(raw) != e["sha256"] or sha256(padded) != e["flash_image_sha256"]:
            raise SystemExit("%s: hash mismatch, run `bitstreams.py check`" % e["version"])
        base = "fnirsi_1013d-fpga-al3-%s" % e["version"].lower()
        (out / (base + ".bin")).write_bytes(raw)
        (out / (base + "-flash1m.bin")).write_bytes(padded)
        written += [base + ".bin", base + "-flash1m.bin"]
    (out / "BITSTREAMS.md").write_bytes((dist / "BITSTREAMS.md").read_bytes())
    written.append("BITSTREAMS.md")
    return written


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="verify fpga/al3/dist against manifest.json and BITSTREAMS.md")
    c.add_argument("--release", action="store_true", help="also require one benched recommended image, no placeholders")
    p = sub.add_parser("pack", help="write the release assets into --out")
    p.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)
    errors = check(release=args.cmd == "check" and args.release)
    for e in errors:
        print("::error::bitstreams: %s" % e, file=sys.stderr)
    if errors:
        return 1
    if args.cmd == "pack":
        for name in pack(args.out):
            print(args.out / name)
    else:
        print("fpga/al3/dist: %d bitstreams consistent" % len(load()[0]["bitstreams"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
