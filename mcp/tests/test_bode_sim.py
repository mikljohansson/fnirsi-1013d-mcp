"""Bode sweep against the simulator: the DUT model (RC / series RLC on the generator) and, from H2 on, the sweep
runner end to end."""

from __future__ import annotations

import dataclasses
import json
import math

import numpy as np
import pytest

from fnirsi1013d.simulator import GenState, dut_response, parse_signal
from fnirsi1013d import protocol as P

RC = (1000.0, 1.6e3, 0.0, 10e-9)  # rs, r, l, c
RLC = (1000.0, 30.0, 1e-3, 10e-9)


def _h(f: float, rs_r_l_c=RC) -> complex:
    _, r, ll, c = rs_r_l_c
    w = 2j * math.pi * f
    return 1 / (1 + w * r * c + w * w * ll * c)


def _fund(x: np.ndarray, f: float, fs: float) -> complex:
    n = np.arange(x.size)
    return complex(2 / x.size * np.dot(x - x.mean(), np.exp(-2j * math.pi * f / fs * n)))


@pytest.mark.parametrize("net", [RC, RLC])
@pytest.mark.parametrize("f", [1e3, 10e3, 50.3e3, 300e3])
def test_dut_response_matches_the_analytic_ratio(net, f):
    fs = 20000 * f  # 20000 samples per period, 2 periods
    t = np.arange(40000) / fs + 3.21e-3
    v1 = dut_response(t, f, 0.5, 3.1, *net, node=0, bw=0)
    v2 = dut_response(t, f, 0.5, 3.1, *net, node=1, bw=0)
    h = _fund(v2, f, fs) / _fund(v1, f, fs)
    # bare edges: the sampled step limits it (1 / 20000 of a period, x 30 at the RLC's resonance where the input
    # node's fundamental is small next to its edges)
    assert abs(h / _h(f, net) - 1) < 1e-2
    # the front end is the same pole on both nodes: it cancels in the ratio, and with the edges resolved the ratio
    # is the analytic one
    w1 = dut_response(t, f, 0.5, 3.1, *net, node=0)
    w2 = dut_response(t, f, 0.5, 3.1, *net, node=1)
    assert abs(_fund(w2, f, fs) / _fund(w1, f, fs) / _h(f, net) - 1) < 2e-4


def test_dut_levels_steady_state_and_periodicity():
    f = 10e3
    t = np.linspace(0, 1e-4, 401) + 1.3e-7  # no sample on an edge
    a = dut_response(t, f, 0.5, 3.1, *RC, node=0, bw=0)
    b = dut_response(t + 1e-4, f, 0.5, 3.1, *RC, node=0, bw=0)
    assert np.allclose(a, b, atol=1e-9)  # periodic: the steady state, no start-up transient
    out = dut_response(t, f, 0.5, 3.1, *RC, node=1, bw=0)
    assert out.mean() == pytest.approx(1.55, abs=0.05) and out.min() > 0 and out.max() < 3.1
    # input node: the pin's step divides between R21 and the DUT (r / (rs + r) of 3.1 V jumps at an edge)
    lo, hi = dut_response(np.array([0.4999e-4, 0.5001e-4]), f, 0.5, 3.1, *RC, node=0, bw=0)
    assert lo - hi == pytest.approx(3.1 * 1.6 / 2.6, rel=0.01)
    # far below the corner the output follows the pin
    slow = dut_response(np.linspace(0, 1e-2, 1001), 100.0, 0.5, 3.1, *RC, node=1, bw=0)
    assert slow.max() == pytest.approx(3.1, abs=0.01) and slow.min() == pytest.approx(0.0, abs=0.01)


def test_dut_signals_parse_eval_and_generator_off():
    s_in = parse_signal("dut_in:r=1.6k,c=10n")
    s_out = parse_signal("dut_out:r=1.6k,c=10n,gain=0.97,delay=5ns")
    assert s_out.dut_params == RC and s_out.dut_h(10e3) == pytest.approx(_h(10e3))
    assert parse_signal("dut_out:l=1m,c=10n,r=30").dut_h(50e3) == pytest.approx(_h(50e3, RLC))
    gen = GenState(1, P.gen_p16(10_000_000), P.gen_p16(10_000_000) // 2)
    t = np.arange(20000) / 200e6
    assert np.allclose(s_in.eval(t, gen), -1.24 + dut_response(t, gen.frequency_hz, 0.5, 3.1, *RC, node=0))
    slow = GenState(1, P.gen_p16(100_000), P.gen_p16(100_000) // 2)  # 100 Hz: the nodes reach both levels
    v_in = s_in.eval(np.linspace(0, 1e-2, 2001), slow)
    assert v_in.min() == pytest.approx(-1.24, abs=0.01) and v_in.max() == pytest.approx(1.86, abs=0.01)
    assert s_in.dc == pytest.approx(0.31)
    v_out = s_out.eval(t, gen)
    ref = -1.24 + 0.97 * dut_response(t - 5e-9, gen.frequency_hz, 0.5, 3.1, *RC, node=1)
    assert np.allclose(v_out, ref)
    off = GenState(0, gen.p16, gen.h16)
    assert np.all(s_out.eval(t, off) == -1.24)  # pin tri-stated: the DUT rests at its ground
    with pytest.raises(ValueError, match="dut_in"):
        parse_signal("bode:r=1")


# --- the sweep runner (H2 / H3) ---------------------------------------------------------------------------

from fnirsi1013d import bode as B  # noqa: E402
from fnirsi1013d.scope import Scope  # noqa: E402

RC_IN, RC_OUT = "dut_in:r=1.6k,c=10n", "dut_out:r=1.6k,c=10n"
#: a 99.5 kHz corner, swept above 24 kHz where the simulator's 12.5 MSa/s captures are cheap
FAST_IN, FAST_OUT = "dut_in:r=1.6k,c=1n", "dut_out:r=1.6k,c=1n"
FAST = (1000.0, 1.6e3, 0.0, 1e-9)


def _fast(f):
    return _h(f, FAST)


def _sim(sim_factory, ch1=RC_IN, ch2=RC_OUT, **kw):
    srv = sim_factory(**kw)
    srv.device.cfg.signals = (parse_signal(ch1), parse_signal(ch2))
    return srv


def _run(s, plans, thru=None, **kw):
    kw.setdefault("sleep", lambda x: None)
    kw.setdefault("busy_retry_s", 0.0)
    return B.BodeRunner(s, **kw).run(plans, thru)


def _errors(res, expect=_h):
    out = []
    for p in res.points:
        he = expect(p.plan.f_actual)
        out.append((p.plan.f_actual, p.mag_db - B.db(abs(he)), math.degrees(np.angle(p.h / he))))
    return out


def _gen_config(g: P.Gen) -> P.Gen:
    """GET_GEN without the live 0x39 bits (the step, the gap, a commit still pending, the bursts done, the marker
    seen): they move with the wall clock, so a before / after snapshot compares the configuration only."""
    return dataclasses.replace(g, pat_status=g.pat_status & P.PAT_ST_RUNNING, bursts=0,
                               pat_status3=g.pat_status3 & P.PAT_ST3_ACTIVE)


def _state(s):
    return (s.get_channel(0, cached=False), s.get_channel(1, cached=False), s.get_timebase(cached=False),
            s.get_trigger(cached=False), _gen_config(s.get_gen()), s.get_decim().filter, s.get_decim().flags)


def test_rc_sweep_end_to_end_auto_range(sim_factory):
    """The design's bench RC (1.6 kOhm, 10 nF) through the simulator's 16-bit CIC3 + FIR path, CH2 down-ranging
    with the splice: within 0.02 dB / 0.2 deg of the analytic response to 1 MHz (bench acceptance 0.2 dB / 1 deg to
    100 kHz), markers on the analytic values, every setting given back."""
    srv = _sim(sim_factory, noise_lsb=1.0)
    with Scope.connect(srv.url) as s:
        s.set_gen(enable=True, freq_hz=1234.0, duty=0.3)
        before = _state(s)
        plans = B.plan_sweep(1e3, 1e6, 10)
        res = _run(s, plans)
        assert _state(s) == before
    assert len(res.points) == 10 and res.stopped_early is None
    for f, e_db, e_deg in _errors(res):
        assert abs(e_db) < 0.02 and abs(e_deg) < 0.2, (f, e_db, e_deg)
    assert res.range_switches >= 2 and {p.ch2_range for p in res.points} >= {3, 5}
    assert [p.ch1_range for p in res.points] == [3] * 10
    assert sum("spliced" in p.flags for p in res.points) == len(res.splices) == res.range_switches
    assert all(abs(sp["factor_db"]) < 0.02 for sp in res.splices)
    assert all(p.filter_effective == "cic3+fir" and p.fir for p in res.points)
    assert not any(set(p.flags) & B.BAD_FLAGS for p in res.points), [p.flags for p in res.points]
    mk = res.summary()
    fc = 1 / (2 * math.pi * 1.6e3 * 10e-9)
    assert mk["shape"] == "low-pass" and mk["minus_3db_hz"][0] == pytest.approx(1.0075 * fc, rel=0.01)
    assert mk["phase_crossings_hz"]["-45"] == pytest.approx(fc, rel=0.01)
    assert res.meta["start_ranges"] == {"CH1": 3, "CH2": 3}


def test_thru_calibration_removes_a_channel_mismatch(sim_factory, tmp_path):
    """CH2 reads 3 % low and 20 ns late (probe / front-end mismatch): the thru sweep (both probes on the DUT input)
    measures it and the DUT sweep divided by it is the RC again."""
    mismatch = ",gain=0.97,delay=20ns"
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_IN + mismatch, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 1e6, 6)
    with Scope.connect(srv.url) as s:
        thru_res = _run(s, plans)
        thru_res.role = "thru"
        k = 0.97 * np.exp(-2j * math.pi * np.array([p.plan.f_actual for p in thru_res.points]) * 20e-9)
        assert np.allclose([p.h for p in thru_res.points], k, rtol=2e-3, atol=0)
        cal = B.ThruCal.load(B.ThruCal.from_sweep(thru_res, "t", (1.0, 1.0)).save(tmp_path / "t.json"))
        srv.device.cfg.signals = (parse_signal(FAST_IN), parse_signal(FAST_OUT + mismatch))
        raw = _run(s, plans, background=False)
        res = _run(s, plans, thru=cal)
        other = _run(s, B.plan_sweep(31e3, 32e3, 2), thru=cal, background=False)
    assert res.thru_cal == "t"
    for f, e_db, e_deg in _errors(raw, _fast):
        assert e_db == pytest.approx(B.db(0.97), abs=0.03)
        assert e_deg == pytest.approx(-360 * f * 20e-9, abs=0.2)
    for f, e_db, e_deg in _errors(res, _fast):
        assert abs(e_db) < 0.03 and abs(e_deg) < 0.2, (f, e_db, e_deg)
    # CH2 went to more sensitive ranges on the DUT than on the thru: noted, still divided out
    assert any("cal_range" in p.flags for p in res.points) and not any("no_cal" in p.flags for p in res.points)
    assert all("no_cal" in p.flags for p in other.points)


def test_fixed_ch2_range_and_clipping(sim_factory):
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 2e6, 5)
    with Scope.connect(srv.url) as s:
        s.set_channel(1, range_index=5)  # 100 mV/div: the passband clips
        res = _run(s, plans, ch2_range="fixed", background=False)
        assert s.get_channel(1, cached=False).range == 5
        res2 = _run(s, plans, ch2_range="0.5V", background=False)
    assert res.range_switches == 0 and {p.ch2_range for p in res.points} == {5}
    assert "clipped" in res.points[0].flags and "clipped" not in res.points[-1].flags
    assert B.markers(res.points)["flagged_points"] >= 1
    assert {p.ch2_range for p in res2.points} == {3} and res2.ch2_mode == "fixed"
    assert not any("clipped" in p.flags for p in res2.points)
    n_clip = sum("clipped" in p.flags for p in res.points)
    assert any(c.startswith(f"{n_clip} point(s) clipped") for c in B.result_caveats(res))


DIV_LOW = "dut_out:r=1.6k,c=1n,gain=0.05,offset=-1.1"  # 75 mV peak about -1 V DC from the probe reference
WINDOW = (50.0, 50.0, 20.0, 5.0, 1.5, 0.2)  # the 0.1 V/div range pins beyond +-0.2 V


def test_ch2_overload_flat_record(sim_factory):
    """Bench 2026-10-19: wiring B puts the DUT about 1.2 V below the probe reference; on 0.1 V/div the stage in front of
    the offset injection pins and the ADC sees a constant the offset loop can centre. Fixed range: refused with the
    remedy; auto: stepped up and flagged; AC coupling: measured."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=DIV_LOW, noise_lsb=0.5)
    srv.device.cfg.frontend_window_v = WINDOW
    plans = B.plan_sweep(30e3, 300e3, 3)
    with Scope.connect(srv.url) as s:
        with pytest.raises(B.BodeError, match="saturated.*wiring A.*ch2_coupling='ac'"):
            _run(s, plans, ch2_range="0.1", background=False)
        ac = _run(s, plans, ch2_range="0.1", ch2_coupling="ac", background=False)
        assert {p.ch2_range for p in ac.points} == {5} and not any("ch2_overload" in p.flags for p in ac.points)
        assert any("AC coupled" in c for c in ac.caveats)
        ref = _run(s, plans, ch2_range="0.5", background=False)
        for a, r in zip(ac.points, ref.points):
            assert abs(B.db(abs(a.h)) - B.db(abs(r.h))) < 0.1
        assert s.get_channel(1, cached=False).coupling == 0  # restored
    m = B.Measurement(1, 1e-6, 0, 0, -90, None, None, None, False, False, 50, 0.01, 0, -1.0, 40, 0.01, 128, 128)
    assert m.flat2 and not B.Measurement(1, 1, 0, 0, -90, None, None, None, False, False, 50, 20, 0, 0, 40, 25,
                                         128, 128).flat2


def test_auto_range_steps_up_from_a_saturated_range(sim_factory):
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=DIV_LOW, noise_lsb=0.5)
    srv.device.cfg.frontend_window_v = WINDOW
    with Scope.connect(srv.url) as s:
        res = _run(s, B.plan_sweep(1e6, 2e6, 3), background=False)  # the output is small: auto wants 0.1 V/div
    assert all(p.ch2_range <= 4 for p in res.points)
    assert not any("ch2_overload" in p.flags for p in res.points)


def test_offset_exhausted_is_a_caveat_not_an_exception(sim_factory):
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    with Scope.connect(srv.url) as s:
        r = B.BodeRunner(s, sleep=lambda x: None)
        cfg = s.get_channel(1, cached=False)
        assert r._shift_offset(1, cfg, 1e6) is False  # far beyond the PWM: clamped
        assert any("offset range exhausted" in c for c in r.caveats)

        real = s.set_channel

        def refuse(c, **kw):
            if "dcoffset" in kw and len(kw) == 1:
                from fnirsi1013d.transport import DeviceError
                raise DeviceError(P.Cmd.SET_CHANNEL, P.Status.E_BAD_ARG, 3, "pwm out of range")
            return real(c, **kw)
        s.set_channel = refuse
        r2 = B.BodeRunner(s, sleep=lambda x: None)
        assert r2._shift_offset(0, s.get_channel(0, cached=False), 5.0) is False
        assert len([c for c in r2.caveats if "offset range exhausted" in c]) == 1


def test_floor_sweep_flags_near_floor(sim_factory):
    """CH2 on the DUT ground (here: no signal): every point sits on the background, flagged and left out of the
    markers."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2="zero", noise_lsb=1.0)
    with Scope.connect(srv.url) as s:
        res = _run(s, B.plan_sweep(30e3, 300e3, 4))
    assert all("near_floor" in p.flags or "low_snr" in p.flags for p in res.points)
    assert all(p.background_v is not None for p in res.points)
    assert res.summary()["valid_points"] == 0 and max(p.mag_db for p in res.points) < -50


def test_legacy_and_low_frequency_points(sim_factory):
    """Above 1.5 MHz the plan uses 200 MSa/s (8-bit, legacy); at 20 Hz the CIC3 limit makes the firmware run CIC2
    (N 32768). Both still measure the RC (a lighter one, 160 Ohm / 100 pF, so 2-5 MHz stays in the passband)."""
    srv = _sim(sim_factory, ch1="dut_in:r=160,c=100p", ch2="dut_out:r=160,c=100p", noise_lsb=0.5)
    with Scope.connect(srv.url) as s:
        res = _run(s, B.plan_sweep(2e6, 5e6, 2), background=False)
        lo = _run(s, [B.plan_point(20.0)], background=False)

    def rc(f):
        return 1 / (1 + 2j * math.pi * f * 160 * 100e-12)

    for f, e_db, e_deg in _errors(res, rc):
        assert abs(e_db) < 0.05 and abs(e_deg) < 0.5, (f, e_db, e_deg)
    assert {p.filter_effective for p in res.points} == {"legacy"} and {p.plan.n for p in res.points} == {1}
    assert lo.points[0].filter_effective == "cic2+fir" and lo.points[0].plan.n == 32768
    assert abs(lo.points[0].mag_db) < 0.01


def test_busy_scope_stops_with_a_partial_table(sim_factory):
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 300e3, 6)

    def progress(i, n, text):
        if i == 3:
            srv.device.ui_busy = True  # the person started the calibration wizard's measurement on the scope
            srv.device.ui_hold = P.BusyDetail.CAL

    with Scope.connect(srv.url) as s:
        res = _run(s, plans, progress=progress)
        srv.device.ui_busy = False
        srv.device.ui_hold = 0
    assert len(res.points) == 3 and "E_BUSY" in res.stopped_early and "calibration" in res.stopped_early
    assert any("could not restore" in c for c in res.caveats)


def test_menu_opened_during_a_sweep_closes(sim_factory):
    """Protocol 1.23 (fw 0.2.78): a menu the person opens during a host sweep is closed by the next request."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 300e3, 6)

    def progress(i, n, text):
        if i == 3:
            srv.device.ui_busy = True

    with Scope.connect(srv.url) as s:
        res = _run(s, plans, progress=progress)
    assert len(res.points) == 6 and not res.stopped_early
    assert srv.device.ui_yields >= 1 and not srv.device.ui_busy


def test_runner_refuses_without_the_filter_and_restores_ext(sim_factory):
    with Scope.connect(_sim(sim_factory, caps_clear=int(P.Caps.DECIM)).url) as s:  # a firmware without the cap
        with pytest.raises(B.BodeError, match="decimation filter"):
            _run(s, B.plan_sweep(1e3, 10e3, 2))
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    with Scope.connect(srv.url) as s:
        s.set_trigger(source=2)  # EXT on the calibration pin: the generator is off
        res = _run(s, B.plan_sweep(50e3, 200e3, 2), background=False)
        assert s.get_trigger(cached=False).source == 2 and not s.get_gen().enable
    assert any("EXT" in c for c in res.caveats) and len(res.points) == 2


def test_cli_plan_only_and_sweep(sim_factory, tmp_path, capsys):
    assert B.main(["--plan-only", "--start", "1kHz", "--stop", "10kHz", "--points", "3"]) == 0
    out = capsys.readouterr().out
    assert "200192" in out and "3 points, estimated" in out
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    assert B.main(["--port", srv.url, "--start", "50kHz", "--stop", "500kHz", "--points", "3"]) == 2  # no --yes
    assert B.main(["--port", srv.url, "--start", "10Hz", "--stop", "100kHz", "--points", "50",
                   "--max-duration", "5", "--yes"]) == 2
    csv_p, json_p, thru_p = tmp_path / "r.csv", tmp_path / "r.json", tmp_path / "thru.json"
    assert B.main(["--port", srv.url, "--start", "50kHz", "--stop", "500kHz", "--points", "3", "--yes",
                   "--no-background", "--thru", "measure", "--thru-out", str(thru_p)]) == 0
    assert B.ThruCal.load(thru_p).name == "thru"
    assert B.main(["--port", srv.url, "--start", "50kHz", "--stop", "500kHz", "--points", "3", "--yes",
                   "--no-background", "--thru", str(thru_p), "--csv", str(csv_p), "--json", str(json_p),
                   "--settle", "fast"]) == 0
    out = capsys.readouterr().out
    assert "csv:" in out and len(csv_p.read_text().splitlines()) == 4
    j = json.loads(json_p.read_text())
    assert j["thru_cal"] == "thru" and j["settle_periods"] == 3 and len(j["table"]) == 3


def test_resonance_steps_ch2_up_and_back_down(sim_factory):
    """A series RLC (1 mH, 10 nF, 95 Ohm) from a 10 Ohm source: Q about 3, the output about 10 dB above the input at
    50 kHz. CH2 starts on the range the 25 kHz pre-scan picks, clips towards the peak, steps up (splicing on the
    previous point), and steps down again past it; H stays on the analytic curve and the markers find f0 and Q."""
    net = "r=95,l=1m,c=10n,rs=10"
    srv = _sim(sim_factory, ch1=f"dut_in:{net}", ch2=f"dut_out:{net}", noise_lsb=0.5)
    with Scope.connect(srv.url) as s:
        res = _run(s, B.plan_sweep(25e3, 150e3, 16), background=False)
    rlc = (10.0, 95.0, 1e-3, 10e-9)
    for f, e_db, e_deg in _errors(res, lambda f: _h(f, rlc)):
        assert abs(e_db) < 0.05 and abs(e_deg) < 0.3, (f, e_db, e_deg)
    ups = [sp for sp in res.splices if sp["to_range"] < sp["from_range"]]
    downs = [sp for sp in res.splices if sp["to_range"] > sp["from_range"]]
    assert ups and downs and ups[0]["f_hz"] < downs[-1]["f_hz"]
    assert not any("clipped" in p.flags for p in res.points)  # every clipped capture was repeated
    mk = res.summary()
    f = np.geomspace(20e3, 200e3, 20001)
    mag = np.abs(_h(f, rlc))
    assert mk["shape"] == "resonant"
    assert mk["peak"]["f_hz"] == pytest.approx(f[np.argmax(mag)], rel=0.01)
    assert mk["peak"]["mag_db"] == pytest.approx(B.db(mag.max()), abs=0.1)
    band = f[mag >= mag.max() / math.sqrt(2)]
    assert mk["peak"]["q"] == pytest.approx(f[np.argmax(mag)] / (band[-1] - band[0]), rel=0.05)


def test_flat_first_record_is_retaken_not_a_traceback(sim_factory):
    """Bench 2026-10-20 11:16 (FPGA 0x153A): the first record after a configuration change was one saturated value;
    measure_pair divided by CH1's zero tone. The runner retakes a flat record once: the pre-scan ranges as usual and
    the sweep matches the analytic RC."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 1e6, 5)
    with Scope.connect(srv.url) as s:
        clean = _run(s, plans, background=False)
        srv.device.flat_next = 1  # the pre-scan's first capture
        res = _run(s, plans, background=False)
    assert res.meta["flat_retakes"] == 1 and srv.device.flat_next == 0
    assert res.meta["start_ranges"] == clean.meta["start_ranges"]
    for f, e_db, e_deg in _errors(res, _fast):
        assert abs(e_db) < 0.03 and abs(e_deg) < 0.2, (f, e_db, e_deg)
    assert not any(set(p.flags) & B.BAD_FLAGS for p in res.points)
    assert not any("flat" in c for c in res.caveats)


def test_flat_record_twice_flags_the_point(sim_factory):
    """A point whose record stays flat after the retake: ch1_low + incoherent, no H (None in the table), a caveat
    naming it, and the rest of the sweep measured."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 1e6, 5)
    done = []
    with Scope.connect(srv.url) as s:
        clean = _run(s, plans, background=False)

        def hook(dev):  # the last point's capture and its retake are flat
            if dev.settings.gen.p16 == plans[-1].period << 16 and not done:
                done.append(1)
                dev.flat_next = 2
        srv.device.on_arm = hook
        res = _run(s, plans, background=False)
    assert len(res.points) == len(plans) and res.stopped_early is None
    bad = res.points[-1]
    assert {"ch1_low", "incoherent"} <= set(bad.flags) and not bad.valid
    assert math.isnan(bad.mag_db) and bad.row()["mag_db"] is None and bad.row()["phase_deg"] is None
    assert bad.retries >= 1
    assert any("flat or had no tone even after a retake at 1 frequency" in c for c in res.caveats)
    assert any(c.startswith("1 point(s) ch1_low") for c in B.result_caveats(res))
    for p, q in zip(res.points[:-1], clean.points[:-1]):
        assert abs(p.mag_db - q.mag_db) < 0.03
    json.dumps(res.to_json())  # the table serialises (None, not NaN)
    B._fmt_row(bad.row())


def test_measure_pair_never_divides_by_a_flat_ch1():
    pp = B.plan_point(100e3)
    L = pp.record
    flat = np.full(L, 2.5)
    t = np.arange(L) / pp.sample_rate
    v2 = 0.1 * np.cos(2 * math.pi * pp.f_actual * t)
    ms = B.measure_pair(flat, v2, pp, vpl1=0.1, vpl2=0.004)
    assert ms.flat1 and ms.ch1_dead and ms.suspect and math.isnan(ms.h.real)
    assert ms.half_rel is None
    ok = B.measure_pair(2.5 + 0.5 * np.cos(2 * math.pi * pp.f_actual * t), v2, pp, vpl1=0.01, vpl2=0.004)
    assert not ok.suspect and abs(ok.h) == pytest.approx(0.2, rel=1e-6)


def test_prescan_flat_twice_is_a_caveat(sim_factory):
    """The pre-scan's record flat twice: CH1 stays on the pre-scan range with a caveat; the sweep still completes."""
    srv = _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, noise_lsb=0.5)
    plans = B.plan_sweep(30e3, 300e3, 3)
    with Scope.connect(srv.url) as s:
        srv.device.flat_next = 2
        res = _run(s, plans, background=False)
    assert res.meta["start_ranges"]["CH1"] == 1
    assert any("pre-scan's CH1 record" in c for c in res.caveats)
    assert len(res.points) == 3 and all(math.isfinite(p.mag_db) for p in res.points)
