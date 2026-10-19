"""SD sector access over the USB protocol (fw >= 0.2.2, cap SD_SECTORS; docs/protocol.md section 14 row 40).

Used by ``fnirsi1013d-flash --persist`` to write a scope image to SD sector 750 without removing the card. Every
step is checked: each staged chunk's CRC, the stage CRC the device verifies before writing, the CRC the device
reads back from the card after writing, and finally SD_INFO VERIFY (eGON checksum over the stored length).
Backups come first (repo rule: never write without a verified backup).
"""

from __future__ import annotations

import hashlib
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from . import protocol as P
from .transport import Transport

#: SD_WRITE blocks the main loop for the write plus its read-back: up to ~4 s for 128 sectors on the bench (fw
#: 0.2.3). So write 32 sectors per request with a 5 s timeout, and resend with the same seq on a timeout (the
#: duplicate cache answers a completed write without writing again).
WRITE_SECTORS = 32
WRITE_TIMEOUT_S = 5.0
WRITE_RETRIES = 4
READ_TIMEOUT_S = 5.0
IMAGE_SECTOR = 750


class SdError(Exception):
    pass


@dataclass
class Backup:
    path: Path
    first_sector: int
    sectors: int
    sha256: str


class SdCard:
    def __init__(self, t: Transport, say: Callable[[str], None] = print, stage_delay_s: float = 0.0,
                 write_sectors: int = WRITE_SECTORS):
        self.t = t
        self.say = say
        #: sectors per SD_WRITE_SECTORS (1..128); each write stages its own piece at offset 0
        self.write_sectors = max(1, min(P.SD_MAX_SECTORS, int(write_sectors)))
        #: pause between SD_STAGE requests (each already waits for its answer); bench throttle
        self.stage_delay_s = stage_delay_s

    def info(self, verify: bool = False, identify: bool = False) -> P.SdInfo:
        """SD_INFO; ``identify`` (protocol 1.16): the card image's identity (a scan of the image the first time)."""
        flags = (P.SD_FLAG_VERIFY if verify else 0) | (P.SD_FLAG_IDENT if identify else 0)
        r = self.t.request(P.Cmd.SD_INFO, bytes([flags, 0, 0, 0]), timeout=WRITE_TIMEOUT_S if flags else None)
        return P.SdInfo.unpack(r.payload)

    def read(self, sector: int, count: int) -> bytes:
        out = bytearray()
        done = 0
        while done < count:
            n = min(P.SD_MAX_SECTORS, count - done)
            r = self.t.request(P.Cmd.SD_READ_SECTORS, P.SdReadReq(sector + done, n).pack(), timeout=READ_TIMEOUT_S)
            h = P.SdReadHeader.unpack(r.payload)
            data = r.payload[16:]
            if h.sector != sector + done or h.count != n or len(data) != n * 512:
                raise SdError(f"SD_READ answer for {h.sector}+{h.count} ({len(data)} B), asked {sector + done}+{n}")
            if P.crc32_iso_hdlc(data) != h.crc32:
                raise SdError(f"SD_READ sectors {sector + done}+{n}: data CRC does not match the device's")
            out += data
            done += n
        return bytes(out)

    def backup(self, sector: int, count: int, path: Path) -> Backup:
        """Read, save, re-read the file and compare (a verified backup)."""
        data = self.read(sector, count)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if path.read_bytes() != data:
            raise SdError(f"backup {path} does not read back identically")
        again = self.read(sector, count)
        if again != data:
            raise SdError("two reads of the backup range differ: the card or the link is unreliable; not writing")
        return Backup(path, sector, count, hashlib.sha256(data).hexdigest())

    def stage(self, data: bytes) -> None:
        if len(data) > P.SD_STAGE_BYTES:
            raise ValueError("stage holds 64 KiB")
        for off in range(0, len(data), P.SD_STAGE_MAX_CHUNK):
            chunk = data[off: off + P.SD_STAGE_MAX_CHUNK]
            if off and self.stage_delay_s:
                time.sleep(self.stage_delay_s)
            r = self.t.request(P.Cmd.SD_STAGE, P.SdStageHeader(off, len(chunk)).pack() + chunk)
            h = P.SdStageResp.unpack(r.payload)
            if (h.offset, h.length, h.crc32) != (off, len(chunk), P.crc32_iso_hdlc(chunk)):
                raise SdError(f"SD_STAGE at {off}: answer {h} does not match the chunk")

    def write_staged(self, sector: int, data: bytes, flags: int = 0) -> None:
        count = len(data) // 512
        req = P.SdWriteReq(sector, count, flags, P.crc32_iso_hdlc(data))
        r = self.t.request(P.Cmd.SD_WRITE_SECTORS, req.pack(), timeout=WRITE_TIMEOUT_S, retries=WRITE_RETRIES)
        h = P.SdWriteResp.unpack(r.payload)
        if (h.sector, h.count) != (sector, count) or h.crc32 != req.crc32:
            raise SdError(f"SD_WRITE {sector}+{count}: read-back CRC 0x{h.crc32:08X} != 0x{req.crc32:08X}")

    def write(self, sector: int, data: bytes, flags: int = 0, progress: Callable[[int, int], None] | None = None) -> None:
        """Write ``data`` (padded to 512) from ``sector`` in ``write_sectors`` pieces: stage, write, check."""
        data = bytes(data) + b"\0" * (-len(data) % 512)
        step = self.write_sectors * 512
        for off in range(0, len(data), step):
            piece = data[off: off + step]
            self.stage(piece)
            self.write_staged(sector + off // 512, piece, flags)
            if progress:
                progress(off + len(piece), len(data))

    def write_piece(self, sector: int, piece: bytes, flags: int = 0) -> None:
        """One <= 64 KiB piece: stage it, then write it (both checked)."""
        self.stage(piece)
        self.write_staged(sector, piece, flags)

    def verify(self, sector: int, data: bytes) -> None:
        """Read the sectors back with SD_READ_SECTORS and compare (independent of the write's own read-back)."""
        data = bytes(data) + b"\0" * (-len(data) % 512)
        back = self.read(sector, len(data) // 512)
        if back != data:
            bad = next(i for i in range(len(data)) if back[i] != data[i])
            raise SdError(f"SD_READ compare failed at byte {bad} (sector {sector + bad // 512})")


@dataclass
class WritePlan:
    """What a write would do (``Scope.sd_write(..., dry_run=True)``)."""

    sector: int
    sectors: int
    flags: int
    pieces: list[tuple[int, int]] = field(default_factory=list)
    refusals: list[str] = field(default_factory=list)
    timings: dict[str, float] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return not self.refusals


def plan_write(info: P.SdInfo, sector: int, nbytes: int, flags: int = 0,
               write_sectors: int = WRITE_SECTORS) -> WritePlan:
    """The host-side image of the firmware's checks (section 14 row 40), so a dry run can say what would fail."""
    n = (nbytes + 511) // 512
    plan = WritePlan(sector, n, flags, [(sector + k, min(write_sectors, n - k)) for k in range(0, n, write_sectors)])
    last = sector + n - 1
    if not info.write_end:
        plan.refusals.append("the card has no valid partition table: the firmware refuses writes")
    elif sector < 16 or last >= info.write_end:
        plan.refusals.append(f"sectors {sector}..{last} are outside the write window [16, {info.write_end})")
    if sector < 80 and not flags & P.SD_WR_BOOT:
        plan.refusals.append("sectors 16-79 (bootloader) need the BOOT flag")
    if sector <= 710 and last >= 708 and not flags & P.SD_WR_CONFIG:
        plan.refusals.append("sectors 708-710 (calibration, settings, boot mode) need the CONFIG flag")
    return plan


def timed(timings: dict[str, float], name: str):
    class _T:
        def __enter__(self):
            self.t0 = time.monotonic()

        def __exit__(self, *a):
            timings[name] = round(time.monotonic() - self.t0, 3)
    return _T()


_VER_RX = re.compile(rb"#OK,FNIRSI-1013D-AI,fw=([^,\x00]*),proto=([0-9.]+)")


def image_version(data: bytes) -> str | None:
    """Firmware version an image reports in ``#VER`` (its literal is in the binary), or None."""
    m = _VER_RX.search(data)
    return m.group(1).decode("ascii", "replace") if m else None


def check_image(data: bytes) -> tuple[int, int]:
    """(length, checksum) of a valid eGON.EXE scope image; raises SdError otherwise."""
    magic, length, stored = P.egon_info(data)
    if magic != P.SD_EGON_EXE:
        raise SdError("not an eGON.EXE image (bytes 4..11)")
    if length == 0 or length % 512 or length > len(data) + (-len(data) % 512):
        raise SdError(f"eGON header length {length} invalid for a {len(data)}-byte file (run mksunxi.py)")
    padded = data + b"\0" * (-len(data) % 512)
    if P.egon_checksum(padded, length) != stored:
        raise SdError("eGON checksum does not match (run mksunxi.py on the image)")
    return length, stored
