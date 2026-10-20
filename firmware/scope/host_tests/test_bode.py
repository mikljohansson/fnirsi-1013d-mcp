#!/usr/bin/env python3
"""Host test of the device Bode maths (firmware/scope/bode_dsp.c via build/bode_cli) against the host tool
(mcp/src/fnirsi1013d/bode.py, accepted on the bench 2026-10-19) and its DUT model (simulator.dut_response).

  python3 -I test_bode.py --cli build/bode_cli [--vectors ../../../docs/bode-plan-vectors.json] [-v]

Checks:
  - the plan vectors (docs/bode-plan-vectors.json): every point and sweep row, all fields
  - 3000 random frequencies and 300 random sweeps: the C planner equals plan_point / plan_sweep (M, N, p / q, record,
    increment, window, f in mHz)
  - log2 / exp2 helpers against math
  - measure_pair on records of the simulator's RC (8-bit quantised with noise, CIC-like 16-bit too, legacy and Hann
    points): H within 0.005 dB / 0.03 deg of the host, levels, noise, leakage, the half-record check and its verdict
  - an unsettled record (a decaying transient on CH2) is flagged by both
  - markers: -3 dB and phase crossings of an RC, peak and Q of a resonance, against the analytic values (and the host's
    markers() when scipy is installed)
Needs numpy (the host tool imports nothing else for these parts).
"""
import argparse
import json
import math
import os
import random
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..", "mcp", "src")))

from fnirsi1013d import bode as B  # noqa: E402
from fnirsi1013d.simulator import dut_response  # noqa: E402

fails = []
verbose = False


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL", msg)
    elif verbose:
        print("ok  ", msg)


class Cli:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def ask(self, line, lines=1):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return [self.p.stdout.readline().strip() for _ in range(lines)]

    def plan(self, f_mhz):
        return self.ask(f"P {f_mhz}")[0]

    def sweep(self, a, b, n):
        k = int(self.ask(f"S {a} {b} {n}")[0])
        return [self.p.stdout.readline().strip() for _ in range(k)]

    def close(self):
        self.p.stdin.write("Q\n")
        self.p.stdin.flush()
        self.p.wait(5)


def host_row(pp):
    return f"{pp.m} {pp.n} {pp.divider} {pp.phase_inc} {int(round(pp.f_actual * 1000))} {pp.p} {pp.q} {pp.record} " \
           f"{pp.total_samples} {1 if pp.window == 'hann' else 0}"


def vec_row(r):
    return [r["m"], r["n"], r["divider"], r["phase_inc"], r["f_mhz"], r["p"], r["q"], r["record"], r["total"]]


def c_fields(line):
    return [int(x) for x in line.split()]


def test_vectors(cli, path):
    with open(path, encoding="utf-8") as f:
        v = json.load(f)
    for r in v["points"]:
        got = c_fields(cli.plan(int(round(r["f_req"] * 1000))))[:9]
        check(got == vec_row(r), f"vector point {r['f_req']} Hz: C {got} json {vec_row(r)}")
    for s in v["sweeps"]:
        rows = cli.sweep(int(round(s["start"] * 1000)), int(round(s["stop"] * 1000)), s["points"])
        check(len(rows) == len(s["plan"]), f"vector sweep {s['start']}-{s['stop']}/{s['points']}: {len(rows)} points, "
                                           f"json {len(s['plan'])}")
        for i, (a, r) in enumerate(zip(rows, s["plan"])):
            check(c_fields(a)[:9] == vec_row(r), f"vector sweep {s['start']}-{s['stop']} point {i}: {a} vs {vec_row(r)}")


def f_mhz_tie_ok(c, h, pp):
    """f in mHz: the host rounds a float product; the exact value inc x 97656250 / 2^23 may sit within the float error
    of a half. Accept a 1 mHz difference only there."""
    if c == h:
        return True
    exact = pp.phase_inc * 97656250 / 2 ** 23
    return abs(c - h) == 1 and abs((exact % 1) - 0.5) < 1e-5


def test_random_plans(cli, n_points=3000, n_sweeps=300):
    rnd = random.Random(1013)
    bad = 0
    for _ in range(n_points):
        f_mhz = int(round(10 ** rnd.uniform(3, 10)))
        pp = B.plan_point(f_mhz / 1000)
        c = c_fields(cli.plan(f_mhz))
        h = c_fields(host_row(pp))
        if c[:4] + c[5:] != h[:4] + h[5:] or not f_mhz_tie_ok(c[4], h[4], pp):
            bad += 1
            if bad < 10:
                print(f"  plan mismatch {f_mhz} mHz: C {c} host {h}")
    check(bad == 0, f"random plan points: {bad} of {n_points} differ")
    bad = 0
    for _ in range(n_sweeps):
        a = int(round(10 ** rnd.uniform(3, 10)))
        b = int(round(10 ** rnd.uniform(3, 10)))
        n = rnd.choice([1, 2, 5, 10, 20, 37, 50, 100, 200])
        host = [pp.m for pp in B.plan_sweep(a / 1000, b / 1000, n)]
        c = [c_fields(x)[0] for x in cli.sweep(a, b, n)]
        if host != c:
            bad += 1
            if bad < 10:
                print(f"  sweep mismatch {a}-{b} mHz / {n}: C {c[:8]}... host {host[:8]}...")
    check(bad == 0, f"random sweeps: {bad} of {n_sweeps} differ")


def test_log_exp(cli):
    worst = 0.0
    for v in [1, 2, 3, 10, 1000, 1700, 50000000000, 10000000000, 2 ** 40 + 12345, 999999937]:
        got = int(cli.ask(f"L {v}")[0]) / 2 ** 32
        worst = max(worst, abs(got - math.log2(v)))
    check(worst < 1e-9, f"log2_q32 worst error {worst:.2e}")
    worst = 0.0
    for y in [0.0, 0.5, 1.0, 10.25, 25.7, 33.2, 45.9]:
        got = int(cli.ask(f"E {int(round(y * 2 ** 32))}")[0])
        want = round(2 ** (round(y * 2 ** 32) / 2 ** 32))
        worst = max(worst, abs(got - want) / want)
    check(worst < 1e-12, f"exp2_round worst relative error {worst:.2e}")


# --- measurement --------------------------------------------------------------------------------------------------

RS, R, C = 1000.0, 1600.0, 10e-9
VPL = {1: 0.0857, 2: 0.0343, 3: 0.0172, 4: 0.00686, 5: 0.00343}


def best_range(vpk):
    r = 1
    for cand in range(1, 6):
        if vpk / VPL[cand] <= 0.85 * 128:
            r = cand
    return r


def codes_of(v, vpl, rnd, sigma=0.7):
    x = v / vpl + 128.0 + rnd.normal(0.0, sigma, v.size)
    return np.clip(np.round(x), 0, 255)


def write_u16(path, a):
    np.asarray(a, "<u2").tofile(path)


def run_measure(cli, tmp, pp, raw1, raw2, cpr, vpl1, vpl2):
    f1, f2 = os.path.join(tmp, "a.bin"), os.path.join(tmp, "b.bin")
    write_u16(f1, raw1)
    write_u16(f2, raw2)
    f_mhz = int(round(pp.f_requested * 1000))
    v1, v2 = (int(round(20 * math.log10(v) * 1000)) for v in (vpl1, vpl2))
    out = cli.ask(f"M {f_mhz} {f1} {f2} {cpr} {cpr} 0 0 {v1} {v2}")[0]
    if out == "error":
        return None
    k = [int(x) for x in out.split()]
    names = ("h_mdb h_mdeg lvl1 lvl2 noise1 noise2 leak snr2 half_ok half_mdb half_mdeg half_rel_ppm unsettled ac1 ac2 "
             "cen1 cen2 clip1 clip2 code1 code2 ph1 std1 std2 flags suspect").split()
    return dict(zip(names, k))


def measure_case(cli, tmp, f, rnd, wide=False, transient=None, label=""):
    pp = B.plan_point(f)
    L = pp.record
    t = np.arange(L) / pp.sample_rate
    a = dut_response(t, pp.f_actual, 0.5, 3.1, RS, R, 0.0, C, node=0)
    b = dut_response(t, pp.f_actual, 0.5, 3.1, RS, R, 0.0, C, node=1)
    a = a - a.mean()
    b = b - b.mean()
    if transient is not None:
        b = b + transient(t, pp)
    r1 = best_range(np.max(np.abs(a)))
    r2 = best_range(np.max(np.abs(b)))
    vpl1, vpl2 = VPL[r1], VPL[r2]
    if wide:
        # a 16-bit record: the codes with 8 fraction bits (as the CIC output with its noise averaged down)
        sig = 0.7 / math.sqrt(max(1, pp.n))
        sig_eff = math.sqrt(sig ** 2 + (1 / 256) ** 2 / 12)
        k1 = np.clip(a / vpl1 + 128 + rnd.normal(0, sig, L), 0, 255)
        k2 = np.clip(b / vpl2 + 128 + rnd.normal(0, sig, L), 0, 255)
        raw1, raw2 = np.round(k1 * 256), np.round(k2 * 256)
        c1, c2 = raw1 / 256, raw2 / 256
    else:
        sig_eff = math.sqrt(0.7 ** 2 + 1 / 12)
        c1, c2 = codes_of(a, vpl1, rnd), codes_of(b, vpl2, rnd)
        raw1, raw2 = c1 * 256, c2 * 256
    vv1, vv2 = (c1 - 128) * vpl1, (c2 - 128) * vpl2
    ms = B.measure_pair(vv1, vv2, pp, vpl1=vpl1, vpl2=vpl2, codes1=c1, codes2=c2)
    k = run_measure(cli, tmp, pp, raw1, raw2, 65536, vpl1, vpl2)
    if k is None:
        check(False, f"{label} {f:g} Hz: C measurement refused")
        return None, None, pp
    hd = B.db(abs(ms.h))
    hp = B.wrap_deg(math.degrees(np.angle(ms.h)))
    d_db = k["h_mdb"] / 1000 - hd
    d_deg = ((k["h_mdeg"] / 1000 - hp) + 180) % 360 - 180
    tag = f"{label} {f:g} Hz (M {pp.m}, N {pp.n}, {pp.window}, CH2 range {r2})"
    check(abs(d_db) < 0.005, f"{tag}: |H| C {k['h_mdb'] / 1000:.4f} host {hd:.4f} dB")
    check(abs(d_deg) < 0.03, f"{tag}: arg H C {k['h_mdeg'] / 1000:.3f} host {hp:.3f} deg")
    check(abs(k["lvl1"] / 1000 - B.db(abs(ms.x1))) < 0.005, f"{tag}: V1 C {k['lvl1'] / 1000:.4f} host {B.db(abs(ms.x1)):.4f} dB")
    check(abs(k["code2"] / 1000 - B.db(ms.codes2)) < 0.005, f"{tag}: CH2 codes C {k['code2'] / 1000:.3f} host "
                                                           f"{B.db(ms.codes2):.3f} dB")
    # noise: the device's period-difference median against the known noise (the host's FFT median reads the leakage of
    # a record that is not quite coherent at low f as noise, and is not robust in time; both are used only for flags)
    n2h = B.db(ms.noise2)
    n2t = B.db(2 * sig_eff / math.sqrt(L) * (1.5 ** 0.5 if pp.window == "hann" else 1) * vpl2)
    if transient is None:
        check(abs(k["noise2"] / 1000 - n2t) < 1.0, f"{tag}: CH2 noise C {k['noise2'] / 1000:.2f} known {n2t:.2f} dB "
                                                    f"(host {n2h:.2f})")
    if ms.leak_db > -90:
        check(abs(k["leak"] / 1000 - ms.leak_db) < 3.0 or (ms.leak_db < -60 and k["leak"] < -57000),
              f"{tag}: leakage C {k['leak'] / 1000:.1f} host {ms.leak_db:.1f} dB")
    check((k["half_ok"] == 1) == (ms.half_db is not None), f"{tag}: halves compared C {k['half_ok']} host "
                                                          f"{ms.half_db is not None}")
    if ms.half_db is not None:
        check(abs(k["half_mdb"] / 1000 - ms.half_db) < 0.01, f"{tag}: half dB C {k['half_mdb'] / 1000:.4f} host {ms.half_db:.4f}")
        check(abs(k["half_mdeg"] / 1000 - ms.half_deg) < 0.05, f"{tag}: half deg C {k['half_mdeg'] / 1000:.3f} host {ms.half_deg:.3f}")
        check(abs(k["half_rel_ppm"] / 1e6 - ms.half_rel) < 2e-4 + 0.01 * ms.half_rel,
              f"{tag}: half rel C {k['half_rel_ppm'] / 1e6:.6f} host {ms.half_rel:.6f}")
    check(bool(k["unsettled"]) == ms.unsettled, f"{tag}: unsettled C {k['unsettled']} host {ms.unsettled}")
    check(abs(k["ac2"] / 100 - ms.ac2) < 0.6, f"{tag}: CH2 excursion C {k['ac2'] / 100:.2f} host {ms.ac2:.2f} codes")
    check(bool(k["clip2"]) == bool(np.any((c2 <= 0) | (c2 >= 255))), f"{tag}: CH2 clip flag {k['clip2']}")
    # the DUT itself (analytic H; quantisation and noise on 8-bit records)
    w = 2j * math.pi * pp.f_actual
    ha = 1.0 / (1.0 + w * R * C)
    tol_db = 0.05 if B.db(ms.codes2) > 20 else 0.3
    check(transient is not None or abs(k["h_mdb"] / 1000 - B.db(abs(ha))) < tol_db,
          f"{tag}: |H| C {k['h_mdb'] / 1000:.3f} analytic {B.db(abs(ha)):.3f} dB")
    if verbose:
        print(f"     {tag}: C {k['h_mdb'] / 1000:+.4f} dB {k['h_mdeg'] / 1000:+.3f} deg, host {hd:+.4f} {hp:+.3f}, "
              f"noise2 {k['noise2'] / 1000:.1f}/{n2h:.1f}/{n2t:.1f}, leak {k['leak'] / 1000:.1f}/{ms.leak_db:.1f}")
    return k, ms, pp


def test_measure(cli):
    rnd = np.random.default_rng(42)
    with tempfile.TemporaryDirectory() as tmp:
        for f in (2.0, 20.0, 100.0, 1000.0, 3000.0, 9947.0, 30e3, 100e3, 300e3, 1e6, 2e6, 5e6):
            measure_case(cli, tmp, f, rnd, label="8-bit")
        for f in (50.0, 1000.0, 10e3, 100e3, 1e6):
            measure_case(cli, tmp, f, rnd, wide=True, label="16-bit")

        # a decaying transient on CH2 (a resonance still ringing): both flag the point
        def ring(t, pp):
            return 0.3 * np.exp(-t * pp.f_actual / 2.0) * np.cos(2 * math.pi * pp.f_actual * 1.37 * t)

        k, ms, pp = measure_case(cli, tmp, 10e3, rnd, transient=ring, label="ringing")
        if k is not None:
            check(ms.unsettled and k["unsettled"] == 1, f"ringing 10 kHz: unsettled host {ms.unsettled} C {k['unsettled']}")

        # one-channel level (the background capture)
        pp = B.plan_point(1000.0)
        t = np.arange(pp.record) / pp.sample_rate
        x = np.round((128 + 20 * np.cos(2 * math.pi * pp.f_actual * t + 0.3)) * 256)
        f1 = os.path.join(tmp, "c.bin")
        write_u16(f1, x)
        got = int(cli.ask(f"B 1000000 {f1}")[0]) / 1000
        want = B.db(abs(B.tone(x, pp.f_actual, pp.sample_rate, pp.window)))
        check(abs(got - want) < 0.005, f"background level C {got:.4f} host {want:.4f} dB (raw units)")


# --- markers -------------------------------------------------------------------------------------------------------

def run_markers(cli, f_mhz, mag_db, ph_deg, ok):
    parts = [str(len(f_mhz))]
    for f, m, p, o in zip(f_mhz, mag_db, ph_deg, ok):
        parts += [str(int(f)), str(int(round(m * 1000))), str(int(round(B.wrap_deg(p) * 1000))), str(int(o))]
    k = [int(x) for x in cli.ask("K " + " ".join(parts))[0].split()]
    names = "shape valid n3db f3a f3b ref have_peak peak_mhz peak_mdb have_q q ph45 phm45 ph90 phm90 phm135 phm180".split()
    return dict(zip(names, k))


def host_markers(plans, h):
    try:
        import scipy  # noqa: F401
    except ImportError:
        return None
    pts = [B.PointResult(pp, hh, hh, 1 + 0j, hh, 3, 3, 0.5, 0.5, 60.0, -80.0, 100.0, 100.0 * abs(hh), "cic3", True)
           for pp, hh in zip(plans, h)]
    return B.markers(pts)


def test_markers(cli):
    # RC low-pass 1.6 kOhm / 10 nF, 100 Hz - 1 MHz, 50 points: -3 dB and -45 deg at 9.947 kHz
    plans = B.plan_sweep(100.0, 1e6, 50)
    f = np.array([pp.f_actual for pp in plans])
    h = 1.0 / (1.0 + 2j * np.pi * f * R * C)
    k = run_markers(cli, [round(x * 1000) for x in f], 20 * np.log10(np.abs(h)), np.degrees(np.angle(h)), [1] * len(f))
    fc = 1 / (2 * math.pi * R * C)
    check(k["shape"] == 1, f"RC markers: shape {k['shape']} (1 = low-pass)")
    check(k["n3db"] == 1 and abs(k["f3a"] / 1000 / fc - 1) < 0.01, f"RC -3 dB {k['f3a'] / 1000:.1f} Hz, analytic {fc:.1f}")
    check(abs(k["phm45"] / 1000 / fc - 1) < 0.01, f"RC -45 deg {k['phm45'] / 1000:.1f} Hz, analytic {fc:.1f}")
    check(k["ph45"] == 0 and k["phm90"] == 0, "RC: no +45 / -90 deg crossing")
    hm = host_markers(plans, h)
    if hm is not None:
        check(abs(k["f3a"] / 1000 / hm["minus_3db_hz"][0] - 1) < 0.003,
              f"RC -3 dB C {k['f3a'] / 1000:.1f} host {hm['minus_3db_hz'][0]:.1f} Hz")
    # one flagged point is skipped
    ok = [1] * len(f)
    ok[20] = 0
    k2 = run_markers(cli, [round(x * 1000) for x in f], 20 * np.log10(np.abs(h)), np.degrees(np.angle(h)), ok)
    check(k2["valid"] == len(f) - 1, f"flagged point skipped: valid {k2['valid']}")

    # series RLC output across C (1 kOhm source, 10 Ohm, 1 mH, 10 nF): f0 50.33 kHz, Q 31.6 / (1 + ...)
    rr, ll, cc = 10.0, 1e-3, 10e-9
    plans = B.plan_sweep(30e3, 80e3, 100)
    f = np.array([pp.f_actual for pp in plans])
    w = 2j * np.pi * f
    h = 1.0 / (1.0 + w * rr * cc + w * w * ll * cc)
    k = run_markers(cli, [round(x * 1000) for x in f], 20 * np.log10(np.abs(h)), np.degrees(np.angle(h)), [1] * len(f))
    fd = np.geomspace(30e3, 80e3, 200001)
    hd = np.abs(1.0 / (1.0 + 2j * np.pi * fd * rr * cc - (2 * np.pi * fd) ** 2 * ll * cc))
    i = int(np.argmax(hd))
    lvl = 20 * np.log10(hd[i]) - 3
    db = 20 * np.log10(hd)
    lo = fd[:i][db[:i] < lvl][-1]
    hi = fd[i:][db[i:] < lvl][0]
    q = fd[i] / (hi - lo)
    check(k["shape"] == 4 and k["have_peak"], f"RLC markers: shape {k['shape']} (4 = resonant)")
    check(abs(k["peak_mhz"] / 1000 / fd[i] - 1) < 0.003, f"RLC peak {k['peak_mhz'] / 1000:.1f} Hz, analytic {fd[i]:.1f}")
    check(abs(k["peak_mdb"] / 1000 - db[i]) < 0.3, f"RLC peak level {k['peak_mdb'] / 1000:.2f} dB, analytic {db[i]:.2f}")
    check(k["have_q"] and abs(k["q"] / 100 / q - 1) < 0.03, f"RLC Q {k['q'] / 100:.2f}, analytic {q:.2f}")
    check(abs(k["phm90"] / 1000 / fd[i] - 1) < 0.005, f"RLC -90 deg {k['phm90'] / 1000:.1f} Hz, peak {fd[i]:.1f}")
    hm = host_markers(plans, h)
    if hm is not None and "peak" in hm:
        check(abs(k["peak_mhz"] / 1000 / hm["peak"]["f_hz"] - 1) < 0.002,
              f"RLC peak C {k['peak_mhz'] / 1000:.1f} host {hm['peak']['f_hz']:.1f} Hz")


# --- flags (fw 0.2.87) -----------------------------------------------------------------------------------------------
# The measurement's own flags of the C (bode_meas_flags) against the host's BodeRunner._result on the same records: the
# bench of 2026-10-20 12:00 saw the device flag points the host did not. Records as the simulator's DUT makes them
# (dut_response: the post's square through 1 kOhm into the RC), quantised to 8-bit codes (legacy) or 16-bit words
# (decimated), plus the disturbances each flag exists for. A flag is compared where the host's number is not within the
# estimators' own spread of its threshold (low_snr and the settle verdict use the device's period-difference noise,
# bode_dsp.h: equal for white noise within 1 dB).

FLAG_BITS = {"ch1_clipped": 0x0001, "clipped": 0x0002, "ch2_overload": 0x0004, "ch1_low": 0x0008, "ch2_small": 0x0010,
             "low_snr": 0x0020, "unsettled": 0x0080, "incoherent": 0x0100}


def host_flags(ms):
    """bode.py BodeRunner._result's flags of the measurement (fir_off, near_floor and spliced need the runner)."""
    f = set()
    if ms.clipped1:
        f.add("ch1_clipped")
    if ms.clipped2:
        f.add("clipped")
    if ms.flat2:
        f.add("ch2_overload")
    if not ms.codes1 >= B.CH1_LOW_CODES:
        f.add("ch1_low")
    if ms.codes2 < B.CH2_SMALL_CODES:
        f.add("ch2_small")
    if ms.snr2_db < B.LOW_SNR_DB:
        f.add("low_snr")
    if ms.leak_db > B.LEAK_LIMIT_DB or ms.ch1_dead:
        f.add("incoherent")
    if ms.unsettled:
        f.add("unsettled")
    return f


def near_threshold(ms):
    """Flags whose host number sits within the estimators' spread of the threshold (not compared)."""
    skip = set()
    if abs(ms.codes1 / B.CH1_LOW_CODES - 1) < 0.01:
        skip.add("ch1_low")
    if abs(ms.codes2 / B.CH2_SMALL_CODES - 1) < 0.01:
        skip.add("ch2_small")
    if abs(ms.snr2_db - B.LOW_SNR_DB) < 3.0:
        skip.add("low_snr")
    if not ms.ch1_dead and abs(ms.leak_db - B.LEAK_LIMIT_DB) < 3.0:
        skip.add("incoherent")
    if ms.half_rel is not None and ms.x1 and ms.x2:
        nr = math.sqrt(2) * math.hypot(ms.noise1 / abs(ms.x1), ms.noise2 / abs(ms.x2))
        ratio = ms.half_rel / (4 * math.sqrt(2) * nr) if nr > 0 else math.inf
        if (0.5 < ratio < 2.0 or abs(abs(ms.half_db) - B.SETTLE_DB) < 0.02 or
                abs(abs(ms.half_deg) - B.SETTLE_DEG) < 0.2):
            skip.add("unsettled")
    return skip


def flag_case(cli, tmp, rnd, pp, a, b, r1, r2, wide, label, flat=(False, False)):
    """a, b: volts at the tips (None: that channel's record is one constant code). Returns (host set, C set)."""
    L = pp.record
    vpl1, vpl2 = VPL[r1], VPL[r2]
    codes = []
    for v, vpl, fl in ((a, vpl1, flat[0]), (b, vpl2, flat[1])):
        if fl:
            c = np.full(L, 255.0)
        elif wide:
            sig = 0.7 / math.sqrt(max(1, pp.n))
            c = np.clip(v / vpl + 128 + rnd.normal(0, sig, L), 0, 255)
            c = np.round(c * 256) / 256
        else:
            c = codes_of(v, vpl, rnd)
        codes.append(c)
    c1, c2 = codes
    vv1, vv2 = (c1 - 128) * vpl1, (c2 - 128) * vpl2
    ms = B.measure_pair(vv1, vv2, pp, vpl1=vpl1, vpl2=vpl2, clipped1=(c1 <= 0) | (c1 >= 255),
                        clipped2=(c2 <= 0) | (c2 >= 255), codes1=c1, codes2=c2)
    k = run_measure(cli, tmp, pp, np.round(c1 * 256), np.round(c2 * 256), 65536, vpl1, vpl2)
    if k is None:
        check(False, f"flags {label}: C measurement refused")
        return set(), set()
    hf = host_flags(ms)
    cf = {n for n, bit in FLAG_BITS.items() if k["flags"] & bit}
    skip = near_threshold(ms)
    tag = (f"flags {label} {pp.f_actual:.6g} Hz (N {pp.n}, {pp.window}, r1 {r1} r2 {r2}): host {sorted(hf)} C {sorted(cf)}"
           f" (codes1 {ms.codes1:.1f} codes2 {ms.codes2:.2f} snr {ms.snr2_db:.1f}/{k['snr2'] / 1000:.1f} leak "
           f"{ms.leak_db:.1f}/{k['leak'] / 1000:.1f} dB)")
    check((hf ^ cf) <= skip, tag)
    check(bool(k["suspect"]) == ms.suspect, f"{label} {pp.f_actual:.6g} Hz: suspect C {k['suspect']} host {ms.suspect}")
    if not flat[0] and not flat[1]:
        for c, sd in ((1, ms.std1), (2, ms.std2)):
            got = 10 ** (k[f"std{c}"] / 20000)
            check(abs(got / sd - 1) < 0.005, f"{label} {pp.f_actual:.6g} Hz: std{c} C {got:.4f} host {sd:.4f} codes")
    if verbose:
        print("     ", tag)
    return hf, cf


def rc_volts(pp, rc_r=1000.0, rc_c=15e-9):
    """The owner's RC (post, 1 kOhm, 15 nF; CH1 on the post side of the 1 kOhm as the DUT input) at the plan's rate."""
    t = np.arange(pp.record) / pp.sample_rate
    a = dut_response(t, pp.f_actual, 0.5, 3.1, RS, rc_r, 0.0, rc_c, node=0)
    b = dut_response(t, pp.f_actual, 0.5, 3.1, RS, rc_r, 0.0, rc_c, node=1)
    return t, a - a.mean() + 0.3, b - b.mean() + 0.3


def test_flags(cli):
    rnd = np.random.default_rng(1205)
    counts = {"host": 0, "c": 0, "points": 0}
    with tempfile.TemporaryDirectory() as tmp:
        # the bench sweep of 2026-10-20 12:05: 100 Hz .. 1 MHz, 25 points; CH1 on 0.5 V/div, CH2 on the host's choice
        # and one range coarser (the device's sweep had CH2 one range up below 10 kHz)
        for pp in B.plan_sweep(100.0, 1e6, 25):
            t, a, b = rc_volts(pp)
            wide = pp.n > 1
            r2 = best_range(np.max(np.abs(b - b.mean())))
            for rr in sorted({r2, max(1, r2 - 1)}):
                hf, cf = flag_case(cli, tmp, rnd, pp, a, b, 3, rr, wide, "RC")
                counts["host"] += bool(hf)
                counts["c"] += bool(cf)
                counts["points"] += 1
        # deep stopband on coarse CH2 ranges: ch2_small / low_snr territory (a smaller C moves the corner down)
        for f in (30e3, 100e3, 300e3, 1e6):
            pp = B.plan_point(f)
            t, a, b = rc_volts(pp, rc_c=150e-9)
            for rr in (2, 3, 4):
                flag_case(cli, tmp, rnd, pp, a, b, 3, rr, pp.n > 1, "stopband")
        # disturbances: an interfering tone beside CH1's fundamental (incoherent), a CH2 transient (unsettled), a DC
        # step in CH1 half way (a front-end transient), clipping, flat records (the 0x153A first-record artefact)
        for f in (1000.0, 10e3, 100e3):
            pp = B.plan_point(f)
            t, a, b = rc_volts(pp)
            df = pp.sample_rate / pp.record * 2.5
            flag_case(cli, tmp, rnd, pp, a + 0.05 * np.cos(2 * math.pi * (pp.f_actual + df) * t), b, 3, 3, pp.n > 1,
                      "beside")
            flag_case(cli, tmp, rnd, pp, a, b + 0.3 * np.exp(-t * pp.f_actual / 3.0) *
                      np.cos(2 * math.pi * pp.f_actual * 1.37 * t), 3, 3, pp.n > 1, "ringing")
            flag_case(cli, tmp, rnd, pp, a + 0.4 * (t > t[-1] / 2), b, 3, 3, pp.n > 1, "CH1 step")
            flag_case(cli, tmp, rnd, pp, a, b, 3, 5, pp.n > 1, "CH2 clipped")
            flag_case(cli, tmp, rnd, pp, a, b, 5, 3, pp.n > 1, "CH1 clipped")
            flag_case(cli, tmp, rnd, pp, a, b, 3, 3, pp.n > 1, "CH1 flat", flat=(True, False))
            flag_case(cli, tmp, rnd, pp, a, b, 3, 3, pp.n > 1, "CH2 flat", flat=(False, True))
            flag_case(cli, tmp, rnd, pp, a * 0.02, b * 0.02, 1, 1, pp.n > 1, "CH1 small")
    print(f"  flags: {counts['points']} RC records, flagged by the host {counts['host']}, by the C {counts['c']}")


def main():
    global verbose
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--vectors", default=os.path.join(HERE, "..", "..", "..", "docs", "bode-plan-vectors.json"))
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    verbose = a.v
    cli = Cli(a.cli)
    try:
        test_vectors(cli, a.vectors)
        test_random_plans(cli)
        test_log_exp(cli)
        test_measure(cli)
        test_markers(cli)
        test_flags(cli)
    finally:
        cli.close()
    print(f"test_bode.py: {len(fails)} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
