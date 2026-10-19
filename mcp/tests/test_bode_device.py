"""Protocol 1.20 (fw 0.2.66, docs/protocol.md row 60): the on-device Bode plot. BODE_CTRL / READ_BODE codecs against
the firmware vectors, the simulator's engine (SimBode), ``bode.device_sweep`` against a host sweep of the same DUT,
the thru, STOP / abort / a host ARM (reason 5), the view's restore, and scope_bode_sweep(where='device')."""

from __future__ import annotations

import json
import math
import time
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import bode as B
from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal
from fnirsi1013d.transport import DeviceError

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())
FAST_IN, FAST_OUT = "dut_in:r=1.6k,c=1n", "dut_out:r=1.6k,c=1n"


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


def test_reason_readout_fault():
    # fw 0.2.71: reason 8 ends a sweep whose pre-scan clipped at 5 V/div with a small fundamental (a readout fault)
    h = P.BodeHeader(state=4, reason=8)
    assert h.state_name == "error"
    assert h.reason_text.startswith("ADC readout fault")


def _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, **kw):
    kw.setdefault("variant", "al3f")
    kw.setdefault("noise_lsb", 1.0)
    srv = sim_factory(**kw)
    srv.device.cfg.signals = (parse_signal(ch1), parse_signal(ch2))
    return srv


def _state(s):
    return (s.get_channel(0, cached=False), s.get_channel(1, cached=False), s.get_timebase(cached=False),
            s.get_trigger(cached=False), s.get_gen(), s.get_decim().filter, s.get_decim().flags)


def _wait_done(s, timeout=60.0):
    t0 = time.monotonic()
    while True:
        h, rows = s.read_bode()
        if not h.running:
            return h, rows
        assert time.monotonic() - t0 < timeout
        time.sleep(0.01)


def test_codecs_match_the_firmware_vectors():
    raw = frame_payload("req_bode_ctrl")
    r = P.BodeCtrlReq.unpack(raw)
    assert (r.action, r.flags, r.settle, r.ch2_mode, r.start_chz, r.stop_chz, r.points) == \
        (P.BODE_RUN, P.BODE_F_SETTINGS, 1, 0, 10_000, 100_000_000, 50)
    assert P.BodeCtrlReq.make(P.BODE_RUN, 100.0, 1e6, 50).pack() == raw
    assert P.ReadBodeReq().pack() == frame_payload("req_read_bode")
    assert P.REQUEST_SIZES[P.Cmd.BODE_CTRL] == 16 and P.REQUEST_SIZES[P.Cmd.READ_BODE] == 8
    assert P.CMD_CAP[P.Cmd.BODE_CTRL] == P.CMD_CAP[P.Cmd.READ_BODE] == P.Caps.DECIM
    assert P.BodeHeader.size() == 32 and P.BodeRow.size() == 20 and P.BodeCtrlResp.size() == 40
    assert P.BODE_MAX_ROWS == 3326 and P.Features.BODE == 0x40 and P.DisplayMode.BODE == 5
    assert P.BodeCtrlReq.make(P.BODE_RUN, background=False).flags == P.BODE_F_NO_BACKGROUND


def test_row_flags_are_the_host_names_and_f_comes_from_m():
    host_names = set(B.BAD_FLAGS) | {"ch1_low", "ch2_small", "spliced", "fir_off", "no_cal", "cal_range",
                                     "cal_flagged"}
    assert set(P.BODE_ROW_FLAGS) == host_names and len(P.BODE_ROW_FLAGS) == 14
    # the bit order of row 60: bits 0, 1, 2, 5, 6, 7, 8 are the invalid ones (the host's BAD_FLAGS)
    assert {P.BODE_ROW_FLAGS[i] for i in (0, 1, 2, 5, 6, 7, 8)} == set(B.BAD_FLAGS)
    assert P.bode_flags_from_names(["spliced", "low_snr", "x"], retried=True) == 0x0200 | 0x0020 | 0x4000
    for f in (1.0, 23.9, 1234.5, 99.5e3, 1.49e6, 10e6):
        pp = B.plan_point(f)
        row = P.BodeRow(pp.m, int(round(pp.f_actual * 100)), -301, -4500, 0, -301, 3, 4, 0x0201)
        assert row.f_hz == pp.f_actual and abs(row.f_chz / 100 - row.f_hz) <= 0.005
        back = P.parse_read_bode(P.BodeHeader(planned=1).pack() + row.pack())[1][0]
        assert back == row and back.flag_names == ["ch1_clipped", "spliced"] and back.measured
        pr = B.point_from_row(row, pp, (10.0, 1.0))
        assert pr.mag_db == pytest.approx(-3.01) and pr.phase_deg == pytest.approx(-45.0)
        assert pr.ch1_volts_per_div == P.RANGE_VOLTS_PER_DIV[3] * 10 and pr.flags == ["ch1_clipped", "spliced"]
        assert abs(pr.x2) / abs(pr.x1) == pytest.approx(10 ** (-3.01 / 20))
    assert not P.BodeRow(flags=P.BODE_ROW_NOT_MEASURED).measured


def test_device_and_host_sweeps_agree(sim_factory):
    """The acceptance test of the device path: the same DUT swept by the scope (BODE_CTRL, READ_BODE, rows as
    SweepResult) and by the host tool agree point by point, flags and ranges included, and both match the RC."""
    srv = _sim(sim_factory)
    with Scope.connect(srv.url) as s:
        assert s.status().features & P.Features.BODE
        before = _state(s)
        progress = []
        dev = B.device_sweep(s, 30e3, 1e6, 8, settle="fast", sleep=lambda x: time.sleep(0.005),
                             progress=lambda i, n, t: progress.append((i, n)))
        assert s.status().display_mode == P.DisplayMode.BODE and dev.meta["view_shown"]
        host = B.BodeRunner(s, settle="fast", sleep=lambda x: None, busy_retry_s=0).run(B.plan_sweep(30e3, 1e6, 8))
        s.bode_ctrl(P.BODE_CLOSE)
        assert s.status().display_mode == P.DisplayMode.SCOPE
        assert _state(s) == before  # the view gave every setting back
    assert dev.stopped_early is None and dev.meta["where"] == "device" and progress and progress[-1][1] == 8
    assert len(dev.points) == len(host.points) == 8
    for a, b in zip(dev.points, host.points):
        assert a.plan == b.plan  # same M, N, record: f recomputed from M
        assert abs(a.mag_db - b.mag_db) < 0.02 and abs(a.phase_deg - b.phase_deg) < 0.2, (a.plan.f_actual,)
        assert a.flags == b.flags and (a.ch1_range, a.ch2_range) == (b.ch1_range, b.ch2_range)
        h = 1 / (1 + 2j * math.pi * a.plan.f_actual * 1.6e3 * 1e-9)
        assert abs(a.mag_db - B.db(abs(h))) < 0.03
    assert dev.range_switches == host.range_switches >= 1
    md, mh = dev.summary(), host.summary()
    assert md["shape"] == mh["shape"] == "low-pass"
    assert md["minus_3db_hz"][0] == pytest.approx(mh["minus_3db_hz"][0], rel=2e-3)
    row = dev.table()[0]
    assert row["snr_db"] is None and row["ch2_codes"] is None and set(row) == set(host.table()[0])
    assert any(c.startswith("device sweep:") for c in dev.caveats)
    assert any("still shows its Bode plot" in c for c in dev.caveats)


def test_device_thru_measure_apply_and_stale(sim_factory):
    srv = _sim(sim_factory, ch2=FAST_IN + ",gain=0.95")
    with Scope.connect(srv.url) as s:
        with pytest.raises(DeviceError) as e:
            s.bode_ctrl(P.BODE_THRU_ON)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 4)
        th = B.device_sweep(s, 30e3, 1e6, 5, settle="fast", thru="measure", sleep=lambda x: None)
        assert th.role == "thru" and all(abs(p.mag_db - B.db(0.95)) < 0.02 for p in th.points)
        h = s.bode_ctrl().hdr
        assert h.thru_state == "valid" and h.thru_applied
        raw = B.device_sweep(s, 30e3, 1e6, 5, settle="fast", sleep=lambda x: None)  # switches the thru off
        assert raw.thru_cal is None and any("switched off" in c for c in raw.caveats)
        assert all(abs(p.mag_db - B.db(0.95)) < 0.02 for p in raw.points)
        cal = B.device_sweep(s, 30e3, 1e6, 5, settle="fast", thru="device", sleep=lambda x: None)
        assert cal.thru_cal == "device" and all(abs(p.mag_db) < 0.02 and abs(p.phase_deg) < 0.2 for p in cal.points)
        other = B.device_sweep(s, 30e3, 1e6, 6, settle="fast", thru="device", sleep=lambda x: None)
        assert s.bode_ctrl().hdr.thru_state == "stale"
        assert any("no_cal" in p.flags for p in other.points)  # a plan point the thru does not have
        # a host ThruCal applies to the device's raw rows as to a host sweep
        tc = B.ThruCal.from_sweep(th, "t", (1.0, 1.0))
        app = B.device_sweep(s, 30e3, 1e6, 5, settle="fast", thru=tc, sleep=lambda x: None)
        assert app.thru_cal == "t" and all(abs(p.mag_db) < 0.02 for p in app.points)
        s.bode_ctrl(P.BODE_THRU_CLEAR)
        assert s.bode_ctrl().hdr.thru_state == "none"


def test_stop_abort_host_arm_and_errors(sim_factory):
    srv = _sim(sim_factory)
    with Scope.connect(srv.url) as s:
        for req, detail in ((P.BodeCtrlReq(11), 0), (P.BodeCtrlReq(1, P.BODE_F_SETTINGS, 4, 0, 100, 200, 5), 1),
                            (P.BodeCtrlReq(1, P.BODE_F_SETTINGS, 1, 2, 100, 200, 5), 2),
                            (P.BodeCtrlReq(1, P.BODE_F_SETTINGS, 1, 0, 50, 200, 5), 3),
                            (P.BodeCtrlReq(1, P.BODE_F_SETTINGS, 1, 0, 100, 200, 201), 4)):
            with pytest.raises(DeviceError) as e:
                s.bode_ctrl(req)
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, detail)
        r = s.bode_ctrl(P.BodeCtrlReq.make(P.BODE_RUN, 30e3, 1e6, 60, settle=0))
        assert r.hdr.running and r.hdr.planned == 60 and r.estimate_ms > 0 and r.hdr.sweep_id == 1
        with pytest.raises(DeviceError) as e:
            s.bode_ctrl(P.BODE_RUN)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 2)
        s.bode_ctrl(P.BODE_STOP)
        h, rows = _wait_done(s)
        assert (h.state_name, h.reason) == ("stopped", 6) and 0 < h.measured < 60 and len(rows) == 60
        assert not rows[-1].measured and rows[-1].m and rows[0].measured
        res = B.result_from_device(h, rows)
        assert len(res.points) == h.measured and "stopped: stopped" in res.stopped_early
        # a host capture during a sweep ends it (reason 5)
        s.bode_ctrl(P.BodeCtrlReq.make(P.BODE_RUN, 30e3, 1e6, 60, settle=0))
        while s.read_bode()[0].measured < 1:
            time.sleep(0.005)
        assert s.capture(["CH1"]) is not None
        h, _ = _wait_done(s)
        assert (h.state_name, h.reason, h.sweep_id) == ("error", 5, 2)
        assert h.reason_text == "the PC armed its own capture during the sweep"
        # abort, then close
        s.bode_ctrl(P.BodeCtrlReq.make(P.BODE_RUN, 30e3, 1e6, 60, settle=0))
        s.bode_ctrl(P.BODE_ABORT)
        h = s.bode_ctrl().hdr
        assert (h.state, h.reason, h.current) == (3, 6, -1)
        s.bode_ctrl(P.BODE_CLOSE)
        assert not s.bode_ctrl().hdr.view & 1 and s.status().display_mode == P.DisplayMode.SCOPE
        srv.device.ui_busy = True
        srv.device.ui_hold = P.BusyDetail.SAVE  # 1.23: only a step that must finish keeps E_BUSY
        with pytest.raises(DeviceError) as e:
            s.bode_ctrl()
        assert e.value.status == P.Status.E_BUSY and e.value.detail == P.BusyDetail.SAVE
        srv.device.ui_hold = 0
        assert s.bode_ctrl().hdr is not None  # a menu alone closes for it
        assert s.status().ui_busy == 0
    with Scope.connect(_sim(sim_factory, variant="al3x").url) as s:
        assert not s.status().features & P.Features.BODE
        with pytest.raises(B.BodeError, match="decimation filter"):
            B.device_sweep(s, 30e3, 1e6, 3)
    with pytest.raises(ValueError, match="presets"):
        B.device_settle_code(7)
    with pytest.raises(ValueError, match="'auto' or 'fixed'"):
        B.device_ch2_code("0.5V")


def test_bode_tool_device_mode(sim_factory, tmp_path):
    from test_server import client_for

    srv = _sim(sim_factory, noise_lsb=0.5)
    sweep = dict(start="30kHz", stop="1MHz", points=6, settle="fast")

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_bode_sweep", **sweep, where="device")
            assert r["confirmation_required"] and r["summary"].startswith("the scope runs the sweep itself")
            dev = await c.call("scope_bode_sweep", **sweep, where="device", confirm_token=r["token"])
            assert dev["ok"] and dev["where"] == "device" and dev["points"] == 6
            assert dev["device"]["state"] == "done" and dev["device"]["view_shown"]
            assert "_bode_device" in dev["files"]["json"] and Path(dev["files"]["npz"]).is_file()
            st = await c.call("scope_status")
            assert st["display_mode"] == "bode" and "bode" in st["features"]
            cap = await c.call("scope_capture", channels=["CH1"])
            assert any("Bode plot" in x for x in cap["caveats"])
            args = {**sweep, "where": "device", "close_view": True}
            r = await c.call("scope_bode_sweep", **args)
            dev2 = await c.call("scope_bode_sweep", **args, confirm_token=r["token"])
            assert not dev2["device"]["view_shown"]
            assert (await c.call("scope_status"))["display_mode"] == "scope"
            r = await c.call("scope_bode_sweep", **sweep)
            host = await c.call("scope_bode_sweep", **sweep, confirm_token=r["token"])
            assert set(host) - set(dev) == {"connection"} or set(host) - set(dev) == set()
            for a, b in zip(dev["table"], host["table"]):
                assert a["f_actual"] == b["f_actual"] and abs(a["mag_db"] - b["mag_db"]) < 0.03
                assert abs(a["phase_deg"] - b["phase_deg"]) < 0.3
            assert "presets" in await c.fails("scope_bode_sweep", **{**sweep, "settle": 7}, where="device")
            assert "'auto' or 'fixed'" in await c.fails("scope_bode_sweep", **sweep, ch2_range="0.5V",
                                                         where="device")
            assert "DC coupled" in await c.fails("scope_bode_sweep", **sweep, ch2_coupling="ac", where="device")
    anyio.run(main)
