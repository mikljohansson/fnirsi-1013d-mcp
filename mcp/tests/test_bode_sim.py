"""Bode sweep against the simulator: the DUT model (RC / series RLC on the generator) and, from H2 on, the sweep
runner end to end."""

from __future__ import annotations

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
    gen = GenState(1, P.gen_phase_inc(10_000_000), 1 << 31)
    t = np.arange(20000) / 200e6
    assert np.allclose(s_in.eval(t, gen), -1.24 + dut_response(t, gen.frequency_hz, 0.5, 3.1, *RC, node=0))
    slow = GenState(1, P.gen_phase_inc(100_000), 1 << 31)  # 100 Hz: the nodes reach both levels
    v_in = s_in.eval(np.linspace(0, 1e-2, 2001), slow)
    assert v_in.min() == pytest.approx(-1.24, abs=0.01) and v_in.max() == pytest.approx(1.86, abs=0.01)
    assert s_in.dc == pytest.approx(0.31)
    v_out = s_out.eval(t, gen)
    ref = -1.24 + 0.97 * dut_response(t - 5e-9, gen.frequency_hz, 0.5, 3.1, *RC, node=1)
    assert np.allclose(v_out, ref)
    off = GenState(0, gen.phase_inc, gen.duty_threshold)
    assert np.all(s_out.eval(t, off) == -1.24)  # pin tri-stated: the DUT rests at its ground
    with pytest.raises(ValueError, match="dut_in"):
        parse_signal("bode:r=1")
