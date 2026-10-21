"""Bode sweep, H1: the frequency planner, the single-bin DFT, the quality numbers, markers, thru calibration and the
result shapes (pure functions, no device)."""

from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
import pytest

from fnirsi1013d import bode as B
from fnirsi1013d import protocol as P

VECTORS = Path(__file__).resolve().parents[2] / "docs" / "bode-plan-vectors.json"


# --- planner ----------------------------------------------------------------------------------------------


@pytest.mark.parametrize("f,period,n,record", [
    # the design's examples on the DDS (M) map exactly with P = 4 M (contract 7.4)
    (1e3, 4 * 50048, 512, 3128),  # design 3.2 worked example: 999.0 Hz, S = 391, 8 periods
    (10.0, 4 * 4997120, 65536, 2440),  # design 3.4 table: N 65,536, L 2440
    (100e3, 2000, 16, 2250),
    (1e6, 200, 16, 2050),
    (2e6, 100, 1, 2200),
    (1.01e6, 198, 16, 2178),  # the finer grid: P 198 was not reachable with M (4 M = 196, 200)
])
def test_plan_design_examples(f, period, n, record):
    pp = B.plan_point(f)
    assert (pp.period, pp.n, pp.record) == (period, n, record)


def test_plan_clean_edges_and_actual_frequency():
    for f in np.geomspace(1.0, 10e6, 300):
        pp = B.plan_point(float(f))
        # an integer period of 5 ns clocks: f = 200 MHz / P exactly (protocol 1.27)
        assert pp.f_actual == 200e6 / pp.period
        assert P.gen_period_p16(pp.period) == (pp.period << 16, ((pp.period + 1) // 2) << 16)
        # within 0.1 % of the request below 24 kHz (S >= 257, P a multiple of N) or the nearest integer P above
        # (and the even P of N = 16 above 4095 clocks)
        assert abs(pp.f_actual / f - 1) < (0.0021 if pp.n > 16 else 1.01 / pp.period * (2 if pp.period > 4095 else 1))


def test_plan_rate_rules():
    for f in np.geomspace(1.0, 10e6, 400):
        pp = B.plan_point(float(f))
        spp = pp.period / pp.n
        if pp.f_actual > B.LEGACY_ABOVE_HZ:
            assert pp.n == 1 and pp.divider == 0 and not pp.fir_expected
            continue
        k = int(round(math.log2(pp.n / 16)))
        assert pp.n == 16 * 2 ** k and pp.divider == pp.n - 1
        assert P.decim_n(pp.divider) == pp.n  # the filter's N = 2 (floor(D / 2) + 1) for the odd divider
        assert 8 <= spp <= 512  # inside the FIR's flat band (fo / 4) and at most 512 samples per period
        if pp.n > 16:  # below 24 kHz: S = P / N an integer 256..512, P a multiple of N
            assert pp.q == 1 and 256 <= pp.p <= 512 and pp.period % pp.n == 0
        else:
            assert pp.period <= 512 * 16
            assert pp.period <= B.MAX_RECORD // 2 or pp.period % 2 == 0  # above 4095: even, two blocks fit
        # N is the smallest ladder step with P0 / N <= 512
        p0 = round(200e6 / f)
        assert p0 <= 512 * pp.n and (pp.n == 16 or p0 > 512 * pp.n // 2)


def test_plan_fir_eligibility_and_filter_fallback():
    """Every filtered point is FIR-legal (N >= 16, N % 4 == 0); the firmware keeps CIC3 while N / 2 <= 10334 and
    drops to CIC2 below about 24 Hz (decim_config, the pc_decim.c mirror)."""
    seen = set()
    for f in (1.0, 5.0, 10.0, 23.0, 25.0, 100.0, 1e3, 24e3, 100e3, 1e6):
        pp = B.plan_point(f)
        assert pp.fir_expected
        c = P.decim_config(3, P.DECIM_SET_FIR, pp.divider, pp.total_samples, P.DECIM_HAS_FIR)
        assert c.fir and c.n == pp.n
        assert c.k == (3 if pp.n // 2 <= P.DECIM_CIC3_MAX_N else 2)
        seen.add(c.k)
        c0 = P.decim_config(3, 0, pp.divider, pp.total_samples, P.DECIM_HAS_CIC)  # 0x1534: CIC3 without the FIR
        assert not c0.fir
    assert seen == {2, 3}
    pp = B.plan_point(5e6)
    assert P.decim_config(3, P.DECIM_SET_FIR, pp.divider, pp.total_samples, P.DECIM_HAS_FIR).k == 0  # legacy


def test_plan_records_are_coherent():
    for f in np.geomspace(1.0, 10e6, 300):
        pp = B.plan_point(float(f))
        assert pp.record == pp.blocks * pp.p
        assert pp.periods == pp.blocks * pp.q >= B.MIN_PERIODS
        assert B.MIN_RECORD <= pp.record <= B.MAX_RECORD
        assert pp.total_samples % 2 == 0 and pp.total_samples <= P.DECIM_MAX_TOTAL
        assert pp.record * pp.n == pp.periods * pp.period  # exactly C periods of P in L samples
        assert pp.coherence_error < 1e-6 and pp.window == "rect"
        assert pp.halves or (pp.blocks + 1) * pp.p > B.MAX_RECORD  # even block counts wherever they fit


def test_plan_sweep_order_merge_determinism():
    a = B.plan_sweep(1e6, 100.0, 50)
    assert a == B.plan_sweep(100.0, 1e6, 50)  # either order, measured low to high, deterministic
    assert [p.f_actual for p in a] == sorted(p.f_actual for p in a)
    hi = B.plan_sweep(2e6, 10e6, 100)  # 1 % .. 5 % steps: requests that round to the same P merge
    assert len({p.period for p in hi}) == len(hi) < 100
    # the finer grid (contract 7.4): a 100-per-decade sweep no longer merges points below 2 MHz
    assert len(B.plan_sweep(100e3, 1e6, 100)) == 100
    assert len(B.plan_sweep(100.0, 1e6, 50)) == 50
    with pytest.raises(ValueError):
        B.plan_point(20e6)
    with pytest.raises(ValueError):
        B.plan_point(0.5)


def test_settle_and_duration():
    assert B.parse_settle("normal") == 10 and B.parse_settle("High-Q") == 200 and B.parse_settle(7) == 7
    with pytest.raises(ValueError):
        B.parse_settle("slow")
    pp = B.plan_point(1e3)
    assert pp.settle_s(10) == pytest.approx(10 / pp.f_actual)
    assert B.plan_point(1e6).settle_s(10) == B.MIN_SETTLE_S
    assert pp.filter_settle_s() == pytest.approx(13 * 512 * 5e-9 + 1e-6)
    fast = B.estimate_duration(B.plan_sweep(1e3, 1e6, 50), 10)
    slow = B.estimate_duration(B.plan_sweep(10.0, 1e5, 50), 10)
    assert 2 < fast < 8 and 10 < slow < 30  # design 3.7: about 2 s and 12 s plus background captures


def test_plan_vectors_file_matches_the_planner():
    """docs/bode-plan-vectors.json: the plan the on-device engine (design step D1) must reproduce."""
    d = json.loads(VECTORS.read_text(encoding="utf-8"))
    assert d["format"] == "fnirsi1013d-bode-plan" and d == B.plan_vectors()


# --- DFT ----------------------------------------------------------------------------------------------------


def _square_record(pp: B.PointPlan, h_of_n, amp: float = 1.0, phase0: float = 0.3, n_harm: int = 99,
                   f_scale: float = 1.0) -> np.ndarray:
    """A square's odd harmonics through H(n f) sampled at the plan's rate (Fourier synthesis, band-limited)."""
    fs, f = pp.sample_rate, pp.f_actual * f_scale
    t = np.arange(pp.record) / fs
    x = np.zeros(pp.record)
    for n in range(1, n_harm + 1, 2):
        if n * f >= fs / 2:
            break
        h = h_of_n(n * f)
        x += amp * 4 / math.pi / n * abs(h) * np.cos(2 * math.pi * n * f * t + n * phase0 + np.angle(h))
    return x


def test_tone_exact_on_a_coherent_square():
    pp = B.plan_point(1e3)
    x = _square_record(pp, lambda f: 1.0)
    X = B.tone(x + 0.7, pp.f_actual, pp.sample_rate)  # DC removed, harmonics on other bins
    assert pp.window == "rect" and pp.coherence_error < 1e-4
    assert abs(X) == pytest.approx(4 / math.pi, rel=1e-5)
    assert np.angle(X) == pytest.approx(0.3, abs=1e-5)


def test_tone_low_frequency_is_coherent_and_hann_still_works():
    """Protocol 1.27: the integer-period stimulus makes every record coherent, down to 1 Hz (the DDS's increment
    rounding needed a Hann window below about 50 Hz). The Hann path stays for an off-grid tone: a square 0.3 % off
    the plan's frequency leaks into the rectangular DFT, not into the Hann one."""
    assert all(B.plan_point(float(f)).window == "rect" for f in np.geomspace(1.0, 1e7, 300))
    pp = B.plan_point(1.7)
    x = _square_record(pp, lambda f: 1.0, f_scale=1.003)
    rect = abs(B.tone(x, pp.f_actual * 1.003, pp.sample_rate)) / (4 / math.pi) - 1
    hann = abs(B.tone(x, pp.f_actual * 1.003, pp.sample_rate, "hann")) / (4 / math.pi) - 1
    assert abs(hann) < 1e-4 < 1e-3 < abs(rect)


def test_residual_noise_is_the_bin_noise():
    rng = np.random.default_rng(1)
    pp = B.plan_point(1e3)
    x = _square_record(pp, lambda f: 1.0) + rng.normal(0, 0.1, pp.record)
    est = B.residual_noise(x, pp.periods)
    assert est == pytest.approx(2 * 0.1 / math.sqrt(pp.record), rel=0.15)


def _rc(f, rc=1.6e3 * 10e-9):
    return 1 / (1 + 2j * math.pi * f * rc)


def _adc16(v: np.ndarray, vpl: float, n: int, rng) -> np.ndarray:
    """The 16-bit path in miniature: 8-bit codes of the raw 200 MSa/s samples with 1 code rms of noise (the analog
    noise is the only dither), averaged over N (a boxcar; the CIC's droop and delay are the same on both channels),
    back to volts."""
    raw = np.clip(np.rint(128 + v / vpl + rng.normal(0, 1.0, v.size)), 0, 255)
    return (raw.reshape(-1, n).mean(axis=1) - 128) * vpl


def test_synthetic_rc_with_8bit_quantisation_and_noise():
    """H1 acceptance: < 0.01 dB / 0.05 deg on a synthetic RC (the exact network response, input node and output
    node, at 200 MSa/s) with 8-bit quantisation and noise where the response is above -12 dB, and < 0.05 dB /
    0.3 deg at -20 dB with CH2 one range more sensitive."""
    from fnirsi1013d.simulator import dut_response

    rng = np.random.default_rng(7)
    vpl = 3.1 / 180  # range 3: the 3.1 V square spans about 180 codes
    for f in (1e3, 3e3, 10e3, 30e3, 100e3):
        pp = B.plan_point(f)
        t = (np.arange(pp.record * pp.n) + 0.37) / 200e6 + 1.234e-4
        net = (pp.f_actual, 0.5, 3.1, 1000.0, 1.6e3, 0.0, 10e-9)
        v1 = dut_response(t, *net, node=0) - 1.55  # centred, as the offset does
        v2 = dut_response(t, *net, node=1) - 1.55
        g2 = 2.5 if abs(_rc(pp.f_actual)) < 0.25 else 1.0  # CH2 down-ranged once at the -20 dB point
        ms = B.measure_pair(_adc16(v1, vpl, pp.n, rng), _adc16(v2, vpl / g2, pp.n, rng), pp, vpl1=vpl,
                            vpl2=vpl / g2)
        he = _rc(pp.f_actual)
        e_db = B.db(abs(ms.h)) - B.db(abs(he))
        e_deg = math.degrees(np.angle(ms.h / he))
        lim = (0.01, 0.05) if abs(he) > 0.25 else (0.05, 0.3)
        assert abs(e_db) < lim[0] and abs(e_deg) < lim[1], (f, e_db, e_deg)
        assert not ms.unsettled and ms.leak_db < B.LEAK_LIMIT_DB


def test_synthetic_rlc_peak_and_phase():
    ll, c, r = 1e-3, 10e-9, 30.0  # series RLC, output across C: f0 50.3 kHz, Q = sqrt(L / C) / (R + 1 k) ~ 0.31

    def h(f):
        w = 2j * math.pi * f
        return 1 / (1 + w * r * c + w * w * ll * c)

    for f in (20e3, 50e3, 80e3):
        pp = B.plan_point(f)
        x1 = _square_record(pp, lambda f: 1.0)
        x2 = _square_record(pp, h)
        ms = B.measure_pair(x1, x2, pp, vpl1=0.01, vpl2=0.01)
        assert abs(ms.h / h(pp.f_actual) - 1) < 1e-6


def test_quality_numbers():
    pp = B.plan_point(10e3)
    x1 = _square_record(pp, lambda f: 1.0)
    x2 = _square_record(pp, _rc)
    ok = B.measure_pair(x1, x2, pp, vpl1=0.01, vpl2=0.01)
    assert ok.leak_db < -100 and not ok.unsettled and ok.half_rel < 1e-9 and ok.codes1 == pytest.approx(127.3, 0.01)
    # a stimulus not locked to the sample clock: energy beside the fundamental
    bad = B.measure_pair(_square_record(pp, lambda f: 1.0, f_scale=1.002), _square_record(pp, _rc, f_scale=1.002),
                         pp, vpl1=0.01, vpl2=0.01)
    assert bad.leak_db > B.LEAK_LIMIT_DB
    # the DUT still settling: the output grows during the record
    ramp = x2 * np.linspace(0.97, 1.0, x2.size)
    un = B.measure_pair(x1, ramp, pp, vpl1=0.01, vpl2=0.01)
    assert un.unsettled and un.half_db > B.SETTLE_DB
    # clipping and code levels
    cl = np.zeros(x1.size, bool)
    cl[5] = True
    m = B.measure_pair(x1, x2, pp, vpl1=0.1, vpl2=0.01, clipped2=cl)
    assert m.clipped2 and not m.clipped1 and m.codes1 == pytest.approx(12.73, 0.01)
    with pytest.raises(B.BodeError):
        B.measure_pair(x1[:100], x2[:100], pp, vpl1=0.01, vpl2=0.01)


# --- markers, results, thru calibration -------------------------------------------------------------------


def _result(f: float, h: complex, flags=()) -> B.PointResult:
    pp = B.plan_point(f)
    return B.PointResult(pp, h, h, 1 + 0j, h, 3, 3, 0.5, 0.5, 60.0, -90.0, 100.0, 100 * abs(h), "cic3+fir", True,
                         list(flags))


def test_markers_low_pass_and_flags():
    fc = 1 / (2 * math.pi * 1.6e3 * 10e-9)
    pts = [_result(f, _rc(B.plan_point(f).f_actual)) for f in np.geomspace(1e3, 1e6, 50)]
    mk = B.markers(pts)
    assert mk["shape"] == "low-pass" and mk["valid_points"] == 50
    # 3 dB below the 1 kHz reference (-0.044 dB): |H| = -3.044 dB at 1.0075 fc
    assert mk["minus_3db_hz"][0] == pytest.approx(fc * 1.0075, rel=1e-3)
    assert mk["phase_crossings_hz"]["-45"] == pytest.approx(fc, rel=1e-3)
    pts[20].flags.append("clipped")
    assert B.markers(pts)["flagged_points"] == 1


def test_ch2_small_is_grey_and_the_floor_caveat():
    """fw 0.2.99 (owner 2026-10-20 16:22): ch2_small points do not count, and a sweep with them on CH2's finest range
    says what to do, in the scope's words (bench: a two-pole RC 10 kHz .. 10 MHz, CH2 under one code from 540 kHz)."""
    assert "ch2_small" in B.BAD_FLAGS and "ch1_low" not in B.BAD_FLAGS and "spliced" not in B.BAD_FLAGS

    def rc2(f):
        return _rc(f) ** 2

    fs = np.geomspace(10e3, 10e6, 12)
    pts = [_result(f, rc2(B.plan_point(f).f_actual)) for f in fs]
    for p in pts:
        p.ch2_range = 5
    assert B.ch2_floor_hz(pts) is None
    assert not any(c.startswith("CH2 too small") for c in B.result_caveats(B.SweepResult(pts, 10.0, "auto")))
    # the tail from point 6 (about 433 kHz) up: generator pickup climbing back, under 3 codes
    for k, p in enumerate(pts[6:]):
        p.flags.append("ch2_small")
        p.h = p.h_raw = 10 ** ((-69 + 7 * k) / 20)
    pts[6].flags.append("low_snr")
    assert not any(p.valid for p in pts[6:]) and all(p.valid for p in pts[:6])
    assert B.markers(pts)["valid_points"] == 6 and B.markers(pts)["flagged_points"] == 6
    f6 = pts[6].plan.f_actual
    assert B.ch2_floor_hz(pts) == f6
    cav = B.result_caveats(B.SweepResult(pts, 10.0, "auto"))
    assert cav[0] == (f"CH2 too small above {B.fmt_f3(f6)} to measure: set Stop below it or press AUTO SET. "
                      "Less pickup: short ground lead on CH2.")
    assert cav[0].startswith("CH2 too small above 43") and any(c.startswith("6 point(s) ch2_small") for c in cav)
    assert B.ch2_floor_text(540e3).startswith("CH2 too small above 540 kHz to measure:")
    # any order; a measured point above the run moves it up; scattered: the lowest
    assert B.ch2_floor_hz(pts[::-1]) == f6
    pts[10].flags.remove("ch2_small")
    assert B.ch2_floor_hz(pts) == pts[11].plan.f_actual
    pts[11].flags.remove("ch2_small")
    assert B.ch2_floor_hz(pts) == f6
    # on a coarser range a finer one helps: not this caveat (the per-flag one stays)
    for p in pts:
        p.ch2_range = 4
    cav = B.result_caveats(B.SweepResult(pts, 10.0, "auto"))
    assert B.ch2_floor_hz(pts) is None and not any(c.startswith("CH2 too small") for c in cav)
    # the scope's rounding (bode_ui.c bu_fmt_f3)
    for hz, t in ((540e3, "540 kHz"), (541.317e3, "541 kHz"), (54.1317e3, "54.1 kHz"), (5.41317e3, "5.41 kHz"),
                  (999.499e3, "999 kHz"), (999.5e3, "1.00 MHz"), (1.234567e6, "1.23 MHz"), (10e6, "10.0 MHz"),
                  (980.0, "980 Hz"), (1.0, "1.00 Hz"), (9.995e3, "10.0 kHz")):
        assert B.fmt_f3(hz) == t, (hz, B.fmt_f3(hz))


def test_markers_resonance_q():
    f0, q = 50e3, 10.0

    def h(f):
        x = f / f0
        return 1 / (1 - x * x + 1j * x / q)

    pts = [_result(f, h(B.plan_point(f).f_actual)) for f in np.geomspace(20e3, 120e3, 100)]
    mk = B.markers(pts)
    assert mk["shape"] == "resonant"
    assert mk["peak"]["f_hz"] == pytest.approx(f0, rel=0.01)
    assert mk["peak"]["q"] == pytest.approx(q, rel=0.03)
    assert mk["peak"]["mag_db"] == pytest.approx(20 * math.log10(q), abs=0.05)


def test_result_table_csv_json(tmp_path):
    pts = [_result(f, _rc(f)) for f in (1e3, 1e4, 1e5)]
    pts[2].flags.append("near_floor")
    res = B.SweepResult(pts, 10.0, "auto", started="2026-10-19T10:00:00")
    rows = res.table()
    assert set(rows[0]) >= {"f_requested", "f_actual", "mag_db", "phase_deg", "flags", "ch2_volts_per_div",
                            "sample_rate", "filter", "fir", "snr_db"}
    assert rows[1]["mag_db"] == pytest.approx(-3.01, abs=0.05)
    text = Path(res.write_csv(tmp_path / "s.csv")).read_text().splitlines()
    assert text[0].startswith("f_requested,f_actual,mag_db") and text[3].endswith("near_floor")
    j = json.loads(Path(res.write_json(tmp_path / "s.json")).read_text())
    assert j["table"] == json.loads(json.dumps(rows)) and j["summary"]["valid_points"] == 2
    assert res.arrays()["h"].dtype == complex


def test_thru_cal_apply_save_load(tmp_path):
    mismatch = 0.97 * np.exp(-1j * 0.01)
    thru = B.SweepResult([_result(f, mismatch) for f in (1e3, 1e4)], 10.0, "auto", role="thru")
    cal = B.ThruCal.from_sweep(thru, "t1", (1.0, 1.0))
    cal2 = B.ThruCal.load(cal.save(tmp_path / "t1.json"))
    assert cal2.points == cal.points and cal2.ch2_range == cal.ch2_range
    p = _result(1e4, 0.5 * mismatch)
    cal2.apply(p)
    assert p.h == pytest.approx(0.5) and p.flags == []
    q = _result(1e5, 0.5)
    cal2.apply(q)
    assert q.flags == ["no_cal"] and q.h == 0.5
    r = _result(1e3, 0.5)
    r.ch2_range = 5
    cal2.apply(r)
    assert r.flags == ["cal_range"]
    with pytest.raises(ValueError):
        (tmp_path / "x.json").write_text("{}")
        B.ThruCal.load(tmp_path / "x.json")
