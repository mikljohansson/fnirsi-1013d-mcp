"""Protocol 1.30 (fw-hostkeep, docs/protocol.md row 72): the host's last single capture is kept until the next host ARM,
whatever the scope's UI does meanwhile; when the latest capture's raw words are gone the screen's trace is returned.

The bench case (2026-11-12, fw 0.3.2): an agent armed a host single (CH2 falling, normal), ended its turn; the owner
caused the event several times and touched the scope (RUN). Coming back, the agent found run_state RUN, origin ui_run,
READ_RAW E_BUSY detail 1 ("UI capture running"), and after STOP E_STATE detail 1 ("ring overwritten"): the event was
lost. These tests replay that sequence on the simulator and get the event's words back."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope, ScopeError
from fnirsi1013d.transport import DeviceError

from test_server import client_for

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text("utf-8"))


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


# --- wire layouts against the firmware's vectors ------------------------------------------------------------------


def test_status_vector_carries_the_kept_capture():
    raw = frame_payload("resp_get_status_1_30")
    st = P.StatusResp.unpack(raw)
    assert (st.capture_id, st.kept_capture_id, st.run_state, st.capture_origin) == (885, 877, 1, 1)
    assert st.pack() == raw


def test_capture_info_vector_kept_flags():
    raw = frame_payload("resp_get_capture_info_1_30_kept")
    ci = P.CaptureInfo.unpack(raw)
    assert ci.header.capture_id == 877 and ci.header.kept_flags == P.KEPT_F_IS | P.KEPT_F_COPY
    assert ci.pack() == raw


def test_no_kept_vector_and_host_requests():
    err = P.parse_error_payload(frame_payload("resp_read_raw_e_state_no_kept"))
    assert err[0] == P.STATE_NO_KEPT
    req = P.ReadRawReq.unpack(frame_payload("host_req_read_raw_kept"))
    assert req.flags == P.ReadRawReq.KEPT and req.addr_mode == 1 and req.core_mask == 0x0F
    assert frame_payload("host_req_get_capture_info_kept")[0] == P.CAPINFO_REQ_KEPT


# --- simulator and client ------------------------------------------------------------------------------------------


def _bench_case(dev, scope: Scope):
    """The host's single, then RUN at the scope with more UI captures, then the UI armed in normal mode with no trigger
    coming. Returns the host capture's id and its CH1 words (as read before the UI touched anything)."""
    scope.set_trigger(source=0, edge=0, level=128, mode=1)
    scope.arm()
    st = scope.wait(2.0)
    assert st.acq_state == P.AcqState.DONE and st.capture_origin == P.CaptureOrigin.HOST
    assert st.kept_capture_id == st.capture_id
    kept_id = st.capture_id
    ring = dev.kept["ring"].copy()
    dev.ui_press_run()
    assert dev.ui_run_capture(3)
    dev.ui_set_trigger(level=255)  # no trigger comes any more (the event is over)
    dev.ui_rearm()
    return kept_id, ring


def test_bench_case_on_the_client(sim, scope):
    dev = sim.device
    # nothing kept before the first host single
    assert scope.kept_capture_info() is None
    with pytest.raises(DeviceError) as e:
        scope.read_raw(0x03, count=8, kept=True)
    assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.STATE_NO_KEPT)
    kept_id, ring = _bench_case(dev, scope)
    st = scope.status()
    assert st.run_state == P.RunState.RUN and st.capture_origin == P.CaptureOrigin.UI_RUN
    assert st.capture_id == kept_id + 3 and st.kept_capture_id == kept_id

    # the latest capture's words are gone: E_BUSY detail 1, as on the bench
    with pytest.raises(DeviceError) as e:
        scope.read_raw(0x03, count=8)
    assert (e.value.status, e.value.detail) == (P.Status.E_BUSY, P.BusyDetail.CAPTURE)

    ci = scope.kept_capture_info()
    assert ci is not None and ci.header.capture_id == kept_id and ci.header.origin == P.CaptureOrigin.HOST
    assert ci.header.kept_flags == P.KEPT_F_IS | P.KEPT_F_COPY
    cap = scope.read_kept_capture(["CH1", "CH2"], keep_raw=True)
    assert cap.capture_id == kept_id and cap.origin == "host"
    start = ci.header.capture_start_word
    tw = ci.trigger().total_words
    idx = (start + np.arange(tw)) % ring.shape[1]
    assert np.array_equal(cap.channels["CH1"].raw_a, ring[0, idx]) and np.array_equal(cap.channels["CH2"].raw_b,
                                                                                      ring[3, idx])
    assert scope.kept_capture_info().header.kept_flags & P.KEPT_F_READ

    # STOP at the scope: the latest is "ring overwritten", the kept capture is still there; a host STOP too
    dev.ui_press_stop()
    with pytest.raises(DeviceError) as e:
        scope.read_raw(0x03, count=8)
    assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 1)
    scope.stop()
    assert scope.read_kept_capture(["CH1"]).capture_id == kept_id
    # the screen's trace of the latest capture is readable (READ_TRACE never waits)
    hdr, _ = scope.read_trace(["CH1"])
    assert hdr.capture_id == kept_id + 3

    # the next host ARM drops it; the new single is kept at its DONE (the simulator completes it at once)
    dev.ui_set_trigger(level=128)
    scope.arm()
    st = scope.wait(2.0)
    assert st.kept_capture_id == st.capture_id == kept_id + 4


def test_latest_kept_capture_is_served_from_the_copy(sim, scope):
    """Fault (b) when the screen's last capture is the host's: RUN armed a UI capture that never finishes, STOP."""
    dev = sim.device
    scope.set_trigger(source=0, edge=0, level=128, mode=1)
    scope.arm()
    st = scope.wait(2.0)
    dev.ui_press_run()
    dev.ui_set_trigger(level=255)
    dev.ui_rearm()
    hdr, _ = scope.read_raw(0x03, count=8)  # RUN: after the hold, the copy (no E_BUSY)
    assert hdr.capture_id == st.capture_id
    dev.ui_press_stop()
    hdr, _ = scope.read_raw(0x03, count=8)  # stopped: the copy (no "ring overwritten")
    assert hdr.capture_id == st.capture_id


def test_wide_kept_capture(sim, scope):
    """The 16-bit layout (decimation filter on) is read from the kept capture like any other."""
    dev = sim.device
    scope.set_timebase(divider=99)
    scope.set_decim("boxcar")
    scope.set_trigger(source=0, edge=0, level=128, mode=0, total_samples=4000, pretrigger_samples=400)
    scope.arm()
    st = scope.wait(2.0)
    want = scope.read_capture(["CH1"])
    dev.ui_press_run()
    assert dev.ui_run_capture(2)
    cap = scope.read_kept_capture(["CH1"])
    assert cap.capture_id == st.capture_id and cap.decim is not None and cap.decim.k
    assert np.array_equal(cap.channels["CH1"].samples16, want.channels["CH1"].samples16)


def test_older_firmware_has_no_kept_capture(sim_factory):
    srv = sim_factory(proto_minor=29)
    s = Scope.connect(srv.url)
    try:
        s.arm()
        st = s.wait(2.0)
        assert st.kept_capture_id == 0 and not s.has_kept and s.kept_capture_info() is None
        with pytest.raises(ScopeError):
            s.read_raw(0x03, count=8, kept=True)
        with pytest.raises(DeviceError) as e:  # the wire refuses the flag as fw 0.3.2 does
            s._req(P.Cmd.READ_RAW, P.ReadRawReq(0x03, 1, 0, P.ReadRawReq.KEPT, 0, 8).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 3)
    finally:
        s.close()


# --- the MCP tools: the bench sequence end to end ----------------------------------------------------------------



@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_bench_case_through_the_tools(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    dev = srv.device
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_trigger", source="CH1", edge="falling", level=0.0, mode="normal",
                     pretrigger_fraction=0.3)
        r = await c.call("scope_acquisition_control", action="arm")
        assert r["acq_state"] in ("ARMED", "DONE")  # the simulator's event comes at once (time_scale 0)
        r = await c.call("scope_acquisition_control", action="wait", timeout_s=2.0)
        assert r["acq_state"] == "DONE"
        st = await c.call("scope_status")
        kept_id = st["capture_id"]
        assert st["host_capture_kept"] == {"capture_id": kept_id, "fetched": False, "still_in_sample_memory": True}

        # the owner presses RUN, the scope captures on, then waits for a trigger that does not come
        dev.ui_press_run()
        assert dev.ui_run_capture(8)
        dev.ui_set_trigger(level=255)
        dev.ui_rearm()
        st = await c.call("scope_status")
        assert st["run_state"] == "RUN" and st["capture_origin"] == "ui_run" and st["capture_id"] == kept_id + 8
        assert st["host_capture_kept"]["still_in_sample_memory"] is False

        # 1. auto: the host's own single, with a caveat naming both captures
        r = await c.call("scope_fetch_waveform", channels=["CH1"])
        assert r["capture_source"] == "host_kept" and r["capture_id"] == kept_id and r["origin"] == "host"
        assert any(f"host's own single capture {kept_id}" in x and f"capture {kept_id + 8}" in x for x in r["caveats"])
        assert r["trigger"] == "fired"
        data = np.load(r["file"])
        assert data["CH1_volts"].size == r["n_samples"]

        # 2. read once: auto now returns the screen's capture; its raw words are gone (E_BUSY 1): the screen's trace
        r = await c.call("scope_fetch_waveform", channels=["CH1"])
        assert r["capture_source"] == "screen_trace" and r["capture_id"] == kept_id + 8
        assert any("READ_TRACE" in x and "RUN loop armed" in x for x in r["caveats"])

        # 3. STOP: 'host' still reads the event, auto the screen's trace ("ring overwritten")
        await c.call("scope_acquisition_control", action="stop")
        r = await c.call("scope_fetch_waveform", channels=["CH1"], capture="host")
        assert r["capture_source"] == "host_kept" and r["capture_id"] == kept_id
        r = await c.call("scope_fetch_waveform", channels=["CH1"])
        assert r["capture_source"] == "screen_trace" and any("STOP at the scope" in x for x in r["caveats"])
        r = await c.call("scope_fetch_waveform", channels=["CH1"], capture="latest", window=[-1e-4, 1e-4])
        assert r["capture_source"] == "screen_trace" and r["capture_id"] == kept_id + 8
        st = await c.call("scope_status")
        assert st["host_capture_kept"]["fetched"] is True

        # 4. the next host capture replaces it; scope_measure without capture analyses the kept event too
        dev.ui_set_trigger(level=128)
        r = await c.call("scope_capture", channels=["CH1"])
        new_id = r["capture_id"]
        st = await c.call("scope_status")
        assert st["host_capture_kept"]["capture_id"] == new_id and st["host_capture_kept"]["fetched"] is True
        assert "need" not in await c.fails("scope_fetch_waveform", capture="bogus")


@pytest.mark.anyio
async def test_tools_on_older_firmware(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25", proto_minor=29)
    dev = srv.device
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_trigger", source="CH1", edge="falling", level=0.0, mode="normal")
        await c.call("scope_acquisition_control", action="arm")
        await c.call("scope_acquisition_control", action="wait", timeout_s=2.0)
        st = await c.call("scope_status")
        assert "host_capture_kept" not in st
        assert "protocol 1.30" in await c.fails("scope_fetch_waveform", capture="host")
        dev.ui_press_run()
        assert dev.ui_run_capture(2)
        dev.ui_set_trigger(level=255)
        dev.ui_rearm()
        # as before 1.30: the latest capture; its words gone, the screen's trace with the caveat
        r = await c.call("scope_fetch_waveform", channels=["CH1"])
        assert r["capture_source"] == "screen_trace" and r["origin"] != "host"
