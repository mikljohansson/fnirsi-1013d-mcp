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
5. wait for the serial port to come back and print ``#VER``.

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
                 sleep: Callable[[float], None] = time.sleep):
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
                        return v.get("fw", "")
                except TransportError:
                    pass
                finally:
                    t.close()
            self.sleep(0.5)
        raise FlashError(f"the scope's serial port did not come back within {self.port_timeout:.0f} s; the image "
                         "may have crashed (check the screen) - power-cycle to recover", 6)

    def run_all(self) -> int:
        try:
            self.check_image()
            if self.no_fel:
                self.step("--no-fel: assuming the scope is already in FEL")
            else:
                self.enter_fel()
            self.wait_fel_device()
            self.load()
            self.wait_port()
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
    a = ap.parse_args(argv)
    f = Flasher(port=a.port, image=a.image, no_fel=a.no_fel, sha256=a.sha256, distro=a.distro, sunxi_fel=a.sunxi_fel,
                fel_timeout=a.fel_timeout, port_timeout=a.port_timeout)
    sys.exit(f.run_all())


if __name__ == "__main__":
    main()
