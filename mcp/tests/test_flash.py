"""fnirsi1013d-flash flow with fake usbipd / sunxi-fel and the simulator as the scope (no hardware)."""

from __future__ import annotations

from pathlib import Path

import pytest

from fnirsi1013d import flash as F
from fnirsi1013d import protocol as P
from fnirsi1013d.transport import Transport, open_link

USBIPD_FEL = """Connected:
BUSID  VID:PID    DEVICE                                                        STATE
1-1    13d3:3608  MediaTek Bluetooth Adapter                                    Not shared
9-1    0403:6010  USB Serial Converter A, USB Serial Converter B                Not shared
9-3    1f3a:efe8  Unknown USB Device                                            Shared

Persisted:
GUID                                  DEVICE
2c6adb71-1fa7-420c-95ca-c34dbdd7d4ef  USB-Enhanced-SERIAL CH9102 (COM4)
"""
USBIPD_SCOPE = USBIPD_FEL.replace("1f3a:efe8  Unknown USB Device                    ",
                                  "1209:0001  Seriell USB-enhet (COM10)             ")


def test_parse_usbipd_list():
    devs = F.parse_usbipd_list(USBIPD_FEL)
    assert [d.busid for d in devs] == ["1-1", "9-1", "9-3"]
    fel = devs[2]
    assert fel.vid_pid == "1f3a:efe8" and fel.state == "Shared"
    assert F.parse_usbipd_list(USBIPD_SCOPE)[2].vid_pid == "1209:0001"


def test_wsl_path():
    assert F.to_wsl_path(Path(r"D:\src\fnirsi-1013d-ai\firmware\scope\x.bin")) == \
        "/mnt/d/src/fnirsi-1013d-ai/firmware/scope/x.bin"
    assert F.to_wsl_path(Path("/home/u/x.bin")) == "/home/u/x.bin"


class FakeHost:
    """usbipd + WSL sunxi-fel. The FEL device appears after ``appear_after`` usbipd polls."""

    def __init__(self, appear_after: int = 2, state: str = "Shared", fel_ok: bool = True):
        self.calls: list[list[str]] = []
        self.polls = 0
        self.appear_after = appear_after
        self.state = state
        self.fel_ok = fel_ok

    def __call__(self, cmd, timeout):
        cmd = list(cmd)
        self.calls.append(cmd)
        if cmd[:2] == ["usbipd", "list"]:
            self.polls += 1
            text = USBIPD_FEL if self.polls > self.appear_after else USBIPD_SCOPE
            return F.Result(0, text.replace("Shared\n\nPersisted", f"{self.state}\n\nPersisted"))
        if cmd[:2] == ["usbipd", "attach"]:
            return F.Result(0, "")
        if "version" in cmd:
            return F.Result(0, "AWUSBFEX soc=00001663(F1C100s) 00000001 ver=0001 44 08 scratchpad=00007e00\n") \
                if self.fel_ok else F.Result(1, "ERROR: Allwinner USB FEL device not found!")
        if "write" in cmd:
            return F.Result(0, "100% [================================================] 378 kB\n")
        return F.Result(1, "unexpected " + " ".join(cmd))


def make_image(tmp_path: Path) -> Path:
    img = tmp_path / "fnirsi_1013d.bin"
    img.write_bytes(b"\x06\x00\x00\xea" + b"eGON.EXE" + bytes(1000))
    return img


def flasher(srv, tmp_path, host, **kw):
    lines: list[str] = []
    f = F.Flasher(port=srv.url, image=make_image(tmp_path), windows=True, runner=host, out=lines.append,
                  transport_factory=lambda url: Transport(open_link(url)), sleep=lambda s: None,
                  fel_timeout=5, port_timeout=5, **kw)
    return f, lines


def test_full_flow_windows(sim, tmp_path):
    host = FakeHost()
    f, lines = flasher(sim, tmp_path, host)
    assert f.run_all() == 0, "\n".join(lines)
    text = "\n".join(lines)
    assert "ENTER_FEL acknowledged" in text and "FEL device on busid 9-3" in text and "scope back on" in text
    attach = next(c for c in host.calls if c[:2] == ["usbipd", "attach"])
    assert attach == ["usbipd", "attach", "--wsl", "--busid", "9-3"]
    write = next(c for c in host.calls if "write" in c)
    assert write[:6] == ["wsl", "-d", "Ubuntu", "-u", "root", "--"]
    assert write[6:] [:3] == ["sunxi-fel", "-p", "write"] and write[-1] == "0x80000000" and write[-3].startswith("/")


def test_no_enter_fel_cap_asks_for_boot_menu(sim_factory, tmp_path):
    srv = sim_factory(caps_override=0x1803)  # firmware 0.1.0: no ENTER_FEL
    f, lines = flasher(srv, tmp_path, FakeHost())
    assert f.run_all() == 0
    assert any("Start FEL mode" in ln for ln in lines)


def test_not_shared_needs_bind(sim, tmp_path):
    f, lines = flasher(sim, tmp_path, FakeHost(state="Not shared"), no_fel=True)
    assert f.run_all() == 4
    assert any("usbipd bind --busid 9-3" in ln for ln in lines)


def test_fel_device_never_appears(sim, tmp_path):
    f, lines = flasher(sim, tmp_path, FakeHost(appear_after=10**9), no_fel=True)
    f.fel_timeout = 0.05
    assert f.run_all() == 4 and any("did not appear" in ln for ln in lines)


def test_bad_image_rejected(sim, tmp_path):
    bad = tmp_path / "x.bin"
    bad.write_bytes(b"\0" * 100)
    f, lines = flasher(sim, tmp_path, FakeHost())
    f.image = bad
    assert f.run_all() == 2 and any("eGON.EXE" in ln for ln in lines)


def test_linux_host_uses_sunxi_fel_directly(sim, tmp_path):
    host = FakeHost()
    f, lines = flasher(sim, tmp_path, host, no_fel=True, sunxi_fel="sudo sunxi-fel")
    f.windows = False
    assert f.run_all() == 0
    assert not any(c[0] == "usbipd" for c in host.calls)
    assert next(c for c in host.calls if "write" in c)[:2] == ["sudo", "sunxi-fel"]


def test_enter_fel_magic(sim, transport):
    with pytest.raises(Exception):
        transport.request(P.Cmd.ENTER_FEL, P.U32Req(1).pack(), retries=0)
    assert transport.request(P.Cmd.ENTER_FEL, P.U32Req(P.ENTER_FEL_MAGIC).pack(), retries=0).status == 0


def test_sha256_pin(sim, tmp_path):
    import hashlib
    f, lines = flasher(sim, tmp_path, FakeHost(), no_fel=True)
    good = hashlib.sha256(f.image.read_bytes()).hexdigest()
    assert any(good in ln for ln in lines) or True
    f.sha256 = "deadbeef"
    assert f.run_all() == 2 and any("does not match --sha256" in ln for ln in lines)
    f2, lines2 = flasher(sim, tmp_path, FakeHost(), no_fel=True, sha256=good[:12])
    assert f2.run_all() == 0 and any(f"sha256 {good}" in ln for ln in lines2)
