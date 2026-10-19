"""fnirsi1013d-soak: event log, drop detection, waiting for the port and reopening (simulator, scripted failures)."""

from __future__ import annotations

import pytest

from fnirsi1013d import soak as S
from fnirsi1013d.transport import TransportError, open_link

WIN22 = ("write to COM10 failed: WriteFile failed (PermissionError(13, 'The device does not recognize the "
         "command.', None, 22))")


def test_winerror_parsing():
    assert S.winerror(WIN22) == 22
    assert S.winerror("could not open port 'COM10': FileNotFoundError(2, 'x', None, 2)") == 2
    assert S.winerror("[WinError 31] A device attached to the system is not functioning") == 31
    assert S.winerror("no response to GET_STATUS") is None


class FlakyLink:
    """A real simulator link whose n-th write (counted over all links of a test) fails like the bench drop."""

    writes = 0

    def __init__(self, url: str, fail_at: set[int]):
        self.inner = open_link(url)
        self.fail_at = fail_at
        self.description = self.inner.description

    def write(self, data: bytes) -> None:
        FlakyLink.writes += 1
        if FlakyLink.writes in self.fail_at:
            raise TransportError(WIN22)
        self.inner.write(data)

    def read(self, max_bytes: int, timeout: float) -> bytes:
        return self.inner.read(max_bytes, timeout)

    def flush_input(self) -> None:
        self.inner.flush_input()

    def close(self) -> None:
        self.inner.close()


def run_soak(sim, *, fail_at=(), present=None, **kw):
    FlakyLink.writes = 0
    lines: list[str] = []
    kw.setdefault("duration", 0.6)
    kw.setdefault("interval", 0.01)
    s = S.Soak(sim.url, out=lines.append, link_factory=lambda url: FlakyLink(url, set(fail_at)),
               present=present or (lambda port: True), poll_down=0.01, **kw)
    return s.run(), lines, s


@pytest.mark.parametrize("mode", ["status", "ping", "capture"])
def test_clean_soak(sim, mode):
    rc, lines, s = run_soak(sim, mode=mode)
    text = "\n".join(lines)
    assert rc == 0 and s.st.failures == 0 and s.st.requests > 3, text
    assert "host: USB selective suspend" in lines[0] and "open tcp://" in text
    assert "stats start (31 counters)" in text and "stats end delta" in text
    assert "summary requests" in lines[-1] and "failures 0, drops 0" in lines[-1]
    assert all(ln[:4].isdigit() and ln[10] == "T" and ln[19] == "." for ln in lines)  # ISO time with ms


def test_drop_is_logged_waited_out_and_reopened(sim):
    calls = {"n": 0}

    def present(port: str) -> bool:  # gone for the first two polls after the drop, then back
        calls["n"] += 1
        if calls["n"] == 1:  # what the device logs during a host port reset (fw 0.2.15 freezes a snapshot)
            sim.device.log_event(5, 0x22, 0x2101)
            sim.device.log_event(2)
            sim.device.log_event(1)
        return calls["n"] > 2
    rc, lines, s = run_soak(sim, fail_at={12}, present=present, duration=0.8)
    text = "\n".join(lines)
    assert rc == 1 and s.st.drops == 1 and s.st.failures == 1, text
    assert "fail #1 GET_STATUS: write to COM10 failed" in text and "[winerror 22]" in text
    i_gone, i_back, i_reopen = (next(i for i, ln in enumerate(lines) if k in ln)
                                for k in (" gone", "back after", "reopen tcp://"))
    assert i_gone < i_back < i_reopen
    assert "ok (down" in lines[i_reopen]
    # fw 0.2.12: the first requests after the reopen are GET_EVENT_LOG, then GET_LINK_STATS, then GET_INFO
    i_ev = next(i for i, ln in enumerate(lines) if "events reopen:" in ln)
    i_st = next(i for i, ln in enumerate(lines) if "stats reopen delta" in ln)
    i_open = max(i for i, ln in enumerate(lines) if " open tcp://" in ln)
    assert i_back < i_ev < i_st < i_open < i_reopen
    assert any("dtr 0" in ln for ln in lines[i_ev:i_st])  # the drop, decoded with device ms and host time
    assert "dtr_down +1" in lines[i_st]  # the simulator saw the old connection close
    snap = next(i for i, ln in enumerate(lines) if "pre-reset snapshot: bus reset at device" in ln)
    assert i_ev < snap < i_st and "before reset:" in lines[snap + 1] and "ep0_setup bRequest 0x22" in "\n".join(
        lines[snap:i_st]) and len(s.snapshots) == 1
    assert "drops 1 (down " in lines[-1] and "longest gap" in lines[-1]
    assert s.st.requests > 13  # it carried on after the reopen (the drop was at write 12)


def test_port_that_never_returns(sim):
    rc, lines, s = run_soak(sim, fail_at={12}, present=lambda port: False, duration=5.0, reopen_timeout=0.1)
    assert rc == 1 and s.st.drops == 1
    assert any("FAILED: tcp://" in ln and "did not come back" in ln for ln in lines)
    assert "summary" in lines[-1]


def test_cannot_start(sim):
    s = S.Soak("tcp://127.0.0.1:1", out=lambda ln: None, duration=0.1)
    assert s.run() == 2


def test_selective_suspend_parsing(monkeypatch):
    class R:
        stdout = ("    Current AC Power Setting Index: 0x00000001\n"
                  "    Current DC Power Setting Index: 0x00000000\n")
    monkeypatch.setattr(S.sys, "platform", "win32")
    monkeypatch.setattr(S.subprocess, "run", lambda *a, **k: R())
    assert S.selective_suspend() == "AC enabled, DC disabled"


def test_scope_event_log_api_and_tool(sim, sim_factory, tmp_path):
    import anyio

    from fnirsi1013d.scope import Scope
    from test_server import client_for
    sim.device.log_event(2)
    sim.device.log_event(1)
    with Scope.connect(sim.url) as s:
        log = s.event_log()
        assert log.total >= 2 and [e.name for e in log.events][-2:] == ["suspend", "bus_reset"]

    async def main():
        async with client_for(sim, tmp_path) as c:
            r = await c.call("scope_event_log", last=16)
            types = [e["type"] for e in r["events"]]
            i = types.index("suspend")
            assert types[i + 1] == "bus_reset" and "dtr" in types[i:] and all(e["ago_ms"] >= 0 for e in r["events"])
        async with client_for(sim_factory(legacy=True), tmp_path) as c:
            assert "protocol 1.5" in await c.fails("scope_event_log")
    anyio.run(main)


def test_device_self_restart_is_flagged_and_counted(sim):
    """Bench 0.2.12/0.2.16: the firmware restarted itself from RAM; #VER unchanged, uptime_ms went backwards."""
    import threading
    threading.Timer(0.3, sim.device.self_restart).start()
    rc, lines, s = run_soak(sim, duration=0.8)
    text = "\n".join(lines)
    assert rc == 1 and s.st.restarts == 1 and s.st.drops == 0, text
    assert "DEVICE RESTARTED (uptime went backwards: " in text and "device restarts 1" in lines[-1]
    i = next(i for i, ln in enumerate(lines) if "DEVICE RESTARTED" in ln)
    assert "restart record" in lines[i + 1]  # read right away (31-counter simulator: "not in this firmware")
    assert any("uptime open " in ln for ln in lines) and any("uptime end " in ln for ln in lines)


def test_snapshot_survives_plain_reads_and_is_rearmed_after_saving(sim):
    """fw 0.2.18: a plain GET_EVENT_LOG keeps the reset snapshot; the soak saves it, then re-arms with flags = 1."""
    from fnirsi1013d import protocol as P
    from fnirsi1013d.scope import Scope
    sim.device.log_event(2)
    sim.device.log_event(1)  # freezes a snapshot
    with Scope.connect(sim.url) as sc:
        assert sc.event_log().frozen and sc.event_log().frozen  # plain reads do not consume it
        with pytest.raises(Exception):
            sc.t.request(P.Cmd.GET_EVENT_LOG, b"\x08", retries=0)  # unknown flag bit: E_BAD_ARG
        with pytest.raises(Exception):
            sc.t.request(P.Cmd.GET_EVENT_LOG, b"\x06", retries=0)  # 1.22: trace on and off at once: E_BAD_ARG
        assert sc.event_log(rearm=True).frozen and not sc.event_log().frozen
    sim.device.log_event(2)
    sim.device.log_event(1)
    rc, lines, s = run_soak(sim, duration=0.3)
    text = "\n".join(lines)
    assert "events start: pre-reset snapshot" in text and "events start: snapshot re-armed" in text
    assert len(s.snapshots) == 1 and not sim.device.ev_frozen


def test_soak_prints_bus_ring_events_evicted_from_the_main_ring(sim):
    """fw 0.2.24: a TX abort flooded out of the 128-entry ring by DTR / EP0 chatter is still in the bus-level ring."""
    sim.device.log_event(13, 0, 64)
    sim.device.log_event(17, 0x40, 0x0001)
    for _ in range(140):
        sim.device.log_event(9, 1)
    rc, lines, s = run_soak(sim, duration=0.3)
    text = "\n".join(lines)
    assert "bus ring 2 new of 2 since boot, 2 not in the main ring:" in text, text
    assert "bus: device" in text and "TXCSR 0x0001 TxPktRdy" in text


def test_retired_counters_are_not_watched():
    """Counters 47 / 57 are always 0 since fw 0.2.21; a delta means older firmware and is reported separately."""
    assert not set(S.RETIRED) & set(S.WATCH)
    a = {"usb_irq_polled": 0, "usb_bus_resets": 0}
    b = {"usb_irq_polled": 3, "usb_bus_resets": 1}
    assert S.deltas(a, b) == {"usb_bus_resets": 1} and S.deltas(a, b, S.RETIRED) == {"usb_irq_polled": 3}
