"""mcp-notes (2026-11-12): the bench session's requests: inline waveform size (preview_points), window = what the
statistics cover (the file keeps the record), level crossings, the FFT fallback of the frequency, the compact
scope_status, and scope_wait_for_event."""

from __future__ import annotations

import json
import math
import threading
import time

import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P
from fnirsi1013d.simulator import Signal

from test_server import client_for

pytestmark = pytest.mark.anyio

#: the bench's falling edge: CH2 at 18 V, released into an RC of 50 us towards 0 V (5 V/div DC)
TAU = 50e-6
DECAY = Signal("decay", {"period": 10e-3, "width": 1e-3, "tx": 18.0, "amp": 18.0, "tau": TAU, "offset": 0.0})
LEVELS = [15.3, 14.0, 8.0, 1.6]
TRIG_V = 17.0


@pytest.fixture
def anyio_backend():
    return "asyncio"


def size(r) -> int:
    return len(json.dumps(r))


def buck_record(fs: float, n: int = 16382, seed: int = 1) -> np.ndarray:
    """483 kHz ripple of 13 mV rms with ringing switching spikes (0.24 Vpp), 1 code of noise, quantised to the 8-bit
    step at 50 mV/div (about 3.4 mV)."""
    lsb = 0.1 * 8.8 / 256
    rng = np.random.default_rng(seed)
    t = np.arange(n) / fs + rng.uniform(0, 1 / 483e3)
    v = Signal("buck", {}).eval(t, None) + rng.normal(0, lsb, n)  # type: ignore[arg-type]
    return np.round(v / lsb) * lsb


# --- item 4: the frequency's FFT fallback ------------------------------------------------------------------------


@pytest.mark.parametrize("fs", [20e6, 200e6])
def test_buck_ripple_frequency_falls_back_to_the_fft_peak(fs):
    v = buck_record(fs)
    old = A.frequency(v, 1 / fs)
    assert old["frequency"] is None and old["reason"].startswith("irregular crossings")  # what the bench session saw
    new = A.frequency_or_fft(v, 1 / fs)
    assert new["method"] == "fft_peak" and new["duty"] is None
    assert new["resolution"] == pytest.approx(fs / v.size)
    assert abs(new["frequency"] - 483e3) <= new["resolution"]


def test_regular_signal_keeps_the_crossings():
    t = np.arange(10000) * 1e-7
    r = A.frequency_or_fft(np.sin(2 * np.pi * 10e3 * t), 1e-7)
    assert r["method"] == "crossings" and r["frequency"] == pytest.approx(10e3, rel=1e-3) and r["duty"] is not None
    flat = A.frequency_or_fft(np.zeros(1000), 1e-7)
    assert flat["method"] is None and flat["frequency"] is None
    # part of one slow period plus noise (the battery's RC output at 200 MSa/s, finding 4a): the strongest "line" is a
    # first bin, which is no frequency
    rng = np.random.default_rng(3)
    ramp = np.round((np.linspace(0, 0.3, 3000) + rng.normal(0, 0.02, 3000)) / 0.04) * 0.04
    r = A.frequency_or_fft(ramp, 5e-9)
    assert r["frequency"] is None and r["method"] is None


async def test_measure_buck_ripple_on_the_simulator(sim_factory, tmp_path):
    srv = sim_factory(signals=(Signal("buck", {}), Signal("zero")))
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.05, coupling="AC")
        await c.call("scope_set_channel", channel="CH2", enabled=False)
        for fs in ("20MS/s", "200MS/s"):
            await c.call("scope_set_timebase", sample_rate=fs)
            await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="auto",
                         total_samples=16000)
            cap = await c.call("scope_capture", channels=["CH1"], preview_points=0)
            v = np.load(cap["file"])["CH1_volts"].astype(np.float64)
            assert A.frequency(v, cap["dt"])["frequency"] is None  # the old code: null
            m = (await c.call("scope_measure", items=["frequency", "period", "duty"]))["channels"]["CH1"]
            assert m["frequency_method"] == "fft_peak" and m["duty"] is None
            assert abs(m["frequency"] - 483e3) <= m["frequency_resolution"]
            assert m["frequency_reason"].startswith("irregular crossings")
            assert cap["channels"]["CH1"]["frequency_method"] == "fft_peak"  # the capture result says so too
            mres = await c.call("scope_measure", items=["frequency"])
            assert any("strongest spectral line" in x for x in mres["caveats"])


# --- item 3: level crossings ------------------------------------------------------------------------------------


def test_level_crossings_on_a_noisy_quantised_decay():
    fs, lsb = 10e6, 0.17
    t = (np.arange(16000) - 800) / fs
    v = np.where(t < 0, 18.0, 18 * np.exp(-np.clip(t, 0, None) / TAU))
    v = np.round((v + np.random.default_rng(2).normal(0, lsb, t.size)) / lsb) * lsb
    r = A.level_crossings(t, v, LEVELS + [30.0, 17.99], edge="falling", smooth=5, lsb=lsb, between=(15.3, 1.6))
    by = {d["level"]: d for d in r["levels"]}
    for lv in LEVELS:
        d = by[lv]
        assert d["count"] == 1, d
        x = d["crossings"][0]
        truth = TAU * math.log(18 / lv)
        assert abs(x["t"] - truth) < max(4 * x["t_sigma"], 2 / fs), (lv, x, truth)
        assert x["slope_v_per_s"] < 0 and x["edge"] == "falling"
    assert by[30.0]["count"] == 0 and by[17.99]["count"] == 0
    assert any("never reached" in c for c in r["caveats"]) and any("17.99" in c for c in r["caveats"])
    assert abs(r["between"]["interval"] - TAU * math.log(15.3 / 1.6)) < 2e-6


def test_level_crossings_several_and_capped():
    t = np.arange(20000) * 1e-7
    v = np.sin(2 * np.pi * 10e3 * t + 0.3)
    r = A.level_crossings(t, v, [0.0], edge="either", max_per_level=5)
    d = r["levels"][0]
    assert d["count"] == 40 and len(d["crossings"]) == 5 and d["truncated"]
    assert any("crossed 40 times" in c for c in r["caveats"])
    rising = A.level_crossings(t, v, [0.0], edge="rising")["levels"][0]
    assert rising["count"] == 20 and abs(rising["first"] - (2 * np.pi - 0.3) / (2 * np.pi * 10e3)) < 1e-8  # interpolated between samples


async def setup_decay(c, total=16000):
    await c.call("scope_set_channel", channel="CH1", enabled=False)
    await c.call("scope_set_channel", channel="CH2", volts_per_div=5.0, coupling="DC")
    await c.call("scope_set_timebase", sample_rate="10MS/s")
    await c.call("scope_set_trigger", source="CH2", edge="falling", level=TRIG_V, mode="normal",
                 pretrigger_fraction=0.05, total_samples=total)


def check_crossings(cr, t_tol=1.5e-6):
    by = {d["level"]: d for d in cr["levels"]}
    for lv in LEVELS:
        d = by[lv]
        assert d["count"] == 1, d
        assert abs(d["first"] - TAU * math.log(TRIG_V / lv)) < t_tol, (lv, d)  # relative to the trigger
    b = cr["between"]
    assert b["from_level"] == 15.3 and b["to_level"] == 1.6
    assert abs(b["interval"] - TAU * math.log(15.3 / 1.6)) < 1e-6


async def test_measure_crossings_from_capture_and_file(sim_factory, tmp_path):
    srv = sim_factory(signals=(Signal("zero"), DECAY))
    async with client_for(srv, tmp_path) as c:
        await setup_decay(c)
        cap = await c.call("scope_capture", channels=["CH2"], preview_points=0)
        r = await c.call("scope_measure", channels=["CH2"], items=["crossings", "fall_time"], levels=LEVELS,
                         edge="falling", smooth="0.5us")
        cr = r["channels"]["CH2"]["crossings"]
        assert cr["smooth_samples"] == 5 and cr["edge"] == "falling"
        check_crossings(cr)
        f = await c.call("scope_measure", source=cap["file"], channels=["CH2"], levels=LEVELS, edge="falling",
                         smooth=5)
        assert f["source"] == cap["file"] and "vpp" in f["channels"]["CH2"]  # default items + crossings
        check_crossings(f["channels"]["CH2"]["crossings"])
        msg = await c.fails("scope_measure", items=["crossings"])
        assert "needs levels" in msg


# --- items 1 and 2: preview_points and the window ------------------------------------------------------------------


async def test_preview_points_sizes(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
        await c.call("scope_set_channel", channel="CH2", enabled=True, volts_per_div=1.0)
        await c.call("scope_set_timebase", sample_rate=1e6)
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                     pretrigger_fraction=0.5, total_samples=10000)
        d = await c.call("scope_capture", channels=["CH1", "CH2"])
        z = await c.call("scope_capture", channels=["CH1", "CH2"], preview_points=0)
        p = await c.call("scope_capture", channels=["CH1", "CH2"], preview_points=50)
        s = await c.call("scope_capture", channels=["CH1", "CH2"], window=["-200us", "200us"], preview_points=250)
        print(f"two-channel capture of 10000 samples: default {size(d)} B, preview_points=0 {size(z)} B, "
              f"50 {size(p)} B; 401-sample window inline {size(s)} B")
        assert len(d["preview"]["CH1"]["vmin"]) == 250 and "samples" not in d  # the default is unchanged
        assert "preview" not in z and "samples" not in z and size(z) < size(d) / 4
        assert z["channels"]["CH1"]["vpp"] == pytest.approx(0.5, abs=0.03) and z["file"]
        assert len(p["preview"]["CH2"]["vmax"]) == 50
        assert len(s["samples"]["t"]) == s["stats_span"]["n_samples"] <= 402  # 2 x 250 >= 401: in full
        f = await c.call("scope_fetch_waveform", channels=["CH1", "CH2"], preview_points=0)
        assert "preview" not in f and "samples" not in f
        a = await c.call("scope_averaged_capture", n=4, channels=["CH1"], preview_points=0)
        assert "preview" not in a and "mean" not in a
        a = await c.call("scope_averaged_capture", n=4, channels=["CH1"], preview_points=20)
        assert len(a["preview"]["CH1"]["vmin"]) == 20


async def test_fetch_window_keeps_the_record(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
        await c.call("scope_set_timebase", sample_rate=1e6)
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                     pretrigger_fraction=0.5, total_samples=10000)
        await c.call("scope_capture", channels=["CH1"], preview_points=0)
        # a quarter period after the trigger: the sine's rising half, so vmin / vmax describe only that span
        f = await c.call("scope_fetch_waveform", channels=["CH1"], window=["0", "25us"])
        sp = f["stats_span"]
        assert not sp["whole_record"] and sp["n_samples"] in (26, 27) and f["n_samples"] == 10000
        assert f["channels"]["CH1"]["min"] > -0.05 and f["channels"]["CH1"]["max"] > 0.2
        assert np.load(f["file"])["t"].size == 10000
        assert "outside the capture" in await c.fails("scope_fetch_waveform", window=["1s", "2s"])


# --- item 5: the compact scope_status -----------------------------------------------------------------------------


async def test_status_compact_and_detail(client_factory_sim, tmp_path):
    async with client_for(client_factory_sim, tmp_path) as c:
        st = await c.call("scope_status")
        full = await c.call("scope_status", detail=True)
        print(f"scope_status compact {size(st)} B, detail {size(full)} B")
        for k in ("mode", "display_mode", "run_state", "acq_state", "flags", "capture_id", "capture_origin",
                  "settings_seq", "capture_settings_seq", "ms_since_arm", "control", "ui_busy"):
            assert k in st, k
        for k in ("link_stats", "device_link_stats", "battery_level", "features", "uptime_s"):
            assert k not in st and k in full, k
        assert size(st) < 600 and size(full) > 3 * size(st)


@pytest.fixture
def client_factory_sim(sim_factory):
    return sim_factory()


# --- item 6: scope_wait_for_event --------------------------------------------------------------------------------


async def test_wait_for_event_catches_a_later_edge(sim_factory, tmp_path):
    srv = sim_factory(signals=(Signal("zero"), Signal("dc", {"v": 18.0})))
    async with client_for(srv, tmp_path, event_poll_s=0.2) as c:
        await setup_decay(c)
        progress = []

        async def on_progress(done, total, message):
            progress.append((done, total, message))

        def person():
            time.sleep(0.8)
            srv.device.event(1, DECAY)
        th = threading.Thread(target=person)
        th.start()
        res = await c.session.call_tool("scope_wait_for_event", {
            "channel": "CH2", "edge": "falling", "level": TRIG_V, "pre": "20us", "post": "600us", "timeout_s": 30,
            "levels": LEVELS, "smooth": 5}, progress_callback=on_progress)
        th.join()
        assert not res.isError, res.content
        r = res.structuredContent
        assert r["ok"] and r["trigger"] == "fired" and 0.3 < r["waited_s"] < 10
        assert "preview" not in r and "samples" not in r  # preview_points 0 by default
        assert r["n_samples"] == 6200 and r["applied"]["pretrigger_samples"] == 200
        assert r["crossings"]["channel"] == "CH2"
        check_crossings(r["crossings"])
        assert len(progress) >= 3 and progress[-1][1] == 30 and "waiting" in progress[-1][2]
        st = await c.call("scope_status")
        assert st["run_state"] == "STOP" and st["capture_origin"] == "host"
        assert np.load(r["file"])["CH2_volts"].size == 6200


async def test_wait_for_event_timeout_leaves_stop(sim_factory, tmp_path):
    srv = sim_factory(signals=(Signal("zero"), Signal("dc", {"v": 18.0})))
    async with client_for(srv, tmp_path, event_poll_s=0.2) as c:
        await setup_decay(c)
        r = await c.call("scope_wait_for_event", channel="CH2", edge="falling", level=TRIG_V, timeout_s=0.7)
        assert not r["ok"] and r["outcome"] == "timeout" and r["trigger"] == "none"
        assert r["run_state"] == "STOP" and r["acq_state"] == "ABORTED" and "STOP" in r["caveats"][-1]
        st = await c.call("scope_status")
        assert st["run_state"] == "STOP" and st["acq_state"] == "ABORTED"
        cfg = await c.call("scope_get_config")
        assert cfg["trigger"]["mode"] == "normal" and cfg["trigger"]["source"] == "CH2"
        assert "more than the 16382" in await c.fails("scope_wait_for_event", channel="CH2", pre="1ms", post="2ms")
        assert "outside CH2's range" in await c.fails("scope_wait_for_event", channel="CH2", level=100)


async def test_wait_for_event_keeps_a_capture_done_at_the_timeout(sim_factory, tmp_path):
    """The event arrives between the last WAIT and the STOP: the firmware marks the host single ABORTED, but the
    capture is in the ring with a new capture_id, so it is read and returned."""
    srv = sim_factory(signals=(Signal("zero"), Signal("dc", {"v": 18.0})))
    async with client_for(srv, tmp_path, event_poll_s=0.2) as c:
        await setup_decay(c)
        s = c.sess.get_scope()
        orig = s.stop

        def stop_after_the_event():
            srv.device.event(1, DECAY)  # completes at once (time_scale 0)
            orig()
            with srv.device.lock:
                srv.device.acq_state = P.AcqState.ABORTED  # as the firmware's h_stop does
            return s.status()
        s.stop = stop_after_the_event  # type: ignore[method-assign]
        r = await c.call("scope_wait_for_event", channel="CH2", edge="falling", level=TRIG_V, timeout_s=0.5,
                         levels=[8.0])
        assert r["ok"] and r["trigger"] == "fired"
        assert any("read after the STOP" in x for x in r["caveats"])
        assert abs(r["crossings"]["levels"][0]["first"] - TAU * math.log(TRIG_V / 8.0)) < 1.5e-6
