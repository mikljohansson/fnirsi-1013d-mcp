"""Host-side signal analysis: statistics, edges, edge-aligned averaging, spectra and decay fits."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Sequence

import numpy as np

# ---------------------------------------------------------------------------------------------------------
# Basic statistics


def stats(v: np.ndarray) -> dict[str, float]:
    v = np.asarray(v, dtype=np.float64)
    if v.size == 0:
        return {}
    mean = float(v.mean())
    return {
        "min": float(v.min()), "max": float(v.max()), "mean": mean, "rms": float(np.sqrt(np.mean(v * v))),
        "ac_rms": float(v.std()), "vpp": float(v.max() - v.min()),
    }


def crossings(v: np.ndarray, level: float, edge: str = "rising", hysteresis: float = 0.0) -> np.ndarray:
    """Fractional sample indices where v crosses level (linear interpolation). With hysteresis the signal must
    have been beyond level -/+ hysteresis since the previous crossing."""
    v = np.asarray(v, dtype=np.float64)
    if v.size < 2:
        return np.empty(0)
    if hysteresis <= 0:
        if edge == "rising":
            idx = np.flatnonzero((v[:-1] < level) & (v[1:] >= level))
        else:
            idx = np.flatnonzero((v[:-1] >= level) & (v[1:] < level))
    else:
        lo, hi = level - hysteresis, level + hysteresis
        out = []
        armed = False
        for i in range(1, v.size):
            if edge == "rising":
                if v[i - 1] < lo:
                    armed = True
                if armed and v[i - 1] < level <= v[i]:
                    out.append(i - 1)
                    armed = False
            else:
                if v[i - 1] > hi:
                    armed = True
                if armed and v[i - 1] >= level > v[i]:
                    out.append(i - 1)
                    armed = False
        idx = np.asarray(out, dtype=np.int64)
    if idx.size == 0:
        return np.empty(0)
    a, b = v[idx], v[idx + 1]
    with np.errstate(divide="ignore", invalid="ignore"):
        frac = np.where(b != a, (level - a) / (b - a), 0.5)
    return idx + frac


def frequency(v: np.ndarray, dt: float) -> dict[str, float | None]:
    """Frequency, period and duty from mean-level crossings with 10 % hysteresis."""
    v = np.asarray(v, dtype=np.float64)
    if v.size < 4:
        return {"frequency": None, "period": None, "duty": None}
    lo, hi = np.percentile(v, 2), np.percentile(v, 98)
    if hi - lo <= 0:
        return {"frequency": None, "period": None, "duty": None}
    mid = 0.5 * (lo + hi)
    hy = 0.1 * (hi - lo)
    r = crossings(v, mid, "rising", hy)
    f = crossings(v, mid, "falling", hy)
    if r.size < 2:
        return {"frequency": None, "period": None, "duty": None}
    period = float(np.mean(np.diff(r))) * dt
    duty = None
    highs = []
    for x in r[:-1]:
        nxt = f[f > x]
        if nxt.size:
            highs.append(nxt[0] - x)
    if highs and period > 0:
        duty = float(np.clip(np.mean(highs) * dt / period, 0, 1))
    return {"frequency": 1.0 / period if period > 0 else None, "period": period, "duty": duty}


def transition_time(v: np.ndarray, dt: float, edge: str = "rising") -> float | None:
    """10-90 % rise (or 90-10 % fall) time of the first full edge."""
    v = np.asarray(v, dtype=np.float64)
    lo, hi = np.percentile(v, 2), np.percentile(v, 98)
    if hi - lo <= 0:
        return None
    l10, l90 = lo + 0.1 * (hi - lo), lo + 0.9 * (hi - lo)
    if edge == "rising":
        a, b = crossings(v, l10, "rising"), crossings(v, l90, "rising")
    else:
        a, b = crossings(v, l90, "falling"), crossings(v, l10, "falling")
    for x in a:
        y = b[b > x]
        if y.size:
            return float((y[0] - x) * dt)
    return None


def delay(v1: np.ndarray, v2: np.ndarray, dt: float) -> float | None:
    """Delay of v2 relative to v1 (positive: v2 later): median over the rising mid-level crossings of v1 of the
    offset to the nearest rising crossing of v2, wrapped to +-half a period for periodic signals."""
    def rising(v: np.ndarray) -> np.ndarray:
        lo, hi = np.percentile(v, 2), np.percentile(v, 98)
        if hi - lo <= 0:
            return np.empty(0)
        return crossings(v, 0.5 * (lo + hi), "rising", 0.1 * (hi - lo))
    a, b = rising(np.asarray(v1, float)), rising(np.asarray(v2, float))
    if a.size == 0 or b.size == 0:
        return None
    d = np.array([b[np.argmin(np.abs(b - x))] - x for x in a])
    if a.size >= 2:
        period = float(np.median(np.diff(a)))
        d = (d + period / 2) % period - period / 2
    return float(np.median(d)) * dt


def preview(t: np.ndarray, v: np.ndarray, points: int = 250) -> dict[str, list[float]]:
    """Decimated min/max envelope with at most 2 x points values."""
    n = v.size
    if n <= 2 * points:
        return {"t": _r(t), "v": _r(v)}
    edges = np.linspace(0, n, points + 1).astype(int)
    tt, vmin, vmax = [], [], []
    for a, b in zip(edges[:-1], edges[1:]):
        seg = v[a:b]
        tt.append(float(t[a]))
        vmin.append(float(seg.min()))
        vmax.append(float(seg.max()))
    return {"t": _r(np.array(tt)), "vmin": _r(np.array(vmin)), "vmax": _r(np.array(vmax))}


def _r(a: np.ndarray, sig: int = 6) -> list[float]:
    return [float(f"{x:.{sig}g}") for x in np.asarray(a, dtype=np.float64)]


# ---------------------------------------------------------------------------------------------------------
# Averaging


@dataclass
class Averager:
    """Running mean / variance of equally long records (Welford)."""

    n: int = 0
    mean: np.ndarray | None = None
    m2: np.ndarray | None = None
    clipped_any: np.ndarray | None = None

    def add(self, x: np.ndarray, clipped: np.ndarray | None = None) -> None:
        x = np.asarray(x, dtype=np.float64)
        if self.mean is None:
            self.mean = np.zeros_like(x)
            self.m2 = np.zeros_like(x)
            self.clipped_any = np.zeros(x.shape, dtype=bool)
        assert self.m2 is not None and self.clipped_any is not None
        self.n += 1
        d = x - self.mean
        self.mean += d / self.n
        self.m2 += d * (x - self.mean)
        if clipped is not None:
            self.clipped_any |= clipped

    @property
    def std(self) -> np.ndarray:
        assert self.m2 is not None
        return np.sqrt(self.m2 / max(self.n - 1, 1))


def shift_fractional(x: np.ndarray, shift: float) -> np.ndarray:
    """y[i] = x[i + shift] by linear interpolation (edge samples held)."""
    idx = np.arange(x.size, dtype=np.float64)
    return np.interp(idx + shift, idx, x)


def find_edge_near(v: np.ndarray, level: float, edge: str, near: float, search: int) -> float | None:
    """Fractional index of the crossing closest to ``near`` within +-search samples."""
    lo = max(0, int(near) - search)
    hi = min(v.size, int(near) + search + 2)
    c = crossings(v[lo:hi], level, edge)
    if c.size == 0:
        return None
    c = c + lo
    return float(c[np.argmin(np.abs(c - near))])


def effective_bits(noise_lsb_single: float, n: int) -> float:
    """Resolution estimate: 8 bits plus log2(sqrt(n)), but only while the noise dithers (>= ~0.5 LSB rms);
    without dither averaging cannot resolve below one code."""
    if n <= 1 or noise_lsb_single < 0.5:
        return 8.0
    return 8.0 + 0.5 * math.log2(n)


# ---------------------------------------------------------------------------------------------------------
# Spectrum


def window(name: str, n: int) -> np.ndarray:
    name = name.lower()
    k = np.arange(n)
    if name in ("rect", "rectangular", "none"):
        return np.ones(n)
    if name == "hann":
        return 0.5 - 0.5 * np.cos(2 * np.pi * k / n)
    if name == "flattop":
        a = (0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368)
    elif name in ("blackman-harris", "blackmanharris"):
        a = (0.35875, 0.48829, 0.14128, 0.01168)
    else:
        raise ValueError(f"unknown window {name!r}; use hann, flattop, blackman-harris or rect")
    w = np.zeros(n)
    for i, c in enumerate(a):
        w += (-1) ** i * c * np.cos(2 * np.pi * i * k / n)
    return w


def amplitude_spectrum(v: np.ndarray, dt: float, window_fn: str = "hann", detrend: bool = True
                       ) -> tuple[np.ndarray, np.ndarray, float]:
    """One-sided RMS amplitude spectrum (volts rms per bin, coherent-gain corrected) and the ENBW in Hz."""
    v = np.asarray(v, dtype=np.float64)
    if detrend:
        v = v - v.mean()
    n = v.size
    w = window(window_fn, n)
    cg = w.sum() / n
    X = np.fft.rfft(v * w) / (n * cg)
    amp = np.abs(X)
    amp[1:] *= 2.0
    amp_rms = amp / math.sqrt(2.0)
    amp_rms[0] = amp[0]
    f = np.fft.rfftfreq(n, dt)
    enbw = float(n * np.sum(w * w) / (w.sum() ** 2)) / (n * dt)
    return f, amp_rms, enbw


def peaks(f: np.ndarray, amp: np.ndarray, count: int = 10, fmin: float = 0.0, fmax: float | None = None,
          min_sep_bins: int = 3, floor: float | None = None) -> list[tuple[float, float, int]]:
    """Local maxima (frequency, amplitude, bin) sorted by amplitude, at least min_sep_bins apart."""
    if fmax is None:
        fmax = float(f[-1])
    sel = (f >= fmin) & (f <= fmax)
    sel[0] = False  # never report DC as a peak
    a = np.where(sel, amp, 0.0)
    cand = np.flatnonzero((a[1:-1] > a[:-2]) & (a[1:-1] >= a[2:])) + 1
    if floor is not None:
        cand = cand[a[cand] > floor]
    cand = cand[np.argsort(a[cand])[::-1]]
    chosen: list[int] = []
    for c in cand:
        if all(abs(int(c) - x) >= min_sep_bins for x in chosen):
            chosen.append(int(c))
        if len(chosen) >= count:
            break
    out = []
    for c in chosen:
        # parabolic interpolation on the log magnitude for a better frequency
        fr = float(f[c])
        if 0 < c < len(a) - 1 and a[c - 1] > 0 and a[c + 1] > 0 and a[c] > 0:
            l, m, r = np.log(a[c - 1]), np.log(a[c]), np.log(a[c + 1])
            den = l - 2 * m + r
            if den != 0:
                fr = float(f[c] + 0.5 * (l - r) / den * (f[1] - f[0]))
        out.append((fr, float(amp[c]), c))
    return out


def scope_fft_db(codes: np.ndarray, zero_code: float, window_fn: str, n: int) -> np.ndarray:
    """Float model of the on-scope spectrum (fx_fft.c, as firmware/scope/host_tests/test_fft.py checks it): the first
    ``n`` trace codes minus ``zero_code``, windowed, one-sided power per bin in dB re 1 code RMS (a sine of amplitude
    A codes centred on a bin reads 20 log10(A / sqrt 2) on every window; DC and Nyquist carry their full value).
    Bins with no power read -inf."""
    x = np.asarray(codes[:n], dtype=np.float64) - zero_code
    if x.size != n:
        raise ValueError(f"scope_fft_db: {x.size} samples, need {n}")
    w = window(window_fn, n)
    p = np.abs(np.fft.rfft(w * x)) ** 2 * 2 / w.sum() ** 2
    p[0] /= 2
    p[n // 2] /= 2
    with np.errstate(divide="ignore"):
        return 10 * np.log10(p)


def compare_spectra(device_db: np.ndarray, host_db: np.ndarray, span_db: float = 60.0) -> dict[str, Any]:
    """Compare two spectra over the same bins (dB, NaN / -inf = empty) on the bins within ``span_db`` of the host
    peak, DC excluded: worst and median level difference (device - host) and both peak bins."""
    d = np.asarray(device_db, dtype=np.float64)
    h = np.asarray(host_db, dtype=np.float64)
    if d.size != h.size or d.size < 3:
        raise ValueError("compare_spectra: spectra of different length")
    hp = int(np.nanargmax(np.where(np.isfinite(h[1:]), h[1:], -np.inf))) + 1
    dp = int(np.nanargmax(np.where(np.isfinite(d[1:]), d[1:], -np.inf))) + 1
    sel = np.isfinite(h) & np.isfinite(d) & (h > h[hp] - span_db)
    sel[0] = False
    diff = d[sel] - h[sel]
    return {"bins_compared": int(sel.sum()), "max_abs_diff_db": float(np.max(np.abs(diff))) if diff.size else None,
            "median_diff_db": float(np.median(diff)) if diff.size else None, "host_peak_bin": hp,
            "device_peak_bin": dp, "host_peak_db": float(h[hp]), "device_peak_db": float(d[dp])}


def db(x: float, ref: float = 1.0) -> float:
    return 20 * math.log10(max(x, 1e-15) / ref)


def thd(f: np.ndarray, amp: np.ndarray, fundamental_bin: int, harmonics: int = 5, halfwidth: int = 3) -> float | None:
    """THD (ratio) using power summed around each harmonic bin."""
    def band(k: int) -> float:
        lo, hi = max(1, k - halfwidth), min(len(amp), k + halfwidth + 1)
        return float(np.sum(amp[lo:hi] ** 2))
    p1 = band(fundamental_bin)
    if p1 <= 0:
        return None
    ph = 0.0
    for h in range(2, harmonics + 1):
        k = fundamental_bin * h
        if k >= len(amp):
            break
        ph += band(k)
    return math.sqrt(ph / p1)


# ---------------------------------------------------------------------------------------------------------
# Decay fits


@dataclass
class FitResult:
    model: str
    params: dict[str, float]
    errors: dict[str, float]
    r2: float
    residual_rms: float
    n: int
    t: np.ndarray = field(repr=False)
    y: np.ndarray = field(repr=False)
    fit: np.ndarray = field(repr=False)
    ok: bool = True
    message: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {"model": self.model, "params": {k: float(f"{v:.6g}") for k, v in self.params.items()},
                "stderr": {k: float(f"{v:.3g}") for k, v in self.errors.items()},
                "r2": float(f"{self.r2:.6f}"), "residual_rms": float(f"{self.residual_rms:.4g}"), "n": self.n,
                "ok": self.ok, "message": self.message}


def _exp(t: np.ndarray, a: float, tau: float) -> np.ndarray:
    return a * np.exp(-t / tau)


def _exp_off(t: np.ndarray, a: float, tau: float, c: float) -> np.ndarray:
    return a * np.exp(-t / tau) + c


def _exp2(t: np.ndarray, a1: float, tau1: float, a2: float, tau2: float, c: float) -> np.ndarray:
    return a1 * np.exp(-t / tau1) + a2 * np.exp(-t / tau2) + c


def _power(t: np.ndarray, a: float, k: float, c: float) -> np.ndarray:
    return a * np.power(t, -k) + c


def _initial_tau(t: np.ndarray, y: np.ndarray, c: float) -> tuple[float, float]:
    """Log-linear estimate of (a, tau) on the part of y - c that is clearly above the noise."""
    z = y - c
    sign = 1.0 if np.median(z[: max(3, z.size // 10)]) >= 0 else -1.0
    z = sign * z
    good = z > max(z.max() * 0.05, 1e-12)
    if good.sum() >= 3:
        p = np.polyfit(t[good], np.log(z[good]), 1)
        tau = -1.0 / p[0] if p[0] < 0 else (t[-1] - t[0]) / 3
        a = sign * math.exp(p[1])
    else:
        tau = (t[-1] - t[0]) / 3
        a = sign * float(np.abs(z).max())
    tau = float(np.clip(tau, (t[1] - t[0]) if t.size > 1 else 1e-9, 1e3 * (t[-1] - t[0] + 1e-12)))
    return a, tau


def fit_decay(t: np.ndarray, y: np.ndarray, model: str = "exp+offset", offset: float | None = None) -> FitResult:
    """Fit y(t) (t = time after the edge, seconds). ``offset`` fixes the baseline (from a baseline window)."""
    import warnings

    from scipy.optimize import OptimizeWarning
    from scipy.optimize import curve_fit as _curve_fit

    def curve_fit(*a: Any, **k: Any) -> Any:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", OptimizeWarning)  # noise-free data: covariance undefined, handled below
            return _curve_fit(*a, **k)

    t = np.asarray(t, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    if t.size < 5:
        raise ValueError(f"only {t.size} samples in the fit window; widen t_start..t_end or raise the sample rate")
    yy = y - offset if offset is not None else y
    span = float(t[-1] - t[0]) or 1e-9
    tail_c = float(np.median(yy[-max(3, yy.size // 10):]))
    try:
        if model == "exp" or (model == "exp+offset" and offset is not None):
            a0, tau0 = _initial_tau(t, yy, 0.0)
            popt, pcov = curve_fit(_exp, t, yy, p0=(a0, tau0), maxfev=20000)
            names, fn = ("amplitude", "tau"), _exp
        elif model == "exp+offset":
            a0, tau0 = _initial_tau(t, yy, tail_c)
            popt, pcov = curve_fit(_exp_off, t, yy, p0=(a0, tau0, tail_c), maxfev=20000)
            names, fn = ("amplitude", "tau", "offset"), _exp_off
        elif model == "exp2":
            a0, tau0 = _initial_tau(t, yy, tail_c)
            p0 = (0.7 * a0, tau0 * 0.5, 0.3 * a0, tau0 * 2.0, tail_c if offset is None else 0.0)
            popt, pcov = curve_fit(_exp2, t, yy, p0=p0, maxfev=40000)
            names, fn = ("amplitude", "tau", "amplitude2", "tau2", "offset"), _exp2
            # order: tau < tau2
            if popt[1] > popt[3]:
                popt = popt[[2, 3, 0, 1, 4]]
                pcov = pcov[np.ix_([2, 3, 0, 1, 4], [2, 3, 0, 1, 4])]
        elif model == "power":
            if np.any(t <= 0):
                raise ValueError("power-law fit needs t_start > 0")
            p0 = (float(yy[0] * t[0]), 1.0, tail_c if offset is None else 0.0)
            popt, pcov = curve_fit(_power, t, yy, p0=p0, maxfev=20000)
            names, fn = ("amplitude", "k", "offset"), _power
        else:
            raise ValueError(f"unknown model {model!r}; use exp, exp+offset, exp2 or power")
    except RuntimeError as e:
        return FitResult(model, {}, {}, float("nan"), float("nan"), int(t.size), t, y, np.full_like(y, np.nan),
                         ok=False, message=f"fit did not converge: {e}")
    fit = fn(t, *popt)
    if offset is not None:
        fit = fit + offset
    res = y - fit
    ss_res = float(np.sum(res ** 2))
    ss_tot = float(np.sum((y - y.mean()) ** 2)) or 1e-30
    perr = np.sqrt(np.clip(np.diag(pcov), 0, None)) if pcov is not None and np.all(np.isfinite(pcov)) else \
        np.full(len(popt), np.nan)
    params = dict(zip(names, (float(p) for p in popt)))
    if offset is not None:
        params["offset"] = float(offset)
    msg = ""
    ok = True
    for key in ("tau", "tau2"):
        if key in params and not (0 < params[key] < 100 * span):
            ok = False
            msg = f"{key} = {params[key]:.3g} s is outside the plausible range for a {span:.3g} s window"
    return FitResult(model, params, dict(zip(names, (float(e) for e in perr))), 1 - ss_res / ss_tot,
                     math.sqrt(ss_res / t.size), int(t.size), t, y, fit, ok, msg)


def blank_after_clip(clipped: np.ndarray, n_blank: int) -> np.ndarray:
    """Boolean mask of samples to exclude: every clipped sample and the n_blank samples after it."""
    clipped = np.asarray(clipped, dtype=bool)
    if n_blank <= 0 or not clipped.any():
        return clipped.copy()
    k = np.ones(n_blank + 1)
    return np.convolve(clipped.astype(float), k)[: clipped.size] > 0


def sample_points(t: np.ndarray, y: np.ndarray, delays: Sequence[float], width: float) -> list[dict[str, float]]:
    """Mean of y over [d, d + width] for each delay, the way PI detectors sample the decay."""
    out = []
    for d in delays:
        sel = (t >= d) & (t <= d + max(width, 0.0))
        if not sel.any():
            i = int(np.argmin(np.abs(t - d)))
            sel = np.zeros_like(t, dtype=bool)
            sel[i] = True
        out.append({"delay": float(d), "width": float(width), "value": float(np.mean(y[sel])),
                    "n": int(sel.sum())})
    return out
