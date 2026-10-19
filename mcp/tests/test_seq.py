"""Protocol 1.17 (fw 0.2.58, FPGA 0x1536, section 14 row 56): the sequencer, segmented capture and hardware averaging.

Maths against the "seq" vectors (generated from firmware/scope/pc_seq.c), the wire layouts against the firmware's frame
vectors, the simulator's runs through the client, and the tools.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope, SeqError
from fnirsi1013d.transport import DeviceError

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())
SQUARE = "square:freq=10kHz,amp=0.3"


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


def ssim(sim_factory, signal=SQUARE, **kw):
    return sim_factory(variant="al3s", signal=signal, **kw)


def legacy_segments_setup(s: Scope, mode: int = 1) -> None:
    s.set_timebase(divider=0)  # 200 MSa/s, legacy
    s.set_trigger(source=0, edge=0, level=128, mode=mode, pretrigger_samples=32, total_samples=240)


# --- maths and wire format ----------------------------------------------------------------------------------


def test_seq_maths_match_the_firmware_vectors():
    cases = VECTORS["seq"]["cases"]
    assert len(cases) >= 70
    for c in cases:
        op, a, out = c["op"], c["args"], c["out"]
        if op == "fit":
            mode, flags, m, count, n, pre, total, wide = a
            got = [P.seq_fit(mode, flags, m, count, n, pre, total, bool(wide)), P.seq_max_count(mode, m, total),
                   P.seq_slots(mode, flags, m, count), P.seq_cmd57(mode, flags), count & 0xFFF, n & 0xFFFF]
        elif op == "hold":
            got = [P.seq_holdoff_units(a[0])]
        elif op == "entry":
            e = P.SeqEntry.decode(int(a[0], 16))
            got = [e.timestamp, e.offset, int(e.triggered), int(e.valid)]
        elif op == "segw":
            got = [P.seq_seg_word(*a)]
        elif op == "plan":
            got = list(P.seq_ui_plan(*a))
        elif op == "avg":
            got = [P.seq_avg_code(*a)]
        else:
            raise AssertionError(op)
        assert got == out, c


def test_wire_layouts_match_the_firmware_frames():
    assert P.SetSeqReq.size() == 16 and P.SeqState.size() == 48 and P.ReadSeqReq.size() == 12
    assert P.ReadSeqHeader.size() == 16 and P.SeqRunInfo.size() == 20
    req = P.SetSeqReq.unpack(frame_payload("req_set_seq"))
    assert (req.mode, req.flags, req.m, req.count, req.holdoff_ns) == (1, 0, 7, 32, 1000)
    assert P.SetSeqReq.make(1, 0, 7, 32, 0, 1000).pack() == frame_payload("req_set_seq")
    avg = P.SetSeqReq.unpack(frame_payload("req_set_seq_average_256"))
    assert (avg.mode, avg.count, avg.n) == (2, 1, 256)
    assert P.SetSeqReq.make(2, count=1, n=256).pack() == frame_payload("req_set_seq_average_256")
    assert P.SetSeqReq.make(2, count=1, n=65536).n_raw == 0 and P.SetSeqReq.make(2, count=1, n=65536).n == 65536
    rq = P.ReadSeqReq.unpack(frame_payload("req_read_seq"))
    assert (rq.what, rq.first, rq.count) == (0, 0, 32)
    ra = P.ReadSeqReq.unpack(frame_payload("req_read_seq_average_raw"))
    assert (ra.what, ra.ch_mask, ra.shift, ra.first, ra.count) == (1, 3, 0xFF, 0, 1000)
    rr = P.ReadRawReq.unpack(frame_payload("req_read_raw_segment_5"))
    assert (rr.addr_mode, rr.core_mask, rr.layout, rr.address, rr.count) == (P.ReadRawReq.ADDR_SEGMENT, 3, 1,
                                                                             5 << 16, 120)
    st = P.SeqState.unpack(frame_payload("resp_get_seq_segments_done"))
    assert (st.mode, st.m, st.count, st.holdoff_ns, st.holdoff_units, st.fit) == (1, 7, 32, 1000, 200, 0)
    assert (st.pre_words, st.total_words, st.max_count) == (16, 120, 64)
    assert st.fpga_status == P.SEQ_ST_DONE | P.SEQ_SEGMENTS and st.fpga_count == 32 and st.fpga_untriggered == 1
    assert (st.held, st.run_capture_id, st.run_count, st.run_untriggered, st.run_mode) == (1, 9, 32, 1, 1)
    assert st.pack() == frame_payload("resp_get_seq_segments_done")
    body = frame_payload("resp_read_seq_table")
    h = P.ReadSeqHeader.unpack(body)
    assert (h.capture_id, h.first, h.count, h.what, h.m_or_shift, h.pre_words, h.total_words) == (9, 0, 2, 0, 7, 16,
                                                                                                    120)
    e = [P.SeqEntry.decode(x) for x in struct.unpack_from("<2Q", body, 16)]
    assert (e[0].timestamp, e[0].offset, e[0].triggered, e[0].valid) == (0, 0, True, True)
    assert e[1].seconds == pytest.approx(100e-6) and e[1].offset == 7
    status = P.StatusResp.unpack(frame_payload("resp_get_status_1_17"))
    assert (status.features, status.ui_average, status.ui_average_log2n) == (0x1F, 2, 6)
    assert P.feature_names(status.features) == ["ext_trigger", "decim", "fir", "segments", "hw_average"]
    assert len(status.pack()) == 48 and status.pack() == frame_payload("resp_get_status_1_17")
    assert P.StatusResp.unpack(status.pack()[:44]).features == 0  # 1.16 firmware: the tail reads 0


def test_capture_info_96_and_decim_state_36():
    h = P.CaptureInfoHeader(9, 3, 0, 1, 16, 0, 0, 30, 0, 0, 128, 1, 32, 240)
    run = P.SeqRunInfo(1, P.SEQ_RUN_CIRCULAR | P.SEQ_RUN_STOPPED, 7, 0, 1000, 2, 4, 0, 1000)
    ci = P.CaptureInfo(h, [P.CaptureChannelInfo(), P.CaptureChannelInfo()], run=run)
    raw = ci.pack()
    assert len(raw) == 96 and raw[76:80] == bytes([1, 3, 7, 0])
    back = P.CaptureInfo.unpack(raw)
    assert back.run == run and back.is_run and back.run.slots == 64 and back.decim is None
    assert P.CaptureInfo.unpack(raw[:76]).run is None and not P.CaptureInfo.unpack(raw[:76]).is_run
    assert P.DecimState.size() == 36
    d = P.DecimState(features=0x1F, ui_average=1).pack()
    assert struct.unpack_from("<HBB", d, 32) == (0x1F, 1, 0)


# --- simulator through the client ---------------------------------------------------------------------------


def test_status_features_and_ui_average(sim_factory):
    srv = ssim(sim_factory)
    s = Scope.connect(srv.url)
    try:
        st = s.status()
        assert st.features == 0x5F and st.ui_average == 0 and s.info.proto_minor >= 17  # + 1.20 BODE (bit 6)
        assert s.info.caps & P.Caps.SEQ
        s.set_timebase(divider=0)
        s.set_trigger(pretrigger_samples=1500, total_samples=3000)
        srv.device.ui_average = True
        st = s.status()
        assert (st.ui_average, st.ui_average_log2n) == (2, 6)  # 1500 words at 200 MSa/s: 64 records in 40 ms
        assert s.get_decim().features == 0x5F and s.get_decim().ui_average == 2
        s.set_timebase(divider=19)
        s.set_decim("cic2")  # 16-bit records never fit the accumulator: software averaging
        assert s.status().ui_average == 1
    finally:
        s.close()
    old = sim_factory(variant="al3f")
    with Scope.connect(old.url) as s:
        assert s.status().features == 0x47
        with pytest.raises(DeviceError) as e:
            s.get_seq()
        assert e.value.status == P.Status.E_UNSUPPORTED


def test_segment_run_timestamps_and_records(sim_factory):
    srv = ssim(sim_factory)
    with Scope.connect(srv.url) as s:
        legacy_segments_setup(s)
        g = s.get_seq()
        assert (g.pre_words, g.total_words, g.wide, g.fit) == (16, 120, 0, 0)
        run = s.capture_segments(["CH1", "CH2"], 32, holdoff_ns=1000)
        r = run.run
        assert (r.mode, r.m, r.count, r.requested, r.untriggered, r.holdoff_ns) == (P.SEQ_SEGMENTS, 7, 32, 32, 0, 1000)
        assert len(run.segments) == 32 and all(g.entry.valid and g.entry.triggered for g in run.segments)
        ts = np.array([g.t_trigger for g in run.segments])
        assert np.allclose(np.diff(ts), 100e-6, atol=10e-9)  # one 10 kHz period apart
        for g in run.segments:
            v = g.capture.channels["CH1"].volts
            assert v.size == 240 and g.capture.pretrigger_samples == 32
            assert v[:30].max() < 0 and v[34:60].min() > 0  # the rising edge at the pretrigger point
        assert s.get_seq().mode == P.SEQ_OFF  # the setting is cleared after the run
        st = s.get_seq()
        assert st.held == 1 and st.run_capture_id == run.capture_id and st.fpga_status & P.SEQ_ST_DONE
        # the plain readers refuse a segment run; addr_mode 0 and 3 work
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x03, addr_mode=1, count=10)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 4)
        with pytest.raises(DeviceError) as e:
            s.read_trace(["CH1"])
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 5)
        hdr, cores = s.read_raw(0x01, addr_mode=P.ReadRawReq.ADDR_SEGMENT, address=(3 << 16) | 5, count=10)
        assert hdr.ring_words == 128 and 3 * 128 <= hdr.start_word < 4 * 128
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x01, addr_mode=P.ReadRawReq.ADDR_SEGMENT, address=32 << 16, count=10)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 4)
        # a plain capture replaces the run
        assert s.capture(["CH1"]) is not None
        with pytest.raises(DeviceError) as e:
            s.read_seq_table(0, 1)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 0)


def test_segments_untriggered_in_auto_mode(sim_factory):
    srv = ssim(sim_factory, signal="dc:v=0.2")
    with Scope.connect(srv.url) as s:
        legacy_segments_setup(s, mode=0)
        run = s.capture_segments(["CH1"], 4)
        assert run.run.count == 4 and run.run.untriggered == 4
        assert all(g.entry.valid and not g.entry.triggered for g in run.segments)
        assert all(g.capture.auto_timeout for g in run.segments)
        assert "auto time-out" in run.notes[0]


def test_segments_stop_ends_a_run_and_circular_wraps(sim_factory):
    srv = ssim(sim_factory, signal="dc:v=0.2")  # normal mode, no trigger: the run never ends by itself
    with Scope.connect(srv.url) as s:
        legacy_segments_setup(s)
        run = s.capture_segments(["CH1"], 4, timeout_s=0.2)
        assert run.run.count == 0 and run.run.flags & P.SEQ_RUN_STOPPED
        assert not any(g.entry.valid for g in run.segments) and "ended with STOP" in run.notes[0]
    srv = ssim(sim_factory)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=0)
        s.set_trigger(source=0, edge=0, level=128, mode=1, pretrigger_samples=64, total_samples=1000)  # m 9
        run = s.capture_segments(["CH1"], 4, circular=True, timeout_s=0.2, read=False)
        r = run.run
        slots = 8192 >> 9
        assert r.m == 9 and r.slots == slots and len(run.entries) == slots
        assert r.flags == P.SEQ_RUN_CIRCULAR | P.SEQ_RUN_STOPPED | P.SEQ_RUN_WRAPPED and r.count > slots
        invalid = [i for i, e in enumerate(run.entries) if not e.valid]
        assert invalid == [r.count % slots]  # the slot being refilled
        ts = sorted(e.timestamp for e in run.entries if e.valid)
        assert np.allclose(np.diff(ts) * 5e-9, 100e-6, atol=10e-9)


def test_segment_fit_errors(sim_factory):
    srv = ssim(sim_factory)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=0)
        s.set_trigger(pretrigger_samples=100, total_samples=9000)  # 4500 words: no slot holds it
        with pytest.raises(SeqError) as e:
            s.capture_segments(["CH1"], 2)
        assert e.value.fit == P.SEQ_FIT_M
        s.set_trigger(pretrigger_samples=100, total_samples=1000)
        with pytest.raises(SeqError) as e:
            s.capture_segments(["CH1"], 17, m=9)  # 8192 >> 9 = 16 slots
        assert e.value.fit == P.SEQ_FIT_COUNT
        with pytest.raises(DeviceError) as e:
            s.set_seq(P.SEQ_SEGMENTS, m=7, count=4)  # 500 words > 2^7 - 2
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.SEQ_FIT_TOTAL)
        with pytest.raises(DeviceError) as e:
            s.set_seq(P.SEQ_AVERAGE, count=1, n=300)  # 8-bit records: N <= 257
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 6)
        st = s.set_seq(P.SEQ_SEGMENTS, m=9, count=4)
        s.set_trigger(total_samples=2000)  # the setting no longer fits: ARM refuses
        assert s.get_seq().fit == P.SEQ_FIT_TOTAL and st.fit == 0
        with pytest.raises(DeviceError) as e:
            s.arm()
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 3)
        s.set_seq(P.SEQ_OFF)
        assert s.arm().acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED, P.AcqState.DONE)


def test_hardware_average_legacy(sim_factory):
    srv = ssim(sim_factory, signal="square:freq=1kHz,amp=0.3", noise_lsb=2.0)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=0)
        s.set_trigger(source=0, edge=0, level=128, mode=1, pretrigger_samples=200, total_samples=2000)
        single = s.capture(["CH1"])
        av = s.capture_hw_average(["CH1"], 64)
        r = av.run
        assert (r.mode, r.n, r.requested, r.count) == (P.SEQ_AVERAGE, 64, 1, 64) and av.records == [64]
        m = av.positions[0].channels["CH1"]
        v1 = single.channels["CH1"].volts
        flat, flat1 = m.volts[400:], v1[400:]
        assert m.volts.size == 2000 and abs(flat.mean() - flat1.mean()) < 0.5 * single.channels["CH1"].volts_per_lsb
        assert flat.std() < flat1.std() / 5  # 64 records: noise / 8
        # the sums read back exactly as the shifted lanes
        _, sums = s.read_seq_average(["CH1"], P.SEQ_SHIFT_SUMS, 0, 1000)
        _, shifted = s.read_seq_average(["CH1"], 6, 0, 1000)
        assert np.array_equal(sums[0][0] >> 6, shifted[0][0])
        # the last record stays readable as a normal capture
        last = s.read_capture(["CH1"])
        assert last.capture_info.run.mode == P.SEQ_AVERAGE and last.n == 2000


def test_hardware_average_positions_and_16bit(sim_factory):
    srv = ssim(sim_factory, signal="sine:freq=10kHz,amp=0.3")
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=19)  # 10 MSa/s with CIC2: 16-bit records
        s.set_decim("cic2")
        s.set_trigger(source=0, edge=0, level=128, mode=1, pretrigger_samples=100, total_samples=1000)
        av = s.capture_hw_average(["CH1", "CH2"], 16, positions=2)
        r = av.run
        assert r.wide == 1 and r.requested == 2 and av.records == [16, 16]
        for cap in av.positions:
            v = cap.channels["CH1"].volts
            assert cap.decim is not None and v.size == 1000
            assert v.max() == pytest.approx(0.3, abs=0.02) and v.min() == pytest.approx(-0.3, abs=0.02)
        _, sums = s.read_seq_average(["CH1"], P.SEQ_SHIFT_SUMS)
        assert sums[0].dtype == np.uint32 and sums[0].size == 2000
        with pytest.raises(DeviceError) as e:
            s.read_seq_table(0, 1)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 2)


# --- tools --------------------------------------------------------------------------------------------------


def test_segment_and_hardware_averaging_tools(sim_factory, tmp_path):
    from test_server import client_for
    srv = ssim(sim_factory)

    async def main():
        async with client_for(srv, tmp_path) as c:
            st = await c.call("scope_status")
            assert st["features"] == ["ext_trigger", "decim", "fir", "segments", "hw_average", "bode"]
            assert st["ui_averaging"] == "off" and st["hardware_averaging"] is False
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                         pretrigger_samples=32, total_samples=240)
            r = await c.call("scope_capture_segments", count=8, channels=["CH1"], holdoff="1us")
            assert r["segments"] == 8 and r["completed"] == 8 and r["slot_words"] == 128
            assert r["interval_s"]["mean"] == pytest.approx(100e-6, abs=1e-8)
            assert r["table"][0]["t"] == 0 and r["table"][1]["CH1"]["vpp"] > 0.5
            npz = np.load(r["file"])
            assert npz["CH1_volts"].shape == (8, 240) and npz["timestamps_s"].size == 8
            a = await c.call("scope_averaged_capture", n=16, channels=["CH1"], hardware=True, reject_clipped=False)
            assert a["averaging"] == "hardware" and a["n_used"] == 16 and a["n_samples"] == 240
            assert any("hardware averaging (FPGA 0x1536" in x for x in a["caveats"])
            await c.call("scope_set_trigger", total_samples=6000)
            assert "2046 words" in await c.fails("scope_averaged_capture", n=16, hardware=True)
            assert "needs FPGA bitstream 0x1536" not in await c.fails("scope_capture_segments", count=100)
        async with client_for(sim_factory(variant="al3f"), tmp_path) as c:
            assert "needs FPGA bitstream 0x1536" in await c.fails("scope_capture_segments", count=4)
            assert "needs FPGA bitstream 0x1536" in await c.fails("scope_averaged_capture", n=4, hardware=True)
    anyio.run(main)
