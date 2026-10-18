"""Host-side analysis on synthetic data (no device)."""

from __future__ import annotations

import math

import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d.units import parse_quantity


def test_parse_quantity():
    assert parse_quantity("100us") == pytest.approx(100e-6)
    assert parse_quantity("10kHz", "Hz") == pytest.approx(1e4)
    assert parse_quantity("20MS/s") == pytest.approx(20e6)
    assert parse_quantity("1.5 V", "V") == 1.5
    assert parse_quantity("-2e-6") == -2e-6
    assert parse_quantity("30%") == pytest.approx(0.3)
    assert parse_quantity(5) == 5.0
    with pytest.raises(ValueError):
        parse_quantity("10kHz", "s")
    with pytest.raises(ValueError):
        parse_quantity("fast")


def test_crossings_interpolate():
    v = np.array([0.0, 1.0, 2.0, 1.0, 0.0])
    assert A.crossings(v, 1.5, "rising").tolist() == [1.5]
    assert A.crossings(v, 1.5, "falling").tolist() == [2.5]


def test_frequency_and_duty():
    dt = 1e-6
    t = np.arange(20000) * dt
    v = np.where(np.mod(t * 1234.0, 1.0) < 0.3, 1.0, 0.0) + np.random.default_rng(1).normal(0, 0.02, t.size)
    fr = A.frequency(v, dt)
    assert fr["frequency"] == pytest.approx(1234.0, rel=1e-3)
    assert fr["duty"] == pytest.approx(0.3, abs=0.01)


def test_shift_fractional():
    x = np.arange(10, dtype=float)
    assert A.shift_fractional(x, 0.5)[2] == pytest.approx(2.5)


@pytest.mark.parametrize("model,params", [("exp", (0.4, 50e-6)), ("exp+offset", (0.4, 50e-6, 0.02))])
def test_fit_decay_models(model, params):
    t = np.linspace(5e-6, 400e-6, 800)
    y = params[0] * np.exp(-t / params[1]) + (params[2] if len(params) > 2 else 0.0)
    y = y + np.random.default_rng(2).normal(0, 0.002, t.size)
    fr = A.fit_decay(t, y, model)
    assert fr.ok and fr.params["tau"] == pytest.approx(params[1], rel=0.02) and fr.r2 > 0.999


def test_fit_exp2_orders_taus():
    t = np.linspace(2e-6, 500e-6, 2000)
    y = 0.5 * np.exp(-t / 8e-6) + 0.2 * np.exp(-t / 120e-6)
    fr = A.fit_decay(t, y, "exp2")
    assert fr.params["tau"] == pytest.approx(8e-6, rel=0.02) and fr.params["tau2"] == pytest.approx(120e-6, rel=0.02)


def test_fit_power_law():
    t = np.linspace(10e-6, 500e-6, 500)
    y = 1e-6 * t ** -1.2
    fr = A.fit_decay(t, y, "power")
    assert fr.params["k"] == pytest.approx(1.2, rel=0.02)


def test_fit_rejects_tiny_window():
    with pytest.raises(ValueError, match="widen"):
        A.fit_decay(np.arange(3.0), np.arange(3.0))


def test_spectrum_amplitude_and_thd():
    dt = 1e-6
    t = np.arange(16384) * dt
    v = 0.5 * np.sin(2 * np.pi * 10e3 * t) + 0.05 * np.sin(2 * np.pi * 30e3 * t)
    f, amp, enbw = A.amplitude_spectrum(v, dt, "flattop")
    pk = A.peaks(f, amp, 3)
    assert pk[0][0] == pytest.approx(10e3, rel=1e-3)
    assert pk[0][1] == pytest.approx(0.5 / math.sqrt(2), rel=0.01)
    assert A.thd(f, amp, pk[0][2], halfwidth=6) == pytest.approx(0.1, rel=0.05)
    assert enbw > 0


def test_blank_after_clip():
    c = np.zeros(10, bool)
    c[2] = True
    assert A.blank_after_clip(c, 3).nonzero()[0].tolist() == [2, 3, 4, 5]


def test_averager_welford():
    rng = np.random.default_rng(3)
    av = A.Averager()
    data = rng.normal(1.0, 2.0, size=(400, 50))
    for row in data:
        av.add(row)
    assert np.allclose(av.mean, data.mean(axis=0)) and np.allclose(av.std, data.std(axis=0, ddof=1))


def test_delay_periodic():
    dt = 1e-7
    t = np.arange(20000) * dt
    v1 = np.sin(2 * np.pi * 1e4 * t)
    v2 = np.sin(2 * np.pi * 1e4 * (t - 3e-6))
    assert A.delay(v1, v2, dt) == pytest.approx(3e-6, abs=2e-7)
