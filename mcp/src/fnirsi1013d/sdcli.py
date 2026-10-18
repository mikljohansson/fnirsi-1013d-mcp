"""``fnirsi1013d-sd``: SD card access through the scope's USB protocol (fw >= 0.2.2, cap SD_SECTORS), for the bench.

    fnirsi1013d-sd info [--verify]                       # card, partition, write window, eGON headers 16/80/750
    fnirsi1013d-sd read 750 128 -o sectors.bin           # CRC-checked SD_READ_SECTORS
    fnirsi1013d-sd write image.bin --sector 750 --dry-run
    fnirsi1013d-sd write image.bin --sector 750 --yes    # asks to type the confirmation, backs up first

``write`` backs up the target sectors to ``backups/sdcard/sd_<first>-<end>_<time>.bin`` (verified), stages and
writes 128-sector pieces (stage CRC and the card's read-back CRC checked), then reads everything back and
compares. Sectors 16-79 (bootloader) need ``--boot``, 708-710 (calibration, settings, boot mode) ``--config``.
Writing changes what the scope boots: only with the lead's ack and the owner present (AGENTS.md bench rules).
Exit 0 on success; every failure prints one ``FAILED:`` line and exits 7 (2 for bad arguments or files).
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path
from typing import Callable

from . import protocol as P
from .sdcard import SdError, plan_write
from .scope import Scope
from .transport import DeviceError, TransportError

REPO = Path(__file__).resolve().parents[3]
MAGIC_NAMES = {0: "none", 1: "eGON.BT0", 2: "eGON.EXE"}
CHECK_NAMES = {0: "not checked", 1: "checksum ok", 2: "checksum BAD", 3: "length invalid"}


def _port(arg: str) -> str:
    if arg.lower() != "auto":
        return arg
    from .transport import PIDCODES_TEST_PIDS, PIDCODES_VID, list_candidates
    for c in list_candidates():
        if c.vid == PIDCODES_VID and c.pid in PIDCODES_TEST_PIDS:
            return c.device
    raise SdError("no scope serial port (1209:0001) found; pass --port")


def describe_info(info: P.SdInfo) -> list[str]:
    out = [f"card {info.card_sectors} sectors ({info.card_sectors * 512 / 2**20:.0f} MiB), partition at "
           f"{info.part_start or 'none (no valid MBR)'}, write window "
           + (f"[16, {info.write_end})" if info.write_end else "off") + f", stage {info.stage_bytes} B"]
    for h in info.headers:
        out.append(f"  sector {h.sector}: {MAGIC_NAMES.get(h.magic, h.magic)}"
                   + (f", length {h.length}, checksum 0x{h.checksum:08X}, {CHECK_NAMES.get(h.check, h.check)}"
                      if h.magic else ""))
    return out


def run(argv: list[str] | None = None, *, scope_factory: Callable[[str], Scope] | None = None,
        out: Callable[[str], None] = print, confirm: Callable[[str], str] = input,
        backup_dir: Path | None = None) -> int:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-sd", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("FNIRSI_MCP_PORT", "auto"))
    sp = ap.add_subparsers(dest="cmd", required=True)
    pi = sp.add_parser("info", help="SD_INFO")
    pi.add_argument("--verify", action="store_true", help="recompute the eGON checksums on the card")
    pr = sp.add_parser("read", help="read sectors to a file")
    pr.add_argument("sector", type=int)
    pr.add_argument("count", type=int)
    pr.add_argument("-o", "--output", type=Path, required=True)
    pw = sp.add_parser("write", help="write a file to the card (backup first)")
    pw.add_argument("file", type=Path)
    pw.add_argument("--sector", type=int, required=True)
    pw.add_argument("--boot", action="store_true", help="allow sectors 16-79")
    pw.add_argument("--config", action="store_true", help="allow sectors 708-710")
    pw.add_argument("--dry-run", action="store_true", help="check the window and flags only, write nothing")
    pw.add_argument("--yes", action="store_true", help="required for a real write (then type the confirmation)")
    a = ap.parse_args(argv)
    t0 = time.monotonic()

    def fail(msg: str, code: int = 7) -> int:
        out(f"FAILED: {msg}")
        return code

    if a.cmd == "write":
        if not a.file.is_file():
            return fail(f"{a.file} not found", 2)
        if not a.dry_run and not a.yes:
            return fail("a real write needs --yes (try --dry-run first)", 2)
    try:
        s = (scope_factory or (lambda url: Scope.connect(url)))(_port(a.port))
    except (SdError, TransportError) as e:
        return fail(f"cannot connect: {e}")
    try:
        if a.cmd == "info":
            for line in describe_info(s.sd_info(a.verify)):
                out(line)
            return 0
        if a.cmd == "read":
            data = s.sd_read(a.sector, a.count)
            a.output.write_bytes(data)
            out(f"read {a.count} sectors from {a.sector} to {a.output} in {time.monotonic() - t0:.2f} s")
            return 0
        data = a.file.read_bytes()
        flags = (P.SD_WR_BOOT if a.boot else 0) | (P.SD_WR_CONFIG if a.config else 0)
        plan = plan_write(s.sd_info(), a.sector, len(data), flags)
        out(f"plan: {plan.sectors} sectors at {a.sector} in {len(plan.pieces)} piece(s), flags 0x{flags:X}")
        if not plan.ok:
            return fail("; ".join(plan.refusals))
        if a.dry_run:
            out("dry run: the firmware would accept this write; nothing written")
            return 0
        want = f"WRITE {plan.sectors}@{a.sector}"
        if confirm(f"type '{want}' to write the card: ").strip() != want:
            return fail("not confirmed; nothing written", 2)
        bdir = backup_dir or REPO / "backups" / "sdcard"
        bpath = bdir / f"sd_{a.sector}-{a.sector + plan.sectors}_{time.strftime('%Y%m%d-%H%M%S')}.bin"
        bk = s.sd_card().backup(a.sector, plan.sectors, bpath)
        out(f"backup verified: {bk.path} sha256 {bk.sha256}")
        res = s.sd_write(data, a.sector, flags, progress=lambda d, n: out(f"written {d}/{n} B"))
        out(f"written and read back identically: write {res.timings.get('write', 0):.2f} s, compare "
            f"{res.timings.get('read_compare', 0):.2f} s, total {time.monotonic() - t0:.2f} s")
        return 0
    except (SdError, DeviceError, TransportError, OSError) as e:
        return fail(str(e))
    finally:
        s.close()


def main(argv: list[str] | None = None) -> None:
    sys.exit(run(argv))


if __name__ == "__main__":
    main()
