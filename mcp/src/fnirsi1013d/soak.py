"""``fnirsi1013d-soak``: hold the scope's port open and poll it for a long time, logging every link event.

    fnirsi1013d-soak --port COM10                          # 30 min of GET_STATUS at 10 Hz
    fnirsi1013d-soak --mode capture --duration 600         # 4 KiB TEST_PATTERN (content-checked) each interval
    fnirsi1013d-soak --mode ping --interval 0.02 --log soak.log

Purpose: reproduce and time USB drops (bench 2026-10-18, fw 0.2.11: during a plain 10 Hz GET_STATUS poll the
host's WriteFile failed with Windows error 22 and the device re-enumerated). LOCAL-safe: only PING, GET_STATUS,
GET_INFO, GET_LINK_STATS and TEST_PATTERN. Never gives up on a drop: it waits for the port to come back (up to
``--reopen-timeout``), reopens and continues. One line per event, wall-clock timestamps in ms:

    2026-10-18T17:01:02.345 open COM10 (fw 0.2.11, proto 1.4)
    2026-10-18T17:01:02.360 stats start rx_packets=10 ... usb_bus_resets=1 ...
    2026-10-18T17:12:40.118 fail #3 GET_STATUS: write to COM10 failed: ... [winerror 22]
    2026-10-18T17:12:40.120 port COM10 gone
    2026-10-18T17:12:42.640 port COM10 back after 2.52 s
    2026-10-18T17:12:43.020 reopen COM10 ok (down 2.90 s)
    2026-10-18T17:12:43.030 stats reopen delta usb_bus_resets +1 usb_suspends +1 (since start: ...)
    2026-10-18T17:31:02.400 summary requests 17985, failures 1, drops 1, longest gap 2.90 s, ...

Exit status: 0 when nothing failed, 1 when any request failed or the link dropped, 2 when it could not start.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Callable

from . import protocol as P
from .transport import (PIDCODES_TEST_PIDS, PIDCODES_VID, Link, Transport, TransportError, list_candidates,
                        open_link)

#: counters whose deltas tell what happened on the USB side
WATCH = ("link_resets", "dtr_up", "dtr_down", "usb_bus_resets", "usb_suspends", "usb_resumes", "usb_disconnects",
         "ep0_setups", "ep0_bad_setups", "rx_stalls", "hdr_crc_errors", "payload_crc_errors", "rx_timeouts")
#: Windows: USB settings subgroup / USB selective suspend setting
POWERCFG = ("2a737441-1930-4402-8d77-b2bebba308a3", "48e6b7a6-50f5-4782-a5d4-53bb8f07e226")


def winerror(text: str) -> int | None:
    """The Windows error code in a pyserial exception text, e.g. "...(PermissionError(13, '...', None, 22))"."""
    m = re.search(r"None, (\d+)\)", text) or re.search(r"\[WinError (\d+)\]", text) or \
        re.search(r"error code (\d+)", text, re.I)
    return int(m.group(1)) if m else None


def selective_suspend() -> str:
    """powercfg's USB selective suspend setting for the active scheme (AC / DC), or why it is unknown."""
    if sys.platform != "win32":
        return "n/a (not Windows)"
    try:
        out = subprocess.run(["powercfg", "/query", "SCHEME_CURRENT", *POWERCFG], capture_output=True, text=True,
                             timeout=10, errors="replace").stdout
    except (OSError, subprocess.SubprocessError) as e:
        return f"unknown ({e})"
    vals = dict(re.findall(r"Current (AC|DC) Power Setting Index:\s*0x([0-9a-fA-F]+)", out))
    if not vals:
        return "unknown (powercfg output not recognised; setting hidden or localised)"
    name = {0: "disabled", 1: "enabled"}
    return ", ".join(f"{k} {name.get(int(v, 16), int(v, 16))}" for k, v in sorted(vals.items()))


def _comports():
    from serial.tools import list_ports
    return list_ports.comports()


def port_info(port: str) -> str:
    """The port's USB identity and hub path (pyserial's hwid / location), once at start."""
    if port.lower().startswith("tcp://"):
        return f"{port} (simulator)"
    for c in _comports():
        if c.device.lower() == port.lower():
            vp = f"{c.vid:04x}:{c.pid:04x}" if c.vid is not None else "?"
            return f"{c.device} {vp} {c.description}, location {c.location or '?'}, hwid {c.hwid}"
    return f"{port} (not in the port list)"


def port_present(port: str) -> bool:
    if port.lower().startswith("tcp://"):
        return True  # the simulator: a refused connection is what tells it is down
    return any(c.device.lower() == port.lower() for c in _comports())


@dataclass
class SoakStats:
    requests: int = 0
    failures: int = 0
    drops: int = 0
    longest_gap: float = 0.0
    latencies: list[float] = field(default_factory=list)
    down_times: list[float] = field(default_factory=list)


class Soak:
    def __init__(self, port: str, *, duration: float = 1800.0, interval: float = 0.1, mode: str = "status",
                 reopen_timeout: float = 60.0, out: Callable[[str], None] = print,
                 link_factory: Callable[[str], Link] = open_link,
                 present: Callable[[str], bool] = port_present, clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep, poll_down: float = 0.5):
        self.port, self.duration, self.interval, self.mode = port, duration, interval, mode
        self.reopen_timeout, self.say, self.link_factory, self.present = reopen_timeout, out, link_factory, present
        self.clock, self.sleep, self.poll_down = clock, sleep, poll_down
        self.st = SoakStats()
        self.t: Transport | None = None
        self.start_stats: dict[str, int] | None = None
        self.last_stats: dict[str, int] | None = None
        self.n_counters = 0

    def log(self, msg: str) -> None:
        self.say(f"{_dt.datetime.now().isoformat(timespec='milliseconds')} {msg}")

    # --- link ----------------------------------------------------------------------------------------------

    def open(self) -> None:
        self.t = Transport(self.link_factory(self.port), retries=0)
        info = P.InfoResp.unpack(self.t.request(P.Cmd.GET_INFO, retries=1).payload)
        self.proto_minor = info.proto_minor
        self.log(f"open {self.port} (fw {info.fw_version_str}, proto {info.proto_major}.{info.proto_minor})")

    def close(self) -> None:
        if self.t is not None:
            try:
                self.t.close()
            except Exception:  # noqa: BLE001
                pass
            self.t = None

    def stats(self, label: str) -> None:
        assert self.t is not None
        try:
            cur = P.parse_link_stats(self.t.request(P.Cmd.GET_LINK_STATS, retries=1).payload)
        except (TransportError, Exception) as e:  # noqa: BLE001
            self.log(f"stats {label} unavailable ({e})")
            return
        self.n_counters = len(cur)
        if self.start_stats is None:
            self.start_stats = cur
            self.log(f"stats {label} ({len(cur)} counters) " + " ".join(f"{k}={v}" for k, v in cur.items()))
        else:
            prev = self.last_stats or self.start_stats
            d = deltas(prev, cur)
            dt = deltas(self.start_stats, cur)
            self.log(f"stats {label} delta {fmt(d) or 'none'} (since start: {fmt(dt) or 'none'})")
        self.last_stats = cur

    def wait_and_reopen(self) -> float:
        """The link is down: wait for the port to come back and reopen. Returns the down time."""
        t_down = self.clock()
        self.close()
        gone_logged = False
        deadline = t_down + self.reopen_timeout
        while True:
            if not self.present(self.port):
                if not gone_logged:
                    self.log(f"port {self.port} gone")
                    gone_logged = True
            else:
                if gone_logged:
                    self.log(f"port {self.port} back after {self.clock() - t_down:.2f} s")
                    gone_logged = False
                try:
                    self.open()
                    down = self.clock() - t_down
                    self.log(f"reopen {self.port} ok (down {down:.2f} s)")
                    self.stats("reopen")
                    return down
                except (TransportError, OSError) as e:
                    self.log(f"reopen {self.port} failed: {e}" + _wec(str(e)))
                    self.close()
            if self.clock() >= deadline:
                raise TransportError(f"{self.port} did not come back within {self.reopen_timeout:g} s")
            self.sleep(self.poll_down)

    # --- requests -------------------------------------------------------------------------------------------

    def one(self) -> None:
        assert self.t is not None
        if self.mode == "ping":
            data = os.urandom(16)
            if self.t.request(P.Cmd.PING, data, retries=0).payload != data:
                raise TransportError("PING echo differs")
        elif self.mode == "capture":
            n = 4096
            body = self.t.request(P.Cmd.TEST_PATTERN, P.TestPatternReq(n, 3).pack(), retries=0).payload
            if body != P.test_pattern_bytes(n, 3):
                raise TransportError("TEST_PATTERN content differs")
        else:
            self.t.request(P.Cmd.GET_STATUS, retries=0)

    def run(self) -> int:
        self.log(f"host: USB selective suspend {selective_suspend()}; port {port_info(self.port)}")
        self.log(f"soak mode {self.mode}, interval {self.interval:g} s, duration {self.duration:g} s")
        try:
            self.open()
        except (TransportError, OSError) as e:
            self.log(f"FAILED: cannot open {self.port}: {e}{_wec(str(e))}")
            return 2
        self.stats("start")
        t_end = self.clock() + self.duration
        last_ok = self.clock()
        name = {"status": "GET_STATUS", "ping": "PING", "capture": "TEST_PATTERN"}[self.mode]
        try:
            while self.clock() < t_end:
                t0 = self.clock()
                self.st.requests += 1
                try:
                    self.one()
                    now = self.clock()
                    self.st.latencies.append(now - t0)
                    self.st.longest_gap = max(self.st.longest_gap, now - last_ok)
                    last_ok = now
                except (TransportError, OSError) as e:
                    self.st.failures += 1
                    text = str(e)
                    self.log(f"fail #{self.st.failures} {name}: {text}{_wec(text)}")
                    if self._link_down(text):
                        self.st.drops += 1
                        try:
                            self.st.down_times.append(self.wait_and_reopen())
                        except TransportError as e2:
                            self.log(f"FAILED: {e2}")
                            break
                    elif self.t is not None:
                        try:
                            self.t.recover()
                        except TransportError as e3:
                            self.log(f"recover failed: {e3}; treating as a drop")
                            self.st.drops += 1
                            try:
                                self.st.down_times.append(self.wait_and_reopen())
                            except TransportError as e2:
                                self.log(f"FAILED: {e2}")
                                break
                rest = self.interval - (self.clock() - t0)
                if rest > 0:
                    self.sleep(rest)
        except KeyboardInterrupt:
            self.log("interrupted")
        if self.t is not None:
            self.stats("end")
        self.close()
        self.summary()
        return 0 if not (self.st.failures or self.st.drops) else 1

    def _link_down(self, text: str) -> bool:
        """A write/read error of the port itself (as opposed to a missing answer) means the device left."""
        t = text.lower()
        return any(k in t for k in ("write to", "read from", "closed by the device", "cannot open")) or \
            not self.present(self.port)

    def summary(self) -> None:
        s = self.st
        lat = sorted(s.latencies)
        lat_txt = (f"latency median {statistics.median(lat) * 1e3:.1f} ms, p99 {lat[int(0.99 * (len(lat) - 1))] * 1e3:.1f}"
                   f" ms, max {lat[-1] * 1e3:.1f} ms" if lat else "no successful request")
        cd = fmt(deltas(self.start_stats, self.last_stats)) if self.start_stats and self.last_stats else "unavailable"
        self.log(f"summary requests {s.requests}, failures {s.failures}, drops {s.drops}"
                 + (f" (down {', '.join(f'{d:.2f}' for d in s.down_times)} s)" if s.down_times else "")
                 + f", longest gap {s.longest_gap:.2f} s, {lat_txt}; counters delta {cd or 'none'}")


def deltas(a: dict[str, int] | None, b: dict[str, int] | None) -> dict[str, int]:
    if not a or not b:
        return {}
    return {k: b[k] - a.get(k, 0) for k in WATCH if k in b and b[k] != a.get(k, 0)}


def fmt(d: dict[str, int]) -> str:
    return " ".join(f"{k} {v:+d}" for k, v in d.items())


def _wec(text: str) -> str:
    w = winerror(text)
    return f" [winerror {w}]" if w is not None else ""


def _auto_port() -> str:
    for c in list_candidates():
        if c.vid == PIDCODES_VID and c.pid in PIDCODES_TEST_PIDS:
            return c.device
    raise SystemExit("fnirsi1013d-soak: no scope serial port (1209:0001) found; pass --port")


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-soak", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("FNIRSI_MCP_PORT", "auto"))
    ap.add_argument("--duration", type=float, default=1800.0, help="seconds (default 30 min)")
    ap.add_argument("--interval", type=float, default=0.1, help="seconds between requests (default 0.1)")
    ap.add_argument("--mode", choices=("status", "ping", "capture"), default="status",
                    help="GET_STATUS, PING, or a content-checked 4 KiB TEST_PATTERN")
    ap.add_argument("--reopen-timeout", type=float, default=60.0, help="seconds to wait for the port after a drop")
    ap.add_argument("--log", help="also append the lines to this file")
    a = ap.parse_args(argv)
    port = _auto_port() if a.port.lower() == "auto" else a.port
    fh = open(a.log, "a", encoding="utf-8") if a.log else None

    def out(line: str) -> None:
        print(line, flush=True)
        if fh:
            fh.write(line + "\n")
            fh.flush()
    try:
        rc = Soak(port, duration=a.duration, interval=a.interval, mode=a.mode, reopen_timeout=a.reopen_timeout,
                  out=out).run()
    finally:
        if fh:
            fh.close()
    sys.exit(rc)


if __name__ == "__main__":
    main()
