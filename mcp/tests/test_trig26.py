"""Protocol 1.26 (FPGA 0x153C, docs/plans/fpga-0x153c.md, docs/protocol.md row 66): SET_TRIGGER2 and the trigger types
(pulse width, Nth edge, timeout), the hold-off, GET_TRIGGER 52 / GET_STATUS 52 / GET_CAPTURE_INFO 112 bytes against the
firmware's vectors; peak detect (SET_DECIM 4, READ_RAW layout 3) and the hardware counter (GET_COUNTER) from the contract
(the firmware's C6 / C7 must match these bytes); the simulator's qualifier, hold-off, peak and counter models; the tools
scope_set_trigger (types), scope_counter, scope_capture(peak=true), scope_report on the new kinds."""

from __future__ import annotations

import json
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import SimConfig, TrigQual, TriggerState, gate_count, parse_signal, signal_edges
from fnirsi1013d.transport import DeviceError
from test_server import client_for

VECTORS = {v["name"]: v for v in json.loads(
    (Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())["vectors"]}


def payload(name: str) -> bytes:
    return bytes.fromhex(VECTORS[name]["frames"][0]["payload"])


# --- protocol -----------------------------------------------------------------------------------------------


def test_constants_and_caps():
    assert P.PROTO_MINOR >= 26 and P.FW_FPGA_VERSION >= 0x153C  # 1.27 (0x153D) builds on 1.26
    assert P.Cmd.SET_TRIGGER2 == 0x0219 and P.Cmd.GET_COUNTER == 0x0213
    assert (P.Caps.HOLDOFF, P.Caps.TRIG_TYPES, P.Caps.PEAK_DETECT, P.Caps.COUNTER) == (4, 1 << 20, 1 << 21, 1 << 22)
    assert (P.Features.TRIG_TYPES, P.Features.PEAK, P.Features.COUNTER) == (0x100, 0x200, 0x400)
    assert P.CMD_CAP[P.Cmd.SET_TRIGGER2] == P.Caps.TRIG_TYPES and P.CMD_CAP[P.Cmd.GET_COUNTER] == P.Caps.COUNTER
    assert P.REQUEST_SIZES[P.Cmd.SET_TRIGGER2] == 24 and P.REQUEST_SIZES[P.Cmd.GET_COUNTER] == 8
    assert P.Trigger2.size() == 24 and P.StatusResp.size() == 52 and P.CounterResp.size() == 24
    assert P.FPGA_MISMATCH_TEXT.endswith(f"0x{P.FW_FPGA_VERSION:04X}")


def test_set_trigger2_matches_the_firmware_vectors():
    """The host struct packs the bytes gen_vectors.py (fw-c5) produced for every type."""
    assert P.Trigger2(P.TRIG_PULSE_WIDTH, 1, 0, 1, 1500, 0, 140, 0, 200000, 0).pack() == payload("req_set_trigger2")
    assert P.Trigger2(P.TRIG_NTH_EDGE, 0, 0, 2, 3, 100000, 128, 0, 0, 0).pack() == payload("req_set_trigger2_nth_edge_ext")
    assert P.Trigger2(P.TRIG_PULSE_WIDTH, 0, P.TRIG2_F_LONGER, 0, 0xFFFFFF, 0, 100, 0, 0, 0).pack() == \
        payload("req_set_trigger2_pulse_longer_max")
    assert P.Trigger2(P.TRIG_TIMEOUT, 1, 0, 0, 20000, 0, 128, 0, 0, 0).pack() == payload("req_set_trigger2_timeout")
    assert P.Trigger2(P.TRIG_EDGE, 0, 0, 0, 0, 0, 128, 0, 0, 0).pack() == payload("req_set_trigger2_edge")
    t2 = P.Trigger2.unpack(payload("req_set_trigger2"))
    assert (t2.type_name, t2.polarity_name, t2.longer, t2.holdoff_s) == ("pulse_width", "negative", False, 1e-3)
    assert t2.describe()["condition"] == "shorter" and t2.describe()["width_samples"] == 1500
    e = P.parse_error_payload(payload("resp_set_trigger2_e_bad_arg_runt"))
    assert e[0] == P.TRIG2_D_TYPE and "0x153E" in e[1]
    assert P.TRIG2_DETAIL_NAMES[8] == "H" and P.TRIG2_D_LEVEL2 == 14
    assert P.holdoff_units(2.5e-3) == 500000 and P.holdoff_units(1e-9) == 0 and P.holdoff_units(1e9) == 0xFFFFFFFF


def test_trigger_52_status_52_capture_info_112_vectors():
    t = P.Trigger.unpack(payload("resp_get_trigger_1_26"))
    assert len(payload("resp_get_trigger_1_26")) == 52 and t.pack() == payload("resp_get_trigger_1_26")
    assert t.holdoff == 200000 and t.trigger2 is not None and t.trigger2.holdoff == 200000 and t.type == P.TRIG_PULSE_WIDTH
    assert (t.trigger2.source, t.trigger2.polarity, t.trigger2.level) == (t.source, t.edge, t.level)
    old = P.Trigger.unpack(payload("resp_get_trigger_1_26")[:28])
    assert old.trigger2 is None and old.type == P.TRIG_EDGE and len(old.pack()) == 28
    st = P.StatusResp.unpack(payload("resp_get_status_1_26"))
    assert st.pack() == payload("resp_get_status_1_26") and (st.fpga_37, st.fpga_34, st.edges_left) == (0x10, 0x22, 2)
    ts = st.trigger_status()
    assert ts["type"] == "nth_edge" and ts["holdoff_running"] and ts["counting"] and ts["edges_left"] == 2
    assert P.feature_names(st.features)[-1] == "trig_types"
    ci = P.CaptureInfo.unpack(payload("resp_get_capture_info_1_26"))
    assert ci.pack() == payload("resp_get_capture_info_1_26") and ci.trig2 == P.CaptureTrig2(2, 0, 3, 100000, 0)
    tr = ci.trigger()
    assert tr.trigger2 is not None and tr.trigger2.type == P.TRIG_NTH_EDGE and tr.trigger2.h == 100000
    assert "trig2" in ci.settings_key()
    assert P.CaptureInfo.unpack(payload("resp_get_capture_info_1_26")[:96]).trig2 is None


def test_counter_and_peak_structs_from_the_contract():
    r = P.CounterResp.unpack(payload("host_resp_get_counter"))
    assert (r.e, r.t, r.w, r.min, r.max, r.level, r.hysteresis) == (1000, 20_000_000, 4_000_000, 112, 147, 128, 6)
    assert r.frequency_hz == 10000.0 and r.period_s == pytest.approx(100e-6) and r.width_s == pytest.approx(20e-6)
    assert r.duty == 0.2 and r.new and r.valid and r.gate_s == 0.1 and r.effective_source == "CH1"
    assert r.freq_mhz == 10_000_000 == P.counter_freq_mhz(1000, 20_000_000) and r.resolution == 5e-8
    assert P.counter_freq_mhz(0, 5) == 0 and P.counter_freq_mhz(10**9, 1) == 0xFFFFFFFF
    assert not P.CounterResp(flags=P.COUNTER_F_NEW, e=0, t=0).valid
    req = P.CounterReq.unpack(payload("req_get_counter_config" if "req_get_counter_config" in VECTORS else
                                      "host_req_get_counter_config"))
    assert (req.what, req.gate, req.source, req.level, req.hysteresis) == (1, 3, 2, 140, 6)
    # peak detect: filter 4, the shift byte's bits, READ_RAW layout 3, GET_CAPTURE_INFO 79 bit 1
    assert P.parse_decim_filter("peak") == (4, False) and P.DECIM_PEAK == 4
    c = P.decim_config(P.DECIM_PEAK, 0, 199, 4000, P.DECIM_HAS_FIR)
    assert (c.k, c.n, c.gain, c.shift, c.peak, c.fir, c.filter_name, c.code_per_count) == (1, 200, 1, 0, True, False,
                                                                                            "peak", 1.0)
    assert c.status == 0x81 and c.shift_byte == 0xC0
    back = P.DecimConfig.from_status(c.status, c.shift_byte, c.n)
    assert back.peak and back.gain == 1 and back.filter_name == "peak" and back.scale_q24 == 0
    assert P.decim_config(P.DECIM_PEAK, 0, 0, 4000, P.DECIM_HAS_FIR).why == 2  # 200 MSa/s stays legacy
    assert P.decim_config(P.DECIM_PEAK, 0, 199, 9000, P.DECIM_HAS_FIR).why == 3
    fir = P.decim_config(2, P.DECIM_SET_FIR, 199, 4000, P.DECIM_HAS_FIR)
    assert fir.shift_byte == fir.shift == 7 and not fir.peak  # no peak bits otherwise
    ds = P.DecimState.unpack(payload("host_resp_get_decim_peak"))
    assert ds.filter_setting == "peak" and ds.next.peak and ds.next_shift == 0xC0
    ci = P.CaptureInfo.unpack(payload("host_resp_get_capture_info_1_26_peak"))
    assert len(payload("host_resp_get_capture_info_1_26_peak")) == 112
    assert ci.peak and ci.run is not None and ci.run.wide == 3 and ci.decim is not None and ci.decim.peak
    assert ci.trig2 == P.CaptureTrig2(1, 1, 1500, 0, 500000) and ci.trigger().trigger2.longer
    hdr = P.ReadRawHeader.unpack(payload("host_resp_read_raw_peak"))
    assert hdr.layout == P.ReadRawReq.LAYOUT_PEAK == 3 and payload("host_resp_read_raw_peak")[16:24] == \
        bytes((100, 101, 90, 102, 140, 141, 170, 139))


# --- the qualifier model --------------------------------------------------------------------------------------


def _stream() -> np.ndarray:
    """20 high samples every 100, plus a 60-sample pulse at 500 (merging with the one at 550: high 500..569)."""
    a = np.zeros(2000, np.uint8)
    for s in range(50, 2000, 100):
        a[s:s + 20] = 200
    a[500:560] = 200
    return a


def _fire(tr: TriggerState, chunks: int = 1) -> int:
    a = _stream()
    q = TrigQual(tr, 128)
    pos, prev = 0, None
    for ch in np.array_split(a, chunks):
        i = q.feed(ch, prev, pos)
        if i >= 0:
            return pos + i
        prev, pos = int(ch[-1]), pos + len(ch)
    return -1


@pytest.mark.parametrize("chunks", [1, 7])
def test_qualifier_reference_model(chunks):
    assert _fire(TriggerState(ttype=1, edge=0, n=30), chunks) == 70  # shorter than 30: the first 20-wide pulse's end
    assert _fire(TriggerState(ttype=1, edge=0, n=30, longer=1), chunks) == 570  # longer: the 70-wide pulse
    assert _fire(TriggerState(ttype=1, edge=0, n=20), chunks) == -1  # shorter than 20: 20 is not < 20, 70 neither
    assert _fire(TriggerState(ttype=1, edge=0, n=21), chunks) == 70  # shorter than 21: the 20-wide pulse
    assert _fire(TriggerState(ttype=1, edge=1, n=100), chunks) == 150  # negative pulse 70..149 (80 low) shorter than 100
    assert _fire(TriggerState(ttype=2, edge=0, n=3), chunks) == 250  # third rising edge
    assert _fire(TriggerState(ttype=2, edge=1, n=2), chunks) == 170  # second falling edge
    assert _fire(TriggerState(ttype=2, edge=0, n=2, h=90), chunks) == 150  # the 80-sample gaps are not idle (< 90)
    assert _fire(TriggerState(ttype=2, edge=0, n=2, h=70), chunks) == 500  # the 80 gaps restart the count; 450 + 500 don't
    assert _fire(TriggerState(ttype=3, edge=0, n=40), chunks) == 540  # held high 41 samples: only the long pulse
    assert _fire(TriggerState(ttype=3, edge=1, n=70), chunks) == 140  # held low: the gap 70..149 (the first is 50 long)
    assert _fire(TriggerState(ttype=3, edge=1, n=40), chunks) == 40  # from the arm: samples 0..40 low


def test_qualifier_pulse_must_begin_after_the_arm():
    a = np.concatenate((np.full(30, 200, np.uint8), np.zeros(100, np.uint8), np.full(10, 200, np.uint8),
                        np.zeros(50, np.uint8)))
    q = TrigQual(TriggerState(ttype=1, edge=0, n=50), 128)
    assert q.feed(a, 200, 0) == 140  # the pulse that began before the arm (0..29) does not count; 130..139 does


def test_counter_helpers():
    sq = parse_signal("square:freq=10kHz,amp=1,duty=0.2")
    period, r, f = signal_edges(sq, None, 0.5, 0.4)
    assert (period, r, f) == (1e-4, [0.0], [2e-5])
    assert gate_count(period, r, f, 0.0123, 0.1) == (1000, 20_000_000, 4_000_000)
    sine = parse_signal("sine:freq=1kHz,amp=0.5")
    period, r, f = signal_edges(sine, None, 0.0, -0.05)
    assert period == 1e-3 and r[0] == pytest.approx(0.0) and 0.5e-3 < f[0] < 0.52e-3
    assert signal_edges(parse_signal("dc:v=1"), None, 0.5, 0.4) is None
    assert signal_edges(sine, None, 0.9, 0.85) is None  # the level is above the amplitude
    gate = parse_signal("gate:period=1ms,width=200us,high=3.3,low=0")
    period, r, f = signal_edges(gate, None, 1.65, 1.6)
    assert (period, r) == (1e-3, [0.0]) and f == [pytest.approx(2e-4)]
    assert gate_count(1e-3, [0.0], [2e-4], 0.0, 1.0)[0] == 1000


# --- the simulator through the client -----------------------------------------------------------------------


def _scope(sim_factory, **kw) -> tuple[Scope, object]:
    srv = sim_factory(**kw)
    s = Scope.connect(srv.url)
    return s, srv


def test_set_trigger2_round_trip_and_refusals(sim_factory):
    s, _ = _scope(sim_factory)
    try:
        assert s.info.caps & P.Caps.TRIG_TYPES and s.info.caps & P.Caps.HOLDOFF
        t = s.get_trigger(cached=False)
        assert t.trigger2 == P.Trigger2(0, 0, 0, 0, 0, 0, 128, 0, 0, 0) and len(t.pack()) == 52
        t = s.set_trigger2(type="pulse_width", polarity="negative", n=1500, source=1, level=140, holdoff_s=1e-3)
        assert t.trigger2.pack() == payload("req_set_trigger2") and t.holdoff == 200000 and t.edge == 1
        assert t.source == 1 and t.level == 140
        t = s.set_trigger2(type="nth_edge", polarity="rising", n=3, h=100000, source=2, level=128, holdoff=0)
        assert t.trigger2.pack() == payload("req_set_trigger2_nth_edge_ext")
        t = s.set_trigger2(type="edge", source=0)  # a type change zeroes N and H
        assert t.trigger2.pack() == payload("req_set_trigger2_edge")
        st = s.status()
        assert len(st.pack()) == 52 and st.trigger_status()["type"] == "edge"
        for req, detail in ((P.Trigger2(4, 0, 0, 0, 0, 0, 128), 0), (P.Trigger2(6, 0, 0, 0, 0, 0, 128), 0),
                            (P.Trigger2(0, 2, 0, 0, 0, 0, 128), 1), (P.Trigger2(1, 0, 2, 0, 5, 0, 128), 2),
                            (P.Trigger2(0, 0, 1, 0, 0, 0, 128), 2), (P.Trigger2(0, 0, 0, 4, 0, 0, 128), 3),
                            (P.Trigger2(1, 0, 0, 0, 0, 0, 128), 4), (P.Trigger2(2, 0, 0, 0, 70000, 0, 128), 4),
                            (P.Trigger2(0, 0, 0, 0, 1, 0, 128), 4), (P.Trigger2(1, 0, 0, 0, 5, 7, 128), 8),
                            (P.Trigger2(2, 0, 0, 0, 1, 1 << 24, 128), 8), (P.Trigger2(0, 0, 0, 0, 0, 0, 256), 12),
                            (P.Trigger2(0, 0, 0, 0, 0, 0, 128, 5), 14)):
            with pytest.raises(DeviceError) as e:
                s.t.request(P.Cmd.SET_TRIGGER2, req.pack())
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, detail), req
        assert s.get_trigger(cached=False).trigger2.pack() == payload("req_set_trigger2_edge")  # nothing written
        with pytest.raises(ValueError):
            s.set_trigger2(type="pulse_width", polarity="rising")
    finally:
        s.close()


def test_older_firmware_model_has_no_extension(sim_factory):
    s, _ = _scope(sim_factory, proto_minor=25)
    try:
        assert not s.info.caps & P.Caps.TRIG_TYPES and s.info.proto_minor == 25
        assert s.get_trigger(cached=False).trigger2 is None and len(s.status().pack()) == 52  # the host struct
        assert len(s.t.request(P.Cmd.GET_STATUS).payload) == 48 and len(s.t.request(P.Cmd.GET_TRIGGER).payload) == 28
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_TRIGGER2, P.Trigger2().pack())
        assert e.value.status == P.Status.E_UNSUPPORTED
        with pytest.raises(DeviceError):
            s.read_counter()
    finally:
        s.close()


def _square_scope(sim_factory, **kw):
    """10 kHz 20 % square on CH1 at 10 MSa/s legacy: a pulse is 20 us = 200 samples = 100 trigger words."""
    s, srv = _scope(sim_factory, signal="square:freq=10kHz,amp=1,duty=0.2", **kw)
    s.set_channel(0, range_index=1)
    s.set_timebase(divider=19)
    s.set_trigger(source=0, edge=0, level=128, mode=1, pretrigger_samples=1000, total_samples=4000)
    return s, srv


def _at_trigger(cap) -> tuple[float, float]:
    """The sample before the trigger word's core-A sample and that sample (legacy: index 2 Pw + 1)."""
    v = cap.channels["CH1"].volts
    return float(v[cap.pretrigger_samples - 1]), float(v[cap.pretrigger_samples + 1])


def test_pulse_width_nth_edge_timeout_captures(sim_factory):
    s, srv = _square_scope(sim_factory)
    try:
        s.set_trigger2(type="pulse_width", polarity="positive", n=150)  # shorter than 150 words: every 100-word pulse
        cap = s.capture(["CH1"], timeout_s=2)
        assert cap is not None and cap.triggered
        before, at = _at_trigger(cap)
        assert before > 0.5 and at < -0.5  # the trailing edge: high before, low at the trigger sample
        ci = s.capture_info()
        assert ci.trig2 == P.CaptureTrig2(1, 0, 150, 0, 0) and ci.trigger().trigger2.n == 150
        s.set_trigger2(longer=True, n=50)  # longer than 50: fires too
        assert s.capture(["CH1"], timeout_s=2).triggered
        s.set_trigger2(n=150)  # longer than 150: never
        assert s.capture(["CH1"], timeout_s=0.5) is None
        s.set_trigger2(type="nth_edge", polarity="rising", n=3)
        cap = s.capture(["CH1"], timeout_s=2)
        before, at = _at_trigger(cap)
        assert cap.triggered and before < -0.5 and at > 0.5  # a rising edge
        s.set_trigger2(type="timeout", polarity="high", n=50)  # held high 51 words: the 100-word pulse
        cap = s.capture(["CH1"], timeout_s=2)
        assert cap.triggered and all(x > 0.5 for x in cap.channels["CH1"].volts[cap.pretrigger_samples - 90:
                                                                                  cap.pretrigger_samples + 2])
        s.set_trigger2(type="timeout", polarity="high", n=150)  # never held that long
        assert s.capture(["CH1"], timeout_s=0.5) is None
        s.set_trigger2(type="timeout", polarity="low", n=300)  # the 400-word low gap
        assert s.capture(["CH1"], timeout_s=2).triggered
        s.set_trigger2(type="timeout", polarity="low", n=500)
        assert s.capture(["CH1"], timeout_s=0.5) is None
    finally:
        s.close()


def test_nth_edge_idle_restarts_the_count(sim_factory):
    """A 1 kHz gate 200 us wide: 1 rising edge per ms. Nth edge 2 with an idle time shorter than the gap never fires;
    with a longer idle time it fires on the second edge."""
    s, _ = _scope(sim_factory, signal="gate:period=1ms,width=200us,high=3,low=0")
    try:
        s.set_channel(0, range_index=1)
        s.set_timebase(divider=199)  # 1 MSa/s: a word is 2 us legacy
        s.set_trigger(source=0, edge=0, level=140, mode=1, pretrigger_samples=200, total_samples=4000)
        s.set_trigger2(type="nth_edge", polarity="rising", n=2, h=300)  # idle 300 words = 600 us < the 800 us gap
        assert s.capture(["CH1"], timeout_s=0.5) is None
        s.set_trigger2(h=450)  # 900 us > the gap: the count runs through
        cap = s.capture(["CH1"], timeout_s=2)
        assert cap is not None and cap.triggered
    finally:
        s.close()


def test_holdoff_skips_pulses_in_segments_and_across_arms(sim_factory):
    """1 kHz square, hold-off 2.5 ms: every third edge triggers (bench check 5), segment timestamps 3 ms apart; a
    re-arm right after a capture still waits out the hold-off; SET_TRIGGER2 clears it (0x66) for the next arm."""
    s, srv = _scope(sim_factory, signal="square:freq=1kHz,amp=1")
    try:
        s.set_channel(0, range_index=1)
        s.set_timebase(divider=1999)  # 100 kSa/s legacy: a word is 20 us, the 40-sample record 0.4 ms
        s.set_trigger(source=0, edge=0, level=128, mode=1, pretrigger_samples=4, total_samples=80)
        s.set_trigger2(type="edge", holdoff_s=2.5e-3)
        assert s.get_trigger(cached=False).holdoff == 500000
        run = s.capture_segments(["CH1"], count=4, timeout_s=5)
        ts = [seg.t_trigger for seg in run.segments]
        gaps = np.diff(ts)
        assert len(ts) == 4 and np.allclose(gaps, 3e-3, atol=1e-4), gaps
        s.set_trigger2(holdoff=0)
        run = s.capture_segments(["CH1"], count=4, timeout_s=5)
        assert np.allclose(np.diff([seg.t_trigger for seg in run.segments]), 1e-3, atol=1e-4)
        # across host re-arms: the model counts the remaining hold-off from the next arm
        s.set_trigger2(holdoff_s=2.5e-3)
        cap = s.capture(["CH1"], timeout_s=2)
        d = srv.device
        assert cap.triggered and d.holdoff_left_s > 0 and s.status().holdoff_running
        pc_t = d._pending_capture
        assert pc_t is None
        cap2 = s.capture(["CH1"], timeout_s=2)
        assert cap2.triggered and cap2.trigger.holdoff == 500000
        s.set_trigger2(holdoff_s=2.5e-3)  # a new definition: 0x66, the next arm starts clean
        assert d.holdoff_clear_pending
        s.capture(["CH1"], timeout_s=2)
        assert not d.holdoff_clear_pending
    finally:
        s.close()


def test_counter_on_the_simulator(sim_factory):
    s, srv = _scope(sim_factory, signal="square:freq=10kHz,amp=1,duty=0.2")
    try:
        s.set_channel(0, range_index=1)
        r = s.read_counter()
        assert r.valid and r.new and r.frequency_hz == pytest.approx(10e3, rel=1e-6) and r.duty == pytest.approx(0.2)
        assert r.gate == 2 and r.effective_source == "CH1" and r.e == 1000 and 100 < r.min < 128 < r.max < 160
        assert r.freq_mhz == 10_000_000
        r2 = s.read_counter()
        assert r2.seq == r.seq and not r2.new  # no new gate yet
        r = s.read_counter(gate=3, source="CH2", level=205)  # the 1 kHz 0..3 V sync square on CH2, level ~1.5 V
        assert r.gate == 3 and r.source == 2 and r.effective_source == "CH2" and r.frequency_hz == pytest.approx(10e3)
        assert r.e == 10000 and r.t == 200_000_000 and r.new and r.resolution == 5e-9  # the 10 kHz sync square
        r = s.read_counter(source="EXT")  # the pin: CH2's signal through the 1.65 V threshold
        assert r.effective_source == "EXT" and (r.min, r.max) == (0, 255) and r.frequency_hz == pytest.approx(10e3)
        r = s.read_counter(source="trigger", level=250, hysteresis=6)  # the level above the signal: no edges
        assert not r.valid and r.e == 0 and r.frequency_hz is None
        for req, detail in ((P.CounterReq(2), 0), (P.CounterReq(1, 4, 0, 128, 6), 1), (P.CounterReq(1, 2, 4, 128, 6), 2),
                            (P.CounterReq(1, 2, 0, 10, 20), 4)):
            with pytest.raises(DeviceError) as e:
                s.t.request(P.Cmd.GET_COUNTER, req.pack())
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, detail)
        r = s.read_counter(gate=0, level=128)
        assert r.gate == 0 and not r.valid
    finally:
        s.close()


def test_peak_detect_on_the_simulator(sim_factory):
    """10 ns spikes every 1 us at 1 MSa/s: legacy sampling misses most, peak detect keeps every one; the layouts."""
    s, srv = _scope(sim_factory, signal="gate:period=1us,width=10ns,high=1,low=0", noise_lsb=0.5)
    try:
        s.set_channel(0, range_index=1)
        s.set_timebase(divider=199)  # 1 MSa/s: N = 200 raw samples per word
        s.set_trigger(source=0, edge=0, level=128, mode=0, pretrigger_samples=100, total_samples=2000)
        plain = s.capture(["CH1"], timeout_s=2)
        assert plain is not None and not plain.peak
        hit_plain = int(np.sum(plain.channels["CH1"].volts > 0.5))
        st = s.set_decim("peak")
        assert st.filter_setting == "peak" and st.next.peak and st.next_shift & P.DECIM_PEAK_ACT and st.next_n == 200
        assert s.get_trigger(cached=False).total_words == 2000  # the 16-bit layout: words = samples
        cap = s.capture(["CH1", "CH2"], timeout_s=2)
        ch = cap.channels["CH1"]
        assert cap.peak and cap.decim is not None and cap.decim.peak and ch.peak and ch.samples16 is None
        assert ch.min_volts.shape == ch.max_volts.shape == (2000,) and ch.min_codes.dtype == np.uint8
        hit_peak = int(np.sum(ch.max_volts > 0.5))
        assert hit_peak >= 1990 and hit_plain < 400, (hit_plain, hit_peak)  # every window holds one spike
        assert np.all(ch.min_volts <= ch.volts) and np.all(ch.volts <= ch.max_volts)
        assert float(np.median(ch.min_volts)) < 0.1 and np.all(ch.max_volts >= ch.min_volts)
        ci = s.capture_info()
        assert ci.peak and ci.run.wide == P.CAPINFO_WIDE | P.CAPINFO_PEAK and ci.decim_shift & P.DECIM_PEAK_ACT
        assert ci.trigger().total_words == 2000 and ci.decim.filter_name == "peak"
        # the record is read with layout 3 only
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x03, layout=P.ReadRawReq.LAYOUT_WIDE, count=4)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 2)
        hdr, data = s.read_raw(0x03, layout=P.ReadRawReq.LAYOUT_PEAK, count=5)
        assert hdr.layout == 3 and data[0].shape == (2, 5) and np.all(data[0][1] >= data[0][0])
        # averaging is refused, the UI Average is not armed
        s.set_trigger(total_samples=1000, pretrigger_samples=100)
        with pytest.raises(DeviceError) as e:
            s.set_seq(P.SEQ_AVERAGE, count=1, n=4)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.SEQ_FIT_PEAK)
        srv.device.ui_average = True
        assert s.status().ui_average == 0
        # the processed trace of a peak record is the midpoint; READ_TRACE WIDE gives { max, min } words
        tr = s.read_trace_capture(["CH1"])
        assert tr.peak and tr.channels["CH1"].min_volts is not None
        # back to a filter: the Filter word was kept, layout 3 refused on a normal record
        st = s.set_decim("cic2")
        assert st.filter_setting == "cic2" and not st.next.peak
        s.capture(["CH1"], timeout_s=2)
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x03, layout=P.ReadRawReq.LAYOUT_PEAK, count=4)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 2)
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_DECIM, P.SetDecimReq(5, 0).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
        s.set_timebase(divider=0)  # 200 MSa/s: peak requested stays legacy (why 2)
        st = s.set_decim("peak")
        assert st.filter_setting == "peak" and not st.next.peak and st.why == 2 and st.next_shift & P.DECIM_PEAK_EN
    finally:
        s.close()


def test_peak_refused_without_the_cap(sim_factory):
    s, _ = _scope(sim_factory, caps_clear=int(P.Caps.PEAK_DETECT))
    try:
        with pytest.raises(ValueError):
            s.set_decim("peak")
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_DECIM, P.SetDecimReq(4, 0).pack())
        assert e.value.status == P.Status.E_UNSUPPORTED
    finally:
        s.close()


# --- the tools ------------------------------------------------------------------------------------------------


def test_tools_trigger_types_counter_peak(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=10kHz,amp=1,duty=0.2")

    async def main():
        async with client_for(srv, tmp_path) as c:
            info = await c.call("scope_info")
            assert info["fw_fpga_version"] == f"0x{P.FW_FPGA_VERSION:04X}" and info["fpga_mismatch"] is False and info["caveats"] == []
            prof = await c.call("scope_profile")
            assert prof["live"]["trigger_types"] and prof["live"]["counter"] and prof["live"]["peak_detect"]
            assert "counter" in prof["static"] and "peak_detect" in prof["static"]
            await c.call("scope_set_channel", channel="CH1", volts_per_div=2.5)
            await c.call("scope_set_timebase", sample_rate="10MS/s")
            r = await c.call("scope_set_trigger", source="CH1", level=0.0, mode="normal", pretrigger_fraction=0.25,
                             total_samples=4000)
            assert r["applied"]["type"]["type"] == "edge" and r["applied"]["holdoff_s"] == 0
            assert "hold-off is not implemented" not in " ".join(r["caveats"])
            # pulse width shorter than 25 us: at 10 MSa/s legacy a trigger sample is 200 ns -> 125 samples (the
            # 20 us pulses are 100)
            r = await c.call("scope_set_trigger", type="pulse_width", polarity="positive", width="25us")
            t = r["applied"]["type"]
            assert (t["type"], t["polarity"], t["condition"], t["width_samples"]) == ("pulse_width", "positive",
                                                                                     "shorter", 125)
            assert t["width_s"] == pytest.approx(25e-6) and t["stream_sample_s"] == pytest.approx(200e-9)
            assert any("trailing edge" in x for x in r["caveats"])
            cap = await c.call("scope_capture", channels=["CH1"])
            assert cap["trigger"] == "fired" and cap["capture_settings"]["trigger"]["source"] == "CH1"
            r = await c.call("scope_set_trigger", type="pulse_width", longer=True, width="25us")
            assert r["applied"]["type"]["condition"] == "longer" and r["applied"]["type"]["width_samples"] == 125
            assert (await c.call("scope_capture", channels=["CH1"], timeout_s=0.3))["trigger"] == "none"
            r = await c.call("scope_set_trigger", type="nth_edge", polarity="rising", n=3, idle="40us")
            assert r["applied"]["type"]["n"] == 3 and r["applied"]["type"]["idle_samples"] == 200
            r = await c.call("scope_set_trigger", type="timeout", polarity="high", width="1.23us")
            assert r["applied"]["type"]["held_samples"] == 6 and any("rounded" in x for x in r["caveats"])
            r = await c.call("scope_set_trigger", type="edge", edge="rising", hold_off="2.5ms")
            assert r["applied"]["holdoff"] == 500000 and r["applied"]["holdoff_s"] == pytest.approx(2.5e-3)
            assert r["applied"]["type"]["type"] == "edge"
            st = await c.call("scope_status")
            assert "trig_types" in st["features"] and st["trigger_status"]["type"] == "pulse_width"  # the last arm's
            assert "0x153E" in await c.fails("scope_set_trigger", type="runt")
            assert "pulse_width" in await c.fails("scope_set_trigger", type="nth_edge", width="1us")
            r = await c.call("scope_set_trigger", type="pulse_width", width="1ns", hold_off="0s")
            assert r["applied"]["type"]["width_samples"] == 1 and any("clamped" in x for x in r["caveats"])
            await c.call("scope_set_trigger", type="edge")
            # the counter: tier 0 read, tier 1 configure
            r = await c.call("scope_counter")
            assert r["source"] == "CH1" and r["frequency_hz"] == pytest.approx(10e3, rel=1e-6)
            assert r["duty"] == pytest.approx(0.2) and r["period_s"] == pytest.approx(100e-6) and r["valid"]
            assert r["vmax"] > r["vmin"] and r["vpp"] > 1.5 and "5 ns clock" in r["resolution"]
            assert r["gate_s"] == 0.1 and r["readings"][0]["edges"] == 1000 and Path(r["file"]).is_file()
            r = await c.call("scope_counter", gate="1s", source="CH2", level=1.5, readings=2)
            assert r["source"] == "CH2" and len(r["readings"]) == 2 and r["frequency_hz"] == pytest.approx(10e3)
            assert r["frequency_spread_hz"] == 0
            rep = await c.call("scope_report", source=r["file"])
            assert rep["kind"] == "counter" and rep["table"][0]["frequency"].startswith("10 kHz")
            # peak detect through scope_capture: one shot, the Filter restored
            await c.call("scope_set_timebase", sample_rate="1MS/s")
            await c.call("scope_set_trigger", source="CH1", mode="auto", total_samples=3000)
            cap = await c.call("scope_capture", channels=["CH1"], peak=True)
            assert cap["peak"] and cap["decimation"]["filter"] == "peak" and "envelope" in cap["channels"]["CH1"]
            env = cap["channels"]["CH1"]["envelope"]
            assert env["max"] > 0.9 and env["min"] < -0.9 and "CH1_min" in cap["preview"] and "CH1_max" in cap["preview"]
            assert any("set back to 'off'" in x for x in cap["caveats"]) and any("peak-detect record" in x
                                                                                  for x in cap["caveats"])
            with np.load(cap["file"]) as z:
                assert {"CH1_min_volts", "CH1_max_volts", "CH1_min_codes", "CH1_max_codes"} <= set(z.files)
                assert json.loads(str(z["meta_json"]))["kind"] == "peak"
            assert (await c.call("scope_get_config"))["decimation"]["setting"] == "off"
            rep = await c.call("scope_report", source=cap["file"])
            assert rep["kind"] == "peak" and "envelope min" in rep["table"][0]
            # the sticky setting, and averaging refused with it
            r = await c.call("scope_set_decimation", filter="peak")
            assert r["applied"]["setting"] == "peak" and any("averaging" in x for x in r["caveats"])
            assert "peak detect" in await c.fails("scope_averaged_capture", n=2, channels=["CH1"])
            cap = await c.call("scope_capture", channels=["CH1"])
            assert cap["peak"] and not any("set back" in x for x in cap["caveats"])
            await c.call("scope_set_decimation", filter="off")
            # status: the one UI Average behaviour
            srv.device.ui_average = True
            await c.call("scope_set_trigger", source="CH1", mode="auto", total_samples=3000)
            st = await c.call("scope_status")
            assert st["ui_averaging"] in ("mcu", "hardware") and st["ui_averaging_records"] >= 2
    anyio.run(main)


def test_tools_on_a_mismatch_and_without_the_caps(sim_factory, tmp_path):
    bad = sim_factory(fpga_version=0x153B)
    old = sim_factory(caps_clear=int(P.Caps.TRIG_TYPES | P.Caps.COUNTER | P.Caps.PEAK_DETECT))

    async def main():
        async with client_for(bad, tmp_path / "a") as c:
            info = await c.call("scope_info")
            assert info["fpga_mismatch"] and f"needs 0x{P.FW_FPGA_VERSION:04X}" in info["caveats"][0]
            prof = await c.call("scope_profile")
            assert f"needs 0x{P.FW_FPGA_VERSION:04X}" in prof["caveats"][0]
            assert f"needs 0x{P.FW_FPGA_VERSION:04X}" in await c.fails("scope_capture", channels=["CH1"])
        async with client_for(old, tmp_path / "b") as c:
            assert "TRIG_TYPES" in await c.fails("scope_set_trigger", type="pulse_width", width="1us")
            assert "COUNTER" in await c.fails("scope_counter")
            assert "PEAK_DETECT" in await c.fails("scope_capture", peak=True)
            assert "PEAK_DETECT" in await c.fails("scope_set_decimation", filter="peak")
    anyio.run(main)


def test_sim_config_defaults():
    cfg = SimConfig()
    assert cfg.fpga_version == P.FW_FPGA_VERSION and cfg.variant == "al3"
