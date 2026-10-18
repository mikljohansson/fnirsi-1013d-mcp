"""Simulator semantics (ring, trigger, read_lag, ownership) through the high-level Scope client."""

from __future__ import annotations

import time

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope, compensate, interleave
from fnirsi1013d.transport import DeviceError, TransportError


def remote_scope(srv) -> Scope:
    s = Scope.connect(srv.url)
    s.take_remote(watchdog_s=0)
    return s


@pytest.mark.parametrize("variant,version,ring,maxs", [("stock", 0x1432, 4096, 3000), ("al3", 0x1532, 8192, 16382),
                                                        ("ef2", 0x1632, 6144, 12286), ("ep4", 0x1732, 6144, 12286)])
def test_variant_identity(sim_factory, variant, version, ring, maxs):
    srv = sim_factory(variant=variant)
    with Scope.connect(srv.url) as s:
        i = s.info
        assert (i.fpga_version, i.ring_words, i.max_total_samples) == (version, ring, maxs)
        p = s.profile()
        assert p.variant == variant
        assert p.programmable == (variant != "stock") and p.has_generator == (variant != "stock")


def test_stock_geometry_is_fixed(sim_factory):
    srv = sim_factory(variant="stock")
    s = remote_scope(srv)
    try:
        with pytest.raises(DeviceError) as e:
            s.set_trigger(pretrigger_samples=200, total_samples=1000)
        assert e.value.status == P.Status.E_UNSUPPORTED
        tr = s.get_trigger(cached=False)
        assert (tr.pretrigger_samples, tr.total_samples, tr.pretrigger_words) == (1500, 3000, 750)
        with pytest.raises(DeviceError) as e:
            s.set_gen(enable=True)
        assert e.value.status == P.Status.E_UNSUPPORTED
        cap = s.capture(["CH1"], 1.0)
        assert cap is not None and cap.n == 3000
    finally:
        s.close()


def test_trigger_validation_al3(sim_factory):
    s = remote_scope(sim_factory())
    try:
        for kw, bit in ((dict(pretrigger_samples=101, total_samples=1000), 4),
                        (dict(pretrigger_samples=100, total_samples=17000), 5),
                        (dict(pretrigger_samples=1000, total_samples=1000), 4)):
            with pytest.raises(DeviceError) as e:
                s.set_trigger(**kw)
            assert e.value.status == P.Status.E_BAD_ARG and e.value.detail == bit
        with pytest.raises(DeviceError) as e:
            s.set_trigger(holdoff=5)
        assert e.value.status == P.Status.E_UNSUPPORTED
        with pytest.raises(DeviceError) as e:
            s.set_trigger(source=2)
        assert e.value.status == P.Status.E_UNSUPPORTED
    finally:
        s.close()


def test_trigger_edge_at_pretrigger_index(sim_factory):
    """Falling TX gate on CH2: the hardware trigger word sits at pretrigger_words; the first low core-A sample
    is at interleaved index pretrigger_samples + 1 (B before A)."""
    srv = sim_factory(signal="decay:tau=80us,amp=0.2,period=1ms,width=200us", noise_lsb=0.0)
    s = remote_scope(srv)
    try:
        s.set_channel(1, range_index=1)  # 2.5 V/div: gate 3.3 V not clipped
        s.set_timebase(divider=9)
        cfg = s.get_channel(1)
        lvl = int(round(1.65 / cfg.volts_per_lsb + cfg.zero_code))  # mid-level of the 3.3 V gate
        s.set_trigger(source=1, edge=1, level=lvl, mode=1, pretrigger_samples=800, total_samples=16000)
        for _ in range(5):
            cap = s.capture(["CH1", "CH2"], 1.0, keep_raw=True)
            assert cap is not None and cap.triggered and not cap.auto_timeout
            a = cap.channels["CH2"].raw_a
            k = 400  # pretrigger_words
            assert a[k - 1] >= lvl > a[k]
            st = s.status()
            assert st.capture_start_word == (st.trigger_addr - 400) % 8192
    finally:
        s.close()


def test_auto_timeout_flags(sim_factory):
    srv = sim_factory(signal="zero")
    s = remote_scope(srv)
    try:
        s.set_trigger(mode=0, level=250, pretrigger_samples=1000, total_samples=2000)
        cap = s.capture(["CH1"], 1.0)
        assert cap is not None and cap.auto_timeout and not cap.triggered
        st = s.status()
        assert st.trigger_addr == 500 and st.capture_start_word == 0
        assert st.flags & P.StatusFlags.AUTO_TIMEOUT
    finally:
        s.close()


def test_force_and_stop(sim_factory):
    srv = sim_factory(signal="zero")
    s = remote_scope(srv)
    try:
        s.set_trigger(mode=1, level=250)
        with pytest.raises(DeviceError) as e:
            s.force()
        assert e.value.status == P.Status.E_STATE
        s.arm()
        assert s.status().acq_state == P.AcqState.ARMED
        st = s.force()
        assert st.acq_state == P.AcqState.DONE and st.flags & P.StatusFlags.AUTO_TIMEOUT
        s.arm()
        st = s.stop()
        assert st.acq_state == P.AcqState.ABORTED
        with pytest.raises(DeviceError) as e:
            s.read_raw(1, count=10)
        assert e.value.status == P.Status.E_STATE
        hdr, d = s.read_raw(1, count=10, allow_not_done=True)
        assert hdr.acq_state == P.AcqState.ABORTED and d[0].size == 10
    finally:
        s.close()


def test_read_raw_addressing_and_layouts(sim_factory):
    s = remote_scope(sim_factory())
    try:
        s.set_trigger(mode=0, pretrigger_samples=1000, total_samples=4000)
        assert s.capture(["CH1"], 1.0) is not None
        st = s.status()
        h0, blocks = s.read_raw(0x0F, addr_mode=0, address=st.capture_start_word, count=100)
        h1, blocks1 = s.read_raw(0x0F, addr_mode=1, address=0, count=100)
        h2, blocks2 = s.read_raw(0x0F, addr_mode=2, address=-500, count=100)
        assert h0.start_word == h1.start_word == h2.start_word
        for c in range(4):
            assert np.array_equal(blocks[c], blocks1[c]) and np.array_equal(blocks[c], blocks2[c])
        hi, il = s.read_raw(0x0F, addr_mode=1, address=0, count=100, layout=1)
        assert np.array_equal(il[0][0::2], blocks[1]) and np.array_equal(il[0][1::2], blocks[0])
        with pytest.raises(DeviceError) as e:
            s.read_raw(0x01, count=10, layout=1)
        assert e.value.status == P.Status.E_BAD_ARG
        # wrap: last words of the ring followed by the first
        h, w = s.read_raw(0x01, addr_mode=0, address=8190, count=4)
        full = s.read_raw(0x01, addr_mode=0, address=0, count=8192)[1][0]
        assert np.array_equal(w[0], full[[8190, 8191, 0, 1]])
    finally:
        s.close()


def test_read_lag_mismatch_shifts_data(sim_factory):
    good = sim_factory(seed=7, hw_read_lag=1, fw_read_lag=1)
    bad = sim_factory(seed=7, hw_read_lag=1, fw_read_lag=0)
    for srv in (good, bad):
        srv.device.ring[:] = np.arange(8192, dtype=np.uint32).astype(np.uint8)
        srv.device.acq_state = P.AcqState.DONE
        srv.device.control = P.Control.REMOTE
    with Scope.connect(good.url) as sg, Scope.connect(bad.url) as sb:
        g = sg.read_raw(1, addr_mode=0, address=100, count=8)[1][0]
        b = sb.read_raw(1, addr_mode=0, address=100, count=8)[1][0]
    assert list(g) == list(range(100, 108))
    assert list(b) == list(range(99, 107))  # one word early: what bench test T3 must catch


def test_debug_fpga_stale_first_byte(sim_factory):
    srv = sim_factory()
    srv.device.ring[0, :] = 50
    srv.device.ring[0, 200:210] = np.arange(10) + 60
    s = remote_scope(srv)
    try:
        data = s.debug_fpga([(P.DBG_WRITE_CMD, 0x06), (P.DBG_READ_DATA, 2), (P.DBG_WRITE_CMD, 0x1F),
                             (P.DBG_WRITE_DATA, b"\x00\xc8"), (P.DBG_WRITE_CMD, 0x20), (P.DBG_READ_DATA, 4)])
        assert data[:2] == b"\x15\x32"
        assert data[2] == 61 and list(data[3:]) == [61, 62, 63]  # stale (+1) then mem[201..]
    finally:
        s.close()


def test_compensation_removes_core_offset(sim_factory):
    srv = sim_factory(signal="dc:v=0", noise_lsb=0.3)
    s = remote_scope(srv)
    try:
        s.set_trigger(mode=0, pretrigger_samples=1000, total_samples=4000)
        cap = s.capture(["CH1"], 1.0, keep_raw=True)
        ch = cap.channels["CH1"]
        assert abs(ch.raw_b.mean() - ch.raw_a.mean() - 3.0) < 0.3  # simulated core B offset
        assert abs(ch.codes[0::2].mean() - ch.codes[1::2].mean()) < 0.3
        assert abs(ch.volts.mean()) < 2 * ch.volts_per_lsb
    finally:
        s.close()


def test_volts_scaling_and_probe(sim_factory):
    srv = sim_factory(signal="dc:v=0.3", noise_lsb=0.0)
    s = remote_scope(srv)
    try:
        s.set_channel(0, range_index=4)  # 200 mV/div
        s.set_trigger(mode=0, pretrigger_samples=100, total_samples=1000)
        v1 = s.capture(["CH1"], 1.0).channels["CH1"].volts.mean()
        assert abs(v1 - 0.3) < 0.01
        s.set_channel(0, probe_index=2)  # 10x: same codes, ten times the volts
        v10 = s.capture(["CH1"], 1.0).channels["CH1"].volts.mean()
        assert abs(v10 - 3.0) < 0.1
        cfg = s.get_channel(0)
        assert cfg.uv_per_lsb_q8 == P.uv_per_lsb_q8(P.DEFAULT_INPUT_CALIBRATION[4], 4)
    finally:
        s.close()


def test_offset_moves_zero_code(sim_factory):
    s = remote_scope(sim_factory(signal="dc:v=0", noise_lsb=0.0))
    try:
        c0 = s.set_channel(0, dcoffset=0)
        c1 = s.set_channel(0, dcoffset=-300)
        assert c1.zero_code > c0.zero_code
        s.set_trigger(mode=0, pretrigger_samples=100, total_samples=1000)
        cap = s.capture(["CH1"], 1.0)
        assert abs(cap.channels["CH1"].codes.mean() - c1.zero_code) < 1.0
        assert abs(cap.channels["CH1"].volts.mean()) < 0.01
    finally:
        s.close()


def test_read_trace_matches_host_compensation(sim_factory):
    s = remote_scope(sim_factory())
    try:
        s.set_trigger(mode=0, pretrigger_samples=1000, total_samples=4000)
        cap = s.capture(["CH1"], 1.0)
        hdr, tr = s.read_trace(["CH1"])
        meta, data = tr[0]
        assert hdr.sample_count == 4000 and hdr.hw_trigger_index == 1000 and hdr.origin == 0
        assert meta.adc_b_comp == -3
        host = cap.channels["CH1"].codes
        assert np.array_equal(data[2:].astype(float), host[2:])  # first pair: stale pipeline byte
        assert abs(int(data[0]) - host[0]) <= 1 and abs(int(data[1]) - host[1]) <= 1
    finally:
        s.close()


def test_local_mode_trace_and_restrictions(sim_factory):
    srv = sim_factory()
    with Scope.connect(srv.url) as s:
        hdr, tr = s.read_trace(["CH1", "CH2"])
        assert hdr.origin == 1 and set(tr) == {0, 1}
        with pytest.raises(DeviceError) as e:
            s.set_timebase(divider=3)
        assert e.value.status == P.Status.E_NOT_REMOTE
        assert s.status().control == P.Control.LOCAL


def test_remote_snapshot_restore_and_keep(sim_factory):
    srv = sim_factory()
    with Scope.connect(srv.url) as s:
        div0 = s.get_timebase(cached=False).divider
        s.take_remote()
        s.set_timebase(divider=3)
        s.release()
        assert s.get_timebase(cached=False).divider == div0
        assert s.status().exit_reason == P.ExitReason.HOST_RELEASE
        s.take_remote()
        s.set_timebase(divider=5)
        s.release(keep_settings=True)
        assert s.get_timebase(cached=False).divider == 5


def test_user_tap_watchdog_and_dtr(sim_factory):
    srv = sim_factory()
    s = Scope.connect(srv.url)
    s.take_remote(watchdog_s=0)
    srv.device.user_tap()
    st = s.status()
    assert st.control == P.Control.LOCAL and st.exit_reason == P.ExitReason.USER_TAP
    s.take_remote(watchdog_s=1)
    time.sleep(1.3)
    st = s.status()
    assert st.control == P.Control.LOCAL and st.exit_reason == P.ExitReason.WATCHDOG
    s.take_remote(watchdog_s=0)
    s.close()  # DTR drop
    time.sleep(0.1)
    s2 = Scope.connect(srv.url)
    st = s2.status()
    assert st.control == P.Control.LOCAL and st.exit_reason == P.ExitReason.DTR_DROP
    s2.take_remote(ignore_dtr=True)
    s2.close()  # link drops without release
    time.sleep(0.1)
    with Scope.connect(srv.url) as s3:
        assert s3.status().control == P.Control.REMOTE


def test_ui_busy(sim_factory):
    srv = sim_factory()
    srv.device.ui_busy = True
    with Scope.connect(srv.url) as s:
        assert s.status().ui_busy == 1
        with pytest.raises(DeviceError) as e:
            s.take_remote()
        assert e.value.status == P.Status.E_BUSY


def test_device_average_matches_host(sim_factory):
    srv = sim_factory(signal="sine:freq=50kHz,amp=0.25", noise_lsb=1.0)
    s = remote_scope(srv)
    try:
        s.set_channel(0, range_index=4)
        cfg = s.get_channel(0)
        lvl = int(round(cfg.zero_code))
        s.set_trigger(source=0, edge=0, level=lvl, mode=1, pretrigger_samples=200, total_samples=2000)
        s.set_timebase(divider=19)
        hdr, data = s.acquire_avg_device(32, ["CH1"], 0, 2000, 500)
        assert hdr.n_used == 32 and hdr.count == 2000
        mean = data[0]
        t = (np.arange(2000) - 200) / 10e6
        # rising zero crossing at the trigger: compare with an ideal sine through the trigger point
        ideal = cfg.zero_code + 0.25 * np.sin(2 * np.pi * 50e3 * t) / cfg.volts_per_lsb
        assert np.sqrt(np.mean((mean - ideal) ** 2)) < 2.5  # within the 2-sample trigger quantisation
    finally:
        s.close()


def test_helpers():
    a = np.array([1, 3, 5], dtype=np.uint8)
    b = np.array([2, 4, 6], dtype=np.uint8)
    assert list(interleave(a, b, 0)) == [2, 1, 4, 3, 6, 5]
    assert list(interleave(a, b, 1)) == [1, 2, 3, 4, 5, 6]
    assert list(compensate(np.array([0, 250, 128], dtype=np.uint8), 10)) == [10, 255, 138]


def test_chunked_reads_match_single_reads(sim_factory):
    """With a 5 KiB response cap (firmware 0.1.0 on hardware) READ_RAW, READ_TRACE and READ_SCREEN are split
    and give the same data; the simulator drops anything larger, as the real firmware did."""
    srv = sim_factory(fail_responses_over=5120)
    small = Scope.connect(srv.url, max_response=5120)
    try:
        small.take_remote(watchdog_s=0)
        small.set_trigger(mode=0, pretrigger_samples=1000, total_samples=16000)
        cap = small.capture(["CH1", "CH2"], 1.0, keep_raw=True)
        assert cap.n == 16000
        srv.device.cfg.fail_responses_over = None
        ref = small.read_raw(0x0F, count=8000)
        srv.device.cfg.fail_responses_over = 5120
        for c in range(4):
            raw = (cap.channels["CH1"] if c < 2 else cap.channels["CH2"])
            assert np.array_equal(raw.raw_a if c % 2 == 0 else raw.raw_b, ref[1][c])
        hdr, tr = small.read_trace(["CH1", "CH2"])
        assert hdr.count == 16000 and tr[0][1].size == 16000 and tr[1][1].size == 16000
        assert small.read_screen().shape == (480, 800)
        # unchunked: the 32 KiB response is dropped and the request times out
        with pytest.raises(TransportError):
            small.t.request(P.Cmd.READ_RAW, P.ReadRawReq(0x0F, 1, 0, 0, 0, 8000).pack(), timeout=0.2, retries=0)
    finally:
        small.close()
