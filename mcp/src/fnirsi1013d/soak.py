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
         "ep0_setups", "ep0_bad_setups", "rx_stalls", "hdr_crc_errors", "payload_crc_errors", "rx_timeouts",
         "restarts")
#: counters 47 and 57, retired in fw 0.2.21 (always 0 there; kept so decoders by count work): a delta is reported
#: as a firmware older than 0.2.21
RETIRED = ("usb_irq_polled", "usb_irq_races")
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
    restarts: int = 0
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
        self.proto_minor = 0
        #: device events already printed (GET_EVENT_LOG total at the last read)
        self.events_seen = 0
        #: fw 0.2.24 bus-level ring entries seen (its own since-boot counter)
        self.bus_seen = 0
        #: fw 0.2.15 reset snapshots seen (kept for the run)
        self.snapshots: list[P.EventLog] = []
        #: last GET_STATUS uptime_ms seen (a decrease = the device restarted)
        self.last_uptime: int | None = None

    def log(self, msg: str) -> None:
        self.say(f"{_dt.datetime.now().isoformat(timespec='milliseconds')} {msg}")

    # --- link ----------------------------------------------------------------------------------------------

    def open(self, after_drop: bool = False) -> None:
        """Open the port. After a drop the very first requests are the fast-path GET_EVENT_LOG and GET_LINK_STATS
        (fw 0.2.12), so the event ring still holds what happened before the reset; then GET_INFO."""
        self.t = Transport(self.link_factory(self.port), retries=0)
        if after_drop and self.proto_minor >= 5:
            self.events("reopen")
            self.stats("reopen")
        info = P.InfoResp.unpack(self.t.request(P.Cmd.GET_INFO, retries=1).payload)
        self.proto_minor = info.proto_minor
        self.log(f"open {self.port} (fw {info.fw_version_str}, proto {info.proto_major}.{info.proto_minor})")
        self.uptime("reopen" if after_drop else "open")

    def check_uptime(self, up: int) -> bool:
        """Record GET_STATUS uptime_ms; True (and a DEVICE RESTARTED line) when it went backwards."""
        prev, self.last_uptime = self.last_uptime, up
        if prev is not None and up < prev:
            self.st.restarts += 1
            self.log(f"DEVICE RESTARTED (uptime went backwards: {prev} -> {up} ms)")
            self.events_seen = self.bus_seen = 0  # the device's event counters start over too
            self.restart_record()
            return True
        return False

    def restart_record(self) -> None:
        """fw 0.2.19: read the restart record (GET_LINK_STATS 49-56) and the event log right away, raw."""
        if self.t is None:
            return
        try:
            cur = P.parse_link_stats(self.t.request(P.Cmd.GET_LINK_STATS, retries=1).payload)
        except Exception as e:  # noqa: BLE001
            self.log(f"restart record unavailable ({e})")
            return
        if "restarts" in cur:
            et = cur.get("restart_exc_type", 0)
            self.log("restart record (raw, map with the ELF): " + " ".join(
                f"{k}=0x{cur[k]:08X}" for k in P.RESTART_RECORD if k in cur)
                + f" [exc type {et}: {P.RESTART_EXC_TYPES.get(et, '?')}]")
        else:
            self.log(f"restart record: not in this firmware ({len(cur)} counters)")
        if self.proto_minor >= 5:
            self.events("restart")

    def uptime(self, label: str) -> None:
        assert self.t is not None
        try:
            st = P.StatusResp.unpack(self.t.request(P.Cmd.GET_STATUS, retries=1).payload)
        except Exception as e:  # noqa: BLE001
            self.log(f"uptime {label} unavailable ({e})")
            return
        self.log(f"uptime {label} {st.uptime_ms} ms")
        self.check_uptime(st.uptime_ms)

    def close(self) -> None:
        if self.t is not None:
            try:
                self.t.close()
            except Exception:  # noqa: BLE001
                pass
            self.t = None

    def events(self, label: str, t_drop: _dt.datetime | None = None) -> None:
        """Print GET_EVENT_LOG entries not printed yet, with device ms and the matching host wall-clock time."""
        assert self.t is not None
        try:
            # fw 0.2.18: a plain read keeps the reset snapshot; it is re-armed below, after it was saved
            log = P.parse_event_log(self.t.request(P.Cmd.GET_EVENT_LOG, retries=1).payload)
        except Exception as e:  # noqa: BLE001
            self.log(f"events {label} unavailable ({e})")
            return
        host_now = _dt.datetime.now()
        if log.total < self.events_seen:
            self.log(f"events {label}: the device's event counter went back ({self.events_seen} -> {log.total}): "
                     "it restarted")
            self.events_seen = 0
        new = log.total - self.events_seen
        shown = log.events[-new:] if 0 < new <= len(log.events) else (log.events if new > 0 else [])
        lost = max(0, new - len(log.events))
        self.log(f"events {label}: {len(shown)} new of {log.total} since boot (device now {log.now_ms} ms)"
                 + (f", {lost} older ones overwritten" if lost else ""))
        for e in shown:
            at = host_now - _dt.timedelta(milliseconds=log.now_ms - e.ms)
            self.log(f"  device {e.ms} ms (~host {at.isoformat(timespec='milliseconds')}) {e.describe()}")
        if log.has_bus_ring:  # fw 0.2.24: bus-level events the EP0 / DTR chatter may have evicted from the main ring
            if log.bus_total < self.bus_seen:
                self.bus_seen = 0
            newb = log.bus_total - self.bus_seen
            showb = log.bus_events[-newb:] if 0 < newb <= len(log.bus_events) else (log.bus_events if newb > 0 else [])
            printed = {(e.ms, e.type, e.a, e.b) for e in shown}
            extra = [e for e in showb if (e.ms, e.type, e.a, e.b) not in printed]
            if newb > 0:
                self.log(f"events {label}: bus ring {newb} new of {log.bus_total} since boot"
                         + (f", {len(extra)} not in the main ring:" if extra else ""))
            for e in extra:
                at = host_now - _dt.timedelta(milliseconds=log.now_ms - e.ms)
                self.log(f"  bus: device {e.ms} ms (~host {at.isoformat(timespec='milliseconds')}) {e.describe()}")
            self.bus_seen = log.bus_total
        if log.frozen:  # fw 0.2.15+: the events right before the first bus reset since the snapshot was armed
            at = host_now - _dt.timedelta(milliseconds=log.now_ms - log.reset_ms)
            self.log(f"events {label}: pre-reset snapshot: bus reset at device {log.reset_ms} ms "
                     f"(~host {at.isoformat(timespec='milliseconds')}), {len(log.snapshot)} events before it:")
            for e in log.snapshot:
                at = host_now - _dt.timedelta(milliseconds=log.now_ms - e.ms)
                self.log(f"  before reset: device {e.ms} ms (~host {at.isoformat(timespec='milliseconds')}) "
                         f"{e.describe()}")
            self.snapshots.append(log)
        self.events_seen = log.total
        if log.has_snapshot and (log.frozen or label == "start"):
            try:  # saved: now re-arm it for the next reset (fw 0.2.18 flag; older firmware ignores the byte)
                self.t.request(P.Cmd.GET_EVENT_LOG, bytes([P.EVENT_LOG_REARM]), retries=1)
                self.log(f"events {label}: snapshot re-armed")
            except Exception as e:  # noqa: BLE001
                self.log(f"events {label}: re-arm failed ({e})")

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
                    self.open(after_drop=True)  # its uptime check reads the restart record on a restart
                    down = self.clock() - t_down
                    self.log(f"reopen {self.port} ok (down {down:.2f} s)")
                    if self.proto_minor < 5:
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
            st = P.StatusResp.unpack(self.t.request(P.Cmd.GET_STATUS, retries=0).payload)
            self.check_uptime(st.uptime_ms)

    def run(self) -> int:
        self.log(f"host: USB selective suspend {selective_suspend()}; port {port_info(self.port)}")
        self.log(f"soak mode {self.mode}, interval {self.interval:g} s, duration {self.duration:g} s")
        try:
            self.open()
        except (TransportError, OSError) as e:
            self.log(f"FAILED: cannot open {self.port}: {e}{_wec(str(e))}")
            return 2
        self.stats("start")
        if self.proto_minor >= 5:
            self.events("start")
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
            if self.proto_minor >= 5:
                self.events("end")
            self.stats("end")
            self.uptime("end")
        self.close()
        self.summary()
        return 0 if not (self.st.failures or self.st.drops or self.st.restarts) else 1

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
        ret = deltas(self.start_stats, self.last_stats, RETIRED)
        if ret:
            cd = (cd + " " if cd else "") + f"(retired counters moved, firmware older than 0.2.21: {fmt(ret)})"
        self.log(f"summary requests {s.requests}, failures {s.failures}, drops {s.drops}"
                 + (f" (down {', '.join(f'{d:.2f}' for d in s.down_times)} s)" if s.down_times else "")
                 + f", device restarts {s.restarts}"
                 + f", longest gap {s.longest_gap:.2f} s, {lat_txt}; counters delta {cd or 'none'}")


def deltas(a: dict[str, int] | None, b: dict[str, int] | None, names: tuple[str, ...] = WATCH) -> dict[str, int]:
    if not a or not b:
        return {}
    return {k: b[k] - a.get(k, 0) for k in names if k in b and b[k] != a.get(k, 0)}


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
