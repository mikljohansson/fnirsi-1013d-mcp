"""Hands-free FEL dev loop: running scope -> ENTER_FEL -> load a new image into RAM -> back on the serial port.

    fnirsi1013d-flash                                   # auto-detect the scope, default image
    fnirsi1013d-flash --image firmware/scope/build-gcc10-pc/fnirsi_1013d.bin --port COM10
    fnirsi1013d-flash --no-fel                          # the scope is already in FEL (boot menu)

Steps (each printed with its duration; non-zero exit on the first failure):

1. open the scope's port (auto-detect 1209:0001), ``#VER``, GET_INFO;
2. ENTER_FEL (capability bit ENTER_FEL; without it the user is asked to pick "Start FEL mode" in the boot menu);
3. wait for the boot ROM's USB device 1f3a:efe8. Windows: ``usbipd list`` / ``usbipd attach --wsl`` hands it to
   WSL; Linux: it is used directly;
4. ``sunxi-fel version``, then ``sunxi-fel -p write 0x7FFFFFE0 <image> exe 0x80000000`` (RAM only: SD card and
   SPI flash are untouched; a power cycle returns to whatever the SD card boots);
5. wait for the serial port to come back and print ``#VER``;
6. with ``--persist`` (firmware >= 0.2.2, cap SD_SECTORS): back up SD sectors 750..write_end to
   ``backups/sd_<first>-<end>_<time>.bin`` (read twice, saved, re-read), write the image to sector 750 in 64 KiB
   pieces (stage, write, read-back CRC per piece), then SD_INFO VERIFY (eGON checksum on the card). The next
   power cycle boots it from the card. ``--persist-only`` skips steps 2-5 (the running firmware writes the card).
   ``--restore <backup>`` writes a backup file back to the sectors in its name.

    fnirsi1013d-flash --image <bin> --sha256 <hash> --persist      # RAM load, then make it permanent
    fnirsi1013d-flash --image <bin> --persist-only                 # just write the card
    fnirsi1013d-flash --restore backups/sd_750-2048_20261018-120000.bin

This changes what the scope runs. Announce it to whoever is at the bench before running it (CLAUDE.md bench rules).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

from . import protocol as P
from .sdcard import IMAGE_SECTOR, SdCard, SdError, check_image, image_version
from .transport import (PIDCODES_TEST_PIDS, PIDCODES_VID, DeviceError, Transport, TransportError, list_candidates,
                        open_link, parse_ver)

FEL_VID_PID = "1f3a:efe8"
LOAD_ADDR = 0x7FFFFFE0
EXEC_ADDR = 0x80000000
REPO = Path(__file__).resolve().parents[3]
#: ENTER_FEL attempts. The firmware answers OK before it reboots, and a request lost on the way in (fw <= 0.1.5
#: RX-ring bug, ~4 % of requests) is harmless to resend, so each attempt is a fresh request (new seq).
ENTER_FEL_ATTEMPTS = 3
ENTER_FEL_INTERVAL_S = 1.0
DEFAULT_IMAGE = REPO / "firmware" / "scope" / "build-gcc10-pc" / "fnirsi_1013d.bin"


class FlashError(Exception):
    def __init__(self, msg: str, code: int = 1):
        super().__init__(msg)
        self.code = code


@dataclass
class Result:
    rc: int
    out: str


Runner = Callable[[Sequence[str], float], Result]


def run_cmd(cmd: Sequence[str], timeout: float) -> Result:
    try:
        p = subprocess.run(list(cmd), capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return Result(127, f"{cmd[0]}: not found")
    except subprocess.TimeoutExpired as e:
        return Result(124, f"timeout after {timeout} s: {e.stdout or ''}{e.stderr or ''}")
    return Result(p.returncode, (p.stdout or "") + (p.stderr or ""))


def to_wsl_path(path: Path) -> str:
    """D:\\src\\x\\y.bin -> /mnt/d/src/x/y.bin (paths already POSIX are returned unchanged)."""
    s = str(path)
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", s)
    if not m:
        return s.replace("\\", "/")
    return f"/mnt/{m.group(1).lower()}/" + m.group(2).replace("\\", "/")


@dataclass
class UsbipdDevice:
    busid: str
    vid_pid: str
    description: str
    state: str


def parse_usbipd_list(text: str) -> list[UsbipdDevice]:
    """Parse the "Connected:" table of ``usbipd list`` (usbipd-win 4.x/5.x)."""
    out = []
    in_connected = False
    for line in text.splitlines():
        if line.strip().lower().startswith("connected"):
            in_connected = True
            continue
        if line.strip().lower().startswith("persisted"):
            in_connected = False
            continue
        if not in_connected:
            continue
        m = re.match(r"^\s*(\d+-[\d.]+)\s+([0-9a-fA-F]{4}:[0-9a-fA-F]{4})\s+(.*?)\s{2,}(\S.*?)\s*$", line)
        if m:
            out.append(UsbipdDevice(m.group(1), m.group(2).lower(), m.group(3), m.group(4)))
    return out


class Flasher:
    def __init__(self, *, port: str = "auto", image: Path = DEFAULT_IMAGE, no_fel: bool = False,
                 sha256: str | None = None,
                 distro: str = "Ubuntu", fel_timeout: float = 20.0, port_timeout: float = 15.0,
                 sunxi_fel: str = "sunxi-fel", windows: bool | None = None, runner: Runner = run_cmd,
                 out: Callable[[str], None] = print, transport_factory: Callable[[str], Transport] | None = None,
                 sleep: Callable[[float], None] = time.sleep, persist: bool = False, persist_only: bool = False,
                 restore: Path | None = None, backup_dir: Path | None = None, force: bool = False,
                 stage_delay_ms: float = 0.0, port_return_s: float = 15.0, piece_attempts: int = 4):
        self.port = port
        self.image = Path(image)
        self.sha256 = sha256.lower() if sha256 else None
        self.no_fel = no_fel
        self.distro = distro
        self.fel_timeout = fel_timeout
        self.port_timeout = port_timeout
        self.sunxi_fel = shlex.split(sunxi_fel)
        self.windows = sys.platform == "win32" if windows is None else windows
        self.run = runner
        self.say = out
        self.transport_factory = transport_factory or (lambda url: Transport(open_link(url)))
        self.sleep = sleep
        self.persist = persist or persist_only
        self.persist_only = persist_only
        self.restore_file = Path(restore) if restore else None
        self.backup_dir = Path(backup_dir) if backup_dir else REPO / "backups" / "sdcard"
        self.force = force
        self.stage_delay_s = stage_delay_ms / 1000.0
        #: how long to wait for the serial port to come back after a USB drop during a card write
        self.port_return_s = port_return_s
        #: tries per 64 KiB piece (first try + resends after drops or E_IO)
        self.piece_attempts = piece_attempts
        self.running_fw: str | None = None
        self.t0 = time.monotonic()

    # --- helpers ---------------------------------------------------------------------------------------

    def step(self, msg: str) -> None:
        self.say(f"[{time.monotonic() - self.t0:6.2f} s] {msg}")

    def fel_cmd(self, *args: str) -> list[str]:
        if self.windows:
            return ["wsl", "-d", self.distro, "-u", "root", "--", *self.sunxi_fel, *args]
        return [*self.sunxi_fel, *args]

    def image_arg(self) -> str:
        return to_wsl_path(self.image.resolve()) if self.windows else str(self.image.resolve())

    def _scope_port(self) -> str:
        if self.port.lower() != "auto":
            return self.port
        for c in list_candidates():
            if c.vid == PIDCODES_VID and c.pid in PIDCODES_TEST_PIDS:
                return c.device
        raise FlashError("no scope serial port (1209:0001) found; pass --port, or use --no-fel if the scope is "
                         "already in FEL", 3)

    # --- steps ---------------------------------------------------------------------------------------------

    def check_image(self) -> None:
        if not self.image.is_file():
            raise FlashError(f"image {self.image} not found; build it (firmware/README.md) or pass --image", 2)
        data = self.image.read_bytes()
        if len(data) < 64 or data[4:12] != b"eGON.EXE":
            raise FlashError(f"{self.image} does not look like an eGON.EXE scope image (bytes 4..11 = "
                             f"{data[4:12]!r})", 2)
        digest = hashlib.sha256(data).hexdigest()
        self.step(f"image {self.image} ({len(data)} bytes) sha256 {digest}")
        if self.sha256 and not digest.startswith(self.sha256):
            raise FlashError(f"image sha256 {digest} does not match --sha256 {self.sha256}: the file changed (was it "
                             "rebuilt in place?)", 2)
        self._image_bytes = data

    def enter_fel(self) -> None:
        port = self._scope_port()
        self.step(f"open {port}")
        t = self.transport_factory(port)
        try:
            ver = t.ascii("#VER", timeout=0.3)
            self.step(f"#VER -> {ver}")
            info = P.InfoResp.unpack(t.request(P.Cmd.GET_INFO).payload)
            self.step(f"GET_INFO: fw {info.fw_version_str}, fpga 0x{info.fpga_version:04X}, caps 0x{info.caps:X}")
            if not info.caps & P.Caps.ENTER_FEL:
                self.step("this firmware has no ENTER_FEL: on the scope, open the boot menu (power on with the menu "
                          "boot mode) and pick 'Start FEL mode'; waiting for the FEL device")
                return
            self._send_enter_fel(t, info.fw_version_str)
        except TransportError as e:
            raise FlashError(f"talking to the scope on {port} failed: {e}", 3) from e
        finally:
            t.close()

    def _send_enter_fel(self, t: Transport, old_fw: str) -> None:
        payload = P.U32Req(P.ENTER_FEL_MAGIC).pack()
        last = ""
        for attempt in range(1, ENTER_FEL_ATTEMPTS + 1):
            t0 = time.monotonic()
            try:
                t.request(P.Cmd.ENTER_FEL, payload, timeout=ENTER_FEL_INTERVAL_S, retries=0)
                self.step(f"ENTER_FEL acknowledged (attempt {attempt}); the scope leaves USB")
                return
            except DeviceError as e:
                if e.status != P.Status.E_BUSY:
                    raise FlashError(f"ENTER_FEL refused: {e}", 3) from e
                last = "E_BUSY (a scope menu is open: close it)"
            except TransportError as e:
                last = str(e)
            # The OK may have been lost after the scope executed ENTER_FEL (seen on 0.1.5): look for the boot ROM
            # device before resending. Not a failure, so never "FAILED" here.
            if self.fel_device_present():
                self.step(f"ENTER_FEL answer lost (attempt {attempt}: {last}), but the FEL device {FEL_VID_PID} is "
                          "present: continuing")
                return
            self.step(f"ENTER_FEL not answered yet (attempt {attempt}/{ENTER_FEL_ATTEMPTS}: {last}); FEL device not "
                      "seen, retrying" if attempt < ENTER_FEL_ATTEMPTS else
                      f"ENTER_FEL not answered (attempt {attempt}/{ENTER_FEL_ATTEMPTS}: {last}); checking #VER")
            if attempt < ENTER_FEL_ATTEMPTS:
                self.sleep(max(0.0, ENTER_FEL_INTERVAL_S - (time.monotonic() - t0)))
                if self.fel_device_present():
                    self.step(f"FEL device {FEL_VID_PID} appeared: ENTER_FEL was executed, continuing")
                    return
        # No answer at all. The OK could have been lost after the scope executed it: if the old firmware
        # still answers #VER, it did not.
        try:
            ver = t.ascii("#VER", timeout=0.3)
        except TransportError:
            ver = None
        if ver and ver.startswith("#OK"):
            raise FlashError(f"ENTER_FEL got no answer after {ENTER_FEL_ATTEMPTS} attempts ({last}) and the scope "
                             f"still runs firmware {parse_ver(ver).get('fw', old_fw)} ({ver}); nothing was loaded. "
                             "Close any scope menu and rerun, or use the boot menu and --no-fel", 3)
        self.step(f"ENTER_FEL unanswered after {ENTER_FEL_ATTEMPTS} attempts but #VER is silent: assuming the scope "
                  "rebooted into FEL")

    def fel_device_present(self) -> bool:
        """One quick look for the boot ROM's USB device (usbipd list on Windows, sunxi-fel version elsewhere)."""
        if self.windows:
            r = self.run(["usbipd", "list"], 10)
            return r.rc == 0 and any(d.vid_pid == FEL_VID_PID for d in parse_usbipd_list(r.out))
        r = self.run(self.fel_cmd("version"), 5)
        return r.rc == 0 and "AWUSBFEX" in r.out

    def wait_fel_device(self) -> None:
        deadline = time.monotonic() + self.fel_timeout
        if self.windows:
            dev = None
            while time.monotonic() < deadline:
                r = self.run(["usbipd", "list"], 10)
                if r.rc == 127:
                    raise FlashError("usbipd not found: install usbipd-win (firmware/README.md section 4)", 4)
                dev = next((d for d in parse_usbipd_list(r.out) if d.vid_pid == FEL_VID_PID), None)
                if dev:
                    break
                self.sleep(0.5)
            if dev is None:
                raise FlashError(f"FEL device {FEL_VID_PID} did not appear within {self.fel_timeout:.0f} s "
                                 "(usbipd list). If the scope shows 'FEL mode' but nothing enumerates, power-cycle "
                                 "it and use the boot menu", 4)
            self.step(f"FEL device on busid {dev.busid} ({dev.state})")
            st = dev.state.lower()
            if st.startswith("not shared"):
                raise FlashError(f"busid {dev.busid} is not shared with WSL; once, from an admin shell: "
                                 f"usbipd bind --busid {dev.busid}  (or --hardware-id {FEL_VID_PID})", 4)
            if "attached" not in st:
                r = self.run(["usbipd", "attach", "--wsl", "--busid", dev.busid], 30)
                if r.rc != 0:
                    raise FlashError(f"usbipd attach failed (rc {r.rc}): {r.out.strip()[:300]}", 4)
                self.step(f"attached busid {dev.busid} to WSL")
        # sunxi-fel version until it answers (the device needs a moment after attach / enumeration)
        last = ""
        while True:
            r = self.run(self.fel_cmd("version"), 10)
            last = r.out.strip()
            if r.rc == 0 and "AWUSBFEX" in r.out:
                self.step(f"sunxi-fel version -> {last.splitlines()[0] if last else ''}")
                return
            if r.rc == 127 or "not found" in last.lower() and "usb" not in last.lower():
                raise FlashError(f"sunxi-fel not available ({last[:200]}); install sunxi-tools in "
                                 f"{'WSL ' + self.distro if self.windows else 'this system'}", 5)
            if time.monotonic() >= deadline + 10:
                raise FlashError(f"sunxi-fel version did not see the FEL device: {last[:300]}", 5)
            self.sleep(0.5)

    def load(self) -> None:
        # the file must not change between the check and the load (it can be rebuilt in place)
        if self.image.read_bytes() != getattr(self, "_image_bytes", None):
            raise FlashError(f"{self.image} changed after it was checked; rerun", 2)
        img = self.image_arg()
        cmd = self.fel_cmd("-p", "write", f"0x{LOAD_ADDR:08X}", img, "exe", f"0x{EXEC_ADDR:08X}")
        self.step("load: " + " ".join(cmd))
        r = self.run(cmd, 60)
        if r.rc != 0:
            raise FlashError(f"sunxi-fel write/exe failed (rc {r.rc}): {r.out.strip()[:400]}", 5)
        self.step("image written and started")

    def wait_port(self) -> str:
        deadline = time.monotonic() + self.port_timeout
        while time.monotonic() < deadline:
            port = self.port
            if port.lower() == "auto":
                port = next((c.device for c in list_candidates()
                             if c.vid == PIDCODES_VID and c.pid in PIDCODES_TEST_PIDS), "")
            if port:
                try:
                    t = self.transport_factory(port)
                except TransportError:
                    self.sleep(0.5)
                    continue
                try:
                    ver = t.ascii("#VER", timeout=0.3)
                    if ver and ver.startswith("#OK"):
                        v = parse_ver(ver)
                        self.step(f"scope back on {port}: {ver}")
                        self.running_fw = v.get("fw", "")
                        return self.running_fw
                except TransportError:
                    pass
                finally:
                    t.close()
            self.sleep(0.5)
        raise FlashError(f"the scope's serial port did not come back within {self.port_timeout:.0f} s; the image "
                         "may have crashed (check the screen) - power-cycle to recover", 6)

    # --- SD card (fw >= 0.2.2) ---------------------------------------------------------------------------

    def _sd_session(self) -> tuple[Transport, SdCard]:
        port = self._scope_port()
        try:
            t = self.transport_factory(port)
            info = P.InfoResp.unpack(t.request(P.Cmd.GET_INFO).payload)
        except TransportError as e:
            raise FlashError(f"talking to the scope on {port} failed: {e}", 7) from e
        if not info.caps & P.Caps.SD_SECTORS:
            t.close()
            raise FlashError(f"firmware {info.fw_version_str} has no SD sector commands (cap SD_SECTORS, fw >= 0.2.2); "
                             "load such an image first (without --persist) or write the card with make_sd_image.py", 7)
        self.step(f"SD access through fw {info.fw_version_str} on {port}")
        return t, SdCard(t, self.say, self.stage_delay_s)

    def _reconnect_sd(self) -> tuple[Transport, SdCard]:
        """After a USB drop: wait for the port to come back (it re-enumerates by itself), then a new session."""
        deadline = time.monotonic() + self.port_return_s
        last = ""
        while True:
            try:
                return self._sd_session()
            except FlashError as e:
                last = str(e)
                if "no SD sector commands" in last:
                    raise
            if time.monotonic() >= deadline:
                raise FlashError(f"the serial port did not come back within {self.port_return_s:g} s ({last})", 7)
            self.sleep(0.5)

    def _write_resuming(self, t: Transport, sd: SdCard, sector: int, image: bytes, partial_msg: str
                        ) -> tuple[Transport, SdCard, dict[int, int]]:
        """Write ``image`` in 128-sector pieces. A piece that fails (USB drop, E_IO, a CRC refusal) is resent,
        after reconnecting when the link dropped; pieces before it are verified by the card's read-back CRC.
        Returns the (possibly new) session and {piece index: resends}."""
        pieces = [(sector + off // 512, image[off: off + P.SD_STAGE_BYTES])
                  for off in range(0, len(image), P.SD_STAGE_BYTES)]
        resends: dict[int, int] = {}
        i = 0
        while i < len(pieces):
            sec, piece = pieces[i]
            try:
                sd.write_piece(sec, piece)
            except (TransportError, DeviceError, SdError) as e:
                n = resends.get(i, 0) + 1
                resends[i] = n
                if n >= self.piece_attempts:
                    raise FlashError(f"piece {i} (sectors {sec}..{sec + len(piece) // 512 - 1}) failed "
                                     f"{n} times, last: {e}. {partial_msg}", 7) from e
                self.step(f"piece {i} (sector {sec}) not confirmed ({e}); resend {n}")
                if isinstance(e, TransportError):
                    t.close()
                    self.step(f"link lost: waiting up to {self.port_return_s:g} s for the port to come back")
                    try:
                        t, sd = self._reconnect_sd()
                    except FlashError as e2:
                        raise FlashError(f"{e2}. {partial_msg}", 7) from e
                    self.step("reconnected; resuming from the first unconfirmed piece")
                continue
            done = sec - sector + len(piece) // 512
            self.step(f"written {done * 512}/{len(image)} B")
            i += 1
        return t, sd, resends

    def persist_image(self) -> None:
        data = self.image.read_bytes()
        if data != getattr(self, "_image_bytes", data):
            raise FlashError(f"{self.image} changed after it was checked; rerun", 2)
        try:
            length, checksum = check_image(data)
        except SdError as e:
            raise FlashError(f"{self.image}: {e}", 2) from e
        image = data[:length] if len(data) >= length else data + b"\0" * (length - len(data))
        img_ver = image_version(data)
        t, sd = self._sd_session()
        try:
            ver = t.ascii("#VER", timeout=0.3) or ""
            running = parse_ver(ver).get("fw", "") if ver.startswith("#OK") else ""
            self.step(f"running firmware {running or '?'}, image reports {img_ver or '?'}")
            if (not running or running != img_ver) and not self.force:
                raise FlashError(f"the image reports firmware {img_ver or 'unknown'} but the scope runs "
                                 f"{running or 'unknown'} (#VER): persist only an image that is running and tested. "
                                 "Load it first (without --persist-only), or pass --force", 7)
            t0 = time.monotonic()
            info = sd.info()
            n = length // 512
            self.step(f"card {info.card_sectors} sectors, partition at {info.part_start}, write window "
                      f"[16, {info.write_end}); image {length} B = {n} sectors at {IMAGE_SECTOR}")
            if not info.write_end:
                raise FlashError("the card has no valid partition table, so the firmware refuses writes", 7)
            if IMAGE_SECTOR + n > info.write_end:
                raise FlashError(f"the image ({n} sectors) does not fit below sector {info.write_end}", 7)
            old = info.header(IMAGE_SECTOR)
            old_n = 0
            if old is not None and old.magic:
                self.step(f"sector {IMAGE_SECTOR} now holds eGON magic {old.magic}, length {old.length}")
                old_n = (old.length + 511) // 512
            # back up what the write covers, and the whole old image if it is longer
            end = min(info.write_end, IMAGE_SECTOR + max(n, old_n))
            stamp = time.strftime("%Y%m%d-%H%M%S")
            bpath = self.backup_dir / f"sd_{IMAGE_SECTOR}-{end}_{stamp}.bin"
            t1 = time.monotonic()
            bk = sd.backup(IMAGE_SECTOR, end - IMAGE_SECTOR, bpath)
            self.step(f"backup of sectors {IMAGE_SECTOR}..{end - 1} verified in {time.monotonic() - t1:.2f} s: "
                      f"{bk.path} sha256 {bk.sha256}")
            partial = (f"THE CARD MAY HOLD A PARTIAL IMAGE at sector {IMAGE_SECTOR}: do not power-cycle the scope "
                       f"before repairing it (rerun this command, or fnirsi1013d-flash --restore {bpath})")
            t1 = time.monotonic()
            t, sd, resends = self._write_resuming(t, sd, IMAGE_SECTOR, image, partial)
            t_write = time.monotonic() - t1
            t1 = time.monotonic()
            try:
                sd.verify(IMAGE_SECTOR, image)
            except (SdError, TransportError, DeviceError) as e:
                raise FlashError(f"SD_READ compare after the write failed: {e}. {partial}", 7) from e
            rs = ", ".join(f"piece {k}: {v}" for k, v in sorted(resends.items())) or "none"
            self.step(f"write {t_write:.2f} s ({length / max(t_write, 1e-6) / 1024:.0f} KiB/s), resends: {rs}; "
                      f"SD_READ compare of all {n} sectors {time.monotonic() - t1:.2f} s: identical")
            try:
                v = sd.info(verify=True).header(IMAGE_SECTOR)
            except (SdError, TransportError, DeviceError) as e:
                raise FlashError(f"SD_INFO VERIFY after the write failed: {e}. The SD_READ compare was identical, "
                                 "so the image is probably complete; rerun to confirm before a power cycle", 7) from e
            if v is None or v.magic != P.SD_EGON_EXE or v.check != P.SD_CHECK_OK or v.length != length:
                raise FlashError(f"SD_INFO VERIFY of sector {IMAGE_SECTOR} failed ({v}). {partial}", 7)
            self.step(f"sector {IMAGE_SECTOR} verified on the card: eGON.EXE, length {length}, checksum "
                      f"0x{checksum:08X}, total {time.monotonic() - t0:.2f} s. The next power cycle boots it "
                      "(boot-mode byte and sectors 16-79 untouched)")
        except (SdError, TransportError, DeviceError) as e:
            raise FlashError(f"persisting to the SD card failed before the write: {e}. Nothing was written; backups "
                             f"(if made) are in {self.backup_dir}", 7) from e
        finally:
            t.close()

    def restore_backup(self) -> None:
        f = self.restore_file
        assert f is not None
        m = re.match(r"sd_(\d+)-(\d+)_", f.name)
        if not m or not f.is_file():
            raise FlashError(f"{f}: not a backup made by --persist (name sd_<first>-<end>_<time>.bin)", 2)
        first, end = int(m.group(1)), int(m.group(2))
        data = f.read_bytes()
        if len(data) != (end - first) * 512:
            raise FlashError(f"{f} is {len(data)} B, its name says {(end - first) * 512}", 2)
        t, sd = self._sd_session()
        try:
            self.step(f"restoring {f} to sectors {first}..{end - 1}")
            sd.write(first, data, progress=lambda done, total: self.step(f"written {done}/{total} B"))
            if sd.read(first, end - first) != data:
                raise FlashError("restored sectors do not read back identically", 7)
            self.step("restore verified")
        except (SdError, TransportError, DeviceError) as e:
            raise FlashError(f"restore failed: {e}", 7) from e
        finally:
            t.close()

    def run_all(self) -> int:
        try:
            if self.restore_file is not None:
                self.restore_backup()
                self.step("done")
                return 0
            self.check_image()
            if not self.persist_only:
                if self.no_fel:
                    self.step("--no-fel: assuming the scope is already in FEL")
                else:
                    self.enter_fel()
                self.wait_fel_device()
                self.load()
                self.wait_port()
            if self.persist:
                self.persist_image()
            self.step("done")
            return 0
        except FlashError as e:
            self.step(f"FAILED: {e}")
            return e.code or 1
        except Exception as e:  # noqa: BLE001 - every failure must print FAILED and exit non-zero
            self.step(f"FAILED: unexpected {type(e).__name__}: {e}")
            return 1
        except KeyboardInterrupt:
            self.step("FAILED: interrupted")
            return 130


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-flash", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("FNIRSI_MCP_PORT", "auto"),
                    help="scope serial port (default: auto-detect 1209:0001)")
    ap.add_argument("--image", type=Path, default=DEFAULT_IMAGE, help=f"scope image (default {DEFAULT_IMAGE})")
    ap.add_argument("--no-fel", action="store_true", help="the scope is already in FEL; skip ENTER_FEL")
    ap.add_argument("--sha256", help="expected image sha256 (full or prefix); refuse to load anything else")
    ap.add_argument("--distro", default="Ubuntu", help="WSL distribution with sunxi-tools (Windows only)")
    ap.add_argument("--sunxi-fel", default="sunxi-fel", help="sunxi-fel command, e.g. 'sudo sunxi-fel' on Linux")
    ap.add_argument("--fel-timeout", type=float, default=20.0, help="seconds to wait for 1f3a:efe8")
    ap.add_argument("--port-timeout", type=float, default=15.0, help="seconds to wait for the serial port after exe")
    ap.add_argument("--persist", action="store_true", help="after the RAM load, write the image to SD sector 750 "
                    "(backup first; fw >= 0.2.2)")
    ap.add_argument("--persist-only", action="store_true", help="only write the image to the card through the running "
                    "firmware (no FEL)")
    ap.add_argument("--restore", type=Path, metavar="BACKUP", help="write a --persist backup file back to the card")
    ap.add_argument("--backup-dir", type=Path, help="where SD backups go (default: <repo>/backups/sdcard)")
    ap.add_argument("--stage-delay-ms", type=float, default=0.0, help="pause between SD_STAGE pieces (bench throttle)")
    ap.add_argument("--port-return", type=float, default=15.0, help="seconds to wait for the port after a USB drop "
                    "during a card write before giving up")
    ap.add_argument("--force", action="store_true", help="persist even if the image's version differs from the "
                    "running firmware's #VER")
    a = ap.parse_args(argv)
    f = Flasher(port=a.port, image=a.image, no_fel=a.no_fel, sha256=a.sha256, distro=a.distro, sunxi_fel=a.sunxi_fel,
                fel_timeout=a.fel_timeout, port_timeout=a.port_timeout, persist=a.persist,
                persist_only=a.persist_only, restore=a.restore, backup_dir=a.backup_dir, force=a.force,
                stage_delay_ms=a.stage_delay_ms, port_return_s=a.port_return)
    sys.exit(f.run_all())


if __name__ == "__main__":
    main()
