"""The host-side findings of the 2026-11-06 MCP battery on the unit (docs/hardware-status.md 16:21-16:45), each against
the simulator.

Finding 3: scope_capture_and_fft / scope_dvm labelled a 200 kSa/s record 1 MSa/s. fw 0.2.105's GET_TIMEBASE reports a
host's raw divider only until the UI's time/div moves; a SET_TRIGGER total that the UI's time/div does not fit at the
UI's own rate snaps it (acq_view_fit), the session forgets the raw divider and GET_TIMEBASE reports the UI table's
divider while the FPGA keeps the raw one (``SimConfig.tb_raw_snap``). The capture context (the path FFT and DVM use)
took its dt from GET_TIMEBASE.
"""

from __future__ import annotations

import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P

from test_server import client_for

pytestmark = pytest.mark.anyio


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _ui_at_1msps(srv) -> None:
    """The scope's own settings at 1 MSa/s (UI index 21: 100 us/div at 1 MSa/s), as on the unit before the battery."""
    srv.device.ui_set_timebase(21)
    assert srv.device.timebase().divider == 199


async def _battery_sequence(c) -> None:
    """The battery's acq group up to its first FFT: a raw 200 kSa/s, a segment run's short record and back."""
    await c.call("scope_set_timebase", sample_rate="200kS/s")
    await c.call("scope_set_trigger", source="CH1", edge="rising", mode="normal", level=0.0, pretrigger_fraction=0.5,
                 total_samples=1000)
    await c.call("scope_set_trigger", source="CH1", edge="rising", mode="normal", level=0.0, pretrigger_fraction=0.5,
                 total_samples=3000)


async def test_raw_divider_forgotten_by_get_timebase_is_modelled(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=1kHz,amp=1", tb_raw_snap=True)
    _ui_at_1msps(srv)
    async with client_for(srv, tmp_path) as c:
        await _battery_sequence(c)
        dev = srv.device
        assert dev.settings.divider == 999  # the FPGA keeps the raw divider
        assert dev.timebase().divider == 199  # GET_TIMEBASE: the UI table's, as fw 0.2.105
        assert dev.decim_state().divider == 999  # GET_DECIM: the one the FPGA holds


async def test_fft_and_dvm_use_the_divider_the_fpga_holds(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=1kHz,amp=1", tb_raw_snap=True)
    _ui_at_1msps(srv)
    async with client_for(srv, tmp_path) as c:
        await _battery_sequence(c)
        fft = await c.call("scope_capture_and_fft", channel="CH1", n_avg=2, peaks=3)
        assert fft["sample_rate"] == pytest.approx(200e3)
        assert fft["peaks"][0]["frequency"] == pytest.approx(1000, rel=0.01)
        dvm = await c.call("scope_dvm", channels=["CH1"], n=2)
        assert dvm["channels"]["CH1"]["frequency"] == pytest.approx(1000, rel=0.01)
        assert any("over 15 ms" in x for x in dvm["caveats"]), dvm["caveats"]
        cap = await c.call("scope_capture", channels=["CH1"], timeout_s=2.0)
        assert cap["sample_rate"] == pytest.approx(200e3)


async def test_timebase_reports_follow_the_fpga(sim_factory, tmp_path):
    """The tools that report the time base (the trigger's pre / post times, scope_get_config) read the held divider."""
    srv = sim_factory(signal="square:freq=1kHz,amp=1", tb_raw_snap=True)
    _ui_at_1msps(srv)
    async with client_for(srv, tmp_path) as c:
        await _battery_sequence(c)
        tr = await c.call("scope_set_trigger", total_samples=3000)
        assert tr["applied"]["pre_trigger_time"] == pytest.approx(1500 * 5e-6)
        s = c.sess.get_scope()
        assert s.get_timebase(cached=False).divider == 999
        assert s.capture_context().timebase.dt == pytest.approx(5e-6)
        assert P.timebase_for_divider(999).dt == pytest.approx(5e-6)


# ---- finding 4a: a frequency for a trace without a period ------------------------------------------------------


def test_no_frequency_from_noise_and_slope():
    """R0b's CH2 at 200 MSa/s: 3000 samples of 1.46-code noise on a 6-code slope (11 codes peak to peak) read 39 MHz
    with duty 1.0; now no frequency, with the reason."""
    rng = np.random.default_rng(7)
    lsb = 0.0349
    v = (np.linspace(0, 6, 3000) + rng.normal(0, 1.46, 3000)).round() * lsb - 1.15
    fr = A.frequency(v, 5e-9)
    assert fr["frequency"] is None and fr["duty"] is None
    assert "noise" in fr["reason"]
    # a real square of the same record length with the same noise still reads
    t = np.arange(3000) * 5e-9
    sq = np.where(np.mod(t * 1e6, 1.0) < 0.5, 40.0, 0.0) + rng.normal(0, 1.46, 3000)
    fr = A.frequency(sq.round() * lsb, 5e-9)
    assert fr["frequency"] == pytest.approx(1e6, rel=1e-3) and fr.get("reason") is None


def test_no_frequency_from_irregular_crossings():
    rng = np.random.default_rng(3)
    edges = np.cumsum(rng.uniform(5, 60, 80)).astype(int)  # a random telegraph: big swing, no period
    v = np.zeros(edges[-1] + 10)
    for i, e in enumerate(edges[:-1]):
        v[e:edges[i + 1]] = i % 2
    fr = A.frequency(v + rng.normal(0, 0.01, v.size), 1e-6)
    assert fr["frequency"] is None and "irregular" in fr["reason"]


async def test_capture_summary_gives_no_frequency_without_a_period(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=1kHz,amp=1")
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_timebase", sample_rate="200MS/s")
        await c.call("scope_set_trigger", source="CH1", edge="rising", mode="auto", level=0.0, total_samples=3000)
        cap = await c.call("scope_capture", channels=["CH1"], timeout_s=2.0)
        ch = cap["channels"]["CH1"]
        assert ch["frequency"] is None and ch["frequency_reason"]
        m = await c.call("scope_measure", items=["frequency", "duty"], channels=["CH1"])
        assert m["channels"]["CH1"]["frequency"] is None and m["channels"]["CH1"]["frequency_reason"]


# ---- finding 4e: the counter's first reading after a configuration ---------------------------------------------


async def test_counter_reading_after_a_source_change_is_a_fresh_gate(sim_factory, tmp_path):
    """G3 / G5: the configure's answer is the gate held before it (the old source); the tool waits for a gate
    counted wholly after the configuration."""
    from fnirsi1013d.simulator import parse_signal

    srv = sim_factory(signals=(parse_signal("sine:freq=10kHz,amp=1"),
                               parse_signal("square:freq=1kHz,amp=1.5,offset=1.5")), counter_stale_config=True)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_timebase", sample_rate="200MS/s")
        r = await c.call("scope_counter", source="CH1", gate="10ms", readings=2)
        assert r["frequency_hz"] == pytest.approx(10e3, rel=1e-3)
        # pinned thresholds: no auto retune, so the only stale gate is the configure's own answer (CH1's)
        r = await c.call("scope_counter", source="CH2", level=0.75, hysteresis=6, readings=1)
        assert r["source"] == "CH2"
        assert r["frequency_hz"] == pytest.approx(1e3, rel=1e-3), r["readings"]
        assert any("before the configuration took effect" in x for x in r["caveats"]), r["caveats"]


# ---- finding 4c: the loopback self-test's trigger level ---------------------------------------------------------


async def _self_test(c):
    tok = (await c.call("scope_self_test_loopback"))["token"]
    return await c.call("scope_self_test_loopback", confirm_token=tok)


async def test_self_test_triggers_on_the_post_seen_against_the_probe_ground(sim_factory, tmp_path):
    """The post's square reads -1.24 .. +1.86 V against the probe ground (the battery's 16:4x run triggered at
    +1.65 V, near the top): the level comes from the measured swing."""
    srv = sim_factory(signal="gen:high=3.1,offset=-1.24", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        r = await _self_test(c)
        caps = [x for x in r["checks"] if x["check"].startswith("capture")]
        assert not caps, caps  # every capture triggered
        assert all(x["pass"] for x in r["checks"] if x["check"].startswith("frequency")), r["checks"]


async def test_self_test_without_the_post_says_how_to_wire_it(sim_factory, tmp_path):
    srv = sim_factory(signal="zero", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        r = await _self_test(c)
        assert not r["passed"]
        bad = [x for x in r["checks"] if not x["pass"] and "calibration post directly" in x.get("reason", "")]
        assert bad, r["checks"]


# ---- finding 4b: scope_sweep's pace ------------------------------------------------------------------------------


def test_sweep_needs_no_poll_when_set_gen_says_taken(sim):
    """G10: 199 GET_GEN polls for 200 steps (each a main-loop pass on the unit). SET_GEN's answer carries the status
    read after the commit; a commit already taken needs no poll."""
    from fnirsi1013d.scope import Scope
    from fnirsi1013d.transport import connect

    s = Scope(connect(sim.url))
    try:
        s.set_gen(enable=True, freq_hz=100.0, duty=0.5)
        st = s.sweep_gen(np.geomspace(100, 200, 20).tolist(), 1e-4)  # steps far sooner than a 10 ms period
        assert st["steps"] == 20
        assert st["pending_polls"] == 0, st
    finally:
        s.close()


def test_simulator_clock_resolves_the_generator_commit():
    """The commit is pending for 15 ns. time.monotonic on Windows before Python 3.13 ticks every 15.6 ms, so a SET_GEN
    answer read pending there (CI, windows-latest py3.12, 2026-11-07): the simulator's default clock is perf_counter."""
    import inspect
    import time

    from fnirsi1013d import simulator

    cls = next(c for _, c in inspect.getmembers(simulator, inspect.isclass)
               if c.__module__ == simulator.__name__ and "clock" in inspect.signature(c.__init__).parameters)
    assert inspect.signature(cls.__init__).parameters["clock"].default is time.perf_counter
    assert time.get_clock_info("perf_counter").resolution < 1e-6
