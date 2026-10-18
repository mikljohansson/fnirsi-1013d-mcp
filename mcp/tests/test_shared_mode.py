"""Collaborative (shared) mode, protocol 1.3: simulator semantics and the Scope client (docs/plans/collaborative-mode.md)."""

from __future__ import annotations

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope, ScopeError
from fnirsi1013d.transport import DeviceError


def test_caps_version_and_status_body(sim, transport):
    info = P.InfoResp.unpack(transport.request(P.Cmd.GET_INFO).payload)
    assert info.caps & P.Caps.SHARED_MODE and info.proto_minor == P.PROTO_MINOR == 12
    assert transport.ascii("#VER").split(",")[3] == "proto=1.12"
    body = transport.request(P.Cmd.GET_STATUS).payload
    assert len(body) == 44
    st = P.StatusResp.unpack(body)
    assert st.control == P.Control.SHARED and st.run_state == P.RunState.RUN and st.watchdog_remaining_s == 0xFFFF


def test_legacy_firmware_keeps_protocol_1_2(legacy_sim, legacy_transport):
    info = P.InfoResp.unpack(legacy_transport.request(P.Cmd.GET_INFO).payload)
    assert not info.caps & P.Caps.SHARED_MODE and info.proto_minor == 2
    assert len(legacy_transport.request(P.Cmd.GET_STATUS).payload) == 32
    with pytest.raises(DeviceError) as e:
        legacy_transport.request(P.Cmd.GET_CAPTURE_INFO)
    assert e.value.status == P.Status.E_UNKNOWN_CMD


def test_everything_accepted_without_remote_and_set_remote_is_a_noop(sim, scope):
    st = scope.legacy_take_remote(watchdog_s=1)  # old hosts still send it: OK, nothing changes
    assert st.control == P.Control.SHARED and not scope.remote
    scope.set_timebase(divider=9)
    scope.arm()
    assert scope.wait(1.0).acq_state == P.AcqState.DONE
    sim.device.user_tap()  # there is no banner to tap
    assert scope.legacy_release().control == P.Control.SHARED
    assert scope.status().exit_reason == 0 and scope.ensure_control() is False


def test_settings_visible_both_ways_and_settings_seq(sim, scope):
    dev = sim.device
    seq0 = scope.status().settings_seq
    dev.ui_set_channel(0, range=3, coupling=1)  # the user at the scope
    st = scope.status()
    assert st.settings_seq == seq0 + 1
    cfg = scope.get_channel(0)  # the cache was dropped when settings_seq moved
    assert (cfg.range, cfg.coupling) == (3, 1)
    scope.set_channel(1, range_index=2)  # the host: same live settings
    assert dev.settings.channels[1].range == 2 and scope.status().settings_seq == seq0 + 2
    scope.set_channel(1, range_index=2)  # no change, no bump
    assert scope.status().settings_seq == seq0 + 2


def test_run_mode_captures_and_capture_id(sim, scope):
    dev = sim.device
    st = scope.status()
    assert st.run_state == P.RunState.RUN and st.capture_id == 1 and st.capture_origin == P.CaptureOrigin.UI_RUN
    dev.ui_run_capture(3)  # the UI loop finished three captures since the last poll
    st = scope.status()
    assert st.capture_id == 4 and st.acq_state == P.AcqState.IDLE  # UI captures do not touch the host state
    hdr, _ = scope.read_raw(0x01, count=16)  # no ARM needed: the latest capture
    assert hdr.capture_id == 4


def test_host_single_capture_leaves_stop_and_run_resumes(sim, scope):
    dev = sim.device
    cap = scope.capture(["CH1"], 1.0)
    assert cap.origin == "host" and cap.settings_seq == scope.status().settings_seq
    st = scope.status()
    assert st.run_state == P.RunState.STOP and st.capture_origin == P.CaptureOrigin.HOST
    st = scope.set_acq_state(True)
    assert st.run_state == P.RunState.RUN
    dev.ui_run_capture()
    assert scope.status().capture_origin == P.CaptureOrigin.UI_RUN
    dev.ui_press_stop()
    assert scope.status().run_state == P.RunState.STOP
    with pytest.raises(DeviceError) as e:
        scope.t.request(P.Cmd.SET_ACQ_STATE, P.SetAcqStateReq(2).pack())
    assert e.value.status == P.Status.E_BAD_ARG


def test_run_pressed_during_a_host_single_aborts_it(sim_factory):
    srv = sim_factory(signal="zero")
    with Scope.connect(srv.url) as s:
        s.set_trigger(mode=1, level=250)  # never triggers
        s.arm()
        assert s.status().run_state == P.RunState.SINGLE
        srv.device.ui_press_run()
        st = s.status()
        assert st.acq_state == P.AcqState.ABORTED and st.run_state == P.RunState.RUN


def test_ring_not_readable_after_an_aborted_arm(sim_factory):
    srv = sim_factory(signal="zero")
    with Scope.connect(srv.url) as s:
        s.read_raw(0x01, count=8)  # the boot RUN capture
        s.set_trigger(mode=1, level=250)
        s.arm()
        s.stop()
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x01, count=8)
        assert e.value.status == P.Status.E_STATE
        hdr, _ = s.read_raw(0x01, count=8, allow_not_done=True)
        assert hdr.acq_state == P.AcqState.ABORTED
        srv.device.ui_press_run()
        srv.device.ui_set_trigger(mode=0)
        srv.device.ui_run_capture()
        s.read_raw(0x01, count=8)  # a finished capture again


def test_capture_info_is_the_capture_time_settings(sim, scope):
    dev = sim.device
    scope.set_trigger(source=1, edge=1, level=150, mode=0, pretrigger_samples=800, total_samples=8000)
    scope.set_channel(0, range_index=4, probe_index=2)
    cap = scope.capture(["CH1"], 1.0)
    dev.ui_set_channel(0, range=1)  # changed after the capture
    dev.ui_set_timebase(21)
    ci = scope.capture_info()
    h = ci.header
    assert h.capture_id == cap.capture_id and h.origin == P.CaptureOrigin.HOST
    assert (h.trig_source, h.trig_edge, h.trig_level, h.pretrigger_samples, h.total_samples) == (1, 1, 150, 800, 8000)
    assert ci.channels[0].range == 4 and ci.channels[0].probe_index == 2 and h.divider == 19
    late = scope.read_capture(["CH1"])  # converted with the capture's settings, not the live ones
    assert late.capture_id == cap.capture_id and late.dt == cap.dt and late.channels["CH1"].config.range == 4
    assert np.allclose(late.channels["CH1"].volts, cap.channels["CH1"].volts)
    assert scope.get_channel(0).range == 1


def test_read_trace_in_shared_mode_uses_capture_settings(sim, scope):
    sim.device.ui_set_channel(0, range=4)
    sim.device.ui_run_capture()
    sim.device.ui_set_channel(0, range=2)
    cap = scope.read_trace_capture(["CH1"])
    assert cap.origin == "ui_run" and cap.channels["CH1"].config.range == 4 and cap.settings_seq == 1


def test_chunked_read_racing_run_mode_pauses_run(sim_factory):
    srv = sim_factory()
    dev = srv.device
    orig = dev._cmd_read_raw

    def new_capture_before_each_chunk(req):
        if dev.run_state == P.RunState.RUN:
            dev.ui_run_capture()
        return orig(req)
    dev._cmd_read_raw = new_capture_before_each_chunk
    with Scope.connect(srv.url, max_response=2048) as s:
        cap = s.read_capture(["CH1", "CH2"])
        assert cap.n == 3000 and any("RUN was paused" in x for x in cap.notes)
        assert s.status().run_state == P.RunState.RUN  # resumed


def test_capture_context_needs_a_stable_settings_seq(sim, scope):
    ctx = scope.capture_context()
    assert ctx.settings_seq == scope.status().settings_seq and set(ctx.channels) == {0, 1}
    orig = sim.device._cmd_get_trigger

    def knob_turning(req):
        sim.device.ui_set_trigger(level=(sim.device.settings.trigger.level + 1) % 256)
        return orig(req)
    sim.device._cmd_get_trigger = knob_turning
    with pytest.raises(ScopeError):
        scope.capture_context()


def test_capture_with_a_matching_context_skips_capture_info(sim, scope):
    ctx = scope.capture_context()
    n0 = sim.device.link["frames"]
    cap = scope.capture(["CH1"], 1.0, ctx=ctx)
    assert cap.settings_seq == ctx.settings_seq and cap.capture_info is None
    assert sim.device.link["frames"] - n0 == 4  # ARM, WAIT, READ_RAW, GET_CAL (first use); no GET_CAPTURE_INFO
    sim.device.ui_set_channel(0, range=3)
    cap = scope.capture(["CH1"], 1.0, ctx=ctx)
    assert cap.settings_seq != ctx.settings_seq and cap.capture_info is not None
    assert cap.channels["CH1"].config.range == 3  # converted with the settings it was taken under


def test_screen_shows_mcp_chip_not_banner(sim, scope):
    sim.device.dtr = True
    fb = scope.read_screen()
    assert (fb[4:24, 700:756] == 0x07E0).all() and not (fb[0:24, 0:600] == 0x001F).all()


def test_settings_changed_during_a_capture_mark_it_invalid(sim_factory):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25", time_scale=1.0)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=199)  # 1 MS/s: a 16000-sample capture takes ~16 ms
        s.set_trigger(mode=0, pretrigger_samples=1000, total_samples=16000)
        ctx = s.capture_context()
        s.arm()
        srv.device.ui_set_channel(0, coupling=1)  # while it runs
        st = s.wait(2.0)
        assert st.acq_state == P.AcqState.DONE and st.capture_settings_seq == P.SEQ_INVALID
        ci = s.capture_info()
        assert ci.header.flags & P.CaptureInfoHeader.F_SETTINGS_CHANGED
        assert ci.header.settings_seq == P.SEQ_INVALID != ctx.settings_seq


def test_status_before_the_first_capture(sim_factory):
    srv = sim_factory()
    srv.device.run_state = P.RunState.STOP  # booted in STOP, nothing captured yet
    with Scope.connect(srv.url) as s:
        st = s.status()
        assert st.capture_id == 0 and st.capture_settings_seq == P.SEQ_INVALID
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x01, count=4)
        assert e.value.status == P.Status.E_STATE and e.value.detail == 0
        with pytest.raises(DeviceError):
            s.capture_info()


def test_transport_uses_the_shared_bulk_timeout(sim, scope):
    scope.status()
    assert scope.t.shared and not scope.t.remote
    assert scope.t.timeouts.for_cmd(P.Cmd.READ_RAW, scope.t.remote, scope.t.shared) == 2.5


def test_set_acq_state_during_a_pending_wait_aborts_it(sim_factory):
    """fw 0.2.0: SET_ACQ_STATE is a control request (F_CONTROL); RUN while a WAIT is pending aborts the WAIT."""
    srv = sim_factory(signal="zero")
    with Scope.connect(srv.url) as s:
        s.set_trigger(mode=1, level=250)
        s.arm()
        sent = []

        def press_run():
            if not sent:
                sent.append(s.t.control(P.Cmd.SET_ACQ_STATE, P.SetAcqStateReq(1).pack()))
                return True
            return False
        resp = s.t.deferred(P.Cmd.WAIT, P.U32Req(2000).pack(), 3.0, cancel=press_run, on_cancel=lambda: None,
                            check=False)
        assert resp.status == P.Status.E_ABORTED
        assert P.StatusResp.unpack(sent[0].payload).run_state == P.RunState.RUN


def test_link_stats_has_the_0_2_0_counters(scope):
    ls = scope.link_stats()
    assert "first_poll_ms" in ls and "first_done_ms" in ls and len(ls) == 31


def test_read_trace_count_is_clipped_on_1_3(sim, scope):
    """fw 0.2.1 clips a count past the end (0.1.7 / 0.2.0 answered E_BAD_ARG detail 8, see test_simulator_scope)."""
    hdr = P.ReadTraceHeader.unpack(scope.t.request(P.Cmd.READ_TRACE, P.ReadTraceReq(1, 0, 2990, 50).pack()).payload)
    assert hdr.sample_count == 3000 and hdr.count == 10
    with pytest.raises(DeviceError) as e:
        scope.t.request(P.Cmd.READ_TRACE, P.ReadTraceReq(1, 0, 3001, 0).pack())
    assert e.value.status == P.Status.E_BAD_ARG and e.value.detail == 4
